#pragma once

#include "drawtool.h"

#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QHash>
#include <QPixmap>
#include <QStack>
#include <QSize>
#include <QSizeF>

#include <memory>

class QGraphicsEllipseItem;
class QGraphicsSceneMouseEvent;
class QKeyEvent;
class ImageItem;

class PaintScene : public QGraphicsScene
{
    Q_OBJECT

public:
    explicit PaintScene(QObject* parent = nullptr);

    // 设置本地绘制样式，新的笔画从下一次鼠标按下开始使用该样式。
    void setPenColor(const QColor& color);
    void setPenWidth(int width);

    // 切换当前绘图策略；切换时会先结束未完成的本地笔画。
    void setShapeType(ShapeType type);

    // 更新当前场景的本地编辑权限。
    void setEditable(bool editable);

    // 返回当前场景是否允许本地鼠标绘制。
    bool isEditable() const;

    // 读取工具栏显示所需的当前画笔颜色和宽度。
    QColor getPenColor();
    int getPenWidth();

    // 鼠标离开画布或权限变化时隐藏橡皮擦范围光标。
    void hideEraserCursor();

    // 将远端 DrawReq 应用到当前场景中的对应操作。
    void applyRemoteDraw(const message::DrawReq& req);

    // 清空图元、远端操作、本地撤销记录和当前未完成操作。
    void resetScene();

    // 返回是否存在可撤销的本地图元。
    bool canUndoLocal() const;

    // 删除最后一个本地图元；远端图元不会进入该撤销栈。
    void undoLastLocalItem();

    // 创建图片图元并写入场景索引，record_undo 为 true 时离线导入可使用撤销。
    ImageItem* addImageItem(const QString& item_id,
                            const QString& asset_id,
                            const QString& asset_ref,
                            const QString& asset_sha256,
                            const QString& mime_type,
                            const QSize& original_size,
                            const QPixmap& pixmap,
                            const QPointF& scene_pos,
                            const QSizeF& display_size = QSizeF(),
                            bool record_undo = true);

    // 根据 item_id 删除图片图元，远程删除和本地清理共用同一套生命周期处理。
    bool removeImageItem(const QString& item_id);

    // 按 item_id 查找图片图元，找不到时返回 nullptr。
    ImageItem* findImageItem(const QString& item_id) const;

    // 应用远端图片变换，调用方负责在协议层完成权限和字段范围校验。
    bool updateImageTransform(const QString& item_id,
                              const QPointF& scene_pos,
                              const QSizeF& display_size,
                              qreal scale_x,
                              qreal scale_y,
                              qreal rotation);

protected:
    // 将鼠标按下事件转换为当前策略的一次本地操作。
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

    // 将鼠标移动事件交给当前操作，并发送增量同步信号。
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;

    // 结束当前操作、记录撤销信息并发送结束同步信号。
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

    // 将选中图片的删除、缩放和旋转快捷键转换为统一图元操作。
    void keyPressEvent(QKeyEvent* event) override;

private:
    // 结束当前本地操作并发送一条完整的结束事件。
    void finishCurrentOperation(const QPointF& end_pos);

    // 清除当前操作引用，不删除场景中的图元。
    void clearCurrentOperation();

    // 将已完成操作加入本地撤销栈和 item_id 索引。
    void recordFinishedLocalItem(const QString& item_id,
                                 const std::shared_ptr<IDrawTool>& operation);

    // 根据当前鼠标位置更新橡皮擦范围光标。
    void updateEraserCursor(const QPointF& pos);

    // 当前选择的形状类型和具体工具对象。
    ShapeType _currShapeType = Shape_Pen;
    std::shared_ptr<IDrawTool> _currentTool;

    // 当前正在进行的本地操作及其协议 ID。
    QString _currUuid;
    std::shared_ptr<IDrawTool> _currentOperation;

    // 当前画笔配置和本地编辑权限。
    QColor _penColor = Qt::black;
    int _penWidth = 3;
    bool _editable = true;

    // 只用于显示橡皮擦操作范围，不参与绘画和网络同步。
    QGraphicsEllipseItem* _eraserCursorItem = nullptr;

    // 远端每个 item_id 对应一个独立操作，允许多个笔画交错到达。
    struct RemoteItem
    {
        ShapeType shape = Shape_Unknown;
        std::shared_ptr<IDrawTool> operation;
    };
    QHash<QString, RemoteItem> _remoteItems;

    // 本地撤销记录保存操作句柄，避免把具体图元类型暴露给撤销管理逻辑。
    struct DrawItemRecord
    {
        QString item_id; // 本地操作关联的稳定图元标识。
        ShapeType shape = Shape_Unknown; // 传统绘画工具的类型，图片记录使用 Shape_Unknown。
        std::shared_ptr<IDrawTool> operation; // 传统绘画操作句柄，图片记录为空。
        ImageItem* image_item = nullptr; // 图片撤销记录对应的图元指针，由 PaintScene 负责生命周期。
        bool is_image = false; // 标识当前记录是图片图元还是传统绘画图元。
    };
    QStack<DrawItemRecord> _localUndoStack;

    // 保留 item_id 到图元的索引，便于兼容现有撤销和后续联机撤销扩展。
    QHash<QString, QGraphicsItem*> _localItems;

    // 图片图元单独维护索引，资源元数据和 QGraphicsItem 生命周期都由 PaintScene 统一管理。
    QHash<QString, ImageItem*> _imageItems;
    ImageItem* _activeImageItem = nullptr; // 当前由 QGraphicsScene 处理鼠标拖动的图片图元。

    // 删除图片时可选地同步移除撤销记录，避免远程删除影响离线撤销历史。
    bool removeImageItemInternal(const QString& item_id, bool remove_undo_record);

signals:
    // 本地笔画开始时发送 UUID、类型、起点、颜色和宽度。
    void sigStrokeStart(QString uuid, int type, QPointF startPos, QColor color, int width);

    // 本地笔画移动时发送新增点或当前几何终点。
    void sigStrokeMove(QString uuid, int type, QPointF currentPos);

    // 本地笔画结束时发送 UUID、类型和最终位置。
    void sigStrokeEnd(QString uuid, int type, QPointF endPos);

    // 鼠标坐标变化，用于状态栏显示当前位置。
    void sigCursorPosChanged(QPointF pos);

    // 本地图片创建完成后通知 Canvas，在线模式可据此请求签名并发送 ImageOperation。
    void sigImageInserted(QString item_id,
                          QString asset_id,
                          QString asset_ref,
                          QString asset_sha256,
                          QString mime_type,
                          QSize original_size,
                          QPointF scene_pos,
                          QSizeF display_size);

    // 图片图元被本地拖动或变换后通知 Canvas 发送更新操作。
    void sigImageGeometryChanged(QString item_id, QRectF scene_rect, qreal rotation, qreal scale);

    // 本地删除键请求移除图片，Canvas 负责序列化在线删除或直接执行离线删除。
    void sigImageDeleteRequested(QString item_id);

    // 用户双击失败占位图时转发重试请求；重新申请签名和下载仍由 Canvas 串行排队处理。
    void sigImageRetryRequested(QString item_id);

    // 用户双击成功图片时转发预览请求；具体窗口由 Canvas 创建，场景不依赖 UI 对话框。
    void sigImagePreviewRequested(QString item_id);
};
