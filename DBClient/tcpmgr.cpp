#include "tcpmgr.h"

#include <limits>
#include <utility>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include "canvastcpworker.h"
#include "usermgr.h"

TcpMgr::TcpMgr():_host(""),_port(0),_reconnect_timer(new QTimer(this)),_reconnect_cnt(0)
{
    // 1. 创建独立网络线程，所有 socket 回调通过 queued signal 回到控制器线程。
    qRegisterMetaType<DecodedTcpMessage>("DecodedTcpMessage");
    _worker = new CanvasTcpWorker;
    _worker->moveToThread(&_network_thread);
    connect(&_network_thread, &QThread::finished, _worker, &QObject::deleteLater);
    connect(this, &TcpMgr::SigConnectRequested, _worker, &CanvasTcpWorker::ConnectToServer, Qt::QueuedConnection);
    connect(this, &TcpMgr::SigSendRequested, _worker, &CanvasTcpWorker::SendData, Qt::QueuedConnection);
    connect(this, &TcpMgr::SigDisconnectRequested, _worker, &CanvasTcpWorker::DisconnectFromServer, Qt::QueuedConnection);
    connect(_worker, &CanvasTcpWorker::SigConnected, this, [this](quint64 generation) {
        if (generation != _connection_generation || _is_logged_out) return;
        _is_connected = true;
        if (_exit_pending || _is_offline_reconnect || !_pending_room_id.isEmpty())
            send_canvas_login();
        else
            emit sig_con_success(true);
    }, Qt::QueuedConnection);
    connect(_worker, &CanvasTcpWorker::SigMessageDecoded, this, &TcpMgr::HandleMessage, Qt::QueuedConnection);
    connect(_worker, &CanvasTcpWorker::SigDisconnected, this, [this](quint64 generation) {
        if (generation != _connection_generation) return;
        _is_connected = false;
        _is_authenticated = false;
        _room_request_pending = false;
        if (_exit_pending)
        {
            emit SigExitTransportFailed(QStringLiteral("连接已断开，退出尚未确认，请重试"));
            return;
        }
        if (_is_logged_out || !_pending_room_id.isEmpty()) return;
        emit sig_go_lobby("服务器连接断开，正在尝试重连...");
        _is_offline_reconnect = true;
        slot_start_reconnect();
    }, Qt::QueuedConnection);
    connect(_worker, &CanvasTcpWorker::SigSocketError, this,
            [this](quint64 generation, QAbstractSocket::SocketError error, const QString& detail) {
        if (generation != _connection_generation || _is_logged_out) return;
        qDebug() << "[TcpMgr] socket error:" << detail;
        if (_exit_pending)
        {
            _is_authenticated = false;
            emit SigExitTransportFailed(QStringLiteral("退出连接失败，请重试"));
            return;
        }
        if (error == QAbstractSocket::ConnectionRefusedError || error == QAbstractSocket::HostNotFoundError ||
            error == QAbstractSocket::SocketTimeoutError) emit sig_con_success(false);
        if (_is_offline_reconnect)
        {
            emit sig_go_lobby("网络错误，正在尝试重连…");
            slot_start_reconnect();
        }
    }, Qt::QueuedConnection);
    connect(_worker, &CanvasTcpWorker::SigDecodeFailed, this, [this](quint64 generation, quint16 message_id) {
        if (generation != _connection_generation || _is_logged_out || _exit_pending) return;
        if (message_id == ID_CREAT_ROOM_RSP || message_id == ID_JOIN_ROOM_RSP) _room_request_pending = false;
    }, Qt::QueuedConnection);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &TcpMgr::StopWorker);
    _network_thread.start();

    // 2. 定时器和业务处理表留在 UI 线程，兼容现有连接、房间和退出状态机。
    _reconnect_timer->setSingleShot(true);
    connect(_reconnect_timer, &QTimer::timeout, this, &TcpMgr::slot_do_reconnect);
    initHandlers();
    connect(this, &TcpMgr::sig_send_data, this, &TcpMgr::slot_send_data);
}

TcpMgr::~TcpMgr()
{
    // 1. 正常退出与直接析构共用回收入口。
    StopWorker();
}

void TcpMgr::StopWorker()
{
    // 1. 作废排队事件并暂停重连，应用对象已销毁时也不依赖排队命令退出。
    _reconnect_timer->stop();
    _is_logged_out = true;
    ++_connection_generation;
    if (_network_thread.isRunning())
    {
        // 2. quit 可跨线程调用，finished 的延迟删除在网络线程回收 worker 和 socket。
        _network_thread.quit();
        _network_thread.wait();
    }
}

void TcpMgr::StartConnection()
{
    // 1. 每次切换连接都推进代次，屏蔽已经排队的旧连接事件。
    ++_connection_generation;
    _is_connected = false;
    _is_authenticated = false;
    emit SigConnectRequested(_host, _port, _connection_generation);
}

void TcpMgr::HandleMessage(quint64 generation, const DecodedTcpMessage& decoded)
{
    // 1. 只处理当前连接且未注销的已解码消息。
    if (generation != _connection_generation || _is_logged_out) return;
    Q_ASSERT(QThread::currentThread() == thread());
    const auto id = static_cast<ReqId>(decoded._message_id);
    // 2. 退出时只交付终态回包，场景与业务信号仍在 UI 线程按序执行。
    auto it = _handlers.find(id);
    if (it == _handlers.end()) return;
    if (_exit_pending && id != ID_CANVAS_LOGIN_RSP && id != ID_LEAVE_ROOM_RSP &&
        id != ID_DESTROY_ROOM_RSP && id != ID_ROOM_DESTROYED_BROADCAST) return;
    it.value()(decoded._payload);
}

void TcpMgr::initHandlers()
{
    // 1. 按消息编号登记账号、房间和图元回调，文字确认交给画布处理。
    _handlers.insert(ReqId::ID_CANVAS_LOGIN_RSP,[this](const DecodedTcpMessage::Payload& payload){

        const auto& jsonObj = std::get<QJsonObject>(payload);
        if(!jsonObj.contains("error"))      //没有error这个键
        {
            int err = ErrorCodes::ERR_JSON;
            qDebug()<<"登录失败,error json解析失败 "<<err;
            emit sig_login_failed(err);
            return;
        }
        int err = jsonObj["error"].toInt();
        if(err != ErrorCodes::SUCCESS)
        {
            qDebug()<<"登录失败,error is "<<err;
            if (_exit_pending)
            {
                emit SigExitTransportFailed(QStringLiteral("退出连接鉴权失败，错误码 %1；请重试").arg(err));
                return;
            }
            emit sig_login_failed(err);
            return;
        }

        qDebug()<<"tcpmgr: recv error is "<<jsonObj["error"].toInt()<<" uid is "<<jsonObj["uid"].toInt();

        _is_authenticated = true;
        if (_exit_pending)
        {
            emit SigExitConnectionReady();
            return;
        }
        // 重定向切服后的 join：登录成功后再发 join
        if(!_pending_room_id.isEmpty())
        {
                qDebug() << "[TcpMgr] redirect: login ok, auto join room:" << _pending_room_id;

            message::JoinRoomReq req;
            req.set_uid(UserMgr::getInstance()->getMyInfo()->_id);
            req.set_room_id(_pending_room_id.toStdString());

            std::string binaryData;
            req.SerializeToString(&binaryData);

            this->slot_send_data(ReqId::ID_JOIN_ROOM_REQ, QByteArray::fromStdString(binaryData));

            _pending_room_id.clear();
            _pending_uid = 0;
            return;
        }

        // 掉线重连: 登录成功后自动join上次房间
        if(_is_offline_reconnect && !_resume_room_id.isEmpty())
        {
             qDebug() << "[TcpMgr] offline reconnect: login ok, auto join room:" << _resume_room_id;

             //发送join房间请求给服务器
            message::JoinRoomReq req;
            req.set_uid(UserMgr::getInstance()->getMyInfo()->_id);
            req.set_room_id(_resume_room_id.toStdString());

            std::string binaryData;
            req.SerializeToString(&binaryData);

            this->slot_send_data(ReqId::ID_JOIN_ROOM_REQ, QByteArray::fromStdString(binaryData));
            qDebug() << "[JOIN SEND] tag=offline_auto room=" << _resume_room_id;
            return; // 等 Join 成功再切页面
        }
        // 已退房后的普通重连只恢复 TCP，不能再次初始化账号或取走大厅部件。
        if (_is_offline_reconnect)
        {
            _is_offline_reconnect = false;
            _reconnect_timer->stop();
            _reconnect_cnt = 0;
            return;
        }
        emit sig_switch_canvas();   // 普通首次登录： 切到 Lobby
    });

    // 2. 退出结果交给控制器，统一处理关联、重试和页面收尾。
    _handlers.insert(ID_LEAVE_ROOM_RSP, [this](const DecodedTcpMessage::Payload& payload) {
        const auto& response = std::get<message::LeaveRoomRsp>(payload);
        emit SigRoomExitResponse(QString::fromStdString(response.room_id()), QString::fromStdString(response.request_id()), response.error());
    });
    _handlers.insert(ID_DESTROY_ROOM_RSP, [this](const DecodedTcpMessage::Payload& payload) {
        const auto& response = std::get<message::DestroyRoomRsp>(payload);
        emit SigRoomExitResponse(QString::fromStdString(response.room_id()), QString::fromStdString(response.request_id()), response.error());
    });
    _handlers.insert(ID_ROOM_DESTROYED_BROADCAST, [this](const DecodedTcpMessage::Payload& payload) {
        const auto& notification = std::get<message::RoomDestroyedBroadcast>(payload);
        emit SigRoomDestroyed(QString::fromStdString(notification.room_id()), QString::fromStdString(notification.reason()));
    });

    //注册创建房间回包函数
    _handlers.insert(ReqId::ID_CREAT_ROOM_RSP,[this](const DecodedTcpMessage::Payload& payload){
        _room_request_pending = false;

        const auto& jsonObj = std::get<QJsonObject>(payload);
        if(!jsonObj.contains("error"))      //没有error这个键
        {
            int err = ErrorCodes::ERR_JSON;
            qDebug()<<"创建房间失败,error json解析失败 "<<err;
            return;
        }
        int err = jsonObj["error"].toInt();
        if(err != ErrorCodes::SUCCESS)
        {
            qDebug()<<"创建房间失败,error is "<<err;
            return;
        }
        qDebug()<<"创建房间成功！";
        std::shared_ptr<RoomInfo> room_info = std::make_shared<RoomInfo>();
        room_info->id = jsonObj["room_id"].toString();
        room_info->name = jsonObj["room_name"].toString();
        room_info->host = jsonObj["room_host"].toString();
        room_info->port = jsonObj["room_port"].toInt();
        room_info->owner_uid = jsonObj["owner_uid"].toInt();
        room_info->width = jsonObj["width"].toInt();
        room_info->height = jsonObj["height"].toInt();

        _resume_room_id = room_info->id;
        _room_host = room_info->host;
        _room_port = (uint16_t)room_info->port;

        emit sig_create_room_finish(room_info);      //发送信号通知 Lobby
    });


    //注册加入房间回包函数
    _handlers.insert(ReqId::ID_JOIN_ROOM_RSP,[this](const DecodedTcpMessage::Payload& payload){
        _room_request_pending = false;

        const auto& rsp = std::get<message::JoinRoomRsp>(payload);
        int err = rsp.error();                  //获取错误码

        //处理重定向逻辑
        if(err == message::ErrorCodes::NeedRedirect)
        {
            std::string targetHost = rsp.redirect_host();
            int targetPort = rsp.redirect_port();
            QString room_id = QString::fromStdString(rsp.room_id());
            int uid = UserMgr::getInstance()->getMyInfo()->_id;
            qDebug() << "[Redirect] Server requested redirect to: "
                     << QString::fromStdString(targetHost) << ":" << targetPort;
            TcpMgr::getInstance()->slot_switch_server(QString::fromStdString(targetHost), targetPort,room_id,uid);
            return;
        }

        if (err != message::ErrorCodes::SUCCESS)
        {
            qDebug() << "加入/创建房间失败, ErrorCode: " << err;
            // emit sigJoinRoomFailed(err); // 通知UI弹窗提示错误
            return;
        }

        qDebug() << "加入房间成功！RoomID:" << QString::fromStdString(rsp.room_id());
        std::shared_ptr<RoomInfo> room_info = std::make_shared<RoomInfo>();
        room_info->id = QString::fromStdString(rsp.room_id());
        room_info->name = QString::fromStdString(rsp.room_name());
        room_info->owner_uid = rsp.owner_uid();
        room_info->host = QString::fromStdString(rsp.redirect_host());
        room_info->port = rsp.redirect_port();
        room_info->width = rsp.canvas_width();
        room_info->height = rsp.canvas_height();

        qDebug()<<"成员个数："<<rsp.member_list_size();
        qDebug()<<"房主id : "<<room_info->owner_uid<<" name: "<<room_info->name;

        // 解析成员列表快照
        for(int i = 0;i < rsp.member_list_size();i++)
        {
            const message::UserInfo& protoUser = rsp.member_list(i);
            UserInfo user_info;
            user_info._id = protoUser.uid();
            user_info._name = QString::fromStdString(protoUser.name());
            user_info._avatar = QString::fromStdString(protoUser.avatar_url());

            room_info->members.append(user_info);
        }

        // 记录“恢复用”的房间信息（以后断线就连这个CanvasServer + join这个房间）
        _resume_room_id = room_info->id;
        _room_host = room_info->host;
        _room_port = (uint16_t)room_info->port;

        if (_is_offline_reconnect)  // 掉线重连成功逻辑
        {
            qDebug() << "[TcpMgr] offline reconnect: join ok, back to canvas";
            _is_offline_reconnect = false;
            _reconnect_timer->stop();
            _reconnect_cnt = 0;

            emit sig_resume_join_finish(room_info);
            return;
        }
        emit sig_join_room_finish(room_info);   //发送给UI，UI 根据 members 初始化列表

    });

    // 注册有人加入广播
    _handlers.insert(ReqId::ID_USER_JOIN_BROADCAST, [this](const DecodedTcpMessage::Payload& payload) {
        const auto& msg = std::get<message::UserJoinRoomBroadcast>(payload);

        const message::UserInfo& info = msg.user_info();

        // 构造本地结构体，方便传给 UI
        UserInfo newMember;
        newMember._id = info.uid();
        newMember._name = QString::fromStdString(info.name());
        newMember._avatar = QString::fromStdString(info.avatar_url());

        qDebug() << "广播: 用户加入 - " << newMember._name << " (UID:" << newMember._id << ")";

        // 发送信号通知 UI (CanvasWidget/UserListWidget) 添加一行
        emit sig_user_joined(newMember);
    });

    // 注册有人离开广播
    _handlers.insert(ReqId::ID_USER_LEAVE_BROADCAST, [this](const DecodedTcpMessage::Payload& payload) {
        const auto& msg = std::get<message::UserLeaveRoomBroadcast>(payload);

        if (!msg.room_id().empty() && QString::fromStdString(msg.room_id()) != _resume_room_id) return;
        int uid = msg.uid();
        qDebug() << "广播: 用户离开 - UID:" << uid;

        // 发送信号通知 UI 移除对应的那一行
        emit sig_user_left(uid);
    });

    //注册绘画广播 (服务器转发别人画的)
    _handlers.insert(ReqId::ID_DRAW_RSP,[this](const DecodedTcpMessage::Payload& payload){
        emit sig_draw_broadcast(std::get<message::DrawReq>(payload));  //发送广播信号
    });

    // 图片操作使用独立消息 ID，不能复用 DrawReq 的解析路径，否则字段含义会被误解释。
    _handlers.insert(ReqId::ID_IMAGE_OPERATION_RSP,[this](const DecodedTcpMessage::Payload& payload){
        emit sig_image_operation_broadcast(std::get<message::ImageOperation>(payload));
    });
    // 通用编辑有独立消息类型，必须保持与其他绘画回包的到达顺序。
    _handlers.insert(ReqId::ID_ITEM_EDIT_RSP, [this](const DecodedTcpMessage::Payload& payload) {
        // 1. 转交已解码载荷，画布统一排队并保持创建与编辑的先后顺序。
        emit SigItemEditReceived(std::get<message::ItemEditOperationRsp>(payload));
    });
    _handlers.insert(ReqId::ID_ITEM_CREATE_BATCH_RSP, [this](const DecodedTcpMessage::Payload& payload) {
        // 1. 批量创建保持 TCP 到达顺序，画布统一处理本地确认和远端回放。
        emit SigItemCreateBatchReceived(std::get<message::ItemCreateBatchRsp>(payload));
    });
    _handlers.insert(ReqId::ID_TEXT_OPERATION_RSP, [this](const DecodedTcpMessage::Payload& payload) {
        emit SigTextOperationReceived(std::get<message::TextOperationRsp>(payload));
    });

    // 注册收到群聊消息


    // 注册授权编辑回包
    _handlers.insert(ReqId::ID_GRANT_EDIT_RSP,[this](const DecodedTcpMessage::Payload& payload){
        const auto& rsp = std::get<message::GrantEditRsp>(payload);
        qDebug() << "授权编辑回包, error=" << rsp.error()
                 << " target=" << rsp.target_uid()
                 << " can_edit=" << rsp.can_edit();
    });

    // 注册取消编辑权限回包
    _handlers.insert(ReqId::ID_REVOKE_EDIT_RSP,[this](const DecodedTcpMessage::Payload& payload){
        const auto& rsp = std::get<message::RevokeEditRsp>(payload);
        qDebug() << "取消编辑权限回包, error=" << rsp.error()
                 << " target=" << rsp.target_uid()
                 << " can_edit=" << rsp.can_edit();
    });

    // 注册编辑权限变更广播
    _handlers.insert(ReqId::ID_PERMISSION_CHANGED_BROADCAST,[this](const DecodedTcpMessage::Payload& payload){
        const auto& msg = std::get<message::PermissionChangedBroadcast>(payload);

        qDebug() << "编辑权限变更广播, target=" << msg.target_uid()
                 << " can_edit=" << msg.can_edit()
                 << " operator=" << msg.operator_uid();
        emit sig_permission_changed(msg.target_uid(), msg.can_edit());
    });
    // 注册收到群聊消息
    _handlers.insert(ReqId::ID_CHAT_RSP,[this](const DecodedTcpMessage::Payload& payload){
        const auto& rsp = std::get<message::ChatRsp>(payload);

        // 发送接收群聊消息信号
        emit sig_chat_received(
            (int)rsp.uid(),
            QString::fromStdString(rsp.name()),
            QString::fromStdString(rsp.avatar_url()),
            QString::fromStdString(rsp.room_id()),
            QString::fromStdString(rsp.content()),
            (qulonglong)rsp.server_ts()
            );
    });
}

void TcpMgr::slot_tcp_connect(ServerInfo si)
{
    // 1. 新一轮主动登录解除退出状态，不继承上次重连的退避次数。
    qDebug()<<"接收到 tcp connect singal";

    _exit_pending = false;
    _is_authenticated = false;
    _is_logged_out = false;
    _reconnect_timer->stop();
    _reconnect_cnt = 0;
    _is_offline_reconnect = false;

    // 2. 使用本次登录返回的地址建立连接。
    qDebug()<<"Connnecting to Server......";
    _host = si.Host;
    _port = static_cast<uint16_t>(si.Port.toUInt());

    StartConnection();
}

void TcpMgr::Logout()
{
    // 1. 标记主动退出并停止重连定时器，避免断开旧连接后再次连接服务器。
    _exit_pending = false;
    _is_authenticated = false;
    _room_request_pending = false;
    _is_logged_out = true;
    _reconnect_timer->stop();
    _reconnect_cnt = 0;
    _is_offline_reconnect = false;

    // 2. 清除重定向和房间恢复目标，下一次登录从全新连接开始。
    _pending_room_id.clear();
    _pending_uid = 0;
    _resume_room_id.clear();
    _room_host.clear();
    _room_port = 0;
    _host.clear();
    _port = 0;

    // 3. 推进代次后在网络线程中止 TCP，服务端按连接关闭流程移除成员。
    ++_connection_generation;
    _is_connected = false;
    emit SigDisconnectRequested(_connection_generation);
}

void TcpMgr::slot_send_data(ReqId reqid, QByteArray data)
{
    // 1. 退出账号后忽略旧界面的发送请求。
    if (_is_logged_out) return;
    if (_exit_pending && reqid != ID_CANVAS_LOGIN_REQ && reqid != ID_LEAVE_ROOM_REQ && reqid != ID_DESTROY_ROOM_REQ) return;
    if (data.size() > std::numeric_limits<quint16>::max())
    {
        qWarning() << "[TcpMgr] slot_send_data: message exceeds TCP frame limit, id=" << reqid;
        return;
    }
    if (reqid == ID_CREAT_ROOM_REQ || reqid == ID_JOIN_ROOM_REQ) _room_request_pending = true;
    // 2. worker 在线程内按原协议添加网络字节序包头并保持发送顺序。
    emit SigSendRequested(static_cast<quint16>(reqid), std::move(data), _connection_generation);
}

void TcpMgr::slot_grant_edit(const QString& room_id, int target_uid)    //授权给别人权限处理函数
{
    message::GrantEditReq req;
    req.set_operator_uid(UserMgr::getInstance()->getUid());     //设置操作者id
    req.set_room_id(room_id.toStdString());                     //设置房间id
    req.set_target_uid(target_uid);                             //设置目标id

    std::string out;
    if (!req.SerializeToString(&out)) return;
    slot_send_data(ReqId::ID_GRANT_EDIT_REQ, QByteArray::fromStdString(out));
}

void TcpMgr::slot_revoke_edit(const QString& room_id, int target_uid)       //撤销授权处理函数
{
    message::RevokeEditReq req;
    req.set_operator_uid(UserMgr::getInstance()->getUid());
    req.set_room_id(room_id.toStdString());
    req.set_target_uid(target_uid);

    std::string out;
    if (!req.SerializeToString(&out)) return;
    slot_send_data(ReqId::ID_REVOKE_EDIT_REQ, QByteArray::fromStdString(out));
}

void TcpMgr::slot_switch_server(const QString &host, int port,const QString& room_id, int uid)
{
    // 1. 保存重定向目标，暂停掉线恢复，下一次鉴权后再自动加入房间。
    _is_offline_reconnect = false; // 切服不是掉线重连
    _reconnect_timer->stop();
    _reconnect_cnt = 0;
    // 先把任务记在小本本上
    _pending_room_id = room_id;
    _pending_uid = uid;
    _host = host;
    _port = port;

    // 2. 切服时旧事件由代次隔离，连接替换交给 worker 所属线程。
    qDebug() << "[TcpMgr] Switching to " << host << ":" << port;
    StartConnection();

}

int TcpMgr::calc_backoff_ms()   //计算指数退避ms，也就是下次发送连接请求的时间
{
    // 200, 400, 800, 1600, 3200, 5000
    int base = 200;
    int ms = base * (1 << qMin(_reconnect_cnt, 5));
    return qMin(ms, 5000);
}

void TcpMgr::slot_start_reconnect() // 启动/继续指数退避重连
{
    // 1. QTimer 只能在所属线程的事件循环中启动，保留外部调用的线程保护。
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, &TcpMgr::slot_start_reconnect, Qt::QueuedConnection);
        return;
    }

    // 2. 主动退出后丢弃旧重连任务，等待新一次登录重新启用连接。
    if (_is_logged_out || _exit_pending || _reconnect_timer->isActive())
        return;
    int backoff = calc_backoff_ms();
    qDebug() << "[TcpMgr] will reconnect in" << backoff << "ms, cnt=" << _reconnect_cnt;
    _reconnect_timer->start(backoff);    //启动定时器,backoff ms 后开始重连
}

void TcpMgr::slot_do_reconnect()    // 真正进行一次 connectToHost
{
    // 1. 定时器回调必须在对象线程执行。
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, &TcpMgr::slot_do_reconnect, Qt::QueuedConnection);
        return;
    }

    // 2. 主动退出后不能重新连接；其他情况优先使用房间服务器地址。
    if (_is_logged_out || _exit_pending) return;
    QString host = !_room_host.isEmpty() ? _room_host : _host;
    uint16_t port = (_room_port != 0) ? _room_port : _port;

    if (host.isEmpty() || port == 0)
    {
        qDebug() << "[TcpMgr] no host/port to reconnect";
        emit sig_go_lobby("没有可用服务器地址，请重新登录。");
        _is_offline_reconnect = false;
        return;
    }

    // 3. 更新重连次数并使用新连接恢复会话。
    _reconnect_cnt++;
    _host = host;
    _port = port;

    qDebug() << "[TcpMgr] reconnecting to" << host << ":" << port;
    StartConnection();
}

void TcpMgr::send_canvas_login() //复用LoginWidget 的 CanvasLogin 组包
{
    // 序列化发送内容（完全复用 LoginWidget::slot_tcp_con_finish 的逻辑）
    QJsonObject jsonObj;
    jsonObj["uid"] = UserMgr::getInstance()->getMyInfo()->_id;
    jsonObj["token"] = UserMgr::getInstance()->getToken();
    jsonObj["name"] = UserMgr::getInstance()->getName();
    jsonObj["avatar"] = UserMgr::getInstance()->getAvatar();

    QJsonDocument jsonDoc(jsonObj);
    QByteArray jsonString = jsonDoc.toJson(QJsonDocument::Indented);

    this->slot_send_data(ReqId::ID_CANVAS_LOGIN_REQ, jsonString);
}

void TcpMgr::SetExitPending(bool pending)
{
    // 1. 退出只暂停自动恢复及普通提交，不屏蔽完成消息。
    _exit_pending = pending;
    if (pending)
    {
        _reconnect_timer->stop();
        _is_offline_reconnect = false;
        _pending_room_id.clear();
        _pending_uid = 0;
    }
}

void TcpMgr::PrepareExitConnection(const QString& host, int port)
{
    // 1. 已认证连接直接发送退出，重试不自动加入旧房间。
    if (_is_connected && _is_authenticated && _host == host && _port == port)
    {
        emit SigExitConnectionReady();
        return;
    }
    // 2. 重连前废弃旧代次，connected 回调只发送认证。
    _is_authenticated = false;
    _host = host;
    _port = static_cast<uint16_t>(port);
    StartConnection();
}

void TcpMgr::ClearRoomRecovery()
{
    // 1. 确认退出后废弃自动回房目标，账号连接仍然保留。
    _resume_room_id.clear();
    _room_host.clear();
    _room_port = 0;
    _pending_room_id.clear();
    _pending_uid = 0;
    _room_request_pending = false;
}

bool TcpMgr::IsRoomRequestPending() const
{
    // 1. 创建和加入尚未确认时，注销不能跳过未知的房间身份。
    return _room_request_pending || !_pending_room_id.isEmpty();
}
