#include "assetserviceprotocol.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QImageReader>
#include <QImage>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>
#include <QTimer>
#include <QtEndian>

QJsonObject AssetCacheRecord::ToJson() const
{
    // 1. 缓存路径和凭据不进入协议。
    return {{"sha256", _asset_sha256}, {"mimeType", _mime_type}, {"width", _original_size.width()},
        {"height", _original_size.height()}, {"byteSize", _byte_size}};
}

AssetCacheRecord AssetCacheRecord::FromJson(const QJsonObject& object)
{
    // 1. 保持整数尺寸和字节数。
    AssetCacheRecord record; // 元数据。
    record._asset_sha256 = object.value("sha256").toString().toLower();
    record._mime_type = object.value("mimeType").toString();
    record._original_size = QSize(object.value("width").toInt(), object.value("height").toInt());
    record._byte_size = object.value("byteSize").toInteger();
    return record;
}

QString AssetServiceProtocol::CacheRoot()
{
    // 1. 测试使用独立根目录，正常运行保持旧目录位置。
    const QString override_path = QCoreApplication::instance()->property("assetServiceRoot").toString(); // 测试覆盖。
    return QDir::cleanPath(QDir(override_path.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : override_path).absolutePath());
}

QString AssetServiceProtocol::RootDigest()
{
    // 1. Windows 路径大小写不能产生两个管道。
    QString root = CacheRoot(); // 稳定路径。
#ifdef Q_OS_WIN
    root = root.toLower();
#endif
    return QString::fromLatin1(QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString AssetServiceProtocol::ServerName()
{
    // 1. 版本通过握手验证。
    return QStringLiteral("SyncCanvas.AssetService.") + RootDigest().left(24);
}

QString AssetServiceProtocol::NewId()
{
    // 1. 避免并发请求及句柄碰撞。
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool AssetServiceProtocol::IsSha256(const QString& value)
{
    // 1. 文件名严格限定为内容摘要。
    if (value.size() != 64)
    {
        return false;
    }
    for (const QChar character : value)
    {
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
        {
            return false;
        }
    }
    return true;
}

AssetCacheResult AssetServiceProtocol::ValidateImage(const QByteArray& data)
{
    // 1. 在解码前限制编码大小和像素尺寸。
    AssetCacheResult result; // 校验结果。
    result._error_code = QStringLiteral("InvalidImage");
    result._error_message = QStringLiteral("图片格式、大小或尺寸不符合要求");
    if (data.isEmpty() || data.size() > MAX_IMAGE_BYTES)
    {
        return result;
    }
    QBuffer buffer; // 内容读取设备。
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer); // 当前线程解码器。
    reader.setDecideFormatFromContent(true);
    const QByteArray format = reader.format().toLower(); // 实际格式。
    const QSize size = reader.size(); // 解码前尺寸。
    if ((format != "png" && format != "jpg" && format != "jpeg" && format != "webp") ||
        !size.isValid() || size.width() > MAX_IMAGE_DIMENSION || size.height() > MAX_IMAGE_DIMENSION)
    {
        return result;
    }
    // 2. 解码确认内容完整，协议传输仍使用原编码。
    const QImage image = reader.read(); // 已解码像素。
    if (image.isNull() || image.size() != size)
    {
        return result;
    }
    result._record._asset_sha256 = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    result._record._mime_type = format == "webp" ? QStringLiteral("image/webp")
        : format == "png" ? QStringLiteral("image/png") : QStringLiteral("image/jpeg");
    result._record._original_size = size;
    result._record._byte_size = data.size();
    result._data = data;
    result._status = QStringLiteral("ok");
    result._error_code.clear();
    result._error_message.clear();
    return result;
}

QJsonObject AssetServiceProtocol::Response(const QString& request_id, const QString& status,
    const QString& error_code, const QString& message, const QJsonObject& payload)
{
    // 1. 回显请求 ID，不依赖完成顺序。
    return {{"version", VERSION}, {"requestId", request_id}, {"status", status},
        {"errorCode", error_code}, {"message", message}, {"payload", payload}};
}

QByteArray AssetServiceProtocol::EncodeFrame(const QJsonObject& message, const QByteArray& binary)
{
    // 1. 上限校验后构造固定帧头。
    const QByteArray json = QJsonDocument(message).toJson(QJsonDocument::Compact); // UTF-8 消息。
    if (json.isEmpty() || json.size() > MAX_JSON_BYTES || binary.size() > MAX_IMAGE_BYTES)
    {
        return QByteArray();
    }
    QByteArray frame(8, '\0'); // 长度头。
    qToBigEndian<quint32>(static_cast<quint32>(json.size()), frame.data());
    qToBigEndian<quint32>(static_cast<quint32>(binary.size()), frame.data() + 4);
    frame.append(json);
    frame.append(binary);
    return frame;
}

bool AssetServiceProtocol::TakeFrame(QByteArray& buffer, QJsonObject* message, QByteArray* binary, QString* error)
{
    // 1. 先校验长度，半包不消费。
    error->clear();
    if (buffer.size() < 8)
    {
        return false;
    }
    const quint32 json_size = qFromBigEndian<quint32>(buffer.constData()); // JSON 长度。
    const quint32 binary_size = qFromBigEndian<quint32>(buffer.constData() + 4); // 图片长度。
    if (!json_size || json_size > MAX_JSON_BYTES || binary_size > MAX_IMAGE_BYTES)
    {
        *error = QStringLiteral("IPC 帧长度超限");
        return false;
    }
    const qsizetype frame_size = 8 + static_cast<qsizetype>(json_size) + binary_size; // 安全总长度。
    if (buffer.size() < frame_size)
    {
        return false;
    }
    // 2. 使用解析器验证对象并保留后续粘包。
    QJsonParseError parse_error; // 解析状态。
    const QJsonDocument document = QJsonDocument::fromJson(buffer.mid(8, json_size), &parse_error); // 控制对象。
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        *error = QStringLiteral("IPC JSON 格式错误");
        return false;
    }
    *message = document.object();
    *binary = buffer.mid(8 + json_size, binary_size);
    buffer.remove(0, frame_size);
    return true;
}

AssetIpcChannel::AssetIpcChannel(QLocalSocket* socket, QObject* parent)
    : QObject(parent), _socket(socket)
{
    // 1. 套接字只在所属事件线程使用。
    _socket->setParent(this);
    _socket->setReadBufferSize(256 * 1024);
    connect(_socket, &QLocalSocket::readyRead, this, &AssetIpcChannel::ReadFrames);
    connect(_socket, &QLocalSocket::bytesWritten, this, &AssetIpcChannel::FlushWrites);
    QTimer::singleShot(0, this, &AssetIpcChannel::ReadFrames);
}

QLocalSocket* AssetIpcChannel::Socket() const
{
    // 1. 返回所属套接字。
    return _socket;
}

bool AssetIpcChannel::Send(const QJsonObject& message, const QByteArray& binary)
{
    // 1. Qt 发送缓冲也计入容量。
    const QByteArray frame = AssetServiceProtocol::EncodeFrame(message, binary); // 待发帧。
    if (frame.isEmpty() || _queued_bytes + _socket->bytesToWrite() + frame.size() > AssetServiceProtocol::MAX_BUFFER_BYTES)
    {
        Fail(QStringLiteral("IPC 发送缓冲超限"));
        return false;
    }
    _write_queue.enqueue(frame);
    _queued_bytes += frame.size();
    FlushWrites();
    return true;
}

bool AssetIpcChannel::CanSend(const QJsonObject& message, qsizetype binary_size) const
{
    // 1. Qt 和应用发送缓冲共同计入十六 MiB 上限。
    const qsizetype json_size = QJsonDocument(message).toJson(QJsonDocument::Compact).size(); // 控制消息大小。
    return _socket->state() == QLocalSocket::ConnectedState && json_size <= AssetServiceProtocol::MAX_JSON_BYTES
        && binary_size <= AssetServiceProtocol::MAX_IMAGE_BYTES
        && _queued_bytes + _socket->bytesToWrite() + 8 + json_size + binary_size <= AssetServiceProtocol::MAX_BUFFER_BYTES;
}

void AssetIpcChannel::ReadFrames()
{
    // 1. 分块读取并及时消费粘包。
    while (_socket->bytesAvailable() > 0)
    {
        _receive_buffer.append(_socket->read(64 * 1024));
        if (_receive_buffer.size() > AssetServiceProtocol::MAX_BUFFER_BYTES)
        {
            Fail(QStringLiteral("IPC 接收缓冲超限"));
            return;
        }
        QJsonObject message; // 控制消息。
        QByteArray binary; // 编码图片。
        QString error; // 分帧错误。
        while (AssetServiceProtocol::TakeFrame(_receive_buffer, &message, &binary, &error))
        {
            emit sigFrameReady(message, binary);
            if (_socket->state() == QLocalSocket::UnconnectedState)
            {
                return;
            }
        }
        if (!error.isEmpty())
        {
            Fail(error);
            return;
        }
    }
}

void AssetIpcChannel::FlushWrites()
{
    // 1. 保留发送偏移，积压时等待 bytesWritten。
    while (!_write_queue.isEmpty() && _socket->state() == QLocalSocket::ConnectedState && _socket->bytesToWrite() < 256 * 1024)
    {
        const QByteArray& frame = _write_queue.head(); // 队首帧。
        const qint64 count = _socket->write(frame.constData() + _write_offset,
            qMin<qsizetype>(64 * 1024, frame.size() - _write_offset)); // 实际接收长度。
        if (count <= 0)
        {
            if (count < 0)
            {
                Fail(QStringLiteral("IPC 写入失败"));
            }
            return;
        }
        _write_offset += count;
        _queued_bytes -= count;
        if (_write_offset == frame.size())
        {
            _write_queue.dequeue();
            _write_offset = 0;
        }
    }
}

void AssetIpcChannel::Fail(const QString& message)
{
    // 1. 通知上层回收连接资源。
    _receive_buffer.clear();
    _write_queue.clear();
    _write_offset = 0;
    _queued_bytes = 0;
    emit sigProtocolError(message);
    _socket->abort();
}
