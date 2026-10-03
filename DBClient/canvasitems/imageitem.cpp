#include "imageitem.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

namespace
{
constexpr qreal DEFAULT_IMAGE_WIDTH = 160.0;
constexpr qreal DEFAULT_IMAGE_HEIGHT = 120.0;
constexpr qreal MIN_IMAGE_SIZE = 1.0;
}

ImageItem::ImageItem(const QString& item_id, QGraphicsItem* parent)
    : CanvasItem(item_id, parent)
    , _item_rect(0.0, 0.0, DEFAULT_IMAGE_WIDTH, DEFAULT_IMAGE_HEIGHT)
{
    // 初始状态必须是 Loading，即使本地立即会填充图片，避免在异步接收端短暂显示错误状态。
    setLoadState(ImageLoadState::Loading);
}

ImageItem::~ImageItem()
{
    // QPixmap 是值类型，会在图元销毁时自动释放；资源文件由 ImageAssetManager 的缓存统一管理。
}

QRectF ImageItem::boundingRect() const
{
    // 给边框留出一个像素的绘制空间，防止选中框被 QGraphicsScene 裁掉。
    return _item_rect.adjusted(-1.0, -1.0, 1.0, 1.0);
}

void ImageItem::paint(QPainter* painter,
                      const QStyleOptionGraphicsItem* option,
                      QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    if (!painter)
    {
        return;
    }

    // 图片可能在网络线程完成后才回到 UI 线程，这里只读取已经提交到图元的值，避免绘制阶段触碰网络对象。
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (_load_state == ImageLoadState::Ready && !_pixmap.isNull())
    {
        // KeepAspectRatioByExpanding 会裁剪内容，不适合画布资源；这里保持完整图片并在矩形内留出空白。
        const QPixmap scaled_pixmap = _pixmap.scaled(_item_rect.size().toSize(),
                                                     Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation);
        const QRectF target_rect(QPointF(_item_rect.center().x() - scaled_pixmap.width() / 2.0,
                                         _item_rect.center().y() - scaled_pixmap.height() / 2.0),
                                 QSizeF(scaled_pixmap.size()));
        painter->drawPixmap(target_rect,
                            scaled_pixmap,
                            QRectF(scaled_pixmap.rect()));
    }
    else
    {
        // Loading 和 Failed 都需要保留稳定尺寸，否则异步状态改变会造成场景布局跳动。
        const QColor background = _load_state == ImageLoadState::Failed
                                      ? QColor(255, 239, 239)
                                      : QColor(239, 243, 248);
        painter->fillRect(_item_rect, background);
        painter->setPen(_load_state == ImageLoadState::Failed
                            ? QColor(190, 70, 70)
                            : QColor(90, 100, 115));
        const QString text = _load_state == ImageLoadState::Failed
                                 ? (_error_message.isEmpty()
                                        ? QStringLiteral("图片加载失败")
                                        : _error_message)
                                 : QStringLiteral("正在加载图片...");
        painter->drawText(_item_rect.adjusted(8.0, 8.0, -8.0, -8.0),
                          Qt::AlignCenter | Qt::TextWordWrap,
                          text);
    }

    // 选中框属于交互反馈，不会进入资源同步数据，避免远端用户看到本地选择状态。
    if (isSelected())
    {
        QPen selection_pen(QColor(40, 120, 220));
        selection_pen.setWidthF(1.5);
        painter->setPen(selection_pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(_item_rect);
    }

    painter->restore();
}

void ImageItem::setAssetMetadata(const QString& asset_id,
                                 const QString& asset_ref,
                                 const QString& asset_sha256,
                                 const QString& mime_type)
{
    // 元数据来自网关或本地导入，图元只保存引用，不保存带权限的临时 URL。
    _asset_id = asset_id;
    _asset_ref = asset_ref;
    _asset_sha256 = asset_sha256;
    _mime_type = mime_type;
}

void ImageItem::setAssetIds(const QString& asset_id, const QString& asset_ref)
{
    // 上传签名返回的资源 ID 可能晚于本地预览图元创建，单独更新 ID 可以保持预览连续可见。
    if (!asset_id.isEmpty())
    {
        _asset_id = asset_id;
    }
    if (!asset_ref.isEmpty())
    {
        _asset_ref = asset_ref;
    }
}

QString ImageItem::assetId() const
{
    // 资源 ID 由调用方用于构造下载签名请求，返回副本可以避免外部修改图元元数据。
    return _asset_id;
}

QString ImageItem::assetRef() const
{
    // asset_ref 只表示稳定对象引用，不包含短期访问签名，适合保存到房间操作历史。
    return _asset_ref;
}

QString ImageItem::assetSha256() const
{
    // 摘要用于缓存命中和网络下载后的完整性校验，始终按字符串返回以保持协议格式一致。
    return _asset_sha256;
}

QString ImageItem::mimeType() const
{
    // MIME 类型由本地解码或网关白名单校验得到，供上传 Content-Type 和远端重建使用。
    return _mime_type;
}

void ImageItem::setOriginalSize(const QSize& original_size)
{
    // 非法尺寸会导致 QGraphicsScene 计算出空包围盒，直接忽略而不是创建不可见图元。
    if (!original_size.isValid() || original_size.isEmpty())
    {
        return;
    }

    _original_size = original_size;
    if (!_has_custom_display_size)
    {
        updateDefaultDisplayRect();
    }
    update();
}

QSize ImageItem::originalSize() const
{
    // 原始像素尺寸与画布显示尺寸分离，缩放图元时仍能保留源资源信息。
    return _original_size;
}

void ImageItem::setDisplaySize(const QSizeF& display_size)
{
    // 变换数据来自网络时也要限制为正数，避免恶意或损坏数据产生负包围盒。
    if (!display_size.isValid() ||
        display_size.width() < MIN_IMAGE_SIZE ||
        display_size.height() < MIN_IMAGE_SIZE)
    {
        return;
    }

    prepareGeometryChange();
    _item_rect.setSize(display_size);
    _has_custom_display_size = true;
    update();
}

QSizeF ImageItem::displaySize() const
{
    // 返回局部显示尺寸，场景位置和旋转由 QGraphicsItem 的标准变换属性单独保存。
    return _item_rect.size();
}

void ImageItem::setPixmap(const QPixmap& pixmap)
{
    // 先通知场景几何即将变化，再替换像素，避免图片尺寸参与默认布局时产生未通知的包围盒改变。
    _pixmap = pixmap;
    if (!_pixmap.isNull())
    {
        if (!_original_size.isValid())
        {
            _original_size = _pixmap.size();
        }
        if (!_has_custom_display_size)
        {
            updateDefaultDisplayRect();
        }
        setLoadState(ImageLoadState::Ready);
    }
    else
    {
        setLoadState(ImageLoadState::Failed);
    }
    update();
}

QPixmap ImageItem::pixmap() const
{
    // QPixmap 使用隐式共享，返回副本不会立即复制像素，也不会暴露图元内部可写引用。
    return _pixmap;
}

void ImageItem::setLoadState(ImageLoadState load_state)
{
    if (_load_state == load_state)
    {
        update();
        return;
    }

    _load_state = load_state;
    update();
    emit sigLoadStateChanged(itemId(), _load_state);
}

ImageItem::ImageLoadState ImageItem::loadState() const
{
    // 资源管理器和界面层通过状态区分加载中、成功和失败占位图。
    return _load_state;
}

void ImageItem::setErrorMessage(const QString& error_message)
{
    // 错误文本只用于界面提示，不记录响应正文或签名 URL，避免敏感信息进入图元状态。
    _error_message = error_message.left(128);
    update();
}

QString ImageItem::errorMessage() const
{
    // 错误文本只用于本地 UI 提示，调用方不应把它作为协议字段广播。
    return _error_message;
}

void ImageItem::updateDefaultDisplayRect()
{
    if (!_original_size.isValid() || _original_size.isEmpty())
    {
        return;
    }

    // 大图默认限制为 800x600 内，保留宽高比，避免一张高分辨率图片遮挡整个房间。
    const QSizeF source_size = _original_size;
    const QSizeF max_size(800.0, 600.0);
    const QSizeF display_size = source_size.scaled(max_size, Qt::KeepAspectRatio);
    prepareGeometryChange();
    _item_rect.setSize(display_size);
}

void ImageItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    // 只有 Failed 状态的占位图提供双击重试入口；Loading 和 Ready 状态下双击没有业务含义，
    // 交回 QGraphicsObject 默认处理，避免干扰选中、移动等标准图元交互。
    if (_load_state != ImageLoadState::Failed)
    {
        QGraphicsObject::mouseDoubleClickEvent(event);
        return;
    }

    // 事件在信号发出前确认接收，防止场景再把双击转发给父图元或视图默认处理器造成重复响应。
    if (event)
    {
        event->accept();
    }

    // 重试只传递图元稳定 ID；资源元数据仍保存在图元内，由 Canvas 在重新申请签名时统一读取，
    // 这样重试会走与首次下载完全相同的串行签名队列，不会绕过任何权限或校验逻辑。
    emit sigRetryRequested(itemId());
}
