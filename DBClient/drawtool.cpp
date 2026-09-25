#include "drawtool.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QPainterPath>
#include <QPen>

#include <QtGlobal>

#include <cstdint>
#include <utility>

namespace
{
constexpr qreal MIN_DIST_SQ = 4.0;

// DrawReq 使用 0xAARRGGBB，转换集中在策略层以保持 Canvas 只负责传输。
QColor colorFromArgbInt(int32_t argb)
{
    const int alpha = (argb >> 24) & 0xFF;
    const int red = (argb >> 16) & 0xFF;
    const int green = (argb >> 8) & 0xFF;
    const int blue = argb & 0xFF;
    return QColor(red, green, blue, alpha);
}

// 防止协议中的非法 pen_style 直接构造出不可预期的 QPen。
Qt::PenStyle penStyleFromInt(int pen_style)
{
    if (pen_style < static_cast<int>(Qt::NoPen) ||
        pen_style > static_cast<int>(Qt::CustomDashLine))
    {
        return Qt::SolidLine;
    }

    return static_cast<Qt::PenStyle>(pen_style);
}

class PathTool : public IDrawTool
{
public:
    // 路径工具同时服务 Pen 和 Eraser，类型决定颜色和宽度规则。
    explicit PathTool(ShapeType shape_type)
        : _shape_type(shape_type)
    {
    }

    bool isPathBased() const override { return true; }
    bool usesCursorOverlay() const override { return _shape_type == Shape_Eraser; }

    ShapeType shapeType() const override
    {
        return _shape_type;
    }

    QGraphicsItem* item() const override
    {
        return _item;
    }

    QPointF currentPosition() const override
    {
        return _current_pos;
    }

    bool isStarted() const override
    {
        return _item != nullptr;
    }

    void beginLocal(QGraphicsScene* scene,
                    const QPointF& start_pos,
                    const DrawStyle& style) override
    {
        // 本地路径从起点 moveTo，后续 MOVE 只追加增量点。
        _start_pos = start_pos;
        _current_pos = start_pos;
        _last_pos = start_pos;
        _path = QPainterPath();
        _path.moveTo(start_pos);

        _item = new QGraphicsPathItem();
        const QColor color = _shape_type == Shape_Eraser ? Qt::white : style.color;
        const int width = _shape_type == Shape_Eraser ? style.width * 3 : style.width;
        _item->setPen(QPen(color,
                           width,
                           penStyleFromInt(style.pen_style),
                           Qt::RoundCap,
                           Qt::RoundJoin));
        _item->setPath(_path);
        scene->addItem(_item);
    }

    bool moveLocal(const QPointF& current_pos) override
    {
        // 高频鼠标事件在操作层做距离过滤，避免无意义的 UI 更新和网络信号。
        if (!_item)
        {
            return false;
        }

        const qreal dx = current_pos.x() - _last_pos.x();
        const qreal dy = current_pos.y() - _last_pos.y();
        if ((dx * dx + dy * dy) < MIN_DIST_SQ)
        {
            return false;
        }

        appendPoint(current_pos);
        _last_pos = current_pos;
        _current_pos = current_pos;
        return true;
    }

    void endLocal(const QPointF& end_pos) override
    {
        // END 必须补上最后坐标，即使它距离上一采样点很近也不能丢失。
        if (!_item)
        {
            return;
        }

        appendPoint(end_pos);
        _last_pos = end_pos;
        _current_pos = end_pos;
    }

    void beginRemote(QGraphicsScene* scene,
                     const message::DrawReq& request) override
    {
        // 远端 START 使用协议中的样式，不能复用本地当前画笔配置。
        _start_pos = QPointF(request.start_x(), request.start_y());
        _current_pos = _start_pos;
        _last_pos = _start_pos;
        _path = QPainterPath();
        _path.moveTo(_start_pos);

        _item = new QGraphicsPathItem();
        const QColor color = _shape_type == Shape_Eraser
                                  ? Qt::white
                                  : colorFromArgbInt(request.color());
        const int width = _shape_type == Shape_Eraser
                              ? request.width() * 3
                              : request.width();
        _item->setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        _item->setPath(_path);
        scene->addItem(_item);
    }

    void updateRemote(const message::DrawReq& request) override
    {
        // 优先消费 path_points；没有增量点时使用 current_x/current_y 兼容旧包。
        if (!_item)
        {
            return;
        }

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

private:
    void appendPoint(const QPointF& point)
    {
        // QPainterPath 是路径状态的唯一来源，图元只保存它的当前显示结果。
        _path.lineTo(point);
        if (_item)
        {
            _item->setPath(_path);
        }
    }

    ShapeType _shape_type = Shape_Unknown;
    QGraphicsPathItem* _item = nullptr;
    QPainterPath _path;
    QPointF _start_pos;
    QPointF _last_pos;
    QPointF _current_pos;
};

class RectTool : public IDrawTool
{
public:
    bool isPathBased() const override { return false; }
    bool usesCursorOverlay() const override { return false; }
    // 矩形只需保存起点和当前终点，绘制时统一 normalized 支持反向拖拽。
    ShapeType shapeType() const override
    {
        return Shape_Rect;
    }

    QGraphicsItem* item() const override
    {
        return _item;
    }

    QPointF currentPosition() const override
    {
        return _current_pos;
    }

    bool isStarted() const override
    {
        return _item != nullptr;
    }

    void beginLocal(QGraphicsScene* scene,
                    const QPointF& start_pos,
                    const DrawStyle& style) override
    {
        // 先创建零尺寸矩形，移动事件再逐步更新其包围框。
        _start_pos = start_pos;
        _current_pos = start_pos;
        _item = new QGraphicsRectItem();
        _item->setPen(QPen(style.color,
                           style.width,
                           penStyleFromInt(style.pen_style),
                           Qt::SquareCap,
                           Qt::MiterJoin));
        _item->setRect(QRectF(start_pos, start_pos));
        scene->addItem(_item);
    }

    bool moveLocal(const QPointF& current_pos) override
    {
        // 几何图元每次移动都需要更新预览，因此不做路径距离过滤。
        if (!_item)
        {
            return false;
        }

        _current_pos = current_pos;
        _item->setRect(QRectF(_start_pos, current_pos).normalized());
        return true;
    }

    void endLocal(const QPointF& end_pos) override
    {
        // 释放鼠标时再次设置最终矩形，确保没有遗漏最后一个位置。
        if (!_item)
        {
            return;
        }

        _current_pos = end_pos;
        _item->setRect(QRectF(_start_pos, end_pos).normalized());
    }

    void beginRemote(QGraphicsScene* scene,
                     const message::DrawReq& request) override
    {
        // 远端几何图元必须使用发送者的颜色和宽度，保证多人视图一致。
        _start_pos = QPointF(request.start_x(), request.start_y());
        _current_pos = _start_pos;
        _item = new QGraphicsRectItem();
        _item->setPen(QPen(colorFromArgbInt(request.color()),
                           request.width(),
                           Qt::SolidLine,
                           Qt::SquareCap,
                           Qt::MiterJoin));
        _item->setRect(QRectF(_start_pos, _start_pos));
        scene->addItem(_item);
    }

    void updateRemote(const message::DrawReq& request) override
    {
        // 远端 MOVE 和 END 共用同一套矩形更新逻辑。
        if (!_item)
        {
            return;
        }

        _current_pos = QPointF(request.current_x(), request.current_y());
        _item->setRect(QRectF(_start_pos, _current_pos).normalized());
    }

private:
    QGraphicsRectItem* _item = nullptr;
    QPointF _start_pos;
    QPointF _current_pos;
};

class OvalTool : public IDrawTool
{
public:
    bool isPathBased() const override { return false; }
    bool usesCursorOverlay() const override { return false; }
    // 椭圆和矩形共享起点/终点模型，但使用独立图元类型保持扩展边界清晰。
    ShapeType shapeType() const override
    {
        return Shape_Oval;
    }

    QGraphicsItem* item() const override
    {
        return _item;
    }

    QPointF currentPosition() const override
    {
        return _current_pos;
    }

    bool isStarted() const override
    {
        return _item != nullptr;
    }

    void beginLocal(QGraphicsScene* scene,
                    const QPointF& start_pos,
                    const DrawStyle& style) override
    {
        // 初始椭圆为零尺寸，避免按下时出现未定义的预览区域。
        _start_pos = start_pos;
        _current_pos = start_pos;
        _item = new QGraphicsEllipseItem();
        _item->setPen(QPen(style.color,
                           style.width,
                           penStyleFromInt(style.pen_style),
                           Qt::RoundCap,
                           Qt::RoundJoin));
        _item->setRect(QRectF(start_pos, start_pos));
        scene->addItem(_item);
    }

    bool moveLocal(const QPointF& current_pos) override
    {
        // 椭圆预览跟随当前鼠标位置，并保留反向拖拽能力。
        if (!_item)
        {
            return false;
        }

        _current_pos = current_pos;
        _item->setRect(QRectF(_start_pos, current_pos).normalized());
        return true;
    }

    void endLocal(const QPointF& end_pos) override
    {
        // 结束时覆盖最后一次几何位置，确保本地和远端终点一致。
        if (!_item)
        {
            return;
        }

        _current_pos = end_pos;
        _item->setRect(QRectF(_start_pos, end_pos).normalized());
    }

    void beginRemote(QGraphicsScene* scene,
                     const message::DrawReq& request) override
    {
        // 远端 START 创建与本地同形状的椭圆，样式来自 DrawReq。
        _start_pos = QPointF(request.start_x(), request.start_y());
        _current_pos = _start_pos;
        _item = new QGraphicsEllipseItem();
        _item->setPen(QPen(colorFromArgbInt(request.color()),
                           request.width(),
                           Qt::SolidLine,
                           Qt::RoundCap,
                           Qt::RoundJoin));
        _item->setRect(QRectF(_start_pos, _start_pos));
        scene->addItem(_item);
    }

    void updateRemote(const message::DrawReq& request) override
    {
        // MOVE/END 只修改几何包围框，不重建图元，避免闪烁。
        if (!_item)
        {
            return;
        }

        _current_pos = QPointF(request.current_x(), request.current_y());
        _item->setRect(QRectF(_start_pos, _current_pos).normalized());
    }

private:
    QGraphicsEllipseItem* _item = nullptr;
    QPointF _start_pos;
    QPointF _current_pos;
};

class LineTool : public IDrawTool
{
public:
    bool isPathBased() const override { return false; }
    bool usesCursorOverlay() const override { return false; }
    // 直线操作保存固定起点和动态终点，适合本地预览及远端重放。
    ShapeType shapeType() const override
    {
        return Shape_Line;
    }

    QGraphicsItem* item() const override
    {
        return _item;
    }

    QPointF currentPosition() const override
    {
        return _current_pos;
    }

    bool isStarted() const override
    {
        return _item != nullptr;
    }

    void beginLocal(QGraphicsScene* scene,
                    const QPointF& start_pos,
                    const DrawStyle& style) override
    {
        // 初始线段退化为起点到起点，保证按下后立即有可见图元。
        _start_pos = start_pos;
        _current_pos = start_pos;
        _item = new QGraphicsLineItem();
        _item->setPen(QPen(style.color,
                           style.width,
                           penStyleFromInt(style.pen_style),
                           Qt::RoundCap,
                           Qt::RoundJoin));
        _item->setLine(QLineF(start_pos, start_pos));
        scene->addItem(_item);
    }

    bool moveLocal(const QPointF& current_pos) override
    {
        // 直线每次移动都直接更新终点，网络层仍发送轻量坐标包。
        if (!_item)
        {
            return false;
        }

        _current_pos = current_pos;
        _item->setLine(QLineF(_start_pos, current_pos));
        return true;
    }

    void endLocal(const QPointF& end_pos) override
    {
        // 最终释放位置覆盖预览位置，确保撤销记录保存完整结果。
        if (!_item)
        {
            return;
        }

        _current_pos = end_pos;
        _item->setLine(QLineF(_start_pos, end_pos));
    }

    void beginRemote(QGraphicsScene* scene,
                     const message::DrawReq& request) override
    {
        // 远端直线使用协议起点创建，等待 MOVE/END 更新终点。
        _start_pos = QPointF(request.start_x(), request.start_y());
        _current_pos = _start_pos;
        _item = new QGraphicsLineItem();
        _item->setPen(QPen(colorFromArgbInt(request.color()),
                           request.width(),
                           Qt::SolidLine,
                           Qt::RoundCap,
                           Qt::RoundJoin));
        _item->setLine(QLineF(_start_pos, _start_pos));
        scene->addItem(_item);
    }

    void updateRemote(const message::DrawReq& request) override
    {
        // 远端几何更新只修改线段终点，不替换既有 QGraphicsItem。
        if (!_item)
        {
            return;
        }

        _current_pos = QPointF(request.current_x(), request.current_y());
        _item->setLine(QLineF(_start_pos, _current_pos));
    }

private:
    QGraphicsLineItem* _item = nullptr;
    QPointF _start_pos;
    QPointF _current_pos;
};

class PenTool final : public PathTool
{
public:
    PenTool() : PathTool(Shape_Pen) {}
};

class EraserTool final : public PathTool
{
public:
    EraserTool() : PathTool(Shape_Eraser) {}
};
}

std::shared_ptr<IDrawTool> DrawToolFactory::create(ShapeType type)
{
    switch (type)
    {
    case Shape_Pen:
        return std::make_shared<PenTool>();
    case Shape_Eraser:
        return std::make_shared<EraserTool>();
    case Shape_Rect:
        return std::make_shared<RectTool>();
    case Shape_Oval:
        return std::make_shared<OvalTool>();
    case Shape_Line:
        return std::make_shared<LineTool>();
    default:
        return nullptr;
    }
}

bool DrawToolFactory::isPathBased(ShapeType type)
{
    return type == Shape_Pen || type == Shape_Eraser;
}

bool DrawToolFactory::usesCursorOverlay(ShapeType type)
{
    return type == Shape_Eraser;
}
