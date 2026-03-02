#include "tcpmgr.h"
#include "message.pb.h"
#include "usermgr.h"
#include <QNetworkProxy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

TcpMgr::TcpMgr():_host(""),_port(0),_b_recv_pending(false),_message_id(0),_message_len(0)
{
    //连接成功
    QObject::connect(&_socket,&QTcpSocket::connected,this,[this](){
        qDebug()<<"连接到 Server！";

        if(!_pending_room_id.isEmpty()) //检查是否有“重定向后的加入房间”任务
        {
            qDebug() << "[TcpMgr] Detected pending Redirect Task. Re-sending JoinRoomReq...";

            // 构造请求包
            message::JoinRoomReq req;
            req.set_uid(_pending_uid);
            req.set_room_id(_pending_room_id.toStdString());

            // 序列化
            std::string binaryData;
            req.SerializeToString(&binaryData);
            QByteArray sendData = QByteArray::fromStdString(binaryData);

            // 重定向切服：先登录
            send_canvas_login();
            return;
        }
        // 掉线重连连上后：自动 CanvasLogin
        if (_is_offline_reconnect)
        {
            qDebug() << "[TcpMgr] offline reconnect connected, auto canvas login...";
            send_canvas_login();
            return;
        }
        // 首次登录到CanvasServer服务
        emit sig_con_success(true);
    });

    //准备读取数据
    QObject::connect(&_socket,&QTcpSocket::readyRead,this,[this](){
        //当有数据可读时，读取所有数据
        //读取数据追加到缓冲区buffer
        _buffer.append(_socket.readAll());


        // 循环处理_buffer 直到缓冲区无数据，避免粘包问题
        while(_buffer.size() > 0)
        {
            QDataStream stream(&_buffer,QIODevice::ReadOnly);
            stream.setByteOrder(QDataStream::BigEndian);        //设置大端字节序(网络字节序)
            stream.setVersion(QDataStream::Qt_6_5);             //设置版本
            //解析消息头(当前没有等待的消息)
            if(!_b_recv_pending)
            {
                //消息头不够(4字节),等待更多数据
                if(_buffer.size() < sizeof(quint16) * 2)
                {
                    break;
                }
                //读取消息id和长度
                stream >> _message_id >> _message_len;
                _buffer = _buffer.mid(sizeof(quint16) * 2);
                qDebug() << "Message ID:" << _message_id << ", Length:" << _message_len;

            }

            //解析消息体
            if(_buffer.size() < _message_len)   //长度不足
            {
                _b_recv_pending = true;         //当前有等待的消息
                break;
            }

            _b_recv_pending = false;            //当前无等待消息

            //消息体足够，解析处理
            QByteArray messageBody = _buffer.mid(0,_message_len);
            qDebug() << "Received body:" << messageBody;

            handleMsg(ReqId(_message_id),_message_len,messageBody);

            //移除已经解析的消息体
            _buffer = _buffer.mid(_message_len);    //如果执行完mid 长度不为 0，说明读取到了下一条消息
        }
    });

    //错误处理
    QObject::connect(&_socket,QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
                     [&](QAbstractSocket::SocketError socketError){
                         Q_UNUSED(socketError);

                         qDebug() << "Error:" << _socket.errorString() ;
                         switch (socketError)
                         {
                         case QTcpSocket::ConnectionRefusedError:     //服务端拒绝
                             qDebug() << "Connection Refused!";
                             emit sig_con_success(false);
                             break;
                         case QTcpSocket::RemoteHostClosedError:      //服务端关闭
                             qDebug() << "Remote Host Closed Connection!";
                             break;
                         case QTcpSocket::HostNotFoundError:          //未找到目标服务(host,port)
                             qDebug() << "Host Not Found!";
                             emit sig_con_success(false);
                             break;
                         case QTcpSocket::SocketTimeoutError:         //连接超时
                             qDebug() << "Connection Timeout!";
                             emit sig_con_success(false);
                             break;
                         case QTcpSocket::NetworkError:               //网络错误
                             qDebug() << "Network Error!";
                             break;
                         default:
                             qDebug() << "Other Error!";
                             break;
                         }
                         if (_is_offline_reconnect)
                         {
                             emit sig_go_lobby("网络错误，正在尝试重连…");
                             slot_start_reconnect();
                         }
                     });

    _reconnect_timer.setSingleShot(true);   //设置单次定时器
    QObject::connect(&_reconnect_timer, &QTimer::timeout, this, &TcpMgr::slot_do_reconnect);     //连接定时器timeout 重新连接服务器

    // disconnected监听
    QObject::connect(&_socket,&QTcpSocket::disconnected,this,[this](){
        qDebug() << "[TcpMgr] disconnected";

        // 如果是重定向切服造成的断开 (slot_switch_server 里 abort)，不要走掉线重连
        if(!_pending_room_id.isEmpty())
        {
            qDebug()<<"[TcpMgr] 在重定向切换期间断开连接，忽略离线重连";
            return;
        }

        //通知UI回大厅 (本地行为，不依赖服务器在线)
        emit sig_go_lobby("服务器连接断开，正在尝试重连...");

        if (_is_offline_reconnect)
        {
            slot_start_reconnect();
            return;
        }

        //标记进入掉线流程
        _is_offline_reconnect = true;

        //启动指数退避重连
        slot_start_reconnect();
    });

    initHandlers();

    //连接tcp发送信号与槽函数
    connect(this,&TcpMgr::sig_send_data,this,&TcpMgr::slot_send_data);
}

void TcpMgr::initHandlers()
{
    //注册登录回包函数
    _handlers.insert(ReqId::ID_CANVAS_LOGIN_RSP,[this](ReqId id,int len,QByteArray data){
        qDebug()<<"handle id is "<<id<<" data is "<<data;

        //将ByteArray转换成QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        //检测是否转换成功
        if(jsonDoc.isNull())
        {
            qDebug()<<"creat QJsonDocument 错误";
            return;
        }
        QJsonObject jsonObj = jsonDoc.object();
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
            emit sig_login_failed(err);
            return;
        }

        qDebug()<<"tcpmgr: recv error is "<<jsonObj["error"].toInt()<<" uid is "<<jsonObj["uid"].toInt();

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
        emit sig_switch_canvas();   // 普通首次登录： 切到 Lobby
    });

    //注册创建房间回包函数
    _handlers.insert(ReqId::ID_CREAT_ROOM_RSP,[this](ReqId id,int len,QByteArray data){
        qDebug()<<"handle id is "<<id<<" data is "<<data;

        //将ByteArray转换成QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        //检测是否转换成功
        if(jsonDoc.isNull())
        {
            qDebug()<<"creat QJsonDocument 错误";
            return;
        }
        QJsonObject jsonObj = jsonDoc.object();
        if(!jsonObj.contains("error"))      //没有error这个键
        {
            int err = ErrorCodes::ERR_JSON;
            qDebug()<<"创建房间失败,error json解析失败 "<<err;
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
    _handlers.insert(ReqId::ID_JOIN_ROOM_RSP,[this](ReqId id,int len,QByteArray data){
        qDebug()<<"handle id is "<<id<<" data is "<<data;

        message::JoinRoomRsp rsp;
        // 从 QByteArray 解析 Protobuf
        if (!rsp.ParseFromArray(data.data(), data.size()))
        {
            qDebug() << "Create/Join Room Rsp: Protobuf解析失败";
            return;
        }
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
            _reconnect_timer.stop();
            _reconnect_cnt = 0;

            emit sig_resume_join_finish(room_info);
            return;
        }
        emit sig_join_room_finish(room_info);   //发送给UI，UI 根据 members 初始化列表

    });

    // 注册有人加入广播
    _handlers.insert(ReqId::ID_USER_JOIN_BROADCAST, [this](ReqId id, int len, QByteArray data) {
        message::UserJoinRoomBroadcast msg;
        if (!msg.ParseFromArray(data.data(), data.size()))
        {
            qDebug() << "解析 UserJoinRoomBroadcast 失败";
            return;
        }

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
    _handlers.insert(ReqId::ID_USER_LEAVE_BROADCAST, [this](ReqId id, int len, QByteArray data) {
        message::UserLeaveRoomBroadcast msg;
        if (!msg.ParseFromArray(data.data(), data.size()))
        {
            qDebug() << "解析 UserLeaveRoomBroadcast 失败";
            return;
        }

        int uid = msg.uid();
        qDebug() << "广播: 用户离开 - UID:" << uid;

        // 发送信号通知 UI 移除对应的那一行
        emit sig_user_left(uid);
    });

    //注册绘画广播 (服务器转发别人画的)
    _handlers.insert(ReqId::ID_DRAW_RSP,[this](ReqId,int,QByteArray data){
        emit sig_draw_broadcast(data);  //发送广播信号
    });

    // 注册收到群聊消息
    _handlers.insert(ReqId::ID_CHAT_RSP,[this](ReqId,int,QByteArray data){
        message::ChatRsp rsp;
        if(!rsp.ParseFromArray(data.data(),data.size()))
        {
            qDebug() << "解析 UserLeaveRoomBroadcast 失败";
            return;
        }

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

void TcpMgr::handleMsg(ReqId id, int len, QByteArray data)
{
    auto it = _handlers.find(id);
    if(it == _handlers.end())
    {
        qDebug()<<"not found id ["<<id<<"] to handle";
        return;
    }
    it.value()(id,len,data);
}

void TcpMgr::slot_tcp_connect(ServerInfo si)
{
    qDebug()<<"接收到 tcp connect singal";

    //尝试连接服务器
    qDebug()<<"Connnecting to Server......";
    _host = si.Host;
    _port = static_cast<uint16_t>(si.Port.toUInt());

    _socket.setProxy(QNetworkProxy::NoProxy);           //禁用代理
    _socket.connectToHost(_host,_port);                 //连接服务器
}

void TcpMgr::slot_send_data(ReqId reqid, QByteArray data)
{
    quint16 id = reqid;
    quint16 len = static_cast<quint16>(data.size());

    //创建一个QByteArray用于存储准备发送的数据
    QByteArray block;
    QDataStream out(&block,QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);       //设置网络字节序(大端字节序)

    //写入包头
    out<<id<<len;

    //写入body
    block.append(data);
    _socket.write(block);                           //发送数据
}

void TcpMgr::slot_switch_server(const QString &host, int port,const QString& room_id, int uid)
{

    _is_offline_reconnect = false; // 切服不是掉线重连
    // 先把任务记在小本本上
    _pending_room_id = room_id;
    _pending_uid = uid;
    _host = host;
    _port = port;

    //主动断开当前连接
    _socket.abort();
    qDebug() << "[TcpMgr] Switching to " << host << ":" << port;
    _socket.connectToHost(host, port);

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
    // 如果已经在等 timer，就别重复启动
    if (_reconnect_timer.isActive())
        return;
    int backoff = calc_backoff_ms();
    qDebug() << "[TcpMgr] will reconnect in" << backoff << "ms, cnt=" << _reconnect_cnt;
    _reconnect_timer.start(backoff);    //启动定时器,backoff ms 后开始重连
}

void TcpMgr::slot_do_reconnect()    // 真正进行一次 connectToHost
{
    //优先连接 "房间所属服务器", 没有就连接当前 _host/_post
    QString host = !_room_host.isEmpty() ? _room_host : _host;
    uint16_t port = (_room_port != 0) ? _room_port : _port;

    if (host.isEmpty() || port == 0)
    {
        qDebug() << "[TcpMgr] no host/port to reconnect";
        emit sig_go_lobby("没有可用服务器地址，请重新登录。");
        _is_offline_reconnect = false;
        return;
    }

    // 次数+1，用于下次退避更久
    _reconnect_cnt++;
    // 确保 socket 干净
    if (_socket.state() != QAbstractSocket::UnconnectedState)
        _socket.abort();

    _host = host;
    _port = port;

    qDebug() << "[TcpMgr] reconnecting to" << host << ":" << port;
    _socket.setProxy(QNetworkProxy::NoProxy);
    _socket.connectToHost(host, port);
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
