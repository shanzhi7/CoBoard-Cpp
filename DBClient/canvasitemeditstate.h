/***********************************************************************************
* @file         canvasitemeditstate.h
* @brief        图元编辑快照、几何适配和统一撤销值类型
* @author       shanzhi
* @date         2026/10/15
* @history
***********************************************************************************/
#pragma once

#include "canvasitems/textitem.h"

#include <QHash>
#include <QLineF>
#include <QPainterPath>
#include <QTransform>
#include <QVector>

struct CanvasItemEditState
{
    QString _item_id; // 顶层图元的稳定标识。
    int _kind = 0; // 与新增编辑协议一致的图元类型，1 至 9。
    QPointF _position; // 图元在场景中的位置。
    QPointF _origin; // 局部旋转及缩放中心。
    qreal _rotation = 0.0; // 绕原点的角度，单位为度。
    qreal _scale = 1.0; // Qt 公共等比缩放。
    QTransform _transform; // 保留导入文件的公共仿射变换。
    QVector<double> _geometry; // 路径为六元映射，矩形和线为四元，图片和文字为宽高。
    QString _text; // 独立文字或封闭图元附属文字的纯文本。
    TextStyle _text_style; // 独立文字或附属文字的统一样式。
    bool operator==(const CanvasItemEditState& other) const; // 判断一次编辑是否实际改变了可同步状态。
    bool operator!=(const CanvasItemEditState& other) const; // 返回完整状态差异。
};

struct CanvasItemEditUndoOperation
{
    QVector<CanvasItemEditState> _before; // 同一手势中各图元的操作前快照。
    QVector<CanvasItemEditState> _after; // 同一手势中各图元的最终快照。
    QString _gesture_id; // 将混合多选移动合并为一条撤销记录。
    QHash<QString, quint64> _revisions; // 在线确认版本，撤销使用服务端版本校验。
};

namespace CanvasItemEdit
{
int Kind(const CanvasItem* item); // 返回图元对应的稳定编辑类型。
bool IsClosed(const CanvasItem* item); // 判断是否支持附属文字。
QRectF Bounds(const CanvasItem* item); // 返回不含交互控制柄的局部几何范围。
QPainterPath Outline(const CanvasItem* item); // 返回实际笔迹轮廓或图片、文字完整区域。
QPainterPath Interior(const CanvasItem* item); // 返回封闭图形内部，供双击编辑命中。
CanvasItemEditState Capture(const CanvasItem* item); // 读取完整可编辑值状态。
bool Apply(CanvasItem* item, const CanvasItemEditState& state); // 不发起本地编辑提交地恢复完整状态。
bool IsValid(const CanvasItemEditState& state); // 拒绝非有限变换、非法尺寸和超限文字。
void Resize(CanvasItem* item, const CanvasItemEditState& before, const QRectF& target); // 从起始快照更新局部尺寸并保持字体及线宽。
void LayoutBoundText(CanvasItem* item); // 按安全内接矩形排版并仅在必要时增加父高度。
}

Q_DECLARE_METATYPE(CanvasItemEditState)
