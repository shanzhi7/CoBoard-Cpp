/***********************************************************************************
* @file         diamonditem.h
* @brief        菱形自定义画布图元及其几何和样式接口
* @author       shanzhi
* @date         2026/10/07
* @history
***********************************************************************************/
#pragma once

#include "canvasitem.h"

#include <QPainterPath>
#include <QPen>
#include <QRectF>

class QPainter;
class QStyleOptionGraphicsItem;
class QWidget;

class DiamondItem final : public CanvasItem
{
public:
    explicit DiamondItem(const QString& item_id, QGraphicsItem* parent = nullptr); // 创建由外接矩形四边中点组成的菱形。
    ~DiamondItem() override; // 释放菱形的值类型几何和样式。

    QRectF boundingRect() const override; // 返回包含线宽、线帽和连接的菱形包围盒。
    QPainterPath shape() const override; // 返回菱形边框笔迹，供后续精确命中使用。
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override; // 使用当前画笔绘制无填充菱形。

    void SetRect(const QRectF& rect); // 规范化外接矩形并更新四个顶点。
    QRectF Rect() const; // 返回菱形的规范化外接矩形。
    void SetPen(const QPen& pen); // 设置边框画笔并刷新笔迹范围。
    QPen Pen() const; // 返回当前边框画笔副本。

private:
    void RebuildGeometry(); // 重建四边路径和包含画笔宽度的边框轮廓。

    QRectF _rect; // 图元局部坐标中的规范化外接矩形。
    QPen _pen{Qt::black, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin}; // 菱形边框画笔，默认无填充。
    QPainterPath _path; // 按上、右、下、左顺序闭合的菱形路径。
    QPainterPath _stroke_path; // 包含线宽的边框轮廓，用于包围盒和命中。
};
