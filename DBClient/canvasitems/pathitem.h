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
#include <QTransform>

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
    QPainterPath OriginalPath() const; // 返回缩放前不可变的完整路径，供状态和文件恢复。
    QTransform PathTransform() const; // 返回相对于原始路径的累计几何映射。
    void SetPathTransform(const QTransform& transform); // 从原始路径重新计算显示路径，避免累计误差。

private:
    QPainterPath _path; // 路径图元保存的全部绘制点。
    QPen _pen; // 路径绘制使用的画笔。
    QPainterPath _original_path; // 绘制结束时的原始路径，编辑缩放不覆盖它。
    QTransform _path_transform; // 原始路径到当前路径的绝对仿射映射。
};
