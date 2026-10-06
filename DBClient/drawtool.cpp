#include "drawtool.h"

#include "drawtools/erasertool.h"
#include "drawtools/linetool.h"
#include "drawtools/ovaltool.h"
#include "drawtools/pentool.h"
#include "drawtools/recttool.h"

#include <QtGlobal>

namespace
{
// DrawReq 使用 0xAARRGGBB，统一在工具层转换后交给自定义图元绘制。
QColor ColorFromArgbInt(int32_t argb)
{
    // 1. 按协议顺序拆分透明度和三个颜色通道。
    const int alpha = (argb >> 24) & 0xFF;
    const int red = (argb >> 16) & 0xFF;
    const int green = (argb >> 8) & 0xFF;
    const int blue = argb & 0xFF;
    return QColor(red, green, blue, alpha);
}

// 防止协议中的非法 pen_style 直接构造出不可预期的 QPen。
Qt::PenStyle PenStyleFromInt(int pen_style)
{
    // 1. 将超出 Qt 枚举范围的值降级为实线，保持远端绘制可用。
    if (pen_style < static_cast<int>(Qt::NoPen) ||
        pen_style > static_cast<int>(Qt::CustomDashLine))
    {
        return Qt::SolidLine;
    }

    return static_cast<Qt::PenStyle>(pen_style);
}
}

namespace DrawToolUtils
{
QColor colorFromArgbInt(int32_t argb)
{
    // 1. 通过统一转换函数保持所有远端 Tool 的颜色解析一致。
    return ColorFromArgbInt(argb);
}

Qt::PenStyle penStyleFromInt(int pen_style)
{
    // 1. 通过统一校验函数保持本地和远端画笔样式边界一致。
    return PenStyleFromInt(pen_style);
}
}

std::shared_ptr<IDrawTool> DrawToolFactory::create(ShapeType type)
{
    // 1. 工厂只负责按协议图元类型构造 Tool，不再保存具体 Tool 类定义。
    switch (type)
    {
    case Shape_Pen:
        return std::make_shared<PenTool>();
    case Shape_Eraser:
        return std::make_shared<EraserTool>();
    case Shape_Rect:
        return std::make_shared<RectTool>();
    case Shape_Oval:
        return std::make_shared<OvalTool>();
    case Shape_Line:
        return std::make_shared<LineTool>();
    default:
        return nullptr;
    }
}

bool DrawToolFactory::isPathBased(ShapeType type)
{
    // 1. 路径工具使用批量点缓存，Canvas 依赖该判断控制网络发送节奏。
    return type == Shape_Pen || type == Shape_Eraser;
}

bool DrawToolFactory::usesCursorOverlay(ShapeType type)
{
    // 1. 只有橡皮擦需要显示范围光标，范围光标本身不属于画布内容。
    return type == Shape_Eraser;
}
