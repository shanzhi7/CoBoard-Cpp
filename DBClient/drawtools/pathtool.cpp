#include "pathtool.h"

#include "../canvasitems/pathitem.h"

#include <QGraphicsScene>
#include <QPen>

namespace
{
constexpr qreal MIN_DIST_SQ = 4.0; // 原始采样点间距的平方，保留现有绘制与网络负载限制。
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
    // 1. 起笔保留原始坐标，后续平滑段以相邻采样点的中点连接。
    _start_pos = start_pos;
    _last_pos = start_pos;
    _current_pos = start_pos;
    _is_finished = false;
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
    // 1. 缺少图元或已经收笔时不能继续修改路径。
    if (!_item || _is_finished)
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

    // 3. 只追加一个新曲线段，临时尾段跟随当前采样坐标，不重新拟合已有笔迹。
    appendPoint(current_pos);
    UpdateItemPath();
    return true;
}

void PathTool::endLocal(const QPointF& end_pos)
{
    // 1. 收笔必须保留最后坐标，即使它距离上一采样点很近也不能丢失。
    if (!_item || _is_finished)
    {
        return;
    }
    appendPoint(end_pos);

    // 2. 最后半段曲线延伸至松手位置并固化，后续缩放和保存使用完整笔迹。
    _is_finished = true;
    UpdateItemPath();
}

void PathTool::beginRemote(QGraphicsScene* scene,
                           const message::DrawReq& request)
{
    // 1. 远端和历史回放从同一起点初始化平滑状态，不受网络分包边界影响。
    _start_pos = QPointF(request.start_x(), request.start_y());
    _last_pos = _start_pos;
    _current_pos = _start_pos;
    _is_finished = false;
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
    // 1. 没有 START 或已经完成的笔画不再接收增量点，避免重复收笔覆盖后续变换。
    if (!_item || _is_finished)
    {
        return;
    }

    // 2. 每个原始点都推进控制点，批量包与逐点到达使用同一平滑顺序。
    if (request.path_points_size() > 0)
    {
        for (int index = 0; index < request.path_points_size(); ++index)
        {
            const auto& point = request.path_points(index);
            appendPoint(QPointF(point.x(), point.y()));
        }
    } else
    {
        appendPoint(QPointF(request.current_x(), request.current_y()));
    }

    // 3. 每包只刷新一次图元，END 与本地收笔共同固化准确终点。
    _is_finished = request.cmd() == message::CMD_END;
    UpdateItemPath();
}

void PathTool::appendPoint(const QPointF& point)
{
    // 1. END 前的 MOVE 可能已携带相同终点，忽略重复点以保持本地与回放曲线一致。
    if (point == _last_pos)
    {
        return;
    }

    // 2. 画笔以旧采样点为控制点、新旧点中点为终点；橡皮擦保持直线覆盖规则。
    if (_shape_type == Shape_Pen)
    {
        const QPointF midpoint = (_last_pos + point) / 2.0;
        _path.quadTo(_last_pos, midpoint);
    } else
    {
        _path.lineTo(point);
    }

    // 3. 保存真实采样位置，距离过滤和网络坐标不能使用平滑后的中点。
    _last_pos = point;
    _current_pos = point;
}

void PathTool::UpdateItemPath()
{
    // 1. 显示路径可临时延伸到最新坐标，已确定曲线始终保留到前一个中点。
    if (!_item)
    {
        return;
    }
    QPainterPath display_path = _path;
    if (_shape_type == Shape_Pen && display_path.currentPosition() != _last_pos)
    {
        display_path.quadTo(_last_pos, _last_pos);
    }

    // 2. 仅收笔时固化尾段，下一采样点到来时不会重复追加上一帧的临时曲线。
    if (_is_finished)
    {
        _path = display_path;
    }
    _item->setPath(display_path);
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
