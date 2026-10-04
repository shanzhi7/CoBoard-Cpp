#include "imagepreviewdialog.h"

#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QShowEvent>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>

namespace
{
constexpr qreal ZOOM_STEP = 1.2;
constexpr qreal MAX_ZOOM_SCALE = 16.0;
constexpr int TOOLBAR_HEIGHT = 44;
}

ImagePreviewDialog::ImagePreviewDialog(const QPixmap& pixmap, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("图片预览"));
    setModal(true);
    setMinimumSize(320, 240);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);

    // 保留原图像素，由视图变换负责缩放；不反复生成缩略图，避免连续放大时累积画质损失。
    auto* scene = new QGraphicsScene(this);
    auto* image_item = scene->addPixmap(pixmap);
    // 统一按源像素建立坐标，避免高 DPI 的 QPixmap 自带比例导致原始比例显示尺寸错误。
    image_item->setScale(pixmap.devicePixelRatio());
    image_item->setTransformationMode(Qt::SmoothTransformation);
    scene->setSceneRect(image_item->sceneBoundingRect());

    _image_view = new QGraphicsView(scene, this);
    _image_view->setObjectName(QStringLiteral("image_preview_view"));
    _image_view->setFrameShape(QFrame::NoFrame);
    _image_view->setBackgroundBrush(QColor(32, 33, 36));
    _image_view->setRenderHint(QPainter::SmoothPixmapTransform);
    _image_view->setAlignment(Qt::AlignCenter);
    _image_view->setDragMode(QGraphicsView::ScrollHandDrag);
    _image_view->setTransformationAnchor(QGraphicsView::NoAnchor);
    _image_view->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    // 隐藏滚动条使图片区域保持完整；ScrollHandDrag 仍通过内部滚动条完成平移。
    _image_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _image_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* toolbar = new QWidget(this);
    toolbar->setObjectName(QStringLiteral("image_preview_toolbar"));
    toolbar->setFixedHeight(TOOLBAR_HEIGHT);
    toolbar->setStyleSheet(QStringLiteral(
        "QWidget#image_preview_toolbar { background-color: #292a2d; }"
        "QLabel, QToolButton { color: #eeeeee; }"
        "QToolButton { border: 0; background-color: transparent; font-size: 16px; }"
        "QToolButton:hover { background-color: #414348; }"
        "QToolButton:pressed { background-color: #515358; }"
        "QToolButton:disabled { color: #777777; }"));
    auto* toolbar_layout = new QHBoxLayout(toolbar);
    toolbar_layout->setContentsMargins(8, 4, 8, 4);
    toolbar_layout->setSpacing(8);
    toolbar_layout->addStretch();

    _zoom_out_button = new QToolButton(toolbar);
    _zoom_out_button->setText(QStringLiteral("-"));
    _zoom_out_button->setToolTip(QStringLiteral("缩小"));
    _zoom_out_button->setFixedSize(32, 32);
    toolbar_layout->addWidget(_zoom_out_button);

    _zoom_label = new QLabel(toolbar);
    _zoom_label->setAlignment(Qt::AlignCenter);
    _zoom_label->setFixedWidth(64);
    toolbar_layout->addWidget(_zoom_label);

    _zoom_in_button = new QToolButton(toolbar);
    _zoom_in_button->setText(QStringLiteral("+"));
    _zoom_in_button->setToolTip(QStringLiteral("放大"));
    _zoom_in_button->setFixedSize(32, 32);
    toolbar_layout->addWidget(_zoom_in_button);

    auto* fit_button = new QToolButton(toolbar);
    fit_button->setObjectName(QStringLiteral("image_preview_fit"));
    // Qt 标准图标通常为深色，转为浅色保证深色工具栏上仍有足够对比度。
    QPixmap fit_icon = style()->standardIcon(QStyle::SP_TitleBarMaxButton).pixmap(QSize(16, 16));
    QPainter icon_painter(&fit_icon);
    icon_painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    icon_painter.fillRect(fit_icon.rect(), Qt::white);
    icon_painter.end();
    fit_button->setIcon(QIcon(fit_icon));
    fit_button->setToolTip(QStringLiteral("适应窗口"));
    fit_button->setFixedSize(32, 32);
    toolbar_layout->addWidget(fit_button);

    auto* actual_size_button = new QToolButton(toolbar);
    actual_size_button->setObjectName(QStringLiteral("image_preview_actual_size"));
    actual_size_button->setText(QStringLiteral("1:1"));
    actual_size_button->setToolTip(QStringLiteral("原始比例"));
    actual_size_button->setFixedSize(40, 32);
    toolbar_layout->addWidget(actual_size_button);
    toolbar_layout->addStretch();

    connect(_zoom_out_button, &QToolButton::clicked, this, [this]() {
        ZoomImage(1.0 / ZOOM_STEP, _image_view->viewport()->rect().center());
    });
    connect(_zoom_in_button, &QToolButton::clicked, this, [this]() {
        ZoomImage(ZOOM_STEP, _image_view->viewport()->rect().center());
    });
    connect(fit_button, &QToolButton::clicked, this, &ImagePreviewDialog::FitImage);
    connect(actual_size_button, &QToolButton::clicked, this, &ImagePreviewDialog::ShowActualSize);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(_image_view, 1);
    layout->addWidget(toolbar);
    _image_view->viewport()->installEventFilter(this);

    // 初始窗口尽量匹配图片宽高比，小图也会放大适应；选用窗口所在屏幕而不是固定主屏。
    const QScreen* current_screen = screen();
    const QSize available_size = current_screen ? current_screen->availableGeometry().size() : QSize(1024, 768);
    const QSize preview_bounds(qMax(320, available_size.width() * 4 / 5),
                               qMax(196, available_size.height() * 4 / 5 - TOOLBAR_HEIGHT));
    const QSize image_size = pixmap.isNull() ? QSize(640, 480) : pixmap.size();
    const QSize preview_size = image_size.scaled(preview_bounds, Qt::KeepAspectRatio);
    resize(qMax(320, preview_size.width()), qMax(240, preview_size.height() + TOOLBAR_HEIGHT));
}

ImagePreviewDialog::~ImagePreviewDialog()
{
    // 场景、视图和工具栏均归属于对话框，Qt 会释放子控件与场景中的图片图元。
}

bool ImagePreviewDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _image_view->viewport())
    {
        if (event->type() == QEvent::Wheel)
        {
            auto* wheel_event = static_cast<QWheelEvent*>(event);
            // 普通滚轮按 120 一格计算；触控板使用像素增量，指数缩放可兼容连续的小幅输入。
            const int delta = wheel_event->angleDelta().y() != 0
                                  ? wheel_event->angleDelta().y()
                                  : wheel_event->pixelDelta().y();
            if (delta != 0)
            {
                ZoomImage(qPow(ZOOM_STEP, delta / 120.0), wheel_event->position().toPoint());
            }
            event->accept();
            return true;
        }
        if (event->type() == QEvent::MouseButtonDblClick)
        {
            auto* mouse_event = static_cast<QMouseEvent*>(event);
            if (mouse_event->button() == Qt::LeftButton)
            {
                // 双击在完整显示和原始比例间切换，方便快速查看细节后恢复全图。
                if (_auto_fit)
                {
                    ShowActualSize();
                } else
                {
                    FitImage();
                }
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::Resize)
        {
            // 手动缩放后不重新适应，避免用户拖动窗口时丢失正在查看的细节比例。
            if (_auto_fit)
            {
                FitImage();
            } else
            {
                UpdateZoomControls();
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void ImagePreviewDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (_auto_fit)
    {
        FitImage();
    }
}

qreal ImagePreviewDialog::FitScale() const
{
    const QRectF image_rect = _image_view->sceneRect();
    if (image_rect.isEmpty())
    {
        return 1.0;
    }
    const QSize viewport_size = _image_view->viewport()->size();
    return qMin(qMax(1, viewport_size.width()) / image_rect.width(),
                qMax(1, viewport_size.height()) / image_rect.height());
}

void ImagePreviewDialog::FitImage()
{
    // 直接计算等比矩阵，避免 fitInView 的额外边距；完整保留图片内容，不做裁切或拉伸。
    _auto_fit = true;
    const qreal fit_scale = FitScale();
    _image_view->setTransform(QTransform::fromScale(fit_scale, fit_scale));
    _image_view->centerOn(_image_view->sceneRect().center());
    UpdateZoomControls();
}

void ImagePreviewDialog::ShowActualSize()
{
    _auto_fit = false;
    _image_view->resetTransform();
    _image_view->centerOn(_image_view->sceneRect().center());
    UpdateZoomControls();
}

void ImagePreviewDialog::ZoomImage(qreal multiplier, const QPoint& viewport_pos)
{
    const qreal current_scale = _image_view->transform().m11();
    // 小图默认适应比例可能超过 1600%，上下限必须包含适应比例和原始比例，避免首次缩放反向跳变。
    const qreal min_scale = qMin(1.0, FitScale()) / 10.0;
    const qreal max_scale = qMax(MAX_ZOOM_SCALE, FitScale());
    const qreal target_scale = qBound(min_scale, current_scale * multiplier, max_scale);
    if (qFuzzyCompare(current_scale, target_scale))
    {
        return;
    }

    _auto_fit = false;
    const QPointF anchor_before = _image_view->mapToScene(viewport_pos);
    _image_view->scale(target_scale / current_scale, target_scale / current_scale);
    const QPointF anchor_after = _image_view->mapToScene(viewport_pos);
    // 修正缩放后的中心位置，让鼠标指向的图片细节尽量保持在原屏幕位置。
    const QPointF center_pos = _image_view->mapToScene(_image_view->viewport()->rect().center());
    _image_view->centerOn(center_pos + anchor_before - anchor_after);
    UpdateZoomControls();
}

void ImagePreviewDialog::UpdateZoomControls()
{
    const qreal current_scale = _image_view->transform().m11();
    _zoom_label->setText(QStringLiteral("%1%").arg(qRound(current_scale * 100.0)));
    _zoom_out_button->setEnabled(current_scale > qMin(1.0, FitScale()) / 10.0 + 0.000001);
    _zoom_in_button->setEnabled(current_scale < qMax(MAX_ZOOM_SCALE, FitScale()) - 0.000001);
}
