/***********************************************************************************
* @file         canvasselectioncontroller.h
* @brief        图元轮廓选择、混合拖动、尺寸变更和旋转手势控制器
* @author       shanzhi
* @date         2026/10/15
* @history
***********************************************************************************/
#pragma once

#include "canvasitemeditstate.h"

#include <QObject>
#include <QPointer>

class PaintScene;
class QGraphicsSceneMouseEvent;
class QGraphicsView;
class CanvasSelectionOverlay;

class CanvasSelectionController : public QObject
{
public:
    explicit CanvasSelectionController(PaintScene* scene); // 创建由场景拥有的控制器及非内容覆盖层。
    ~CanvasSelectionController() override; // 释放场景拥有的覆盖层前解除引用。
    bool Press(QGraphicsSceneMouseEvent* event); // 按轮廓、控制柄和已选内部决定手势。
    bool Move(QGraphicsSceneMouseEvent* event); // 依据开始快照实时计算一次手势的结果。
    bool Release(QGraphicsSceneMouseEvent* event); // 松开后提交最终状态及一条撤销记录。
    CanvasItem* HitItem(const QPointF& scene_pos, bool include_closed_interior, QWidget* widget = nullptr) const; // 按堆叠顺序命中顶层内容。
    void Cancel(const QString& item_id = QString(), bool notify = false); // 恢复活动手势，远端覆盖时静默取消。
    bool IsActive() const; // 返回是否存在鼠标按下后的编辑手势。
    void Refresh(); // 刷新选择覆盖层，不改变选择状态。
    void SetOverlayVisible(bool visible); // 导出和场景替换期间显式隐藏交互反馈。
    void BeforeSceneClear(); // 场景清空前移除覆盖层，避免重复释放。
    void AfterSceneClear(); // 场景清空后重新安装覆盖层。

private:
    QGraphicsView* ViewForWidget(QWidget* widget) const; // 查找产生事件的视口，保持固定像素命中。
    QVector<CanvasItem*> SelectedItems() const; // 返回可交互的顶层选中图元。
    void StartGesture(const QVector<CanvasItem*>& items, int handle, QGraphicsSceneMouseEvent* event); // 记录手势开始快照和坐标基准。
    void UpdateSingleItem(const QPointF& scene_pos); // 按局部轴执行单个图元伸缩或旋转。
    void NotifyChanges(bool finished); // 按同一手势标识提交实际变化。

    PaintScene* _scene = nullptr; // Qt 父对象拥有控制器，场景生命周期更长。
    CanvasSelectionOverlay* _overlay = nullptr; // 由场景绘制的辅助覆盖层，不参与文件和网络。
    QVector<CanvasItemEditState> _before; // 同一手势全部图元的开始快照。
    QString _gesture_id; // 合并混合多选移动的撤销标识。
    QPointF _start_scene_pos; // 按下时的场景坐标。
    QPointF _start_local_pos; // 单图元按下时的局部坐标。
    QRectF _start_bounds; // 单图元开始时不含控制柄的真实范围。
    QTransform _scene_to_local; // 手势开始时的逆变换，避免移动期间坐标漂移。
    int _handle = -1; // -1 为移动，0 至 3 为角，4 至 7 为边，8 为旋转。
    bool _is_active = false; // 是否正在拥有鼠标手势。
};
