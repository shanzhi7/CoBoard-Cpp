#include "imageassetmanager.h"

#include <QBuffer>
#include <QFutureWatcher>
#include <QtConcurrent>

#include "assetserviceprotocol.h"
#include "servicecommandclient.h"

ImageAssetManager::ImageAssetManager(QObject* parent) : QObject(parent)
{
    // 1. UI 不打开缓存文件、不创建数据库或 OSS 网络对象。
    _prepare_thread_pool.setMaxThreadCount(2);
    connect(ServiceCommandClient::getInstance().get(), &ServiceCommandClient::sigResponseReady, this, &ImageAssetManager::ReceiveResponse);
    connect(ServiceCommandClient::getInstance().get(), &ServiceCommandClient::sigServiceDisconnected, this, [this] {
        ++_service_generation;
        _asset_handles.clear();
        emit sigServiceLost();
    });
}

ImageAssetManager::~ImageAssetManager()
{
    // 1. 销毁前保证工作线程不再引用本类。
    CancelAll();
    _prepare_thread_pool.waitForDone();
}

void ImageAssetManager::Submit(const RequestContext& context, const QJsonObject& payload, const QByteArray& binary)
{
    // 1. 客户端保证响应异步到达，先登记请求上下文。
    const QString request_id = ServiceCommandClient::getInstance()->SendCommand(context._command, payload, binary); // IPC UUID。
    _requests.insert(request_id, context);
}

void ImageAssetManager::prepareLocalAssetAsync(const QString& file_path, const QString& request_id)
{
    // 1. 原始文件仅由代理读取。
    RequestContext context; // 导入关联。
    context._command = "ImportFile";
    context._request_id = request_id;
    context._source_path = file_path;
    Submit(context, {{"filePath", file_path}});
}

void ImageAssetManager::PrepareImageDataAsync(const QImage& image, const QString& request_id)
{
    // 1. PNG 压缩放到工作线程，完成后提交代理。
    const quint64 generation = _generation; // 迟到结果校验。
    auto* watcher = new QFutureWatcher<QByteArray>(this); // UI 回调。
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher, request_id, generation] {
        const QByteArray data = watcher->result(); // 已编码数据。
        watcher->deleteLater();
        if (generation != _generation)
        {
            return;
        }
        if (data.isEmpty() || data.size() > MAX_FILE_SIZE)
        {
            emit sigLocalAssetFailed(request_id, {}, "剪贴板图片无效或超过 10 MiB");
            return;
        }
        RequestContext context; // 剪贴板导入关联。
        context._command = "ImportImageData";
        context._request_id = request_id;
        Submit(context, {}, data);
    });
    watcher->setFuture(QtConcurrent::run(&_prepare_thread_pool, [image] {
        QByteArray data; // 内存 PNG 编码。
        if (image.isNull() || image.width() > MAX_IMAGE_WIDTH || image.height() > MAX_IMAGE_HEIGHT)
        {
            return data;
        }
        QBuffer buffer(&data); // 不创建临时文件。
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG"))
        {
            data.clear();
        }
        return data;
    }));
}

void ImageAssetManager::LoadAssetAsync(const QString& asset_id, const QString& sha256, const QString& mime_type)
{
    // 1. 缓存命中不请求网关签名。
    if (_pending_downloads.contains(asset_id))
    {
        return;
    }
    _pending_downloads.insert(asset_id);
    RequestContext context; // 缓存读取关联。
    context._command = "LoadAsset";
    context._asset_id = asset_id;
    context._sha256 = sha256;
    context._mime_type = mime_type;
    Submit(context, {{"sha256", sha256}});
}

void ImageAssetManager::ReadAssetDataAsync(const QString& sha256, const QString& request_id)
{
    // 1. 复用 LoadAsset，原始文件只能由缓存代理读取。
    RequestContext context;
    context._command = QStringLiteral("LoadAsset");
    context._request_id = request_id;
    context._sha256 = sha256;
    context._is_data_request = true;
    Submit(context, {{"sha256", sha256}});
}

void ImageAssetManager::ImportAssetDataAsync(const QByteArray& data, const QString& request_id)
{
    // 1. 复用内存图片登记指令，文件导入结果不触发单图片插入信号。
    RequestContext context;
    context._command = QStringLiteral("ImportImageData");
    context._request_id = request_id;
    context._is_data_request = true;
    Submit(context, {}, data);
}

void ImageAssetManager::CancelAssetDataRequest(const QString& request_id)
{
    // 1. 只取消携带该文件关联 ID 的门面请求，不影响其他图片传输。
    const auto request_ids = _requests.keys();
    for (const QString& ipc_request_id : request_ids)
    {
        const RequestContext context = _requests.value(ipc_request_id);
        if (context._is_data_request && context._request_id == request_id)
        {
            _requests.remove(ipc_request_id);
            ServiceCommandClient::getInstance()->CancelRequest(ipc_request_id);
        }
    }
}

void ImageAssetManager::downloadAsset(const QString& asset_id, const QUrl& url, const QString& sha256, const QString& mime_type)
{
    // 1. 代理再次查缓存并合并跨窗口下载。
    if (_pending_downloads.contains(asset_id))
    {
        return;
    }
    _pending_downloads.insert(asset_id);
    RequestContext context; // 下载关联。
    context._command = "DownloadAsset";
    context._asset_id = asset_id;
    context._sha256 = sha256;
    context._mime_type = mime_type;
    Submit(context, {{"sha256", sha256}, {"url", url.toString(QUrl::FullyEncoded)}});
}

void ImageAssetManager::uploadAsset(const QString& asset_id, const QString& asset_handle, const QUrl& url, const QString& mime_type, const QString& sha256, qint64 byte_size)
{
    // 1. 仅发送 PUT 签名及句柄，不传登录 Token。
    RequestContext context; // 上传关联。
    context._command = "UploadAsset";
    context._asset_id = asset_id;
    context._asset_handle = asset_handle;
    context._sha256 = sha256;
    context._mime_type = mime_type;
    Submit(context, {{"assetHandle", asset_handle}, {"url", url.toString(QUrl::FullyEncoded)}, {"sha256", sha256}, {"mimeType", mime_type}, {"byteSize", byte_size}});
}

void ImageAssetManager::Fail(const RequestContext& context, const QString& message)
{
    // 1. 按业务类型返回终态失败。
    if (context._is_data_request)
    {
        emit sigAssetDataFailed(context._request_id, message);
    } else if (context._command.startsWith("Import"))
    {
        emit sigLocalAssetFailed(context._request_id, context._source_path, message);
    } else if (context._command == "UploadAsset")
    {
        ReleaseAsset(context._asset_handle);
        emit sigAssetUploadFailed(context._asset_id, message);
    } else
    {
        _pending_downloads.remove(context._asset_id);
        emit sigAssetFailed(context._asset_id, message);
    }
}

void ImageAssetManager::ReceiveResponse(const QString& request_id, const QJsonObject& response, const QByteArray& binary)
{
    // 1. 共享客户端中的其他门面请求不属于本对象。
    if (!_requests.contains(request_id))
    {
        return;
    }
    const RequestContext context = _requests.take(request_id); // 终态关联。
    const QString status = response.value("status").toString(); // 响应状态。
    if (status == "miss" && context._command == "LoadAsset" && !context._is_data_request)
    {
        _pending_downloads.remove(context._asset_id);
        emit sigAssetCacheMiss(context._asset_id);
        return;
    }
    if (status != "ok")
    {
        Fail(context, response.value("message").toString("图片代理请求失败"));
        return;
    }
    const QJsonObject payload = response.value("payload").toObject(); // 元数据。
    const AssetCacheRecord record = AssetCacheRecord::FromJson(payload); // 图片属性。
    if (context._command == "UploadAsset")
    {
        ReleaseAsset(context._asset_handle);
        if (record._asset_sha256 != context._sha256 || record._mime_type != context._mime_type)
        {
            Fail(context, "上传资源元数据不匹配");
            return;
        }
        emit sigAssetUploaded(context._asset_id, record._asset_sha256, record._mime_type);
        return;
    }
    const QString handle = payload.value("assetHandle").toString(); // 解码期间也必须能取消保护。
    if (!handle.isEmpty())
    {
        _asset_handles.insert(handle);
    }
    // 2. 文件任务只取得编码数据，临时登记保护立即归还，像素已由文件线程准备。
    if (context._is_data_request)
    {
        ReleaseAsset(handle);
        emit sigAssetDataReady(context._request_id, binary);
        return;
    }
    const quint64 generation = _generation; // 房间切换校验。
    const quint64 service_generation = _service_generation; // 断线后旧结果不可继续交付。
    auto* watcher = new QFutureWatcher<QImage>(this); // QPixmap 只在 UI 线程创建。
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, context, record, handle, generation, service_generation] {
        const QImage image = watcher->result(); // 校验后的像素。
        watcher->deleteLater();
        if (generation != _generation)
        {
            ReleaseAsset(handle);
            return;
        }
        if (service_generation != _service_generation)
        {
            Fail(context, "图片后台服务已重启，请重新加载");
            return;
        }
        if (image.isNull())
        {
            ReleaseAsset(handle);
            Fail(context, "图片解码或完整性校验失败");
            return;
        }
        // 3. 在主线程创建 QPixmap，交付业务结果。
        const QPixmap pixmap = QPixmap::fromImage(image); // GUI 像素。
        if (context._command.startsWith("Import"))
        {
            ImageAssetInfo info; // 房间图片类型。
            info.asset_id = record._asset_sha256;
            info.asset_sha256 = record._asset_sha256;
            info.mime_type = record._mime_type;
            info.original_size = record._original_size;
            info.byte_size = record._byte_size;
            info._asset_handle = handle;
            emit sigLocalAssetReady(context._request_id, context._source_path, info, pixmap);
        } else
        {
            _pending_downloads.remove(context._asset_id);
            emit sigAssetReady(context._asset_id, pixmap, record._asset_sha256, record._original_size, record._mime_type);
        }
    });
    watcher->setFuture(QtConcurrent::run(&_prepare_thread_pool, [binary, record, context] {
        const AssetCacheResult result = AssetServiceProtocol::ValidateImage(binary); // 协议数据也需要验证。
        if (result._status != "ok" || result._record._asset_sha256 != record._asset_sha256 || result._record._original_size != record._original_size
            || result._record._mime_type != record._mime_type || result._record._byte_size != record._byte_size
            || (!context._sha256.isEmpty() && result._record._asset_sha256 != context._sha256)
            || (!context._mime_type.isEmpty() && result._record._mime_type != context._mime_type))
        {
            return QImage();
        }
        return QImage::fromData(binary);
    }));
}

void ImageAssetManager::ReleaseAsset(const QString& asset_handle)
{
    // 1. 幂等释放，由代理解除文件保护。
    if (!asset_handle.isEmpty() && _asset_handles.remove(asset_handle))
    {
        ServiceCommandClient::getInstance()->SendCommand("ReleaseAsset", {{"assetHandle", asset_handle}});
    }
}

void ImageAssetManager::CancelAssetUpload(const QString& asset_handle)
{
    // 1. 先移除门面上下文，取消响应不能误推进 Canvas 的新上传。
    const auto request_ids = _requests.keys(); // 稳定请求快照。
    for (const QString& request_id : request_ids)
    {
        if (_requests.value(request_id)._command == "UploadAsset" && _requests.value(request_id)._asset_handle == asset_handle)
        {
            _requests.remove(request_id);
            ServiceCommandClient::getInstance()->CancelRequest(request_id);
        }
    }
    // 2. 代理收到取消后关闭上传文件，再解除最后一个保护持有者。
    ReleaseAsset(asset_handle);
}

void ImageAssetManager::TouchAssetAsync(const QString& sha256)
{
    // 1. 预览只更新 LRU，不访问文件。
    ServiceCommandClient::getInstance()->SendCommand("TouchAsset", {{"sha256", sha256}});
}

void ImageAssetManager::CancelAll()
{
    // 1. 先废弃回调，再取消本门面的 IPC 请求。
    ++_generation;
    const auto request_ids = _requests.keys(); // 不取消其他门面的任务。
    _requests.clear();
    for (const QString& request_id : request_ids)
    {
        ServiceCommandClient::getInstance()->CancelRequest(request_id);
    }
    // 2. 实际上传另持保护，不会因句柄释放提前删除文件。
    const auto handles = _asset_handles; // 避免边遍历边移除。
    for (const QString& handle : handles)
    {
        ReleaseAsset(handle);
    }
    _pending_downloads.clear();
}
