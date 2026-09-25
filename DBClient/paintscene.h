#pragma once

#include "drawtool.h"

#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QHash>
#include <QStack>

#include <memory>

class QGraphicsEllipseItem;
class QGraphicsSceneMouseEvent;

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

protected:
    // 将鼠标按下事件转换为当前策略的一次本地操作。
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

    // 将鼠标移动事件交给当前操作，并发送增量同步信号。
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;

    // 结束当前操作、记录撤销信息并发送结束同步信号。
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

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
        QString item_id;
        ShapeType shape = Shape_Unknown;
        std::shared_ptr<IDrawTool> operation;
    };
    QStack<DrawItemRecord> _localUndoStack;

    // 保留 item_id 到图元的索引，便于兼容现有撤销和后续联机撤销扩展。
    QHash<QString, QGraphicsItem*> _localItems;

signals:
    // 本地笔画开始时发送 UUID、类型、起点、颜色和宽度。
    void sigStrokeStart(QString uuid, int type, QPointF startPos, QColor color, int width);

    // 本地笔画移动时发送新增点或当前几何终点。
    void sigStrokeMove(QString uuid, int type, QPointF currentPos);

    // 本地笔画结束时发送 UUID、类型和最终位置。
    void sigStrokeEnd(QString uuid, int type, QPointF endPos);

    // 鼠标坐标变化，用于状态栏显示当前位置。
    void sigCursorPosChanged(QPointF pos);
};
