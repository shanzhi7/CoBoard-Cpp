#include "canvasselectioncontroller.h"

#include "paintscene.h"
#include "canvasitems/arrowitem.h"
#include "canvasitems/lineitem.h"

#include <cmath>

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QPainter>
#include <QPainterPathStroker>
#include <QSignalBlocker>
#include <QUuid>

namespace
{
struct SelectionFrame
{
    QPolygonF _corners; // 视口像素中的左上、右上、右下、左下控制点。
    QPointF _rotation_point; // 顶边外侧 24 像素的旋转圆心。
};

SelectionFrame BuildFrame(const CanvasItem* item, const QTransform& scene_to_view)
{
    // 1. 控制框随图元旋转，退化线仅在显示上预留控制柄空间。
    const QRectF rect = CanvasItemEdit::Bounds(item);
    const QTransform transform = item->sceneTransform() * scene_to_view;
    SelectionFrame frame;
    frame._corners << transform.map(rect.topLeft()) << transform.map(rect.topRight())
                   << transform.map(rect.bottomRight()) << transform.map(rect.bottomLeft());
    QPointF axis_x = transform.map(QPointF(1, 0)) - transform.map(QPointF());
    QPointF axis_y = transform.map(QPointF(0, 1)) - transform.map(QPointF());
    axis_x /= qMax(0.000001, std::hypot(axis_x.x(), axis_x.y()));
    axis_y /= qMax(0.000001, std::hypot(axis_y.x(), axis_y.y()));
    if (QLineF(frame._corners[0], frame._corners[1]).length() < 12.0)
    {
        frame._corners[0] -= axis_x * 6.0;
        frame._corners[3] -= axis_x * 6.0;
        frame._corners[1] += axis_x * 6.0;
        frame._corners[2] += axis_x * 6.0;
    }
    if (QLineF(frame._corners[0], frame._corners[3]).length() < 12.0)
    {
        frame._corners[0] -= axis_y * 6.0;
        frame._corners[1] -= axis_y * 6.0;
        frame._corners[2] += axis_y * 6.0;
        frame._corners[3] += axis_y * 6.0;
    }
    frame._rotation_point = (frame._corners[0] + frame._corners[1]) / 2.0 - axis_y * 24.0;
    return frame;
}

qreal DistanceToSegment(const QPointF& point, const QPointF& start, const QPointF& end)
{
    // 1. 有限线段投影用于边缘命中，不能把延长线当作可拖动边。
    const QPointF delta = end - start;
    const qreal length_squared = QPointF::dotProduct(delta, delta);
    const qreal ratio = length_squared > 0.0 ? qBound(0.0, QPointF::dotProduct(point - start, delta) / length_squared, 1.0) : 0.0;
    return QLineF(point, start + delta * ratio).length();
}

int HitHandle(const CanvasItem* item, const SelectionFrame& frame, const QPointF& point)
{
    // 1. 旋转优先于四角和边缘，图片与其他图元共用局部坐标控制柄。
    if (QLineF(point, frame._rotation_point).length() <= 8.0)
    {
        return 8;
    }
    for (int index = 0; index < 4; ++index)
    {
        if (QLineF(point, frame._corners[index]).length() <= 6.0)
        {
            return index;
        }
    }

    // 2. 精确水平或竖直的线段禁用无意义的垂直轴边缘缩放。
    const QRectF rect = CanvasItemEdit::Bounds(item);
    const int kind = CanvasItemEdit::Kind(item);
    for (int index = 0; index < 4; ++index)
    {
        if ((kind == 4 || kind == 6) &&
            ((qFuzzyIsNull(rect.height()) && (index == 0 || index == 2)) ||
             (qFuzzyIsNull(rect.width()) && (index == 1 || index == 3))))
        {
            continue;
        }
        if (DistanceToSegment(point, frame._corners[index], frame._corners[(index + 1) % 4]) <= 6.0)
        {
            return index + 4;
        }
    }
    return -1;
}

QPointF OppositeAnchor(const QRectF& rect, int handle)
{
    // 1. 伸缩固定对角或对边中心，中心变化不使旋转图元跳动。
    switch (handle)
    {
    case 0: return rect.bottomRight();
    case 1: return rect.bottomLeft();
    case 2: return rect.topLeft();
    case 3: return rect.topRight();
    case 4: return QPointF(rect.center().x(), rect.bottom());
    case 5: return QPointF(rect.left(), rect.center().y());
    case 6: return QPointF(rect.center().x(), rect.top());
    case 7: return QPointF(rect.right(), rect.center().y());
    default: return rect.center();
    }
}
}

class CanvasSelectionOverlay final : public QGraphicsItem
{
public:
    explicit CanvasSelectionOverlay(PaintScene* scene) : _scene(scene) // 创建不参与命中和序列化的辅助图层。
    {
        // 1. 覆盖层始终位于内容上方，事件由控制器统一路由。
        setZValue(1000000.0);
        setAcceptedMouseButtons(Qt::NoButton);
    }
    QRectF boundingRect() const override // 返回覆盖内容和屏幕控制柄的场景范围。
    {
        // 1. 视口最小缩放为 10%，400 场景单位足以容纳顶部控制柄。
        return _bounds;
    }
    QPainterPath shape() const override // 禁止覆盖层拦截正常内容命中。
    {
        // 1. 空形状确保辅助矩形不会成为用户图元。
        return QPainterPath();
    }
    void Refresh() // 更新真实选择及包围范围。
    {
        // 1. 选择只读取顶层内容，附属编辑器与橡皮擦不进入覆盖层。
        prepareGeometryChange();
        _items.clear();
        _bounds = QRectF();
        for (QGraphicsItem* graphics_item : _scene->selectedItems())
        {
            auto* item = dynamic_cast<CanvasItem*>(graphics_item);
            if (!item || item->parentItem() || CanvasItemEdit::Kind(item) == 5)
            {
                continue;
            }
            _items.append(item);
            _bounds = _bounds.united(item->sceneBoundingRect());
        }
        _bounds.adjust(-400, -400, 400, 400);
        update();
    }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override // 以固定像素绘制蓝框及空心控制柄。
    {
        // 1. 把几何映射到当前绘图设备，框与控制柄不会随画布缩放变粗。
        const QTransform scene_to_view = painter->worldTransform();
        painter->save();
        painter->resetTransform();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(QColor(105, 95, 255), 1.0));
        painter->setBrush(Qt::NoBrush);
        for (const QPointer<CanvasItem>& item : _items)
        {
            if (!item || !item->isSelected())
            {
                continue;
            }
            const SelectionFrame frame = BuildFrame(item, scene_to_view);
            painter->drawPolygon(frame._corners);
            if (_items.size() != 1 || _scene->IsTextEditing())
            {
                continue;
            }
            painter->setBrush(Qt::white);
            for (const QPointF& corner : frame._corners)
            {
                painter->drawRect(QRectF(corner - QPointF(4, 4), QSizeF(8, 8)));
            }
            painter->drawEllipse(frame._rotation_point, 4.0, 4.0);
            painter->setBrush(Qt::NoBrush);
        }
        painter->restore();
    }
private:
    PaintScene* _scene; // 场景拥有覆盖层，引用仅在绘制生命周期内使用。
    QRectF _bounds; // 包含选中对象及控制柄的场景范围。
    QVector<QPointer<CanvasItem>> _items; // 图元删除后自动失效的选择引用。
};

CanvasSelectionController::CanvasSelectionController(PaintScene* scene)
    : QObject(scene), _scene(scene)
{
    // 1. QObject 跟随场景销毁，选择改变只刷新辅助图层。
    AfterSceneClear();
    connect(scene, &QGraphicsScene::selectionChanged, this, [this]() { Refresh(); });
}

CanvasSelectionController::~CanvasSelectionController()
{
    // 1. 场景可能已在基类析构中释放图元，所有权由显式清空流程协调。
    BeforeSceneClear();
}

QGraphicsView* CanvasSelectionController::ViewForWidget(QWidget* widget) const
{
    // 1. 优先使用实际事件视口，测试没有 widget 时使用首个视图。
    for (QGraphicsView* view : _scene->views())
    {
        if (view->viewport() == widget)
        {
            return view;
        }
    }
    return _scene->views().isEmpty() ? nullptr : _scene->views().first();
}

QVector<CanvasItem*> CanvasSelectionController::SelectedItems() const
{
    // 1. 过滤非内容对象，所有手势均只操作顶层图元。
    QVector<CanvasItem*> result;
    for (QGraphicsItem* graphics_item : _scene->selectedItems())
    {
        auto* item = dynamic_cast<CanvasItem*>(graphics_item);
        if (item && !item->parentItem() && CanvasItemEdit::Kind(item) != 5)
        {
            result.append(item);
        }
    }
    return result;
}

CanvasItem* CanvasSelectionController::HitItem(const QPointF& scene_pos, bool include_closed_interior, QWidget* widget) const
{
    // 1. 在视口中膨胀实际笔迹六像素，缩放与导入仿射矩阵都不改变可点击距离。
    QGraphicsView* view = ViewForWidget(widget);
    const QTransform scene_to_view = view ? view->viewportTransform() : QTransform();
    const QPointF point = scene_to_view.map(scene_pos);
    for (QGraphicsItem* graphics_item : _scene->items(Qt::DescendingOrder))
    {
        auto* item = dynamic_cast<CanvasItem*>(graphics_item);
        if (!item || item->parentItem() || !item->isVisible() || CanvasItemEdit::Kind(item) == 5 || item->property("canvas_drawing").toBool())
        {
            continue;
        }
        const QTransform transform = item->sceneTransform() * scene_to_view;
        QPainterPath path = transform.map(CanvasItemEdit::Outline(item));
        QPainterPathStroker tolerance;
        tolerance.setWidth(12.0);
        if (path.contains(point) || tolerance.createStroke(path).contains(point) ||
            (include_closed_interior && CanvasItemEdit::Interior(item).contains(item->mapFromScene(scene_pos))))
        {
            return item;
        }
    }
    return nullptr;
}

void CanvasSelectionController::StartGesture(const QVector<CanvasItem*>& items, int handle, QGraphicsSceneMouseEvent* event)
{
    // 1. 待确认对象允许查看选择，禁止加入新的混合移动操作。
    if (!_scene->isEditable() || items.isEmpty())
    {
        return;
    }
    for (CanvasItem* item : items)
    {
        if (item->property("canvas_edit_pending").toBool())
        {
            return;
        }
    }
    _before.clear();
    for (CanvasItem* item : items)
    {
        _before.append(CanvasItemEdit::Capture(item));
    }
    _gesture_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    _start_scene_pos = event->scenePos();
    _start_local_pos = items.first()->mapFromScene(_start_scene_pos);
    _start_bounds = CanvasItemEdit::Bounds(items.first());
    _scene_to_local = items.first()->sceneTransform().inverted();
    _handle = handle;
    _is_active = true;
}

bool CanvasSelectionController::Press(QGraphicsSceneMouseEvent* event)
{
    // 1. 单选先检测显式控制柄，多选只支持共同移动。
    if (event->button() != Qt::LeftButton)
    {
        return false;
    }
    const QVector<CanvasItem*> selected = SelectedItems();
    QGraphicsView* view = ViewForWidget(event->widget());
    const QTransform transform = view ? view->viewportTransform() : QTransform();
    if (selected.size() == 1 && !event->modifiers().testFlag(Qt::ControlModifier))
    {
        const int handle = HitHandle(selected.first(), BuildFrame(selected.first(), transform), transform.map(event->scenePos()));
        if (handle >= 0)
        {
            StartGesture(selected, handle, event);
            event->accept();
            return true;
        }
    }

    // 2. 首次只按轮廓选择，已选图元内部可直接拖动，Ctrl 用于混合增减选择。
    CanvasItem* hit = HitItem(event->scenePos(), false, event->widget());
    if (event->modifiers().testFlag(Qt::ControlModifier))
    {
        if (hit)
        {
            hit->setSelected(!hit->isSelected());
        }
        event->accept();
        return true;
    }
    if (hit && !hit->isSelected())
    {
        _scene->clearSelection();
        hit->setSelected(true);
    } else if (!hit)
    {
        for (CanvasItem* item : selected)
        {
            if (CanvasItemEdit::Bounds(item).contains(item->mapFromScene(event->scenePos())))
            {
                hit = item;
                break;
            }
        }
        if (!hit)
        {
            _scene->clearSelection();
        }
    }
    if (hit)
    {
        StartGesture(SelectedItems(), -1, event);
    }
    Refresh();
    event->accept();
    return true;
}

void CanvasSelectionController::UpdateSingleItem(const QPointF& scene_pos)
{
    // 1. 所有坐标来自手势开始的逆变换，旋转或尺寸变化不改变后续计算基准。
    const CanvasItemEditState before = _before.first();
    CanvasItem* item = _scene->FindCanvasItem(before._item_id);
    if (!item)
    {
        return;
    }
    const qreal current_height = CanvasItemEdit::Bounds(item).height();
    const qreal current_rotation = item->rotation();
    const QSignalBlocker blocker(item);
    CanvasItemEdit::Apply(item, before);
    const QPointF local_pos = _scene_to_local.map(scene_pos);
    if (_handle == 8)
    {
        const QPointF center = _start_bounds.center();
        const QPointF scene_center = item->mapToScene(center);
        const QPointF initial = _start_local_pos - center;
        const QPointF current = local_pos - center;
        const qreal delta = std::atan2(current.y(), current.x()) - std::atan2(initial.y(), initial.x());
        item->setTransformOriginPoint(center);
        // 跨越 atan2 分支时选择最近的整圈角度，连续拖动不会突然跳回上一圈。
        const qreal rotation = before._rotation + delta * 180.0 / 3.14159265358979323846;
        item->setRotation(rotation + 360.0 * std::round((current_rotation - rotation) / 360.0));
        item->setPos(item->pos() + scene_center - item->mapToScene(center));
        return;
    }

    // 2. 退化直线角柄绑定真实端点，拖动能够从水平或竖直产生斜率。
    const QPointF delta = local_pos - _start_local_pos;
    if ((before._kind == 4 || before._kind == 6) && _handle < 4 &&
        (qFuzzyIsNull(_start_bounds.width()) || qFuzzyIsNull(_start_bounds.height())))
    {
        QLineF line(before._geometry[0], before._geometry[1], before._geometry[2], before._geometry[3]);
        const bool is_horizontal = qFuzzyIsNull(_start_bounds.height());
        const bool first_side = is_horizontal ? (_handle == 0 || _handle == 3) : (_handle == 0 || _handle == 1);
        const bool first_is_first_side = is_horizontal ? line.x1() <= line.x2() : line.y1() <= line.y2();
        if (first_side == first_is_first_side)
        {
            line.setP1(line.p1() + delta);
        } else
        {
            line.setP2(line.p2() + delta);
        }
        if (auto* value = dynamic_cast<LineItem*>(item))
        {
            value->setLine(line);
        } else
        {
            static_cast<ArrowItem*>(item)->SetLine(line);
        }
        return;
    }

    // 3. 四边和四角采用固定对边规则，文字只改变布局宽高，字体保持不变。
    QRectF target = _start_bounds;
    const qreal minimum = before._kind == 4 || before._kind == 6 ? 0.0 : 12.0;
    if (_handle == 0 || _handle == 3 || _handle == 7)
    {
        target.setLeft(qMin(target.right() - minimum, target.left() + delta.x()));
    }
    if (_handle == 1 || _handle == 2 || _handle == 5)
    {
        target.setRight(qMax(target.left() + minimum, target.right() + delta.x()));
    }
    if (_handle == 0 || _handle == 1 || _handle == 4)
    {
        target.setTop(qMin(target.bottom() - minimum, target.top() + delta.y()));
    }
    if (_handle == 2 || _handle == 3 || _handle == 6)
    {
        target.setBottom(qMax(target.top() + minimum, target.bottom() + delta.y()));
    }
    if (CanvasItemEdit::IsClosed(item) && item->BoundTextItem() && (_handle == 5 || _handle == 7))
    {
        target.setHeight(qMax(target.height(), current_height));
    }
    const QPointF scene_anchor = item->mapToScene(OppositeAnchor(_start_bounds, _handle));
    CanvasItemEdit::Resize(item, before, target);
    const QRectF after_bounds = CanvasItemEdit::Bounds(item);
    item->setTransformOriginPoint(after_bounds.center());
    item->setPos(item->pos() + scene_anchor - item->mapToScene(OppositeAnchor(after_bounds, _handle)));
}

bool CanvasSelectionController::Move(QGraphicsSceneMouseEvent* event)
{
    // 1. 未进入手势的移动只刷新鼠标指针，不产生状态或网络操作。
    if (!_is_active)
    {
        return false;
    }
    if (!_scene->isEditable())
    {
        Cancel();
        return true;
    }
    if (_handle >= 0)
    {
        UpdateSingleItem(event->scenePos());
    } else
    {
        const QPointF delta = event->scenePos() - _start_scene_pos;
        for (const CanvasItemEditState& before : _before)
        {
            CanvasItem* item = _scene->FindCanvasItem(before._item_id);
            if (!item)
            {
                continue;
            }
            const QSignalBlocker blocker(item);
            item->setPos(before._position + delta);
        }
    }

    // 2. 同一手势的中间结果由网络层节流，释放时必须另发最终状态。
    NotifyChanges(false);
    Refresh();
    event->accept();
    return true;
}

void CanvasSelectionController::NotifyChanges(bool finished)
{
    // 1. 快照固定为手势起点，混合移动在场景中合并为同一条撤销记录。
    for (const CanvasItemEditState& before : _before)
    {
        CanvasItem* item = _scene->FindCanvasItem(before._item_id);
        if (!item)
        {
            continue;
        }
        const CanvasItemEditState after = CanvasItemEdit::Capture(item);
        if (finished || after != before)
        {
            _scene->SubmitItemEditChange(before, after, _gesture_id, finished);
        }
    }
}

bool CanvasSelectionController::Release(QGraphicsSceneMouseEvent* event)
{
    // 1. 保存最终鼠标位置并先结束内部状态，提交回调不能重复结束手势。
    if (!_is_active || event->button() != Qt::LeftButton)
    {
        return false;
    }
    Move(event);
    _is_active = false;
    NotifyChanges(true);
    _before.clear();
    Refresh();
    event->accept();
    return true;
}

void CanvasSelectionController::Cancel(const QString& item_id, bool notify)
{
    // 1. 目标被远端覆盖时只撤出该目标，混合移动中的其他成员继续完成同一手势。
    if (!_is_active)
    {
        return;
    }
    QVector<CanvasItemEditState> cancelled;
    for (int index = _before.size() - 1; index >= 0; --index)
    {
        if (item_id.isEmpty() || _before[index]._item_id == item_id)
        {
            cancelled.append(_before.takeAt(index));
        }
    }
    if (_before.isEmpty())
    {
        _is_active = false;
    }

    // 2. 先移除内部手势记录再发送回滚，网络回调不能重复取消同一目标。
    for (const CanvasItemEditState& before : cancelled)
    {
        CanvasItemEdit::Apply(_scene->FindCanvasItem(before._item_id), before);
        if (notify)
        {
            _scene->SubmitItemEditChange(before, before, _gesture_id, true);
        }
    }
    Refresh();
}

bool CanvasSelectionController::IsActive() const
{
    // 1. 场景切换模式或撤销前查询是否需要结束当前手势。
    return _is_active;
}

void CanvasSelectionController::Refresh()
{
    // 1. 显式刷新也用于远端应用，不能依赖被阻塞的选择信号。
    if (_overlay)
    {
        _overlay->Refresh();
    }
}

void CanvasSelectionController::SetOverlayVisible(bool visible)
{
    // 1. 导出隐藏辅助层后可安全阻塞场景信号。
    if (_overlay)
    {
        _overlay->setVisible(visible);
    }
}

void CanvasSelectionController::BeforeSceneClear()
{
    // 1. 覆盖层单独回收，不留给 clear 与 QObject 析构重复释放。
    if (!_overlay)
    {
        return;
    }
    if (_overlay->scene())
    {
        _overlay->scene()->removeItem(_overlay);
    }
    delete _overlay;
    _overlay = nullptr;
}

void CanvasSelectionController::AfterSceneClear()
{
    // 1. 场景重新使用时恢复空辅助层，选中状态仍来自内容本身。
    if (_overlay)
    {
        return;
    }
    _overlay = new CanvasSelectionOverlay(_scene);
    _scene->addItem(_overlay);
    Refresh();
}
