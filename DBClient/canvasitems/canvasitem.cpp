#include "canvasitem.h"
#include "textitem.h"
#include "../canvasitemeditstate.h"

#include <QKeyEvent>
#include <QGraphicsScene>

CanvasItem::CanvasItem(const QString& item_id, QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , _item_id(item_id)
{
    // 这些标志由基类统一开启，使图片、文字等后续图元具备一致的选择和拖动行为。
    // ItemSendsGeometryChanges 是后续在线变换同步的前提，不能只依赖鼠标事件捕获。
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemIsFocusable, true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
}

CanvasItem::~CanvasItem()
{
    // QGraphicsScene 会负责场景中的图元生命周期；这里不主动操作 scene，避免析构期间重复移除。
}

QString CanvasItem::itemId() const
{
    // 通过值返回标识，调用方不会持有基类内部字符串的引用。
    return _item_id;
}

void CanvasItem::setItemId(const QString& item_id)
{
    // 空标识无法参与远端历史关联，拒绝写入可以避免后续广播出现不可追踪的图元。
    if (item_id.isEmpty())
    {
        return;
    }

    _item_id = item_id;
}

void CanvasItem::setInteractionEnabled(bool enabled)
{
    // 1. 权限切换必须同时关闭选择、鼠标拖动和键盘焦点，避免只读成员改变本地图元状态。
    _interaction_enabled = enabled;
    setFlag(QGraphicsItem::ItemIsMovable, enabled);
    setFlag(QGraphicsItem::ItemIsSelectable, enabled);
    setFlag(QGraphicsItem::ItemIsFocusable, enabled);
    if (!enabled)
    {
        setSelected(false);
        clearFocus();
    }
}

bool CanvasItem::isInteractionEnabled() const
{
    // 1. 返回图元编辑许可，模式和待确认状态仍由场景控制。
    return _interaction_enabled;
}

QPainterPath CanvasItem::shape() const
{
    // 1. 共用几何适配器，避免显示范围与鼠标首次命中范围混淆。
    return CanvasItemEdit::Outline(this);
}

TextItem* CanvasItem::BoundTextItem() const
{
    // 1. 子组件由 Qt 父子关系释放，调用方不得独立登记或销毁。
    return _bound_text_item;
}

void CanvasItem::SetBoundTextData(const QString& text, const TextStyle& style)
{
    // 1. 只有明确的封闭几何支持附属文字，空文字也保留可重用编辑器。
    if (!CanvasItemEdit::IsClosed(this)) return;
    if (!_bound_text_item)
    {
        _bound_text_item = new TextItem(itemId() + QStringLiteral("/label"), this);
        _bound_text_item->setFlag(ItemIsSelectable, false);
        _bound_text_item->setFlag(ItemIsMovable, false);
        _bound_text_item->setAcceptedMouseButtons(Qt::NoButton);
        connect(_bound_text_item, &TextItem::SigLayoutChanged, this, &CanvasItem::UpdateBoundTextLayout);
    }

    // 2. 批量替换内容时禁止递归增高，随后用最终文档尺寸完成一次布局。
    const bool was_active = _is_bound_text_layout_active;
    _is_bound_text_layout_active = true;
    _bound_text_item->SetTextData({itemId() + QStringLiteral("/label"), text, style, _bound_text_item->pos()});
    _is_bound_text_layout_active = was_active;
    UpdateBoundTextLayout();
}

void CanvasItem::SetBoundTextLayoutEnabled(bool enabled)
{
    // 1. 快照恢复先写完父几何与文本，再统一刷新布局。
    _is_bound_text_layout_enabled = enabled;
}

void CanvasItem::UpdateBoundTextLayout()
{
    // 1. 文档布局变化会重入尺寸通知，使用图元级守卫保证一次稳定更新。
    if (!_bound_text_item || _is_bound_text_layout_active || !_is_bound_text_layout_enabled) return;
    _is_bound_text_layout_active = true;
    CanvasItemEdit::LayoutBoundText(this);
    _bound_text_item->setVisible(!_bound_text_item->GetTextData()._text.isEmpty() || _bound_text_item->IsEditing());
    _is_bound_text_layout_active = false;
}

QVariant CanvasItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    // 先交给 Qt 完成内部状态更新，再从当前图元读取最终场景坐标，避免发出旧位置。
    const QVariant result = QGraphicsObject::itemChange(change, value);

    if (change == QGraphicsItem::ItemPositionHasChanged ||
        change == QGraphicsItem::ItemTransformHasChanged ||
        change == QGraphicsItem::ItemRotationHasChanged ||
        change == QGraphicsItem::ItemScaleHasChanged)
    {
        // 场景矩形包含旋转和缩放后的范围，发送它比本地坐标更适合跨客户端恢复。
        const QRectF scene_rect = mapRectToScene(boundingRect());
        emit sigGeometryChanged(_item_id, scene_rect, rotation(), scale());
    }

    return result;
}

void CanvasItem::keyPressEvent(QKeyEvent* event)
{
    if (!event)
    {
        return;
    }

    // 删除由上层场景决定是否立即执行，避免图元直接销毁导致在线操作绕过权限和序列化。
    if (_interaction_enabled &&
        (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace))
    {
        emit sigDeleteRequested(_item_id);
        event->accept();
        return;
    }

    // 未处理按键交给 QGraphicsObject，保持 Qt 默认焦点和快捷键行为。
    QGraphicsObject::keyPressEvent(event);
}
