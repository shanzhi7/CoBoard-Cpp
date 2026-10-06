/***********************************************************************************
* @file         linetool.h
* @brief        直线自定义图元绘制工具
* @author       shanzhi
* @date         2026/10/06
* @history
***********************************************************************************/
#pragma once

#include "../drawtool.h"

class LineItem;

class LineTool final : public IDrawTool
{
public:
    LineTool(); // 创建直线 Tool。
    ~LineTool() override; // 释放直线 Tool。

    bool isPathBased() const override; // 返回直线不是路径型工具。
    bool usesCursorOverlay() const override; // 返回直线不需要光标叠加层。
    ShapeType shapeType() const override; // 返回 Shape_Line。
    QGraphicsItem* item() const override; // 返回当前直线自定义图元。
    QPointF currentPosition() const override; // 返回最近一次处理的鼠标位置。
    bool isStarted() const override; // 返回是否已经创建直线图元。

    void beginLocal(QGraphicsScene* scene, const QString& item_id, const QPointF& start_pos, const DrawStyle& style) override; // 创建本地直线图元。
    bool moveLocal(const QPointF& current_pos) override; // 更新本地直线预览。
    void endLocal(const QPointF& end_pos) override; // 设置本地直线最终终点。
    void beginRemote(QGraphicsScene* scene, const message::DrawReq& request) override; // 根据远端 START 创建直线图元。
    void updateRemote(const message::DrawReq& request) override; // 应用远端 MOVE 或 END 几何。

private:
    void updateLine(const QPointF& end_pos); // 根据起点和终点更新直线。
    void configurePen(const QColor& color, int width, int pen_style); // 设置直线样式。

    LineItem* _item = nullptr; // 当前操作关联的直线图元。
    QPointF _start_pos; // 当前直线起点。
    QPointF _current_pos; // 当前直线终点。
};
