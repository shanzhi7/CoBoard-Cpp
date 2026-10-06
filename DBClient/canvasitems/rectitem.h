/***********************************************************************************
* @file         rectitem.h
* @brief        矩形自定义画布图元
* @author       shanzhi
* @date         2026/10/06
* @history
***********************************************************************************/
#pragma once

#include "canvasitem.h"

#include <QPen>
#include <QRectF>

class QPainter;
class QStyleOptionGraphicsItem;
class QWidget;

class RectItem final : public CanvasItem
{
public:
    explicit RectItem(const QString& item_id, QGraphicsItem* parent = nullptr); // 创建矩形图元。
    ~RectItem() override; // 释放矩形图元。

    QRectF boundingRect() const override; // 返回包含画笔宽度的矩形包围盒。
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override; // 绘制矩形边框。

    void setRect(const QRectF& rect); // 设置矩形几何范围。
    QRectF rect() const; // 返回当前矩形几何范围。
    void setPen(const QPen& pen); // 设置矩形边框画笔。
    QPen pen() const; // 返回当前矩形边框画笔。

private:
    QRectF _rect; // 矩形在图元局部坐标中的几何范围。
    QPen _pen; // 矩形边框画笔。
};
