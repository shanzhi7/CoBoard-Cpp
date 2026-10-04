#include "assetdatabasemanager.h"

#include <utility>

#include <QDateTime>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPromise>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QtConcurrent>

class ThreadLocalConnection
{
public:
    explicit ThreadLocalConnection(const QString& connection_name) : _connection_name(connection_name) // 保存线程专属名称。
    {
        // 1. 连接由所属线程创建和释放，只保存名称。
    }
    ~ThreadLocalConnection() // 在线程退出时移除 Qt 连接注册。
    {
        // 1. 先销毁最后一份数据库对象，再移除注册项。
        {
            QSqlDatabase database = QSqlDatabase::database(_connection_name, false); // 当前线程连接。
            database.close();
        }
        QSqlDatabase::removeDatabase(_connection_name);
    }
    QString _connection_name; // 不跨线程持有 QSqlDatabase 对象。
};

namespace
{
AssetCacheResult CacheError(const QString& message) // 统一后台失败结果。
{
    // 1. 不把文件内容或外部 URL 放入错误信息。
    AssetCacheResult result; // 失败结果。
    result._error_message = message;
    return result;
}

QFuture<AssetCacheResult> CompletedError(const QString& message) // 为停止后的调用返回已完成 Future。
{
    // 1. 避免在已停止的线程池中提交任务。
    QPromise<AssetCacheResult> promise; // 即时结果。
    promise.start();
    promise.addResult(CacheError(message));
    promise.finish();
    return promise.future();
}
}

AssetCacheLease::AssetCacheLease(std::function<void()> release) : _release(std::move(release))
{
    // 1. 引用释放动作由管理器提供。
}

AssetCacheLease::~AssetCacheLease()
{
    // 1. 最后一个共享持有者释放文件保护。
    _release();
}

AssetDatabaseManager::AssetDatabaseManager()
{
    // 1. 所有 SQL 和缓存文件操作限定在后台线程。
    _database_path = QDir(AssetServiceProtocol::CacheRoot()).filePath("canvas-assets.db");
    _cache_directory = QDir(AssetServiceProtocol::CacheRoot()).filePath("canvas-assets");
    _read_thread_pool.setMaxThreadCount(2);
    _write_thread_pool.setMaxThreadCount(1);
}

AssetDatabaseManager::~AssetDatabaseManager()
{
    // 1. 服务正常停止和异常析构共用退出流程。
    Shutdown();
}

QSqlDatabase AssetDatabaseManager::GetThreadLocalConnection(bool is_read_only)
{
    // 1. 每个线程仅创建一份连接，读写池不交换连接对象。
    if (!_thread_connections.hasLocalData())
    {
        const QString connection_name = "AssetCache_" + AssetServiceProtocol::NewId(); // 全局唯一连接名。
        QSqlDatabase database = QSqlDatabase::addDatabase("QSQLITE", connection_name); // 所属线程连接。
        database.setDatabaseName(_database_path);
        database.setConnectOptions(is_read_only ? "QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000" : "QSQLITE_BUSY_TIMEOUT=5000");
        _thread_connections.setLocalData(new ThreadLocalConnection(connection_name));
        if (!database.open())
        {
            qCritical() << "AssetDatabaseManager Open pid=" << QCoreApplication::applicationPid() << database.lastError().text();
            return database;
        }
        if (!is_read_only)
        {
            QSqlQuery journal_query(database); // 必须验证实际 WAL 模式，而非仅检查 exec。
            const bool has_wal = journal_query.exec("PRAGMA journal_mode=WAL") && journal_query.next()
                && journal_query.value(0).toString().toLower() == "wal"; // 不支持 WAL 时拒绝启动。
            journal_query.finish();
            if (!has_wal || !Execute(database, "PRAGMA synchronous=NORMAL")
                || !Execute(database, "PRAGMA wal_autocheckpoint=1000"))
            {
                qCritical() << "AssetDatabaseManager Configure WAL failed pid=" << QCoreApplication::applicationPid();
                database.close();
                return database;
            }
        }
        qDebug() << "AssetDatabaseManager Connection pid=" << QCoreApplication::applicationPid() << "readOnly=" << is_read_only;
    }
    // 2. 返回值只能在当前线程的任务作用域使用。
    return QSqlDatabase::database(_thread_connections.localData()->_connection_name, false);
}

bool AssetDatabaseManager::Execute(QSqlDatabase& database, const QString& sql)
{
    // 1. SQL 错误必须包含上下文，不静默忽略。
    QSqlQuery query(database); // 当前线程查询。
    if (query.exec(sql))
    {
        return true;
    }
    qWarning() << "AssetDatabaseManager SQL pid=" << QCoreApplication::applicationPid() << query.lastError().text();
    return false;
}

QFuture<AssetCacheResult> AssetDatabaseManager::InitializeAsync()
{
    // 1. 初始化排在所有写入之前。
    QMutexLocker locker(&_task_mutex); // 提交和停止互斥。
    if (_is_stopping)
    {
        return CompletedError("缓存服务正在停止");
    }
    return QtConcurrent::run(&_write_thread_pool, [this] { return Initialize(); });
}

AssetCacheResult AssetDatabaseManager::Initialize()
{
    // 1. 独占锁已由 ServiceCommandServer 持有，才允许创建目录和索引。
    if (!QDir().mkpath(_cache_directory))
    {
        return CacheError("无法创建图片缓存目录");
    }
    QSqlDatabase database = GetThreadLocalConnection(false); // 唯一写连接。
    if (!database.isOpen())
    {
        return CacheError("SQLite 插件不可用或无法打开缓存索引");
    }
    QSqlQuery version_query(database); // 拒绝未来版本，防止降级破坏索引。
    if (!version_query.exec("PRAGMA user_version") || !version_query.next() || version_query.value(0).toInt() > 1)
    {
        return CacheError("缓存数据库版本不兼容");
    }
    version_query.finish();
    if (!Execute(database, "CREATE TABLE IF NOT EXISTS asset_cache (sha256 TEXT PRIMARY KEY, relative_path TEXT NOT NULL, byte_size INTEGER NOT NULL, mime_type TEXT NOT NULL, width INTEGER NOT NULL, height INTEGER NOT NULL, last_access_ms INTEGER NOT NULL, cache_state TEXT NOT NULL)")
        || !Execute(database, "CREATE INDEX IF NOT EXISTS asset_lru ON asset_cache(cache_state,last_access_ms,sha256)")
        || !Execute(database, "CREATE TABLE IF NOT EXISTS cache_meta (key TEXT PRIMARY KEY,value INTEGER NOT NULL)")
        || !Execute(database, "INSERT OR IGNORE INTO cache_meta VALUES('cleanup_requested',0)")
        || !Execute(database, "PRAGMA user_version=1"))
    {
        return CacheError("无法初始化缓存表结构");
    }
    // 2. 迁移旧文件及修复崩溃留下的孤立文件、删除状态。
    if (!ReconcileCacheDirectory(database))
    {
        return CacheError("缓存目录恢复失败，请检查代理日志");
    }
    QSqlQuery query(database); // 恢复跨重启的清理意图。
    if (!query.exec("SELECT value FROM cache_meta WHERE key='cleanup_requested'") || !query.next())
    {
        return CacheError("无法读取缓存维护状态");
    }
    _needs_cleanup = query.value(0).toBool() || TotalBytes(database) > CACHE_HIGH_WATERMARK_BYTES;
    _is_initialized = true;
    AssetCacheResult result; // 就绪结果。
    result._status = "ok";
    result._error_code.clear();
    qDebug() << "AssetDatabaseManager Initialize pid=" << QCoreApplication::applicationPid() << "bytes=" << TotalBytes(database);
    return result;
}

bool AssetDatabaseManager::ReconcileCacheDirectory(QSqlDatabase& database)
{
    // 1. 检查实际文件；恢复不使用旧资源 ID，而以校验后的 SHA-256 去重。
    const QFileInfoList files = QDir(_cache_directory).entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot); // 仅本目录文件。
    QSet<QString> recovered; // 已恢复的规范名称。
    for (const QFileInfo& file_info : files)
    {
        if (file_info.isSymLink())
        {
            if (!QFile::remove(file_info.filePath()))
            {
                return false;
            }
            continue;
        }
        QFile file(file_info.filePath()); // 有界读取旧缓存。
        if (file_info.size() <= 0 || file_info.size() > AssetServiceProtocol::MAX_IMAGE_BYTES)
        {
            if (!QFile::remove(file_info.filePath()))
            {
                return false;
            }
            continue;
        }
        if (!file.open(QIODevice::ReadOnly))
        {
            return false;
        }
        const QByteArray data = file.read(AssetServiceProtocol::MAX_IMAGE_BYTES + 1); // 检查中断写入或损坏图片。
        file.close();
        AssetCacheResult result = AssetServiceProtocol::ValidateImage(data); // 内容决定文件名。
        if (result._status != "ok")
        {
            if (!QFile::remove(file_info.filePath()))
            {
                return false;
            }
            continue;
        }
        const QString canonical_path = QDir(_cache_directory).filePath(result._record._asset_sha256); // 规范路径。
        if (canonical_path != file_info.filePath())
        {
            QSaveFile canonical_file(canonical_path); // 原子迁移，失败保留源文件。
            if (!canonical_file.open(QIODevice::WriteOnly) || canonical_file.write(data) != data.size() || !canonical_file.commit())
            {
                return false;
            }
        }
        QSqlQuery insert(database); // 保留已存在记录的 LRU 时间。
        insert.prepare("INSERT INTO asset_cache VALUES(?,?,?,?,?,?,?, 'ready') ON CONFLICT(sha256) DO UPDATE SET relative_path=excluded.relative_path,byte_size=excluded.byte_size,mime_type=excluded.mime_type,width=excluded.width,height=excluded.height,cache_state='ready'");
        insert.addBindValue(result._record._asset_sha256);
        insert.addBindValue(result._record._asset_sha256);
        insert.addBindValue(result._record._byte_size);
        insert.addBindValue(result._record._mime_type);
        insert.addBindValue(result._record._original_size.width());
        insert.addBindValue(result._record._original_size.height());
        insert.addBindValue(file_info.lastModified().toMSecsSinceEpoch());
        if (!insert.exec())
        {
            qCritical() << "AssetDatabaseManager Reconcile" << insert.lastError().text();
            return false;
        }
        recovered.insert(result._record._asset_sha256);
        if (canonical_path != file_info.filePath() && !QFile::remove(file_info.filePath()))
        {
            return false;
        }
    }
    // 2. 删除已没有文件的索引，避免幽灵命中及错误容量。
    QSqlQuery query(database); // 收集后再修改，不跨查询游标删除。
    if (!query.exec("SELECT sha256 FROM asset_cache"))
    {
        return false;
    }
    QStringList missing; // 待移除记录。
    while (query.next())
    {
        const QString sha256 = query.value(0).toString(); // 索引摘要。
        if (!recovered.contains(sha256))
        {
            missing.append(sha256);
        }
    }
    query.finish();
    for (const QString& sha256 : missing)
    {
        QSqlQuery remove(database); // 参数化删除。
        remove.prepare("DELETE FROM asset_cache WHERE sha256=?");
        remove.addBindValue(sha256);
        if (!remove.exec())
        {
            return false;
        }
    }
    return true;
}

QSharedPointer<AssetCacheLease> AssetDatabaseManager::AcquireAssetLease(const QString& asset_sha256)
{
    // 1. 引用与淘汰预留互斥，锁内不读取文件或 SQL。
    {
        QMutexLocker locker(&_asset_mutex); // 引用保护。
        if (_evicting_assets.contains(asset_sha256))
        {
            return {};
        }
        ++_asset_use_counts[asset_sha256];
    }
    const std::weak_ptr<AssetDatabaseManager> owner = shared_from_this(); // 不延长服务生命周期。
    return QSharedPointer<AssetCacheLease>::create([owner, asset_sha256] {
        if (const auto manager = owner.lock())
        {
            {
                QMutexLocker locker(&manager->_asset_mutex); // 最后引用释放后才能清理。
                if (--manager->_asset_use_counts[asset_sha256] == 0)
                {
                    manager->_asset_use_counts.remove(asset_sha256);
                }
            }
            manager->ScheduleCleanup();
        }
    });
}

QFuture<AssetCacheResult> AssetDatabaseManager::LoadAssetAsync(const QString& asset_sha256)
{
    // 1. 所有路径来自校验后的摘要，外部相对路径不能直接参与读文件。
    QMutexLocker locker(&_task_mutex); // 停止期间拒绝任务。
    if (_is_stopping || !_is_initialized || !AssetServiceProtocol::IsSha256(asset_sha256))
    {
        return CompletedError("缓存未就绪或资源摘要无效");
    }
    return QtConcurrent::run(&_read_thread_pool, [this, asset_sha256] {
        AssetCacheResult result; // 默认未命中。
        result._status = "miss";
        result._error_code = "CacheMiss";
        auto lease = AcquireAssetLease(asset_sha256); // 查询前预留，避免返回已删除文件。
        if (!lease)
        {
            return result;
        }
        QSqlDatabase database = GetThreadLocalConnection(true); // WAL 读取线程连接。
        QSqlQuery query(database); // 仅查规范资源。
        query.prepare("SELECT sha256 FROM asset_cache WHERE sha256=? AND cache_state='ready'");
        query.addBindValue(asset_sha256);
        if (!query.exec())
        {
            return CacheError("读取缓存索引失败");
        }
        if (!query.next())
        {
            return result;
        }
        query.finish();
        // 2. 文件由引用保护，校验实际内容；坏缓存按 miss 处理允许重新下载。
        const QString file_path = QDir(_cache_directory).filePath(asset_sha256); // 安全路径。
        QFile file(file_path); // 有界读取。
        if (QFileInfo(file_path).isSymLink() || !file.open(QIODevice::ReadOnly))
        {
            return result;
        }
        AssetCacheResult validated = AssetServiceProtocol::ValidateImage(file.read(AssetServiceProtocol::MAX_IMAGE_BYTES + 1)); // 校验内容。
        file.close();
        if (validated._status != "ok" || validated._record._asset_sha256 != asset_sha256)
        {
            qWarning() << "AssetDatabaseManager Load invalid sha=" << asset_sha256;
            return result;
        }
        validated._file_path = file_path;
        validated._lease = lease;
        TouchAssetAsync(asset_sha256);
        qDebug() << "AssetDatabaseManager Load hit sha=" << asset_sha256 << "bytes=" << validated._record._byte_size;
        return validated;
    });
}

QFuture<AssetCacheResult> AssetDatabaseManager::StoreAssetAsync(const QByteArray& data)
{
    // 1. 单写线程避免文件提交、元数据登记和清理交叉写入。
    QMutexLocker locker(&_task_mutex); // 安全提交。
    if (_is_stopping || !_is_initialized)
    {
        return CompletedError("缓存未就绪或正在停止");
    }
    return QtConcurrent::run(&_write_thread_pool, [this, data] {
        QSqlDatabase database = GetThreadLocalConnection(false); // 唯一写连接。
        AssetCacheResult result = StoreAsset(data, database); // 登记资源。
        ScheduleCleanup();
        return result;
    });
}

AssetCacheResult AssetDatabaseManager::StoreAsset(const QByteArray& data, QSqlDatabase& database)
{
    // 1. 图片格式、像素尺寸和内容摘要统一从实际数据获得。
    AssetCacheResult result = AssetServiceProtocol::ValidateImage(data); // 可信元数据。
    if (result._status != "ok")
    {
        return result;
    }
    result._lease = AcquireAssetLease(result._record._asset_sha256);
    if (!result._lease)
    {
        return CacheError("资源正在清理，请重试");
    }
    result._file_path = QDir(_cache_directory).filePath(result._record._asset_sha256);
    QSqlQuery existing_record(database); // 索引不可写时不能继续产生未计量的文件。
    existing_record.prepare("SELECT sha256 FROM asset_cache WHERE sha256=?");
    existing_record.addBindValue(result._record._asset_sha256);
    if (!existing_record.exec())
    {
        qCritical() << "AssetDatabaseManager Store lookup" << existing_record.lastError().text();
        return CacheError("无法查询缓存索引");
    }
    const bool was_indexed = existing_record.next(); // 已计量文件不能在登记失败时误删。
    existing_record.finish();
    // 2. 文件先原子提交，再登记索引；崩溃形成的孤立文件在下一次启动恢复。
    QFile existing_file(result._file_path); // 重复导入不能替换正在上传的文件。
    bool is_duplicate = false; // 已缓存相同编码。
    if (!QFileInfo(result._file_path).isSymLink() && existing_file.open(QIODevice::ReadOnly))
    {
        is_duplicate = existing_file.read(AssetServiceProtocol::MAX_IMAGE_BYTES + 1) == data;
        existing_file.close();
    }
    if (!is_duplicate)
    {
        QSaveFile file(result._file_path); // 不跟随外部路径。
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        {
            return CacheError("无法提交缓存文件");
        }
    }
    QSqlQuery query(database); // 摘要去重。
    query.prepare("INSERT INTO asset_cache VALUES(?,?,?,?,?,?,?,'ready') ON CONFLICT(sha256) DO UPDATE SET relative_path=excluded.relative_path,byte_size=excluded.byte_size,mime_type=excluded.mime_type,width=excluded.width,height=excluded.height,last_access_ms=excluded.last_access_ms,cache_state='ready'");
    query.addBindValue(result._record._asset_sha256);
    query.addBindValue(result._record._asset_sha256);
    query.addBindValue(result._record._byte_size);
    query.addBindValue(result._record._mime_type);
    query.addBindValue(result._record._original_size.width());
    query.addBindValue(result._record._original_size.height());
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!query.exec())
    {
        qCritical() << "AssetDatabaseManager Store sha=" << result._record._asset_sha256 << query.lastError().text();
        if (!was_indexed && !QFile::remove(result._file_path))
        {
            // 登记和回收均失败时停止接收资源，避免继续生成未计量文件。
            _is_initialized = false;
            qCritical() << "AssetDatabaseManager Store rollback failed sha=" << result._record._asset_sha256;
        }
        return CacheError("无法登记缓存索引");
    }
    if (TotalBytes(database) > CACHE_HIGH_WATERMARK_BYTES)
    {
        _needs_cleanup = true;
        Execute(database, "UPDATE cache_meta SET value=1 WHERE key='cleanup_requested'");
    }
    qDebug() << "AssetDatabaseManager Store sha=" << result._record._asset_sha256 << "bytes=" << result._record._byte_size;
    return result;
}

void AssetDatabaseManager::TouchAssetAsync(const QString& asset_sha256)
{
    // 1. 更新访问时间只进入写线程，不持有引用锁。
    QMutexLocker locker(&_task_mutex); // 停止检查。
    if (_is_stopping || !_is_initialized || !AssetServiceProtocol::IsSha256(asset_sha256))
    {
        return;
    }
    (void)QtConcurrent::run(&_write_thread_pool, [this, asset_sha256] {
        QSqlDatabase database = GetThreadLocalConnection(false); // 写连接。
        QSqlQuery query(database); // LRU 更新时间。
        query.prepare("UPDATE asset_cache SET last_access_ms=? WHERE sha256=? AND cache_state='ready'");
        query.addBindValue(QDateTime::currentMSecsSinceEpoch());
        query.addBindValue(asset_sha256);
        if (!query.exec())
        {
            qWarning() << "AssetDatabaseManager Touch sha=" << asset_sha256 << query.lastError().text();
        }
    });
}

qint64 AssetDatabaseManager::TotalBytes(QSqlDatabase& database)
{
    // 1. 待删除记录仍计入容量，删除文件成功后才扣减。
    QSqlQuery query(database); // 容量统计。
    if (!query.exec("SELECT COALESCE(SUM(byte_size),0) FROM asset_cache") || !query.next())
    {
        qCritical() << "AssetDatabaseManager TotalBytes" << query.lastError().text();
        return -1;
    }
    return query.value(0).toLongLong();
}

QFuture<AssetCacheResult> AssetDatabaseManager::GetCacheStatsAsync()
{
    // 1. 统计使用独立读连接，避免阻塞服务事件循环。
    QMutexLocker locker(&_task_mutex); // 停止检查。
    if (_is_stopping || !_is_initialized)
    {
        return CompletedError("缓存未就绪");
    }
    return QtConcurrent::run(&_read_thread_pool, [this] {
        QSqlDatabase database = GetThreadLocalConnection(true); // WAL 读连接。
        QSqlQuery query(database); // 一条语句取得一致统计。
        if (!query.exec("SELECT COUNT(*),COALESCE(SUM(byte_size),0),(SELECT value FROM cache_meta WHERE key='cleanup_requested') FROM asset_cache") || !query.next())
        {
            return CacheError("无法获取缓存统计");
        }
        AssetCacheResult result; // 统计结果。
        result._status = "ok";
        result._error_code.clear();
        result._payload = {{"assetCount", query.value(0).toLongLong()}, {"totalBytes", query.value(1).toLongLong()},
            {"needsCleanup", query.value(2).toBool()}, {"highWatermarkBytes", CACHE_HIGH_WATERMARK_BYTES}, {"lowWatermarkBytes", CACHE_LOW_WATERMARK_BYTES}};
        return result;
    });
}

void AssetDatabaseManager::ScheduleCleanup()
{
    // 1. 多次引用释放和维护时钟合并为一个清理任务。
    QMutexLocker locker(&_task_mutex); // 调度状态。
    if (_is_stopping || !_is_initialized || _is_cleanup_scheduled)
    {
        return;
    }
    _is_cleanup_scheduled = true;
    (void)QtConcurrent::run(&_write_thread_pool, [this] {
        ExecuteLruCleanup();
        QMutexLocker locker(&_task_mutex); // 允许下一轮维护。
        _is_cleanup_scheduled = false;
    });
}

void AssetDatabaseManager::ExecuteLruCleanup()
{
    // 1. 超高水位后保持清理需求，受保护资源解除引用后继续。
    QSqlDatabase database = GetThreadLocalConnection(false); // 单写连接。
    const qint64 before_bytes = TotalBytes(database); // 本轮前容量。
    _needs_cleanup = _needs_cleanup || before_bytes > CACHE_HIGH_WATERMARK_BYTES;
    if (!_needs_cleanup || before_bytes < 0)
    {
        return;
    }
    Execute(database, "UPDATE cache_meta SET value=1 WHERE key='cleanup_requested'");
    QSqlQuery query(database); // 先复制顺序，避免删除期间持有游标。
    if (!query.exec("SELECT sha256,byte_size FROM asset_cache ORDER BY last_access_ms,sha256"))
    {
        return;
    }
    QList<QPair<QString, qint64>> candidates; // 候选快照。
    while (query.next())
    {
        candidates.append({query.value(0).toString(), query.value(1).toLongLong()});
    }
    query.finish();
    qint64 total_bytes = before_bytes; // 实际已删容量。
    int removed_count = 0; // 成功删除数。
    int attempted_count = 0; // 包括失败，本批最多处理 32 文件。
    for (const auto& candidate : candidates)
    {
        if (total_bytes < CACHE_LOW_WATERMARK_BYTES || attempted_count >= 32)
        {
            break;
        }
        const QString sha256 = candidate.first; // 淘汰摘要。
        {
            QMutexLocker locker(&_asset_mutex); // 与新引用原子竞争。
            if (_asset_use_counts.value(sha256) > 0)
            {
                continue;
            }
            _evicting_assets.insert(sha256);
        }
        ++attempted_count;
        // 2. 短锁之外登记删除意图，再删除文件和索引。
        QSqlQuery mark(database); // 崩溃恢复标记。
        mark.prepare("UPDATE asset_cache SET cache_state='evicting' WHERE sha256=?");
        mark.addBindValue(sha256);
        const QString path = QDir(_cache_directory).filePath(sha256); // 内容寻址路径。
        const bool is_deleted = AssetServiceProtocol::IsSha256(sha256) && mark.exec()
            && (!QFileInfo::exists(path) || QFile::remove(path)); // 文件缺失也可校准索引。
        QSqlQuery finish(database); // 仅成功删除才扣容量。
        finish.prepare(is_deleted ? "DELETE FROM asset_cache WHERE sha256=?" : "UPDATE asset_cache SET cache_state='ready' WHERE sha256=?");
        finish.addBindValue(sha256);
        if (finish.exec() && is_deleted)
        {
            total_bytes -= candidate.second;
            ++removed_count;
        } else
        {
            qWarning() << "AssetDatabaseManager Cleanup failed sha=" << sha256 << finish.lastError().text();
        }
        {
            QMutexLocker locker(&_asset_mutex); // 删除完成后开放引用竞争。
            _evicting_assets.remove(sha256);
        }
    }
    // 3. 未达到低水位时保留需求，后续维护继续分批处理。
    _needs_cleanup = total_bytes >= CACHE_LOW_WATERMARK_BYTES;
    Execute(database, _needs_cleanup ? "UPDATE cache_meta SET value=1 WHERE key='cleanup_requested'" : "UPDATE cache_meta SET value=0 WHERE key='cleanup_requested'");
    qDebug() << "AssetDatabaseManager Cleanup pid=" << QCoreApplication::applicationPid() << "removed=" << removed_count << "before=" << before_bytes << "after=" << total_bytes << "pending=" << _needs_cleanup;
}

void AssetDatabaseManager::Shutdown()
{
    // 1. 禁止新任务，先回收读线程，避免读取结束后继续提交写任务。
    {
        QMutexLocker locker(&_task_mutex); // 停止状态。
        if (_is_stopping)
        {
            return;
        }
        _is_stopping = true;
    }
    _read_thread_pool.waitForDone();
    _write_thread_pool.waitForDone();
    // 2. 在写线程执行 checkpoint；线程退出时 TLS 析构关闭连接。
    if (_is_initialized)
    {
        auto checkpoint = QtConcurrent::run(&_write_thread_pool, [this] {
            QSqlDatabase database = GetThreadLocalConnection(false); // 所属线程的新连接。
            Execute(database, "PRAGMA wal_checkpoint(TRUNCATE)");
        }); // 最后一个维护任务。
        checkpoint.waitForFinished();
        _write_thread_pool.waitForDone();
    }
    _is_initialized = false;
    qDebug() << "AssetDatabaseManager Shutdown pid=" << QCoreApplication::applicationPid();
}
