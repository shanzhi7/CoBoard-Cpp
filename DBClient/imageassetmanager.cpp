#include "imageassetmanager.h"

#include <QCryptographicHash>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QImageReader>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrentRun>

namespace
{
struct LocalAssetResult
{
    bool success = false; // 后台阶段是否完成全部读取、校验和缓存写入。
    ImageAssetInfo asset_info; // 已由图片内容计算出的资源元数据。
    QImage image; // 在线程池中解码的像素，回到 UI 线程后才转换为 QPixmap。
    QString error_message; // 失败原因，不包含本地路径以外的敏感内容。
};

QString LocalCacheDirectory()
{
    // 工作线程不能依赖 ImageAssetManager 实例，直接复用同一 Qt 应用数据目录规则。
    const QString base_path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir::cleanPath(base_path + QDir::separator() + QStringLiteral("canvas-assets"));
}

bool WriteLocalCacheFile(const QString& asset_id,
                         const QByteArray& data,
                         QString* cache_path)
{
    // 内容摘要由 SHA-256 生成，只会得到单级安全文件名；原子提交避免异常退出暴露半文件。
    const QString directory = LocalCacheDirectory();
    if (!QDir().mkpath(directory))
    {
        return false;
    }
    const QString target_path = QDir::cleanPath(directory + QDir::separator() + asset_id);
    QSaveFile cache_file(target_path);
    if (!cache_file.open(QIODevice::WriteOnly) || cache_file.write(data) != data.size())
    {
        return false;
    }
    if (!cache_file.commit())
    {
        return false;
    }
    if (cache_path)
    {
        *cache_path = target_path;
    }
    return true;
}

LocalAssetResult PrepareImageDataInWorker(const QByteArray& data)
{
    LocalAssetResult result;

    // 文件和剪贴板数据共用这一段校验，保证两种入口的格式、尺寸和缓存规则完全一致。
    if (data.isEmpty() || data.size() > ImageAssetManager::MAX_FILE_SIZE)
    {
        result.error_message = QStringLiteral("图片大小超过 10 MB 限制");
        return result;
    }

    // QImageReader 在工作线程内按内容识别格式并完成解码，不接触 UI 专用的 QPixmap。
    QBuffer buffer;
    buffer.setData(data);
    if (!buffer.open(QIODevice::ReadOnly))
    {
        result.error_message = QStringLiteral("无法读取图片数据");
        return result;
    }
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    const QByteArray format = reader.format().toLower();
    if (format != QByteArrayLiteral("png") &&
        format != QByteArrayLiteral("jpg") &&
        format != QByteArrayLiteral("jpeg") &&
        format != QByteArrayLiteral("webp"))
    {
        result.error_message = QStringLiteral("只支持 PNG、JPG、JPEG 和 WEBP 图片");
        return result;
    }
    const QString mime_type = QMimeDatabase().mimeTypeForData(data).name().toLower();
    if (mime_type != QStringLiteral("image/png") &&
        mime_type != QStringLiteral("image/jpeg") &&
        mime_type != QStringLiteral("image/webp"))
    {
        result.error_message = QStringLiteral("图片 MIME 类型不在允许范围内");
        return result;
    }
    const QImage image = reader.read();
    if (image.isNull())
    {
        result.error_message = QStringLiteral("图片解码失败");
        return result;
    }
    if (image.width() <= 0 || image.height() <= 0 ||
        image.width() > ImageAssetManager::MAX_IMAGE_WIDTH ||
        image.height() > ImageAssetManager::MAX_IMAGE_HEIGHT)
    {
        result.error_message = QStringLiteral("图片像素尺寸超过 4096 限制");
        return result;
    }

    const QString asset_sha256 = QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    QString cache_path;
    if (!WriteLocalCacheFile(asset_sha256, data, &cache_path))
    {
        result.error_message = QStringLiteral("无法写入图片缓存");
        return result;
    }

    result.asset_info.asset_id = asset_sha256;
    result.asset_info.asset_ref = asset_sha256;
    result.asset_info.asset_sha256 = asset_sha256;
    result.asset_info.mime_type = mime_type;
    result.asset_info.original_size = image.size();
    result.asset_info.byte_size = data.size();
    result.asset_info.local_file_path = cache_path;
    result.image = image;
    result.success = true;
    return result;
}

LocalAssetResult PrepareLocalAssetInWorker(const QString& file_path)
{
    LocalAssetResult result;

    // 先读取文件元数据，超限文件不进入内存，避免用户选择异常大文件拖垮线程池。
    QFileInfo file_info(file_path);
    if (!file_info.exists() || !file_info.isFile())
    {
        result.error_message = QStringLiteral("图片文件不存在");
        return result;
    }
    if (file_info.size() <= 0 || file_info.size() > ImageAssetManager::MAX_FILE_SIZE)
    {
        result.error_message = QStringLiteral("图片大小超过 10 MB 限制");
        return result;
    }

    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.error_message = QStringLiteral("无法读取图片文件");
        return result;
    }
    const QByteArray data = file.readAll();
    file.close();
    if (data.isEmpty() || data.size() != file_info.size())
    {
        result.error_message = QStringLiteral("图片读取结果不完整");
        return result;
    }

    return PrepareImageDataInWorker(data);
}

LocalAssetResult PrepareClipboardImageInWorker(const QImage& image)
{
    // 剪贴板图片先统一编码为 PNG，再复用文件入口的内容校验和内容寻址缓存。
    if (image.isNull())
    {
        LocalAssetResult result;
        result.error_message = QStringLiteral("剪贴板图片为空");
        return result;
    }

    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
    {
        LocalAssetResult result;
        result.error_message = QStringLiteral("无法编码剪贴板图片");
        return result;
    }
    return PrepareImageDataInWorker(data);
}
}

ImageAssetManager::ImageAssetManager(QObject* parent)
    : QObject(parent)
    , _network_manager(new QNetworkAccessManager(this))
{
    // 网络请求统一归属当前 Qt 线程，所有完成信号都会回到 UI 事件循环，不在回调里触碰场景绘制对象。
}

ImageAssetManager::~ImageAssetManager()
{
    // QNetworkAccessManager 作为子对象会先取消并释放回复；清空映射避免析构阶段再访问悬空指针。
    _download_contexts.clear();
    _upload_contexts.clear();
    _pending_downloads.clear();
}

QString ImageAssetManager::cacheDirectory() const
{
    // AppDataLocation 随平台选择用户可写目录，避免把缓存写入程序安装目录或项目目录。
    const QString base_path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir::cleanPath(base_path + QDir::separator() + QStringLiteral("canvas-assets"));
}

QString ImageAssetManager::buildCachePath(const QString& asset_id) const
{
    // 只使用净化后的单级文件名，防止服务端返回的资源 ID 通过 ../ 越出缓存目录。
    return QDir::cleanPath(cacheDirectory() + QDir::separator() + sanitizeAssetId(asset_id));
}

QString ImageAssetManager::calculateSha256(const QByteArray& data) const
{
    // 统一使用小写十六进制，协议和缓存比较时不再因大小写产生两份资源。
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

bool ImageAssetManager::isSupportedMimeType(const QString& mime_type) const
{
    // 白名单与上传端、CanvasServer 的限制保持一致，避免客户端缓存任意非图片内容。
    const QString normalized_type = mime_type.trimmed().toLower();
    return normalized_type == QStringLiteral("image/png") ||
           normalized_type == QStringLiteral("image/jpeg") ||
           normalized_type == QStringLiteral("image/webp");
}

bool ImageAssetManager::prepareLocalAsset(const QString& file_path,
                                          ImageAssetInfo* asset_info,
                                          QPixmap* pixmap,
                                          QString* error_message)
{
    if (!asset_info || !pixmap)
    {
        if (error_message)
        {
            *error_message = QStringLiteral("图片输出参数无效");
        }
        return false;
    }

    // 本地文件先检查大小，再读取内容，避免超大文件一次性进入内存造成 UI 进程压力。
    QFileInfo file_info(file_path);
    if (!file_info.exists() || !file_info.isFile())
    {
        if (error_message)
        {
            *error_message = QStringLiteral("图片文件不存在");
        }
        return false;
    }
    if (file_info.size() <= 0 || file_info.size() > MAX_FILE_SIZE)
    {
        if (error_message)
        {
            *error_message = QStringLiteral("图片大小超过 10 MB 限制");
        }
        return false;
    }

    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error_message)
        {
            *error_message = QStringLiteral("无法读取图片文件");
        }
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    ImageAssetInfo parsed_info;
    QPixmap parsed_pixmap;
    if (!readAndValidateImage(data, &parsed_info, &parsed_pixmap, error_message))
    {
        return false;
    }

    // 内容摘要既能作为离线资源 ID，也能复用缓存，避免用户多次导入同一文件产生重复副本。
    parsed_info.asset_sha256 = calculateSha256(data);
    parsed_info.asset_id = parsed_info.asset_sha256;
    parsed_info.asset_ref = parsed_info.asset_id;
    if (!writeCacheFile(parsed_info.asset_id, data, &parsed_info.local_file_path))
    {
        if (error_message)
        {
            *error_message = QStringLiteral("无法写入图片缓存");
        }
        return false;
    }

    *asset_info = parsed_info;
    *pixmap = parsed_pixmap;
    return true;
}

void ImageAssetManager::prepareLocalAssetAsync(const QString& file_path,
                                               const QString& request_id)
{
    if (file_path.isEmpty() || request_id.isEmpty())
    {
        emit sigLocalAssetFailed(request_id,
                                 file_path,
                                 QStringLiteral("图片读取参数无效"));
        return;
    }

    // QFutureWatcher 归属于 ImageAssetManager；销毁管理器会断开回调，工作线程只持有值类型参数。
    auto* watcher = new QFutureWatcher<LocalAssetResult>(this);
    connect(watcher,
            &QFutureWatcher<LocalAssetResult>::finished,
            this,
            [this, watcher, request_id, file_path]() {
                // finished 已回到管理器所属线程，QPixmap 只能在此处从工作线程的 QImage 创建。
                const LocalAssetResult result = watcher->result();
                watcher->deleteLater();
                if (!result.success)
                {
                    emit sigLocalAssetFailed(request_id,
                                             file_path,
                                             result.error_message.isEmpty()
                                                 ? QStringLiteral("图片读取失败")
                                                 : result.error_message);
                    return;
                }

                emit sigLocalAssetReady(request_id,
                                        file_path,
                                        result.asset_info.asset_id,
                                        result.asset_info.asset_ref,
                                        result.asset_info.asset_sha256,
                                        result.asset_info.mime_type,
                                        result.asset_info.original_size,
                                        result.asset_info.byte_size,
                                        QPixmap::fromImage(result.image),
                                        result.asset_info.local_file_path);
            });

    // 读取、摘要、格式校验、解码和缓存写入全部在线程池中完成，UI 线程只处理完成信号。
    watcher->setFuture(QtConcurrent::run(PrepareLocalAssetInWorker, file_path));
}

void ImageAssetManager::PrepareImageDataAsync(const QImage& image,
                                              const QString& request_id)
{
    if (image.isNull() || request_id.isEmpty())
    {
        emit sigLocalAssetFailed(request_id,
                                 QString(),
                                 QStringLiteral("剪贴板图片参数无效"));
        return;
    }

    // QImage 是隐式共享值类型，可以安全交给线程池；编码完成后再回 UI 线程创建 QPixmap。
    auto* watcher = new QFutureWatcher<LocalAssetResult>(this);
    connect(watcher,
            &QFutureWatcher<LocalAssetResult>::finished,
            this,
            [this, watcher, request_id]() {
                const LocalAssetResult result = watcher->result();
                watcher->deleteLater();
                if (!result.success)
                {
                    emit sigLocalAssetFailed(request_id,
                                             QString(),
                                             result.error_message.isEmpty()
                                                 ? QStringLiteral("剪贴板图片读取失败")
                                                 : result.error_message);
                    return;
                }

                emit sigLocalAssetReady(request_id,
                                        QString(),
                                        result.asset_info.asset_id,
                                        result.asset_info.asset_ref,
                                        result.asset_info.asset_sha256,
                                        result.asset_info.mime_type,
                                        result.asset_info.original_size,
                                        result.asset_info.byte_size,
                                        QPixmap::fromImage(result.image),
                                        result.asset_info.local_file_path);
            });

    watcher->setFuture(QtConcurrent::run(PrepareClipboardImageInWorker, image));
}

void ImageAssetManager::downloadAsset(const QString& asset_id,
                                      const QUrl& download_url,
                                      const QString& expected_sha256,
                                      const QString& mime_type)
{
    if (asset_id.isEmpty() || !download_url.isValid())
    {
        emit sigAssetFailed(asset_id, QStringLiteral("图片下载参数无效"));
        return;
    }

    // 同一个资源可能同时被历史回放中的多个图元引用，只保留一个网络请求并让后续调用复用缓存。
    if (_pending_downloads.contains(asset_id))
    {
        return;
    }

    ImageAssetInfo cached_info;
    QPixmap cached_pixmap;
    QString cache_error;
    if (readCachedAsset(asset_id, expected_sha256, mime_type,
                        &cached_info, &cached_pixmap, &cache_error))
    {
        emit sigAssetReady(asset_id,
                           cached_pixmap,
                           cached_info.asset_sha256,
                           cached_info.original_size,
                           cached_info.mime_type,
                           cached_info.local_file_path);
        return;
    }

    // URL 只被交给 Qt 网络层使用，不写入日志，避免短期签名泄露到调试输出。
    QNetworkRequest request(download_url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = _network_manager->get(request);
    _download_contexts.insert(reply, DownloadContext{asset_id,
                                                      expected_sha256.toLower(),
                                                      mime_type.toLower()});
    _pending_downloads.insert(asset_id);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleDownloadFinished(reply);
    });
}

void ImageAssetManager::uploadAsset(const QString& asset_id,
                                    const QString& file_path,
                                    const QUrl& upload_url,
                                    const QString& mime_type,
                                    const QString& expected_sha256,
                                    qint64 expected_byte_size)
{
    const QString normalized_sha256 = expected_sha256.trimmed().toLower();
    if (asset_id.isEmpty() || !upload_url.isValid() || !isSupportedMimeType(mime_type) ||
        normalized_sha256.size() != 64 ||
        normalized_sha256.contains(QRegularExpression(QStringLiteral("[^0-9a-f]"))) ||
        expected_byte_size <= 0)
    {
        emit sigAssetUploadFailed(asset_id, QStringLiteral("图片上传参数无效"));
        return;
    }

    QFileInfo file_info(file_path);
    if (!file_info.exists() || !file_info.isFile() ||
        file_info.size() <= 0 || file_info.size() > MAX_FILE_SIZE ||
        file_info.size() != expected_byte_size)
    {
        emit sigAssetUploadFailed(asset_id, QStringLiteral("图片文件不存在、大小超限或内容已变化"));
        return;
    }

    QFile* file = new QFile(file_path);
    if (!file->open(QIODevice::ReadOnly))
    {
        delete file;
        emit sigAssetUploadFailed(asset_id, QStringLiteral("无法打开待上传图片"));
        return;
    }

    QNetworkRequest request(upload_url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, mime_type);
    request.setHeader(QNetworkRequest::ContentLengthHeader, file_info.size());
    QNetworkReply* reply = _network_manager->put(request, file);
    file->setParent(reply);
    _upload_contexts.insert(reply, UploadContext{asset_id, normalized_sha256, mime_type.toLower()});
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleUploadFinished(reply);
    });
}

bool ImageAssetManager::readAndValidateImage(const QByteArray& data,
                                             ImageAssetInfo* asset_info,
                                             QPixmap* pixmap,
                                             QString* error_message) const
{
    if (!asset_info || !pixmap)
    {
        return false;
    }
    if (data.isEmpty() || data.size() > MAX_FILE_SIZE)
    {
        if (error_message)
        {
            *error_message = QStringLiteral("图片数据为空或大小超限");
        }
        return false;
    }

    // QImageReader 同时完成格式识别和尺寸读取，不能只根据文件扩展名相信输入内容。
    QBuffer buffer;
    buffer.setData(data);
    if (!buffer.open(QIODevice::ReadOnly))
    {
        if (error_message)
        {
            *error_message = QStringLiteral("无法读取图片数据");
        }
        return false;
    }
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    const QByteArray format = reader.format().toLower();
    const QString mime_type = QMimeDatabase().mimeTypeForData(data).name().toLower();
    if (format != QByteArrayLiteral("png") &&
        format != QByteArrayLiteral("jpg") &&
        format != QByteArrayLiteral("jpeg") &&
        format != QByteArrayLiteral("webp"))
    {
        if (error_message)
        {
            *error_message = QStringLiteral("只支持 PNG、JPG、JPEG 和 WEBP 图片");
        }
        return false;
    }
    if (!isSupportedMimeType(mime_type))
    {
        if (error_message)
        {
            *error_message = QStringLiteral("图片 MIME 类型不在允许范围内");
        }
        return false;
    }

    QPixmap decoded_pixmap;
    if (!decoded_pixmap.loadFromData(data))
    {
        if (error_message)
        {
            *error_message = QStringLiteral("图片解码失败");
        }
        return false;
    }
    const QSize image_size = decoded_pixmap.size();
    if (!image_size.isValid() || image_size.width() <= 0 || image_size.height() <= 0 ||
        image_size.width() > MAX_IMAGE_WIDTH || image_size.height() > MAX_IMAGE_HEIGHT)
    {
        if (error_message)
        {
            *error_message = QStringLiteral("图片像素尺寸超过 4096 限制");
        }
        return false;
    }

    asset_info->mime_type = mime_type;
    asset_info->original_size = image_size;
    asset_info->byte_size = data.size();
    *pixmap = decoded_pixmap;
    return true;
}

bool ImageAssetManager::writeCacheFile(const QString& asset_id,
                                       const QByteArray& data,
                                       QString* cache_path) const
{
    const QString directory = cacheDirectory();
    if (!QDir().mkpath(directory))
    {
        return false;
    }

    const QString target_path = buildCachePath(asset_id);
    QSaveFile cache_file(target_path);
    if (!cache_file.open(QIODevice::WriteOnly) || cache_file.write(data) != data.size())
    {
        return false;
    }
    if (!cache_file.commit())
    {
        return false;
    }
    if (cache_path)
    {
        *cache_path = target_path;
    }
    return true;
}

bool ImageAssetManager::readCachedAsset(const QString& asset_id,
                                         const QString& expected_sha256,
                                         const QString& mime_type,
                                         ImageAssetInfo* asset_info,
                                         QPixmap* pixmap,
                                         QString* error_message) const
{
    const QString path = buildCachePath(asset_id);
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
    {
        if (error_message)
        {
            *error_message = QStringLiteral("图片缓存未命中");
        }
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    const QString actual_sha256 = calculateSha256(data);
    if (!expected_sha256.isEmpty() && actual_sha256 != expected_sha256.toLower())
    {
        // 缓存损坏或资源被替换时删除当前缓存，下次请求会重新下载而不是反复使用坏数据。
        QFile::remove(path);
        if (error_message)
        {
            *error_message = QStringLiteral("图片缓存校验失败");
        }
        return false;
    }

    ImageAssetInfo parsed_info;
    QPixmap parsed_pixmap;
    if (!readAndValidateImage(data, &parsed_info, &parsed_pixmap, error_message))
    {
        QFile::remove(path);
        return false;
    }
    parsed_info.asset_id = asset_id;
    parsed_info.asset_ref = asset_id;
    parsed_info.asset_sha256 = actual_sha256;
    parsed_info.local_file_path = path;
    if (!mime_type.isEmpty() && isSupportedMimeType(mime_type))
    {
        parsed_info.mime_type = mime_type;
    }
    *asset_info = parsed_info;
    *pixmap = parsed_pixmap;
    return true;
}

QString ImageAssetManager::sanitizeAssetId(const QString& asset_id) const
{
    QString safe_id;
    safe_id.reserve(asset_id.size());
    for (const QChar character : asset_id)
    {
        if (character.isLetterOrNumber() || character == QLatin1Char('-') || character == QLatin1Char('_'))
        {
            safe_id.append(character);
        }
    }
    if (safe_id.isEmpty())
    {
        safe_id = QStringLiteral("invalid-asset");
    }
    return safe_id;
}

void ImageAssetManager::handleDownloadFinished(QNetworkReply* reply)
{
    const DownloadContext context = _download_contexts.take(reply);
    _pending_downloads.remove(context.asset_id);

    if (!reply || reply->error() != QNetworkReply::NoError)
    {
        emit sigAssetFailed(context.asset_id, QStringLiteral("图片下载失败"));
        if (reply)
        {
            reply->deleteLater();
        }
        return;
    }

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    const QString actual_sha256 = calculateSha256(data);
    if (!context.expected_sha256.isEmpty() && actual_sha256 != context.expected_sha256)
    {
        emit sigAssetFailed(context.asset_id, QStringLiteral("图片下载校验失败"));
        return;
    }

    ImageAssetInfo asset_info;
    QPixmap pixmap;
    QString error_message;
    if (!readAndValidateImage(data, &asset_info, &pixmap, &error_message))
    {
        emit sigAssetFailed(context.asset_id, error_message);
        return;
    }

    asset_info.asset_id = context.asset_id;
    asset_info.asset_ref = context.asset_id;
    asset_info.asset_sha256 = actual_sha256;
    if (!context.mime_type.isEmpty() && isSupportedMimeType(context.mime_type))
    {
        asset_info.mime_type = context.mime_type;
    }
    if (!writeCacheFile(context.asset_id, data, &asset_info.local_file_path))
    {
        emit sigAssetFailed(context.asset_id, QStringLiteral("图片缓存写入失败"));
        return;
    }

    emit sigAssetReady(context.asset_id,
                       pixmap,
                       asset_info.asset_sha256,
                       asset_info.original_size,
                       asset_info.mime_type,
                       asset_info.local_file_path);
}

void ImageAssetManager::handleUploadFinished(QNetworkReply* reply)
{
    const UploadContext context = _upload_contexts.take(reply);
    if (!reply || reply->error() != QNetworkReply::NoError)
    {
        emit sigAssetUploadFailed(context.asset_id, QStringLiteral("图片上传失败"));
        if (reply)
        {
            reply->deleteLater();
        }
        return;
    }

    // PUT 回复正文可能包含服务商 XML，客户端只根据 HTTP 成功状态继续，绝不把正文写入日志或协议。
    reply->deleteLater();
    emit sigAssetUploaded(context.asset_id, context.expected_sha256, context.mime_type);
}
