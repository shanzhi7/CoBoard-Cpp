#include "recttool.h"

#include "../canvasitems/rectitem.h"

#include <QGraphicsScene>
#include <QPen>

RectTool::RectTool()
{
    // 1. 矩形 Tool 的起点和终点由每次 beginLocal 或 beginRemote 初始化。
}

RectTool::~RectTool()
{
    // 1. 场景负责矩形图元生命周期，Tool 只持有非拥有指针。
    _item = nullptr;
}

bool RectTool::isPathBased() const
{
    // 1. 矩形只发送起点和当前终点，不使用路径点批处理。
    return false;
}

bool RectTool::usesCursorOverlay() const
{
    // 1. 矩形不需要橡皮擦范围光标。
    return false;
}

ShapeType RectTool::shapeType() const
{
    // 1. 返回矩形协议类型。
    return Shape_Rect;
}

QGraphicsItem* RectTool::item() const
{
    // 1. 返回基类指针，兼容 PaintScene 的撤销管理。
    return _item;
}

QPointF RectTool::currentPosition() const
{
    // 1. 返回最后一次矩形终点。
    return _current_pos;
}

bool RectTool::isStarted() const
{
    // 1. 矩形图元创建成功后即可接收 MOVE 和 END。
    return _item != nullptr;
}

void RectTool::beginLocal(QGraphicsScene* scene,
                          const QString& item_id,
                          const QPointF& start_pos,
                          const DrawStyle& style)
{
    // 1. 保存起点并创建零尺寸矩形，按下鼠标后立即具备可绘制状态。
    _start_pos = start_pos;
    _current_pos = start_pos;
    _item = new RectItem(item_id);
    configurePen(style.color, style.width, style.pen_style);
    _item->setRect(QRectF(start_pos, start_pos));
    if (scene)
    {
        // 2. 将自定义图元交给场景管理，后续只更新其几何状态。
        scene->addItem(_item);
    }
}

bool RectTool::moveLocal(const QPointF& current_pos)
{
    // 1. 几何图元每次移动都需要更新预览，不使用路径距离过滤。
    if (!_item)
    {
        return false;
    }

    // 2. 更新终点并保留反向拖拽能力。
    _current_pos = current_pos;
    updateRect(current_pos);
    return true;
}

void RectTool::endLocal(const QPointF& end_pos)
{
    // 1. 释放鼠标时再次设置最终矩形，避免遗漏最后一次位置。
    if (!_item)
    {
        return;
    }

    _current_pos = end_pos;
    updateRect(end_pos);
}

void RectTool::beginRemote(QGraphicsScene* scene,
                           const message::DrawReq& request)
{
    // 1. 远端 START 使用协议起点、颜色和宽度创建矩形图元。
    _start_pos = QPointF(request.start_x(), request.start_y());
    _current_pos = _start_pos;
    _item = new RectItem(QString::fromStdString(request.item_id()));
    configurePen(DrawToolUtils::colorFromArgbInt(request.color()),
                 request.width(),
                 static_cast<int>(Qt::SolidLine));
    _item->setRect(QRectF(_start_pos, _start_pos));
    if (scene)
    {
        // 2. 远端图元加入场景后等待 MOVE 和 END 更新。
        scene->addItem(_item);
    }
}

void RectTool::updateRemote(const message::DrawReq& request)
{
    // 1. 没有 START 对应图元时无法恢复矩形，直接忽略异常包。
    if (!_item)
    {
        return;
    }

    // 2. MOVE 和 END 共用同一套规范化矩形更新逻辑。
    _current_pos = QPointF(request.current_x(), request.current_y());
    updateRect(_current_pos);
}

void RectTool::updateRect(const QPointF& end_pos)
{
    // 1. normalized() 保持原有任意方向拖拽行为。
    _item->setRect(QRectF(_start_pos, end_pos).normalized());
}

void RectTool::configurePen(const QColor& color, int width, int pen_style)
{
    // 1. 保持旧 QGraphicsRectItem 使用的方形线帽和斜接连接。
    _item->setPen(QPen(color,
                       width,
                       DrawToolUtils::penStyleFromInt(pen_style),
                       Qt::SquareCap,
                       Qt::MiterJoin));
}
