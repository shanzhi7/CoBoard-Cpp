#include "paintscene.h"
#include "canvasitems/imageitem.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDebug>
#include <QUuid>
#include <QTransform>

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
    if (event->button() != Qt::LeftButton)
    {
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    // 当前笔画尚未结束时不切换到图片拖动，避免同一鼠标序列同时持有两种交互状态。
    if (_currentOperation && _currentOperation->isStarted())
    {
        return;
    }

    // 图片图元需要保留 QGraphicsScene 的选择和拖动机制；不能把点击图片误当作新画笔的起点。
    QGraphicsItem* hit_item = itemAt(event->scenePos(), QTransform());
    auto* image_item = dynamic_cast<ImageItem*>(hit_item);
    if (image_item)
    {
        if (!_editable)
        {
            // 只读场景不交给 QGraphicsScene 处理拖动，但仍保持与可编辑场景一致的单选/Ctrl 多选语义。
            const bool append_selection = event->modifiers() & Qt::ControlModifier;
            if (!append_selection)
            {
                clearSelection();
            }
            image_item->setSelected(append_selection
                                        ? !image_item->isSelected()
                                        : true);
            event->accept();
            return;
        }

        // 可编辑场景交给 QGraphicsScene 处理标准选择规则：普通点击单选，Ctrl 点击追加或取消选择。
        _activeImageItem = image_item;
        setFocus(Qt::MouseFocusReason);
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    // 只允许拥有编辑权限的左键事件创建本地操作。
    if (!_editable)
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

    // 拖动图片时由 Qt 负责更新位置，CanvasItem 的几何信号会把最终状态交给同步层。
    if (_activeImageItem)
    {
        QGraphicsScene::mouseMoveEvent(event);
        return;
    }

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
    if (_activeImageItem)
    {
        QGraphicsScene::mouseReleaseEvent(event);
        _activeImageItem = nullptr;
        return;
    }

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

void PaintScene::keyPressEvent(QKeyEvent* event)
{
    if (!event)
    {
        return;
    }

    ImageItem* selected_image = nullptr;
    for (QGraphicsItem* graphics_item : selectedItems())
    {
        selected_image = dynamic_cast<ImageItem*>(graphics_item);
        if (selected_image)
        {
            break;
        }
    }
    if (!selected_image || !_editable)
    {
        QGraphicsScene::keyPressEvent(event);
        return;
    }

    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        // 删除只传递稳定 ID，资源引用和本地缓存不进入房间操作正文。
        emit sigImageDeleteRequested(selected_image->itemId());
        event->accept();
        return;
    }

    const qreal current_scale_x = selected_image->transform().m11();
    const qreal current_scale_y = selected_image->transform().m22();
    if ((event->modifiers() & Qt::ControlModifier) &&
        (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal ||
         event->key() == Qt::Key_Minus))
    {
        // 缩放使用固定步长并限制范围，连续按键不会产生服务端拒绝的极端变换。
        const qreal factor = event->key() == Qt::Key_Minus ? 1.0 / 1.1 : 1.1;
        const qreal next_scale_x = qBound(0.1, current_scale_x * factor, 8.0);
        const qreal next_scale_y = qBound(0.1, current_scale_y * factor, 8.0);
        selected_image->setTransform(QTransform::fromScale(next_scale_x, next_scale_y), false);
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_BracketLeft || event->key() == Qt::Key_BracketRight)
    {
        // 方括号以五度步长旋转图片，几何信号会把变换同步给房间。
        const qreal delta = event->key() == Qt::Key_BracketLeft ? -5.0 : 5.0;
        selected_image->setRotation(selected_image->rotation() + delta);
        event->accept();
        return;
    }

    QGraphicsScene::keyPressEvent(event);
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

        if (record.is_image)
        {
            // 图片记录直接按 item_id 从索引删除；不发送网络删除操作，离线撤销只影响本地画布。
            if (record.image_item && record.image_item->scene() == this)
            {
                _imageItems.remove(record.item_id);
                removeItem(record.image_item);
                delete record.image_item;
                return;
            }
            continue;
        }

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
    _localUndoStack.push(DrawItemRecord{item_id,
                                        operation->shapeType(),
                                        operation,
                                        nullptr,
                                        false});
}

ImageItem* PaintScene::addImageItem(const QString& item_id,
                                    const QString& asset_id,
                                    const QString& asset_ref,
                                    const QString& asset_sha256,
                                    const QString& mime_type,
                                    const QSize& original_size,
                                    const QPixmap& pixmap,
                                    const QPointF& scene_pos,
                                    const QSizeF& display_size,
                                    bool record_undo)
{
    if (item_id.isEmpty())
    {
        // 没有稳定 ID 或有效像素的图片无法参与本地撤销和远程历史，直接拒绝创建。
        return nullptr;
    }

    // 历史回放可能重复收到同一个创建操作，先移除旧图元可以避免同一 ID 显示两份内容。
    if (_imageItems.contains(item_id))
    {
        removeImageItemInternal(item_id, true);
    }

    auto* image_item = new ImageItem(item_id);
    image_item->setAssetMetadata(asset_id, asset_ref, asset_sha256, mime_type);
    image_item->setOriginalSize(original_size);
    if (!pixmap.isNull())
    {
        image_item->setPixmap(pixmap);
    }
    if (display_size.isValid() && !display_size.isEmpty())
    {
        image_item->setDisplaySize(display_size);
    }
    image_item->setPos(scene_pos);
    addItem(image_item);
    _imageItems.insert(item_id, image_item);
    connect(image_item, &CanvasItem::sigGeometryChanged,
            this, &PaintScene::sigImageGeometryChanged);
    // 失败占位图支持双击重试；信号对信号转发，PaintScene 不接触网络层，重试仍由 Canvas 统一排队。
    connect(image_item, &ImageItem::sigRetryRequested,
            this, &PaintScene::sigImageRetryRequested);
    // 图片图元只报告预览意图，预览窗口由 Canvas 管理，避免场景层承担窗口生命周期。
    connect(image_item, &ImageItem::sigPreviewRequested,
            this, &PaintScene::sigImagePreviewRequested);

    if (record_undo)
    {
        // 离线导入加入与传统图元相同的撤销栈；远程历史通过 false 避免污染本地操作记录。
        _localUndoStack.push(DrawItemRecord{item_id,
                                            Shape_Unknown,
                                            nullptr,
                                            image_item,
                                            true});
    }

    // Canvas 可以在这里接入在线上传和 ImageOperation 序列化，PaintScene 不直接依赖网络层。
    emit sigImageInserted(item_id,
                          asset_id,
                          asset_ref,
                          asset_sha256,
                          mime_type,
                          original_size,
                          scene_pos,
                          image_item->displaySize());
    return image_item;
}

bool PaintScene::removeImageItem(const QString& item_id)
{
    // 公共删除默认移除本地撤销记录，防止用户删除后又通过撤销恢复一份已不存在的图元。
    return removeImageItemInternal(item_id, true);
}

ImageItem* PaintScene::findImageItem(const QString& item_id) const
{
    // QHash 查找不会遍历场景图元，历史回放和资源下载完成时可安全地按 ID 定位。
    return _imageItems.value(item_id, nullptr);
}

bool PaintScene::updateImageTransform(const QString& item_id,
                                      const QPointF& scene_pos,
                                      const QSizeF& display_size,
                                      qreal scale_x,
                                      qreal scale_y,
                                      qreal rotation)
{
    ImageItem* image_item = findImageItem(item_id);
    if (!image_item || !display_size.isValid() || display_size.isEmpty() ||
        scale_x <= 0.0 || scale_y <= 0.0)
    {
        // 远端变换数据必须先经过调用方的协议范围检查；这里仍拒绝会产生无效包围盒的值。
        return false;
    }

    // 先更新局部尺寸，再设置位置和变换矩阵，确保最终 boundingRect 与场景坐标保持一致。
    image_item->setDisplaySize(display_size);
    image_item->setPos(scene_pos);
    image_item->setScale(1.0);
    image_item->setTransform(QTransform::fromScale(scale_x, scale_y), false);
    image_item->setRotation(rotation);
    return true;
}

bool PaintScene::removeImageItemInternal(const QString& item_id, bool remove_undo_record)
{
    auto iterator = _imageItems.find(item_id);
    if (iterator == _imageItems.end() || !iterator.value())
    {
        return false;
    }

    ImageItem* image_item = iterator.value();
    _imageItems.erase(iterator);
    if (_activeImageItem == image_item)
    {
        _activeImageItem = nullptr;
    }
    if (remove_undo_record)
    {
        // 只删除对应记录，保留其他图片和传统图元的撤销顺序。
        for (int index = _localUndoStack.size() - 1; index >= 0; --index)
        {
            if (_localUndoStack[index].is_image &&
                _localUndoStack[index].item_id == item_id)
            {
                _localUndoStack.removeAt(index);
                break;
            }
        }
    }

    if (image_item->scene() == this)
    {
        removeItem(image_item);
    }
    delete image_item;
    return true;
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
    _imageItems.clear();
    _currUuid.clear();
    _activeImageItem = nullptr;

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
