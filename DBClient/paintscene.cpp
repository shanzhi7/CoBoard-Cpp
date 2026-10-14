#include "paintscene.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <QSignalBlocker>
#include <QDebug>
#include <QUuid>
#include <QTransform>

#include "canvasitemserializer.h"
#include "canvasitems/imageitem.h"

bool PaintScene::BuildCanvasDocument(CanvasDocument* document, QString* error_message) const
{
    // 1. 文档只记录画布范围和纯色背景，不读取视图状态或选中状态。
    CanvasDocument snapshot;
    snapshot._canvas_size = QSize(qRound(sceneRect().width()), qRound(sceneRect().height()));
    snapshot._background = backgroundBrush().color();
    if (snapshot._canvas_size.isEmpty() || backgroundBrush().style() != Qt::SolidPattern)
    {
        *error_message = QStringLiteral("画布尺寸或背景不支持保存");
        return false;
    }

    // 2. Qt 返回真实堆叠顺序，辅助光标不是 CanvasItem，自然排除。
    for (QGraphicsItem* graphics_item : items(Qt::AscendingOrder))
    {
        auto* item = dynamic_cast<CanvasItem*>(graphics_item);
        if (!item) continue;
        CanvasItemData item_data;
        if (!CanvasItemSerializer::SerializeItem(item, &item_data, &snapshot, error_message)) return false;
        snapshot._items.append(item_data);
        snapshot._item_order.append(item->itemId());
        if (snapshot._items.size() > CanvasDocumentLimits::MAX_ITEMS)
        {
            *error_message = QStringLiteral("画布图元超过 10000 个");
            return false;
        }
    }
    *document = snapshot;
    return true;
}

bool PaintScene::LoadCanvasDocument(const CanvasDocument& document, QString* error_message)
{
    // 1. 此入口只构建临时场景，失败后由调用者整体销毁，不清空当前画布。
    if (!views().isEmpty() || items().size() != 1 || document._items.size() != document._item_order.size())
    {
        *error_message = QStringLiteral("请在新的临时场景恢复画布");
        return false;
    }
    QMap<QString, CanvasItemData> item_dataMap;
    for (const CanvasItemData& item_data : document._items)
    {
        const QString item_id = item_data.value("item_id").toString();
        if (item_id.isEmpty() || item_dataMap.contains(item_id))
        {
            *error_message = QStringLiteral("画布图元 ID 重复或为空");
            return false;
        }
        item_dataMap.insert(item_id, item_data);
    }

    // 2. 依次加入图元，保留原始层级和同层插入顺序，并重建图片索引。
    QSet<QString> loaded_ids;
    for (const QString& item_id : document._item_order)
    {
        if (!item_dataMap.contains(item_id) || loaded_ids.contains(item_id))
        {
            *error_message = QStringLiteral("画布图元顺序无效");
            return false;
        }
        CanvasItem* item = CanvasItemSerializer::DeserializeItem(item_dataMap.value(item_id), document._asset_dataMap, error_message);
        if (!item) return false;
        addItem(item);
        loaded_ids.insert(item_id);
        if (auto* image_item = dynamic_cast<ImageItem*>(item))
        {
            _imageItems.insert(item_id, image_item);
            UpdateImageInteraction(image_item);
            connect(image_item, &CanvasItem::sigGeometryChanged, this, &PaintScene::sigImageGeometryChanged);
            connect(image_item, &ImageItem::sigRetryRequested, this, &PaintScene::sigImageRetryRequested);
            connect(image_item, &ImageItem::sigPreviewRequested, this, &PaintScene::sigImagePreviewRequested);
        }
        if (auto* text_item = dynamic_cast<TextItem*>(item)) ConnectTextItem(text_item);
    }

    // 3. 导入图元是初始内容，不加入撤销栈；后续本地操作继续沿用原有记录。
    setSceneRect(QRectF(QPointF(), document._canvas_size));
    setBackgroundBrush(document._background);
    clearSelection();
    _localUndoStack.clear();
    return true;
}

QImage PaintScene::RenderCanvasImage(QString* error_message)
{
    // 1. 导出整个场景范围，像素尺寸不依赖视口和缩放，并拒绝图片占位状态。
    if (!FinishTextEditing())
    {
        *error_message = QStringLiteral("请先完成有效的文字编辑");
        return QImage();
    }
    const QSize size(qRound(sceneRect().width()), qRound(sceneRect().height()));
    if (size.isEmpty() || static_cast<qint64>(size.width()) * size.height() > CanvasDocumentLimits::MAX_IMAGE_PIXELS)
    {
        *error_message = QStringLiteral("画布尺寸过大，图片导出最多支持 6400 万像素");
        return QImage();
    }
    for (QGraphicsItem* item : items())
    {
        auto* image_item = dynamic_cast<ImageItem*>(item);
        if (image_item && (image_item->loadState() != ImageItem::ImageLoadState::Ready || image_item->pixmap().isNull()))
        {
            *error_message = QStringLiteral("图片尚未就绪，请等待或重新加载后再导出");
            return QImage();
        }
    }
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
    {
        *error_message = QStringLiteral("无法分配导出图片内存");
        return image;
    }
    image.fill(Qt::transparent);

    // 2. 隐藏选中框和橡皮擦光标，渲染完成后立即恢复原状态。
    const QSignalBlocker signal_blocker(this);
    const QList<QGraphicsItem*> selected_items = selectedItems();
    const bool is_cursor_visible = _eraserCursorItem->isVisible();
    clearSelection();
    _eraserCursorItem->hide();
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    render(&painter, QRectF(QPointF(), size), sceneRect(), Qt::IgnoreAspectRatio);
    painter.end();
    for (QGraphicsItem* item : selected_items) item->setSelected(true);
    _eraserCursorItem->setVisible(is_cursor_visible);
    return image;
}

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
    // 1. 手形的鼠标序列由视图消费；场景也拒绝旁路事件，避免图元误移动。
    if (_interaction_mode == CanvasInteractionMode::Pan)
    {
        event->accept();
        return;
    }

    // 2. 命中子编辑器时回溯父图元；空白点击先提交原草稿，再按文字工具新建。
    QGraphicsItem* hit_item = itemAt(event->scenePos(), QTransform());
    while (hit_item && !dynamic_cast<TextItem*>(hit_item)) hit_item = hit_item->parentItem();
    auto* text_item = dynamic_cast<TextItem*>(hit_item);
    if (_editing_text_item && text_item != _editing_text_item && !FinishTextEditing()) return;
    if (_interaction_mode == CanvasInteractionMode::Text && event->button() == Qt::LeftButton)
    {
        if (!_editable) return;
        if (!text_item)
        {
            TextItemData data{QUuid::createUuid().toString(QUuid::WithoutBraces), QString(), _text_style, event->scenePos()};
            text_item = AddTextItem(data);
        }
        if (text_item && !_editing_text_item) BeginTextEditing(text_item);
        if (text_item && text_item->IsEditing()) QGraphicsScene::mousePressEvent(event);
        event->accept();
        return;
    }

    // 3. 选择和 Ctrl 多选交给 Qt；不可移动的只读图片仍能正常选择。
    if (_interaction_mode == CanvasInteractionMode::Select)
    {
        QGraphicsScene::mousePressEvent(event);
        _text_drag_positionMap.clear();
        for (QGraphicsItem* item : selectedItems())
        {
            auto* selected_text = dynamic_cast<TextItem*>(item);
            if (selected_text && selected_text->flags().testFlag(QGraphicsItem::ItemIsMovable))
                _text_drag_positionMap.insert(selected_text->itemId(), selected_text->pos());
        }
        if (event->button() == Qt::LeftButton)
        {
            PrepareImageDrag();
        }
        return;
    }

    // 4. 绘图模式不调用图片默认事件；在图片上按下也只创建当前绘图操作。
    if (event->button() != Qt::LeftButton || !_editable ||
        (_currentOperation && _currentOperation->isStarted()))
    {
        event->accept();
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

    // 5. 沿用图元 ID、样式和 START 信号，不把交互模式写入绘画协议。
    const DrawStyle style{_penColor, _penWidth, static_cast<int>(Qt::SolidLine)};
    _currentOperation->beginLocal(this, _currUuid, start_pos, style);

    // 网络层继续使用原有信号，PaintScene 不直接依赖 TCP 发送逻辑。
    emit sigStrokeStart(_currUuid,
                        static_cast<int>(_currShapeType),
                        start_pos,
                        _penColor,
                        _penWidth);
    event->accept();
}

void PaintScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    // 1. 坐标显示与编辑权限无关；手形模式由视图另行转发坐标。
    emit sigCursorPosChanged(event->scenePos());

    // 2. 图片移动完全沿用 Qt 默认多选拖动，实时几何信号继续进入同步层。
    if (_interaction_mode == CanvasInteractionMode::Select || _interaction_mode == CanvasInteractionMode::Text)
    {
        QGraphicsScene::mouseMoveEvent(event);
        return;
    }

    if (_interaction_mode != CanvasInteractionMode::Draw || !_editable)
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
    // 1. Qt 先结束图片鼠标抓取，再为实际移动的图片补发最终状态。
    if (_interaction_mode == CanvasInteractionMode::Select || _interaction_mode == CanvasInteractionMode::Text)
    {
        QGraphicsScene::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton)
        {
            FinishImageDrag(_editable);
            const auto positionMap = _text_drag_positionMap;
            _text_drag_positionMap.clear();
            for (auto iterator = positionMap.cbegin(); iterator != positionMap.cend(); ++iterator)
            {
                TextItem* item = FindTextItem(iterator.key());
                if (_editable && item && item->pos() != iterator.value()) emit SigTextMoved(iterator.key());
            }
        }
        return;
    }
    if (_interaction_mode == CanvasInteractionMode::Pan)
    {
        event->accept();
        return;
    }

    // 2. 绘图模式只结束具有编辑权限的左键笔画。
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

void PaintScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    // 1. 成功图片的预览和失败图片的重试仅在鼠标模式向图元分发。
    if (_interaction_mode == CanvasInteractionMode::Select)
    {
        QGraphicsItem* hit_item = itemAt(event->scenePos(), QTransform());
        while (hit_item && !dynamic_cast<TextItem*>(hit_item)) hit_item = hit_item->parentItem();
        if (auto* text_item = dynamic_cast<TextItem*>(hit_item))
        {
            BeginTextEditing(text_item);
            event->accept();
            return;
        }
        QGraphicsScene::mouseDoubleClickEvent(event);
        PrepareImageDrag();
        return;
    }

    // 2. 绘图双击沿用按下逻辑，手形事件仍由视图消费。
    mousePressEvent(event);
}

void PaintScene::keyPressEvent(QKeyEvent* event)
{
    if (!event)
    {
        return;
    }

    // 1. 活动文本优先接收字符、粘贴和文本撤销，不能进入画布删除逻辑。
    if (IsTextEditing())
    {
        QGraphicsScene::keyPressEvent(event);
        return;
    }
    if (event->matches(QKeySequence::Undo))
    {
        emit SigUndoRequested();
        event->accept();
        return;
    }
    if (_editable && (_interaction_mode == CanvasInteractionMode::Select || _interaction_mode == CanvasInteractionMode::Text) &&
        (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace))
    {
        const auto selected_items = selectedItems();
        for (QGraphicsItem* item : selected_items)
        {
            auto* text_item = dynamic_cast<TextItem*>(item);
            if (text_item && !_pending_text_ids.contains(text_item->itemId())) emit SigTextDeleteRequested(text_item->itemId());
        }
    }

    // 2. 禁止旧焦点图元在绘图、手形或只读状态下通过默认按键处理修改图片。
    if (_interaction_mode != CanvasInteractionMode::Select || !_editable)
    {
        event->accept();
        return;
    }

    // 3. 保留已有图片快捷键；图片批量缩放和旋转不属于本轮范围。
    ImageItem* selected_image = nullptr;
    for (QGraphicsItem* graphics_item : selectedItems())
    {
        selected_image = dynamic_cast<ImageItem*>(graphics_item);
        if (selected_image)
        {
            break;
        }
    }
    if (!selected_image)
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
    // 1. 切换绘图策略前结束当前操作，保证已发送 START 的笔画最终收到 END。
    if (_currentOperation && _currentOperation->isStarted())
    {
        finishCurrentOperation(_currentOperation->currentPosition());
    }

    _currShapeType = type;
    _currentTool = DrawToolFactory::create(type);

    // 2. 光标还需满足绘图模式和权限条件，不能在鼠标或手形模式泄漏范围提示。
    if (_interaction_mode == CanvasInteractionMode::Draw &&
        DrawToolFactory::usesCursorOverlay(type) && _editable)
    {
        _eraserCursorItem->show();
    }
    else
    {
        _eraserCursorItem->hide();
    }
}

void PaintScene::SetInteractionMode(CanvasInteractionMode mode)
{
    // 1. 切换模式前结束当前笔画和图片拖动，不能遗留跨工具的鼠标抓取。
    if (_interaction_mode != mode)
    {
        if (!FinishTextEditing()) return;
        if (_currentOperation && _currentOperation->isStarted())
        {
            finishCurrentOperation(_currentOperation->currentPosition());
        }
        FinishImageDrag(_editable);
        if (QGraphicsItem* grabber = mouseGrabberItem())
        {
            grabber->ungrabMouse();
        }
    }

    // 2. 只改变移动和焦点能力，不能用关闭选择标记的接口清掉现有选择。
    _interaction_mode = mode;
    for (TextItem* item : _text_itemMap) UpdateTextInteraction(item);
    hideEraserCursor();
    for (ImageItem* image_item : _imageItems)
    {
        UpdateImageInteraction(image_item);
    }
}

CanvasInteractionMode PaintScene::InteractionMode() const
{
    // 1. 场景模式独立于绘图类型，鼠标和手形不创建绘图 Tool。
    return _interaction_mode;
}

void PaintScene::PrepareImageDrag()
{
    // 1. 只记录 Qt 实际抓取的可编辑图片，点击空白或只读选择不产生变换提交。
    _image_drag_start_positions.clear();
    auto* grabbed_image = dynamic_cast<ImageItem*>(mouseGrabberItem());
    if (!_editable || !grabbed_image)
    {
        return;
    }

    // 2. Qt 会同时移动已选图元；用 ID 保存起点可安全应对远端删除。
    for (QGraphicsItem* item : selectedItems())
    {
        if (auto* image_item = dynamic_cast<ImageItem*>(item))
        {
            _image_drag_start_positions.insert(image_item->itemId(), image_item->pos());
        }
    }
}

void PaintScene::FinishImageDrag(bool send_final)
{
    // 1. 先移出当前记录，信号回调删除图片或切换模式时不会重复提交。
    const auto start_positions = _image_drag_start_positions;
    _image_drag_start_positions.clear();
    if (!send_final)
    {
        return;
    }

    // 2. 仅补发仍存在且实际移动的图片，未移动的点击不产生网络消息。
    for (auto iterator = start_positions.cbegin(); iterator != start_positions.cend(); ++iterator)
    {
        ImageItem* image_item = findImageItem(iterator.key());
        if (image_item && image_item->pos() != iterator.value())
        {
            emit sigImageGeometryChanged(image_item->itemId(),
                                         image_item->sceneBoundingRect(),
                                         image_item->rotation(),
                                         image_item->scale());
        }
    }
}

void PaintScene::UpdateImageInteraction(ImageItem* image_item)
{
    // 1. 选择标记始终保留，模式通过场景和视图的事件分发控制实际选择操作。
    const bool can_move = _interaction_mode == CanvasInteractionMode::Select && _editable;
    image_item->setFlag(QGraphicsItem::ItemIsMovable, can_move);
    image_item->setFlag(QGraphicsItem::ItemIsFocusable, can_move);
    if (!can_move)
    {
        // 2. 清除旧键盘焦点，保持图片选中状态供切回鼠标工具后继续操作。
        image_item->clearFocus();
    }
}

void PaintScene::setEditable(bool editable)
{
    // 1. 权限撤销先关闭后续提交，再解除 Qt 抓取；不能发出未授权的最终变换。
    _editable = editable;
    if (!_editable)
    {
        FinishTextEditing(true);
        FinishImageDrag(false);
        if (QGraphicsItem* grabber = mouseGrabberItem())
        {
            grabber->ungrabMouse();
        }
        if (_currentOperation && _currentOperation->isStarted())
        {
            recordFinishedLocalItem(_currUuid, _currentOperation);
            clearCurrentOperation();
        }
        hideEraserCursor();
    }

    // 2. 新权限同步到所有已有图片，只读成员仍可选择和预览。
    for (ImageItem* image_item : _imageItems)
    {
        UpdateImageInteraction(image_item);
    }
    for (TextItem* item : _text_itemMap) UpdateTextInteraction(item);
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
    // 1. 逐条跳过失效记录，文字仅恢复内容和样式，不恢复移动位置。
    while (!_localUndoStack.isEmpty())
    {
        const DrawItemRecord record = _localUndoStack.pop();

        if (record.is_text)
        {
            TextItem* text_item = FindTextItem(record.item_id);
            if (!text_item) continue;
            if (record.before_text._text.isEmpty()) RemoveTextItem(record.item_id);
            else
            {
                TextItemData restored = record.before_text;
                restored._position = text_item->pos();
                text_item->SetTextData(restored);
            }
            return;
        }

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

void PaintScene::ConnectTextItem(TextItem* item)
{
    // 1. 文字索引由场景拥有，导入、远端回放和本地创建统一走同一连接入口。
    if (!item) return;
    _text_itemMap.insert(item->itemId(), item);
    UpdateTextInteraction(item);
    connect(item, &TextItem::SigEditRequested, this, [this](const QString& item_id) {
        BeginTextEditing(FindTextItem(item_id));
    });
    connect(item, &TextItem::SigFinishRequested, this, [this](bool is_cancel) {
        FinishTextEditing(is_cancel);
    });
    connect(item, &TextItem::SigTextError, this, &PaintScene::SigTextError);
}

void PaintScene::UpdateTextInteraction(TextItem* item)
{
    // 1. 编辑器期间禁止移动；选择模式才允许拖动，待确认图元保持只读。
    if (!item) return;
    const bool can_move = _editable && !_pending_text_ids.contains(item->itemId()) &&
                          _interaction_mode == CanvasInteractionMode::Select && !item->IsEditing();
    item->setFlag(QGraphicsItem::ItemIsMovable, can_move);
    item->setFlag(QGraphicsItem::ItemIsFocusable, can_move || item->IsEditing());
}

TextItem* PaintScene::AddTextItem(const TextItemData& data)
{
    // 1. 新建文字只接受唯一 ID 和合法样式，空内容允许作为编辑草稿暂存。
    if (!TextItem::IsValidData(data, true) || _text_itemMap.contains(data._item_id)) return nullptr;
    auto* item = new TextItem(data._item_id);
    item->SetTextData(data);
    addItem(item);
    ConnectTextItem(item);
    return item;
}

TextItem* PaintScene::FindTextItem(const QString& item_id) const
{
    // 1. 文本 ID 只在当前场景内解析。
    return _text_itemMap.value(item_id, nullptr);
}

bool PaintScene::UpdateTextItem(const TextItemData& data)
{
    // 1. 远端规范化值直接替换内容和样式，保留公共变换之外的场景状态。
    TextItem* item = FindTextItem(data._item_id);
    if (!item || !TextItem::IsValidData(data)) return false;
    item->SetTextData(data);
    UpdateTextInteraction(item);
    return true;
}

bool PaintScene::RemoveTextItem(const QString& item_id)
{
    // 1. 删除先结束活动编辑，再移除索引和场景对象。
    TextItem* item = FindTextItem(item_id);
    if (!item) return false;
    if (_editing_text_item == item)
    {
        item->CancelEditing();
        _editing_text_item.clear();
    }
    _text_itemMap.remove(item_id);
    _pending_text_ids.remove(item_id);
    InvalidateTextUndo(item_id);
    removeItem(item);
    // 2. Esc 和空内容完成可能来自子编辑器的事件回调，事件返回后再销毁。
    item->setParent(this);
    item->deleteLater();
    return true;
}

bool PaintScene::BeginTextEditing(TextItem* item)
{
    // 1. 同时只允许一个编辑器，并保存位置独立的内容样式快照。
    if (!_editable || !item || _pending_text_ids.contains(item->itemId())) return false;
    if (_editing_text_item && _editing_text_item != item && !FinishTextEditing()) return false;
    if (!_editing_text_item)
    {
        _editing_text_item = item;
        _text_before_edit = item->GetTextData();
        item->BeginEditing();
        UpdateTextInteraction(item);
        emit SigTextEditingChanged();
    }
    return item->IsEditing();
}

bool PaintScene::FinishTextEditing(bool is_cancel)
{
    // 1. 取消恢复原快照；完成则校验、删除空新建或发出一次完整提交。
    TextItem* item = _editing_text_item;
    if (!item) return true;
    const TextItemData before = _text_before_edit;
    if (is_cancel)
    {
        item->CancelEditing();
        _editing_text_item.clear();
        if (before._text.isEmpty()) RemoveTextItem(before._item_id);
        else UpdateTextInteraction(item);
        emit SigTextEditingChanged();
        return true;
    }
    if (!item->CommitEditing()) return false;
    const TextItemData after = item->GetTextData();
    _editing_text_item.clear();
    UpdateTextInteraction(item);
    if (after._text.trimmed().isEmpty())
    {
        const QString item_id = after._item_id;
        RemoveTextItem(item_id);
        if (!before._text.isEmpty()) emit SigTextDeleteRequested(item_id);
        emit SigTextEditingChanged();
        return true;
    }
    if (_is_text_undo_enabled && (before._text != after._text || !(before._style == after._style)))
        RecordTextOperation(before, after);
    if (before._text != after._text || !(before._style == after._style)) emit SigTextCommitted(before, after);
    emit SigTextEditingChanged();
    return true;
}

bool PaintScene::IsTextEditing() const
{
    // 1. 活动引用由 QPointer 自动随图元删除失效。
    return _editing_text_item && _editing_text_item->IsEditing();
}

TextItem* PaintScene::ActiveTextItem() const
{
    // 1. 编辑优先，否则返回当前唯一选中文字。
    if (_editing_text_item) return _editing_text_item;
    const QList<QGraphicsItem*> selected_items = selectedItems();
    return selected_items.size() == 1 ? dynamic_cast<TextItem*>(selected_items.first()) : nullptr;
}

void PaintScene::SetTextStyle(const TextStyle& style)
{
    // 1. 有目标时立即应用样式，没有目标时仅更新新建默认值。
    _text_style = style;
    if (TextItem* item = ActiveTextItem())
    {
        if (!_pending_text_ids.contains(item->itemId())) item->SetTextStyle(style);
    }
}

void PaintScene::SetTextUndoEnabled(bool enabled)
{
    // 1. 在线操作等待服务端确认后再登记，离线可在完成编辑时即时登记。
    _is_text_undo_enabled = enabled;
}

void PaintScene::SetTextPending(const QString& item_id, bool is_pending)
{
    // 1. 每个文字独立等待确认，不阻塞其他图元。
    if (is_pending) _pending_text_ids.insert(item_id);
    else _pending_text_ids.remove(item_id);
    UpdateTextInteraction(FindTextItem(item_id));
}

void PaintScene::RecordTextOperation(const TextItemData& before, const TextItemData& after)
{
    // 1. 内容和样式使用一条快照记录，位置保持为撤销前后当前值。
    DrawItemRecord record;
    record.item_id = after._item_id;
    record.is_text = true;
    record.before_text = before;
    record.after_text = after;
    _localUndoStack.push(record);
}

void PaintScene::InvalidateTextUndo(const QString& item_id)
{
    // 1. 远端修改后移除同一文字的本地快照，避免覆盖新版本。
    QStack<DrawItemRecord> retained;
    while (!_localUndoStack.isEmpty())
    {
        const DrawItemRecord record = _localUndoStack.pop();
        if (!record.is_text || record.item_id != item_id) retained.push(record);
    }
    while (!retained.isEmpty()) _localUndoStack.push(retained.pop());
}

bool PaintScene::TakeTextUndoOperation(TextItemData* before, bool* is_create)
{
    // 1. 从顶部寻找文字记录，保留其他图元顺序以兼容现有撤销入口。
    if (!before || !is_create) return false;
    for (int index = _localUndoStack.size() - 1; index >= 0; --index)
    {
        const DrawItemRecord record = _localUndoStack.at(index);
        if (!record.is_text || !FindTextItem(record.item_id) || _pending_text_ids.contains(record.item_id)) continue;
        _localUndoStack.removeAt(index);
        *before = record.before_text;
        *is_create = record.before_text._text.isEmpty();
        return true;
    }
    return false;
}

bool PaintScene::CanUndoLocalOperation() const
{
    // 1. 统一入口与旧的 canUndoLocal 保持一致。
    return !_localUndoStack.isEmpty();
}

void PaintScene::UndoLastLocalOperation()
{
    // 1. 复用原有栈，文字修改恢复内容样式并保留当前位置。
    undoLastLocalItem();
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
    // 1. 创建入口供本地导入和远端回放复用，稳定 ID 是索引及撤销记录的前提。
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
    // 2. 新图片按当前模式配置交互，不改变工具或已有选择。
    UpdateImageInteraction(image_item);
    connect(image_item, &CanvasItem::sigGeometryChanged,
            this, &PaintScene::sigImageGeometryChanged);
    // 失败占位图支持双击重试；信号对信号转发，PaintScene 不接触网络层，重试仍由 Canvas 统一排队。
    connect(image_item, &ImageItem::sigRetryRequested,
            this, &PaintScene::sigImageRetryRequested);
    // 图片图元只报告预览意图，预览窗口由 Canvas 管理，避免场景层承担窗口生命周期。
    connect(image_item, &ImageItem::sigPreviewRequested,
            this, &PaintScene::sigImagePreviewRequested);

    // 3. 保留资源、预览和离线撤销链路，选择模式不改变图元创建协议。
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
    // 1. 按 ID 删除拖动记录，远端删除和本地删除都不会遗留活动图片指针。
    auto iterator = _imageItems.find(item_id);
    if (iterator == _imageItems.end() || !iterator.value())
    {
        return false;
    }

    ImageItem* image_item = iterator.value();
    _imageItems.erase(iterator);
    _image_drag_start_positions.remove(item_id);
    // 2. 仅清理对应撤销条目，保留其他图元的创建顺序。
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

    // 3. 场景不再拥有图片时由本入口显式销毁，Qt 自动解除该图元的鼠标抓取。
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
    // 1. 先清理操作、文字编辑引用和撤销记录，再让场景删除图元。
    _currentOperation.reset();
    _remoteItems.clear();
    _localUndoStack.clear();
    _localItems.clear();
    _imageItems.clear();
    _text_itemMap.clear();
    _editing_text_item.clear();
    _pending_text_ids.clear();
    _text_drag_positionMap.clear();
    _currUuid.clear();
    _image_drag_start_positions.clear();

    // 2. 橡皮擦光标属于场景，clear() 会一起删除，因此先摘出并在末尾恢复。
    QGraphicsEllipseItem* cursor = _eraserCursorItem;
    if (cursor)
    {
        removeItem(cursor);
    }

    clear();
    _eraserCursorItem = nullptr;

    // 3. 恢复辅助光标固定属性，保留当前工具供下次进入画布继续使用。
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
