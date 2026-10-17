/***********************************************************************************
* @file         canvastoolregistry.h
* @brief        画布工具按钮、图标和字母快捷键的只读描述表
* @author       shanzhi
* @date         2026/10/15
* @history
***********************************************************************************/
#pragma once

#include <array>

#include <QString>
#include <Qt>

namespace CanvasToolIds
{
constexpr int SELECT = 100; // 本地鼠标工具 ID，不写入绘画协议。
constexpr int HAND = 101; // 本地手形工具 ID，只改变视口状态。
constexpr int TEXT = 102; // 独立文字工具 ID，使用文字操作协议。
}

struct CanvasToolDescriptor
{
    int _tool_id; // 按钮组 ID，绘图工具沿用已有 ShapeType。
    Qt::Key _shortcut_key; // 画布获得焦点时使用的单字母按键。
    QString _button_name; // UI 文件中对应工具按钮的 objectName。
    QString _icon_resource; // 工具的统一风格 SVG 资源路径。
    QString _description; // 用户可见的中文职责说明。
};

class CanvasToolRegistry
{
public:
    static const std::array<CanvasToolDescriptor, 10>& Descriptors(); // 返回生命周期覆盖应用的只读工具描述表。
    static const CanvasToolDescriptor* FindByKey(int key); // 查找按键对应工具，未注册按键返回空指针。
    static const CanvasToolDescriptor* FindById(int tool_id); // 查找按钮 ID 对应工具，未知 ID 返回空指针。
};
