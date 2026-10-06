#include "diamonditem.h"

#include <QPainter>
#include <QPainterPathStroker>
#include <QtGlobal>

DiamondItem::DiamondItem(const QString& item_id, QGraphicsItem* parent)
    : CanvasItem(item_id, parent)
{
    // 1. 菱形与现有几何图元一样由 Tool 绘制，暂不开放选择或移动。
    setInteractionEnabled(false);
    setAcceptedMouseButtons(Qt::NoButton);
}

DiamondItem::~DiamondItem()
{
    // 1. 矩形、路径和画笔是值类型，随图元销毁自动释放。
}

QRectF DiamondItem::boundingRect() const
{
    // 1. 实际笔迹轮廓覆盖粗边框和尖角，避免低缩放时发生裁切。
    return _stroke_path.boundingRect();
}

QPainterPath DiamondItem::shape() const
{
    // 1. 当前菱形无填充，后续命中只覆盖可见边框。
    return _stroke_path;
}

void DiamondItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    // 1. 绘制只读取缓存路径，未开始拖动的零尺寸菱形不产生额外标记。
    if (!painter || _path.isEmpty())
    {
        return;
    }

    // 2. 沿用现有矩形和椭圆的无填充语义，画笔状态不会污染其他图元。
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(_pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(_path);
    painter->restore();
}

void DiamondItem::SetRect(const QRectF& rect)
{
    // 1. 任意方向拖拽都转成规范化矩形，更新前通知场景索引。
    prepareGeometryChange();
    _rect = rect.normalized();

    // 2. 外接矩形变化后重建顶点及笔迹范围。
    RebuildGeometry();
    update();
}

QRectF DiamondItem::Rect() const
{
    // 1. 返回几何副本，避免绕过场景索引更新改变顶点。
    return _rect;
}

void DiamondItem::SetPen(const QPen& pen)
{
    // 1. 边框线宽和连接方式影响包围盒，修改前通知场景。
    prepareGeometryChange();
    _pen = pen;

    // 2. 重建笔迹轮廓，让显示和后续命中使用相同线宽。
    RebuildGeometry();
    update();
}

QPen DiamondItem::Pen() const
{
    // 1. 返回样式副本，不暴露图元内部可写状态。
    return _pen;
}

void DiamondItem::RebuildGeometry()
{
    // 1. 四个顶点取外接矩形四边中点，所有拖拽方向得到相同闭合顺序。
    const QPointF center = _rect.center();
    _path = QPainterPath();
    _path.moveTo(center.x(), _rect.top());
    _path.lineTo(_rect.right(), center.y());
    _path.lineTo(center.x(), _rect.bottom());
    _path.lineTo(_rect.left(), center.y());
    _path.closeSubpath();

    // 2. 缓存实际边框轮廓，NoPen 对应没有可见或可命中的笔迹。
    _stroke_path = QPainterPath();
    if (_pen.style() != Qt::NoPen)
    {
        QPainterPathStroker stroker;
        stroker.setWidth(qMax(1.0, _pen.widthF()));
        stroker.setCapStyle(_pen.capStyle());
        stroker.setJoinStyle(_pen.joinStyle());
        stroker.setMiterLimit(_pen.miterLimit());
        _stroke_path = stroker.createStroke(_path);
    }
}
