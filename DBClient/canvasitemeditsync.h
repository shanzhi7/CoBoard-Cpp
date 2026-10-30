/***********************************************************************************
* @file         canvasitemeditsync.h
* @brief        图元编辑和删除的版本确认、预览、失败恢复与条件撤销
* @author       shanzhi
* @date         2026/10/15
* @history
***********************************************************************************/
#pragma once

#include "canvasitemeditstate.h"
#include "message.pb.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QStringList>

class PaintScene;

class CanvasItemEditSync : public QObject
{
    Q_OBJECT
public:
    explicit CanvasItemEditSync(PaintScene* scene, QObject* parent = nullptr); // 为指定场景创建无阻塞的编辑确认队列。
    void Configure(const QString& room_id, int uid, bool offline, bool editable); // 更新房间上下文，换房时废弃旧请求。
    void SubmitDelete(const QStringList& item_ids); // 按同一目标集合提交条件删除，逐项确认并汇总失败。
    bool IsDeleted(const QString& item_id) const; // 查询本房间删除墓碑，阻止迟到创建重新显示目标。
    void Clear(); // 取消手势并恢复最后确认的状态，停止旧房间请求。
    void Submit(const CanvasItemEditState& before, const CanvasItemEditState& after,
                const QString& gesture_id, bool finished); // 合并同一目标的连续预览与最终状态。
    void ApplyResponse(const message::ItemEditOperationRsp& response); // 按版本处理本地确认、远端更新及历史。
    void ObserveLegacy(const QString& item_id, quint64 revision, bool deleted = false, bool is_local_creation = false); // 将原文字和图片消息纳入同一确认版本。
    void SetCreationPending(const QString& item_id, bool pending); // 图片上传完成前只保存本地预览，不提前提交编辑。
    void Undo(const CanvasItemEditUndoOperation& operation); // 按各目标记录的版本恢复一次完整手势。
    bool HasPending() const; // 导出或继续撤销前判断是否仍有未确认编辑。
    quint64 Revision(const QString& item_id) const; // 返回目标的最新服务端确认版本。

signals:
    void SigSendOperation(QByteArray body); // 把已限制包长的消息交给 Canvas 的 TCP 入口。
    void SigError(QString message); // 提供冲突和失败反馈，不包含凭据。
    void SigItemDeleted(QString item_id, quint64 revision); // 删除确认后清理窗口持有的文字缓存和图片任务。
    void SigStateConfirmed(QString item_id, quint64 revision); // 同步独立文字的兼容版本缓存。

private:
    struct PendingEdit
    {
        CanvasItemEditState _before; // 本次手势开始时的状态，供最终确认后登记撤销。
        CanvasItemEditState _desired; // 最近一次本地预览，等待合并发送。
        CanvasItemEditState _sent; // 当前在途请求提交的状态。
        QString _gesture_id; // 多选移动的公共撤销关联。
        QString _operation_id; // 同一手势的预览、取消和最终提交共用的关联标识。
        quint64 _expected_revision = 0; // 当前发送必须匹配的目标版本。
        qint64 _sent_at_ms = 0; // 请求发出时的单调时钟毫秒值，确认超时后解除交互锁。
        bool _preview_sent = false; // 当前手势已广播预览，取消时通知其他客户端恢复。
        bool _waiting = false; // 仅最终提交等待确认，预览发送不受往返延迟限制。
        bool _finished = false; // 鼠标或编辑器已经结束，最终状态仍需确认。
        QString _delete_group_id; // 非空表示条件删除，关联一次多选删除的汇总结果。
        bool _is_undo = false; // 撤销确认不能再次登记撤销记录。
    };
    struct RemotePreview
    {
        QString _operation_id; // 匹配取消消息，旧手势不能清除新预览。
        int _uid = 0; // 预览发起者，与请求标识一起隔离不同成员。
        qint64 _received_at_ms = 0; // 最近预览到达时间，失联后恢复已提交状态。
    };
    struct DeleteGroup
    {
        int _remaining = 0; // 尚未结束的目标数。
        int _failed = 0; // 已拒绝、冲突或超时的目标数。
    };
    void ApplyDeletedState(const QString& item_id, quint64 revision); // 成功删除或权威缺失共用墓碑、交互及资源清理。
    void FinishDeleteGroup(const QString& group_id, bool success); // 完成一个目标，仅在整组结束时报告失败数量。
    void Flush(); // 每 16 毫秒发送最新预览，最终状态只提交一次并等待确认。
    void ApplyPreview(const message::ItemEditOperationRsp& response); // 应用不推进版本的预览或取消，不生成撤销记录。
    void ExpireRemotePreviews(); // 发起者失联且预览停止后恢复服务端已提交快照。
    void Complete(const QString& item_id); // 完成已确认手势并解除临时交互锁。
    void CancelTarget(const QString& item_id, bool restore); // 停止目标手势并可选恢复最近确认值。
    QPointer<PaintScene> _scene; // 场景销毁后自动失效，不持有图元裸指针。
    QTimer _timer; // GUI 线程中的合并发送定时器。
    QElapsedTimer _request_clock; // 确认超时采用单调时间，不受系统时钟调整影响。
    QString _room_id; // 当前房间标识，拒绝旧房间回包。
    int _uid = 0; // 当前应用登录用户。
    bool _offline = true; // 离线时由场景直接登记撤销。
    bool _editable = false; // 当前权限，撤销时立即停止后续发送。
    QHash<QString, PendingEdit> _pending; // 各目标连续预览独立合并，只有最终提交占用在途请求。
    QHash<QString, RemotePreview> _remote_previews; // 临时显示状态的归属和超时，独立于已确认版本。
    QHash<QString, CanvasItemEditState> _confirmed; // 已确认状态，用于冲突及断线恢复。
    QHash<QString, quint64> _revisions; // 各目标共享编辑版本。
    QHash<QString, quint64> _generic_revisions; // 已应用的通用版本，允许同版本创建后的首个完整快照。
    QHash<QString, DeleteGroup> _delete_groups; // 按用户操作聚合删除结果。
    QSet<QString> _deleted; // 当前房间已删除的稳定 ID。
    bool _canceling = false; // 取消回调期间禁止场景信号重新创建请求。
    QSet<QString> _creation_pending; // 尚未被服务器确认创建的本地图片。
};
