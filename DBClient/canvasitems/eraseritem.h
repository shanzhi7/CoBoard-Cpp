/***********************************************************************************
* @file         eraseritem.h
* @brief        橡皮擦路径自定义图元
* @author       shanzhi
* @date         2026/10/06
* @history
***********************************************************************************/
#pragma once

#include "pathitem.h"

class EraserItem final : public PathItem
{
public:
    explicit EraserItem(const QString& item_id, QGraphicsItem* parent = nullptr); // 创建橡皮擦图元。
    ~EraserItem() override; // 释放橡皮擦图元。
};
