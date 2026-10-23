/***********************************************************************************
* @file         tcpmgr.h
* @brief        画布 TCP 通信、房间重定向和登录连接生命周期
* @author       shanzhi
* @date         2026/10/08
* @history
***********************************************************************************/
#pragma once

#include "global.h"
#include "singleton.h"
#include "decodedtcpmessage.h"
#include <QObject>
#include <QAbstractSocket>
#include <QMap>
#include <QByteArray>
#include <functional>
#include <QTimer>
#include <QThread>

class CanvasTcpWorker;

class TcpMgr : public QObject,public Singleton<TcpMgr>,public std::enable_shared_from_this<TcpMgr>
{
    Q_OBJECT
public:
    explicit TcpMgr(); // 初始化画布连接、消息处理表和重连定时器。
    ~TcpMgr() override; // 停止 worker 并等待网络线程回收。
public:
    friend class Singleton<TcpMgr>;

private:
    QString _host;              //ip
    uint16_t _port;              //端口

    // 暂存重定向后的目标房间信息
    QString _pending_room_id; // 重定向后等待加入的房间 ID。
    int _pending_uid = 0; // 重定向后等待重新登录的用户 ID。

    // --断线重连 (指数退避) 相关--
    QTimer* _reconnect_timer = nullptr; //重连定时器 (singleShot)，挂到 TcpMgr 保证线程随对象迁移
    int _reconnect_cnt;             //重连失败次数 (用于指数退避)
    bool _is_offline_reconnect = false; //用于是否处于 "掉线重连流程" (区分重定向切服流程)
    bool _exit_pending = false; // 正在退出时禁止普通重连、自动 Join 和房间提交。
    bool _is_authenticated = false; // 当前 TCP 连接是否已完成 Canvas 鉴权。
    bool _is_connected = false; // UI 线程记录的当前代次连接状态。
    bool _room_request_pending = false; // 创建或加入尚未获得结果时不能提前注销。
    bool _is_logged_out = false; // 主动退出后屏蔽旧连接通知和自动重连。

    //恢复房间信息 (用于自动join)
    QString _resume_room_id;        //上次所在房间id (Join成功后记录)
    QString _room_host;             //房间所在CanvasServer (Join成功后记录)
    uint16_t _room_port = 0; // 恢复房间时使用的服务器端口。


    QMap<ReqId, std::function<void(const DecodedTcpMessage::Payload&)>> _handlers; // 已解码消息对应的 UI 业务处理函数。
    QThread _network_thread; // 专用事件循环，由 TcpMgr 停止并等待退出。
    CanvasTcpWorker* _worker = nullptr; // 网络线程拥有的 worker，线程结束后删除。
    quint64 _connection_generation = 0; // 过滤连接切换前已经排队的旧事件。

    void initHandlers();        //注册回包函数
    void HandleMessage(quint64 generation, const DecodedTcpMessage& decoded); // 在 UI 线程分派当前连接的已解码结果。
    void StartConnection(); // 推进代次并排队连接当前服务器。
    void StopWorker(); // 应用退出或析构时关闭 socket 并回收线程。

    int calc_backoff_ms();                              //计算指数退避时间
    void send_canvas_login();                           //复用LoginWidget 的 CanvasLogin 组包

signals:
    void SigConnectRequested(QString host, quint16 port, quint64 generation); // 向 worker 排队建立连接。
    void SigSendRequested(quint16 message_id, QByteArray body, quint64 generation); // 向 worker 排队发送请求。
    void SigDisconnectRequested(quint64 generation); // 向 worker 排队注销连接。
    void SigExitConnectionReady(); // 退出专用连接已完成认证。
    void SigExitTransportFailed(QString message); // 退出连接失败，控制器保留原请求。
    void SigRoomExitResponse(QString room_id, QString request_id, int error); // 普通退出或解散的关联结果。
    void SigRoomDestroyed(QString room_id, QString reason); // 当前房间被服务端解散。
    void SigItemEditReceived(const message::ItemEditOperationRsp& response); // 转发已解码的通用图元编辑确认与历史快照。
    void SigTextOperationReceived(const message::TextOperationRsp& response); // 转发已解码的文字确认、远端状态和房间历史。
    void sig_con_success(bool);                                         //连接成功发送该信号
    void sig_send_data(ReqId reqid,QByteArray data);                    //发送tcp包信号
    void sig_login_failed(int err);                                     //登录失败信号

    void sig_switch_canvas();                                           //发送切换canvas信号

    void sig_create_room_finish(std::shared_ptr<RoomInfo>);             //发送创建房间完毕信号
    void sig_join_room_finish(std::shared_ptr<RoomInfo> room_info);     //发送加入房间完毕信号

    void sig_user_joined(UserInfo member);                              //用户加入 (广播)
    void sig_user_left(int uid);                                        //用户离开 (广播)

    void sig_draw_broadcast(const message::DrawReq& request); // 转发已解码的绘画广播。
    void sig_image_operation_broadcast(const message::ImageOperation& operation); // 转发已解码的图片图元操作。
    void sig_permission_changed(int target_uid, bool can_edit);          // 房间编辑权限变更广播

    void sig_go_lobby(QString tip);                                     //断线后回到大厅并且提示
    void sig_resume_join_finish(std::shared_ptr<RoomInfo> room_info);   //掉线恢复 Join 成功 (用于切回canvas)

    // 接收群聊消息信号
    void sig_chat_received(int uid,
                           const QString& name,
                           const QString& avatarUrl,
                           const QString& roomId,
                           const QString& content,
                           qulonglong serverTs); // 通知当前房间的聊天消息。


public slots:
    void slot_tcp_connect(ServerInfo si);                               //用于发起tcp连接请求
    void slot_send_data(ReqId reqid,QByteArray data);                   //用于发送数据给服务器
    void slot_grant_edit(const QString& room_id, int target_uid);        //房主授权成员编辑
    void slot_revoke_edit(const QString& room_id, int target_uid);       //房主取消成员编辑权限
    void slot_switch_server(const QString& host,int port,const QString& room_id, int uid);              //重定向连接服务器
    void SetExitPending(bool pending); // 暂停自动回房和普通房间请求，不屏蔽退出回包。
    void PrepareExitConnection(const QString& host, int port); // 恢复连接后鉴权，退出事务不自动 Join。
    void ClearRoomRecovery(); // 清除已结束房间的重定向与自动恢复目标。
    bool IsRoomRequestPending() const; // 查询尚未完成的创建或加入。
    void Logout(); // 主动退出账号并停止后续自动重连。
    void slot_start_reconnect();                                        // 启动/继续指数退避重连
    void slot_do_reconnect();                                           // 真正执行一次 connectToHost
};
