#pragma once

#include <QDialog>
#include <QPoint>

class QEvent;
class QGraphicsView;
class QLabel;
class QPixmap;
class QShowEvent;
class QToolButton;

// ImagePreviewDialog 负责以独立窗口展示已经加载到客户端内存中的原图。
// 对话框不访问网络和资源管理器，关闭后不会影响画布图元或本地缓存。
class ImagePreviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImagePreviewDialog(const QPixmap& pixmap, QWidget* parent = nullptr); // 创建支持适应窗口、缩放和平移的图片预览。
    ~ImagePreviewDialog() override; // 释放对话框及其子控件。

protected:
    bool eventFilter(QObject* watched, QEvent* event) override; // 在图片区域处理滚轮缩放、双击切换和窗口尺寸变化。
    void showEvent(QShowEvent* event) override; // 首次显示后按真实可视区域计算适应比例。

private:
    void FitImage(); // 等比显示完整图片，并启用随窗口变化自动适应。
    void ShowActualSize(); // 恢复原始比例并关闭自动适应。
    void ZoomImage(qreal multiplier, const QPoint& viewport_pos); // 以指定视口位置为锚点缩放，防止鼠标下的细节跳动。
    void UpdateZoomControls(); // 刷新实际缩放百分比和按钮边界状态。
    qreal FitScale() const; // 根据图片与视口尺寸计算完整显示所需的比例。

    QGraphicsView* _image_view = nullptr; // 图片视图，Qt 负责拖动平移及场景坐标转换。
    QLabel* _zoom_label = nullptr; // 当前图片像素与屏幕逻辑像素的缩放比例。
    QToolButton* _zoom_out_button = nullptr; // 到达最小比例时禁用缩小。
    QToolButton* _zoom_in_button = nullptr; // 到达最大比例时禁用放大。
    bool _auto_fit = true; // 仅适应模式跟随窗口变化，手动缩放后保留用户选择的比例。
};
