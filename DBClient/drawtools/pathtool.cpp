#include "pathtool.h"

#include "../canvasitems/pathitem.h"

#include <QGraphicsScene>
#include <QPen>

namespace
{
constexpr qreal MIN_DIST_SQ = 4.0;
}

PathTool::PathTool(ShapeType shape_type)
    : _shape_type(shape_type)
{
    // 1. 具体 Tool 通过 createItem() 决定最终图元类型。
}

PathTool::~PathTool()
{
    // 1. 场景负责图元生命周期，Tool 只清空自身的非拥有指针。
    _item = nullptr;
}

bool PathTool::isPathBased() const
{
    // 1. 钢笔和橡皮擦都使用增量路径点协议。
    return true;
}

bool PathTool::usesCursorOverlay() const
{
    // 1. 只有橡皮擦需要显示操作范围。
    return _shape_type == Shape_Eraser;
}

ShapeType PathTool::shapeType() const
{
    // 1. 返回创建 Tool 时固定的协议图元类型。
    return _shape_type;
}

QGraphicsItem* PathTool::item() const
{
    // 1. 返回基类指针，兼容 PaintScene 现有撤销和远端清理逻辑。
    return _item;
}

QPointF PathTool::currentPosition() const
{
    // 1. 返回最后一个已处理位置，供切换工具时补发 END 使用。
    return _current_pos;
}

bool PathTool::isStarted() const
{
    // 1. 图元创建成功即可视为当前操作已经开始。
    return _item != nullptr;
}

void PathTool::beginLocal(QGraphicsScene* scene,
                          const QString& item_id,
                          const QPointF& start_pos,
                          const DrawStyle& style)
{
    // 1. 保存起点和路径状态，后续 MOVE 只追加增量点。
    _start_pos = start_pos;
    _last_pos = start_pos;
    _current_pos = start_pos;
    _path = QPainterPath();
    _path.moveTo(start_pos);

    // 2. 创建带稳定 item_id 的自定义图元，并配置本地画笔。
    _item = createItem(item_id);
    if (!_item || !scene)
    {
        return;
    }
    const QColor color = _shape_type == Shape_Eraser ? Qt::white : style.color;
    const int width = _shape_type == Shape_Eraser ? style.width * 3 : style.width;
    configurePen(color, width, DrawToolUtils::penStyleFromInt(style.pen_style));
    _item->setPath(_path);
    scene->addItem(_item);
}

bool PathTool::moveLocal(const QPointF& current_pos)
{
    // 1. 没有图元时无法更新本地路径。
    if (!_item)
    {
        return false;
    }

    // 2. 过滤过近的鼠标点，保持原有网络和绘制负载控制。
    const qreal dx = current_pos.x() - _last_pos.x();
    const qreal dy = current_pos.y() - _last_pos.y();
    if ((dx * dx + dy * dy) < MIN_DIST_SQ)
    {
        return false;
    }

    // 3. 提交路径点并更新位置状态。
    appendPoint(current_pos);
    _last_pos = current_pos;
    _current_pos = current_pos;
    return true;
}

void PathTool::endLocal(const QPointF& end_pos)
{
    // 1. END 必须补上最后坐标，即使它距离上一采样点很近也不能丢失。
    if (!_item)
    {
        return;
    }

    appendPoint(end_pos);
    _last_pos = end_pos;
    _current_pos = end_pos;
}

void PathTool::beginRemote(QGraphicsScene* scene,
                           const message::DrawReq& request)
{
    // 1. 远端 START 使用协议中的起点和图元 ID，不能复用本地画笔配置。
    _start_pos = QPointF(request.start_x(), request.start_y());
    _last_pos = _start_pos;
    _current_pos = _start_pos;
    _path = QPainterPath();
    _path.moveTo(_start_pos);

    // 2. 根据 ShapeType 创建对应的自定义路径图元。
    _item = createItem(QString::fromStdString(request.item_id()));
    if (!_item || !scene)
    {
        return;
    }
    const QColor color = _shape_type == Shape_Eraser
                             ? Qt::white
                             : DrawToolUtils::colorFromArgbInt(request.color());
    const int width = _shape_type == Shape_Eraser ? request.width() * 3 : request.width();
    configurePen(color, width, Qt::SolidLine);
    _item->setPath(_path);
    scene->addItem(_item);
}

void PathTool::updateRemote(const message::DrawReq& request)
{
    // 1. 没有 START 对应图元时无法恢复完整路径，直接忽略异常包。
    if (!_item)
    {
        return;
    }

    // 2. 优先消费批量路径点，兼容没有 path_points 的旧 MOVE 包。
    if (request.path_points_size() > 0)
    {
        for (int index = 0; index < request.path_points_size(); ++index)
        {
            const auto& point = request.path_points(index);
            appendPoint(QPointF(point.x(), point.y()));
        }
        const auto& last_point = request.path_points(request.path_points_size() - 1);
        _current_pos = QPointF(last_point.x(), last_point.y());
        _last_pos = _current_pos;
    }
    else
    {
        const QPointF current_pos(request.current_x(), request.current_y());
        appendPoint(current_pos);
        _current_pos = current_pos;
        _last_pos = current_pos;
    }
}

void PathTool::appendPoint(const QPointF& point)
{
    // 1. QPainterPath 是操作状态的唯一来源，自定义图元只接收更新后的路径。
    _path.lineTo(point);
    if (_item)
    {
        _item->setPath(_path);
    }
}

void PathTool::configurePen(const QColor& color,
                            int width,
                            Qt::PenStyle pen_style)
{
    // 1. 保持旧路径图元的圆形线帽和连接样式。
    QPen pen(color,
             width,
             pen_style,
             Qt::RoundCap,
             Qt::RoundJoin);
    _item->setPen(pen);
}
