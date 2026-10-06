#include "penitem.h"

PenItem::PenItem(const QString& item_id, QGraphicsItem* parent)
    : PathItem(item_id, parent)
{
    // 1. 普通钢笔沿用 PathItem 的路径绘制能力，样式由 PenTool 设置。
}

PenItem::~PenItem()
{
    // 1. 路径和画笔由 PathItem 自动释放。
}
