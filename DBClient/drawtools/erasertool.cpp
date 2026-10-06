#include "erasertool.h"

#include "../canvasitems/eraseritem.h"

EraserTool::EraserTool()
    : PathTool(Shape_Eraser)
{
    // 1. 橡皮擦使用白色覆盖路径，宽度由 PathTool 按协议规则放大。
}

EraserTool::~EraserTool()
{
    // 1. 路径状态由 PathTool 自动释放。
}

PathItem* EraserTool::createItem(const QString& item_id) const
{
    // 1. 为本地或远端操作创建独立的 EraserItem。
    return new EraserItem(item_id);
}
