#include "ovalitem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

OvalItem::OvalItem(const QString& item_id, QGraphicsItem* parent)
    : CanvasItem(item_id, parent)
{
    // 1. 椭圆预览由 OvalTool 更新，关闭图元自身交互以保持旧鼠标行为。
    setInteractionEnabled(false);
    setAcceptedMouseButtons(Qt::NoButton);
}

OvalItem::~OvalItem()
{
    // 1. 外接矩形和画笔是值类型，图元销毁时自动释放。
}

QRectF OvalItem::boundingRect() const
{
    // 1. 画笔向椭圆外侧扩展半个线宽，保证边框完整可见。
    const qreal half_width = _pen.widthF() / 2.0;
    return _rect.adjusted(-half_width,
                          -half_width,
                          half_width,
                          half_width);
}

void OvalItem::paint(QPainter* painter,
                     const QStyleOptionGraphicsItem* option,
                     QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    if (!painter)
    {
        return;
    }

    // 1. 保持旧 QGraphicsEllipseItem 的无填充椭圆边框绘制语义。
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(_pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(_rect);
    painter->restore();
}

void OvalItem::setRect(const QRectF& rect)
{
    // 1. 椭圆外接矩形变化会影响场景索引，先通知 Qt 再提交新值。
    prepareGeometryChange();
    _rect = rect;
    update();
}

QRectF OvalItem::rect() const
{
    // 1. 返回外接矩形副本，防止绕过 prepareGeometryChange 修改状态。
    return _rect;
}

void OvalItem::setPen(const QPen& pen)
{
    // 1. 线宽改变会影响椭圆包围盒，需要触发几何变更通知。
    prepareGeometryChange();
    _pen = pen;
    update();
}

QPen OvalItem::pen() const
{
    // 1. 返回画笔副本，保持图元内部样式封装。
    return _pen;
}
