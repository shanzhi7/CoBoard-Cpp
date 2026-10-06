/***********************************************************************************
* @file         lineitem.h
* @brief        直线自定义画布图元
* @author       shanzhi
* @date         2026/10/06
* @history
***********************************************************************************/
#pragma once

#include "canvasitem.h"

#include <QLineF>
#include <QPen>

class QPainter;
class QStyleOptionGraphicsItem;
class QWidget;

class LineItem final : public CanvasItem
{
public:
    explicit LineItem(const QString& item_id, QGraphicsItem* parent = nullptr); // 创建直线图元。
    ~LineItem() override; // 释放直线图元。

    QRectF boundingRect() const override; // 返回包含画笔宽度的直线包围盒。
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override; // 绘制直线。

    void setLine(const QLineF& line); // 设置直线起点和终点。
    QLineF line() const; // 返回当前直线。
    void setPen(const QPen& pen); // 设置直线画笔。
    QPen pen() const; // 返回当前直线画笔。

private:
    QLineF _line; // 直线在图元局部坐标中的起点和终点。
    QPen _pen; // 直线绘制使用的画笔。
};
