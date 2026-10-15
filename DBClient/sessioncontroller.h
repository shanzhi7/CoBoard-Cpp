/***********************************************************************************
* @file         sessioncontroller.h
* @brief        房间退出与账号注销状态协调，传输和界面通过信号解耦
* @author       shanzhi
* @date         2026/10/14
* @history
***********************************************************************************/
#pragma once

#include "global.h"
#include <QObject>
#include <QTimer>
#include <QJsonObject>
#include <memory>

class SessionController : public QObject
{
    Q_OBJECT
public:
    explicit SessionController(QObject* parent = nullptr); // 创建主窗口拥有的生命周期控制器。
    void SetAccount(int uid, const QString& token); // 保存当前登录上下文，凭据禁止记录日志。
    void SetRoom(std::shared_ptr<RoomInfo> room_info); // 保存业务房间资料，断线清画面不会丢失目标。
    bool HasRoom() const; // 判断仍有房间身份。
    bool IsRoomOwner() const; // 判断退出是否需要解散确认。
    bool IsBusy() const; // 判断退出、清理或注销是否尚未完成。
public slots:
    void RequestRoomExit(); // 发起退出或重试原请求。
    void RequestLogout(); // 先退房再注销，重试保持原注销关联。
    void OnExitConnectionReady(); // 认证恢复完成后只重发退出，不自动加入。
    void OnRoomResponse(QString room_id, QString request_id, int error); // 匹配退出响应并处理成功或可重试失败。
    void OnRoomDestroyed(QString room_id, QString reason); // 处理当前房间被解散，包括暂回大厅的情况。
    void OnLogoutResponse(QString request_id, int uid, int error); // 关联账号撤销结果。
    void OnVoiceLeft(); // 语音资源回收后推进页面或注销。
    void OnTransportFailure(QString message); // 保留未完成目标，提示重试。
signals:
    void SigExitPendingChanged(bool pending); // 控制 TCP 自动重连及普通业务发送。
    void SigPrepareExitConnection(QString host, int port); // 恢复目标服务器连接并鉴权。
    void SigSendRoomRequest(ReqId id, QByteArray body); // 交给 TCP 发送已序列化的退出请求。
    void SigSendLogoutRequest(QJsonObject request); // 交给 HTTP 管理器执行账号注销。
    void SigRoomCleanupRequested(); // 清理画布、任务、房间身份和恢复目标。
    void SigLeaveVoiceRequested(); // 异步离开语音并等待完成信号。
    void SigBusyChanged(bool busy, bool can_retry); // 更新界面等待和重试入口。
    void SigFailure(QString message); // 展示有业务含义的失败信息。
    void SigShowLobby(QString message); // 已完成房间清理后显示大厅。
    void SigLogoutFinished(); // 服务器确认注销后释放本地账号并显示登录页。
private:
    enum class State
    {
        Idle, // 无未完成退出操作。
        WaitingRoom, // 等待退房连接或服务器响应。
        RetryRoom, // 保留房间退出目标，等待用户重试。
        CleaningRoom, // 服务器已确认，等待语音资源释放。
        WaitingLogout, // 等待账号 Token 撤销。
        RetryLogout // 已退房，账号注销等待重试。
    };
    void StartRoomExit(); // 固定目标及请求关联，启动一次连接及退房尝试。
    void StartLogout(); // 保留账号快照和关联，发起一次注销尝试。
    void CompleteRoom(QString reason); // 一次性清理当前房间并异步回收语音。
    void Fail(QString message); // 停止本次等待并保留可重试上下文。
    QTimer _request_timer; // 单次服务器确认最多等待十秒。
    State _state = State::Idle; // 生命周期进度，等待期间禁止普通房间操作。
    int _uid = 0; // 当前账号 ID。
    QString _token; // 当前注销所需凭据，只保存到完成为止。
    std::shared_ptr<RoomInfo> _room_info; // 当前房间业务身份，区别于画面连接状态。
    std::shared_ptr<RoomInfo> _exit_target; // 退出请求发起时的目标快照。
    QString _room_request_id; // 相同退房操作的 UUID。
    QString _logout_request_id; // 相同账号注销的 UUID。
    QString _end_reason; // 完成清理后的大厅提示。
    bool _logout_requested = false; // 退房完成后是否继续注销。
};
