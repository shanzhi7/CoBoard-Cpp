#pragma once

#include <QObject>
#include <QHash>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QImage;
// ImageAssetInfo 描述图片资源本身，不包含短期签名 URL 或用户本地原始路径。
struct ImageAssetInfo
{
    QString asset_id; // 资源稳定 ID，通常使用内容摘要或网关生成的不可预测 ID(唯一主键（UUID）)。
    QString asset_ref; // 网关或 OSS 使用的资源引用，不能当作访问凭据(oss文件路径)。
    QString asset_sha256; // 图片二进制的 SHA-256 十六进制摘要(文件指纹)。
    QString mime_type; // 经过白名单校验的图片 MIME 类型(文件类型)。
    QSize original_size; // 源文件像素尺寸，用于跨客户端保持几何比例(原始尺寸)。
    qint64 byte_size = 0; // 图片二进制大小，单位为字节。
    QString local_file_path; // 应用缓存中的路径，不参与房间协议广播。
};

// ImageAssetManager 负责图片缓存、完整性校验以及 OSS PUT/GET 的异步传输。
class ImageAssetManager : public QObject
{
    Q_OBJECT

public:
    static constexpr qint64 MAX_FILE_SIZE = 10 * 1024 * 1024; // 单个图片允许的最大二进制大小，超过后在客户端直接拒绝。
    static constexpr int MAX_IMAGE_WIDTH = 4096; // 单个图片允许的最大宽度，防止异常图片占用过多内存。
    static constexpr int MAX_IMAGE_HEIGHT = 4096; // 单个图片允许的最大高度，防止异常图片占用过多内存。

    explicit ImageAssetManager(QObject* parent = nullptr); // 创建使用应用数据目录缓存的资源管理器。
    ~ImageAssetManager() override; // 取消未完成网络请求并释放网络管理对象。

    QString cacheDirectory() const; // 返回图片缓存目录，目录创建失败时返回仍可诊断的目标路径。
    QString buildCachePath(const QString& asset_id) const; // 根据安全资源 ID 构造缓存文件路径，阻止路径穿越。
    QString calculateSha256(const QByteArray& data) const; // 计算图片二进制的 SHA-256 十六进制摘要。
    bool isSupportedMimeType(const QString& mime_type) const; // 判断 MIME 是否属于客户端允许的图片白名单。

    bool prepareLocalAsset(const QString& file_path, ImageAssetInfo* asset_info, QPixmap* pixmap, QString* error_message = nullptr); // 同步校验本地图片，供非 UI 调用方兼容使用。
    void prepareLocalAssetAsync(const QString& file_path, const QString& request_id); // 在线程池读取、解码并缓存本地图片，完成信号回到所属 UI 线程。
    void PrepareImageDataAsync(const QImage& image, const QString& request_id); // 在线程池编码、校验并缓存剪贴板图片数据。
    void downloadAsset(const QString& asset_id, const QUrl& download_url, const QString& expected_sha256, const QString& mime_type = QString()); // 异步下载资源，命中缓存时不会访问网络。
    void uploadAsset(const QString& asset_id, const QString& file_path, const QUrl& upload_url, const QString& mime_type, const QString& expected_sha256, qint64 expected_byte_size); // 使用已校验摘要和大小异步上传，避免 UI 线程重复读取文件。

signals:
    void sigAssetReady(QString asset_id, QPixmap pixmap, QString sha256, QSize original_size, QString mime_type, QString local_file_path); // 图片下载或缓存读取成功时发送已校验的像素和元数据。
    void sigAssetFailed(QString asset_id, QString error_message); // 图片读取、下载、校验或解码失败时发送不含敏感响应正文的原因。
    void sigLocalAssetReady(QString request_id, QString file_path, QString asset_id, QString asset_ref, QString sha256, QString mime_type, QSize original_size, qint64 byte_size, QPixmap pixmap, QString local_file_path); // 后台完成本地校验后把像素和摘要交回 UI 线程。
    void sigLocalAssetFailed(QString request_id, QString file_path, QString error_message); // 后台读取或校验失败时通知发起方，失败结果不会创建图元。
    void sigAssetUploaded(QString asset_id, QString sha256, QString mime_type); // OSS PUT 成功后通知调用方可以发送图片图元操作。
    void sigAssetUploadFailed(QString asset_id, QString error_message); // OSS PUT 失败时通知调用方，不会继续广播未完成资源。

private:
    struct DownloadContext
    {
        QString asset_id; // 当前请求对应的资源 ID。
        QString expected_sha256; // 调用方声明的摘要，非空时必须严格匹配。
        QString mime_type; // 调用方声明的 MIME，空值时由图片内容推断。
    };

    struct UploadContext
    {
        QString asset_id; // 当前上传对应的资源 ID。
        QString expected_sha256; // 上传前已由本地文件计算的摘要。
        QString mime_type; // PUT 请求的 Content-Type。
    };

    bool readAndValidateImage(const QByteArray& data, ImageAssetInfo* asset_info, QPixmap* pixmap, QString* error_message) const; // 校验大小、格式、尺寸并解码图片。
    bool writeCacheFile(const QString& asset_id, const QByteArray& data, QString* cache_path) const; // 原子写入缓存文件，避免进程中断留下半文件。
    bool readCachedAsset(const QString& asset_id, const QString& expected_sha256, const QString& mime_type, ImageAssetInfo* asset_info, QPixmap* pixmap, QString* error_message) const; // 读取缓存并重复完整性校验。
    QString sanitizeAssetId(const QString& asset_id) const; // 将外部资源 ID 转换为安全的单级文件名。
    void handleDownloadFinished(QNetworkReply* reply); // 处理下载结束、校验响应和缓存结果。
    void handleUploadFinished(QNetworkReply* reply); // 处理上传结束并释放文件对象。

    QNetworkAccessManager* _network_manager = nullptr; // 所有请求均在客户端 UI 线程异步执行，避免阻塞绘图事件循环。
    QHash<QNetworkReply*, DownloadContext> _download_contexts; // 保存下载回复到资源元数据的映射，直到 finished 信号处理完成。
    QHash<QNetworkReply*, UploadContext> _upload_contexts; // 保存上传回复到资源元数据的映射，直到 finished 信号处理完成。
    QSet<QString> _pending_downloads; // 防止同一个资源因多个图元同时出现而重复下载。
};
