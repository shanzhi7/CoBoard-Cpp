#include "canvastcpworker.h"

#include <limits>
#include <utility>

#include <QDataStream>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkProxy>
#include <QTcpSocket>
#include <QThread>
#include <QDebug>
#include <QtEndian>

#include "global.h"

namespace
{
template<typename T>
bool DecodeProtobuf(const QByteArray& body, DecodedTcpMessage::Payload& payload)
{
    // 1. 解码到独立消息，失败时不交付半成品。
    T response;
    if (!response.ParseFromArray(body.constData(), static_cast<int>(body.size())))
    {
        return false;
    }
    payload = std::move(response);
    return true;
}
}

CanvasTcpWorker::CanvasTcpWorker(QObject* parent) : QObject(parent)
{
    // 1. 构造期间不创建网络资源，moveToThread 后再处理连接命令。
}

CanvasTcpWorker::~CanvasTcpWorker()
{
    // 1. finished 触发的延迟删除仍在所属线程执行，不依赖 UI 或应用事件循环。
    CloseSocket();
}

void CanvasTcpWorker::ConnectToServer(QString host, quint16 port, quint64 generation)
{
    // 1. 旧 socket 的断开事件不参与新连接的重试和退出事务。
    Q_ASSERT(QThread::currentThread() == thread());
    CloseSocket();
    _generation = generation;
    _socket = new QTcpSocket(this);
    _socket->setProxy(QNetworkProxy::NoProxy);

    // 2. 所有网络回调和分帧解码均由当前工作线程执行。
    connect(_socket, &QTcpSocket::connected, this, [this]() {
        _socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        emit SigConnected(_generation);
    });
    connect(_socket, &QTcpSocket::readyRead, this, &CanvasTcpWorker::ReadFrames);
    connect(_socket, &QTcpSocket::disconnected, this, [this]() {
        _buffer.clear();
        emit SigDisconnected(_generation);
    });
    connect(_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        emit SigSocketError(_generation, error, _socket->errorString());
    });

    // 3. 依靠线程事件循环建立连接，不使用阻塞等待。
    _socket->connectToHost(host, port);
}

void CanvasTcpWorker::SendData(quint16 message_id, QByteArray body, quint64 generation)
{
    // 1. 旧代次请求不能写入重定向后的连接，包长必须能由现有两字节字段表示。
    Q_ASSERT(QThread::currentThread() == thread());
    if (generation != _generation || !_socket)
    {
        return;
    }
    if (body.size() > std::numeric_limits<quint16>::max())
    {
        qWarning() << "[CanvasTcpWorker] SendData: oversized message, id=" << message_id;
        return;
    }

    // 2. 保持网络字节序和请求顺序，QTcpSocket 自身缓存尚未写完的数据。
    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << message_id << static_cast<quint16>(body.size());
    frame.append(body);
    if (_socket->write(frame) == -1)
    {
        emit SigSocketError(_generation, _socket->error(), _socket->errorString());
    }
}

void CanvasTcpWorker::DisconnectFromServer(quint64 generation)
{
    // 1. 注销先推进代次，再释放连接和未完成帧。
    Q_ASSERT(QThread::currentThread() == thread());
    _generation = generation;
    CloseSocket();
}

void CanvasTcpWorker::CloseSocket()
{
    // 1. 主动替换连接时不向 UI 发送旧连接的 disconnected。
    if (_socket)
    {
        _socket->disconnect(this);
        _socket->abort();
        delete _socket;
        _socket = nullptr;
    }
    _buffer.clear();
}

void CanvasTcpWorker::ReadFrames()
{
    // 1. 头和体均留在缓冲区中等待收齐，避免半包状态跨连接残留。
    Q_ASSERT(QThread::currentThread() == thread());
    _buffer.append(_socket->readAll());
    constexpr qsizetype header_size = sizeof(quint16) * 2;
    qsizetype offset = 0;
    while (_buffer.size() - offset >= header_size)
    {
        const auto* header = _buffer.constData() + offset;
        const auto message_id = qFromBigEndian<quint16>(header);
        const auto body_size = qFromBigEndian<quint16>(header + sizeof(quint16));
        if (_buffer.size() - offset - header_size < body_size)
        {
            break;
        }

        // 2. 一次回调顺序解码所有完整帧，只在最后移动剩余缓存。
        DecodeMessage(message_id, _buffer.mid(offset + header_size, body_size));
        offset += header_size + body_size;
    }
    _buffer.remove(0, offset);
}

void CanvasTcpWorker::DecodeMessage(quint16 message_id, const QByteArray& body)
{
    // 1. 消息编号决定唯一载荷类型，UI 不再接触原始字节或二次解析。
    DecodedTcpMessage decoded;
    decoded._message_id = message_id;
    bool is_valid = false;
    switch (message_id)
    {
    case ID_CANVAS_LOGIN_RSP:
    case ID_CREAT_ROOM_RSP:
    {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(body, &error);
        is_valid = error.error == QJsonParseError::NoError && document.isObject();
        if (is_valid)
        {
            decoded._payload = document.object();
        }
        break;
    }
    case ID_JOIN_ROOM_RSP:
        is_valid = DecodeProtobuf<message::JoinRoomRsp>(body, decoded._payload);
        break;
    case ID_LEAVE_ROOM_RSP:
        is_valid = DecodeProtobuf<message::LeaveRoomRsp>(body, decoded._payload);
        break;
    case ID_DESTROY_ROOM_RSP:
        is_valid = DecodeProtobuf<message::DestroyRoomRsp>(body, decoded._payload);
        break;
    case ID_ROOM_DESTROYED_BROADCAST:
        is_valid = DecodeProtobuf<message::RoomDestroyedBroadcast>(body, decoded._payload);
        break;
    case ID_USER_JOIN_BROADCAST:
        is_valid = DecodeProtobuf<message::UserJoinRoomBroadcast>(body, decoded._payload);
        break;
    case ID_USER_LEAVE_BROADCAST:
        is_valid = DecodeProtobuf<message::UserLeaveRoomBroadcast>(body, decoded._payload);
        break;
    case ID_DRAW_RSP:
        is_valid = DecodeProtobuf<message::DrawReq>(body, decoded._payload);
        break;
    case ID_IMAGE_OPERATION_RSP:
        is_valid = DecodeProtobuf<message::ImageOperation>(body, decoded._payload);
        break;
    case ID_ITEM_EDIT_RSP:
        is_valid = DecodeProtobuf<message::ItemEditOperationRsp>(body, decoded._payload) &&
                   std::get<message::ItemEditOperationRsp>(decoded._payload).has_operation();
        break;
    case ID_ITEM_CREATE_BATCH_RSP:
        is_valid = DecodeProtobuf<message::ItemCreateBatchRsp>(body, decoded._payload) &&
                   std::get<message::ItemCreateBatchRsp>(decoded._payload).has_batch();
        break;
    case ID_TEXT_OPERATION_RSP:
        is_valid = DecodeProtobuf<message::TextOperationRsp>(body, decoded._payload) &&
                   std::get<message::TextOperationRsp>(decoded._payload).has_operation();
        break;
    case ID_GRANT_EDIT_RSP:
        is_valid = DecodeProtobuf<message::GrantEditRsp>(body, decoded._payload);
        break;
    case ID_REVOKE_EDIT_RSP:
        is_valid = DecodeProtobuf<message::RevokeEditRsp>(body, decoded._payload);
        break;
    case ID_PERMISSION_CHANGED_BROADCAST:
        is_valid = DecodeProtobuf<message::PermissionChangedBroadcast>(body, decoded._payload);
        break;
    case ID_CHAT_RSP:
        is_valid = DecodeProtobuf<message::ChatRsp>(body, decoded._payload);
        break;
    default:
        qWarning() << "[CanvasTcpWorker] DecodeMessage: unknown id=" << message_id;
        return;
    }

    // 2. 失败帧不影响后续边界，不记录含 Token 等敏感内容的原始包体。
    if (!is_valid)
    {
        qWarning() << "[CanvasTcpWorker] DecodeMessage: invalid payload, id=" << message_id;
        emit SigDecodeFailed(_generation, message_id);
        return;
    }
    emit SigMessageDecoded(_generation, std::move(decoded));
}
