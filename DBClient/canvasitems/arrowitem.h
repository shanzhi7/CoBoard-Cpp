/***********************************************************************************
* @file         arrowitem.h
* @brief        单向箭头自定义画布图元及其几何和样式接口
* @author       shanzhi
* @date         2026/10/07
* @history
***********************************************************************************/
#pragma once

#include "canvasitem.h"

#include <QLineF>
#include <QPainterPath>
#include <QPen>

class QPainter;
class QStyleOptionGraphicsItem;
class QWidget;

class ArrowItem final : public CanvasItem
{
public:
    explicit ArrowItem(const QString& item_id, QGraphicsItem* parent = nullptr); // 创建指向线段终点的单向箭头。
    ~ArrowItem() override; // 释放箭头的值类型几何和样式。

    QRectF boundingRect() const override; // 返回覆盖线身、箭头和画笔宽度的包围盒。
    QPainterPath shape() const override; // 返回完整笔迹轮廓，供场景命中和后续交互使用。
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override; // 按当前画笔绘制线身和开放式箭头。

    void SetLine(const QLineF& line); // 设置箭尾和箭头位置，并刷新场景索引。
    QLineF Line() const; // 返回当前箭尾到箭头的方向线段。
    void SetPen(const QPen& pen); // 设置画笔并按线宽调整箭头尺寸。
    QPen Pen() const; // 返回当前画笔副本。

private:
    void RebuildGeometry(); // 重建箭头路径和覆盖线帽、连接的笔迹轮廓。

    QLineF _line; // 图元局部坐标中的箭尾和箭头位置。
    QPen _pen{Qt::black, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin}; // 箭头画笔，默认使用圆形线帽和连接。
    QPainterPath _path; // 线身和两条箭头边的中心路径。
    QPainterPath _stroke_path; // 包含线宽的笔迹轮廓，用于包围盒和命中。
};
