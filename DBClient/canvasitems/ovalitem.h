/***********************************************************************************
* @file         ovalitem.h
* @brief        椭圆自定义画布图元
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

class OvalItem final : public CanvasItem
{
public:
    explicit OvalItem(const QString& item_id, QGraphicsItem* parent = nullptr); // 创建椭圆图元。
    ~OvalItem() override; // 释放椭圆图元。

    QRectF boundingRect() const override; // 返回包含画笔宽度的椭圆包围盒。
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override; // 绘制椭圆边框。

    void setRect(const QRectF& rect); // 设置椭圆外接矩形。
    QRectF rect() const; // 返回当前椭圆外接矩形。
    void setPen(const QPen& pen); // 设置椭圆边框画笔。
    QPen pen() const; // 返回当前椭圆边框画笔。

private:
    QRectF _rect; // 椭圆在图元局部坐标中的外接矩形。
    QPen _pen; // 椭圆边框画笔。
};
