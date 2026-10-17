#include "canvasitemeditsync.h"

#include "canvasitemeditcodec.h"
#include "paintscene.h"

#include <QUuid>

namespace
{
constexpr qint64 EDIT_CONFIRM_TIMEOUT_MS = 5000; // 长连接未返回最终确认时最多等待五秒。
constexpr qint64 PREVIEW_HEARTBEAT_MS = 1000; // 鼠标暂停时维持活动预览，避免其他成员误判为失联。
constexpr qint64 PREVIEW_TIMEOUT_MS = 5000; // 发起者失联后临时预览最多保留五秒。
}

CanvasItemEditSync::CanvasItemEditSync(PaintScene* scene, QObject* parent)
    : QObject(parent), _scene(scene)
{
    // 1. 精确定时器按 16 毫秒合并预览，单调时钟用于最终确认和失联恢复。
    _request_clock.start();
    _timer.setTimerType(Qt::PreciseTimer);
    _timer.setInterval(16);
    connect(&_timer, &QTimer::timeout, this, &CanvasItemEditSync::Flush);
    connect(scene, &PaintScene::SigItemEditChanged, this, &CanvasItemEditSync::Submit);
}

void CanvasItemEditSync::Configure(const QString& room_id, int uid, bool offline, bool editable)
{
    // 1. 房间或身份改变时使原确认队列失效，权限撤销仅取消当前手势。
    if (_room_id != room_id || _uid != uid || _offline != offline)
    {
        Clear();
    }
    if (_editable && !editable)
    {
        _editable = false;
        const auto ids = _pending.keys();
        for (const QString& id : ids)
        {
            CancelTarget(id, true);
        }
    }
    _room_id = room_id;
    _uid = uid;
    _offline = offline;
    _editable = editable;
    if (_scene)
    {
        _scene->SetItemEditUndoEnabled(offline);
    }
    // 2. 只读成员仍需检查远端预览超时，在线期间始终保留接收恢复定时器。
    if (!offline)
    {
        _timer.start();
    } else
    {
        _timer.stop();
    }
}

void CanvasItemEditSync::Clear()
{
    // 1. 先结束活动手势，随后清理版本，迟到回包不能恢复旧房间图元。
    _timer.stop();
    _editable = false;
    const auto ids = _pending.keys();
    for (const QString& id : ids)
    {
        CancelTarget(id, true);
    }
    // 2. 换房前撤销本地显示中的远端预览，不能把临时位置当作最终文档。
    for (const QString& id : _remote_previews.keys())
    {
        if (_scene && _confirmed.contains(id))
        {
            _scene->ApplyItemEditState(_confirmed.value(id));
        }
    }
    _remote_previews.clear();
    _offline = true;
    _uid = 0;
    _confirmed.clear();
    _revisions.clear();
    _generic_revisions.clear();
    _creation_pending.clear();
    _room_id.clear();
}

void CanvasItemEditSync::Submit(const CanvasItemEditState& before, const CanvasItemEditState& after,
                               const QString& gesture_id, bool finished)
{
    // 1. 离线撤销由场景处理；非法或已失去权限的目标不生成网络操作。
    if (!_scene || _canceling || _offline || !_editable || _room_id.isEmpty() || after._item_id.isEmpty())
    {
        return;
    }
    if (_pending.contains(after._item_id) && _pending.value(after._item_id)._gesture_id != gesture_id)
    {
        // 已结束但未确认的旧手势不能被新的按键或拖动覆盖。
        _scene->ApplyItemEditState(_pending.value(after._item_id)._desired);
        return;
    }
    if (!_pending.contains(after._item_id))
    {
        if (before == after)
        {
            return;
        }
        PendingEdit pending;
        pending._before = _confirmed.value(after._item_id, before);
        pending._gesture_id = gesture_id;
        pending._operation_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        pending._expected_revision = _revisions.value(after._item_id);
        _pending.insert(after._item_id, pending);
        if (!_confirmed.contains(after._item_id))
        {
            _confirmed.insert(after._item_id, before);
        }
    }
    // 2. 最终鼠标状态保留到确认之后；拖动中不锁住图元以免截断手势。
    PendingEdit& pending = _pending[after._item_id];
    pending._desired = after;
    pending._finished = pending._finished || finished;
    if (finished)
    {
        _scene->SetItemEditPending(after._item_id, true);
    }
}

void CanvasItemEditSync::Flush()
{
    // 1. 接收方包括只读成员都清理失联预览，发送仍受房间编辑权限约束。
    ExpireRemotePreviews();
    if (!_scene || _offline || !_editable)
    {
        return;
    }
    // 2. 只有最终提交等待确认；拖动中的预览从不因 ACK 未到而停止发送。
    int timeout_count = 0;
    const qint64 now_ms = _request_clock.elapsed();
    const auto ids = _pending.keys();
    for (const QString& id : ids)
    {
        auto iterator = _pending.find(id);
        if (iterator == _pending.end() || _creation_pending.contains(id))
        {
            continue;
        }
        if (iterator->_waiting)
        {
            if (now_ms - iterator->_sent_at_ms >= EDIT_CONFIRM_TIMEOUT_MS)
            {
                CancelTarget(id, true);
                ++timeout_count;
            }
            continue;
        }
        if (!_scene->FindCanvasItem(id))
        {
            CancelTarget(id, false);
            continue;
        }
        PendingEdit& pending = iterator.value();
        const CanvasItemEditState current = _confirmed.value(id, pending._before);
        quint32 fields = CanvasItemEditCodec::ChangedFields(current, pending._desired);
        if (pending._finished && fields == 0 && !pending._preview_sent)
        {
            Complete(id);
            continue;
        }
        if (!pending._finished && pending._preview_sent && pending._desired == pending._sent &&
            now_ms - pending._sent_at_ms < PREVIEW_HEARTBEAT_MS)
        {
            continue;
        }
        // 3. 拖回起点仍要覆盖先前预览；最终空变化也提交一次以结束远端显示。
        if (fields == 0)
        {
            fields = CanvasItemEditCodec::ChangedFields(pending._sent, pending._desired);
            if (fields == 0)
            {
                fields = 1U;
            }
        }
        message::ItemEditOperation operation;
        operation.set_preview(!pending._finished);
        operation.set_uid(_uid);
        operation.set_room_id(_room_id.toStdString());
        operation.set_operation_id(pending._operation_id.toStdString());
        operation.set_target_item_id(id.toStdString());
        operation.set_expected_revision(pending._expected_revision);
        operation.set_fields(fields);
        *operation.mutable_state() = CanvasItemEditCodec::Encode(pending._desired);
        std::string body;
        if (!operation.SerializeToString(&body) || body.size() > 65535)
        {
            CancelTarget(id, true);
            emit SigError(QStringLiteral("图元编辑数据超过协议包长限制"));
            continue;
        }
        pending._sent = pending._desired;
        pending._sent_at_ms = now_ms;
        pending._waiting = pending._finished;
        pending._preview_sent = pending._preview_sent || !pending._finished;
        emit SigSendOperation(QByteArray::fromStdString(body));
    }

    // 4. 超时不能假定服务端已接受修改；解除本地锁，迟到的确认仍按版本正常恢复。
    if (timeout_count > 0)
    {
        emit SigError(QStringLiteral("%1 个图元编辑确认超时，已恢复确认状态并解除锁定，请检查画板服务版本或连接").arg(timeout_count));
    }
}

void CanvasItemEditSync::Complete(const QString& item_id)
{
    // 1. 只有完整手势的最终状态确认后才能进入撤销栈。
    const PendingEdit pending = _pending.take(item_id);
    if (!_scene)
    {
        return;
    }
    _scene->SetItemEditPending(item_id, false);
    const CanvasItemEditState after = _confirmed.value(item_id, pending._desired);
    if (pending._is_undo || pending._before == after)
    {
        _scene->RebaseItemEditUndo(item_id, after, _revisions.value(item_id));
    } else
    {
        _scene->RecordItemEditOperation(pending._before, after, pending._gesture_id, _revisions.value(item_id));
    }
}

void CanvasItemEditSync::CancelTarget(const QString& item_id, bool restore)
{
    // 1. 先移除待提交值，结束手势时产生的本地信号不能复活该请求。
    const PendingEdit pending = _pending.take(item_id);
    if (pending._preview_sent && !_offline && !_room_id.isEmpty())
    {
        message::ItemEditOperation cancel;
        cancel.set_uid(_uid);
        cancel.set_room_id(_room_id.toStdString());
        cancel.set_target_item_id(item_id.toStdString());
        cancel.set_operation_id(pending._operation_id.toStdString());
        cancel.set_preview(true);
        cancel.set_cancel_preview(true);
        emit SigSendOperation(QByteArray::fromStdString(cancel.SerializeAsString()));
    }
    // 2. 本地恢复只读取确认快照，不能把最近一次预览写入撤销记录。
    if (!_scene)
    {
        return;
    }
    const bool was_canceling = _canceling;
    _canceling = true;
    _scene->CancelItemInteraction(item_id);
    _canceling = was_canceling;
    _scene->SetItemEditPending(item_id, false);
    if (restore && _confirmed.contains(item_id))
    {
        _scene->ApplyItemEditState(_confirmed.value(item_id));
    }
}

void CanvasItemEditSync::ApplyResponse(const message::ItemEditOperationRsp& response)
{
    // 1. 各图元独立比较版本；创建消息后的同版本完整快照只允许应用一次。
    if (!_scene || _offline || !response.has_operation() ||
        response.operation().room_id() != _room_id.toStdString())
    {
        return;
    }
    const auto& operation = response.operation();
    if (operation.preview() && response.error() == 0)
    {
        ApplyPreview(response);
        return;
    }
    const QString id = QString::fromStdString(operation.target_item_id());
    const quint64 revision = operation.revision();
    auto iterator = _pending.find(id);
    const bool own = iterator != _pending.end() &&
        iterator->_operation_id == QString::fromStdString(operation.operation_id());
    // 已取消手势的迟到失败只属于旧请求，不能中止同一图元的新手势。
    if (response.error() != 0 && !own)
    {
        return;
    }
    if (revision < _revisions.value(id))
    {
        if (own)
        {
            CancelTarget(id, true);
        }
        return;
    }
    if (!own && response.error() == 0 && _generic_revisions.contains(id) &&
        revision <= _generic_revisions.value(id))
    {
        return;
    }
    CanvasItemEditState state;
    const bool has_state = response.has_current_state() && CanvasItemEditCodec::Decode(response.current_state(), &state) && state._item_id == id;
    if ((response.has_current_state() && !has_state) || (response.error() == 0 && !has_state))
    {
        if (own)
        {
            CancelTarget(id, true);
            emit SigError(QStringLiteral("图元编辑确认内容无效，本次操作已取消"));
        }
        return;
    }
    _remote_previews.remove(id);
    if (has_state)
    {
        _confirmed.insert(id, state);
    }
    _revisions.insert(id, revision);
    if (response.error() == 0)
    {
        _generic_revisions.insert(id, revision);
    }
    // 2. 他人的真实编辑使旧文字创建撤销失效，不能借新版本删除别人改过的文字。
    if (!own && operation.uid() != _uid && !operation.operation_id().empty())
    {
        _scene->InvalidateTextUndo(id);
    }
    // 3. 失败及远端竞争取消手势，缺失目标同步删除，避免留下乐观状态的幽灵图元。
    if (response.error() != 0 || (!own && iterator != _pending.end()))
    {
        CancelTarget(id, has_state);
        if (!has_state)
        {
            _confirmed.remove(id);
            _scene->RemoveCanvasItem(id);
        }
        emit SigError(response.error_message().empty()
            ? QStringLiteral("图元已由其他成员修改，本次编辑已停止")
            : QString::fromStdString(response.error_message()));
    } else if (own)
    {
        PendingEdit& pending = iterator.value();
        pending._waiting = false;
        pending._expected_revision = revision;
        // 只有最终提交成功才使用服务端规范状态，并登记一次完整手势撤销。
        if (pending._desired == pending._sent)
        {
            pending._desired = state;
            _scene->ApplyItemEditState(state);
            if (pending._finished)
            {
                Complete(id);
            }
        }
    } else if (has_state)
    {
        CancelTarget(id, false);
        _scene->ApplyItemEditState(state);
    }
    emit SigStateConfirmed(id, revision);
}

void CanvasItemEditSync::ApplyPreview(const message::ItemEditOperationRsp& response)
{
    // 1. 预览不参与确认去重；同一基础版本可以连续接收任意帧，但不能覆盖新提交。
    const auto& operation = response.operation();
    const QString id = QString::fromStdString(operation.target_item_id());
    const quint64 revision = operation.revision();
    if (id.isEmpty() || operation.uid() == _uid || revision < _revisions.value(id) || _pending.contains(id))
    {
        return;
    }
    if (operation.cancel_preview())
    {
        // 只有当前显示的同一成员、同一手势可以取消，迟到取消不影响后续手势。
        const auto iterator = _remote_previews.constFind(id);
        if (iterator == _remote_previews.constEnd() || iterator->_uid != operation.uid() ||
            iterator->_operation_id != QString::fromStdString(operation.operation_id()))
        {
            return;
        }
        _remote_previews.remove(id);
        CanvasItemEditState confirmed;
        if (response.has_current_state() && CanvasItemEditCodec::Decode(response.current_state(), &confirmed) && confirmed._item_id == id)
        {
            _confirmed.insert(id, confirmed);
            _revisions.insert(id, revision);
            _scene->ApplyItemEditState(confirmed);
        } else if (!response.has_current_state())
        {
            _confirmed.remove(id);
            _scene->RemoveCanvasItem(id);
        }
        return;
    }

    // 2. 保存服务端真正已提交的基线，失联恢复不能依赖屏幕上另一位成员的临时位置。
    CanvasItemEditState state;
    CanvasItemEditState confirmed;
    if (!response.has_current_state() || !response.has_confirmed_state() ||
        !CanvasItemEditCodec::Decode(response.current_state(), &state) || state._item_id != id ||
        !CanvasItemEditCodec::Decode(response.confirmed_state(), &confirmed) || confirmed._item_id != id)
    {
        return;
    }
    _confirmed.insert(id, confirmed);
    _revisions.insert(id, revision);
    _remote_previews.insert(id, RemotePreview{QString::fromStdString(operation.operation_id()), operation.uid(), _request_clock.elapsed()});
    _scene->ApplyItemEditState(state);
}

void CanvasItemEditSync::ExpireRemotePreviews()
{
    // 1. 正常拖动由轻量心跳续期，连接中断或进程退出后按单调时间恢复已提交位置。
    const qint64 now_ms = _request_clock.elapsed();
    for (const QString& id : _remote_previews.keys())
    {
        if (now_ms - _remote_previews.value(id)._received_at_ms < PREVIEW_TIMEOUT_MS)
        {
            continue;
        }
        _remote_previews.remove(id);
        if (_scene && !_pending.contains(id) && _confirmed.contains(id))
        {
            _scene->ApplyItemEditState(_confirmed.value(id));
        }
    }
}

void CanvasItemEditSync::ObserveLegacy(const QString& item_id, quint64 revision, bool deleted, bool is_local_creation)
{
    // 1. 原创建和编辑通道共享版本；重复确认不能中止已经开始的新手势。
    if (!_scene || _offline || revision < _revisions.value(item_id))
    {
        return;
    }
    const bool had_revision = _revisions.contains(item_id);
    if (!deleted && had_revision && revision == _revisions.value(item_id) && !_creation_pending.contains(item_id))
    {
        return;
    }
    _remote_previews.remove(item_id);
    _revisions.insert(item_id, revision);
    if (deleted)
    {
        CancelTarget(item_id, false);
        _confirmed.remove(item_id);
        _creation_pending.remove(item_id);
        return;
    }
    // 2. 调用者已经应用旧协议的规范状态，先保存再取消，防止取消草稿覆盖远端更新。
    if (CanvasItem* item = _scene->FindCanvasItem(item_id))
    {
        _confirmed.insert(item_id, CanvasItemEdit::Capture(item));
    }
    if (is_local_creation)
    {
        if (_pending.contains(item_id))
        {
            _pending[item_id]._expected_revision = revision;
        }
    } else if (had_revision || _pending.contains(item_id))
    {
        CancelTarget(item_id, true);
    }
    _creation_pending.remove(item_id);
}

void CanvasItemEditSync::SetCreationPending(const QString& item_id, bool pending)
{
    // 1. 未上传的图片尚无服务器目标，禁止发送会被必然拒绝的变换请求。
    if (pending)
    {
        _creation_pending.insert(item_id);
    } else
    {
        _creation_pending.remove(item_id);
    }
}

void CanvasItemEditSync::Undo(const CanvasItemEditUndoOperation& operation)
{
    // 1. 多选是一条本地记录，目标分别检查版本，冲突项不得覆盖他人的修改。
    if (!_scene || _offline || !_editable)
    {
        return;
    }
    int conflicts = 0;
    for (const CanvasItemEditState& before : operation._before)
    {
        const QString& id = before._item_id;
        if (!_scene->FindCanvasItem(id) || _pending.contains(id) ||
            _revisions.value(id) != operation._revisions.value(id))
        {
            ++conflicts;
            continue;
        }
        PendingEdit pending;
        pending._before = _scene->CaptureItemEditState(id);
        pending._desired = before;
        pending._gesture_id = operation._gesture_id;
        pending._operation_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        pending._expected_revision = operation._revisions.value(id);
        pending._finished = true;
        pending._is_undo = true;
        _pending.insert(id, pending);
        _scene->ApplyItemEditState(before);
        _scene->SetItemEditPending(id, true);
    }
    // 2. 汇总反馈，避免多选冲突时连续弹出多个提示。
    if (conflicts)
    {
        emit SigError(QStringLiteral("%1 个图元已变化，已跳过这些图元的撤销").arg(conflicts));
    }
}

bool CanvasItemEditSync::HasPending() const
{
    // 1. 未确认结果不能作为可导出的最终快照。
    return !_pending.isEmpty();
}

quint64 CanvasItemEditSync::Revision(const QString& item_id) const
{
    // 1. 尚未编辑的普通绘图使用初始版本零。
    return _revisions.value(item_id);
}
