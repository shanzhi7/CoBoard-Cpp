#include "rectitem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

RectItem::RectItem(const QString& item_id, QGraphicsItem* parent)
    : CanvasItem(item_id, parent)
{
    // 1. 几何图元本轮由 Tool 处理鼠标操作，关闭自身交互避免改变旧行为。
    setInteractionEnabled(false);
    setAcceptedMouseButtons(Qt::NoButton);
}

RectItem::~RectItem()
{
    // 1. 矩形数据是值类型，图元销毁时自动释放。
}

QRectF RectItem::boundingRect() const
{
    // 1. 画笔向矩形边界外扩展半个线宽，避免边框被场景裁掉。
    const qreal half_width = _pen.widthF() / 2.0;
    return _rect.adjusted(-half_width,
                          -half_width,
                          half_width,
                          half_width);
}

void RectItem::paint(QPainter* painter,
                     const QStyleOptionGraphicsItem* option,
                     QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    if (!painter)
    {
        return;
    }

    // 1. 保持旧 QGraphicsRectItem 的无填充边框绘制语义。
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(_pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(_rect);
    painter->restore();
}

void RectItem::setRect(const QRectF& rect)
{
    // 1. 矩形变化会影响场景索引，先通知 Qt 再保存新几何。
    prepareGeometryChange();
    _rect = rect;
    update();
}

QRectF RectItem::rect() const
{
    // 1. 返回值类型副本，避免调用方绕过图元的几何变更流程。
    return _rect;
}

void RectItem::setPen(const QPen& pen)
{
    // 1. 线宽改变会影响包围盒，因此需要触发几何变更通知。
    prepareGeometryChange();
    _pen = pen;
    update();
}

QPen RectItem::pen() const
{
    // 1. 返回画笔副本，保持图元内部样式封装。
    return _pen;
}
