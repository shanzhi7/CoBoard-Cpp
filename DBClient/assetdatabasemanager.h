/***********************************************************************************
* @file         assetdatabasemanager.h
* @brief        图片代理独占的 SQLite 索引、缓存文件和 LRU 容量管理
* @author       shanzhi
* @date         2026/10/04
* @history
***********************************************************************************/
#pragma once

#include "assetserviceprotocol.h"

#include <atomic>
#include <functional>
#include <memory>
#include <QFuture>
#include <QHash>
#include <QMutex>
#include <QSet>
#include <QSqlDatabase>
#include <QThreadPool>
#include <QThreadStorage>

#include "singleton.h"

class ThreadLocalConnection;

class AssetCacheLease
{
public:
    explicit AssetCacheLease(std::function<void()> release); // 保存保护引用释放动作。
    ~AssetCacheLease(); // 最后一个共享引用销毁时解除保护。
private:
    std::function<void()> _release; // 短锁内更新引用计数。
};

class AssetDatabaseManager : public QObject, public Singleton<AssetDatabaseManager>,
                             public std::enable_shared_from_this<AssetDatabaseManager>
{
    Q_OBJECT
    friend class Singleton<AssetDatabaseManager>;
public:
    static constexpr qint64 CACHE_HIGH_WATERMARK_BYTES = 300LL * 1024 * 1024; // 触发水位。
    static constexpr qint64 CACHE_LOW_WATERMARK_BYTES = 100LL * 1024 * 1024; // 目标水位。
    ~AssetDatabaseManager() override; // 在 Qt 应用销毁前释放线程和连接。
    QFuture<AssetCacheResult> InitializeAsync(); // 后台迁移和校准缓存。
    QFuture<AssetCacheResult> LoadAssetAsync(const QString& asset_sha256); // 查询、保护并读取资源。
    QFuture<AssetCacheResult> StoreAssetAsync(const QByteArray& data); // 校验后原子写入。
    QFuture<AssetCacheResult> GetCacheStatsAsync(); // 返回资源数及容量。
    void TouchAssetAsync(const QString& asset_sha256); // 更新已有记录访问时间。
    void ScheduleCleanup(); // 合并清理请求。
    void Shutdown(); // 拒绝新任务并等待后台结束。
private:
    AssetDatabaseManager(); // 只由服务进程创建。
    QSqlDatabase GetThreadLocalConnection(bool is_read_only); // 仅所属线程使用连接。
    bool Execute(QSqlDatabase& database, const QString& sql); // 检查 SQL 错误。
    AssetCacheResult Initialize(); // 初始化 WAL 和缓存索引。
    bool ReconcileCacheDirectory(QSqlDatabase& database); // 恢复目录与索引一致性。
    AssetCacheResult StoreAsset(const QByteArray& data, QSqlDatabase& database); // 写线程内登记资源。
    QSharedPointer<AssetCacheLease> AcquireAssetLease(const QString& asset_sha256); // 取得文件保护。
    qint64 TotalBytes(QSqlDatabase& database); // 统计尚未删除的资源。
    void ExecuteLruCleanup(); // 分批淘汰最旧资源。
    QString _database_path; // 索引文件路径。
    QString _cache_directory; // 服务独占的图片目录。
    QThreadStorage<ThreadLocalConnection*> _thread_connections; // 线程结束移除连接。
    QThreadPool _read_thread_pool; // 两个并行读取线程。
    QThreadPool _write_thread_pool; // 单个写入与清理线程。
    QMutex _task_mutex; // 调度与停止状态保护。
    QMutex _asset_mutex; // 引用计数及淘汰标记保护。
    QHash<QString, int> _asset_use_counts; // 活动引用数量。
    QSet<QString> _evicting_assets; // 已预留的清理候选。
    std::atomic_bool _is_initialized{false}; // 初始化完成状态。
    bool _is_stopping = false; // 停止后拒绝外部任务。
    bool _is_cleanup_scheduled = false; // 清理任务去重。
    bool _needs_cleanup = false; // 写线程维护的水位需求。
};
