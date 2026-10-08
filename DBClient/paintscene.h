/***********************************************************************************
* @file         paintscene.h
* @brief        画布绘图、图片选择及本地和远端图元生命周期管理
* @author       shanzhi
* @date         2026/10/07
* @history
***********************************************************************************/
#pragma once

#include <memory>

#include <QGraphicsScene>
#include <QHash>
#include <QPixmap>
#include <QSize>
#include <QSizeF>
#include <QStack>

#include "canvasinteractionmode.h"
#include "canvasdocument.h"
#include "drawtool.h"

class QGraphicsEllipseItem;
class QGraphicsSceneMouseEvent;
class QKeyEvent;
class ImageItem;

class PaintScene : public QGraphicsScene
{
    Q_OBJECT

public:
    explicit PaintScene(QObject* parent = nullptr); // 初始化绘图工厂和橡皮擦辅助光标。

    void setPenColor(const QColor& color); // 设置之后创建的笔画颜色。
    void setPenWidth(int width); // 设置之后创建的笔画宽度。
    void setShapeType(ShapeType type); // 结束未完成笔画后切换绘图策略。
    void SetInteractionMode(CanvasInteractionMode mode); // 切换事件模式并保留当前图元选择。
    CanvasInteractionMode InteractionMode() const; // 返回当前场景事件模式。
    void setEditable(bool editable); // 更新权限并立即停止未授权的本地交互。
    bool isEditable() const; // 返回本地编辑权限。
    QColor getPenColor(); // 返回当前画笔颜色。
    int getPenWidth(); // 返回当前画笔宽度。
    void hideEraserCursor(); // 鼠标离开画布或模式变化时隐藏辅助光标。
    void applyRemoteDraw(const message::DrawReq& req); // 将远端绘画请求应用到对应图元。
    void resetScene(); // 清空图元、交互状态及本地和远端记录。
    bool canUndoLocal() const; // 判断是否存在可撤销的本地图元。
    void undoLastLocalItem(); // 删除最后一个本地图元，不影响远端图元。
    bool BuildCanvasDocument(CanvasDocument* document, QString* error_message) const; // 按场景绘制顺序构建独立快照。
    bool LoadCanvasDocument(const CanvasDocument& document, QString* error_message); // 仅向尚未接入视图的新场景恢复图元。
    QImage RenderCanvasImage(QString* error_message); // 按画布原尺寸渲染并恢复交互反馈。

    ImageItem* addImageItem(const QString& item_id,
                           const QString& asset_id,
                           const QString& asset_ref,
                           const QString& asset_sha256,
                           const QString& mime_type,
                           const QSize& original_size,
                           const QPixmap& pixmap,
                           const QPointF& scene_pos,
                           const QSizeF& display_size = QSizeF(),
                           bool record_undo = true); // 创建图片并按当前模式配置交互。
    bool removeImageItem(const QString& item_id); // 按稳定 ID 删除图片并清理撤销记录。
    ImageItem* findImageItem(const QString& item_id) const; // 按 ID 查找图片，缺失时返回 nullptr。
    bool updateImageTransform(const QString& item_id,
                              const QPointF& scene_pos,
                              const QSizeF& display_size,
                              qreal scale_x,
                              qreal scale_y,
                              qreal rotation); // 应用调用方已经校验过的远端图片变换。

signals:
    void sigStrokeStart(QString uuid, int type, QPointF startPos, QColor color, int width); // 转发本地 START 的 ID、样式和起点。
    void sigStrokeMove(QString uuid, int type, QPointF currentPos); // 转发本地路径点或几何终点。
    void sigStrokeEnd(QString uuid, int type, QPointF endPos); // 转发本地 END 和最终位置。
    void sigCursorPosChanged(QPointF pos); // 向状态栏转发非手形模式的场景坐标。
    void sigImageInserted(QString item_id,
                          QString asset_id,
                          QString asset_ref,
                          QString asset_sha256,
                          QString mime_type,
                          QSize original_size,
                          QPointF scene_pos,
                          QSizeF display_size); // 通知 Canvas 图片已创建，资源同步由 Canvas 负责。
    void sigImageGeometryChanged(QString item_id, QRectF scene_rect, qreal rotation, qreal scale); // 转发实时变换和拖动结束的最终状态。
    void sigImageDeleteRequested(QString item_id); // 通知 Canvas 执行图片删除。
    void sigImageRetryRequested(QString item_id); // 转发鼠标模式下的图片重试意图。
    void sigImagePreviewRequested(QString item_id); // 转发鼠标模式下的图片预览意图。

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override; // 按模式分发图片选择或绘图 START。
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override; // 更新图片拖动或绘图 MOVE。
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override; // 提交图片最终状态或绘图 END。
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override; // 仅鼠标模式向图片分发双击事件。
    void keyPressEvent(QKeyEvent* event) override; // 按模式和权限处理图片编辑快捷键。

private:
    void finishCurrentOperation(const QPointF& end_pos); // 补齐笔画并记录撤销和 END。
    void clearCurrentOperation(); // 释放当前操作引用，不删除图元。
    void recordFinishedLocalItem(const QString& item_id,
                                 const std::shared_ptr<IDrawTool>& operation); // 登记完成图元和撤销句柄。
    void updateEraserCursor(const QPointF& pos); // 更新与擦除宽度一致的光标。
    void PrepareImageDrag(); // 记录 Qt 鼠标抓取对应的多选图片起始位置。
    void FinishImageDrag(bool send_final); // 清理拖动记录，并按需补发最终状态。
    void UpdateImageInteraction(ImageItem* image_item); // 更新移动和焦点权限，保留选择状态。
    bool removeImageItemInternal(const QString& item_id, bool remove_undo_record); // 删除图片并可选清理撤销记录。

    struct RemoteItem
    {
        ShapeType shape = Shape_Unknown; // 远端协议图元类型。
        std::shared_ptr<IDrawTool> operation; // 同一 ID 的独立远端操作。
    };

    struct DrawItemRecord
    {
        QString item_id; // 本地操作关联的稳定标识。
        ShapeType shape = Shape_Unknown; // 绘画类型，图片使用 Shape_Unknown。
        std::shared_ptr<IDrawTool> operation; // 绘画句柄，图片记录为空。
        ImageItem* image_item = nullptr; // 场景拥有的图片指针。
        bool is_image = false; // 当前记录是否为图片。
    };

    ShapeType _currShapeType = Shape_Pen; // 当前绘图类型，不包含交互模式。
    std::shared_ptr<IDrawTool> _currentTool; // 当前绘图策略。
    QString _currUuid; // 当前本地笔画 ID。
    std::shared_ptr<IDrawTool> _currentOperation; // 未完成的本地绘图操作。
    QColor _penColor = Qt::black; // 新图元使用的画笔颜色。
    int _penWidth = 3; // 新图元使用的画笔宽度。
    bool _editable = true; // 本地编辑权限。
    CanvasInteractionMode _interaction_mode = CanvasInteractionMode::Draw; // 场景事件分发模式。
    QGraphicsEllipseItem* _eraserCursorItem = nullptr; // 不参与绘画和同步的辅助光标。
    QHash<QString, RemoteItem> _remoteItems; // 远端图元 ID 对应的操作。
    QStack<DrawItemRecord> _localUndoStack; // 保持本地创建顺序的撤销栈。
    QHash<QString, QGraphicsItem*> _localItems; // 本地绘画图元 ID 索引。
    QHash<QString, ImageItem*> _imageItems; // 本地和远端图片统一索引。
    QHash<QString, QPointF> _image_drag_start_positions; // 拖动起始位置，避免持有图片裸指针。
};
