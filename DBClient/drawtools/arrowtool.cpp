#include "arrowtool.h"

#include <QGraphicsScene>
#include <QPen>

#include "../canvasitems/arrowitem.h"

ArrowTool::ArrowTool()
{
    // 1. 箭头 Tool 的起点和终点由每次绘制操作初始化。
}

ArrowTool::~ArrowTool()
{
    // 1. 场景负责箭头图元生命周期，Tool 只持有非拥有指针。
    _item = nullptr;
}

bool ArrowTool::isPathBased() const
{
    // 1. 箭头只发送起点和当前终点，不使用路径点批处理。
    return false;
}

bool ArrowTool::usesCursorOverlay() const
{
    // 1. 箭头不需要橡皮擦范围光标。
    return false;
}

ShapeType ArrowTool::shapeType() const
{
    // 1. 返回箭头协议类型。
    return Shape_Arrow;
}

QGraphicsItem* ArrowTool::item() const
{
    // 1. 返回基类指针，兼容 PaintScene 的撤销管理。
    return _item;
}

QPointF ArrowTool::currentPosition() const
{
    // 1. 返回最后一次箭头终点。
    return _current_pos;
}

bool ArrowTool::isStarted() const
{
    // 1. 箭头图元创建成功后即可接收 MOVE 和 END。
    return _item != nullptr;
}

void ArrowTool::beginLocal(QGraphicsScene* scene,
                          const QString& item_id,
                          const QPointF& start_pos,
                          const DrawStyle& style)
{
    // 1. 场景负责图元生命周期，缺失场景或重复开始时不创建对象。
    if (!scene || _item)
    {
        return;
    }

    // 2. 保存起点并创建退化线段，按下鼠标后立即具备可绘制状态。
    _start_pos = start_pos;
    _current_pos = start_pos;
    _item = new ArrowItem(item_id);
    ConfigurePen(style.color, style.width, style.pen_style);
    _item->SetLine(QLineF(start_pos, start_pos));

    // 3. 将自定义图元交给场景管理，后续只更新终点。
    scene->addItem(_item);
}

bool ArrowTool::moveLocal(const QPointF& current_pos)
{
    // 1. 箭头每次移动都直接更新终点，保持原有实时预览。
    if (!_item)
    {
        return false;
    }

    // 2. 更新终点并刷新自定义图元。
    _current_pos = current_pos;
    UpdateLine(current_pos);
    return true;
}

void ArrowTool::endLocal(const QPointF& end_pos)
{
    // 1. 最终释放位置覆盖预览位置，确保撤销记录保存完整结果。
    if (!_item)
    {
        return;
    }

    _current_pos = end_pos;
    UpdateLine(end_pos);
}

void ArrowTool::beginRemote(QGraphicsScene* scene,
                           const message::DrawReq& request)
{
    // 1. 场景负责图元生命周期，缺失场景或重复开始时不创建对象。
    if (!scene || _item)
    {
        return;
    }

    // 2. 远端 START 使用协议起点、颜色和宽度创建箭头图元。
    _start_pos = QPointF(request.start_x(), request.start_y());
    _current_pos = _start_pos;
    _item = new ArrowItem(QString::fromStdString(request.item_id()));
    ConfigurePen(DrawToolUtils::colorFromArgbInt(request.color()),
                 request.width(),
                 static_cast<int>(Qt::SolidLine));
    _item->SetLine(QLineF(_start_pos, _start_pos));

    // 3. 远端图元加入场景后等待 MOVE 和 END 更新。
    scene->addItem(_item);
}

void ArrowTool::updateRemote(const message::DrawReq& request)
{
    // 1. 没有 START 对应图元时无法恢复箭头，直接忽略异常包。
    if (!_item)
    {
        return;
    }

    // 2. MOVE 和 END 只更新箭头终点，不重建图元。
    _current_pos = QPointF(request.current_x(), request.current_y());
    UpdateLine(_current_pos);
}

void ArrowTool::UpdateLine(const QPointF& end_pos)
{
    // 1. 保持起点固定，只更新当前操作的终点。
    _item->SetLine(QLineF(_start_pos, end_pos));
}

void ArrowTool::ConfigurePen(const QColor& color, int width, int pen_style)
{
    // 1. 圆形线帽和连接让线身及箭头使用同一套颜色、线宽和样式。
    _item->SetPen(QPen(color,
                       qMax(1, width),
                       DrawToolUtils::penStyleFromInt(pen_style),
                       Qt::RoundCap,
                       Qt::RoundJoin));
}
