#include "lineitem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

LineItem::LineItem(const QString& item_id, QGraphicsItem* parent)
    : CanvasItem(item_id, parent)
{
    // 1. 直线预览由 LineTool 更新，关闭图元自身交互以保持旧鼠标行为。
    setInteractionEnabled(false);
    setAcceptedMouseButtons(Qt::NoButton);
}

LineItem::~LineItem()
{
    // 1. 直线和画笔是值类型，图元销毁时自动释放。
}

QRectF LineItem::boundingRect() const
{
    // 1. 画笔向线段两侧扩展半个线宽，保证端点和边框可见。
    const qreal half_width = _pen.widthF() / 2.0;
    const QRectF line_rect(_line.p1(), _line.p2());
    return line_rect.normalized().adjusted(-half_width,
                                           -half_width,
                                           half_width,
                                           half_width);
}

void LineItem::paint(QPainter* painter,
                     const QStyleOptionGraphicsItem* option,
                     QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    if (!painter)
    {
        return;
    }

    // 1. 保持旧 QGraphicsLineItem 的直线绘制语义并启用抗锯齿。
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(_pen);
    painter->drawLine(_line);
    painter->restore();
}

void LineItem::setLine(const QLineF& line)
{
    // 1. 线段变化会影响场景索引，先通知 Qt 再保存新几何。
    prepareGeometryChange();
    _line = line;
    update();
}

QLineF LineItem::line() const
{
    // 1. 返回线段副本，保持图元内部几何封装。
    return _line;
}

void LineItem::setPen(const QPen& pen)
{
    // 1. 线宽改变会影响线段包围盒，需要触发几何变更通知。
    prepareGeometryChange();
    _pen = pen;
    update();
}

QPen LineItem::pen() const
{
    // 1. 返回画笔副本，调用方不会直接修改图元状态。
    return _pen;
}
