#pragma once

#include <QGraphicsView>

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

signals:
    void zoomChanged(qreal zoom_factor); // 当前缩放比例发生变化

protected:
    void wheelEvent(QWheelEvent* event) override; // 处理 Ctrl 加滚轮缩放

private:
    static constexpr qreal MIN_ZOOM_FACTOR = 0.10; // 允许的最小缩放比例
    static constexpr qreal MAX_ZOOM_FACTOR = 4.00; // 允许的最大缩放比例
    static constexpr qreal ZOOM_STEP = 1.15; // 每个滚轮单位的缩放基准

    Ui::CanvasGraphicsView* _ui = nullptr; // 视图 UI 配置对象
};
