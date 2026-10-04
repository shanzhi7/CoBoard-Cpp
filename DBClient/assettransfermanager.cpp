#include "assettransfermanager.h"

#include <utility>

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QtConcurrent>

#include "assetdatabasemanager.h"

namespace
{
AssetCacheResult TransferError(const QString& message, const QString& code = "TransferError") // 保持网络错误不含 URL。
{
    // 1. 网络回复正文可能包含签名信息，禁止作为错误消息返回。
    AssetCacheResult result; // 错误结果。
    result._error_code = code;
    result._error_message = message;
    return result;
}
bool IsTransferUrl(const QUrl& url) // 只允许 HTTP 对象传输。
{
    // 1. 不允许 file 等本地协议绕过缓存管理。
    return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty()
        && (url.scheme() == "https" || url.scheme() == "http");
}
}

AssetTransferManager::AssetTransferManager(QObject* parent) : QObject(parent)
{
    // 1. Qt 网络对象始终归服务事件线程管理。
    _network_manager = new QNetworkAccessManager(this);
    _prepare_thread_pool.setMaxThreadCount(2);
}

void AssetTransferManager::Observe(const QString& job_id, QFuture<AssetCacheResult> future)
{
    // 1. 已取消的请求仍等工作线程结束，防止空闲退出时提前释放数据库。
    ++_active_worker_count;
    auto* watcher = new QFutureWatcher<AssetCacheResult>(this); // 回调在服务线程执行。
    connect(watcher, &QFutureWatcher<AssetCacheResult>::finished, this, [this, watcher, job_id] {
        --_active_worker_count;
        Finish(job_id, watcher->result());
        watcher->deleteLater();
        emit sigActivityChanged();
    });
    watcher->setFuture(future);
}

void AssetTransferManager::Finish(const QString& job_id, AssetCacheResult result)
{
    // 1. 取消或断开的任务只释放结果引用，不发送迟到响应。
    if (_jobs.remove(job_id))
    {
        emit sigFinished(job_id, result);
    }
    emit sigActivityChanged();
}

void AssetTransferManager::LoadAsset(const QString& job_id, const QString& sha256)
{
    // 1. 读取任务的连接与文件保护由数据库管理器提供。
    _jobs.insert(job_id);
    Observe(job_id, AssetDatabaseManager::getInstance()->LoadAssetAsync(sha256));
}

void AssetTransferManager::GetCacheStats(const QString& job_id)
{
    // 1. 不在服务事件线程查询 SQL。
    _jobs.insert(job_id);
    Observe(job_id, AssetDatabaseManager::getInstance()->GetCacheStatsAsync());
}

void AssetTransferManager::PrepareLocalAsset(const QString& job_id, const QString& file_path)
{
    // 1. UI 仅提交原始路径；服务的工作线程读取文件。
    _jobs.insert(job_id);
    Observe(job_id, QtConcurrent::run(&_prepare_thread_pool, [file_path] {
        QFile file(file_path); // 原始图片，不是缓存路径。
        if (!QFileInfo(file_path).isFile() || QFileInfo(file_path).size() > AssetServiceProtocol::MAX_IMAGE_BYTES || !file.open(QIODevice::ReadOnly))
        {
            return TransferError("无法读取图片或图片超过 10 MiB", "InvalidImage");
        }
        const QByteArray data = file.read(AssetServiceProtocol::MAX_IMAGE_BYTES + 1); // 有界读取。
        file.close();
        auto future = AssetDatabaseManager::getInstance()->StoreAssetAsync(data); // 单独写池，没有线程互等。
        future.waitForFinished();
        return future.result();
    }));
}

void AssetTransferManager::PrepareImageData(const QString& job_id, const QByteArray& data)
{
    // 1. 已编码的剪贴板数据直接提交后台校验与登记。
    _jobs.insert(job_id);
    Observe(job_id, AssetDatabaseManager::getInstance()->StoreAssetAsync(data));
}

void AssetTransferManager::DownloadAsset(const QString& job_id, const QString& sha256, const QUrl& url)
{
    // 1. 下载前再次查缓存，同一摘要的不同客户端共享全部流程。
    _jobs.insert(job_id);
    if (!AssetServiceProtocol::IsSha256(sha256) || !IsTransferUrl(url))
    {
        Finish(job_id, TransferError("图片摘要或下载地址无效", "InvalidRequest"));
        return;
    }
    if (_download_contexts.contains(sha256))
    {
        _download_contexts[sha256]->_waiters.insert(job_id);
        return;
    }
    auto context = QSharedPointer<DownloadContext>::create(); // 生命周期跨网络和数据库回调。
    context->_sha256 = sha256;
    context->_url = url;
    context->_waiters.insert(job_id);
    _download_contexts.insert(sha256, context);
    ++_active_worker_count;
    auto* watcher = new QFutureWatcher<AssetCacheResult>(this); // 缓存复查。
    connect(watcher, &QFutureWatcher<AssetCacheResult>::finished, this, [this, context, watcher] {
        --_active_worker_count;
        const AssetCacheResult result = watcher->result(); // 缓存结果。
        watcher->deleteLater();
        if (context->_waiters.isEmpty() || result._status != "miss")
        {
            FinishDownload(context, result);
        } else
        {
            _network_queue.enqueue([this, context] { StartDownload(context); });
            DrainNetworkQueue();
        }
        emit sigActivityChanged();
    });
    watcher->setFuture(AssetDatabaseManager::getInstance()->LoadAssetAsync(sha256));
}

void AssetTransferManager::StartDownload(const QSharedPointer<DownloadContext>& context)
{
    // 1. 排队期间所有等待者取消则不访问 OSS。
    if (context->_waiters.isEmpty())
    {
        _download_contexts.remove(context->_sha256);
        return;
    }
    ++_active_network_count;
    QNetworkRequest request(context->_url); // 不输出签名地址。
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    context->_reply = _network_manager->get(request);
    context->_reply->setReadBufferSize(64 * 1024);
    auto* timeout = new QTimer(context->_reply); // 绝对超时，不受持续少量数据影响。
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, context->_reply, &QNetworkReply::abort);
    timeout->start(30000);
    connect(context->_reply, &QNetworkReply::readyRead, this, [context] {
        const qint64 remaining = AssetServiceProtocol::MAX_IMAGE_BYTES + 1 - context->_data.size(); // 超限只多读一个字节。
        context->_data += context->_reply->read(qMax<qint64>(0, remaining));
        if (context->_data.size() > AssetServiceProtocol::MAX_IMAGE_BYTES)
        {
            context->_is_over_limit = true;
            context->_reply->abort();
        }
    });
    connect(context->_reply, &QNetworkReply::finished, this, [this, context] {
        QNetworkReply* reply = context->_reply; // 先结束网络槽，再启动数据库工作。
        context->_reply = nullptr;
        --_active_network_count;
        context->_data += reply->read(AssetServiceProtocol::MAX_IMAGE_BYTES + 1 - context->_data.size());
        const bool is_success = reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200; // 只接受完整对象。
        reply->deleteLater();
        if (!is_success || context->_is_over_limit || context->_data.size() > AssetServiceProtocol::MAX_IMAGE_BYTES || context->_waiters.isEmpty())
        {
            FinishDownload(context, TransferError("图片下载失败、取消或超过 10 MiB"));
        } else
        {
            ++_active_worker_count;
            auto* watcher = new QFutureWatcher<AssetCacheResult>(this); // 校验完成才写缓存。
            connect(watcher, &QFutureWatcher<AssetCacheResult>::finished, this, [this, context, watcher] {
                --_active_worker_count;
                FinishDownload(context, watcher->result());
                watcher->deleteLater();
                emit sigActivityChanged();
            });
            watcher->setFuture(QtConcurrent::run(&_prepare_thread_pool, [context] {
                AssetCacheResult result = AssetServiceProtocol::ValidateImage(context->_data); // OSS 对象完整性验证。
                if (result._status != "ok" || result._record._asset_sha256 != context->_sha256)
                {
                    return TransferError("图片内容摘要不匹配", "IntegrityError");
                }
                auto future = AssetDatabaseManager::getInstance()->StoreAssetAsync(context->_data); // 可信对象落盘。
                future.waitForFinished();
                return future.result();
            }));
        }
        DrainNetworkQueue();
        emit sigActivityChanged();
    });
    qDebug() << "AssetTransferManager Download sha=" << context->_sha256 << "waiters=" << context->_waiters.size();
}

void AssetTransferManager::FinishDownload(const QSharedPointer<DownloadContext>& context, AssetCacheResult result)
{
    // 1. 一份编码数据分发给各连接，引用只在回调期间共享。
    _download_contexts.remove(context->_sha256);
    const auto waiters = context->_waiters; // 避免回调取消时修改迭代容器。
    for (const QString& job_id : waiters)
    {
        Finish(job_id, result);
    }
    emit sigActivityChanged();
}

void AssetTransferManager::UploadAsset(const QString& job_id, const AssetCacheResult& asset, const QUrl& url)
{
    // 1. 排队和传输期间都保留独立的共享保护持有者。
    _jobs.insert(job_id);
    if (!asset._lease || !IsTransferUrl(url))
    {
        Finish(job_id, TransferError("资源句柄或上传地址无效", "InvalidHandle"));
        return;
    }
    AssetCacheResult protected_asset = asset; // 只保留元数据、路径和引用。
    protected_asset._data.clear();
    _network_queue.enqueue([this, job_id, protected_asset, url] {
        if (!_jobs.contains(job_id))
        {
            return;
        }
        auto* file = new QFile(protected_asset._file_path); // 路径只能由代理内部产生。
        if (!file->open(QIODevice::ReadOnly) || file->size() != protected_asset._record._byte_size)
        {
            delete file;
            Finish(job_id, TransferError("上传缓存文件不可用"));
            return;
        }
        ++_active_network_count;
        QNetworkRequest request(url); // URL 只用于请求，不写入日志。
        request.setHeader(QNetworkRequest::ContentTypeHeader, protected_asset._record._mime_type);
        request.setTransferTimeout(30000);
        auto* reply = _network_manager->put(request, file); // 流式上传。
        reply->setReadBufferSize(64 * 1024);
        auto response_bytes = QSharedPointer<qint64>::create(0); // PUT 控制回复同样限制大小。
        connect(reply, &QNetworkReply::readyRead, this, [reply, response_bytes] {
            *response_bytes += reply->readAll().size();
            if (*response_bytes > AssetServiceProtocol::MAX_JSON_BYTES)
            {
                reply->abort();
            }
        });
        file->setParent(reply);
        _upload_contexts.insert(job_id, reply);
        auto* timeout = new QTimer(reply); // 绝对网络超时。
        timeout->setSingleShot(true);
        connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
        timeout->start(30000);
        connect(reply, &QNetworkReply::finished, this, [this, reply, file, job_id, protected_asset] {
            --_active_network_count;
            _upload_contexts.remove(job_id);
            file->close();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); // 不读取 OSS 错误正文。
            AssetCacheResult result = protected_asset; // 先关闭文件，随后才可释放保护。
            if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
            {
                result = TransferError("图片上传失败或超时");
            }
            reply->deleteLater();
            Finish(job_id, result);
            DrainNetworkQueue();
        });
        qDebug() << "AssetTransferManager Upload job=" << job_id << "sha=" << protected_asset._record._asset_sha256;
    });
    DrainNetworkQueue();
}

void AssetTransferManager::DrainNetworkQueue()
{
    // 1. GET 与 PUT 共同遵守四个并行网络请求上限。
    while (!_is_stopping && _active_network_count < 4 && !_network_queue.isEmpty())
    {
        auto start = _network_queue.dequeue(); // 出队后执行，允许取消过期任务。
        start();
    }
    emit sigActivityChanged();
}

void AssetTransferManager::CancelRequest(const QString& job_id)
{
    // 1. 共享下载只移除本请求，最后等待者退出才中止网络。
    _jobs.remove(job_id);
    for (const auto& context : std::as_const(_download_contexts))
    {
        context->_waiters.remove(job_id);
        if (context->_waiters.isEmpty() && context->_reply)
        {
            context->_reply->abort();
            break;
        }
    }
    // 2. 独占上传可以直接中止，finished 回调负责关闭文件。
    if (_upload_contexts.contains(job_id))
    {
        _upload_contexts[job_id]->abort();
    }
    DrainNetworkQueue();
}

bool AssetTransferManager::HasActiveTasks() const
{
    // 1. 取消不等于线程已经结束，退出必须检查实际活动量。
    return !_jobs.isEmpty() || _active_worker_count > 0 || _active_network_count > 0 || !_network_queue.isEmpty();
}

void AssetTransferManager::Shutdown()
{
    // 1. 中止回复之前关闭新任务入口及排队持有的引用。
    _is_stopping = true;
    _jobs.clear();
    _network_queue.clear();
    const auto replies = _network_manager->findChildren<QNetworkReply*>(); // 回调中可能删除映射，使用副本。
    for (auto* reply : replies)
    {
        reply->abort();
    }
    // 2. 导入线程会等待数据库写池，数据库必须在其后停止。
    _prepare_thread_pool.waitForDone();
    _download_contexts.clear();
    _upload_contexts.clear();
}
