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
#include <QtAlgorithms>

#include "canvasitemserializer.h"
#include "canvasselectioncontroller.h"
#include "canvasitems/imageitem.h"

PaintScene::~PaintScene()
{
    // 1. 在 QGraphicsScene 基类释放图元前回收辅助覆盖层，避免 QObject 析构顺序造成悬空。
    delete _selection_controller;
    _selection_controller = nullptr;
}

CanvasItem* PaintScene::FindCanvasItem(const QString& item_id) const
{
    // 1. 弱引用索引覆盖本地、远端和文件图元，删除后不会返回悬空对象。
    const QPointer<CanvasItem> cached = _canvas_itemMap.value(item_id);
    if (cached && cached->scene() == this)
    {
        return cached;
    }
    _canvas_itemMap.remove(item_id);
    for (QGraphicsItem* graphics_item : items())
    {
        auto* item = dynamic_cast<CanvasItem*>(graphics_item);
        if (item && !item->parentItem() && item->itemId() == item_id)
        {
            _canvas_itemMap.insert(item_id, item);
            return item;
        }
    }
    return nullptr;
}

CanvasItemEditState PaintScene::CaptureItemEditState(const QString& item_id) const
{
    // 1. 快照只包含内容，选中框和当前工具不参与网络或撤销。
    return CanvasItemEdit::Capture(FindCanvasItem(item_id));
}

bool PaintScene::ApplyItemEditState(const CanvasItemEditState& state)
{
    // 1. 应用入口不发起本地提交，网络回放不会形成反馈循环。
    const bool applied = CanvasItemEdit::Apply(FindCanvasItem(state._item_id), state);
    if (_selection_controller)
    {
        _selection_controller->Refresh();
    }
    return applied;
}

void PaintScene::CancelItemInteraction(const QString& item_id)
{
    // 1. 远端覆盖时先停止本地手势和草稿，再由调用者应用权威状态。
    if (_selection_controller)
    {
        _selection_controller->Cancel(item_id);
    }
    if (_editing_text_item)
    {
        CanvasItem* owner = dynamic_cast<CanvasItem*>(_editing_text_item->parentItem());
        if (item_id.isEmpty() || _editing_text_item->itemId() == item_id || (owner && owner->itemId() == item_id))
        {
            FinishTextEditing(true);
        }
    }
}

void PaintScene::SetItemEditPending(const QString& item_id, bool pending)
{
    // 1. 确认锁定不清除选择，待确认对象仍可查看但不能开始新手势。
    if (CanvasItem* item = FindCanvasItem(item_id))
    {
        item->setProperty("canvas_edit_pending", pending);
        emit SigEditAvailabilityChanged();
    }
}

void PaintScene::SetItemEditUndoEnabled(bool enabled)
{
    // 1. 在线确认入口负责登记，离线直接记录完成的交互。
    _is_item_edit_undo_enabled = enabled;
}

void PaintScene::RecordItemEditOperation(const CanvasItemEditState& before, const CanvasItemEditState& after,
                                        const QString& gesture_id, quint64 revision)
{
    // 1. 无变化和已删除对象不登记，混合拖动按手势标识合并为一条记录。
    if (before == after || !FindCanvasItem(after._item_id))
    {
        return;
    }
    DrawItemRecord* record = nullptr;
    for (int index = _localUndoStack.size() - 1; index >= 0; --index)
    {
        if (_localUndoStack[index].is_item_edit && _localUndoStack[index].item_edit._gesture_id == gesture_id)
        {
            record = &_localUndoStack[index];
            break;
        }
    }
    if (!record)
    {
        DrawItemRecord value;
        value.item_id = after._item_id;
        value.is_item_edit = true;
        value.item_edit._gesture_id = gesture_id;
        _localUndoStack.push(value);
        record = &_localUndoStack.top();
    }

    // 2. 同一对象重复确认只替换最终状态，保留手势最初的快照。
    int target = -1;
    for (int index = 0; index < record->item_edit._after.size(); ++index)
    {
        if (record->item_edit._after[index]._item_id == after._item_id)
        {
            target = index;
        }
    }
    if (target < 0)
    {
        record->item_edit._before.append(before);
        record->item_edit._after.append(after);
    } else
    {
        record->item_edit._after[target] = after;
    }
    record->item_edit._revisions.insert(after._item_id, revision);
}

bool PaintScene::TakeItemEditUndo(CanvasItemEditUndoOperation* operation)
{
    // 1. 仅消费栈顶，不能越过更晚的创建或文字操作。
    if (!operation || _localUndoStack.isEmpty() || !_localUndoStack.top().is_item_edit)
    {
        return false;
    }
    const auto& value = _localUndoStack.top().item_edit;
    for (const CanvasItemEditState& state : value._before)
    {
        CanvasItem* item = FindCanvasItem(state._item_id);
        if (item && item->property("canvas_edit_pending").toBool())
        {
            return false;
        }
    }
    *operation = _localUndoStack.pop().item_edit;
    return true;
}

void PaintScene::InvalidateItemEditUndo(const QString& item_id)
{
    // 1. 删除或冲突只废弃目标自身，混合手势中的其他对象仍可独立条件撤销。
    for (int index = _localUndoStack.size() - 1; index >= 0; --index)
    {
        if (!_localUndoStack[index].is_item_edit)
        {
            continue;
        }
        auto& operation = _localUndoStack[index].item_edit;
        for (int state_index = operation._before.size() - 1; state_index >= 0; --state_index)
        {
            if (operation._before[state_index]._item_id == item_id)
            {
                operation._before.removeAt(state_index);
                operation._after.removeAt(state_index);
                operation._revisions.remove(item_id);
            }
        }
        if (operation._before.isEmpty())
        {
            _localUndoStack.removeAt(index);
        }
    }
}

void PaintScene::RebaseItemEditUndo(const QString& item_id, const CanvasItemEditState& confirmed, quint64 revision)
{
    // 1. 只推进最近一次仍可撤销操作，状态不匹配说明中间存在新的内容变化。
    for (int index = _localUndoStack.size() - 1; index >= 0; --index)
    {
        if (!_localUndoStack[index].is_item_edit)
        {
            continue;
        }
        auto& operation = _localUndoStack[index].item_edit;
        for (const CanvasItemEditState& after : operation._after)
        {
            if (after._item_id != item_id)
            {
                continue;
            }
            if (after == confirmed)
            {
                operation._revisions.insert(item_id, revision);
            }
            return;
        }
    }
}

bool PaintScene::RemoveCanvasItem(const QString& item_id)
{
    // 1. 删除前终止所有本地交互，避免手势继续持有失效的绘图操作。
    CancelItemInteraction(item_id);
    CanvasItem* item = FindCanvasItem(item_id);
    if (!item)
    {
        return false;
    }
    if (dynamic_cast<ImageItem*>(item))
    {
        return removeImageItem(item_id);
    }
    if (dynamic_cast<TextItem*>(item))
    {
        return RemoveTextItem(item_id);
    }

    // 2. 清理工具和本地创建记录，剩余多选撤销保留其他对象。
    InvalidateItemEditUndo(item_id);
    _canvas_itemMap.remove(item_id);
    _localItems.remove(item_id);
    _remoteItems.remove(item_id);
    PruneCreationUndo(item_id);
    if (_currUuid == item_id)
    {
        clearCurrentOperation();
    }
    removeItem(item);
    item->setParent(this);
    item->deleteLater();
    _selection_controller->Refresh();
    return true;
}

CanvasItem* PaintScene::ContextItemAt(const QPointF& scene_pos, QWidget* widget) const
{
    // 1. 选择控制器统一处理轮廓、已选内部和控制柄，不在菜单层复制几何算法。
    return _selection_controller->ContextItemAt(scene_pos, widget);
}

bool PaintScene::HasPointerInteraction() const
{
    // 1. 绘图和变换共享菜单互斥条件，手形的左键状态由视图检查。
    return _selection_controller->IsActive() || (_currentOperation && _currentOperation->isStarted());
}

void PaintScene::PruneCreationUndo(const QString& item_id)
{
    // 1. 批量创建只剔除已删除成员，避免后续撤销访问失效 ID。
    for (int index = _localUndoStack.size() - 1; index >= 0; --index)
    {
        auto& record = _localUndoStack[index];
        if (record.is_clipboard_batch)
        {
            record.clipboard_item_ids.removeAll(item_id);
            if (record.clipboard_item_ids.isEmpty())
            {
                _localUndoStack.removeAt(index);
            }
        } else if (!record.is_item_edit && record.item_id == item_id)
        {
            _localUndoStack.removeAt(index);
        }
    }
}

QVector<CanvasItem*> PaintScene::SelectedCanvasItems() const
{
    // 1. 选择控制器统一过滤附属文字和橡皮擦，复制范围与移动手势保持一致。
    return _selection_controller ? _selection_controller->SelectedItems() : QVector<CanvasItem*>();
}

PaintScene::UndoKind PaintScene::NextUndoKind() const
{
    // 1. 统一时间顺序由同一栈维护，在线与离线只区分提交方式。
    if (_localUndoStack.isEmpty())
    {
        return UndoKind::None;
    }
    if (_localUndoStack.top().is_item_edit)
    {
        return UndoKind::ItemEdit;
    }
    return _localUndoStack.top().is_text ? UndoKind::Text : UndoKind::Creation;
}

void PaintScene::SubmitItemEditChange(const CanvasItemEditState& before, const CanvasItemEditState& after,
                                     const QString& gesture_id, bool finished)
{
    // 1. 一次完整操作只登记一次，中间预览由网络层节流。
    if (finished && _is_item_edit_undo_enabled)
    {
        RecordItemEditOperation(before, after, gesture_id, 0);
    }
    emit SigItemEditChanged(before, after, gesture_id, finished);
}

void PaintScene::ConfigureCanvasItem(CanvasItem* item)
{
    // 1. 只登记顶层内容，橡皮擦保留白色覆盖语义且不可选择。
    if (!item || item->parentItem())
    {
        return;
    }
    _canvas_itemMap.insert(item->itemId(), item);
    const bool selectable = CanvasItemEdit::Kind(item) != 5;
    item->setFlag(QGraphicsItem::ItemIsSelectable, selectable);
    item->setFlag(QGraphicsItem::ItemIsMovable, false);
    item->setAcceptedMouseButtons(selectable ? Qt::LeftButton : Qt::NoButton);

    // 2. 兼容图元基类的删除意图，所有键盘路径最终都操作同一选中集合。
    connect(item, &CanvasItem::sigDeleteRequested, this, &PaintScene::SigDeleteItemsRequested, Qt::UniqueConnection);
}

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
        if (!item || item->parentItem())
        {
            continue;
        }
        CanvasItemData item_data;
        if (!CanvasItemSerializer::SerializeItem(item, &item_data, &snapshot, error_message))
        {
            return false;
        }
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
    if (!views().isEmpty() || !_canvas_itemMap.isEmpty() || document._items.size() != document._item_order.size())
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
        if (!item)
        {
            return false;
        }
        addItem(item);
        ConfigureCanvasItem(item);
        loaded_ids.insert(item_id);
        if (auto* image_item = dynamic_cast<ImageItem*>(item))
        {
            _imageItems.insert(item_id, image_item);
            UpdateImageInteraction(image_item);
            connect(image_item, &ImageItem::sigRetryRequested, this, &PaintScene::sigImageRetryRequested);
            connect(image_item, &ImageItem::sigPreviewRequested, this, &PaintScene::sigImagePreviewRequested);
        }
        if (auto* text_item = dynamic_cast<TextItem*>(item))
        {
            ConnectTextItem(text_item);
        }
    }

    // 3. 导入图元是初始内容，不加入撤销栈；后续本地操作继续沿用原有记录。
    setSceneRect(QRectF(QPointF(), document._canvas_size));
    setBackgroundBrush(document._background);
    clearSelection();
    _localUndoStack.clear();
    return true;
}

bool PaintScene::InsertClipboardDocument(const CanvasDocument& document, bool record_undo, QVector<QString>* item_ids, QString* error_message)
{
    // 1. 先在独立对象中恢复所有图元，任何失败都释放临时对象而不污染当前场景。
    if (!item_ids || document._items.isEmpty() || document._items.size() != document._item_order.size())
    {
        if (error_message) *error_message = QStringLiteral("剪贴板图元顺序无效");
        return false;
    }
    QMap<QString, CanvasItemData> item_data_map;
    QVector<CanvasItem*> created;
    for (const CanvasItemData& item_data : document._items)
    {
        const QString item_id = item_data.value(QStringLiteral("item_id")).toString();
        if (item_id.isEmpty() || item_data_map.contains(item_id) || FindCanvasItem(item_id))
        {
            if (error_message) *error_message = QStringLiteral("剪贴板图元 ID 无效或已存在");
            return false;
        }
        item_data_map.insert(item_id, item_data);
    }
    for (const QString& item_id : document._item_order)
    {
        CanvasItem* item = CanvasItemSerializer::DeserializeItem(item_data_map.value(item_id), document._asset_dataMap, error_message);
        if (!item)
        {
            qDeleteAll(created);
            return false;
        }
        created.append(item);
    }

    // 2. 全部成功后按剪贴板层级加入并重建图片、文字和统一 ID 索引。
    clearSelection();
    for (CanvasItem* item : created)
    {
        addItem(item);
        ConfigureCanvasItem(item);
        item->setSelected(true);
        item_ids->append(item->itemId());
        if (auto* image_item = dynamic_cast<ImageItem*>(item))
        {
            _imageItems.insert(item->itemId(), image_item);
            UpdateImageInteraction(image_item);
            connect(image_item, &ImageItem::sigRetryRequested, this, &PaintScene::sigImageRetryRequested);
            connect(image_item, &ImageItem::sigPreviewRequested, this, &PaintScene::sigImagePreviewRequested);
        }
        if (auto* text_item = dynamic_cast<TextItem*>(item))
        {
            ConnectTextItem(text_item);
        }
    }
    // 3. 选择控制器在场景加入后刷新，确保多选边框立即包含全部新副本。
    _selection_controller->Refresh();
    if (record_undo) RecordClipboardCreation(*item_ids);
    return true;
}

void PaintScene::RecordClipboardCreation(const QVector<QString>& item_ids)
{
    // 1. 仅为仍属于本场景的完整副本登记一条整体撤销记录。
    if (item_ids.isEmpty()) return;
    for (const QString& item_id : item_ids) if (!FindCanvasItem(item_id)) return;
    DrawItemRecord record;
    record.is_clipboard_batch = true;
    record.clipboard_item_ids = item_ids;
    _localUndoStack.push(record);
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
    _selection_controller->SetOverlayVisible(false);
    const QList<QGraphicsItem*> selected_items = selectedItems();
    const bool is_cursor_visible = _eraserCursorItem->isVisible();
    clearSelection();
    _eraserCursorItem->hide();
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    render(&painter, QRectF(QPointF(), size), sceneRect(), Qt::IgnoreAspectRatio);
    painter.end();
    for (QGraphicsItem* item : selected_items)
    {
        item->setSelected(true);
    }
    _eraserCursorItem->setVisible(is_cursor_visible);
    _selection_controller->Refresh();
    _selection_controller->SetOverlayVisible(true);
    return image;
}

PaintScene::PaintScene(QObject* parent)
    : QGraphicsScene(parent)
{
    // 1. 光标只是交互辅助层，禁止它接收鼠标，确保事件继续落到场景。
    _eraserCursorItem = new QGraphicsEllipseItem();
    _eraserCursorItem->setPen(QPen(Qt::black, 1));
    _eraserCursorItem->setBrush(QBrush(QColor(200, 200, 200, 50)));
    _eraserCursorItem->setZValue(9999);
    _eraserCursorItem->setAcceptedMouseButtons(Qt::NoButton);
    _eraserCursorItem->setAcceptHoverEvents(false);
    _eraserCursorItem->hide();
    addItem(_eraserCursorItem);

    // 2. 工厂负责绘制策略，独立控制器负责完成后的选择及几何手势。
    _currentTool = DrawToolFactory::create(_currShapeType);
    _selection_controller = new CanvasSelectionController(this);
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
    while (hit_item && !dynamic_cast<TextItem*>(hit_item))
    {
        hit_item = hit_item->parentItem();
    }
    auto* text_item = dynamic_cast<TextItem*>(hit_item);
    if (_editing_text_item && text_item != _editing_text_item && !FinishTextEditing())
    {
        return;
    }
    if (_editing_text_item && text_item == _editing_text_item)
    {
        // 活动编辑器保留原生选字和输入法事件，不进入图元手势。
        QGraphicsScene::mousePressEvent(event);
        return;
    }
    if (_interaction_mode == CanvasInteractionMode::Text && event->button() == Qt::LeftButton)
    {
        if (!_editable)
        {
            return;
        }
        if (text_item && text_item->parentItem())
        {
            text_item = nullptr;
        }
        if (!text_item)
        {
            TextItemData data{QUuid::createUuid().toString(QUuid::WithoutBraces), QString(), _text_style, event->scenePos()};
            text_item = AddTextItem(data);
        }
        if (text_item && !_editing_text_item)
        {
            BeginTextEditing(text_item);
        }
        if (text_item && text_item->IsEditing())
        {
            QGraphicsScene::mousePressEvent(event);
        }
        event->accept();
        return;
    }

    // 3. 选择和 Ctrl 多选交给控制器；只读用户仍能正常查看选择。
    if (_interaction_mode == CanvasInteractionMode::Select)
    {
        if (!_selection_controller->Press(event))
        {
            QGraphicsScene::mousePressEvent(event);
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

    // 2. 控制器按起始快照移动图元，文本选字仍由 Qt 原生编辑器处理。
    if (_interaction_mode == CanvasInteractionMode::Select || _interaction_mode == CanvasInteractionMode::Text)
    {
        if (!_selection_controller->Move(event))
        {
            QGraphicsScene::mouseMoveEvent(event);
        }
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
    // 1. 控制器提交最终图元状态，文字选字的释放事件仍交给 Qt。
    if (_interaction_mode == CanvasInteractionMode::Select || _interaction_mode == CanvasInteractionMode::Text)
    {
        if (!_selection_controller->Release(event))
        {
            QGraphicsScene::mouseReleaseEvent(event);
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
    // 1. 双击独立检测封闭内部，不能依赖仅包含边框的首次选择形状。
    if (_interaction_mode == CanvasInteractionMode::Select)
    {
        _selection_controller->Cancel();
        CanvasItem* hit_item = _selection_controller->HitItem(event->scenePos(), true, event->widget());
        if (hit_item && CanvasItemEdit::IsClosed(hit_item))
        {
            BeginBoundTextEditing(hit_item);
        } else if (auto* text_item = dynamic_cast<TextItem*>(hit_item))
        {
            BeginTextEditing(text_item);
        } else if (auto* image_item = dynamic_cast<ImageItem*>(hit_item))
        {
            if (image_item->loadState() == ImageItem::ImageLoadState::Ready)
            {
                emit sigImagePreviewRequested(image_item->itemId());
            } else if (image_item->loadState() == ImageItem::ImageLoadState::Failed)
            {
                emit sigImageRetryRequested(image_item->itemId());
            }
        }
        event->accept();
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
    if (event->key() == Qt::Key_Escape)
    {
        // Esc 恢复手势起点，同时结束已向网络发送的临时预览。
        _selection_controller->Cancel(QString(), true);
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Undo))
    {
        emit SigUndoRequested();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        emit SigDeleteItemsRequested();
        event->accept();
        return;
    }

    // 2. 禁止旧焦点图元在绘图、手形或只读状态下通过默认按键处理修改图片。
    if (_interaction_mode != CanvasInteractionMode::Select || !_editable)
    {
        event->accept();
        return;
    }

    // 3. 保留图片已有键盘缩放和旋转，使用统一状态提交与撤销。
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
    if (selectedItems().size() != 1 || selected_image->property("canvas_edit_pending").toBool())
    {
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
        const CanvasItemEditState before = CanvasItemEdit::Capture(selected_image);
        const QSignalBlocker blocker(selected_image);
        selected_image->setTransform(QTransform::fromScale(next_scale_x, next_scale_y), false);
        SubmitItemEditChange(before, CanvasItemEdit::Capture(selected_image), QUuid::createUuid().toString(), true);
        _selection_controller->Refresh();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_BracketLeft || event->key() == Qt::Key_BracketRight)
    {
        // 方括号以五度步长旋转图片，几何信号会把变换同步给房间。
        const qreal delta = event->key() == Qt::Key_BracketLeft ? -5.0 : 5.0;
        const CanvasItemEditState before = CanvasItemEdit::Capture(selected_image);
        const QSignalBlocker blocker(selected_image);
        selected_image->setRotation(selected_image->rotation() + delta);
        SubmitItemEditChange(before, CanvasItemEdit::Capture(selected_image), QUuid::createUuid().toString(), true);
        _selection_controller->Refresh();
        event->accept();
        return;
    }

    QGraphicsScene::keyPressEvent(event);
}

void PaintScene::finishCurrentOperation(const QPointF& end_pos)
{
    // 1. 先让策略写入最后一点，再记录图元，确保撤销拿到最终几何状态。
    const QString item_id = _currUuid;
    const ShapeType shape = _currShapeType;
    const std::shared_ptr<IDrawTool> operation = _currentOperation;
    operation->endLocal(end_pos);

    recordFinishedLocalItem(item_id, operation);
    emit sigStrokeEnd(item_id, static_cast<int>(shape), end_pos);
    clearCurrentOperation();
    // 2. 完成信号在 END 和状态清理之后发送，窗口可安全切换回鼠标模式。
    ConfigureCanvasItem(dynamic_cast<CanvasItem*>(operation->item()));
    emit SigLocalCreationFinished(item_id);
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
    // 1. 切换模式前取消统一变换手势并结束笔画，不能遗留跨工具的鼠标抓取。
    if (_interaction_mode != mode)
    {
        _selection_controller->Cancel(QString(), true);
        if (!FinishTextEditing())
        {
            return;
        }
        if (_currentOperation && _currentOperation->isStarted())
        {
            finishCurrentOperation(_currentOperation->currentPosition());
        }
        if (QGraphicsItem* grabber = mouseGrabberItem())
        {
            grabber->ungrabMouse();
        }
    }

    // 2. 只改变移动和焦点能力，不能用关闭选择标记的接口清掉现有选择。
    _interaction_mode = mode;
    for (QGraphicsItem* graphics_item : items())
    {
        ConfigureCanvasItem(dynamic_cast<CanvasItem*>(graphics_item));
    }
    for (TextItem* item : _text_itemMap)
    {
        UpdateTextInteraction(item);
    }
    hideEraserCursor();
    for (ImageItem* image_item : _imageItems)
    {
        UpdateImageInteraction(image_item);
    }
    _selection_controller->Refresh();
}

CanvasInteractionMode PaintScene::InteractionMode() const
{
    // 1. 场景模式独立于绘图类型，鼠标和手形不创建绘图 Tool。
    return _interaction_mode;
}

void PaintScene::UpdateImageInteraction(ImageItem* image_item)
{
    // 1. 选择标记始终保留，模式通过场景和视图的事件分发控制实际选择操作。
    const bool can_move = _interaction_mode == CanvasInteractionMode::Select && _editable;
    image_item->setFlag(QGraphicsItem::ItemIsMovable, false);
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
        _selection_controller->Cancel();
        FinishTextEditing(true);
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
    emit SigEditAvailabilityChanged();
    for (ImageItem* image_item : _imageItems)
    {
        UpdateImageInteraction(image_item);
    }
    for (TextItem* item : _text_itemMap)
    {
        UpdateTextInteraction(item);
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
    // 1. 逐条跳过失效记录，文字仅恢复内容和样式，不恢复移动位置。
    while (!_localUndoStack.isEmpty())
    {
        const DrawItemRecord record = _localUndoStack.pop();

        if (record.is_clipboard_batch)
        {
            // 2. 批量粘贴视为一条创建记录，撤销时移除全部副本。
            for (const QString& item_id : record.clipboard_item_ids) RemoveCanvasItem(item_id);
            return;
        }

        if (record.is_item_edit)
        {
            // 2. 混合移动和文字增高按同一完整操作恢复，不逐帧撤销。
            for (const CanvasItemEditState& state : record.item_edit._before)
            {
                ApplyItemEditState(state);
            }
            return;
        }

        if (record.is_text)
        {
            TextItem* text_item = FindTextItem(record.item_id);
            if (!text_item)
            {
                continue;
            }
            if (record.before_text._text.isEmpty())
            {
                RemoveTextItem(record.item_id);
            } else
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
    // 1. 没有 ID 或图元的操作不能参与撤销，否则会污染撤销栈。
    if (item_id.isEmpty() ||
        !operation ||
        !operation->item())
    {
        return;
    }

    _localItems.insert(item_id, operation->item());
    ConfigureCanvasItem(dynamic_cast<CanvasItem*>(operation->item()));
    _localUndoStack.push(DrawItemRecord{item_id,
                                        operation->shapeType(),
                                        operation,
                                        nullptr,
                                        false});
}

void PaintScene::ConnectTextItem(TextItem* item)
{
    // 1. 文字索引由场景拥有，导入、远端回放和本地创建统一走同一连接入口。
    if (!item)
    {
        return;
    }
    ConfigureCanvasItem(item);
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
    if (!item)
    {
        return;
    }
    const bool can_move = _editable && !_pending_text_ids.contains(item->itemId()) &&
                          _interaction_mode == CanvasInteractionMode::Select && !item->IsEditing();
    item->setFlag(QGraphicsItem::ItemIsMovable, false);
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
    PruneCreationUndo(item_id);
    TextItem* item = FindTextItem(item_id);
    _selection_controller->Cancel(item_id);
    InvalidateItemEditUndo(item_id);
    if (!item)
    {
        return false;
    }
    if (_editing_text_item == item)
    {
        item->CancelEditing();
        _editing_text_item.clear();
    }
    _text_itemMap.remove(item_id);
    _canvas_itemMap.remove(item_id);
    _pending_text_ids.remove(item_id);
    InvalidateTextUndo(item_id);
    removeItem(item);
    _selection_controller->Refresh();
    // 2. Esc 和空内容完成可能来自子编辑器的事件回调，事件返回后再销毁。
    item->setParent(this);
    item->deleteLater();
    return true;
}

bool PaintScene::BeginTextEditing(TextItem* item)
{
    // 1. 同时只允许一个编辑器，并保存位置独立的内容样式快照。
    if (!_editable || !item || _pending_text_ids.contains(item->itemId()) || item->property("canvas_edit_pending").toBool())
    {
        return false;
    }
    auto* parent_item = dynamic_cast<CanvasItem*>(item->parentItem());
    if (parent_item && parent_item->property("canvas_edit_pending").toBool())
    {
        return false;
    }
    if (_editing_text_item && _editing_text_item != item && !FinishTextEditing())
    {
        return false;
    }
    if (!_editing_text_item)
    {
        _editing_text_item = item;
        _text_before_edit = item->GetTextData();
        CanvasItem* owner = dynamic_cast<CanvasItem*>(item->parentItem());
        _text_before_item_state = CanvasItemEdit::Capture(owner ? owner : item);
        _text_edit_gesture_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        item->BeginEditing();
        UpdateTextInteraction(item);
        emit SigTextEditingChanged();
    }
    return item->IsEditing();
}

bool PaintScene::BeginBoundTextEditing(CanvasItem* item)
{
    // 1. 编辑权限和最终确认锁同时生效，切换目标前先提交已有草稿。
    if (!_editable || !item || !CanvasItemEdit::IsClosed(item) || item->property("canvas_edit_pending").toBool())
    {
        return false;
    }
    if (_editing_text_item && _editing_text_item != item->BoundTextItem() && !FinishTextEditing())
    {
        return false;
    }
    const CanvasItemEditState before = CanvasItemEdit::Capture(item);
    if (!item->BoundTextItem())
    {
        item->SetBoundTextData(QString(), _text_style);
    }
    TextItem* text_item = item->BoundTextItem();
    if (!text_item)
    {
        return false;
    }

    // 2. 子组件只连接草稿信号，不独立登记网络 ID、撤销记录或导出顺序。
    if (!text_item->property("canvas_bound_connected").toBool())
    {
        text_item->setProperty("canvas_bound_connected", true);
        connect(text_item, &TextItem::SigFinishRequested, this, [this](bool cancel) { FinishTextEditing(cancel); });
        connect(text_item, &TextItem::SigTextError, this, &PaintScene::SigTextError);
    }
    clearSelection();
    item->setSelected(true);
    text_item->setVisible(true);
    if (!BeginTextEditing(text_item))
    {
        return false;
    }
    _text_before_item_state = before;
    item->UpdateBoundTextLayout();
    _selection_controller->Refresh();
    return true;
}

bool PaintScene::FinishTextEditing(bool is_cancel)
{
    // 1. 附属文字恢复父图元完整快照，取消时连同自动增加的高度一起回滚。
    TextItem* item = _editing_text_item;
    if (!item)
    {
        return true;
    }
    const TextItemData before = _text_before_edit;
    CanvasItem* owner = dynamic_cast<CanvasItem*>(item->parentItem());
    if (is_cancel)
    {
        item->CancelEditing();
        _editing_text_item.clear();
        if (owner)
        {
            CanvasItemEdit::Apply(owner, _text_before_item_state);
        } else if (before._text.isEmpty())
        {
            RemoveTextItem(before._item_id);
        } else
        {
            ApplyItemEditState(_text_before_item_state);
        }
        _selection_controller->Refresh();
        emit SigTextEditingChanged();
        return true;
    }

    // 2. 提交先完成输入法，再按父图元状态作为一次原子编辑发送。
    if (!item->CommitEditing())
    {
        return false;
    }
    const TextItemData after = item->GetTextData();
    _editing_text_item.clear();
    UpdateTextInteraction(item);
    if (owner)
    {
        owner->UpdateBoundTextLayout();
        const CanvasItemEditState state = CanvasItemEdit::Capture(owner);
        if (state != _text_before_item_state)
        {
            SubmitItemEditChange(_text_before_item_state, state, _text_edit_gesture_id, true);
        }
        _selection_controller->Refresh();
        emit SigTextEditingChanged();
        return true;
    }

    // 3. 独立文字创建和清空沿用原有协议；已有内容编辑使用完整状态操作。
    if (after._text.trimmed().isEmpty())
    {
        const QString item_id = after._item_id;
        RemoveTextItem(item_id);
        if (!before._text.isEmpty())
        {
            emit SigTextDeleteRequested(item_id);
        }
        emit SigTextEditingChanged();
        return true;
    }
    const CanvasItemEditState state = CanvasItemEdit::Capture(item);
    if (before._text.isEmpty())
    {
        if (_is_text_undo_enabled)
        {
            RecordTextOperation(before, after);
        }
        emit SigTextCommitted(before, after);
        emit SigLocalCreationFinished(item->itemId());
    } else if (state != _text_before_item_state)
    {
        SubmitItemEditChange(_text_before_item_state, state, _text_edit_gesture_id, true);
        emit SigTextCommitted(before, after);
    }
    _selection_controller->Refresh();
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
    if (_editing_text_item)
    {
        return _editing_text_item;
    }
    const QList<QGraphicsItem*> selected_items = selectedItems();
    if (selected_items.size() != 1)
    {
        return nullptr;
    }
    auto* item = dynamic_cast<CanvasItem*>(selected_items.first());
    if (!item)
    {
        return nullptr;
    }
    if (auto* text_item = dynamic_cast<TextItem*>(item))
    {
        return text_item;
    }
    return item->BoundTextItem();
}

void PaintScene::SetTextStyle(const TextStyle& style)
{
    // 1. 有目标时立即应用样式，没有目标时仅更新新建默认值。
    _text_style = style;
    if (TextItem* item = ActiveTextItem())
    {
        auto* parent_item = dynamic_cast<CanvasItem*>(item->parentItem());
        if (!_pending_text_ids.contains(item->itemId()) && !item->property("canvas_edit_pending").toBool() &&
            (!parent_item || !parent_item->property("canvas_edit_pending").toBool()))
        {
            item->SetTextStyle(style);
        }
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
    if (is_pending)
    {
        _pending_text_ids.insert(item_id);
    } else
    {
        _pending_text_ids.remove(item_id);
    }
    SetItemEditPending(item_id, is_pending);
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
    // 1. 只消费真正的栈顶，不能跨过较新的移动或文字排版操作。
    if (!before || !is_create || _localUndoStack.isEmpty())
    {
        return false;
    }
    const DrawItemRecord record = _localUndoStack.top();
    if (!record.is_text || !FindTextItem(record.item_id) || _pending_text_ids.contains(record.item_id))
    {
        return false;
    }
    _localUndoStack.pop();
    *before = record.before_text;
    *is_create = record.before_text._text.isEmpty();
    return true;
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
    ConfigureCanvasItem(image_item);
    _imageItems.insert(item_id, image_item);
    // 2. 新图片按当前模式配置交互，不改变工具或已有选择。
    UpdateImageInteraction(image_item);
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
    // 1. 删除先解除本地手势，确认回包不会再命中已释放的图元。
    CancelItemInteraction(item_id);
    InvalidateItemEditUndo(item_id);
    // 2. 按 ID 删除拖动记录，远端删除和本地删除都不会遗留活动图片指针。
    auto iterator = _imageItems.find(item_id);
    if (iterator == _imageItems.end() || !iterator.value())
    {
        return false;
    }

    ImageItem* image_item = iterator.value();
    _imageItems.erase(iterator);
    // 3. 清理对应创建及批量创建记录，保留其他图元的撤销顺序。
    if (remove_undo_record)
    {
        PruneCreationUndo(item_id);
    }
    _canvas_itemMap.remove(item_id);

    // 4. 场景不再拥有图片时由本入口显式销毁，Qt 自动解除该图元的鼠标抓取。
    if (image_item->scene() == this)
    {
        removeItem(image_item);
    }
    delete image_item;
    return true;
}

void PaintScene::applyRemoteDraw(const message::DrawReq& request)
{
    // 1. 远端协议中的 item_id 是同一笔画 START/MOVE/END 的关联键。
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
        ConfigureCanvasItem(dynamic_cast<CanvasItem*>(operation->item()));
        if (auto* item = dynamic_cast<CanvasItem*>(operation->item()))
        {
            item->setProperty("canvas_drawing", true);
        }
        _remoteItems.insert(item_id, RemoteItem{shape, operation});
        return;
    }

    // 2. MOVE/END 没有对应 START 时无法恢复完整图形，沿用原逻辑直接忽略。
    auto iterator = _remoteItems.find(item_id);
    if (iterator == _remoteItems.end() ||
        !iterator->operation ||
        iterator->shape != shape)
    {
        return;
    }

    iterator->operation->updateRemote(request);
    if (request.cmd() == message::CMD_END)
    {
        if (auto* item = dynamic_cast<CanvasItem*>(iterator->operation->item()))
        {
            item->setProperty("canvas_drawing", false);
        }
    }
    _selection_controller->Refresh();
}

void PaintScene::resetScene()
{
    // 1. 先清理操作、文字编辑引用和撤销记录，再让场景删除图元。
    _selection_controller->Cancel();
    _selection_controller->BeforeSceneClear();
    _canvas_itemMap.clear();
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
    _selection_controller->AfterSceneClear();
}
