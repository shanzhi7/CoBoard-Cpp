/***********************************************************************************
* @file         paintscene.h
* @brief        画布绘图、图片选择及本地和远端图元生命周期管理
* @author       shanzhi
* @date         2026/10/07
* @history
***********************************************************************************/
#pragma once

#include <memory>

#include <QGraphicsScene>
#include <QHash>
#include <QPixmap>
#include <QPointer>
#include <QSize>
#include <QSizeF>
#include <QStack>
#include <QSet>

#include "canvasinteractionmode.h"
#include "canvasdocument.h"
#include "canvasitemeditstate.h"
#include "drawtool.h"
#include "canvasitems/textitem.h"

class QGraphicsEllipseItem;
class QGraphicsSceneMouseEvent;
class QKeyEvent;
class ImageItem;
class CanvasSelectionController;

class PaintScene : public QGraphicsScene
{
    Q_OBJECT

public:
    explicit PaintScene(QObject* parent = nullptr); // 初始化绘图工厂和橡皮擦辅助光标。
    ~PaintScene() override; // 在场景删除内容前回收交互覆盖层和控制器。

    enum class UndoKind
    {
        None, // 撤销栈为空。
        Creation, // 普通笔迹或图片创建。
        Text, // 兼容原有文字创建及编辑记录。
        ItemEdit // 图元变换或附属文字的完整状态操作。
    };
    CanvasItem* FindCanvasItem(const QString& item_id) const; // 按稳定 ID 查找本地、远端或导入的顶层图元。
    CanvasItemEditState CaptureItemEditState(const QString& item_id) const; // 读取图元完整编辑快照。
    bool ApplyItemEditState(const CanvasItemEditState& state); // 恢复完整状态并刷新本地选择框。
    void CancelItemInteraction(const QString& item_id); // 远端覆盖或删除前取消目标正在进行的编辑。
    void SetItemEditPending(const QString& item_id, bool pending); // 最终状态确认前锁定目标，保持只读选择。
    void SetItemEditUndoEnabled(bool enabled); // 离线立即登记，在线等待确认才登记撤销。
    void RecordItemEditOperation(const CanvasItemEditState& before, const CanvasItemEditState& after, const QString& gesture_id, quint64 revision); // 合并同一手势并记录确认版本。
    bool TakeItemEditUndo(CanvasItemEditUndoOperation* operation); // 仅取出栈顶完整图元操作，保持统一时间顺序。
    void InvalidateItemEditUndo(const QString& item_id); // 删除或冲突后只废弃目标在各条手势中的记录。
    void RebaseItemEditUndo(const QString& item_id, const CanvasItemEditState& confirmed, quint64 revision); // 条件撤销成功后推进前一操作的确认版本。
    bool RemoveCanvasItem(const QString& item_id); // 安全删除任意顶层图元并清理所有操作引用。
    UndoKind NextUndoKind() const; // 返回栈顶操作类型供在线撤销分派。
    void SubmitItemEditChange(const CanvasItemEditState& before, const CanvasItemEditState& after, const QString& gesture_id, bool finished); // 控制器提交状态，场景统一通知与登记离线撤销。

    void setPenColor(const QColor& color); // 设置之后创建的笔画颜色。
    void setPenWidth(int width); // 设置之后创建的笔画宽度。
    void setShapeType(ShapeType type); // 结束未完成笔画后切换绘图策略。
    void SetInteractionMode(CanvasInteractionMode mode); // 切换事件模式并保留当前图元选择。
    CanvasInteractionMode InteractionMode() const; // 返回当前场景事件模式。
    void setEditable(bool editable); // 更新权限并立即停止未授权的本地交互。
    bool isEditable() const; // 返回本地编辑权限。
    QColor getPenColor(); // 返回当前画笔颜色。
    int getPenWidth(); // 返回当前画笔宽度。
    void hideEraserCursor(); // 鼠标离开画布或模式变化时隐藏辅助光标。
    void applyRemoteDraw(const message::DrawReq& req); // 将远端绘画请求应用到对应图元。
    void resetScene(); // 清空图元、交互状态及本地和远端记录。
    bool canUndoLocal() const; // 判断是否存在可撤销的本地图元。
    void undoLastLocalItem(); // 删除最后一个本地图元，不影响远端图元。
    bool BuildCanvasDocument(CanvasDocument* document, QString* error_message) const; // 按场景绘制顺序构建独立快照。
    bool LoadCanvasDocument(const CanvasDocument& document, QString* error_message); // 仅向尚未接入视图的新场景恢复图元。
    QImage RenderCanvasImage(QString* error_message); // 按画布原尺寸渲染并恢复交互反馈。
    TextItem* AddTextItem(const TextItemData& data); // 创建文字并登记稳定 ID，不生成撤销记录。
    TextItem* FindTextItem(const QString& item_id) const; // 查找场景拥有的文字图元。
    bool UpdateTextItem(const TextItemData& data); // 应用完整的已确认文字状态。
    bool RemoveTextItem(const QString& item_id); // 删除文字并使对应撤销记录失效。
    bool BeginTextEditing(TextItem* item); // 切换唯一活动文字编辑器。
    bool FinishTextEditing(bool is_cancel = false); // 提交或取消草稿，空内容按删除处理。
    bool IsTextEditing() const; // 返回是否存在活动文字草稿。
    TextItem* ActiveTextItem() const; // 返回编辑文字或单个选中文字。
    void SetTextStyle(const TextStyle& style); // 更新新建默认样式或活动文字样式。
    void SetTextUndoEnabled(bool enabled); // 离线即时登记撤销，在线等待确认后登记。
    void SetTextPending(const QString& item_id, bool is_pending); // 确认期间禁止目标文字再次修改。
    void RecordTextOperation(const TextItemData& before, const TextItemData& after); // 登记创建或完整编辑快照。
    void InvalidateTextUndo(const QString& item_id); // 远端修改或删除后使该文字的本地记录失效。
    bool TakeTextUndoOperation(TextItemData* before, bool* is_create); // 取出最近一条仍有效的本地文字操作。
    bool CanUndoLocalOperation() const; // 返回统一撤销栈是否有记录。
    void UndoLastLocalOperation(); // 离线撤销最近操作，文字修改保留当前坐标。

    ImageItem* addImageItem(const QString& item_id,
                           const QString& asset_id,
                           const QString& asset_ref,
                           const QString& asset_sha256,
                           const QString& mime_type,
                           const QSize& original_size,
                           const QPixmap& pixmap,
                           const QPointF& scene_pos,
                           const QSizeF& display_size = QSizeF(),
                           bool record_undo = true); // 创建图片并按当前模式配置交互。
    bool removeImageItem(const QString& item_id); // 按稳定 ID 删除图片并清理撤销记录。
    ImageItem* findImageItem(const QString& item_id) const; // 按 ID 查找图片，缺失时返回 nullptr。
    bool updateImageTransform(const QString& item_id,
                              const QPointF& scene_pos,
                              const QSizeF& display_size,
                              qreal scale_x,
                              qreal scale_y,
                              qreal rotation); // 应用调用方已经校验过的远端图片变换。

signals:
    void SigItemEditChanged(CanvasItemEditState before, CanvasItemEditState after, QString gesture_id, bool finished); // 通知实时预览或完整编辑结束。
    void SigLocalCreationFinished(QString item_id); // 图元创建完成后请求上层切换鼠标工具。
    void SigTextEditingChanged(); // 活动文字编辑器切换后同步样式控件。
    void SigTextCommitted(TextItemData before, TextItemData after); // 完整编辑结束后提交创建或修改。
    void SigTextDeleteRequested(QString item_id); // 转交删除操作，不建立删除撤销。
    void SigTextMoved(QString item_id); // 拖动松开时提交最终坐标。
    void SigTextError(QString message); // 转发文本长度等编辑错误。
    void SigUndoRequested(); // 非文本编辑期间的 Ctrl+Z 交给窗口。
    void sigStrokeStart(QString uuid, int type, QPointF startPos, QColor color, int width); // 转发本地 START 的 ID、样式和起点。
    void sigStrokeMove(QString uuid, int type, QPointF currentPos); // 转发本地路径点或几何终点。
    void sigStrokeEnd(QString uuid, int type, QPointF endPos); // 转发本地 END 和最终位置。
    void sigCursorPosChanged(QPointF pos); // 向状态栏转发非手形模式的场景坐标。
    void sigImageInserted(QString item_id,
                          QString asset_id,
                          QString asset_ref,
                          QString asset_sha256,
                          QString mime_type,
                          QSize original_size,
                          QPointF scene_pos,
                          QSizeF display_size); // 通知 Canvas 图片已创建，资源同步由 Canvas 负责。
    void sigImageGeometryChanged(QString item_id, QRectF scene_rect, qreal rotation, qreal scale); // 转发实时变换和拖动结束的最终状态。
    void sigImageDeleteRequested(QString item_id); // 通知 Canvas 执行图片删除。
    void sigImageRetryRequested(QString item_id); // 转发鼠标模式下的图片重试意图。
    void sigImagePreviewRequested(QString item_id); // 转发鼠标模式下的图片预览意图。

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override; // 按模式分发图片选择或绘图 START。
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override; // 更新图片拖动或绘图 MOVE。
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override; // 提交图片最终状态或绘图 END。
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override; // 仅鼠标模式向图片分发双击事件。
    void keyPressEvent(QKeyEvent* event) override; // 按模式和权限处理图片编辑快捷键。

private:
    void ConnectTextItem(TextItem* item); // 重建文字索引和编辑信号。
    void ConfigureCanvasItem(CanvasItem* item); // 统一配置内容的选择权限，拖动交给控制器。
    bool BeginBoundTextEditing(CanvasItem* item); // 在封闭图元内部创建或继续编辑附属文字。
    void UpdateTextInteraction(TextItem* item); // 按工具、权限和待确认状态更新移动能力。
    void finishCurrentOperation(const QPointF& end_pos); // 补齐笔画并记录撤销和 END。
    void clearCurrentOperation(); // 释放当前操作引用，不删除图元。
    void recordFinishedLocalItem(const QString& item_id,
                                 const std::shared_ptr<IDrawTool>& operation); // 登记完成图元和撤销句柄。
    void updateEraserCursor(const QPointF& pos); // 更新与擦除宽度一致的光标。
    void PrepareImageDrag(); // 记录 Qt 鼠标抓取对应的多选图片起始位置。
    void FinishImageDrag(bool send_final); // 清理拖动记录，并按需补发最终状态。
    void UpdateImageInteraction(ImageItem* image_item); // 更新移动和焦点权限，保留选择状态。
    bool removeImageItemInternal(const QString& item_id, bool remove_undo_record); // 删除图片并可选清理撤销记录。

    struct RemoteItem
    {
        ShapeType shape = Shape_Unknown; // 远端协议图元类型。
        std::shared_ptr<IDrawTool> operation; // 同一 ID 的独立远端操作。
    };

    struct DrawItemRecord
    {
        QString item_id; // 本地操作关联的稳定标识。
        ShapeType shape = Shape_Unknown; // 绘画类型，图片使用 Shape_Unknown。
        std::shared_ptr<IDrawTool> operation; // 绘画句柄，图片记录为空。
        ImageItem* image_item = nullptr; // 场景拥有的图片指针。
        bool is_image = false; // 当前记录是否为图片。
        bool is_text = false; // 当前记录是否为文字操作。
        TextItemData before_text; // 创建前为空，修改前保存内容及样式。
        TextItemData after_text; // 完整操作完成后的文字值。
        bool is_item_edit = false; // 标记变换或附属文字的完整快照记录。
        CanvasItemEditUndoOperation item_edit; // 同一手势中的一个或多个图元快照。
    };

    ShapeType _currShapeType = Shape_Pen; // 当前绘图类型，不包含交互模式。
    std::shared_ptr<IDrawTool> _currentTool; // 当前绘图策略。
    QString _currUuid; // 当前本地笔画 ID。
    std::shared_ptr<IDrawTool> _currentOperation; // 未完成的本地绘图操作。
    QColor _penColor = Qt::black; // 新图元使用的画笔颜色。
    int _penWidth = 3; // 新图元使用的画笔宽度。
    bool _editable = true; // 本地编辑权限。
    CanvasInteractionMode _interaction_mode = CanvasInteractionMode::Draw; // 场景事件分发模式。
    QGraphicsEllipseItem* _eraserCursorItem = nullptr; // 不参与绘画和同步的辅助光标。
    QHash<QString, RemoteItem> _remoteItems; // 远端图元 ID 对应的操作。
    QStack<DrawItemRecord> _localUndoStack; // 保持本地创建顺序的撤销栈。
    QHash<QString, QGraphicsItem*> _localItems; // 本地绘画图元 ID 索引。
    QHash<QString, ImageItem*> _imageItems; // 本地和远端图片统一索引。
    QHash<QString, QPointF> _image_drag_start_positions; // 拖动起始位置，避免持有图片裸指针。
    QHash<QString, TextItem*> _text_itemMap; // 场景拥有的文字稳定 ID 索引。
    QHash<QString, QPointF> _text_drag_positionMap; // 鼠标拖动开始时的文字坐标。
    QSet<QString> _pending_text_ids; // 等待在线确认的文字 ID。
    QPointer<TextItem> _editing_text_item; // 场景销毁图元后自动清空活动引用。
    TextItemData _text_before_edit; // 完整编辑操作的开始快照。
    TextStyle _text_style; // 没有目标文字时使用的新建默认样式。
    bool _is_text_undo_enabled = true; // 在线仅由确认回包登记撤销。
    CanvasSelectionController* _selection_controller = nullptr; // 场景拥有的独立选择和几何手势控制器。
    CanvasItemEditState _text_before_item_state; // 文本编辑前的父图元或独立文字完整快照。
    QString _text_edit_gesture_id; // 一次文本编辑及自动增高共用的操作标识。
    bool _is_item_edit_undo_enabled = true; // 在线等待确认后才记录完整图元操作。
    mutable QHash<QString, QPointer<CanvasItem>> _canvas_itemMap; // 所有来源顶层图元的统一弱引用索引。
};
