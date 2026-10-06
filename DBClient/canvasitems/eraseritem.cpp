#include "eraseritem.h"

EraserItem::EraserItem(const QString& item_id, QGraphicsItem* parent)
    : PathItem(item_id, parent)
{
    // 1. 橡皮擦仍使用白色路径覆盖画布，样式由 EraserTool 设置。
}

EraserItem::~EraserItem()
{
    // 1. 路径和画笔由 PathItem 自动释放。
}
