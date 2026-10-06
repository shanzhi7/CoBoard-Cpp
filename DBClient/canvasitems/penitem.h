/***********************************************************************************
* @file         penitem.h
* @brief        普通钢笔路径自定义图元
* @author       shanzhi
* @date         2026/10/06
* @history
***********************************************************************************/
#pragma once

#include "pathitem.h"

class PenItem final : public PathItem
{
public:
    explicit PenItem(const QString& item_id, QGraphicsItem* parent = nullptr); // 创建普通钢笔图元。
    ~PenItem() override; // 释放普通钢笔图元。
};
