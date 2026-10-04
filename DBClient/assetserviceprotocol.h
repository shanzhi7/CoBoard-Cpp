/***********************************************************************************
* @file         assetserviceprotocol.h
* @brief        图片代理协议、分帧和资源校验公共类型
* @author       shanzhi
* @date         2026/10/04
* @history
***********************************************************************************/
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QSharedPointer>
#include <QSize>
#include <QString>

class QLocalSocket;
class AssetCacheLease;

struct AssetCacheRecord
{
    QJsonObject ToJson() const; // 返回不含缓存路径的元数据。
    static AssetCacheRecord FromJson(const QJsonObject& object); // 解析图片元数据。
    QString _asset_sha256; // 内容寻址主键。
    QString _mime_type; // 内容识别后的 MIME。
    QSize _original_size; // 原始像素尺寸。
    qint64 _byte_size = 0; // 编码数据大小。
};

struct AssetCacheResult
{
    QString _status = QStringLiteral("error"); // ok、miss 或 error。
    QString _error_code = QStringLiteral("CacheError"); // 失败类型。
    QString _error_message; // 不含凭据的失败原因。
    AssetCacheRecord _record; // 校验后的元数据。
    QJsonObject _payload; // 缓存统计等附加结果。
    QByteArray _data; // 原始图片编码。
    QString _file_path; // 仅服务内部使用的路径。
    QSharedPointer<AssetCacheLease> _lease; // 文件保护引用。
};

namespace AssetServiceProtocol
{
constexpr int VERSION = 1; // 协议版本。
constexpr qsizetype MAX_JSON_BYTES = 64 * 1024; // 控制消息上限。
constexpr qsizetype MAX_IMAGE_BYTES = 10 * 1024 * 1024; // 单图片上限。
constexpr qsizetype MAX_BUFFER_BYTES = 16 * 1024 * 1024; // 连接缓冲上限。
constexpr int MAX_PENDING_REQUESTS = 16; // 待处理请求上限。
constexpr int MAX_IMAGE_DIMENSION = 4096; // 长宽上限。
QString CacheRoot(); // 返回一致的应用数据根目录。
QString RootDigest(); // 返回根目录摘要。
QString ServerName(); // 返回当前缓存对应的管道名。
QString NewId(); // 创建无花括号 UUID。
bool IsSha256(const QString& value); // 校验安全摘要文件名。
AssetCacheResult ValidateImage(const QByteArray& data); // 校验并解码确认图片有效。
QJsonObject Response(const QString& request_id, const QString& status,
    const QString& error_code = QString(), const QString& message = QString(),
    const QJsonObject& payload = QJsonObject()); // 创建统一响应。
QByteArray EncodeFrame(const QJsonObject& message, const QByteArray& binary = QByteArray()); // 编码大端长度头。
bool TakeFrame(QByteArray& buffer, QJsonObject* message, QByteArray* binary, QString* error); // 半包保持原样，错误另行返回。
}

class AssetIpcChannel : public QObject
{
    Q_OBJECT
public:
    explicit AssetIpcChannel(QLocalSocket* socket, QObject* parent = nullptr); // 接管套接字和缓冲。
    bool Send(const QJsonObject& message, const QByteArray& binary = QByteArray()); // 有界排队并发送。
    bool CanSend(const QJsonObject& message, qsizetype binary_size = 0) const; // 在创建句柄前检查发送预算。
    QLocalSocket* Socket() const; // 返回所属套接字。
signals:
    void sigFrameReady(QJsonObject message, QByteArray binary); // 完整消息到达。
    void sigProtocolError(QString message); // 协议错误。
private:
    void ReadFrames(); // 循环处理半包和粘包。
    void FlushWrites(); // 根据背压续写。
    void Fail(const QString& message); // 报错并中止连接。
    QLocalSocket* _socket = nullptr; // 所属线程内的套接字。
    QByteArray _receive_buffer; // 未完成接收帧。
    QQueue<QByteArray> _write_queue; // 待发送帧。
    qsizetype _write_offset = 0; // 队首发送偏移。
    qint64 _queued_bytes = 0; // 尚未提交的字节数。
};
