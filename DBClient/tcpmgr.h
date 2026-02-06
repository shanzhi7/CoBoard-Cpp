#ifndef TCPMGR_H
#define TCPMGR_H

#include "global.h"
#include "singleton.h"
#include <QObject>
#include <QTcpSocket>
#include <QAbstractSocket>
#include <QMap>
#include <QByteArray>
#include <functional>

class TcpMgr : public QObject,public Singleton<TcpMgr>,public std::enable_shared_from_this<TcpMgr>
{
    Q_OBJECT
public:
    explicit TcpMgr();
public:
    friend class Singleton<TcpMgr>;

private:
    QTcpSocket _socket;         //套接字
    QString _host;              //ip
    uint16_t _port;              //端口
    QByteArray _buffer;         //接收数据用的缓冲区
    bool _b_recv_pending;       //是否有等待读取的消息体
    quint16  _message_id;       //请求id
    quint16 _message_len;       //内容长度

    // 【新增】暂存重定向后的目标房间信息
    QString _pending_room_id;
    int _pending_uid = 0;

    QMap<ReqId,std::function<void(ReqId,int,QByteArray)>> _handlers;

    void initHandlers();        //注册回包函数
    void handleMsg(ReqId id,int len,QByteArray data);

signals:
    void sig_con_success(bool);                                         //连接成功发送该信号
    void sig_send_data(ReqId reqid,QByteArray data);                    //发送tcp包信号
    void sig_login_failed(int err);                                     //登录失败信号

    void sig_switch_canvas();                                           //发送切换canvas信号

    void sig_create_room_finish(std::shared_ptr<RoomInfo>);             //发送创建房间完毕信号
    void sig_join_room_finish(std::shared_ptr<RoomInfo> room_info);     //发送加入房间完毕信号

    void sig_user_joined(UserInfo member);                              //用户加入 (广播)
    void sig_user_left(int uid);                                        //用户离开 (广播)

public slots:
    void slot_tcp_connect(ServerInfo si);                               //用于发起tcp连接请求
    void slot_send_data(ReqId reqid,QByteArray data);                   //用于发送数据给服务器
    void slot_switch_server(const QString& host,int port,const QString& room_id, int uid);              //重定向连接服务器
};

#endif // TCPMGR_H
