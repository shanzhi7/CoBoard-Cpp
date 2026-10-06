#include "pentool.h"

#include "../canvasitems/penitem.h"

PenTool::PenTool()
    : PathTool(Shape_Pen)
{
    // 1. 钢笔使用普通路径颜色和宽度规则。
}

PenTool::~PenTool()
{
    // 1. 路径状态由 PathTool 自动释放。
}

PathItem* PenTool::createItem(const QString& item_id) const
{
    // 1. 为本地或远端操作创建独立的 PenItem。
    return new PenItem(item_id);
}
