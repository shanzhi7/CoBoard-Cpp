/***********************************************************************************
* @file         canvasgraphicsview.h
* @brief        画布视口平移、缩放及图片拖放入口
* @author       shanzhi
* @date         2026/10/07
* @history
***********************************************************************************/
#pragma once

#include <QGraphicsView>
#include <QPointF>
#include <QStringList>

#include "canvasinteractionmode.h"

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

namespace Ui
{
class CanvasGraphicsView;
}

class CanvasGraphicsView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit CanvasGraphicsView(QWidget* parent = nullptr); // 初始化画布视图和缩放行为
    ~CanvasGraphicsView() override; // 释放视图 UI 资源

    qreal zoomFactor() const; // 返回当前实际缩放比例
    void resetZoom(); // 将视图缩放比例恢复为 100%
    void SetInteractionMode(CanvasInteractionMode mode); // 切换视口事件模式，保留缩放和滚动位置。
    CanvasInteractionMode InteractionMode() const; // 返回当前视口交互模式。

signals:
    void zoomChanged(qreal zoom_factor); // 当前缩放比例发生变化
    void sigImageFilesDropped(QStringList file_paths, QPointF scene_pos); // 本地图片文件拖入画布时通知上层处理资源导入
    void sigPasteImageRequested(); // 用户在画布视图中触发粘贴快捷键时通知上层读取剪贴板
    void SigCursorScenePositionChanged(QPointF scene_pos); // 手形模式下向状态栏转发滚动后的场景坐标。

protected:
    void wheelEvent(QWheelEvent* event) override; // 处理 Ctrl 加滚轮缩放
    void keyPressEvent(QKeyEvent* event) override; // 拦截画布视图中的粘贴快捷键并保留其他快捷键
    void mouseMoveEvent(QMouseEvent* event) override; // 保留 Qt 视口拖动并更新手形模式下的坐标显示。
    void dragEnterEvent(QDragEnterEvent* event) override; // 接受包含本地文件 URL 的拖放进入事件
    void dragMoveEvent(QDragMoveEvent* event) override; // 拖放移动期间持续确认本地文件格式
    void dropEvent(QDropEvent* event) override; // 将本地文件路径和场景坐标交给 Canvas

private:
    static constexpr qreal MIN_ZOOM_FACTOR = 0.10; // 允许的最小缩放比例
    static constexpr qreal MAX_ZOOM_FACTOR = 4.00; // 允许的最大缩放比例
    static constexpr qreal ZOOM_STEP = 1.15; // 每个滚轮单位的缩放基准

    Ui::CanvasGraphicsView* _ui = nullptr; // 视图 UI 配置对象
    CanvasInteractionMode _interaction_mode = CanvasInteractionMode::Draw; // 视口当前使用的事件模式。
};
