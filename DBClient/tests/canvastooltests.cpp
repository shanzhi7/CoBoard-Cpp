/***********************************************************************************
* @file         canvastooltests.cpp
* @brief        工具按钮、字母快捷键及完成绘制后的交互切换测试
* @author       shanzhi
* @date         2026/10/15
* @history
***********************************************************************************/
#include "canvas.h"
#include "canvasgraphicsview.h"
#include "canvastoolbutton.h"
#include "paintscene.h"
#include "servicecommandclient.h"
#include "servicecommandserver.h"
#include "usermgr.h"

#include <memory>

#include <QApplication>
#include <QGraphicsTextItem>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest>

class CanvasToolTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase(); // 使用隔离的图片代理缓存，避免测试启动外部服务。
    void cleanupTestCase(); // 在临时目录销毁前释放代理线程和客户端连接。
    void ToolShortcuts_data(); // 覆盖需求约定的十个字母及对应工具按钮。
    void ToolShortcuts(); // 验证实际按键、按钮高亮、角标和场景模式一致。
    void InputFocusIsolation(); // 聊天及字号输入不能触发工具快捷键。
    void TextEditingKeepsLetters(); // 原生文字与输入法编辑期间不能截获字母键。
    void ModifiersAndAutoRepeat(); // 保留组合快捷键并避免长按重复切换工具。
    void ToolSwitchPreservesViewport(); // 手形和绘图切换保持视口缩放及滚动位置。
    void DrawingReturnsToSelection_data(); // 覆盖所有已有绘图工具的自然完成。
    void DrawingReturnsToSelection(); // 松开鼠标完成图元后只切换一次并保持 END 信号。

private:
    CanvasGraphicsView* PrepareCanvas(Canvas* canvas); // 显示离线画布并使视图获得键盘焦点。

    QTemporaryDir _temporary_directory; // 测试专属缓存根目录。
    std::unique_ptr<ServiceCommandServer> _asset_service; // 测试进程拥有的图片代理。
};

void CanvasToolTests::initTestCase()
{
    // 1. 初始化测试用户和隔离缓存，Canvas 构造时可直接连接本进程代理。
    QVERIFY(_temporary_directory.isValid());
    QCoreApplication::instance()->setProperty("assetServiceRoot", _temporary_directory.path() + QStringLiteral("/cache"));
    _asset_service = std::make_unique<ServiceCommandServer>();
    QCOMPARE(_asset_service->Start(), ServiceStartResult::Started);
    auto user_info = std::make_shared<UserInfo>();
    user_info->_id = 7;
    user_info->_name = QStringLiteral("工具测试用户");
    UserMgr::getInstance()->setMyInfo(user_info);
}

void CanvasToolTests::cleanupTestCase()
{
    // 1. 先解除客户端连接，再停止代理，保证线程不访问已销毁的临时目录。
    ServiceCommandClient::getInstance()->Disconnect();
    _asset_service->Stop();
    _asset_service.reset();
}

CanvasGraphicsView* CanvasToolTests::PrepareCanvas(Canvas* canvas)
{
    // 1. 使用实际窗口和视图事件路径验证工具行为，不直接调用私有切换槽。
    canvas->enterOfflineMode();
    canvas->resize(1000, 720);
    canvas->show();
    canvas->activateWindow();
    auto* view = canvas->findChild<CanvasGraphicsView*>(QStringLiteral("graphicsView"));
    if (view)
    {
        view->scene()->setSceneRect(0, 0, 1200, 900);
        view->setFocus();
    }
    QCoreApplication::processEvents();
    return view;
}

void CanvasToolTests::ToolShortcuts_data()
{
    // 1. 期望值来自用户约定，独立于生产注册表以发现映射配置错误。
    QTest::addColumn<int>("key");
    QTest::addColumn<QString>("button_name");
    QTest::addColumn<int>("mode");
    QTest::newRow("select") << int(Qt::Key_V) << QStringLiteral("select_tool") << int(CanvasInteractionMode::Select);
    QTest::newRow("hand") << int(Qt::Key_H) << QStringLiteral("hand_tool") << int(CanvasInteractionMode::Pan);
    QTest::newRow("rect") << int(Qt::Key_R) << QStringLiteral("rect_tool") << int(CanvasInteractionMode::Draw);
    QTest::newRow("diamond") << int(Qt::Key_D) << QStringLiteral("diamond_tool") << int(CanvasInteractionMode::Draw);
    QTest::newRow("oval") << int(Qt::Key_O) << QStringLiteral("oval_tool") << int(CanvasInteractionMode::Draw);
    QTest::newRow("arrow") << int(Qt::Key_A) << QStringLiteral("arrow_tool") << int(CanvasInteractionMode::Draw);
    QTest::newRow("line") << int(Qt::Key_L) << QStringLiteral("line_tool") << int(CanvasInteractionMode::Draw);
    QTest::newRow("pen") << int(Qt::Key_P) << QStringLiteral("pen_tool") << int(CanvasInteractionMode::Draw);
    QTest::newRow("text") << int(Qt::Key_T) << QStringLiteral("text_tool") << int(CanvasInteractionMode::Text);
    QTest::newRow("eraser") << int(Qt::Key_E) << QStringLiteral("eraser_tool") << int(CanvasInteractionMode::Draw);
}

void CanvasToolTests::ToolShortcuts()
{
    // 1. 真实按键必须同时切换按钮、场景和视图，图标与角标应能正常加载。
    QFETCH(int, key);
    QFETCH(QString, button_name);
    QFETCH(int, mode);
    Canvas canvas;
    CanvasGraphicsView* view = PrepareCanvas(&canvas);
    QVERIFY(view);
    QTRY_VERIFY(view->hasFocus());
    auto* scene = qobject_cast<PaintScene*>(view->scene());
    auto* button = canvas.findChild<CanvasToolButton*>(button_name);
    QVERIFY(scene && button);
    QSignalSpy shortcut_spy(view, &CanvasGraphicsView::SigToolShortcutRequested);
    QTest::keyClick(view, static_cast<Qt::Key>(key));
    QCOMPARE(shortcut_spy.size(), 1);
    QVERIFY(button->isChecked());
    QCOMPARE(int(view->InteractionMode()), mode);
    QCOMPARE(int(scene->InteractionMode()), mode);
    QCOMPARE(button->ShortcutKey(), QChar(static_cast<ushort>(key)));
    QVERIFY(!button->icon().pixmap(QSize(20, 20)).isNull());
    QCOMPARE(button->focusPolicy(), Qt::NoFocus);

    // 2. 工具按钮点击保留画布焦点，下一次键盘切换仍然可用。
    QTest::mouseClick(button, Qt::LeftButton);
    QVERIFY(view->hasFocus());
}

void CanvasToolTests::InputFocusIsolation()
{
    // 1. 聊天输入与画布快捷键相互独立，普通字母不能改变当前画笔工具。
    Canvas canvas;
    CanvasGraphicsView* view = PrepareCanvas(&canvas);
    QVERIFY(view);
    auto* chat = canvas.findChild<QLineEdit*>(QStringLiteral("input_edit"));
    auto* pen_button = canvas.findChild<CanvasToolButton*>(QStringLiteral("pen_tool"));
    QVERIFY(chat && pen_button);
    QSignalSpy shortcut_spy(view, &CanvasGraphicsView::SigToolShortcutRequested);
    chat->setFocus();
    QTest::keyClicks(chat, "vhrdoalpte");
    QCOMPARE(chat->text(), QStringLiteral("vhrdoalpte"));
    QVERIFY(pen_button->isChecked());
    QCOMPARE(shortcut_spy.size(), 0);

    // 2. 字号控件拥有焦点时只接收自己的输入，不把无效字母转成绘图操作。
    auto* text_button = canvas.findChild<CanvasToolButton*>(QStringLiteral("text_tool"));
    auto* size_box = canvas.findChild<QSpinBox*>(QStringLiteral("text_size_spinbox"));
    QVERIFY(text_button && size_box);
    QTest::mouseClick(text_button, Qt::LeftButton);
    size_box->setFocus();
    QTest::keyClick(size_box, Qt::Key_R);
    QVERIFY(text_button->isChecked());
    QCOMPARE(shortcut_spy.size(), 0);
}

void CanvasToolTests::TextEditingKeepsLetters()
{
    // 1. 各工具字母在文字草稿中都必须成为文本，不能结束编辑或创建其他图元。
    Canvas canvas;
    CanvasGraphicsView* view = PrepareCanvas(&canvas);
    QVERIFY(view);
    auto* scene = qobject_cast<PaintScene*>(view->scene());
    QVERIFY(scene);
    QTest::keyClick(view, Qt::Key_T);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->mapFromScene(QPointF(100, 100)));
    QVERIFY(scene->IsTextEditing());
    QSignalSpy shortcut_spy(view, &CanvasGraphicsView::SigToolShortcutRequested);
    QTest::keyClicks(view, "vhrdoalpte");
    QCOMPARE(shortcut_spy.size(), 0);
    QCOMPARE(scene->ActiveTextItem()->GetTextData()._text, QStringLiteral("vhrdoalpte"));

    // 2. 中文预编辑和确认仍由 Qt 编辑器处理，工具按键分发不触碰输入法事件。
    auto* editor = dynamic_cast<QGraphicsTextItem*>(scene->focusItem());
    QVERIFY(editor);
    QInputMethodEvent preedit(QStringLiteral("候选"), {});
    scene->sendEvent(editor, &preedit);
    QInputMethodEvent committed;
    committed.setCommitString(QStringLiteral("中文"));
    scene->sendEvent(editor, &committed);
    QVERIFY(scene->ActiveTextItem()->GetTextData()._text.endsWith(QStringLiteral("中文")));
    QVERIFY(scene->IsTextEditing());
    QCOMPARE(shortcut_spy.size(), 0);
}

void CanvasToolTests::ModifiersAndAutoRepeat()
{
    // 1. 工具只响应普通字母；已有组合快捷键继续交给视图和场景。
    Canvas canvas;
    CanvasGraphicsView* view = PrepareCanvas(&canvas);
    QVERIFY(view);
    QSignalSpy shortcut_spy(view, &CanvasGraphicsView::SigToolShortcutRequested);
    QTest::keyClick(view, Qt::Key_R, Qt::ControlModifier);
    QTest::keyClick(view, Qt::Key_R, Qt::AltModifier);
    QTest::keyClick(view, Qt::Key_R, Qt::MetaModifier);
    QCOMPARE(shortcut_spy.size(), 0);

    // 2. Shift 输入的大写字母仍可选工具，长按重复事件不重复提交切换。
    view->setFocus();
    QTest::keyClick(view, Qt::Key_R, Qt::ShiftModifier);
    QCOMPARE(shortcut_spy.size(), 1);
    QKeyEvent repeated(QEvent::KeyPress, Qt::Key_R, Qt::NoModifier, QStringLiteral("r"), true);
    QApplication::sendEvent(view, &repeated);
    QCOMPARE(shortcut_spy.size(), 1);
}

void CanvasToolTests::ToolSwitchPreservesViewport()
{
    // 1. 保存实际视口位置和变换，工具切换不能重置用户当前查看的画布区域。
    Canvas canvas;
    CanvasGraphicsView* view = PrepareCanvas(&canvas);
    QVERIFY(view);
    view->scale(1.7, 1.7);
    view->horizontalScrollBar()->setValue(140);
    view->verticalScrollBar()->setValue(90);
    const QTransform transform = view->transform();
    const int horizontal_value = view->horizontalScrollBar()->value();
    const int vertical_value = view->verticalScrollBar()->value();
    QTest::keyClick(view, Qt::Key_H);
    QTest::keyClick(view, Qt::Key_R);
    QCOMPARE(view->transform(), transform);
    QCOMPARE(view->horizontalScrollBar()->value(), horizontal_value);
    QCOMPARE(view->verticalScrollBar()->value(), vertical_value);
}

void CanvasToolTests::DrawingReturnsToSelection_data()
{
    // 1. 几何图元、画笔和橡皮擦的每次完整绘制都需要回到鼠标模式。
    QTest::addColumn<int>("key");
    QTest::newRow("rect") << int(Qt::Key_R);
    QTest::newRow("diamond") << int(Qt::Key_D);
    QTest::newRow("oval") << int(Qt::Key_O);
    QTest::newRow("arrow") << int(Qt::Key_A);
    QTest::newRow("line") << int(Qt::Key_L);
    QTest::newRow("pen") << int(Qt::Key_P);
    QTest::newRow("eraser") << int(Qt::Key_E);
}

void CanvasToolTests::DrawingReturnsToSelection()
{
    // 1. 自然释放鼠标必须先保留一次完整 END，再完成工具和选择状态切换。
    QFETCH(int, key);
    Canvas canvas;
    CanvasGraphicsView* view = PrepareCanvas(&canvas);
    QVERIFY(view);
    auto* scene = qobject_cast<PaintScene*>(view->scene());
    QVERIFY(scene);
    QSignalSpy end_spy(scene, &PaintScene::sigStrokeEnd);
    QTest::keyClick(view, static_cast<Qt::Key>(key));
    const QPoint start = view->mapFromScene(QPointF(100, 100));
    const QPoint end = view->mapFromScene(QPointF(220, 180));
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(view->viewport(), end, 20);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, end);
    QCOMPARE(end_spy.size(), 1);
    QTRY_COMPARE(view->InteractionMode(), CanvasInteractionMode::Select);
    QCOMPARE(scene->InteractionMode(), CanvasInteractionMode::Select);
    auto* select_button = canvas.findChild<CanvasToolButton*>(QStringLiteral("select_tool"));
    QVERIFY(select_button && select_button->isChecked());
}

QTEST_MAIN(CanvasToolTests)
#include "canvastooltests.moc"
