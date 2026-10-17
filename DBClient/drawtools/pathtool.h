/***********************************************************************************
* @file         pathtool.h
* @brief        钢笔和橡皮擦路径绘制工具的公共实现接口
* @author       shanzhi
* @date         2026/10/06
* @history
***********************************************************************************/
#pragma once

#include "../drawtool.h"

#include <QPainterPath>

class PathItem;

class PathTool : public IDrawTool
{
public:
    explicit PathTool(ShapeType shape_type); // 创建指定路径类型的公共 Tool。
    ~PathTool() override; // 释放 Tool 状态，不负责删除场景图元。

    bool isPathBased() const override; // 返回路径工具标记。
    bool usesCursorOverlay() const override; // 返回是否需要橡皮擦范围光标。
    ShapeType shapeType() const override; // 返回当前路径工具类型。
    QGraphicsItem* item() const override; // 返回当前路径自定义图元。
    QPointF currentPosition() const override; // 返回最近一次处理的鼠标位置。
    bool isStarted() const override; // 返回是否已经创建路径图元。

    void beginLocal(QGraphicsScene* scene, const QString& item_id, const QPointF& start_pos, const DrawStyle& style) override; // 创建本地路径图元。
    bool moveLocal(const QPointF& current_pos) override; // 按距离阈值追加本地路径点。
    void endLocal(const QPointF& end_pos) override; // 补齐本地路径最后一点。
    void beginRemote(QGraphicsScene* scene, const message::DrawReq& request) override; // 根据远端 START 创建路径图元。
    void updateRemote(const message::DrawReq& request) override; // 应用远端 MOVE 或 END 路径点。

protected:
    virtual PathItem* createItem(const QString& item_id) const = 0; // 创建具体的 PenItem 或 EraserItem。

private:
    void appendPoint(const QPointF& point); // 按原始采样顺序追加画笔平滑段或橡皮擦直线段。
    void UpdateItemPath(); // 添加临时收笔段显示到最新坐标，结束时固化完整路径。
    void configurePen(const QColor& color, int width, Qt::PenStyle pen_style); // 设置路径画笔和线帽。

    ShapeType _shape_type = Shape_Unknown; // 当前路径工具对应的协议图元类型。
    PathItem* _item = nullptr; // 当前操作关联的自定义路径图元。
    QPainterPath _path; // 已确定的路径段，画笔绘制期间不包含末尾临时收笔段。
    QPointF _start_pos; // 当前路径起点。
    QPointF _last_pos; // 上一次提交给路径的点。
    QPointF _current_pos; // 当前鼠标或远端点位置。
    bool _is_finished = false; // 收笔后忽略重复 MOVE/END，避免平滑尾段重复写入。
};
