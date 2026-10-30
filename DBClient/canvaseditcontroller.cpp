#include "canvaseditcontroller.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QTimer>

#include "canvasgraphicsview.h"
#include "paintscene.h"
#include "canvasitems/imageitem.h"

CanvasEditController::CanvasEditController(PaintScene* scene, CanvasGraphicsView* view, QObject* parent)
    : QObject(parent), _scene(scene), _view(view), _menu(new QMenu(view))
{
    // 1. QAction 仅作为命令，不注册全局快捷键，避免抢占文字和聊天输入。
    _copy_action = _menu->addAction(QStringLiteral("复制"));
    _paste_action = _menu->addAction(QStringLiteral("粘贴"));
    _delete_action = _menu->addAction(QStringLiteral("删除"));
    connect(_copy_action, &QAction::triggered, this, &CanvasEditController::Copy);
    connect(_paste_action, &QAction::triggered, this, &CanvasEditController::Paste);
    connect(_delete_action, &QAction::triggered, this, &CanvasEditController::Delete);

    // 2. 所有工具模式使用相同命令，键盘入口由视图先完成文本焦点隔离。
    connect(view, &CanvasGraphicsView::sigCopyItemsRequested, this, &CanvasEditController::Copy);
    connect(view, &CanvasGraphicsView::sigPasteImageRequested, this, [this](QPointF position) {
        // 1. 键盘触发时只保存本次鼠标落点，复用中心锚定命令。
        _paste_position = position;
        _paste_anchor = CanvasPasteAnchor::Center;
        Paste();
    });
    connect(view, &CanvasGraphicsView::SigDeleteItemsRequested, this, &CanvasEditController::Delete);
    connect(view, &CanvasGraphicsView::SigEditMenuRequested, this, &CanvasEditController::ShowMenu);
    connect(scene, &PaintScene::SigDeleteItemsRequested, this, &CanvasEditController::Delete);
    connect(scene, &PaintScene::SigEditAvailabilityChanged, this, &CanvasEditController::RefreshActions);
    connect(scene, &QGraphicsScene::selectionChanged, this, &CanvasEditController::RefreshActions);
    connect(scene, &QGraphicsScene::changed, this, &CanvasEditController::RefreshActions);
    connect(scene, &PaintScene::SigTextEditingChanged, this, &CanvasEditController::RefreshActions);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &CanvasEditController::RefreshClipboard);

    // 3. Qt 隐藏菜单可能早于 QAction::triggered，延迟释放上下文以保留本次目标。
    connect(_menu, &QMenu::aboutToHide, this, [this]() {
        // 1. 右键释放可能已被菜单消费，先恢复工具光标和输入序列。
        if (_view)
        {
            _view->RestoreToolCursor();
        }
        // 2. 命令执行后再清理上下文，并把焦点交回仍活动的画布。
        QTimer::singleShot(0, this, [this]() {
            // 1. 已重新打开的菜单保留新目标，切换到其他窗口时不抢焦点。
            if (_menu && !_menu->isVisible())
            {
                _has_context = false;
                _context_ids.clear();
                if (_view && _view->isEnabled())
                {
                    QWidget* active_window = QApplication::activeWindow();
                    if (active_window == _menu || active_window == _view->window())
                    {
                        _view->window()->activateWindow();
                        _view->setFocus(Qt::PopupFocusReason);
                    }
                }
                RefreshActions();
            }
        });
    });
    RefreshClipboard();
}

CanvasEditController::~CanvasEditController()
{
    // 1. 菜单依附视图，控制器随场景替换时主动回收，所有连接由 QObject 自动解除。
    delete _menu;
}

void CanvasEditController::CloseMenu()
{
    // 1. 生命周期切换立即清除固定目标，已经排队的动作不能进入新房间。
    _has_context = false;
    _context_ids.clear();
    if (_menu)
    {
        _menu->hide();
    }
}

QStringList CanvasEditController::TargetIds() const
{
    // 1. 菜单只解析打开时的稳定 ID，远端删除后不会扩展到其他选中对象。
    QStringList result;
    if (!_scene)
    {
        return result;
    }
    if (_has_context)
    {
        for (const QString& id : _context_ids)
        {
            if (_scene->FindCanvasItem(id))
            {
                result.append(id);
            }
        }
    } else
    {
        for (CanvasItem* item : _scene->SelectedCanvasItems())
        {
            result.append(item->itemId());
        }
    }
    return result;
}

void CanvasEditController::RefreshActions()
{
    // 1. 文字编辑与空白菜单均不能误操作旧选择；只读复制不依赖编辑权限。
    const QStringList ids = TargetIds();
    const bool is_text_editing = !_scene || _scene->IsTextEditing();
    bool can_copy = !is_text_editing && !ids.isEmpty() && ids.size() <= CanvasClipboardPayload::MAX_ITEMS;
    bool can_delete = !is_text_editing && !ids.isEmpty() && _scene->isEditable();
    for (const QString& id : ids)
    {
        CanvasItem* item = _scene->FindCanvasItem(id);
        if (item->property("canvas_edit_pending").toBool() && !item->property("canvas_upload_local").toBool())
        {
            can_delete = false;
        }
        if (auto* image = dynamic_cast<ImageItem*>(item))
        {
            if (image->loadState() != ImageItem::ImageLoadState::Ready || image->pixmap().isNull())
            {
                can_copy = false;
            }
        }
    }
    _copy_action->setEnabled(can_copy);
    _delete_action->setEnabled(can_delete);
    _paste_action->setEnabled(!is_text_editing && _scene->isEditable() && _clipboard_content._kind != CanvasClipboardContent::Kind::None);
}

void CanvasEditController::RefreshClipboard()
{
    // 1. 解析结果随系统剪贴板失效，菜单展开期间也能更新粘贴状态。
    _clipboard_content = CanvasClipboardPayload::Resolve(QApplication::clipboard()->mimeData());
    RefreshActions();
}

void CanvasEditController::ShowMenu(const QPointF& scene_pos, const QPoint& screen_pos)
{
    // 1. 已有左键手势和文字编辑保留自己的事件语义，不因菜单提交内容。
    if (!_scene || !_view || _scene->IsTextEditing() || _scene->HasPointerInteraction() ||
        QApplication::mouseButtons().testFlag(Qt::LeftButton))
    {
        return;
    }
    CanvasItem* hit = _scene->ContextItemAt(scene_pos, _view->viewport());
    if (hit && !hit->isSelected())
    {
        _scene->clearSelection();
        hit->setSelected(true);
    }

    // 2. 空白菜单保留旧选择，但固定目标为空；内容菜单记录完整混合选择。
    _context_ids.clear();
    if (hit)
    {
        for (CanvasItem* item : _scene->SelectedCanvasItems())
        {
            _context_ids.append(item->itemId());
        }
    }
    _has_context = true;
    _paste_position = scene_pos;
    _paste_anchor = CanvasPasteAnchor::TopLeft;
    _scene->hideEraserCursor();
    _view->setFocus(Qt::MouseFocusReason);
    RefreshActions();
    _menu->popup(screen_pos);
}

void CanvasEditController::Copy()
{
    // 1. 动作执行时再次校验，菜单期间的删除或权限变化不能使用悬空对象。
    RefreshActions();
    if (_copy_action->isEnabled())
    {
        emit SigCopyRequested(TargetIds());
    }
}

void CanvasEditController::Paste()
{
    // 1. 菜单和快捷键共用状态与数据识别，键盘遇到损坏剪贴板时显示原因。
    RefreshActions();
    if (_paste_action->isEnabled())
    {
        emit SigPasteRequested(_paste_position, _paste_anchor);
    } else if (_scene && !_scene->IsTextEditing() && _scene->isEditable() && _clipboard_content._kind == CanvasClipboardContent::Kind::None)
    {
        emit SigError(_clipboard_content._error_message);
    }
}

void CanvasEditController::Delete()
{
    // 1. 整组状态通过后一次转发，等待确认期间重复按键不会叠加删除请求。
    RefreshActions();
    if (_delete_action->isEnabled())
    {
        emit SigDeleteRequested(TargetIds());
    }
}
