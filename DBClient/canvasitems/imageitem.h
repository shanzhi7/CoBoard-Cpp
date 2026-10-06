/***********************************************************************************
* @file         imageitem.h
* @brief        图片自定义图元、资源状态和图片变换接口
* @author       shanzhi
* @date         2026/10/06
* @history
***********************************************************************************/
#pragma once

#include "canvasitem.h"

#include <QPixmap>
#include <QSize>
#include <QRectF>

class QPainter;
class QGraphicsSceneMouseEvent;
class QStyleOptionGraphicsItem;
class QWidget;

// ImageItem 只负责图片图元的几何状态和绘制，不直接访问 HTTP、OSS 或 TCP。
class ImageItem : public CanvasItem
{
    Q_OBJECT

public:
    enum class ImageLoadState
    {
        Loading, // 已创建图元但图片数据尚未准备好，绘制占位内容。
        Ready, // 图片已通过格式和哈希校验，可以正常显示。
        Failed // 下载、读取或校验失败，绘制错误占位内容。
    };

    explicit ImageItem(const QString& item_id, QGraphicsItem* parent = nullptr); // 创建默认尺寸的图片图元。
    ~ImageItem() override; // 释放图片图元持有的像素数据，不删除外部资源缓存。

    QRectF boundingRect() const override; // 返回包含图片和边框的稳定局部矩形。
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override; // 绘制图片、加载占位图或错误提示。

    void setAssetMetadata(const QString& asset_id, const QString& asset_ref, const QString& asset_sha256, const QString& mime_type); // 设置资源引用和校验元数据。
    void setAssetIds(const QString& asset_id, const QString& asset_ref); // 在网关签名完成后更新资源 ID 和引用，不改变图像像素。
    QString assetId() const; // 返回资源的稳定 ID。
    QString assetRef() const; // 返回资源在网关或 OSS 侧的引用。
    QString assetSha256() const; // 返回用于完整性校验的 SHA-256 十六进制摘要。
    QString mimeType() const; // 返回图片 MIME 类型。

    void setOriginalSize(const QSize& original_size); // 保存源图片像素尺寸，不改变已明确设置的显示尺寸。
    QSize originalSize() const; // 返回源图片像素尺寸。
    void setDisplaySize(const QSizeF& display_size); // 设置画布中的显示尺寸并触发重绘。
    QSizeF displaySize() const; // 返回当前画布显示尺寸。

    void setPixmap(const QPixmap& pixmap); // 设置已校验的图片像素并进入可显示状态。
    QPixmap pixmap() const; // 返回当前图片像素的副本，便于导出或测试。
    void setLoadState(ImageLoadState load_state); // 更新加载状态并刷新占位内容。
    ImageLoadState loadState() const; // 返回当前加载状态。
    void setErrorMessage(const QString& error_message); // 设置失败原因，内容会显示在错误占位区域。
    QString errorMessage() const; // 返回最近一次加载失败原因。

protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override; // 成功图片双击预览，失败占位图双击重试，其他状态交回默认交互。

signals:
    void sigLoadStateChanged(QString item_id, ImageItem::ImageLoadState load_state); // 图片加载状态变化时通知资源管理或界面层。
    void sigRetryRequested(QString item_id); // 用户双击失败占位图时通知上层重新获取签名并下载。
    void sigPreviewRequested(QString item_id); // 用户双击成功图片时通知上层打开原图预览。

private:
    void updateDefaultDisplayRect(); // 在没有自定义显示尺寸时根据源图片尺寸计算合理的占位尺寸。

    QString _asset_id; // 图片资源稳定 ID，在线模式用于请求下载地址和去重缓存。
    QString _asset_ref; // 网关返回的资源引用，不保存临时签名或本地绝对路径。
    QString _asset_sha256; // 图片二进制的 SHA-256 摘要，用于下载后完整性校验。
    QString _mime_type; // 图片 MIME 类型，用于显示、上传和协议校验。
    QSize _original_size; // 源图片像素尺寸，用于跨客户端保持宽高比例。
    QRectF _item_rect; // 图片在图元局部坐标系中的显示矩形。
    QPixmap _pixmap; // 已加载的图片像素，加载失败时为空。
    ImageLoadState _load_state = ImageLoadState::Loading; // 当前资源加载状态。
    QString _error_message; // 失败占位图显示的简短错误信息。
    bool _has_custom_display_size = false; // 标记显示尺寸是否由变换或用户操作明确指定。
};
