#include "linetool.h"

#include "../canvasitems/lineitem.h"

#include <QGraphicsScene>
#include <QPen>

LineTool::LineTool()
{
    // 1. 直线 Tool 的起点和终点由每次绘制操作初始化。
}

LineTool::~LineTool()
{
    // 1. 场景负责直线图元生命周期，Tool 只持有非拥有指针。
    _item = nullptr;
}

bool LineTool::isPathBased() const
{
    // 1. 直线只发送起点和当前终点，不使用路径点批处理。
    return false;
}

bool LineTool::usesCursorOverlay() const
{
    // 1. 直线不需要橡皮擦范围光标。
    return false;
}

ShapeType LineTool::shapeType() const
{
    // 1. 返回直线协议类型。
    return Shape_Line;
}

QGraphicsItem* LineTool::item() const
{
    // 1. 返回基类指针，兼容 PaintScene 的撤销管理。
    return _item;
}

QPointF LineTool::currentPosition() const
{
    // 1. 返回最后一次直线终点。
    return _current_pos;
}

bool LineTool::isStarted() const
{
    // 1. 直线图元创建成功后即可接收 MOVE 和 END。
    return _item != nullptr;
}

void LineTool::beginLocal(QGraphicsScene* scene,
                          const QString& item_id,
                          const QPointF& start_pos,
                          const DrawStyle& style)
{
    // 1. 保存起点并创建退化线段，按下鼠标后立即具备可绘制状态。
    _start_pos = start_pos;
    _current_pos = start_pos;
    _item = new LineItem(item_id);
    configurePen(style.color, style.width, style.pen_style);
    _item->setLine(QLineF(start_pos, start_pos));
    if (scene)
    {
        // 2. 将自定义图元交给场景管理，后续只更新终点。
        scene->addItem(_item);
    }
}

bool LineTool::moveLocal(const QPointF& current_pos)
{
    // 1. 直线每次移动都直接更新终点，保持原有实时预览。
    if (!_item)
    {
        return false;
    }

    // 2. 更新终点并刷新自定义图元。
    _current_pos = current_pos;
    updateLine(current_pos);
    return true;
}

void LineTool::endLocal(const QPointF& end_pos)
{
    // 1. 最终释放位置覆盖预览位置，确保撤销记录保存完整结果。
    if (!_item)
    {
        return;
    }

    _current_pos = end_pos;
    updateLine(end_pos);
}

void LineTool::beginRemote(QGraphicsScene* scene,
                           const message::DrawReq& request)
{
    // 1. 远端 START 使用协议起点、颜色和宽度创建直线图元。
    _start_pos = QPointF(request.start_x(), request.start_y());
    _current_pos = _start_pos;
    _item = new LineItem(QString::fromStdString(request.item_id()));
    configurePen(DrawToolUtils::colorFromArgbInt(request.color()),
                 request.width(),
                 static_cast<int>(Qt::SolidLine));
    _item->setLine(QLineF(_start_pos, _start_pos));
    if (scene)
    {
        // 2. 远端图元加入场景后等待 MOVE 和 END 更新。
        scene->addItem(_item);
    }
}

void LineTool::updateRemote(const message::DrawReq& request)
{
    // 1. 没有 START 对应图元时无法恢复直线，直接忽略异常包。
    if (!_item)
    {
        return;
    }

    // 2. MOVE 和 END 只更新直线终点，不重建图元。
    _current_pos = QPointF(request.current_x(), request.current_y());
    updateLine(_current_pos);
}

void LineTool::updateLine(const QPointF& end_pos)
{
    // 1. 保持起点固定，只更新当前操作的终点。
    _item->setLine(QLineF(_start_pos, end_pos));
}

void LineTool::configurePen(const QColor& color, int width, int pen_style)
{
    // 1. 保持旧 QGraphicsLineItem 使用的圆形线帽和连接。
    _item->setPen(QPen(color,
                       width,
                       DrawToolUtils::penStyleFromInt(pen_style),
                       Qt::RoundCap,
                       Qt::RoundJoin));
}
