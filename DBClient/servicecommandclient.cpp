#include "servicecommandclient.h"

#include <utility>

#include <QCoreApplication>
#include <QDebug>
#include <QLocalSocket>
#include <QProcess>
#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

ServiceCommandClient::ServiceCommandClient()
{
    // 1. 所有探测使用事件循环，UI 不调用 waitForConnected。
    _retry_timer.setSingleShot(true);
    _timeout_timer.setInterval(100);
    connect(&_retry_timer, &QTimer::timeout, this, [this] {
        if (_connection_state == "Initializing")
        {
            SendHello();
        } else
        {
            ConnectAttempt();
        }
    });
    connect(&_timeout_timer, &QTimer::timeout, this, &ServiceCommandClient::Maintain);
}

ServiceCommandClient::~ServiceCommandClient()
{
    // 1. 不允许 Qt 应用退出时重新拉起代理。
    Disconnect();
}

void ServiceCommandClient::EnsureService()
{
    // 1. 已有启动周期共用同一连接。
    if (_wants_service && (_connection_state != "Disconnected" || _retry_timer.isActive()))
    {
        return;
    }
    _wants_service = true;
    _has_launched = false;
    _connection_elapsed.start();
    _initialization_elapsed.invalidate();
    _timeout_timer.start();
    ConnectAttempt();
}

QString ServiceCommandClient::SendCommand(const QString& command, const QJsonObject& payload, const QByteArray& binary)
{
    // 1. ID 立即返回，但任何响应延迟到调用方登记上下文之后。
    const QString request_id = AssetServiceProtocol::NewId(); // 跨窗口唯一。
    qint64 queued_bytes = binary.size(); // 本进程排队预算。
    int business_count = 0; // 释放和维护不能被图片任务挤占。
    for (const auto& request : std::as_const(_pending_requests))
    {
        queued_bytes += request._binary.size();
        const QString pending_command = request._message.value("command").toString(); // 维护指令不占业务槽。
        business_count += pending_command != "ReleaseAsset" && pending_command != "TouchAsset";
    }
    const bool is_maintenance = command == "ReleaseAsset" || command == "TouchAsset"; // 确保句柄可释放。
    if ((!is_maintenance && business_count >= AssetServiceProtocol::MAX_PENDING_REQUESTS) || _pending_requests.size() >= 128 || queued_bytes > AssetServiceProtocol::MAX_BUFFER_BYTES
        || binary.size() > AssetServiceProtocol::MAX_IMAGE_BYTES)
    {
        QTimer::singleShot(0, this, [this, request_id] {
            emit sigResponseReady(request_id, AssetServiceProtocol::Response(request_id, "error", "Busy", "图片请求排队已满"), {});
        });
        return request_id;
    }
    PendingRequest request; // 请求上下文。
    request._message = {{"version", AssetServiceProtocol::VERSION}, {"requestId", request_id}, {"command", command}, {"payload", payload}};
    request._binary = binary;
    request._elapsed.start();
    _pending_requests.insert(request_id, request);
    QTimer::singleShot(0, this, [this] {
        EnsureService();
        PumpRequests();
    });
    return request_id;
}

void ServiceCommandClient::ConnectAttempt()
{
    // 1. 旧通道立即断开信号，防止迟到错误影响新连接。
    if (!_wants_service || _connection_state == "Ready")
    {
        return;
    }
    if (_channel)
    {
        _channel->disconnect(this);
        _socket->disconnect(this);
        _socket->abort();
        _channel->deleteLater();
    }
    _socket = new QLocalSocket;
    _channel = new AssetIpcChannel(_socket, this);
    _connection_state = "Connecting";
    _probe_elapsed.start();
    connect(_channel, &AssetIpcChannel::sigFrameReady, this, &ServiceCommandClient::ReceiveResponse);
    connect(_channel, &AssetIpcChannel::sigProtocolError, this, [this](QString reason) { ConnectionFailed(reason, false); });
    connect(_socket, &QLocalSocket::connected, this, [this] {
        _connection_state = "Handshaking";
        SendHello();
    });
    connect(_socket, &QLocalSocket::disconnected, this, [this] {
        if (_connection_state == "Ready" || _connection_state == "Initializing" || _connection_state == "Handshaking")
        {
            ConnectionFailed("图片后台服务连接断开");
        }
    });
    connect(_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError error) {
        if (_connection_state != "Connecting")
        {
            return;
        }
        if (error == QLocalSocket::ServerNotFoundError || error == QLocalSocket::ConnectionRefusedError)
        {
            LaunchService();
            _retry_timer.start(200);
        } else
        {
            ConnectionFailed("无法连接图片服务，请检查权限", false);
        }
    });
    _socket->connectToServer(AssetServiceProtocol::ServerName());
}

void ServiceCommandClient::LaunchService()
{
    // 1. 多 UI 可同时启动候选者，服务内部的 QLockFile 决定唯一掌控者。
    if (_has_launched || !_wants_service)
    {
        return;
    }
    _has_launched = true;
    QProcess process; // detached 进程不依赖首个 UI 存活。
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments({"--asset-service", "--asset-root", AssetServiceProtocol::CacheRoot()});
    process.setWorkingDirectory(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
        arguments->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        arguments->startupInfo->wShowWindow = SW_HIDE;
    });
#endif
    qint64 service_pid = 0; // 日志不含路径和凭据。
    if (!process.startDetached(&service_pid))
    {
        ConnectionFailed("无法启动图片后台服务", false);
        return;
    }
    qDebug() << "ServiceCommandClient Launch pid=" << QCoreApplication::applicationPid() << "servicePid=" << service_pid;
}

void ServiceCommandClient::SendHello()
{
    // 1. 每轮初始化状态查询使用新 UUID。
    if (!_channel || _socket->state() != QLocalSocket::ConnectedState)
    {
        return;
    }
    _hello_request_id = AssetServiceProtocol::NewId();
    _channel->Send({{"version", AssetServiceProtocol::VERSION}, {"requestId", _hello_request_id},
        {"command", "Hello"}, {"payload", QJsonObject{{"rootDigest", AssetServiceProtocol::RootDigest()}}}});
}

void ServiceCommandClient::ReceiveResponse(const QJsonObject& response, const QByteArray& binary)
{
    // 1. 协议身份只能在握手中建立，不接受相同管道名的陌生服务。
    const QString request_id = response.value("requestId").toString(); // 回显关联。
    if (request_id == _hello_request_id)
    {
        const QJsonObject payload = response.value("payload").toObject(); // 服务身份。
        if (response.value("status").toString() != "ok" || response.value("version").toInt() != AssetServiceProtocol::VERSION
            || payload.value("service").toString() != "SyncCanvas.AssetService" || payload.value("protocolVersion").toInt() != AssetServiceProtocol::VERSION
            || payload.value("rootDigest").toString() != AssetServiceProtocol::RootDigest() || payload.value("instanceId").toString().isEmpty())
        {
            ConnectionFailed("图片服务身份或协议版本不兼容", false);
            return;
        }
        if (payload.value("state").toString() == "Initializing")
        {
            _connection_state = "Initializing";
            if (!_initialization_elapsed.isValid())
            {
                _initialization_elapsed.start();
            }
            _retry_timer.start(200);
            return;
        }
        if (payload.value("state").toString() != "Ready")
        {
            ConnectionFailed("图片服务当前不可用", false);
            return;
        }
        _connection_state = "Ready";
        _service_instance_id = payload.value("instanceId").toString();
        _retry_timer.stop();
        qDebug() << "ServiceCommandClient Hello pid=" << QCoreApplication::applicationPid() << "instance=" << _service_instance_id;
        emit sigServiceReady(_service_instance_id);
        PumpRequests();
        return;
    }
    // 2. 只有本进程仍在等待的 UUID 才能分发结果。
    if (response.value("version").toInt() != AssetServiceProtocol::VERSION)
    {
        ConnectionFailed("图片响应协议版本无效", false);
        return;
    }
    if (_pending_requests.remove(request_id))
    {
        emit sigResponseReady(request_id, response, binary);
    }
}

void ServiceCommandClient::PumpRequests()
{
    // 1. 完成 Ready 握手才提交业务，断开时不自动重放上传。
    if (_connection_state != "Ready")
    {
        return;
    }
    const auto keys = _pending_requests.keys(); // 发送可能触发协议错误并清空映射。
    for (const QString& request_id : keys)
    {
        if (!_pending_requests.contains(request_id) || _pending_requests[request_id]._is_sent)
        {
            continue;
        }
        PendingRequest request = _pending_requests[request_id]; // 避免通道错误时持有无效迭代引用。
        if (!_channel->CanSend(request._message, request._binary.size()))
        {
            _pending_requests.remove(request_id);
            emit sigResponseReady(request_id, AssetServiceProtocol::Response(request_id, "error", "Busy", "图片发送缓冲已满"), {});
            continue;
        }
        _pending_requests[request_id]._is_sent = true;
        _pending_requests[request_id]._binary.clear();
        if (!_channel->Send(request._message, request._binary))
        {
            return;
        }
    }
}

void ServiceCommandClient::FailPending(const QString& code, const QString& reason)
{
    // 1. 清空后发信号，允许调用方在回调中提交新任务。
    const auto keys = _pending_requests.keys(); // 未完成 ID 快照。
    _pending_requests.clear();
    for (const QString& request_id : keys)
    {
        emit sigResponseReady(request_id, AssetServiceProtocol::Response(request_id, "error", code, reason), {});
    }
}

void ServiceCommandClient::ConnectionFailed(const QString& reason, bool can_retry)
{
    // 1. 旧连接和旧句柄同时失效；已提交请求必须明确失败。
    const bool was_connected = !_service_instance_id.isEmpty(); // 是否已建立服务身份。
    _connection_state = "Disconnected";
    _service_instance_id.clear();
    _retry_timer.stop();
    if (_channel)
    {
        _socket->disconnect(this);
        _channel->disconnect(this);
        _socket->abort();
        _channel->deleteLater();
        _channel = nullptr;
        _socket = nullptr;
    }
    qWarning() << "ServiceCommandClient Failed pid=" << QCoreApplication::applicationPid() << reason;
    FailPending("ServiceDisconnected", reason);
    if (was_connected)
    {
        emit sigServiceDisconnected();
    }
    // 2. 自动恢复连接，业务结果由用户重新发起，不重放旧请求。
    if (_wants_service && can_retry)
    {
        _has_launched = false;
        _connection_elapsed.start();
        _initialization_elapsed.invalidate();
        _retry_timer.start(500);
    } else
    {
        _wants_service = false;
        _timeout_timer.stop();
    }
}

void ServiceCommandClient::Maintain()
{
    // 1. 单次探测 500ms；启动及握手 10s，初始化独立限时 60s。
    if (_connection_state == "Connecting" && _probe_elapsed.elapsed() >= 500 && !_retry_timer.isActive())
    {
        LaunchService();
        _retry_timer.start(100);
    }
    if ((_connection_state == "Connecting" || _connection_state == "Handshaking") && _connection_elapsed.elapsed() >= 10000)
    {
        ConnectionFailed("图片服务启动或握手超时", false);
        return;
    }
    if (_connection_state == "Initializing" && _initialization_elapsed.elapsed() >= 60000)
    {
        ConnectionFailed("图片缓存初始化超时，服务仍保留独占锁", false);
        return;
    }
    // 2. 业务超时主动释放代理工作，迟到响应被忽略。
    const auto keys = _pending_requests.keys(); // 取消会修改映射。
    for (const QString& request_id : keys)
    {
        if (_pending_requests.contains(request_id) && _pending_requests[request_id]._elapsed.elapsed() >= 120000)
        {
            CancelRequest(request_id);
        }
    }
}

void ServiceCommandClient::CancelRequest(const QString& request_id)
{
    // 1. 本地排队任务直接移除；已提交任务发送取消命令。
    if (!_pending_requests.contains(request_id))
    {
        return;
    }
    const auto pending = _pending_requests.take(request_id); // 终态前解除本地关联。
    if (pending._is_sent && _connection_state == "Ready")
    {
        _channel->Send({{"version", AssetServiceProtocol::VERSION}, {"requestId", AssetServiceProtocol::NewId()},
            {"command", "CancelRequest"}, {"payload", QJsonObject{{"requestId", request_id}}}});
    }
    QTimer::singleShot(0, this, [this, request_id] {
        emit sigResponseReady(request_id, AssetServiceProtocol::Response(request_id, "error", "Canceled", "图片请求取消或超时"), {});
    });
}

void ServiceCommandClient::Disconnect()
{
    // 1. UI 主动退出关闭重连和所有计时器。
    _wants_service = false;
    _retry_timer.stop();
    _timeout_timer.stop();
    _connection_state = "Disconnected";
    if (_channel)
    {
        _socket->disconnect(this);
        _channel->disconnect(this);
        _socket->abort();
        delete _channel;
        _channel = nullptr;
        _socket = nullptr;
    }
    FailPending("Canceled", "客户端已断开图片服务");
}
