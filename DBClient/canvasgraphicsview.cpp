#include "canvasgraphicsview.h"
#include "ui_canvasgraphicsview.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMimeData>
#include <QMouseEvent>
#include <QUrl>
#include <QWheelEvent>
#include <QtMath>

namespace
{
bool HasLocalFileUrls(const QMimeData* mime_data)
{
    if (!mime_data || !mime_data->hasUrls())
    {
        return false;
    }

    for (const QUrl& url : mime_data->urls())
    {
        if (url.isLocalFile())
        {
            return true;
        }
    }

    return false;
}
}

CanvasGraphicsView::CanvasGraphicsView(QWidget* parent)
    : QGraphicsView(parent)
    , _ui(new Ui::CanvasGraphicsView)
{
    // 1. 先应用独立 UI 文件中的基础属性，再设置缩放锚点和默认绘图模式。
    _ui->setupUi(this);

    // 2. 两层都开启拖放，确保手形关闭场景交互后仍可通过 viewport 导入图片。
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);

    // 3. 缩放以鼠标位置作为锚点；工具切换只改变事件模式，不重置这些锚点。
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    SetInteractionMode(CanvasInteractionMode::Draw);
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

void CanvasGraphicsView::SetInteractionMode(CanvasInteractionMode mode)
{
    // 1. ScrollHandDrag 在非交互视图中仍可滚动；关闭场景分发可以彻底隔离图片和绘图事件。
    _interaction_mode = mode;
    const bool is_pan = mode == CanvasInteractionMode::Pan;
    setInteractive(!is_pan);
    setDragMode(is_pan ? QGraphicsView::ScrollHandDrag : QGraphicsView::NoDrag);

    // 2. 不重置变换或滚动条；手形按下和释放时的开合光标继续由 Qt 管理。
    const Qt::CursorShape cursor = is_pan ? Qt::OpenHandCursor
                                         : (mode == CanvasInteractionMode::Select
                                                ? Qt::ArrowCursor
                                                : Qt::CrossCursor);
    setCursor(cursor);
    viewport()->setCursor(cursor);
}

CanvasInteractionMode CanvasGraphicsView::InteractionMode() const
{
    // 1. 返回本地模式，调用方不需要读取 Qt 内部拖动状态。
    return _interaction_mode;
}

void CanvasGraphicsView::mouseMoveEvent(QMouseEvent* event)
{
    // 1. Qt 先完成两条滚动条的更新，保证状态栏使用平移后的坐标映射。
    QGraphicsView::mouseMoveEvent(event);
    if (_interaction_mode == CanvasInteractionMode::Pan)
    {
        // 2. 手形模式不向场景分发鼠标事件，因此单独转发坐标而不创建绘画消息。
        emit SigCursorScenePositionChanged(mapToScene(event->position().toPoint()));
    }
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

void CanvasGraphicsView::keyPressEvent(QKeyEvent* event)
{
    if (event && event->matches(QKeySequence::Paste))
    {
        // 只在画布视图拥有焦点时触发，避免抢占聊天输入框等其他控件的粘贴行为。
        emit sigPasteImageRequested();
        event->accept();
        return;
    }

    // 其他快捷键继续交给 QGraphicsView，由 PaintScene 处理图片变换和绘图快捷键。
    QGraphicsView::keyPressEvent(event);
}

void CanvasGraphicsView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event && HasLocalFileUrls(event->mimeData()))
    {
        // 视图层只判断拖放载荷是否为本地文件，图片格式和大小交给 ImageAssetManager 校验。
        event->acceptProposedAction();
        return;
    }

    if (event)
    {
        event->ignore();
    }
}

void CanvasGraphicsView::dragMoveEvent(QDragMoveEvent* event)
{
    if (event && HasLocalFileUrls(event->mimeData()))
    {
        // 持续接受移动事件，保证用户松开鼠标时能够进入 dropEvent。
        event->acceptProposedAction();
        return;
    }

    if (event)
    {
        event->ignore();
    }
}

void CanvasGraphicsView::dropEvent(QDropEvent* event)
{
    if (!event || !HasLocalFileUrls(event->mimeData()))
    {
        if (event)
        {
            event->ignore();
        }
        return;
    }

    QStringList file_paths;
    for (const QUrl& url : event->mimeData()->urls())
    {
        if (!url.isLocalFile())
        {
            continue;
        }

        const QString file_path = url.toLocalFile();
        const QFileInfo file_info(file_path);
        if (!file_path.isEmpty() && file_info.exists() && file_info.isFile())
        {
            file_paths.append(file_path);
        }
    }

    if (file_paths.isEmpty())
    {
        event->ignore();
        return;
    }

    // QDropEvent 的位置相对于 viewport，QGraphicsView::mapToScene 正好使用同一坐标系。
    emit sigImageFilesDropped(file_paths,
                              mapToScene(event->position().toPoint()));
    event->acceptProposedAction();
}
