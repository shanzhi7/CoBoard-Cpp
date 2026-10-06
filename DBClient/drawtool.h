/***********************************************************************************
* @file         drawtool.h
* @brief        画布绘制工具接口、绘制样式和工具工厂
* @author       shanzhi
* @date         2026/10/06
* @history
***********************************************************************************/
#pragma once

#include <cstdint>
#include <memory>

#include <QColor>
#include <QPointF>
#include <Qt>

#include "global.h"
#include "message.pb.h"

class QGraphicsItem;
class QGraphicsScene;
class QString;

struct DrawStyle
{
    QColor color; // 绘制使用的颜色。
    int width = 1; // 画笔宽度，默认值为 1。
    int pen_style = 1; // 画笔样式，取值与 Qt 画笔样式约定一致。
};

// IDrawTool 同时描述工具能力和一笔绘制的图元状态。
class IDrawTool
{
public:
    virtual ~IDrawTool() = default; // 通过基类指针释放具体绘图工具。

    virtual ShapeType shapeType() const = 0; // 返回工具对应的图形类型。
    virtual bool isPathBased() const = 0; // 判断工具是否按连续路径绘制。
    virtual bool usesCursorOverlay() const = 0; // 判断工具是否需要显示光标叠加层。
    virtual QGraphicsItem* item() const = 0; // 返回当前工具关联的图元。
    virtual QPointF currentPosition() const = 0; // 返回当前绘制位置。
    virtual bool isStarted() const = 0; // 判断本次绘制是否已经开始。

    virtual void beginLocal(QGraphicsScene* scene, const QString& item_id, const QPointF& start_pos, const DrawStyle& style) = 0; // 在本地画布开始一笔绘制。
    virtual bool moveLocal(const QPointF& current_pos) = 0; // 更新本地绘制位置并返回是否产生变化。
    virtual void endLocal(const QPointF& end_pos) = 0; // 在本地画布结束一笔绘制。

    virtual void beginRemote(QGraphicsScene* scene, const message::DrawReq& request) = 0; // 根据远端请求创建绘制图元。
    virtual void updateRemote(const message::DrawReq& request) = 0; // 根据远端请求更新绘制图元。
};

namespace DrawToolUtils
{
QColor colorFromArgbInt(int32_t argb); // 将协议中的 0xAARRGGBB 整数转换为 Qt 颜色。
Qt::PenStyle penStyleFromInt(int pen_style); // 将协议画笔样式转换为受限的 Qt 画笔样式。
}

// 按图元类型创建独立工具，类型能力由同一套显式分支提供。
class DrawToolFactory
{
public:
    static std::shared_ptr<IDrawTool> create(ShapeType type); // 创建指定图形类型的绘图工具。
    static bool isPathBased(ShapeType type); // 判断指定图形类型是否按路径绘制。
    static bool usesCursorOverlay(ShapeType type); // 判断指定图形类型是否需要光标叠加层。
};
