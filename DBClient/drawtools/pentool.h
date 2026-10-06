/***********************************************************************************
* @file         pentool.h
* @brief        普通钢笔自定义图元绘制工具
* @author       shanzhi
* @date         2026/10/06
* @history
***********************************************************************************/
#pragma once

#include "pathtool.h"

class PenTool final : public PathTool
{
public:
    PenTool(); // 创建普通钢笔 Tool。
    ~PenTool() override; // 释放普通钢笔 Tool。

protected:
    PathItem* createItem(const QString& item_id) const override; // 创建 PenItem。
};
