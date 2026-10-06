/***********************************************************************************
* @file         ovaltool.h
* @brief        椭圆自定义图元绘制工具
* @author       shanzhi
* @date         2026/10/06
* @history
***********************************************************************************/
#pragma once

#include "../drawtool.h"

class OvalItem;

class OvalTool final : public IDrawTool
{
public:
    OvalTool(); // 创建椭圆 Tool。
    ~OvalTool() override; // 释放椭圆 Tool。

    bool isPathBased() const override; // 返回椭圆不是路径型工具。
    bool usesCursorOverlay() const override; // 返回椭圆不需要光标叠加层。
    ShapeType shapeType() const override; // 返回 Shape_Oval。
    QGraphicsItem* item() const override; // 返回当前椭圆自定义图元。
    QPointF currentPosition() const override; // 返回最近一次处理的鼠标位置。
    bool isStarted() const override; // 返回是否已经创建椭圆图元。

    void beginLocal(QGraphicsScene* scene, const QString& item_id, const QPointF& start_pos, const DrawStyle& style) override; // 创建本地椭圆图元。
    bool moveLocal(const QPointF& current_pos) override; // 更新本地椭圆预览。
    void endLocal(const QPointF& end_pos) override; // 设置本地椭圆最终范围。
    void beginRemote(QGraphicsScene* scene, const message::DrawReq& request) override; // 根据远端 START 创建椭圆图元。
    void updateRemote(const message::DrawReq& request) override; // 应用远端 MOVE 或 END 几何。

private:
    void updateRect(const QPointF& end_pos); // 根据起点和终点更新规范化外接矩形。
    void configurePen(const QColor& color, int width, int pen_style); // 设置椭圆边框样式。

    OvalItem* _item = nullptr; // 当前操作关联的椭圆图元。
    QPointF _start_pos; // 当前椭圆起点。
    QPointF _current_pos; // 当前椭圆终点。
};
