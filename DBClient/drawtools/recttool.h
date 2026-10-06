/***********************************************************************************
* @file         recttool.h
* @brief        矩形自定义图元绘制工具
* @author       shanzhi
* @date         2026/10/06
* @history
***********************************************************************************/
#pragma once

#include "../drawtool.h"

class RectItem;

class RectTool final : public IDrawTool
{
public:
    RectTool(); // 创建矩形 Tool。
    ~RectTool() override; // 释放矩形 Tool。

    bool isPathBased() const override; // 返回矩形不是路径型工具。
    bool usesCursorOverlay() const override; // 返回矩形不需要光标叠加层。
    ShapeType shapeType() const override; // 返回 Shape_Rect。
    QGraphicsItem* item() const override; // 返回当前矩形自定义图元。
    QPointF currentPosition() const override; // 返回最近一次处理的鼠标位置。
    bool isStarted() const override; // 返回是否已经创建矩形图元。

    void beginLocal(QGraphicsScene* scene, const QString& item_id, const QPointF& start_pos, const DrawStyle& style) override; // 创建本地矩形图元。
    bool moveLocal(const QPointF& current_pos) override; // 更新本地矩形预览。
    void endLocal(const QPointF& end_pos) override; // 设置本地矩形最终范围。
    void beginRemote(QGraphicsScene* scene, const message::DrawReq& request) override; // 根据远端 START 创建矩形图元。
    void updateRemote(const message::DrawReq& request) override; // 应用远端 MOVE 或 END 几何。

private:
    void updateRect(const QPointF& end_pos); // 根据起点和终点更新规范化矩形。
    void configurePen(const QColor& color, int width, int pen_style); // 设置矩形边框样式。

    RectItem* _item = nullptr; // 当前操作关联的矩形图元。
    QPointF _start_pos; // 当前矩形起点。
    QPointF _current_pos; // 当前矩形终点。
};
