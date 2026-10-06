/***********************************************************************************
* @file         erasertool.h
* @brief        橡皮擦自定义图元绘制工具
* @author       shanzhi
* @date         2026/10/06
* @history
***********************************************************************************/
#pragma once

#include "pathtool.h"

class EraserTool final : public PathTool
{
public:
    EraserTool(); // 创建橡皮擦 Tool。
    ~EraserTool() override; // 释放橡皮擦 Tool。

protected:
    PathItem* createItem(const QString& item_id) const override; // 创建 EraserItem。
};
