#include "sessioncontroller.h"
#include "message.pb.h"
#include <QUuid>

SessionController::SessionController(QObject* parent) : QObject(parent)
{
    // 1. 服务器确认使用单次定时器，超时只进入重试，不宣称操作完成。
    _request_timer.setSingleShot(true);
    _request_timer.setInterval(10000);
    connect(&_request_timer, &QTimer::timeout, this, [this]() {
        Fail(QStringLiteral("服务器尚未确认完成，请重试"));
    });
}

void SessionController::SetAccount(int uid, const QString& token)
{
    // 1. 新登录建立全新操作上下文，旧账号回包不能匹配新的关联。
    _request_timer.stop();
    _uid = uid;
    _token = token;
    _room_info.reset();
    _exit_target.reset();
    _room_request_id.clear();
    _logout_request_id.clear();
    _logout_requested = false;
    _state = State::Idle;
}

void SessionController::SetRoom(std::shared_ptr<RoomInfo> room_info)
{
    // 1. 只在无退出事务时接受入房结果，待完成退出不能被迟到 Join 覆盖。
    if (_state == State::Idle) _room_info = std::move(room_info);
}

bool SessionController::HasRoom() const
{
    // 1. 业务身份在断线恢复期间保留，直到服务器确认结束。
    return _room_info && !_room_info->offline;
}

bool SessionController::IsRoomOwner() const
{
    // 1. 用账号 ID 和服务端房主字段判断，不依赖画面初始化顺序。
    return HasRoom() && _room_info->owner_uid == _uid;
}

bool SessionController::IsBusy() const
{
    // 1. 失败后的重试状态同样保护原操作上下文。
    return _state != State::Idle;
}

void SessionController::RequestRoomExit()
{
    // 1. 重复点击只在失败状态触发重试，正在等待时不生成第二个请求。
    if (_state == State::RetryRoom)
    {
        StartRoomExit();
        return;
    }
    if (_state != State::Idle || !HasRoom()) return;
    // 2. 保存独立目标快照，后续断线和页面清理不改变该请求。
    _exit_target = std::make_shared<RoomInfo>(*_room_info);
    _room_request_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    StartRoomExit();
}

void SessionController::RequestLogout()
{
    // 1. 失败重试沿用原凭据和关联，不重复发起正在等待的事务。
    if (_state == State::RetryLogout)
    {
        StartLogout();
        return;
    }
    if (_state == State::RetryRoom)
    {
        _logout_requested = true;
        StartRoomExit();
        return;
    }
    if (_state != State::Idle || _uid <= 0 || _token.isEmpty()) return;
    // 2. 当前有房间时先完成退出，暂回大厅仍须遵守相同顺序。
    _logout_requested = true;
    if (HasRoom()) RequestRoomExit();
    else StartLogout();
}

void SessionController::StartRoomExit()
{
    // 1. 退出期间屏蔽普通自动重连和 Join，连接恢复只为完成原退出。
    _state = State::WaitingRoom;
    emit SigExitPendingChanged(true);
    emit SigBusyChanged(true, false);
    _request_timer.start();
    // 2. 传输层负责判断连接是否可用，鉴权完成后再发送请求。
    emit SigPrepareExitConnection(_exit_target->host, _exit_target->port);
}

void SessionController::OnExitConnectionReady()
{
    // 1. 迟到连接事件不能改变清理或注销阶段。
    if (_state != State::WaitingRoom || !_exit_target) return;
    // 2. 身份不放入房间请求，服务端以认证 Session 为准。
    std::string body;
    const bool is_owner = _exit_target->owner_uid == _uid;
    if (is_owner)
    {
        message::DestroyRoomReq request;
        request.set_room_id(_exit_target->id.toStdString());
        request.set_request_id(_room_request_id.toStdString());
        request.SerializeToString(&body);
    } else
    {
        message::LeaveRoomReq request;
        request.set_room_id(_exit_target->id.toStdString());
        request.set_request_id(_room_request_id.toStdString());
        request.SerializeToString(&body);
    }
    emit SigSendRoomRequest(is_owner ? ID_DESTROY_ROOM_REQ : ID_LEAVE_ROOM_REQ, QByteArray::fromStdString(body));
}

void SessionController::OnRoomResponse(QString room_id, QString request_id, int error)
{
    // 1. 超时后到达的成功仍然有效，其他房间或旧请求结果一律忽略。
    if ((_state != State::WaitingRoom && _state != State::RetryRoom) || !_exit_target ||
        room_id != _exit_target->id || request_id != _room_request_id) return;
    // 2. 只有服务器成功确认才销毁本地房间上下文。
    if (error == message::SUCCESS) CompleteRoom(QStringLiteral("已退出房间"));
    else Fail(QStringLiteral("退出尚未完成，错误码 %1；请重试").arg(error));
}

void SessionController::OnRoomDestroyed(QString room_id, QString reason)
{
    // 1. 暂回大厅和正在申请普通退出的成员都统一处理房间结束。
    if (!_room_info || room_id != _room_info->id || _state == State::CleaningRoom) return;
    CompleteRoom(reason);
}

void SessionController::CompleteRoom(QString reason)
{
    // 1. 服务器已确认，立即废弃房间任务与恢复信息；语音资源异步回收。
    _request_timer.stop();
    _state = State::CleaningRoom;
    _end_reason = std::move(reason);
    _room_info.reset();
    _exit_target.reset();
    emit SigExitPendingChanged(true);
    emit SigBusyChanged(true, false);
    emit SigRoomCleanupRequested();
    // 2. 清理完成信号可能立即发出，状态必须先发布。
    emit SigLeaveVoiceRequested();
}

void SessionController::OnVoiceLeft()
{
    // 1. 只响应本轮房间清理，其他语音停止事件不能推进账号状态。
    if (_state != State::CleaningRoom) return;
    _room_request_id.clear();
    // 2. 注销复用房间清理但不额外切页；普通退房恢复大厅操作。
    if (_logout_requested) StartLogout();
    else
    {
        _state = State::Idle;
        emit SigExitPendingChanged(false);
        emit SigBusyChanged(false, false);
        emit SigShowLobby(_end_reason);
    }
}

void SessionController::StartLogout()
{
    // 1. 注销失败重试沿用原 UUID，旧 HTTP 完成结果不会影响后续账号。
    if (_logout_request_id.isEmpty()) _logout_request_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    _state = State::WaitingLogout;
    emit SigExitPendingChanged(true);
    emit SigBusyChanged(true, false);
    _request_timer.start();
    // 2. 仅向传输层交付必要账号上下文，不清除尚需重试的凭据。
    QJsonObject request;
    request[QStringLiteral("uid")] = _uid;
    request[QStringLiteral("token")] = _token;
    request[QStringLiteral("request_id")] = _logout_request_id;
    emit SigSendLogoutRequest(request);
}

void SessionController::OnLogoutResponse(QString request_id, int uid, int error)
{
    // 1. 仅匹配本轮账号和请求，包括超时后到达的成功。
    if ((_state != State::WaitingLogout && _state != State::RetryLogout) || request_id != _logout_request_id || uid != _uid) return;
    if (error != message::SUCCESS)
    {
        Fail(QStringLiteral("账号注销尚未完成，错误码 %1；请重试").arg(error));
        return;
    }
    // 2. 服务端已撤销凭据，释放控制器敏感上下文并通知主窗口最终清理。
    _request_timer.stop();
    _token.clear();
    _uid = 0;
    _state = State::Idle;
    _logout_requested = false;
    _logout_request_id.clear();
    emit SigLogoutFinished();
    emit SigBusyChanged(false, false);
}

void SessionController::OnTransportFailure(QString message)
{
    // 1. 只有等待网络确认的阶段进入重试，资源回收不受旧网络错误干扰。
    if (_state == State::WaitingRoom || _state == State::WaitingLogout) Fail(std::move(message));
}

void SessionController::Fail(QString message)
{
    // 1. 保留目标、凭据和关联，只停止本次等待。
    _request_timer.stop();
    if (_state == State::WaitingRoom || _state == State::RetryRoom) _state = State::RetryRoom;
    else if (_state == State::WaitingLogout || _state == State::RetryLogout) _state = State::RetryLogout;
    else return;
    // 2. 已退房的注销失败在大厅重试，仍有房间时保留当前页面和内容。
    if (_state == State::RetryLogout) emit SigShowLobby(QStringLiteral("已退出房间，账号注销待重试"));
    emit SigBusyChanged(true, true);
    emit SigFailure(message);
}
