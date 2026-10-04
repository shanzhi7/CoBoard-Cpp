/***********************************************************************************
* @file         imageassetmanager.h
* @brief        画板图片的 IPC 门面及 UI 后台编解码
* @author       shanzhi
* @date         2026/10/04
* @history
***********************************************************************************/
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QThreadPool>
#include <QUrl>

struct ImageAssetInfo
{
    QString asset_id; // 房间资源稳定 ID。
    QString asset_ref; // OSS 对象引用，不包含签名。
    QString asset_sha256; // 内容摘要。
    QString mime_type; // 内容识别 MIME。
    QSize original_size; // 原始像素尺寸。
    qint64 byte_size = 0; // 编码图片字节数。
    QString _asset_handle; // 当前 IPC 连接绑定的缓存保护句柄。
};
Q_DECLARE_METATYPE(ImageAssetInfo)

class ImageAssetManager : public QObject
{
    Q_OBJECT
public:
    static constexpr qint64 MAX_FILE_SIZE = 10 * 1024 * 1024; // 编码图片上限。
    static constexpr int MAX_IMAGE_WIDTH = 4096; // 最大图片宽度。
    static constexpr int MAX_IMAGE_HEIGHT = 4096; // 最大图片高度。
    explicit ImageAssetManager(QObject* parent = nullptr); // 连接进程共享代理。
    ~ImageAssetManager() override; // 取消所属任务及回收编解码线程。
    void prepareLocalAssetAsync(const QString& file_path, const QString& request_id); // 保留已有导入入口，原始文件由代理读取。
    void PrepareImageDataAsync(const QImage& image, const QString& request_id); // 后台编码 PNG，再提交代理。
    void LoadAssetAsync(const QString& asset_id, const QString& sha256, const QString& mime_type); // 签名前查缓存。
    void downloadAsset(const QString& asset_id, const QUrl& url, const QString& sha256, const QString& mime_type = QString()); // 请求代理下载。
    void uploadAsset(const QString& asset_id, const QString& asset_handle, const QUrl& url, const QString& mime_type, const QString& sha256, qint64 byte_size); // 使用受保护句柄上传。
    void ReleaseAsset(const QString& asset_handle); // 离线导入、废弃结果及上传完成解除保护。
    void CancelAssetUpload(const QString& asset_handle); // 删除未广播图元时取消对应 PUT。
    void TouchAssetAsync(const QString& sha256); // 预览时更新 LRU。
    void CancelAll(); // 房间切换取消请求、句柄和迟到解码结果。
signals:
    void sigAssetReady(QString asset_id, QPixmap pixmap, QString sha256, QSize original_size, QString mime_type); // 已校验图片在 UI 线程交付。
    void sigAssetCacheMiss(QString asset_id); // 仅此时向网关获取 GET 签名。
    void sigAssetFailed(QString asset_id, QString error_message); // 加载终态失败。
    void sigLocalAssetReady(QString request_id, QString file_path, ImageAssetInfo asset_info, QPixmap pixmap); // 本地导入结果包含句柄。
    void sigLocalAssetFailed(QString request_id, QString file_path, QString error_message); // 导入失败不创建图元。
    void sigAssetUploaded(QString asset_id, QString sha256, QString mime_type); // PUT 成功后可广播图片图元。
    void sigAssetUploadFailed(QString asset_id, QString error_message); // 上传失败。
    void sigServiceLost(); // 代理重启使所有等待签名的句柄失效。
private:
    struct RequestContext
    {
        QString _command; // 指令类型。
        QString _request_id; // Canvas 本地导入关联。
        QString _source_path; // 原始路径，剪贴板为空。
        QString _asset_id; // 房间资源 ID。
        QString _sha256; // 预期摘要。
        QString _mime_type; // 预期 MIME。
        QString _asset_handle; // 上传保护句柄。
    };
    void Submit(const RequestContext& context, const QJsonObject& payload, const QByteArray& binary = QByteArray()); // 登记异步请求。
    void ReceiveResponse(const QString& request_id, const QJsonObject& response, const QByteArray& binary); // 处理结果并后台解码。
    void Fail(const RequestContext& context, const QString& message); // 发送对应业务失败。
    QHash<QString, RequestContext> _requests; // IPC UUID 到业务上下文。
    QSet<QString> _asset_handles; // 本门面持有的引用。
    QSet<QString> _pending_downloads; // 同一资源只请求一次。
    QThreadPool _prepare_thread_pool; // 两个编解码线程。
    quint64 _generation = 0; // 房间切换废弃后台结果。
    quint64 _service_generation = 0; // 代理重启废弃尚未解码的旧句柄。
};
