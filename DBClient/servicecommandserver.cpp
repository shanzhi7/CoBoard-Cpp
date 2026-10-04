#include "servicecommandserver.h"

#include <utility>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFutureWatcher>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QUuid>

#include "assetdatabasemanager.h"
#include "assetservicelogger.h"
#include "assettransfermanager.h"

ServiceCommandServer::ServiceCommandServer(QObject* parent) : QObject(parent)
{
    // 1. 构造阶段不访问 SQLite，只有取得锁才能初始化缓存。
    _server = new QLocalServer(this);
    _server->setSocketOptions(QLocalServer::UserAccessOption);
    _server->setMaxPendingConnections(32);
    _service_instance_id = AssetServiceProtocol::NewId();
    _idle_timer.setSingleShot(true);
    _idle_timer.setInterval(30000);
    _maintenance_timer.setInterval(1000);
    connect(_server, &QLocalServer::newConnection, this, &ServiceCommandServer::AcceptClients);
    connect(&_idle_timer, &QTimer::timeout, this, [this] {
        if (_client_sessions.isEmpty() && !_transfer_manager->HasActiveTasks())
        {
            Stop();
            QCoreApplication::quit();
        }
    });
    connect(&_maintenance_timer, &QTimer::timeout, this, &ServiceCommandServer::Maintain);
}

ServiceCommandServer::~ServiceCommandServer()
{
    // 1. 所有退出分支统一释放资源。
    Stop();
}

ServiceStartResult ServiceCommandServer::Start()
{
    // 1. 文件锁是唯一性依据，管道错误码不能证明没有活跃服务。
    if (!QDir().mkpath(AssetServiceProtocol::CacheRoot()))
    {
        qCritical() << "ServiceCommandServer Start cannot create cache root";
        return ServiceStartResult::Error;
    }
    _ownership_lock = std::make_unique<QLockFile>(QDir(AssetServiceProtocol::CacheRoot()).filePath("asset-service.lock"));
    _ownership_lock->setStaleLockTime(0);
    if (!_ownership_lock->tryLock(0))
    {
        return _ownership_lock->error() == QLockFile::LockFailedError ? ServiceStartResult::AlreadyRunning : ServiceStartResult::Error;
    }
    AssetServiceLogger::Install();
    // 2. 锁取得后复查端点，权限和超时不得触发端点删除。
    QLocalSocket probe; // 仅服务启动线程允许短暂阻塞探测。
    probe.connectToServer(AssetServiceProtocol::ServerName());
    if (probe.waitForConnected(500))
    {
        AssetServiceLogger::Uninstall();
        _ownership_lock->unlock();
        return ServiceStartResult::AlreadyRunning;
    }
    if (probe.error() != QLocalSocket::ServerNotFoundError && probe.error() != QLocalSocket::ConnectionRefusedError)
    {
        qCritical() << "ServiceCommandServer Probe error=" << probe.error();
        AssetServiceLogger::Uninstall();
        _ownership_lock->unlock();
        return ServiceStartResult::Error;
    }
#ifndef Q_OS_WIN
    if (probe.error() == QLocalSocket::ConnectionRefusedError)
    {
        QLocalServer::removeServer(AssetServiceProtocol::ServerName());
    }
#endif
    if (!_server->listen(AssetServiceProtocol::ServerName()))
    {
        qCritical() << "ServiceCommandServer Listen" << _server->errorString();
        AssetServiceLogger::Uninstall();
        _ownership_lock->unlock();
        return ServiceStartResult::Error;
    }
    // 3. 先响应 Initializing 握手，再后台迁移数据库。
    _is_started = true;
    _transfer_manager = new AssetTransferManager(this);
    connect(_transfer_manager, &AssetTransferManager::sigFinished, this, &ServiceCommandServer::CompleteRequest);
    connect(_transfer_manager, &AssetTransferManager::sigActivityChanged, this, &ServiceCommandServer::UpdateIdleState);
    auto* watcher = new QFutureWatcher<AssetCacheResult>(this); // 初始化完成回到服务线程。
    connect(watcher, &QFutureWatcher<AssetCacheResult>::finished, this, [this, watcher] {
        const auto result = watcher->result(); // 初始化结果。
        watcher->deleteLater();
        if (result._status != "ok")
        {
            qCritical() << "ServiceCommandServer Initialize pid=" << QCoreApplication::applicationPid() << result._error_message;
            Stop();
            QCoreApplication::exit(2);
            return;
        }
        _service_state = "Ready";
        _maintenance_timer.start();
        AssetDatabaseManager::getInstance()->ScheduleCleanup();
        qDebug() << "ServiceCommandServer Ready pid=" << QCoreApplication::applicationPid() << "instance=" << _service_instance_id;
        UpdateIdleState();
    });
    watcher->setFuture(AssetDatabaseManager::getInstance()->InitializeAsync());
    qDebug() << "ServiceCommandServer Start pid=" << QCoreApplication::applicationPid() << "rootDigest=" << AssetServiceProtocol::RootDigest();
    return ServiceStartResult::Started;
}

void ServiceCommandServer::AcceptClients()
{
    // 1. 每条连接单独保存身份、缓冲和活动请求。
    while (_server->hasPendingConnections())
    {
        auto* socket = _server->nextPendingConnection(); // QLocalServer 创建的连接。
        if (_client_sessions.size() >= 32)
        {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        ClientSession session; // 新连接上下文。
        session._client_id = AssetServiceProtocol::NewId();
        session._channel = new AssetIpcChannel(socket, this);
        const QString client_id = session._client_id; // 回调仅捕获稳定 ID。
        _client_sessions.insert(client_id, session);
        connect(session._channel, &AssetIpcChannel::sigFrameReady, this, [this, client_id](QJsonObject request, QByteArray binary) {
            DispatchCommand(client_id, request, binary);
        });
        connect(session._channel, &AssetIpcChannel::sigProtocolError, this, [client_id](const QString& message) {
            qWarning() << "ServiceCommandServer Protocol client=" << client_id << message;
        });
        connect(socket, &QLocalSocket::disconnected, this, [this, client_id] { ReleaseClientResources(client_id); });
        QTimer::singleShot(2000, session._channel, [this, client_id, socket] {
            if (_client_sessions.contains(client_id) && !_client_sessions[client_id]._has_hello)
            {
                socket->abort();
            }
        });
        _idle_timer.stop();
    }
}

bool ServiceCommandServer::SendResponse(const QString& client_id, const QJsonObject& response, const QByteArray& binary)
{
    // 1. 不广播响应，也不记录响应正文或签名 URL。
    if (_client_sessions.contains(client_id))
    {
        auto* channel = _client_sessions[client_id]._channel; // 所属连接。
        if (channel->CanSend(response, binary.size()))
        {
            return channel->Send(response, binary);
        }
        const auto busy = AssetServiceProtocol::Response(response.value("requestId").toString(), "error", "Busy", "图片响应缓冲已满"); // 有界错误响应。
        if (channel->CanSend(busy))
        {
            channel->Send(busy);
        } else
        {
            channel->Socket()->abort();
        }
    }
    return false;
}

void ServiceCommandServer::DispatchCommand(const QString& client_id, const QJsonObject& request, const QByteArray& binary)
{
    // 1. 先校验协议、请求关联和控制对象。
    if (!_client_sessions.contains(client_id))
    {
        return;
    }
    const QString request_id = request.value("requestId").toString(); // 请求 UUID。
    const QString command = request.value("command").toString(); // 固定指令名。
    const QJsonObject payload = request.value("payload").toObject(); // 不打印完整内容。
    auto reply = [this, client_id, request_id](const QString& status, const QString& code = QString(), const QString& message = QString(), const QJsonObject& data = QJsonObject()) {
        SendResponse(client_id, AssetServiceProtocol::Response(request_id, status, code, message, data));
    }; // 即时响应工具。
    if (request.value("version").toInt() != AssetServiceProtocol::VERSION)
    {
        reply("error", "IncompatibleVersion", "图片服务协议版本不兼容");
        return;
    }
    if (QUuid(request_id).isNull() || !request.value("payload").isObject())
    {
        reply("error", "InvalidRequest", "请求 ID 或 payload 无效");
        return;
    }
    if (command == "Hello")
    {
        if (!binary.isEmpty() || payload.value("rootDigest").toString() != AssetServiceProtocol::RootDigest())
        {
            reply("error", "WrongCacheRoot", "缓存目录不匹配");
            return;
        }
        _client_sessions[client_id]._has_hello = true;
        reply("ok", {}, {}, {{"service", "SyncCanvas.AssetService"}, {"protocolVersion", AssetServiceProtocol::VERSION},
            {"rootDigest", AssetServiceProtocol::RootDigest()}, {"state", _service_state}, {"instanceId", _service_instance_id},
            {"clientId", client_id}, {"pid", QCoreApplication::applicationPid()}});
        qDebug() << "ServiceCommandServer Hello pid=" << QCoreApplication::applicationPid() << "client=" << client_id << "request=" << request_id << "state=" << _service_state;
        return;
    }
    if (!_client_sessions[client_id]._has_hello || _service_state != "Ready")
    {
        reply("error", "NotReady", "缓存服务尚未就绪");
        return;
    }
    const QString job_id = client_id + '/' + request_id; // 跨连接请求 ID 不会混淆。
    if (_pending_requests.contains(job_id))
    {
        reply("error", "DuplicateRequest", "请求 ID 已在处理");
        return;
    }
    if (command == "CancelRequest")
    {
        const QString target = client_id + '/' + payload.value("requestId").toString(); // 只能取消本连接。
        if (_pending_requests.contains(target))
        {
            _transfer_manager->CancelRequest(target);
            AssetCacheResult canceled; // 原请求也必须得到终态响应。
            canceled._error_code = "Canceled";
            canceled._error_message = "请求已取消";
            CompleteRequest(target, canceled);
        }
        for (auto it = _asset_handles.begin(); it != _asset_handles.end();)
        {
            if (it->_client_id == client_id && it->_request_id == payload.value("requestId").toString())
            {
                it = _asset_handles.erase(it);
            } else
            {
                ++it;
            }
        }
        reply("ok");
        return;
    }
    if (command == "ReleaseAsset")
    {
        const QString handle = payload.value("assetHandle").toString(); // 不透明客户端句柄。
        if (_asset_handles.contains(handle) && _asset_handles[handle]._client_id == client_id)
        {
            _asset_handles.remove(handle);
        }
        reply("ok");
        return;
    }
    if (command == "TouchAsset")
    {
        const QString sha256 = payload.value("sha256").toString(); // 校验摘要后提交维护。
        if (!AssetServiceProtocol::IsSha256(sha256))
        {
            reply("error", "InvalidRequest", "无效图片摘要");
            return;
        }
        AssetDatabaseManager::getInstance()->TouchAssetAsync(sha256);
        reply("ok");
        return;
    }
    // 2. 每连接和全局预算限制排队输入，避免少量大请求耗尽内存。
    if (_client_sessions[client_id]._requests.size() >= AssetServiceProtocol::MAX_PENDING_REQUESTS
        || _pending_requests.size() >= 64 || _pending_input_bytes + binary.size() > 64 * 1024 * 1024)
    {
        reply("error", "Busy", "图片服务繁忙，请稍后重试");
        return;
    }
    if (command != "ImportImageData" && !binary.isEmpty())
    {
        reply("error", "InvalidRequest", "此指令不接受二进制输入");
        return;
    }
    PendingRequest pending; // 请求生命周期。
    pending._client_id = client_id;
    pending._request_id = request_id;
    pending._command = command;
    pending._input_bytes = binary.size();
    pending._elapsed.start();
    _pending_requests.insert(job_id, pending);
    _pending_input_bytes += binary.size();
    _client_sessions[client_id]._requests.insert(request_id);
    qDebug() << "ServiceCommandServer Dispatch pid=" << QCoreApplication::applicationPid() << "client=" << client_id << "request=" << request_id << "command=" << command;
    if (command == "LoadAsset")
    {
        _transfer_manager->LoadAsset(job_id, payload.value("sha256").toString());
    } else if (command == "ImportFile")
    {
        _transfer_manager->PrepareLocalAsset(job_id, payload.value("filePath").toString());
    } else if (command == "ImportImageData")
    {
        _transfer_manager->PrepareImageData(job_id, binary);
    } else if (command == "DownloadAsset")
    {
        _transfer_manager->DownloadAsset(job_id, payload.value("sha256").toString(), QUrl(payload.value("url").toString()));
    } else if (command == "UploadAsset")
    {
        const QString handle = payload.value("assetHandle").toString(); // 绑定连接验证。
        if (!_asset_handles.contains(handle) || _asset_handles[handle]._client_id != client_id)
        {
            AssetCacheResult error; // 旧代理句柄或其他连接句柄不可用。
            error._error_code = "InvalidHandle";
            error._error_message = "图片句柄已失效，请重新导入";
            CompleteRequest(job_id, error);
        } else
        {
            const auto asset = _asset_handles[handle]._asset; // 句柄关联的可信描述。
            if (payload.value("sha256").toString() != asset._record._asset_sha256
                || payload.value("mimeType").toString() != asset._record._mime_type
                || payload.value("byteSize").toInteger() != asset._record._byte_size)
            {
                AssetCacheResult error; // 防止签名元数据与被上传资源不匹配。
                error._error_code = "IntegrityError";
                error._error_message = "上传描述与句柄资源不匹配";
                CompleteRequest(job_id, error);
            } else
            {
                _transfer_manager->UploadAsset(job_id, asset, QUrl(payload.value("url").toString()));
            }
        }
    } else if (command == "GetCacheStats")
    {
        _transfer_manager->GetCacheStats(job_id);
    } else
    {
        AssetCacheResult error; // 未知指令终态。
        error._error_code = "UnknownCommand";
        error._error_message = "未知图片服务指令";
        CompleteRequest(job_id, error);
    }
}

void ServiceCommandServer::CompleteRequest(const QString& job_id, AssetCacheResult result)
{
    // 1. 客户端离开后，迟到结果只销毁引用。
    if (!_pending_requests.contains(job_id))
    {
        return;
    }
    const PendingRequest pending = _pending_requests.take(job_id); // 终态只处理一次。
    _pending_input_bytes -= pending._input_bytes;
    if (_client_sessions.contains(pending._client_id))
    {
        _client_sessions[pending._client_id]._requests.remove(pending._request_id);
    }
    QJsonObject payload = result._payload.isEmpty() ? result._record.ToJson() : result._payload; // 不包含内部路径。
    QByteArray binary; // 上传和统计不回传图片。
    if (result._status == "ok" && (pending._command == "ImportFile" || pending._command == "ImportImageData") && _client_sessions.contains(pending._client_id))
    {
        int handle_count = 0; // 限制无限导入却不释放的客户端。
        for (const auto& handle : std::as_const(_asset_handles))
        {
            handle_count += handle._client_id == pending._client_id;
        }
        if (handle_count >= 64)
        {
            result._status = "error";
            result._error_code = "Busy";
            result._error_message = "未释放图片句柄过多";
        } else
        {
            const QString handle_id = AssetServiceProtocol::NewId(); // 服务实例内唯一。
            AssetHandle handle; // 只保留缓存引用和元数据。
            handle._client_id = pending._client_id;
            handle._request_id = pending._request_id;
            handle._asset = result;
            handle._asset._data.clear();
            _asset_handles.insert(handle_id, handle);
            payload.insert("assetHandle", handle_id);
        }
    }
    if (result._status == "ok" && pending._command != "UploadAsset" && pending._command != "GetCacheStats")
    {
        binary = result._data;
    }
    if (!SendResponse(pending._client_id, AssetServiceProtocol::Response(pending._request_id, result._status,
        result._error_code, result._error_message, payload), binary))
    {
        _asset_handles.remove(payload.value("assetHandle").toString());
    }
    qDebug() << "ServiceCommandServer Complete pid=" << QCoreApplication::applicationPid() << "client=" << pending._client_id << "request=" << pending._request_id << "status=" << result._status << "sha=" << result._record._asset_sha256 << "elapsedMs=" << pending._elapsed.elapsed();
    UpdateIdleState();
}

void ServiceCommandServer::ReleaseClientResources(const QString& client_id)
{
    // 1. 先移除连接，取消回调不能给其他连接发送结果。
    if (!_client_sessions.contains(client_id))
    {
        return;
    }
    const auto session = _client_sessions.take(client_id); // 稳定资源快照。
    for (const QString& request_id : session._requests)
    {
        const QString job_id = client_id + '/' + request_id; // 取消所属请求。
        _pending_input_bytes -= _pending_requests.take(job_id)._input_bytes;
        _transfer_manager->CancelRequest(job_id);
    }
    // 2. 实际上传另持引用，不会因句柄移除提前删除文件。
    for (auto it = _asset_handles.begin(); it != _asset_handles.end();)
    {
        if (it->_client_id == client_id)
        {
            it = _asset_handles.erase(it);
        } else
        {
            ++it;
        }
    }
    session._channel->deleteLater();
    qDebug() << "ServiceCommandServer Disconnect client=" << client_id;
    UpdateIdleState();
}

void ServiceCommandServer::UpdateIdleState()
{
    // 1. 新连接或实际后台活动取消退出；仅空闲状态第一次进入时计时。
    if (!_is_started || _service_state != "Ready")
    {
        return;
    }
    if (!_client_sessions.isEmpty() || _transfer_manager->HasActiveTasks())
    {
        _idle_timer.stop();
    } else if (!_idle_timer.isActive())
    {
        _idle_timer.start();
        qDebug() << "ServiceCommandServer Idle graceMs=30000 pid=" << QCoreApplication::applicationPid();
    }
}

void ServiceCommandServer::Maintain()
{
    // 1. 普通指令两分钟超时，主动取消后台任务并返回明确失败。
    const auto keys = _pending_requests.keys(); // 取消会修改映射。
    for (const QString& job_id : keys)
    {
        if (_pending_requests.contains(job_id) && _pending_requests[job_id]._elapsed.elapsed() > 120000)
        {
            _transfer_manager->CancelRequest(job_id);
            AssetCacheResult error; // 超时终态。
            error._error_code = "Timeout";
            error._error_message = "图片请求超时";
            CompleteRequest(job_id, error);
            qWarning() << "ServiceCommandServer Timeout job=" << job_id;
        }
    }
    // 2. 每五秒重试因保护引用而暂缓的清理。
    if (++_maintenance_ticks % 5 == 0)
    {
        AssetDatabaseManager::getInstance()->ScheduleCleanup();
    }
    UpdateIdleState();
}

void ServiceCommandServer::Stop()
{
    // 1. 先关闭入口，不允许停止过程中提交新任务。
    if (!_is_started)
    {
        return;
    }
    _is_started = false;
    _service_state = "Stopping";
    _maintenance_timer.stop();
    _idle_timer.stop();
    _server->close();
    const auto clients = _client_sessions.keys(); // 连接回调会修改容器。
    for (const QString& client_id : clients)
    {
        _client_sessions[client_id]._channel->Socket()->abort();
        ReleaseClientResources(client_id);
    }
    // 2. 文件关闭、线程退出和数据库释放全部完成后才能交出锁。
    _transfer_manager->Shutdown();
    delete _transfer_manager;
    _transfer_manager = nullptr;
    AssetDatabaseManager::getInstance()->Shutdown();
    AssetDatabaseManager::getInstance().reset();
    qDebug() << "ServiceCommandServer Stop pid=" << QCoreApplication::applicationPid();
    AssetServiceLogger::Uninstall();
    _ownership_lock->unlock();
}
