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
#include <QTimer>

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

    // 暂存重定向后的目标房间信息
    QString _pending_room_id;
    int _pending_uid = 0;

    // --断线重连 (指数退避) 相关--
    QTimer _reconnect_timer;        //重连定时器 (singleShot)
    int _reconnect_cnt;             //重连失败次数 (用于指数退避)
    bool _is_offline_reconnect = false; //用于是否处于 "掉线重连流程" (区分重定向切服流程)

    //恢复房间信息 (用于自动join)
    QString _resume_room_id;        //上次所在房间id (Join成功后记录)
    QString _room_host;             //房间所在CanvasServer (Join成功后记录)
    uint16_t _room_port = 0;


    QMap<ReqId,std::function<void(ReqId,int,QByteArray)>> _handlers;

    void initHandlers();        //注册回包函数
    void handleMsg(ReqId id,int len,QByteArray data);   //处理收到的Msg

    int calc_backoff_ms();                              //计算指数退避时间
    void send_canvas_login();                           //复用LoginWidget 的 CanvasLogin 组包

signals:
    void sig_con_success(bool);                                         //连接成功发送该信号
    void sig_send_data(ReqId reqid,QByteArray data);                    //发送tcp包信号
    void sig_login_failed(int err);                                     //登录失败信号

    void sig_switch_canvas();                                           //发送切换canvas信号

    void sig_create_room_finish(std::shared_ptr<RoomInfo>);             //发送创建房间完毕信号
    void sig_join_room_finish(std::shared_ptr<RoomInfo> room_info);     //发送加入房间完毕信号

    void sig_user_joined(UserInfo member);                              //用户加入 (广播)
    void sig_user_left(int uid);                                        //用户离开 (广播)

    void sig_draw_broadcast(QByteArray data);                           // 绘画广播 (二进制 DrawReq)

    void sig_go_lobby(QString tip);                                     //断线后回到大厅并且提示
    void sig_resume_join_finish(std::shared_ptr<RoomInfo> room_info);   //掉线恢复 Join 成功 (用于切回canvas)


public slots:
    void slot_tcp_connect(ServerInfo si);                               //用于发起tcp连接请求
    void slot_send_data(ReqId reqid,QByteArray data);                   //用于发送数据给服务器
    void slot_switch_server(const QString& host,int port,const QString& room_id, int uid);              //重定向连接服务器
    void slot_start_reconnect();                                        // 启动/继续指数退避重连
    void slot_do_reconnect();                                           // 真正执行一次 connectToHost
};

#endif // TCPMGR_H
