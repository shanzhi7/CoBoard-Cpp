/***********************************************************************************
* @file         pathitem.h
* @brief        路径类画布图元的公共绘制和几何接口
* @author       shanzhi
* @date         2026/10/06
* @history
***********************************************************************************/
#pragma once

#include "canvasitem.h"

#include <QPainterPath>
#include <QPen>

class QPainter;
class QStyleOptionGraphicsItem;
class QWidget;

class PathItem : public CanvasItem
{
public:
    explicit PathItem(const QString& item_id, QGraphicsItem* parent = nullptr); // 创建不可直接交互的路径图元。
    ~PathItem() override; // 释放路径图元自身的绘制状态。

    QRectF boundingRect() const override; // 返回包含画笔宽度的路径包围盒。
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override; // 绘制路径内容。

    void setPath(const QPainterPath& path); // 替换完整路径并刷新场景索引。
    void appendLineTo(const QPointF& point); // 向路径末尾追加一条直线。
    QPainterPath path() const; // 返回当前路径副本。
    void setPen(const QPen& pen); // 设置路径画笔并更新包围盒。
    QPen pen() const; // 返回当前路径画笔副本。

private:
    QPainterPath _path; // 路径图元保存的全部绘制点。
    QPen _pen; // 路径绘制使用的画笔。
};
