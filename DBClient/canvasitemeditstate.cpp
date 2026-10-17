#include "canvasitemeditstate.h"

#include "canvasitems/arrowitem.h"
#include "canvasitems/diamonditem.h"
#include "canvasitems/eraseritem.h"
#include "canvasitems/imageitem.h"
#include "canvasitems/lineitem.h"
#include "canvasitems/ovalitem.h"
#include "canvasitems/penitem.h"
#include "canvasitems/rectitem.h"

#include <cmath>

#include <QPainterPathStroker>
#include <QSignalBlocker>

namespace
{
QPen ItemPen(const CanvasItem* item)
{
    // 1. 样式留在各图元中，适配层只读取线宽和连接方式。
    if (const auto* value = dynamic_cast<const PathItem*>(item))
    {
        return value->pen();
    }
    if (const auto* value = dynamic_cast<const RectItem*>(item))
    {
        return value->pen();
    }
    if (const auto* value = dynamic_cast<const OvalItem*>(item))
    {
        return value->pen();
    }
    if (const auto* value = dynamic_cast<const LineItem*>(item))
    {
        return value->pen();
    }
    if (const auto* value = dynamic_cast<const ArrowItem*>(item))
    {
        return value->Pen();
    }
    if (const auto* value = dynamic_cast<const DiamondItem*>(item))
    {
        return value->Pen();
    }
    return QPen(Qt::NoPen);
}

void SetRectGeometry(CanvasItem* item, const QRectF& rect)
{
    // 1. 保持各图元的场景索引通知及箭头、菱形缓存实现。
    if (auto* value = dynamic_cast<RectItem*>(item))
    {
        value->setRect(rect);
    } else if (auto* value = dynamic_cast<OvalItem*>(item))
    {
        value->setRect(rect);
    } else if (auto* value = dynamic_cast<DiamondItem*>(item))
    {
        value->SetRect(rect);
    }
}

QTransform RectMapping(const QRectF& source, const QRectF& target)
{
    // 1. 退化路径轴不做除法，只沿该轴平移；直线退化轴由控制器端点规则处理。
    const qreal scale_x = source.width() > 0.000001 ? target.width() / source.width() : 1.0;
    const qreal scale_y = source.height() > 0.000001 ? target.height() / source.height() : 1.0;
    return QTransform(scale_x, 0, 0, scale_y,
                      target.left() - source.left() * scale_x,
                      target.top() - source.top() * scale_y);
}
}

bool CanvasItemEditState::operator==(const CanvasItemEditState& other) const
{
    // 1. 忽略临时光标和选择状态，只比较可同步及可撤销的数据。
    return _item_id == other._item_id && _kind == other._kind && _position == other._position &&
           _origin == other._origin && _rotation == other._rotation && _scale == other._scale &&
           _transform == other._transform && _geometry == other._geometry && _text == other._text &&
           _text_style == other._text_style;
}

bool CanvasItemEditState::operator!=(const CanvasItemEditState& other) const
{
    // 1. 统一差异规则，避免实时提交和撤销登记使用不同判定。
    return !(*this == other);
}

int CanvasItemEdit::Kind(const CanvasItem* item)
{
    // 1. 类型编号与协议约定一致，橡皮擦保留类型但控制器不开放编辑。
    if (dynamic_cast<const EraserItem*>(item))
    {
        return 5;
    }
    if (dynamic_cast<const PenItem*>(item))
    {
        return 1;
    }
    if (dynamic_cast<const RectItem*>(item))
    {
        return 2;
    }
    if (dynamic_cast<const OvalItem*>(item))
    {
        return 3;
    }
    if (dynamic_cast<const LineItem*>(item))
    {
        return 4;
    }
    if (dynamic_cast<const ArrowItem*>(item))
    {
        return 6;
    }
    if (dynamic_cast<const DiamondItem*>(item))
    {
        return 7;
    }
    if (dynamic_cast<const ImageItem*>(item))
    {
        return 8;
    }
    if (dynamic_cast<const TextItem*>(item))
    {
        return 9;
    }
    return 0;
}

bool CanvasItemEdit::IsClosed(const CanvasItem* item)
{
    // 1. 仅矩形、椭圆和菱形具有明确的附属文字内部。
    const int kind = Kind(item);
    return kind == 2 || kind == 3 || kind == 7;
}

QRectF CanvasItemEdit::Bounds(const CanvasItem* item)
{
    // 1. 选择框使用真实几何，线宽和控制柄不写入图元状态。
    if (const auto* value = dynamic_cast<const PathItem*>(item))
    {
        return value->path().boundingRect();
    }
    if (const auto* value = dynamic_cast<const RectItem*>(item))
    {
        return value->rect();
    }
    if (const auto* value = dynamic_cast<const OvalItem*>(item))
    {
        return value->rect();
    }
    if (const auto* value = dynamic_cast<const DiamondItem*>(item))
    {
        return value->Rect();
    }
    if (const auto* value = dynamic_cast<const LineItem*>(item))
    {
        return QRectF(value->line().p1(), value->line().p2()).normalized();
    }
    if (const auto* value = dynamic_cast<const ArrowItem*>(item))
    {
        return QRectF(value->Line().p1(), value->Line().p2()).normalized();
    }
    if (const auto* value = dynamic_cast<const ImageItem*>(item))
    {
        return QRectF(QPointF(), value->displaySize());
    }
    if (const auto* value = dynamic_cast<const TextItem*>(item))
    {
        return value->boundingRect().adjusted(1, 1, -1, -1);
    }
    return QRectF();
}

QPainterPath CanvasItemEdit::Interior(const CanvasItem* item)
{
    // 1. 内部命中与首次轮廓选择分开，双击才使用封闭路径。
    QPainterPath path;
    const QRectF rect = Bounds(item);
    if (dynamic_cast<const RectItem*>(item))
    {
        path.addRect(rect);
    } else if (dynamic_cast<const OvalItem*>(item))
    {
        path.addEllipse(rect);
    } else if (dynamic_cast<const DiamondItem*>(item))
    {
        path.moveTo(rect.center().x(), rect.top());
        path.lineTo(rect.right(), rect.center().y());
        path.lineTo(rect.center().x(), rect.bottom());
        path.lineTo(rect.left(), rect.center().y());
        path.closeSubpath();
    }
    return path;
}

QPainterPath CanvasItemEdit::Outline(const CanvasItem* item)
{
    // 1. 图片和独立文字保留整个区域命中，其余类型按实际笔迹计算。
    QPainterPath path;
    if (!item)
    {
        return path;
    }
    const int kind = Kind(item);
    if (kind == 8 || kind == 9)
    {
        path.addRect(Bounds(item));
        return path;
    }
    if (const auto* value = dynamic_cast<const PathItem*>(item))
    {
        path = value->path();
    } else if (const auto* value = dynamic_cast<const LineItem*>(item))
    {
        path.moveTo(value->line().p1());
        path.lineTo(value->line().p2());
    } else if (const auto* value = dynamic_cast<const ArrowItem*>(item))
    {
        return value->shape();
    } else
    {
        path = Interior(item);
    }

    // 2. 使用线帽和连接生成真实描边，封闭形状不包含其填充内部。
    const QPen pen = ItemPen(item);
    QPainterPathStroker stroker;
    stroker.setWidth(qMax(1.0, pen.widthF()));
    stroker.setCapStyle(pen.capStyle());
    stroker.setJoinStyle(pen.joinStyle());
    stroker.setMiterLimit(pen.miterLimit());
    return stroker.createStroke(path);
}

CanvasItemEditState CanvasItemEdit::Capture(const CanvasItem* item)
{
    // 1. 未知对象返回空状态，场景辅助覆盖层不会进入同步。
    CanvasItemEditState state;
    if (!item || item->parentItem())
    {
        return state;
    }
    state._item_id = item->itemId();
    state._kind = Kind(item);
    state._position = item->pos();
    state._origin = item->transformOriginPoint();
    state._rotation = item->rotation();
    state._scale = item->scale();
    state._transform = item->transform();

    // 2. 几何保持类型原生表示，路径只记录相对于原始点的映射。
    if (const auto* value = dynamic_cast<const PathItem*>(item))
    {
        const QTransform transform = value->PathTransform();
        state._geometry = {transform.m11(), transform.m12(), transform.m21(), transform.m22(), transform.dx(), transform.dy()};
    } else if (state._kind == 4 || state._kind == 6)
    {
        const QLineF line = state._kind == 4 ? static_cast<const LineItem*>(item)->line() : static_cast<const ArrowItem*>(item)->Line();
        state._geometry = {line.x1(), line.y1(), line.x2(), line.y2()};
    } else if (state._kind == 8)
    {
        const QSizeF size = static_cast<const ImageItem*>(item)->displaySize();
        state._geometry = {size.width(), size.height()};
    } else if (state._kind == 9)
    {
        const auto* text_item = static_cast<const TextItem*>(item);
        const QSizeF size = text_item->LayoutSize();
        state._geometry = {size.width(), size.height()};
        state._text = text_item->GetTextData()._text;
        state._text_style = text_item->GetTextData()._style;
    } else
    {
        const QRectF rect = Bounds(item);
        state._geometry = {rect.x(), rect.y(), rect.width(), rect.height()};
    }
    if (item->BoundTextItem())
    {
        state._text = item->BoundTextItem()->GetTextData()._text;
        state._text_style = item->BoundTextItem()->GetTextData()._style;
    }
    return state;
}

bool CanvasItemEdit::IsValid(const CanvasItemEditState& state)
{
    // 1. 有限且可逆的仿射矩阵才能用于鼠标坐标映射。
    if (state._item_id.isEmpty() || state._kind < 1 || state._kind > 9 ||
        !state._transform.isAffine() || !state._transform.isInvertible() || state._scale <= 0.0)
    {
        return false;
    }
    const QVector<double> values{state._position.x(), state._position.y(), state._origin.x(), state._origin.y(),
                                 state._rotation, state._scale, state._transform.m11(), state._transform.m12(),
                                 state._transform.m21(), state._transform.m22(), state._transform.dx(), state._transform.dy()};
    for (double value : values)
    {
        if (!std::isfinite(value))
        {
            return false;
        }
    }
    for (double value : state._geometry)
    {
        if (!std::isfinite(value))
        {
            return false;
        }
    }
    const int size = state._kind == 1 || state._kind == 5 ? 6 : (state._kind >= 8 ? 2 : 4);
    if (state._geometry.size() != size)
    {
        return false;
    }
    if ((state._kind == 2 || state._kind == 3 || state._kind == 7) &&
        (state._geometry[2] < 0.0 || state._geometry[3] < 0.0))
    {
        return false;
    }
    if (state._kind >= 8 && (state._geometry[0] < 0.0 || state._geometry[1] < 0.0))
    {
        return false;
    }

    // 2. 复用文字限额；封闭图形允许清空附属内容。
    return TextItem::IsValidData({state._item_id, state._text, state._text_style, state._position}, true);
}

bool CanvasItemEdit::Apply(CanvasItem* item, const CanvasItemEditState& state)
{
    // 1. 完整校验后才更新，阻止远端回放再次产生旧图片或文字几何发送。
    if (!item || item->itemId() != state._item_id || Kind(item) != state._kind || !IsValid(state))
    {
        return false;
    }
    const QSignalBlocker blocker(item);
    item->SetBoundTextLayoutEnabled(false);
    const auto& geometry = state._geometry;
    if (auto* value = dynamic_cast<PathItem*>(item))
    {
        value->SetPathTransform(QTransform(geometry[0], geometry[1], geometry[2], geometry[3], geometry[4], geometry[5]));
    } else if (auto* value = dynamic_cast<LineItem*>(item))
    {
        value->setLine(QLineF(geometry[0], geometry[1], geometry[2], geometry[3]));
    } else if (auto* value = dynamic_cast<ArrowItem*>(item))
    {
        value->SetLine(QLineF(geometry[0], geometry[1], geometry[2], geometry[3]));
    } else if (auto* value = dynamic_cast<ImageItem*>(item))
    {
        value->setDisplaySize(QSizeF(geometry[0], geometry[1]));
    } else if (auto* value = dynamic_cast<TextItem*>(item))
    {
        value->SetTextData({state._item_id, state._text, state._text_style, state._position});
        value->SetLayoutSize(QSizeF(geometry[0], geometry[1]));
    } else
    {
        SetRectGeometry(item, QRectF(geometry[0], geometry[1], geometry[2], geometry[3]));
    }
    if (IsClosed(item) && (!state._text.isEmpty() || item->BoundTextItem()))
    {
        item->SetBoundTextData(state._text, state._text_style);
    }

    // 2. 几何和文字就绪后恢复公共变换，位置不能取旋转后的外接矩形。
    item->setTransform(state._transform);
    item->setScale(state._scale);
    item->setTransformOriginPoint(state._origin);
    item->setRotation(state._rotation);
    item->setPos(state._position);
    item->SetBoundTextLayoutEnabled(true);
    item->UpdateBoundTextLayout();
    item->update();
    return true;
}

void CanvasItemEdit::Resize(CanvasItem* item, const CanvasItemEditState& before, const QRectF& target)
{
    // 1. 每帧以手势起点重建几何，避免连续缩放积累精度损失。
    if (!item || !Apply(item, before))
    {
        return;
    }
    const QSignalBlocker blocker(item);
    const QRectF source = Bounds(item);
    const QTransform mapping = RectMapping(source, target);
    if (auto* value = dynamic_cast<PathItem*>(item))
    {
        value->SetPathTransform(value->PathTransform() * mapping);
    } else if (auto* value = dynamic_cast<LineItem*>(item))
    {
        value->setLine(mapping.map(value->line()));
    } else if (auto* value = dynamic_cast<ArrowItem*>(item))
    {
        value->SetLine(mapping.map(value->Line()));
    } else if (auto* value = dynamic_cast<TextItem*>(item))
    {
        value->SetLayoutSize(target.size());
    } else if (IsClosed(item))
    {
        SetRectGeometry(item, target);
    }
    item->UpdateBoundTextLayout();
}

void CanvasItemEdit::LayoutBoundText(CanvasItem* item)
{
    // 1. 椭圆和菱形采用安全内接矩形，文字不会伸出其斜边或曲线。
    TextItem* text_item = item ? item->BoundTextItem() : nullptr;
    if (!text_item || !IsClosed(item))
    {
        return;
    }
    QRectF rect = Bounds(item);
    const qreal margin = 8.0 + ItemPen(item).widthF() * 0.5;
    const qreal factor = Kind(item) == 3 ? std::sqrt(2.0) : (Kind(item) == 7 ? 2.0 : 1.0);
    const qreal text_width = qMax(12.0, (rect.width() - 2.0 * margin) / factor);
    text_item->SetLayoutSize(QSizeF(text_width, 0.0));

    // 2. 内容和横向重排只增高，保持局部上边中点的场景位置不跳动。
    const qreal needed_height = text_item->ContentHeight() * factor + 2.0 * margin;
    if (!text_item->GetTextData()._text.isEmpty() && needed_height > rect.height())
    {
        const QPointF anchor(rect.center().x(), rect.top());
        const QPointF scene_anchor = item->mapToScene(anchor);
        rect.setHeight(needed_height);
        SetRectGeometry(item, rect);
        item->setTransformOriginPoint(rect.center());
        item->setPos(item->pos() + scene_anchor - item->mapToScene(anchor));
    }

    // 3. 子编辑器始终使用父图元局部坐标，移动和旋转由 Qt 父子变换共同完成。
    text_item->setPos(rect.center() - QPointF(text_width / 2.0, text_item->ContentHeight() / 2.0));
    text_item->setFlag(QGraphicsItem::ItemIsSelectable, false);
    text_item->setFlag(QGraphicsItem::ItemIsMovable, false);
}
