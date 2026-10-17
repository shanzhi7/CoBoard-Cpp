#include "canvastoolregistry.h"

#include "global.h"

const std::array<CanvasToolDescriptor, 10>& CanvasToolRegistry::Descriptors()
{
    // 1. 工具按钮、提示和快捷键共用此表，避免界面与实际按键映射出现差异。
    static const std::array<CanvasToolDescriptor, 10> descriptors = {{
        {CanvasToolIds::SELECT, Qt::Key_V, QStringLiteral("select_tool"), QStringLiteral(":/res/select_cursor.svg"), QStringLiteral("鼠标工具：选择、移动和变换图元")},
        {CanvasToolIds::HAND, Qt::Key_H, QStringLiteral("hand_tool"), QStringLiteral(":/res/hand_pan.svg"), QStringLiteral("手形工具：拖动画布视口")},
        {Shape_Rect, Qt::Key_R, QStringLiteral("rect_tool"), QStringLiteral(":/res/tool_rect.svg"), QStringLiteral("矩形工具：拖拽绘制矩形")},
        {Shape_Diamond, Qt::Key_D, QStringLiteral("diamond_tool"), QStringLiteral(":/res/shape_diamond.svg"), QStringLiteral("菱形工具：拖拽绘制菱形")},
        {Shape_Oval, Qt::Key_O, QStringLiteral("oval_tool"), QStringLiteral(":/res/tool_oval.svg"), QStringLiteral("椭圆工具：拖拽绘制椭圆")},
        {Shape_Arrow, Qt::Key_A, QStringLiteral("arrow_tool"), QStringLiteral(":/res/shape_arrow.svg"), QStringLiteral("箭头工具：从按下点指向释放点")},
        {Shape_Line, Qt::Key_L, QStringLiteral("line_tool"), QStringLiteral(":/res/tool_line.svg"), QStringLiteral("直线工具：拖拽绘制直线")},
        {Shape_Pen, Qt::Key_P, QStringLiteral("pen_tool"), QStringLiteral(":/res/tool_pen.svg"), QStringLiteral("画笔工具：拖拽绘制自由笔迹")},
        {CanvasToolIds::TEXT, Qt::Key_T, QStringLiteral("text_tool"), QStringLiteral(":/res/tool_text.svg"), QStringLiteral("文字工具：点击创建或编辑文字")},
        {Shape_Eraser, Qt::Key_E, QStringLiteral("eraser_tool"), QStringLiteral(":/res/tool_eraser.svg"), QStringLiteral("橡皮擦工具：拖拽覆盖笔迹")}
    }};
    return descriptors;
}

const CanvasToolDescriptor* CanvasToolRegistry::FindByKey(int key)
{
    // 1. 返回表内稳定地址，不为每次键盘事件构造按钮或额外状态。
    for (const CanvasToolDescriptor& descriptor : Descriptors())
    {
        if (descriptor._shortcut_key == key)
        {
            return &descriptor;
        }
    }
    return nullptr;
}

const CanvasToolDescriptor* CanvasToolRegistry::FindById(int tool_id)
{
    // 1. 按同一份 ID 描述校验界面入口，拒绝误传其他按钮的 ID。
    for (const CanvasToolDescriptor& descriptor : Descriptors())
    {
        if (descriptor._tool_id == tool_id)
        {
            return &descriptor;
        }
    }
    return nullptr;
}
