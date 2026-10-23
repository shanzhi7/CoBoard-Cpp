/***********************************************************************************
* @file         canvasinteractiontests.cpp
* @brief        图元选择、几何手势、附属文字和统一撤销的聚焦测试
* @author       shanzhi
* @date         2026/10/15
* @history
***********************************************************************************/
#include "canvasitemeditstate.h"
#include "canvasitemeditcodec.h"
#include "canvasitemeditsync.h"
#include "paintscene.h"
#include "canvasitems/diamonditem.h"
#include "canvasitems/eraseritem.h"
#include "canvasitems/lineitem.h"
#include "canvasitems/imageitem.h"
#include "canvasitems/ovalitem.h"
#include "canvasitems/penitem.h"
#include "canvasitems/rectitem.h"

#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QSignalSpy>
#include <QPainter>
#include <QKeyEvent>

#include <cmath>
#include <QtTest>

namespace
{
void SendMouse(PaintScene& scene, QEvent::Type type, const QPointF& position, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    // 1. 直接向场景发送真实 Qt 鼠标事件，避免测试依赖桌面焦点和物理分辨率。
    QGraphicsSceneMouseEvent event(type);
    event.setScenePos(position);
    event.setButton(type == QEvent::GraphicsSceneMouseMove ? Qt::NoButton : Qt::LeftButton);
    event.setButtons(type == QEvent::GraphicsSceneMouseRelease ? Qt::NoButton : Qt::LeftButton);
    event.setModifiers(modifiers);
    QApplication::sendEvent(&scene, &event);
}

RectItem* AddRect(PaintScene& scene, const QString& id = QStringLiteral("rect"))
{
    // 1. 使用顶层图元和真实描边，模式切换触发场景统一交互配置。
    auto* item = new RectItem(id);
    item->setRect(QRectF(20, 30, 100, 80));
    item->setPen(QPen(Qt::black, 2));
    scene.addItem(item);
    scene.SetInteractionMode(CanvasInteractionMode::Select);
    return item;
}

ImageItem* AddImage(PaintScene& scene)
{
    // 1. 使用内存原图和固定资源元数据，避免测试触发上传、下载或创建撤销。
    QPixmap pixmap(32, 24);
    pixmap.fill(Qt::red);
    ImageItem* item = scene.addImageItem(QStringLiteral("image"), QStringLiteral("asset"),
        QStringLiteral("ref"), QStringLiteral("hash"), QStringLiteral("image/png"),
        pixmap.size(), pixmap, QPointF(30, 40), QSizeF(160, 120), false);
    scene.SetInteractionMode(CanvasInteractionMode::Select);
    return item;
}

}

class CanvasInteractionTests : public QObject
{
    Q_OBJECT
private slots:
    void ImageResize_data(); // 覆盖四边四角、放大缩小和越界最小尺寸。
    void ImageResize(); // 检查普通及旋转仿射图片的局部尺寸和固定锚点。
    void ImageRotationAndMove(); // 连续旋转多圈后仍可移动并保留角度。
    void ImageStateAndPixels(); // 快照恢复和非等比绘制不改变源像素及资源元数据。
    void ImageMouseSyncAndUndo(); // 鼠标预览和最终确认使用图片通用协议并只登记一次撤销。
    void ImageConflictAndCancellation(); // 图片远端冲突、拒绝及取消中止手势并恢复确认状态。
    void ImagePermissionsAndCompatibility(); // 待创建、撤权、回放及预览重试保持兼容。
    void OutlineOnlyAndInteriorMove(); // 首次选中只命中边框，已选内部允许移动。
    void RotationKeepsCenter(); // 顶部圆柄旋转保持场景中心不变。
    void DegenerateLineResize(); // 水平直线角柄改变端点且不存在除零。
    void BoundTextGrowthAndCancel(); // 附属文本增高、重排及取消保持父图元原子性。
    void StandaloneTextWrapKeepsFont(); // 独立文字伸缩改变排版和留白，不改变字号。
    void MixedGestureUndo(); // 混合移动以单条记录恢复全部对象。
    void EraserIsExcluded(); // 白色覆盖笔迹不参与鼠标选择。
    void OriginalPathMapping(); // 反复应用绝对缩放不会累计改变原始路径。
    void ContinuousPreviewAndFinalUndo(); // 不返回预览 ACK 也能连续发送，最终确认只登记一次撤销。
    void PreviewReturnToOrigin(); // 拖回起点的预览和最终提交不会留下远端残影。
    void RemotePreviewCancellationAndExpiry(); // 只读成员支持预览取消及失联恢复。
    void PreviewConflictAndPermissionRollback(); // 竞争提交、撤权和迟到回包不污染确认状态。
};

void CanvasInteractionTests::OutlineOnlyAndInteriorMove()
{
    // 1. 封闭内部首次单击不选中，轮廓点击后内部拖动产生完整操作。
    PaintScene scene;
    RectItem* item = AddRect(scene);
    QVERIFY(!item->shape().contains(QPointF(70, 70)));
    SendMouse(scene, QEvent::GraphicsSceneMousePress, QPointF(70, 70));
    SendMouse(scene, QEvent::GraphicsSceneMouseRelease, QPointF(70, 70));
    QVERIFY(!item->isSelected());
    SendMouse(scene, QEvent::GraphicsSceneMousePress, QPointF(70, 30));
    SendMouse(scene, QEvent::GraphicsSceneMouseRelease, QPointF(70, 30));
    QVERIFY(item->isSelected());
    SendMouse(scene, QEvent::GraphicsSceneMousePress, QPointF(70, 70));
    SendMouse(scene, QEvent::GraphicsSceneMouseMove, QPointF(95, 85));
    SendMouse(scene, QEvent::GraphicsSceneMouseRelease, QPointF(95, 85));
    QCOMPARE(item->pos(), QPointF(25, 15));
    QCOMPARE(scene.NextUndoKind(), PaintScene::UndoKind::ItemEdit);
    scene.UndoLastLocalOperation();
    QCOMPARE(item->pos(), QPointF());
}

void CanvasInteractionTests::RotationKeepsCenter()
{
    // 1. 旋转控制点从顶边外侧移动四分之一圆，局部中心的场景位置保持不变。
    PaintScene scene;
    RectItem* item = AddRect(scene);
    item->setSelected(true);
    const QPointF center = item->mapToScene(item->rect().center());
    SendMouse(scene, QEvent::GraphicsSceneMousePress, QPointF(70, 6));
    SendMouse(scene, QEvent::GraphicsSceneMouseMove, QPointF(134, 70));
    SendMouse(scene, QEvent::GraphicsSceneMouseRelease, QPointF(134, 70));
    QVERIFY(qAbs(item->rotation() - 90.0) < 0.001);
    QVERIFY(QLineF(center, item->mapToScene(item->rect().center())).length() < 0.001);
}

void CanvasInteractionTests::DegenerateLineResize()
{
    // 1. 零高度线的右上角绑定右端点，不能把辅助框高度当成真实线高。
    PaintScene scene;
    auto* item = new LineItem(QStringLiteral("line"));
    item->setLine(QLineF(20, 50, 120, 50));
    item->setPen(QPen(Qt::black, 2));
    scene.addItem(item);
    scene.SetInteractionMode(CanvasInteractionMode::Select);
    item->setSelected(true);
    SendMouse(scene, QEvent::GraphicsSceneMousePress, QPointF(120, 44));
    SendMouse(scene, QEvent::GraphicsSceneMouseMove, QPointF(160, 74));
    SendMouse(scene, QEvent::GraphicsSceneMouseRelease, QPointF(160, 74));
    QCOMPARE(item->line().p1(), QPointF(20, 50));
    QCOMPARE(item->line().p2(), QPointF(160, 80));
    QVERIFY(CanvasItemEdit::IsValid(CanvasItemEdit::Capture(item)));
}

void CanvasInteractionTests::BoundTextGrowthAndCancel()
{
    // 1. 内部双击可直接进入编辑，增长与父图元属于同一份快照。
    PaintScene scene;
    RectItem* item = AddRect(scene);
    const CanvasItemEditState before = CanvasItemEdit::Capture(item);
    SendMouse(scene, QEvent::GraphicsSceneMouseDoubleClick, QPointF(70, 70));
    QVERIFY(scene.IsTextEditing());
    QVERIFY(item->BoundTextItem());
    item->SetBoundTextData(QStringLiteral("第一行\n第二行\n第三行\n第四行\n第五行"), TextStyle());
    QVERIFY(item->rect().height() > before._geometry[3]);
    QVERIFY(scene.FinishTextEditing(true));
    QCOMPARE(item->rect().height(), before._geometry[3]);
    QVERIFY(item->BoundTextItem()->GetTextData()._text.isEmpty());

    // 2. 提交后整条撤销同时移除内容与自动增加的高度。
    SendMouse(scene, QEvent::GraphicsSceneMouseDoubleClick, QPointF(70, 70));
    item->SetBoundTextData(QStringLiteral("第一行\n第二行\n第三行\n第四行\n第五行"), TextStyle());
    QVERIFY(scene.FinishTextEditing());
    QCOMPARE(scene.NextUndoKind(), PaintScene::UndoKind::ItemEdit);
    scene.UndoLastLocalOperation();
    QCOMPARE(item->rect().height(), before._geometry[3]);
    QVERIFY(item->BoundTextItem()->GetTextData()._text.isEmpty());
}

void CanvasInteractionTests::StandaloneTextWrapKeepsFont()
{
    // 1. 宽度改变使相同文本重排，高度参数仅保留额外空白。
    TextItem item(QStringLiteral("text"));
    TextStyle style;
    style._font_size = 24;
    item.SetTextData({QStringLiteral("text"), QStringLiteral("一个可以根据图元宽度自动换行的中文文本内容"), style, QPointF()});
    item.SetLayoutSize(QSizeF(100, 0));
    const qreal narrow_height = item.ContentHeight();
    item.SetLayoutSize(QSizeF(300, 200));
    QVERIFY(item.ContentHeight() < narrow_height);
    QCOMPARE(item.GetTextData()._style._font_size, 24);
    QVERIFY(item.boundingRect().height() >= 200.0);
}

void CanvasInteractionTests::MixedGestureUndo()
{
    // 1. 两个对象相同手势的确认合并，撤销不会只移动一半选择。
    PaintScene scene;
    RectItem* first = AddRect(scene, QStringLiteral("first"));
    RectItem* second = AddRect(scene, QStringLiteral("second"));
    const CanvasItemEditState first_before = CanvasItemEdit::Capture(first);
    const CanvasItemEditState second_before = CanvasItemEdit::Capture(second);
    first->setPos(30, 40);
    second->setPos(30, 40);
    scene.RecordItemEditOperation(first_before, CanvasItemEdit::Capture(first), QStringLiteral("gesture"), 1);
    scene.RecordItemEditOperation(second_before, CanvasItemEdit::Capture(second), QStringLiteral("gesture"), 1);
    scene.UndoLastLocalOperation();
    QCOMPARE(first->pos(), QPointF());
    QCOMPARE(second->pos(), QPointF());
    QCOMPARE(scene.NextUndoKind(), PaintScene::UndoKind::None);
}

void CanvasInteractionTests::EraserIsExcluded()
{
    // 1. 橡皮擦仍是覆盖笔迹，点击不会把它变成可移动内容。
    PaintScene scene;
    auto* item = new EraserItem(QStringLiteral("eraser"));
    QPainterPath path;
    path.moveTo(20, 50);
    path.lineTo(120, 50);
    item->setPath(path);
    item->setPen(QPen(Qt::white, 20));
    scene.addItem(item);
    scene.SetInteractionMode(CanvasInteractionMode::Select);
    SendMouse(scene, QEvent::GraphicsSceneMousePress, QPointF(70, 50));
    SendMouse(scene, QEvent::GraphicsSceneMouseRelease, QPointF(70, 50));
    QVERIFY(!item->isSelected());
}

void CanvasInteractionTests::OriginalPathMapping()
{
    // 1. 绝对映射重复应用结果一致，原始路径不被缩放覆盖。
    PenItem item(QStringLiteral("pen"));
    QPainterPath path;
    path.moveTo(0, 0);
    path.lineTo(10, 20);
    item.setPath(path);
    item.SetPathTransform(QTransform::fromScale(2, 3));
    const CanvasItemEditState state = CanvasItemEdit::Capture(&item);
    QVERIFY(CanvasItemEdit::Apply(&item, state));
    QVERIFY(CanvasItemEdit::Apply(&item, state));
    QCOMPARE(item.OriginalPath(), path);
    QCOMPARE(item.path().boundingRect(), QRectF(0, 0, 20, 60));
}


void CanvasInteractionTests::ContinuousPreviewAndFinalUndo()
{
    // 1. 连续产生位置、尺寸和角度变化，不返回 ACK，仍应逐帧发出同版本预览。
    PaintScene scene;
    RectItem* item = AddRect(scene);
    CanvasItemEditSync sync(&scene);
    sync.Configure(QStringLiteral("123456"), 7, false, true);
    QSignalSpy sent(&sync, &CanvasItemEditSync::SigSendOperation);
    const CanvasItemEditState before = CanvasItemEdit::Capture(item);
    QString operation_id;
    for (int index = 1; index <= 3; ++index)
    {
        item->setPos(index * 20, index * 10);
        item->setRotation(index * 15);
        item->setRect(QRectF(20, 30, 100 + index * 10, 80));
        scene.SubmitItemEditChange(before, CanvasItemEdit::Capture(item), QStringLiteral("drag"), false);
        QTRY_VERIFY_WITH_TIMEOUT(sent.count() >= index, 200);
        message::ItemEditOperation preview;
        QVERIFY(preview.ParseFromString(sent.at(index - 1).at(0).toByteArray().toStdString()));
        QVERIFY(preview.preview());
        QCOMPARE(preview.expected_revision(), quint64(0));
        if (index == 1)
        {
            operation_id = QString::fromStdString(preview.operation_id());
        }
        QCOMPARE(QString::fromStdString(preview.operation_id()), operation_id);
        QCOMPARE(preview.state().x(), double(index * 20));
        QCOMPARE(sync.Revision(QStringLiteral("rect")), quint64(0));
        QCOMPARE(scene.NextUndoKind(), PaintScene::UndoKind::None);
    }

    // 2. 松手提交一次最终状态，延迟确认期间既不重发也不提前登记撤销。
    item->setPos(85, 45);
    const CanvasItemEditState after = CanvasItemEdit::Capture(item);
    scene.SubmitItemEditChange(before, after, QStringLiteral("drag"), true);
    QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 4, 200);
    message::ItemEditOperation final;
    QVERIFY(final.ParseFromString(sent.at(3).at(0).toByteArray().toStdString()));
    QVERIFY(!final.preview());
    QCOMPARE(final.operation_id(), operation_id.toStdString());
    QTest::qWait(96);
    QCOMPARE(sent.count(), 4);
    QVERIFY(sync.HasPending());
    QCOMPARE(scene.NextUndoKind(), PaintScene::UndoKind::None);
    message::ItemEditOperationRsp confirmation;
    *confirmation.mutable_operation() = final;
    confirmation.mutable_operation()->set_revision(1);
    *confirmation.mutable_current_state() = final.state();
    sync.ApplyResponse(confirmation);
    QVERIFY(!sync.HasPending());
    QCOMPARE(sync.Revision(QStringLiteral("rect")), quint64(1));

    // 3. 一次完整拖动只产生一条撤销，撤销仍使用最终确认版本并等待自己的确认。
    CanvasItemEditUndoOperation undo;
    QVERIFY(scene.TakeItemEditUndo(&undo));
    QCOMPARE(undo._before.size(), 1);
    QVERIFY(undo._before.first() == before);
    QVERIFY(undo._after.first() == after);
    QCOMPARE(undo._revisions.value(QStringLiteral("rect")), quint64(1));
    QCOMPARE(scene.NextUndoKind(), PaintScene::UndoKind::None);
    sync.Undo(undo);
    QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 5, 200);
    message::ItemEditOperation undo_request;
    QVERIFY(undo_request.ParseFromString(sent.at(4).at(0).toByteArray().toStdString()));
    QVERIFY(!undo_request.preview());
    QCOMPARE(undo_request.expected_revision(), quint64(1));
    *confirmation.mutable_operation() = undo_request;
    confirmation.mutable_operation()->set_revision(2);
    *confirmation.mutable_current_state() = undo_request.state();
    sync.ApplyResponse(confirmation);
    QVERIFY(CanvasItemEdit::Capture(item) == before);
    QVERIFY(!sync.HasPending());
    QCOMPARE(scene.NextUndoKind(), PaintScene::UndoKind::None);
}

void CanvasInteractionTests::PreviewReturnToOrigin()
{
    // 1. 拖动后回到起点也必须发送覆盖帧，其他客户端不能停在上一帧位置。
    PaintScene scene;
    RectItem* item = AddRect(scene);
    CanvasItemEditSync sync(&scene);
    sync.Configure(QStringLiteral("123456"), 7, false, true);
    QSignalSpy sent(&sync, &CanvasItemEditSync::SigSendOperation);
    const CanvasItemEditState before = CanvasItemEdit::Capture(item);
    item->setPos(40, 20);
    scene.SubmitItemEditChange(before, CanvasItemEdit::Capture(item), QStringLiteral("return"), false);
    QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 1, 200);
    item->setPos(0, 0);
    scene.SubmitItemEditChange(before, before, QStringLiteral("return"), false);
    QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 2, 200);
    message::ItemEditOperation returned;
    QVERIFY(returned.ParseFromString(sent.at(1).at(0).toByteArray().toStdString()));
    QVERIFY(returned.preview() && returned.fields() != 0);
    QCOMPARE(returned.state().x(), 0.0);

    // 2. 即便最终位置等于起点，仍有一次最终确认结束预览，但没有无效撤销记录。
    scene.SubmitItemEditChange(before, before, QStringLiteral("return"), true);
    QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 3, 200);
    message::ItemEditOperation final;
    QVERIFY(final.ParseFromString(sent.at(2).at(0).toByteArray().toStdString()));
    QVERIFY(!final.preview());
    message::ItemEditOperationRsp response;
    *response.mutable_operation() = final;
    response.mutable_operation()->set_revision(1);
    *response.mutable_current_state() = final.state();
    sync.ApplyResponse(response);
    QVERIFY(!sync.HasPending());
    QCOMPARE(scene.NextUndoKind(), PaintScene::UndoKind::None);
}

void CanvasInteractionTests::RemotePreviewCancellationAndExpiry()
{
    // 1. 只读成员连续应用同版本预览，取消必须恢复服务器基线而不是屏幕上的临时状态。
    PaintScene scene;
    RectItem* item = AddRect(scene);
    CanvasItemEditSync sync(&scene);
    sync.Configure(QStringLiteral("123456"), 8, false, false);
    const CanvasItemEditState before = CanvasItemEdit::Capture(item);
    CanvasItemEditState desired = before;
    desired._position = QPointF(80, 40);
    message::ItemEditOperationRsp preview;
    auto* operation = preview.mutable_operation();
    operation->set_uid(7);
    operation->set_room_id("123456");
    operation->set_target_item_id("rect");
    operation->set_operation_id("remote-drag");
    operation->set_preview(true);
    *preview.mutable_current_state() = CanvasItemEditCodec::Encode(desired);
    *preview.mutable_confirmed_state() = CanvasItemEditCodec::Encode(before);
    sync.ApplyResponse(preview);
    QCOMPARE(item->pos(), desired._position);
    desired._position = QPointF(120, 60);
    *preview.mutable_current_state() = CanvasItemEditCodec::Encode(desired);
    sync.ApplyResponse(preview);
    QCOMPARE(item->pos(), desired._position);
    QCOMPARE(sync.Revision(QStringLiteral("rect")), quint64(0));
    QCOMPARE(scene.NextUndoKind(), PaintScene::UndoKind::None);
    message::ItemEditOperationRsp cancel = preview;
    cancel.mutable_operation()->set_cancel_preview(true);
    cancel.mutable_operation()->set_operation_id("old-drag");
    *cancel.mutable_current_state() = CanvasItemEditCodec::Encode(before);
    sync.ApplyResponse(cancel);
    QCOMPARE(item->pos(), desired._position);
    cancel.mutable_operation()->set_operation_id("remote-drag");
    sync.ApplyResponse(cancel);
    QVERIFY(CanvasItemEdit::Capture(item) == before);

    // 2. 无最终提交且没有心跳时，只读客户端也会在五秒内清理失联预览。
    sync.ApplyResponse(preview);
    QCOMPARE(item->pos(), desired._position);
    QTRY_VERIFY_WITH_TIMEOUT(CanvasItemEdit::Capture(item) == before, 5500);
    QCOMPARE(scene.NextUndoKind(), PaintScene::UndoKind::None);
}

void CanvasInteractionTests::PreviewConflictAndPermissionRollback()
{
    // 1. 他人最终提交覆盖本地预览，取消当前手势并恢复新版本，不能生成本地撤销。
    PaintScene scene;
    RectItem* item = AddRect(scene);
    CanvasItemEditSync sync(&scene);
    sync.Configure(QStringLiteral("123456"), 7, false, true);
    QSignalSpy sent(&sync, &CanvasItemEditSync::SigSendOperation);
    const CanvasItemEditState before = CanvasItemEdit::Capture(item);
    item->setPos(40, 20);
    scene.SubmitItemEditChange(before, CanvasItemEdit::Capture(item), QStringLiteral("local"), false);
    QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 1, 200);
    message::ItemEditOperation first_preview;
    QVERIFY(first_preview.ParseFromString(sent.at(0).at(0).toByteArray().toStdString()));
    CanvasItemEditState other = before;
    other._position = QPointF(200, 100);
    message::ItemEditOperationRsp committed;
    committed.mutable_operation()->set_uid(8);
    committed.mutable_operation()->set_room_id("123456");
    committed.mutable_operation()->set_target_item_id("rect");
    committed.mutable_operation()->set_operation_id("other-final");
    committed.mutable_operation()->set_revision(1);
    *committed.mutable_current_state() = CanvasItemEditCodec::Encode(other);
    sync.ApplyResponse(committed);
    QVERIFY(!sync.HasPending());
    QCOMPARE(item->pos(), other._position);
    QCOMPARE(scene.NextUndoKind(), PaintScene::UndoKind::None);
    QCOMPARE(sent.count(), 2);
    message::ItemEditOperation cancel;
    QVERIFY(cancel.ParseFromString(sent.at(1).at(0).toByteArray().toStdString()));
    QVERIFY(cancel.preview() && cancel.cancel_preview() && !cancel.has_state());

    // 2. 旧预览的迟到错误不能取消新手势；撤权应立即发取消并恢复最新确认位置。
    item->setPos(240, 120);
    scene.SubmitItemEditChange(other, CanvasItemEdit::Capture(item), QStringLiteral("new-local"), false);
    QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 3, 200);
    message::ItemEditOperationRsp late_error;
    *late_error.mutable_operation() = first_preview;
    late_error.mutable_operation()->set_revision(1);
    late_error.set_error(message::ItemEditConflict);
    *late_error.mutable_current_state() = CanvasItemEditCodec::Encode(other);
    sync.ApplyResponse(late_error);
    QVERIFY(sync.HasPending());
    QCOMPARE(item->pos(), QPointF(240, 120));
    sync.Configure(QStringLiteral("123456"), 7, false, false);
    QVERIFY(!sync.HasPending());
    QCOMPARE(item->pos(), other._position);
    QCOMPARE(sent.count(), 4);
}


void CanvasInteractionTests::ImageResize_data()
{
    // 1. 每个控制柄覆盖扩大、缩小和跨过对边三种拖动，并在旋转仿射状态下重复。
    QTest::addColumn<int>("handle");
    QTest::addColumn<int>("size_case");
    QTest::addColumn<bool>("rotated");
    for (int handle = 0; handle < 8; ++handle)
    {
        for (int size_case = 0; size_case < 3; ++size_case)
        {
            for (bool rotated : {false, true})
            {
                const QByteArray name = QStringLiteral("handle_%1_size_%2_rotated_%3")
                    .arg(handle).arg(size_case).arg(rotated).toLatin1();
                QTest::newRow(name.constData()) << handle << size_case << rotated;
            }
        }
    }
}

void CanvasInteractionTests::ImageResize()
{
    // 1. 控制柄和固定点由归一化矩形坐标给出，鼠标位置始终从手势开始的场景矩阵计算。
    QFETCH(int, handle);
    QFETCH(int, size_case);
    QFETCH(bool, rotated);
    PaintScene scene;
    ImageItem* item = AddImage(scene);
    item->setSelected(true);
    if (rotated)
    {
        item->setTransform(QTransform(1.2, 0.1, 0.2, 0.9, 7, -3));
        item->setScale(1.3);
        item->setTransformOriginPoint(QPointF(80, 60));
        item->setRotation(37);
    }
    const QPointF handles[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0.5, 0}, {1, 0.5}, {0.5, 1}, {0, 0.5}};
    const QPointF fraction = handles[handle];
    const QPointF anchor_fraction = QPointF(1, 1) - fraction;
    const QPointF local_start(fraction.x() * 160, fraction.y() * 120);
    const QPointF fixed_point = item->mapToScene(QPointF(anchor_fraction.x() * 160, anchor_fraction.y() * 120));
    const QTransform initial_transform = item->sceneTransform();
    const qreal requested_width = size_case == 0 ? 220 : (size_case == 1 ? 80 : -40);
    const qreal requested_height = size_case == 0 ? 150 : (size_case == 1 ? 60 : -30);
    const QPointF delta((fraction.x() * 2 - 1) * (requested_width - 160),
                        (fraction.y() * 2 - 1) * (requested_height - 120));
    const QSizeF expected(fraction.x() == 0.5 ? 160 : qMax(12.0, requested_width),
                          fraction.y() == 0.5 ? 120 : qMax(12.0, requested_height));

    // 2. 多次移动到同一终点不得累计误差，对边或对角的场景位置不能变化。
    SendMouse(scene, QEvent::GraphicsSceneMousePress, initial_transform.map(local_start));
    SendMouse(scene, QEvent::GraphicsSceneMouseMove, initial_transform.map(local_start + delta * 0.5));
    SendMouse(scene, QEvent::GraphicsSceneMouseMove, initial_transform.map(local_start + delta));
    SendMouse(scene, QEvent::GraphicsSceneMouseMove, initial_transform.map(local_start + delta));
    SendMouse(scene, QEvent::GraphicsSceneMouseRelease, initial_transform.map(local_start + delta));
    QVERIFY(qAbs(item->displaySize().width() - expected.width()) < 0.001);
    QVERIFY(qAbs(item->displaySize().height() - expected.height()) < 0.001);
    const QPointF after_fixed = item->mapToScene(QPointF(anchor_fraction.x() * expected.width(), anchor_fraction.y() * expected.height()));
    QVERIFY(QLineF(fixed_point, after_fixed).length() < 0.001);
    QCOMPARE(item->rotation(), rotated ? 37.0 : 0.0);
    QCOMPARE(item->scale(), rotated ? 1.3 : 1.0);

    // 3. 松开后撤销一次即可恢复全部几何，不能留下按帧登记的撤销。
    scene.UndoLastLocalOperation();
    QCOMPARE(item->displaySize(), QSizeF(160, 120));
    QCOMPARE(item->sceneTransform(), initial_transform);
    QCOMPARE(scene.NextUndoKind(), PaintScene::UndoKind::None);
}

void CanvasInteractionTests::ImageRotationAndMove()
{
    // 1. 连续跨越角度分支并转两圈，旋转中心始终固定，最终角度不重置。
    PaintScene scene;
    ImageItem* item = AddImage(scene);
    item->setSelected(true);
    const QPointF center = item->mapToScene(QPointF(80, 60));
    const QPointF start = center + QPointF(0, -84);
    SendMouse(scene, QEvent::GraphicsSceneMousePress, start);
    for (int degrees = 30; degrees <= 720; degrees += 30)
    {
        const qreal radians = degrees * 3.14159265358979323846 / 180;
        SendMouse(scene, QEvent::GraphicsSceneMouseMove, center + QPointF(84 * std::sin(radians), -84 * std::cos(radians)));
        QVERIFY(qAbs(item->rotation() - degrees) < 0.001);
        QVERIFY(QLineF(center, item->mapToScene(QPointF(80, 60))).length() < 0.001);
    }
    SendMouse(scene, QEvent::GraphicsSceneMouseRelease, start);
    QVERIFY(qAbs(item->rotation() - 720) < 0.001);

    // 2. 内部拖动沿场景坐标移动，旋转后的尺寸和角度保持稳定。
    SendMouse(scene, QEvent::GraphicsSceneMousePress, center);
    SendMouse(scene, QEvent::GraphicsSceneMouseMove, center + QPointF(25, 15));
    SendMouse(scene, QEvent::GraphicsSceneMouseRelease, center + QPointF(25, 15));
    QVERIFY(QLineF(center + QPointF(25, 15), item->mapToScene(QPointF(80, 60))).length() < 0.001);
    QVERIFY(qAbs(item->rotation() - 720) < 0.001);
    QCOMPARE(item->displaySize(), QSizeF(160, 120));
}

void CanvasInteractionTests::ImageStateAndPixels()
{
    // 1. 完整快照包含显示尺寸及公共变换，反复恢复不能改变源像素和资源元数据。
    PaintScene scene;
    ImageItem* item = AddImage(scene);
    const QImage pixels = item->pixmap().toImage();
    const qint64 cache_key = item->pixmap().cacheKey();
    item->setDisplaySize(QSizeF(200, 30));
    item->setTransform(QTransform(1.1, 0.2, 0.3, 0.8, 5, 9));
    item->setScale(1.4);
    item->setTransformOriginPoint(QPointF(100, 15));
    item->setRotation(63);
    const CanvasItemEditState snapshot = CanvasItemEdit::Capture(item);
    item->setDisplaySize(QSizeF(45, 300));
    item->setPos(200, 100);
    for (int index = 0; index < 3; ++index)
    {
        QVERIFY(CanvasItemEdit::Apply(item, snapshot));
        QVERIFY(CanvasItemEdit::Capture(item) == snapshot);
    }
    QCOMPARE(item->pixmap().cacheKey(), cache_key);
    QCOMPARE(item->pixmap().toImage(), pixels);
    QCOMPARE(item->originalSize(), QSize(32, 24));
    QCOMPARE(item->assetId(), QStringLiteral("asset"));
    QCOMPARE(item->assetRef(), QStringLiteral("ref"));
    QCOMPARE(item->assetSha256(), QStringLiteral("hash"));
    QCOMPARE(item->mimeType(), QStringLiteral("image/png"));

    // 2. 非等比显示须填满目标矩形，不得沿用原来的等比留白绘制。
    QImage rendered(200, 30, QImage::Format_ARGB32);
    rendered.fill(Qt::transparent);
    QPainter painter(&rendered);
    item->paint(&painter, nullptr);
    painter.end();
    QCOMPARE(rendered.pixelColor(2, 15), QColor(Qt::red));
    QCOMPARE(rendered.pixelColor(197, 15), QColor(Qt::red));
}

void CanvasInteractionTests::ImageMouseSyncAndUndo()
{
    // 1. 真实图片拖动发出通用图片预览，旧图片变换信号不能重复发送。
    PaintScene scene;
    ImageItem* item = AddImage(scene);
    item->setSelected(true);
    CanvasItemEditSync sync(&scene);
    sync.Configure(QStringLiteral("123456"), 7, false, true);
    QSignalSpy sent(&sync, &CanvasItemEditSync::SigSendOperation);
    QSignalSpy legacy(&scene, &PaintScene::sigImageGeometryChanged);
    const CanvasItemEditState before = CanvasItemEdit::Capture(item);
    SendMouse(scene, QEvent::GraphicsSceneMousePress, QPointF(190, 160));
    SendMouse(scene, QEvent::GraphicsSceneMouseMove, QPointF(230, 180));
    QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 1, 200);
    message::ItemEditOperation preview;
    QVERIFY(preview.ParseFromString(sent.at(0).at(0).toByteArray().toStdString()));
    QVERIFY(preview.preview());
    QCOMPARE(preview.state().kind(), message::ITEM_IMAGE);
    QCOMPARE(preview.state().geometry_size(), 2);
    QCOMPARE(preview.state().geometry(0), 200.0);
    QCOMPARE(preview.state().geometry(1), 140.0);

    // 2. 最终确认才登记一次撤销，回放不能形成反馈循环。
    SendMouse(scene, QEvent::GraphicsSceneMouseRelease, QPointF(250, 190));
    const CanvasItemEditState after = CanvasItemEdit::Capture(item);
    QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 2, 200);
    message::ItemEditOperation final;
    QVERIFY(final.ParseFromString(sent.at(1).at(0).toByteArray().toStdString()));
    QVERIFY(!final.preview());
    QCOMPARE(final.state().kind(), message::ITEM_IMAGE);
    QCOMPARE(final.state().geometry(0), 220.0);
    QCOMPARE(final.state().geometry(1), 150.0);
    QCOMPARE(final.operation_id(), preview.operation_id());
    QCOMPARE(scene.NextUndoKind(), PaintScene::UndoKind::None);
    message::ItemEditOperationRsp confirmation;
    *confirmation.mutable_operation() = final;
    confirmation.mutable_operation()->set_revision(1);
    *confirmation.mutable_current_state() = final.state();
    sync.ApplyResponse(confirmation);
    sync.ApplyResponse(confirmation);
    CanvasItemEditUndoOperation undo;
    QVERIFY(scene.TakeItemEditUndo(&undo));
    QVERIFY(undo._before.first() == before);
    QVERIFY(undo._after.first() == after);
    QCOMPARE(scene.NextUndoKind(), PaintScene::UndoKind::None);
    QCOMPARE(legacy.count(), 0);
    QTest::qWait(40);
    QCOMPARE(sent.count(), 2);

    // 3. 在线撤销同样使用通用协议，确认后精确恢复图片尺寸、位置和旋转原点。
    sync.Undo(undo);
    QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 3, 200);
    message::ItemEditOperation undo_request;
    QVERIFY(undo_request.ParseFromString(sent.at(2).at(0).toByteArray().toStdString()));
    QCOMPARE(undo_request.state().kind(), message::ITEM_IMAGE);
    QCOMPARE(undo_request.expected_revision(), quint64(1));
    *confirmation.mutable_operation() = undo_request;
    confirmation.mutable_operation()->set_revision(2);
    *confirmation.mutable_current_state() = undo_request.state();
    sync.ApplyResponse(confirmation);
    QVERIFY(CanvasItemEdit::Capture(item) == before);
    QVERIFY(!sync.HasPending());
}

void CanvasInteractionTests::ImageConflictAndCancellation()
{
    // 1. 图片缩放预览遭遇远端提交时结束本地手势，松手不能覆盖已经确认的新版本。
    PaintScene scene;
    ImageItem* item = AddImage(scene);
    item->setSelected(true);
    CanvasItemEditSync sync(&scene);
    sync.Configure(QStringLiteral("123456"), 7, false, true);
    QSignalSpy sent(&sync, &CanvasItemEditSync::SigSendOperation);
    const CanvasItemEditState before = CanvasItemEdit::Capture(item);
    SendMouse(scene, QEvent::GraphicsSceneMousePress, QPointF(190, 160));
    SendMouse(scene, QEvent::GraphicsSceneMouseMove, QPointF(230, 180));
    QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 1, 200);
    CanvasItemEditState other = before;
    other._geometry = {90, 60};
    other._rotation = 30;
    other._position = QPointF(200, 100);
    message::ItemEditOperationRsp response;
    response.mutable_operation()->set_uid(8);
    response.mutable_operation()->set_room_id("123456");
    response.mutable_operation()->set_target_item_id("image");
    response.mutable_operation()->set_operation_id("other-final");
    response.mutable_operation()->set_revision(1);
    *response.mutable_current_state() = CanvasItemEditCodec::Encode(other);
    sync.ApplyResponse(response);
    SendMouse(scene, QEvent::GraphicsSceneMouseRelease, QPointF(250, 190));
    QVERIFY(CanvasItemEdit::Capture(item) == other);
    QVERIFY(!sync.HasPending());
    QCOMPARE(sent.count(), 2);
    QCOMPARE(scene.NextUndoKind(), PaintScene::UndoKind::None);

    // 2. 服务端拒绝当前图片移动后恢复最近确认状态，不创建撤销且不能继续原手势。
    const QPointF center = item->mapToScene(QPointF(45, 30));
    SendMouse(scene, QEvent::GraphicsSceneMousePress, center);
    SendMouse(scene, QEvent::GraphicsSceneMouseMove, center + QPointF(30, 20));
    QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 3, 200);
    message::ItemEditOperation request;
    QVERIFY(request.ParseFromString(sent.at(2).at(0).toByteArray().toStdString()));
    *response.mutable_operation() = request;
    response.mutable_operation()->set_revision(1);
    response.set_error(message::ItemEditConflict);
    sync.ApplyResponse(response);
    SendMouse(scene, QEvent::GraphicsSceneMouseRelease, center + QPointF(50, 20));
    QVERIFY(CanvasItemEdit::Capture(item) == other);
    QVERIFY(!sync.HasPending());
    QCOMPARE(scene.NextUndoKind(), PaintScene::UndoKind::None);

    // 3. Esc 主动取消仍发送结束预览，显示状态立即回到手势起点。
    SendMouse(scene, QEvent::GraphicsSceneMousePress, center);
    SendMouse(scene, QEvent::GraphicsSceneMouseMove, center + QPointF(20, 10));
    QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 5, 200);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&scene, &escape);
    QVERIFY(CanvasItemEdit::Capture(item) == other);
    SendMouse(scene, QEvent::GraphicsSceneMouseRelease, center + QPointF(20, 10));
    QVERIFY(CanvasItemEdit::Capture(item) == other);
}

void CanvasInteractionTests::ImagePermissionsAndCompatibility()
{
    // 1. 未确认创建和只读图片仍可选中，但不能改变几何；撤权中止手势并恢复起点。
    PaintScene scene;
    ImageItem* item = AddImage(scene);
    const CanvasItemEditState before = CanvasItemEdit::Capture(item);
    scene.setEditable(false);
    SendMouse(scene, QEvent::GraphicsSceneMousePress, QPointF(110, 100));
    SendMouse(scene, QEvent::GraphicsSceneMouseRelease, QPointF(110, 100));
    QVERIFY(item->isSelected());
    scene.setEditable(true);
    scene.SetItemEditPending(item->itemId(), true);
    SendMouse(scene, QEvent::GraphicsSceneMousePress, QPointF(190, 160));
    SendMouse(scene, QEvent::GraphicsSceneMouseRelease, QPointF(240, 190));
    QVERIFY(CanvasItemEdit::Capture(item) == before);
    scene.SetItemEditPending(item->itemId(), false);
    SendMouse(scene, QEvent::GraphicsSceneMousePress, QPointF(190, 160));
    SendMouse(scene, QEvent::GraphicsSceneMouseMove, QPointF(240, 190));
    QVERIFY(CanvasItemEdit::Capture(item) != before);
    scene.setEditable(false);
    SendMouse(scene, QEvent::GraphicsSceneMouseRelease, QPointF(240, 190));
    QVERIFY(CanvasItemEdit::Capture(item) == before);

    // 2. 旧 ImageOperation 接收入口保留，通用回放不触发本地编辑信号。
    QSignalSpy changed(&scene, &PaintScene::SigItemEditChanged);
    QSignalSpy legacy(&scene, &PaintScene::sigImageGeometryChanged);
    QVERIFY(scene.updateImageTransform(item->itemId(), QPointF(70, 80), QSizeF(90, 60), 1.2, 0.8, 45));
    QCOMPARE(item->pos(), QPointF(70, 80));
    QCOMPARE(item->displaySize(), QSizeF(90, 60));
    QCOMPARE(item->rotation(), 45.0);
    QVERIFY(scene.ApplyItemEditState(before));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(legacy.count(), 0);

    // 3. 图片下载后保持自定义显示尺寸，成功预览和失败重试仍经过原有双击入口。
    scene.setEditable(true);
    item->setLoadState(ImageItem::ImageLoadState::Loading);
    item->setDisplaySize(QSizeF(200, 50));
    item->setPixmap(item->pixmap());
    QCOMPARE(item->displaySize(), QSizeF(200, 50));
    QSignalSpy preview(&scene, &PaintScene::sigImagePreviewRequested);
    QSignalSpy retry(&scene, &PaintScene::sigImageRetryRequested);
    SendMouse(scene, QEvent::GraphicsSceneMouseDoubleClick, QPointF(110, 65));
    QCOMPARE(preview.count(), 1);
    item->setLoadState(ImageItem::ImageLoadState::Failed);
    SendMouse(scene, QEvent::GraphicsSceneMouseDoubleClick, QPointF(110, 65));
    QCOMPARE(retry.count(), 1);
    QVERIFY(scene.removeImageItem(item->itemId()));
    QVERIFY(!scene.findImageItem(QStringLiteral("image")));
}

QTEST_MAIN(CanvasInteractionTests)
#include "canvasinteractiontests.moc"
