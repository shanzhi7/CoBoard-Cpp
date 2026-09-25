#include "paintscene.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsSceneMouseEvent>
#include <QMouseEvent>
#include <QDebug>
#include <QUuid>

PaintScene::PaintScene(QObject* parent)
    : QGraphicsScene(parent)
{
    // 光标只是交互辅助层，禁止它接收鼠标，确保事件继续落到场景。
    _eraserCursorItem = new QGraphicsEllipseItem();
    _eraserCursorItem->setPen(QPen(Qt::black, 1));
    _eraserCursorItem->setBrush(QBrush(QColor(200, 200, 200, 50)));
    _eraserCursorItem->setZValue(9999);
    _eraserCursorItem->setAcceptedMouseButtons(Qt::NoButton);
    _eraserCursorItem->setAcceptHoverEvents(false);
    _eraserCursorItem->hide();
    addItem(_eraserCursorItem);

    // 默认工具必须从工厂创建，避免 PaintScene 内部再维护一套类型分支。
    _currentTool = DrawToolFactory::create(_currShapeType);
}

void PaintScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    // 只允许拥有编辑权限的左键事件创建本地操作。
    if (!_editable)
    {
        return;
    }

    if (event->button() != Qt::LeftButton)
    {
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    // 如果上一个操作仍未结束，不覆盖它，避免两个本地 UUID 共用一个状态。
    if (_currentOperation && _currentOperation->isStarted())
    {
        return;
    }

    // 未注册的工具不创建图元，也不发送无法被远端解释的协议消息。
    if (!_currentTool)
    {
        return;
    }

    const QPointF start_pos = event->scenePos();
    _currUuid = QUuid::createUuid().toString();
    _currentOperation = DrawToolFactory::create(_currShapeType);
    if (!_currentOperation)
    {
        _currUuid.clear();
        return;
    }

    // 操作对象负责具体 QGraphicsItem 的创建和初始样式设置。
    const DrawStyle style{_penColor, _penWidth, static_cast<int>(Qt::SolidLine)};
    _currentOperation->beginLocal(this, start_pos, style);

    // 网络层继续使用原有信号，PaintScene 不直接依赖 TCP 发送逻辑。
    emit sigStrokeStart(_currUuid,
                        static_cast<int>(_currShapeType),
                        start_pos,
                        _penColor,
                        _penWidth);
}

void PaintScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    // 坐标信号与编辑权限无关，离线和只读画布都需要更新状态栏。
    emit sigCursorPosChanged(event->scenePos());

    if (!_editable)
    {
        return;
    }

    // 光标显示由工厂提供的工具属性决定，不再按 ShapeType 写分支。
    if (DrawToolFactory::usesCursorOverlay(_currShapeType))
    {
        updateEraserCursor(event->scenePos());
    }

    // 没有按住左键或当前没有活动操作时，只更新辅助光标。
    if (!(event->buttons() & Qt::LeftButton) ||
        !_currentOperation ||
        !_currentOperation->isStarted())
    {
        return;
    }

    // 策略内部决定是否因距离过小而忽略这次移动。
    if (_currentOperation->moveLocal(event->scenePos()))
    {
        emit sigStrokeMove(_currUuid,
                           static_cast<int>(_currShapeType),
                           event->scenePos());
    }
}

void PaintScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    // 非左键释放和只读场景都不应结束本地绘制。
    if (event->button() != Qt::LeftButton || !_editable)
    {
        return;
    }

    if (!_currentOperation || !_currentOperation->isStarted())
    {
        return;
    }

    finishCurrentOperation(event->scenePos());
}

void PaintScene::finishCurrentOperation(const QPointF& end_pos)
{
    // 先让策略写入最后一点，再记录图元，确保撤销拿到最终几何状态。
    const QString item_id = _currUuid;
    const ShapeType shape = _currShapeType;
    const std::shared_ptr<IDrawTool> operation = _currentOperation;
    operation->endLocal(end_pos);

    recordFinishedLocalItem(item_id, operation);
    emit sigStrokeEnd(item_id, static_cast<int>(shape), end_pos);
    clearCurrentOperation();
}

void PaintScene::clearCurrentOperation()
{
    // 操作对象由撤销栈持有到撤销完成；这里仅释放当前活动引用。
    _currentOperation.reset();
    _currUuid.clear();
}

void PaintScene::setPenColor(const QColor& color)
{
    // 样式只影响之后创建的操作，已经完成的图元保持原有颜色。
    _penColor = color;
}

void PaintScene::setPenWidth(int width)
{
    // 防止负数或零宽度传入 QPen，沿用工具栏的整数配置。
    _penWidth = qMax(1, width);
}

void PaintScene::setShapeType(ShapeType type)
{
    // 切换工具前结束当前操作，保证已发送 START 的笔画最终收到 END。
    if (_currentOperation && _currentOperation->isStarted())
    {
        finishCurrentOperation(_currentOperation->currentPosition());
    }

    _currShapeType = type;
    _currentTool = DrawToolFactory::create(type);

    // 橡皮擦光标的显示完全由工厂属性控制。
    if (DrawToolFactory::usesCursorOverlay(type) && _editable)
    {
        _eraserCursorItem->show();
    }
    else
    {
        _eraserCursorItem->hide();
    }
}

void PaintScene::setEditable(bool editable)
{
    // 只读时隐藏交互光标，避免用户误以为仍可绘制。
    _editable = editable;
    if (!_editable)
    {
        hideEraserCursor();
    }
}

bool PaintScene::isEditable() const
{
    return _editable;
}

QColor PaintScene::getPenColor()
{
    return _penColor;
}

int PaintScene::getPenWidth()
{
    return _penWidth;
}

void PaintScene::hideEraserCursor()
{
    // 光标不是绘画内容，隐藏它不会影响本地或远端操作。
    if (_eraserCursorItem)
    {
        _eraserCursorItem->hide();
    }
}

void PaintScene::updateEraserCursor(const QPointF& pos)
{
    if (!_eraserCursorItem)
    {
        return;
    }

    // 橡皮擦显示范围与实际擦除线宽保持一致。
    const int eraser_width = _penWidth * 3;
    const qreal radius = eraser_width / 2.0;
    _eraserCursorItem->setRect(pos.x() - radius,
                                pos.y() - radius,
                                eraser_width,
                                eraser_width);
    _eraserCursorItem->show();
}

bool PaintScene::canUndoLocal() const
{
    // 远端操作不进入本地撤销栈，避免删除其他用户的图元。
    return !_localUndoStack.isEmpty();
}

void PaintScene::undoLastLocalItem()
{
    // 逐条跳过已被 resetScene 清理的失效记录，保证撤销不会解引用悬空图元。
    while (!_localUndoStack.isEmpty())
    {
        const DrawItemRecord record = _localUndoStack.pop();
        _localItems.remove(record.item_id);

        if (!record.operation)
        {
            continue;
        }

        QGraphicsItem* item = record.operation->item();
        if (!item || item->scene() != this)
        {
            continue;
        }

        // removeItem 不负责 delete，场景所有权移除后必须显式释放图元。
        removeItem(item);
        delete item;
        return;
    }
}

void PaintScene::recordFinishedLocalItem(const QString& item_id,
                                         const std::shared_ptr<IDrawTool>& operation)
{
    // 没有 ID 或图元的操作不能参与撤销，否则会污染撤销栈。
    if (item_id.isEmpty() ||
        !operation ||
        !operation->item())
    {
        return;
    }

    _localItems.insert(item_id, operation->item());
    _localUndoStack.push(DrawItemRecord{item_id, operation->shapeType(), operation});
}

void PaintScene::applyRemoteDraw(const message::DrawReq& request)
{
    // 远端协议中的 item_id 是同一笔画 START/MOVE/END 的关联键。
    const QString item_id = QString::fromStdString(request.item_id());
    const ShapeType shape = static_cast<ShapeType>(request.shape());

    if (request.cmd() == message::CMD_START)
    {
        // 工厂不支持的类型直接丢弃，避免向远端未知图元写入半成品状态。
        std::shared_ptr<IDrawTool> strategy = DrawToolFactory::create(shape);
        if (!strategy)
        {
            qWarning() << "[PaintScene] Ignore unknown remote shape:" << request.shape();
            return;
        }

        // 重复 START 必须先移除旧图元，防止同一个 UUID 同时显示两个对象。
        auto old_iterator = _remoteItems.find(item_id);
        if (old_iterator != _remoteItems.end() && old_iterator->operation)
        {
            QGraphicsItem* old_item = old_iterator->operation->item();
            if (old_item && old_item->scene() == this)
            {
                removeItem(old_item);
                delete old_item;
            }
            _remoteItems.erase(old_iterator);
        }
        const std::shared_ptr<IDrawTool> operation = strategy;
        if (!operation)
        {
            return;
        }

        operation->beginRemote(this, request);
        _remoteItems.insert(item_id, RemoteItem{shape, operation});
        return;
    }

    // MOVE/END 没有对应 START 时无法恢复完整图形，沿用原逻辑直接忽略。
    auto iterator = _remoteItems.find(item_id);
    if (iterator == _remoteItems.end() ||
        !iterator->operation ||
        iterator->shape != shape)
    {
        return;
    }

    iterator->operation->updateRemote(request);
}

void PaintScene::resetScene()
{
    // 先释放所有保存图元裸指针的操作对象，再让 QGraphicsScene 删除图元。
    _currentOperation.reset();
    _remoteItems.clear();
    _localUndoStack.clear();
    _localItems.clear();
    _currUuid.clear();

    // 橡皮擦光标属于场景，clear() 会一起删除，因此先摘出并在末尾恢复。
    QGraphicsEllipseItem* cursor = _eraserCursorItem;
    if (cursor)
    {
        removeItem(cursor);
    }

    clear();
    _eraserCursorItem = nullptr;

    // 恢复交互光标的固定属性，保证下一次进入房间或离线画板仍可使用。
    if (cursor)
    {
        _eraserCursorItem = cursor;
        addItem(_eraserCursorItem);
        _eraserCursorItem->setZValue(9999);
        _eraserCursorItem->setAcceptedMouseButtons(Qt::NoButton);
        _eraserCursorItem->setAcceptHoverEvents(false);
        _eraserCursorItem->hide();
    }
}
