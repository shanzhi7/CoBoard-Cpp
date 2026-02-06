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
        emit sig_con_success(true);

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

            // 直接调用底层的发送函数，或者 emit sig_send_data
            this->slot_send_data(ReqId::ID_JOIN_ROOM_REQ, sendData);

            //发完之后清空任务，防止下次普通重连时误发
            _pending_room_id.clear();
            _pending_uid = 0;
        }
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
        }
        int err = jsonObj["error"].toInt();
        if(err != ErrorCodes::SUCCESS)
        {
            qDebug()<<"登录失败,error is "<<err;
            emit sig_login_failed(err);
            return;
        }

        qDebug()<<"tcpmgr: recv error is "<<jsonObj["error"].toInt()<<" uid is "<<jsonObj["uid"].toInt();
        emit sig_switch_canvas();   //发送信号切换canvas界面
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
