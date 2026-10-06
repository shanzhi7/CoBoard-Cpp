#include "pathitem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

PathItem::PathItem(const QString& item_id, QGraphicsItem* parent)
    : CanvasItem(item_id, parent)
{
    // 1. 路径图元本轮只负责显示绘画结果，交互仍由 PaintScene 的 Tool 状态机处理。
    setInteractionEnabled(false);
    setAcceptedMouseButtons(Qt::NoButton);
}

PathItem::~PathItem()
{
    // 1. QPainterPath 和 QPen 是值类型，图元销毁时会自动释放内部资源。
}

QRectF PathItem::boundingRect() const
{
    // 1. 画笔会向路径两侧扩展半个线宽，包围盒必须覆盖完整笔迹。
    const qreal half_width = _pen.widthF() / 2.0;
    return _path.boundingRect().adjusted(-half_width,
                                         -half_width,
                                         half_width,
                                         half_width);
}

void PathItem::paint(QPainter* painter,
                     const QStyleOptionGraphicsItem* option,
                     QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    if (!painter)
    {
        return;
    }

    // 1. 自定义图元只读取已经提交的路径和画笔，不在绘制阶段修改状态。
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(_pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(_path);
    painter->restore();
}

void PathItem::setPath(const QPainterPath& path)
{
    // 1. 路径改变会影响场景索引，先通知 Qt 即将变化。
    prepareGeometryChange();
    _path = path;
    update();
}

void PathItem::appendLineTo(const QPointF& point)
{
    // 1. 追加点前通知 Qt，保证增量路径的新包围盒能够参与重绘。
    prepareGeometryChange();
    _path.lineTo(point);
    update();
}

QPainterPath PathItem::path() const
{
    // 1. QPainterPath 使用隐式共享，返回副本不会暴露内部可写引用。
    return _path;
}

void PathItem::setPen(const QPen& pen)
{
    // 1. 线宽改变会影响包围盒，因此和路径变化使用相同的几何变更通知。
    prepareGeometryChange();
    _pen = pen;
    update();
}

QPen PathItem::pen() const
{
    // 1. 返回画笔副本，调用方可以安全调整样式而不直接修改图元状态。
    return _pen;
}
