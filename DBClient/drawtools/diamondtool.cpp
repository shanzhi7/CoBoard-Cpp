#include "diamondtool.h"

#include <QGraphicsScene>
#include <QPen>

#include "../canvasitems/diamonditem.h"

DiamondTool::DiamondTool()
{
    // 1. 菱形 Tool 的起点和终点由每次 beginLocal 或 beginRemote 初始化。
}

DiamondTool::~DiamondTool()
{
    // 1. 场景负责菱形图元生命周期，Tool 只持有非拥有指针。
    _item = nullptr;
}

bool DiamondTool::isPathBased() const
{
    // 1. 菱形只发送起点和当前终点，不使用路径点批处理。
    return false;
}

bool DiamondTool::usesCursorOverlay() const
{
    // 1. 菱形不需要橡皮擦范围光标。
    return false;
}

ShapeType DiamondTool::shapeType() const
{
    // 1. 返回菱形协议类型。
    return Shape_Diamond;
}

QGraphicsItem* DiamondTool::item() const
{
    // 1. 返回基类指针，兼容 PaintScene 的撤销管理。
    return _item;
}

QPointF DiamondTool::currentPosition() const
{
    // 1. 返回最后一次菱形终点。
    return _current_pos;
}

bool DiamondTool::isStarted() const
{
    // 1. 菱形图元创建成功后即可接收 MOVE 和 END。
    return _item != nullptr;
}

void DiamondTool::beginLocal(QGraphicsScene* scene,
                          const QString& item_id,
                          const QPointF& start_pos,
                          const DrawStyle& style)
{
    // 1. 场景负责图元生命周期，缺失场景或重复开始时不创建对象。
    if (!scene || _item)
    {
        return;
    }

    // 2. 保存起点并创建零尺寸外接矩形，按下鼠标后立即具备可绘制状态。
    _start_pos = start_pos;
    _current_pos = start_pos;
    _item = new DiamondItem(item_id);
    ConfigurePen(style.color, style.width, style.pen_style);
    _item->SetRect(QRectF(start_pos, start_pos));

    // 3. 将自定义图元交给场景管理，后续只更新其几何状态。
    scene->addItem(_item);
}

bool DiamondTool::moveLocal(const QPointF& current_pos)
{
    // 1. 几何图元每次移动都需要更新预览，不使用路径距离过滤。
    if (!_item)
    {
        return false;
    }

    // 2. 更新终点并保留反向拖拽能力。
    _current_pos = current_pos;
    UpdateRect(current_pos);
    return true;
}

void DiamondTool::endLocal(const QPointF& end_pos)
{
    // 1. 释放鼠标时再次设置最终菱形，避免遗漏最后一次位置。
    if (!_item)
    {
        return;
    }

    _current_pos = end_pos;
    UpdateRect(end_pos);
}

void DiamondTool::beginRemote(QGraphicsScene* scene,
                           const message::DrawReq& request)
{
    // 1. 场景负责图元生命周期，缺失场景或重复开始时不创建对象。
    if (!scene || _item)
    {
        return;
    }

    // 2. 远端 START 使用协议起点、颜色和宽度创建菱形图元。
    _start_pos = QPointF(request.start_x(), request.start_y());
    _current_pos = _start_pos;
    _item = new DiamondItem(QString::fromStdString(request.item_id()));
    ConfigurePen(DrawToolUtils::colorFromArgbInt(request.color()),
                 request.width(),
                 static_cast<int>(Qt::SolidLine));
    _item->SetRect(QRectF(_start_pos, _start_pos));

    // 3. 远端图元加入场景后等待 MOVE 和 END 更新。
    scene->addItem(_item);
}

void DiamondTool::updateRemote(const message::DrawReq& request)
{
    // 1. 没有 START 对应图元时无法恢复菱形，直接忽略异常包。
    if (!_item)
    {
        return;
    }

    // 2. MOVE 和 END 共用同一套规范化外接矩形更新逻辑。
    _current_pos = QPointF(request.current_x(), request.current_y());
    UpdateRect(_current_pos);
}

void DiamondTool::UpdateRect(const QPointF& end_pos)
{
    // 1. 规范化外接矩形，让四个象限的拖拽使用相同顶点生成规则。
    _item->SetRect(QRectF(_start_pos, end_pos).normalized());
}

void DiamondTool::ConfigurePen(const QColor& color, int width, int pen_style)
{
    // 1. 菱形采用圆形线帽和连接，粗边框仍保持完整的四个顶点。
    _item->SetPen(QPen(color,
                       qMax(1, width),
                       DrawToolUtils::penStyleFromInt(pen_style),
                       Qt::RoundCap,
                       Qt::RoundJoin));
}
