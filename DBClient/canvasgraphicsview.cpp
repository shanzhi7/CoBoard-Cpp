#include "canvasgraphicsview.h"
#include "ui_canvasgraphicsview.h"

#include <QWheelEvent>
#include <QtMath>

CanvasGraphicsView::CanvasGraphicsView(QWidget* parent)
    : QGraphicsView(parent)
    , _ui(new Ui::CanvasGraphicsView)
{
    // 先应用独立 UI 文件中的基础属性，再在代码中设置交互相关的锚点。
    _ui->setupUi(this);

    // 缩放时以鼠标所在位置作为锚点，避免放大后工作区域跳离鼠标位置。
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
}

CanvasGraphicsView::~CanvasGraphicsView()
{
    delete _ui;
}

qreal CanvasGraphicsView::zoomFactor() const
{
    // QGraphicsView 的 m11() 是当前变换矩阵的横向缩放分量；本功能只使用等比缩放。
    return transform().m11();
}

void CanvasGraphicsView::resetZoom()
{
    // 视图目前只使用缩放变换，重置整个矩阵可以避免残留其他变换状态。
    resetTransform();
    emit zoomChanged(zoomFactor());
}

void CanvasGraphicsView::wheelEvent(QWheelEvent* event)
{
    // 未按住 Ctrl 时保留 QGraphicsView 的默认滚轮滚动行为。
    if (!(event->modifiers() & Qt::ControlModifier))
    {
        QGraphicsView::wheelEvent(event);
        return;
    }

    // 普通滚轮优先使用 angleDelta；触控板等设备没有角度增量时回退到 pixelDelta。
    qreal delta = event->angleDelta().y();
    if (qFuzzyIsNull(delta))
        delta = event->pixelDelta().y();

    // Ctrl 加滚轮事件由缩放逻辑消费，即使设备没有有效增量也不能继续触发普通滚动。
    if (qFuzzyIsNull(delta))
    {
        event->accept();
        return;
    }

    const qreal current_zoom = zoomFactor();
    const qreal zoom_multiplier = qPow(ZOOM_STEP, delta / 120.0);
    const qreal target_zoom = qBound(MIN_ZOOM_FACTOR,
                                     current_zoom * zoom_multiplier,
                                     MAX_ZOOM_FACTOR);

    // 到达边界后不再改变矩阵，但仍接受事件，避免边界处出现普通滚动或视图抖动。
    if (qFuzzyCompare(current_zoom, target_zoom))
    {
        event->accept();
        return;
    }

    // AnchorUnderMouse 会在变换前后调整滚动条，使鼠标下的场景坐标保持稳定。
    scale(target_zoom / current_zoom, target_zoom / current_zoom);
    emit zoomChanged(zoomFactor());
    event->accept();
}
