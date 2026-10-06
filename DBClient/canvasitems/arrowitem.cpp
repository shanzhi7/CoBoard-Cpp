#include "arrowitem.h"

#include <QPainter>
#include <QPainterPathStroker>
#include <QtGlobal>

ArrowItem::ArrowItem(const QString& item_id, QGraphicsItem* parent)
    : CanvasItem(item_id, parent)
{
    // 1. 箭头由绘图工具处理鼠标操作，沿用其他几何图元的交互限制。
    setInteractionEnabled(false);
    setAcceptedMouseButtons(Qt::NoButton);
}

ArrowItem::~ArrowItem()
{
    // 1. 线段、路径和画笔均为值类型，随图元生命周期自动释放。
}

QRectF ArrowItem::boundingRect() const
{
    // 1. 使用实际笔迹轮廓，斜向箭头和粗线帽也不会被场景裁切。
    return _stroke_path.boundingRect();
}

QPainterPath ArrowItem::shape() const
{
    // 1. 返回笔迹副本，保留将来支持精确命中的接口。
    return _stroke_path;
}

void ArrowItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    // 1. 绘制阶段只读取缓存几何，空路径不产生无方向的箭头标记。
    if (!painter || _path.isEmpty())
    {
        return;
    }

    // 2. 线身和箭头共用画笔，保持颜色、虚线和线宽一致。
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(_pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(_path);
    painter->restore();
}

void ArrowItem::SetLine(const QLineF& line)
{
    // 1. 端点变化会改变箭头方向及包围盒，更新前先通知场景索引。
    prepareGeometryChange();
    _line = line;

    // 2. 本地预览和远端回放共用相同几何算法。
    RebuildGeometry();
    update();
}

QLineF ArrowItem::Line() const
{
    // 1. 返回线段副本，避免绕过几何变更通知修改端点。
    return _line;
}

void ArrowItem::SetPen(const QPen& pen)
{
    // 1. 线宽同时影响箭头大小和笔迹范围，必须刷新场景索引。
    prepareGeometryChange();
    _pen = pen;

    // 2. 样式变更后重新计算几何，粗线箭头仍保持可辨识的方向。
    RebuildGeometry();
    update();
}

QPen ArrowItem::Pen() const
{
    // 1. 返回画笔副本，防止调用方直接改变缓存几何所依赖的线宽。
    return _pen;
}

void ArrowItem::RebuildGeometry()
{
    // 1. 零长度没有方向，清空旧路径并避免单位向量除零。
    _path = QPainterPath();
    _stroke_path = QPainterPath();
    const qreal length = _line.length();
    if (qFuzzyIsNull(length))
    {
        return;
    }

    // 2. 用方向和法向量构造开放式箭头；短线的箭头不会越过线段中点。
    const QPointF direction = (_line.p2() - _line.p1()) / length;
    const QPointF normal(-direction.y(), direction.x());
    const qreal head_length = qMin(length * 0.5, qMax(12.0, _pen.widthF() * 4.0));
    const QPointF head_base = _line.p2() - direction * head_length;
    const QPointF half_spread = normal * (head_length * 0.5);
    _path.moveTo(_line.p1());
    _path.lineTo(_line.p2());
    _path.moveTo(head_base + half_spread);
    _path.lineTo(_line.p2());
    _path.lineTo(head_base - half_spread);

    // 3. 缓存包含线宽、线帽和连接的轮廓，供重绘范围和后续命中共用。
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
