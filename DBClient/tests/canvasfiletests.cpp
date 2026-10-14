/***********************************************************************************
* @file         canvasfiletests.cpp
* @brief        画布文件往返、资源内嵌和图片导出聚焦测试
* @author       shanzhi
* @date         2026/10/08
* @history
***********************************************************************************/
#include "assetserviceprotocol.h"
#include "canvasfilemanager.h"
#include "canvas.h"
#include "canvasitemserializer.h"
#include "canvasitems/arrowitem.h"
#include "canvasitems/diamonditem.h"
#include "canvasitems/eraseritem.h"
#include "canvasitems/imageitem.h"
#include "canvasitems/lineitem.h"
#include "canvasitems/ovalitem.h"
#include "canvasitems/penitem.h"
#include "canvasitems/rectitem.h"
#include "canvasitems/textitem.h"
#include "canvasgraphicsview.h"
#include "imageassetmanager.h"
#include "servicecommandserver.h"
#include "servicecommandclient.h"
#include "usermgr.h"

#include <QApplication>
#include <QAction>
#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QImageReader>
#include <QGraphicsEllipseItem>
#include <QGraphicsView>
#include <QGraphicsTextItem>
#include <QClipboard>
#include <QInputMethodEvent>
#include <QMimeData>
#include <QSpinBox>
#include <QToolButton>
#include <QTextCursor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

class CanvasFileTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase(); // 为测试资源代理创建隔离缓存目录。
    void cleanupTestCase(); // 回收测试代理和缓存线程。
    void DocumentRoundTrip(); // 往返全部几何图元、变换、背景和堆叠顺序。
    void EmbeddedImageRoundTrip(); // 验证图片资源随画布文件内嵌并能恢复图元。
    void RejectConnectionsWithoutChangingSource(); // 非空连接文件失败且原场景保持不变。
    void ExportPngAndJpg(); // 检查全画布尺寸、PNG 透明度和 JPG 白底。
    void ReplacementAndMenuState(); // 验证失败不替换、重复导入及在线菜单权限。
    void InvalidFiles_data(); // Qt 数据驱动测试约定使用 _data 后缀。
    void InvalidFiles(); // 通过窗口文件回调验证导入失败不替换旧场景。
    void CancelReplacementAndStaleImport(); // 验证取消确认及迟到导入结果不替换场景。
    void FailedWritePreservesTarget(); // 失败导出不得覆盖既有目标文件。
    void TextEditingKeys(); // 验证原生输入法、纯文本粘贴和编辑快捷键。
    void TextUndoAndStyle(); // 验证完整编辑撤销、取消和移动位置保留。
    void TextFileAndImageExport(); // 验证文字文件往返和导出不含编辑反馈。
    void TextOnlineResponses(); // 验证确认、冲突、过期响应及断线草稿清理。
    void TextOnlineUndo(); // 验证在线确认后的修改撤销及创建撤销。
    void TextHistoryOrder(); // 验证不同图元历史保持收到的堆叠顺序。

private:
    void AddShapeItems(PaintScene* scene); // 准备各类画布图元供往返测试使用。
    bool ExportAndImport(const CanvasDocument& document, CanvasDocument* restored_document, QString* file_path); // 通过异步文件接口完成一轮读写。

    QTemporaryDir _temporary_directory; // 测试输出与代理缓存的隔离根目录。
    std::unique_ptr<ServiceCommandServer> _asset_service; // 本进程测试图片代理，避免启动外部程序。
};

void CanvasFileTests::initTestCase()
{
    // 1. 单元测试使用独立缓存目录，不触碰用户图片缓存。
    QVERIFY(_temporary_directory.isValid());
    QCoreApplication::instance()->setProperty("assetServiceRoot", _temporary_directory.path() + QStringLiteral("/cache"));
    qRegisterMetaType<CanvasDocument>();
    _asset_service = std::make_unique<ServiceCommandServer>();
    QCOMPARE(_asset_service->Start(), ServiceStartResult::Started);
    auto user_info = std::make_shared<UserInfo>();
    user_info->_id = 7;
    user_info->_name = QStringLiteral("文字测试用户");
    UserMgr::getInstance()->setMyInfo(user_info);
}

void CanvasFileTests::cleanupTestCase()
{
    // 1. 先关闭测试 IPC 客户端，再回收代理线程和隔离缓存。
    ServiceCommandClient::getInstance()->Disconnect();
    _asset_service->Stop();
    _asset_service.reset();
}

void CanvasFileTests::AddShapeItems(PaintScene* scene)
{
    // 1. 笔和橡皮擦直接使用局部路径，验证完整路径及橡皮擦白色画笔往返。
    QPainterPath path;
    path.moveTo(2.5, 3.5);
    path.lineTo(24.0, 36.0);
    path.cubicTo(20.0, 40.0, 45.0, 30.0, 50.0, 55.0);
    path.setFillRule(Qt::WindingFill);
    auto* pen_item = new PenItem(QStringLiteral("pen-id"));
    pen_item->setPath(path);
    pen_item->setPen(QPen(QColor(20, 70, 220, 180), 5, Qt::DashLine, Qt::RoundCap, Qt::BevelJoin));
    pen_item->setPos(12.0, 9.0);
    pen_item->setRotation(18.0);
    pen_item->setOpacity(0.8);
    scene->addItem(pen_item);

    auto* eraser_item = new EraserItem(QStringLiteral("eraser-id"));
    eraser_item->setPath(path);
    eraser_item->setPen(QPen(Qt::white, 14, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    eraser_item->setZValue(2.0);
    scene->addItem(eraser_item);

    // 2. 加入全部几何图元并使用不同层级，确保文件顺序来自场景而非 ID 字典顺序。
    auto* rect_item = new RectItem(QStringLiteral("rect-id"));
    rect_item->setRect(QRectF(1.0, 2.0, 35.0, 20.0));
    rect_item->setPen(QPen(Qt::red, 3, Qt::DotLine));
    rect_item->setPos(40.0, 8.0);
    rect_item->setTransformOriginPoint(QPointF(8.0, 7.0));
    rect_item->setTransform(QTransform::fromScale(1.2, 0.8));
    rect_item->setZValue(-1.0);
    scene->addItem(rect_item);

    auto* oval_item = new OvalItem(QStringLiteral("oval-id"));
    oval_item->setRect(QRectF(4.0, 5.0, 26.0, 18.0));
    scene->addItem(oval_item);

    auto* line_item = new LineItem(QStringLiteral("line-id"));
    line_item->setLine(QLineF(3.0, 8.0, 50.0, 24.0));
    scene->addItem(line_item);

    auto* arrow_item = new ArrowItem(QStringLiteral("arrow-id"));
    arrow_item->SetLine(QLineF(4.0, 9.0, 38.0, 41.0));
    scene->addItem(arrow_item);

    auto* diamond_item = new DiamondItem(QStringLiteral("diamond-id"));
    diamond_item->SetRect(QRectF(5.0, 6.0, 30.0, 44.0));
    scene->addItem(diamond_item);
}

bool CanvasFileTests::ExportAndImport(const CanvasDocument& document,
                                      CanvasDocument* restored_document,
                                      QString* file_path)
{
    // 1. 文件管理器共用测试目录中的图片代理和异步 Qt 文件任务。
    ImageAssetManager asset_manager;
    CanvasFileManager file_manager(&asset_manager);
    *file_path = _temporary_directory.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".synccanvas"));
    const QString export_request_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSignalSpy export_spy(&file_manager, &CanvasFileManager::sigExportFinished);
    file_manager.ExportCanvasAsync(document, *file_path, export_request_id);
    if (!export_spy.wait(10000) || export_spy.count() != 1 || !export_spy.at(0).at(1).toBool()) return false;

    // 2. 解析结束后资源重新登记，所有完成信号均以 request ID 关联。
    const QString import_request_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSignalSpy import_spy(&file_manager, &CanvasFileManager::sigImportReady);
    file_manager.ImportCanvasAsync(*file_path, import_request_id);
    if (import_spy.isEmpty() && !import_spy.wait(10000)) return false;
    if (import_spy.count() != 1 || !import_spy.at(0).at(2).toString().isEmpty()) return false;
    *restored_document = qvariant_cast<CanvasDocument>(import_spy.at(0).at(1));
    return true;
}

void CanvasFileTests::DocumentRoundTrip()
{
    // 1. 先从实际 QGraphicsScene 构建文档，再通过原子文件接口往返。
    PaintScene source_scene;
    source_scene.setSceneRect(QRectF(0.0, 0.0, 120.0, 80.0));
    source_scene.setBackgroundBrush(QColor(10, 20, 30, 180));
    AddShapeItems(&source_scene);
    CanvasDocument source_document;
    QString error_message;
    QVERIFY2(source_scene.BuildCanvasDocument(&source_document, &error_message), qPrintable(error_message));
    CanvasDocument restored_document;
    QString file_path;
    QVERIFY2(ExportAndImport(source_document, &restored_document, &file_path), "画布文件异步往返失败");

    // 2. 临时场景恢复后再次快照，尺寸、背景、图元顺序和全部类型应保持一致。
    PaintScene restored_scene;
    QVERIFY2(restored_scene.LoadCanvasDocument(restored_document, &error_message), qPrintable(error_message));
    CanvasDocument second_snapshot;
    QVERIFY2(restored_scene.BuildCanvasDocument(&second_snapshot, &error_message), qPrintable(error_message));
    QCOMPARE(second_snapshot._canvas_size, source_document._canvas_size);
    QCOMPARE(second_snapshot._background, source_document._background);
    QCOMPARE(second_snapshot._item_order, source_document._item_order);
    QCOMPARE(second_snapshot._items.size(), 7);
    QCOMPARE(second_snapshot._items, source_document._items);
    QCOMPARE(second_snapshot._items.at(0).value("type").toString(), source_document._items.at(0).value("type").toString());
    QCOMPARE(second_snapshot._items.at(0).value("z_value").toDouble(), source_document._items.at(0).value("z_value").toDouble());
    QVERIFY(!restored_scene.canUndoLocal());
}

void CanvasFileTests::EmbeddedImageRoundTrip()
{
    // 1. 资源清单以原始 PNG 字节、SHA-256 和尺寸描述一张唯一图片。
    QImage pixels(8, 6, QImage::Format_ARGB32);
    pixels.fill(QColor(230, 40, 70, 190));
    QByteArray image_data;
    QBuffer buffer(&image_data);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(pixels.save(&buffer, "PNG"));
    const QString sha256 = QString::fromLatin1(QCryptographicHash::hash(image_data, QCryptographicHash::Sha256).toHex());

    // 2. 两个图片图元引用同一摘要，文件应只保存一份资源并恢复两个引用。
    CanvasDocument document;
    document._canvas_size = QSize(60, 40);
    document._background = Qt::white;
    CanvasAssetData asset;
    asset._asset_sha256 = sha256;
    asset._mime_type = QStringLiteral("image/png");
    asset._original_size = pixels.size();
    asset._data = image_data;
    asset._image = pixels;
    document._asset_dataMap.insert(sha256, asset);
    for (const QString& item_id : {QStringLiteral("image-first"), QStringLiteral("image-second")})
    {
        auto* image_item = new ImageItem(item_id);
        image_item->setAssetMetadata(QString(), QString(), sha256, asset._mime_type);
        image_item->setOriginalSize(pixels.size());
        image_item->setDisplaySize(QSizeF(16.0, 12.0));
        image_item->setPixmap(QPixmap::fromImage(pixels));
        QJsonObject item_data;
        QString error_message;
        QVERIFY2(CanvasItemSerializer::SerializeItem(image_item, &item_data, &document, &error_message), qPrintable(error_message));
        delete image_item;
        document._items.append(item_data);
        document._item_order.append(item_id);
    }

    // 3. 另一份代理缓存重新加载文件后，图元重新引用内嵌资源且原始像素保持不变。
    CanvasDocument restored_document;
    QString file_path;
    QVERIFY2(ExportAndImport(document, &restored_document, &file_path), "含图片资源的画布文件往返失败");
    QCOMPARE(restored_document._asset_dataMap.size(), 1);
    QCOMPARE(restored_document._asset_dataMap.value(sha256)._data, image_data);
    QCOMPARE(restored_document._items.size(), 2);
    PaintScene restored_scene;
    QString error_message;
    QVERIFY2(restored_scene.LoadCanvasDocument(restored_document, &error_message), qPrintable(error_message));
    QVERIFY(!restored_scene.findImageItem(QStringLiteral("image-first"))->pixmap().isNull());
    QVERIFY(!restored_scene.findImageItem(QStringLiteral("image-second"))->pixmap().isNull());

    // 4. 场景快照不持有编码数据，重新导出必须通过已有代理读取原始 PNG。
    CanvasDocument cache_snapshot;
    QVERIFY(restored_scene.BuildCanvasDocument(&cache_snapshot, &error_message));
    QVERIFY(cache_snapshot._asset_dataMap.value(sha256)._data.isEmpty());
    CanvasDocument cache_restored_document;
    QString cache_file_path;
    QVERIFY(ExportAndImport(cache_snapshot, &cache_restored_document, &cache_file_path));
    QCOMPARE(cache_restored_document._asset_dataMap.value(sha256)._data, image_data);
}

void CanvasFileTests::RejectConnectionsWithoutChangingSource()
{
    // 1. 创建一个有效文件后加入不支持的连接关系，读取必须明确失败。
    PaintScene source_scene;
    source_scene.setSceneRect(0.0, 0.0, 90.0, 70.0);
    source_scene.setBackgroundBrush(Qt::white);
    AddShapeItems(&source_scene);
    const int original_item_count = source_scene.items().size();
    CanvasDocument document;
    QString error_message;
    QVERIFY(source_scene.BuildCanvasDocument(&document, &error_message));
    QString file_path;
    CanvasDocument restored_document;
    QVERIFY(ExportAndImport(document, &restored_document, &file_path));
    QFile file(file_path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonDocument json_document = QJsonDocument::fromJson(file.readAll());
    file.close();
    QJsonObject root = json_document.object();
    root.insert(QStringLiteral("connections"), QJsonArray{QJsonObject{{QStringLiteral("from"), "pen-id"}}});
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(QJsonDocument(root).toJson()), QJsonDocument(root).toJson().size());
    file.close();

    // 2. 异步导入报告原因，原场景始终保留原来的全部图元。
    ImageAssetManager asset_manager;
    CanvasFileManager file_manager(&asset_manager);
    QSignalSpy import_spy(&file_manager, &CanvasFileManager::sigImportReady);
    file_manager.ImportCanvasAsync(file_path, QStringLiteral("invalid-connection"));
    if (import_spy.isEmpty()) QVERIFY(import_spy.wait(10000));
    QCOMPARE(import_spy.count(), 1);
    QVERIFY(import_spy.at(0).at(2).toString().contains(QStringLiteral("连接")));
    QCOMPARE(source_scene.items().size(), original_item_count);
}

void CanvasFileTests::ExportPngAndJpg()
{
    // 1. 渲染原画布尺寸，半透明红底及选中状态用于验证视觉反馈处理。
    PaintScene scene;
    scene.setSceneRect(0.0, 0.0, 24.0, 18.0);
    scene.setBackgroundBrush(QColor(255, 0, 0, 128));
    QPixmap pixels(8, 6);
    pixels.fill(Qt::blue);
    auto* item = scene.addImageItem("selected", {}, {}, {}, "image/png", pixels.size(), pixels, QPointF(8.0, 5.0));
    scene.SetInteractionMode(CanvasInteractionMode::Select);
    item->setSelected(true);
    QGraphicsEllipseItem* cursor = nullptr;
    for (QGraphicsItem* graphics_item : scene.items())
    {
        if (auto* ellipse = dynamic_cast<QGraphicsEllipseItem*>(graphics_item)) cursor = ellipse;
    }
    QVERIFY(cursor);
    cursor->setRect(0.0, 0.0, 10.0, 10.0);
    cursor->show();
    QGraphicsView view(&scene);
    view.scale(2.5, 2.5);
    view.centerOn(18.0, 12.0);
    QString error_message;
    const QImage rendered_image = scene.RenderCanvasImage(&error_message);
    QVERIFY2(!rendered_image.isNull(), qPrintable(error_message));
    QCOMPARE(rendered_image.size(), QSize(24, 18));
    QVERIFY(item->isSelected());
    QVERIFY(cursor->isVisible());
    const QColor png_background = rendered_image.pixelColor(0, 0);
    QCOMPARE(png_background.alpha(), 128);
    item->setSelected(false);
    cursor->hide();
    QCOMPARE(scene.RenderCanvasImage(&error_message), rendered_image);

    // 2. 检查 PNG 保留透明背景，JPG 将同一颜色与白底合成。
    ImageAssetManager asset_manager;
    CanvasFileManager file_manager(&asset_manager);
    const auto export_image = [&file_manager, this, &rendered_image](const QString& suffix, const QByteArray& format) {
        const QString output_path = _temporary_directory.filePath(QStringLiteral("canvas.") + suffix);
        const QString request_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QSignalSpy spy(&file_manager, &CanvasFileManager::sigExportFinished);
        file_manager.ExportImageAsync(rendered_image, output_path, format, request_id);
        if (spy.isEmpty() && !spy.wait(10000)) return QImage();
        if (spy.isEmpty() || !spy.at(0).at(1).toBool()) return QImage();
        QImageReader reader(output_path);
        return reader.read();
    };
    const QImage png = export_image(QStringLiteral("png"), QByteArray("png"));
    QCOMPARE(png.size(), rendered_image.size());
    QCOMPARE(png.pixelColor(0, 0).alpha(), 128);
    const QImage jpg = export_image(QStringLiteral("jpg"), QByteArray("jpg"));
    QCOMPARE(jpg.size(), rendered_image.size());
    QCOMPARE(jpg.pixelColor(0, 0).alpha(), 255);
    QVERIFY(jpg.pixelColor(0, 0).green() > 90);
    QVERIFY(jpg.pixelColor(0, 0).blue() > 90);
}

void CanvasFileTests::ReplacementAndMenuState()
{
    // 1. 恢复到一半出现未知类型时，只销毁临时场景，原画布指针和内容保持完整。
    Canvas canvas;
    canvas.enterOfflineMode();
    AddShapeItems(canvas._paintScene);
    canvas._paintScene->setPenColor(Qt::green);
    canvas._paintScene->setPenWidth(7);
    CanvasDocument document;
    QString error_message;
    QVERIFY(canvas._paintScene->BuildCanvasDocument(&document, &error_message));
    PaintScene* original_scene = canvas._paintScene;
    const int original_count = original_scene->items().size();
    CanvasDocument invalid_document = document;
    invalid_document._items.last().insert("type", "unsupported");
    QVERIFY(!canvas.ApplyImportedCanvas(invalid_document, &error_message));
    QCOMPARE(canvas._paintScene, original_scene);
    QCOMPARE(canvas._paintScene->items().size(), original_count);

    // 2. 两次覆盖导入不追加图元，画笔设置和之后新建图片的撤销能力仍可使用。
    QVERIFY(canvas.ApplyImportedCanvas(document, &error_message));
    QVERIFY(canvas.ApplyImportedCanvas(document, &error_message));
    QCOMPARE(canvas._paintScene->items().size(), original_count);
    QCOMPARE(canvas._paintScene->getPenColor(), QColor(Qt::green));
    QCOMPARE(canvas._paintScene->getPenWidth(), 7);
    QVERIFY(!canvas._paintScene->canUndoLocal());
    QPixmap pixels(4, 4);
    pixels.fill(Qt::blue);
    canvas._paintScene->addImageItem("new-image", {}, {}, {}, "image/png", QSize(4, 4), pixels, QPointF());
    QVERIFY(canvas._paintScene->canUndoLocal());
    canvas._paintScene->undoLastLocalItem();
    QCOMPARE(canvas._paintScene->items().size(), original_count);

    // 3. 在线只读状态禁止导入，两个导出入口仍然可用。
    canvas._room_info->offline = false;
    canvas._room_info->can_edit = false;
    canvas.UpdateCanvasFileActions();
    QVERIFY(!canvas.findChild<QAction*>("import_canvas_action")->isEnabled());
    QVERIFY(canvas.findChild<QAction*>("export_canvas_action")->isEnabled());
    QVERIFY(canvas.findChild<QAction*>("export_image_action")->isEnabled());

    // 4. 导出前应用已经收到的远端绘画，历史图元也能出现在快照中。
    canvas._room_info->connected = true;
    message::DrawReq remote_draw;
    remote_draw.set_item_id("remote-rect");
    remote_draw.set_shape(message::SHAPE_RECT);
    remote_draw.set_cmd(message::CMD_START);
    remote_draw.set_color(static_cast<int>(QColor(Qt::red).rgba()));
    remote_draw.set_width(3);
    remote_draw.set_start_x(20.0);
    remote_draw.set_start_y(10.0);
    remote_draw.set_uid(42);
    std::string remote_body;
    QVERIFY(remote_draw.SerializeToString(&remote_body));
    canvas.slot_onDrawBroadcast(QByteArray::fromStdString(remote_body));
    canvas.DrainRemoteCanvasOperations();
    CanvasDocument remote_snapshot;
    QVERIFY(canvas._paintScene->BuildCanvasDocument(&remote_snapshot, &error_message));
    QVERIFY(remote_snapshot._item_order.contains(QStringLiteral("remote-rect")));
}

void CanvasFileTests::InvalidFiles_data()
{
    // 1. 同一有效文档通过最小改动生成各类需要拒绝的文件。
    QTest::addColumn<QString>("failure_case");
    for (const QString& failure_case : {QStringLiteral("json"), QStringLiteral("version"), QStringLiteral("duplicate-id"),
            QStringLiteral("order"), QStringLiteral("connections"), QStringLiteral("type"), QStringLiteral("enum"),
            QStringLiteral("dimensions"), QStringLiteral("structure"), QStringLiteral("item-limit"),
            QStringLiteral("file-limit"), QStringLiteral("image"), QStringLiteral("image-pixels"), QStringLiteral("png"),
            QStringLiteral("text-limit"), QStringLiteral("text-style")})
    {
        QTest::newRow(qPrintable(failure_case)) << failure_case;
    }
}

void CanvasFileTests::InvalidFiles()
{
    // 1. 导入错误通过 Canvas 的实际结果连接返回，原场景保持原指针及图元。
    QFETCH(QString, failure_case);
    Canvas canvas;
    canvas.enterOfflineMode();
    AddShapeItems(canvas._paintScene);
    CanvasDocument document;
    QString error_message;
    QVERIFY(canvas._paintScene->BuildCanvasDocument(&document, &error_message));
    CanvasDocument restored_document;
    QString file_path;
    QVERIFY(ExportAndImport(document, &restored_document, &file_path));
    QFile file(file_path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    QJsonArray items = root.value("items").toArray();
    if (failure_case == "version") root.insert("schema_version", 2);
    else if (failure_case == "duplicate-id") items.append(items.first());
    else if (failure_case == "order") root.insert("item_order", QJsonArray{"wrong-id"});
    else if (failure_case == "connections") root.insert("connections", QJsonArray{QJsonObject{{"from", "pen-id"}}});
    else if (failure_case == "type" || failure_case == "enum")
    {
        QJsonObject item = items.first().toObject();
        if (failure_case == "type")
        {
            item.insert("type", "unknown");
        } else
        {
            QJsonObject pen = item.value("pen").toObject();
            pen.insert("cap", 123);
            item.insert("pen", pen);
        }
        items.replace(0, item);
    } else if (failure_case == "text-limit" || failure_case == "text-style")
    {
        QJsonObject item = items.first().toObject();
        item.insert("type", "text");
        item.insert("text", failure_case == "text-limit" ? QString(16385, 'a') : QStringLiteral("valid"));
        item.insert("font_family", "Microsoft YaHei");
        item.insert("font_size", failure_case == "text-style" ? 145 : 24);
        item.insert("text_color", "#ff000000");
        item.insert("bold", false);
        items.replace(0, item);
    } else if (failure_case == "dimensions")
    {
        QJsonObject canvas_json = root.value("canvas").toObject();
        canvas_json.insert("width", -1);
        root.insert("canvas", canvas_json);
    } else if (failure_case == "item-limit")
    {
        while (items.size() <= CanvasDocumentLimits::MAX_ITEMS) items.append(items.first());
    } else if (failure_case == "image" || failure_case == "image-pixels")
    {
        root.insert("assets", QJsonArray{QJsonObject{{"sha256", QString(64, QChar('a'))}, {"mime_type", "image/png"},
            {"width", failure_case == "image" ? 8 : 4097}, {"height", 6}, {"byte_size", 3}, {"data", "data:image/png;base64,YWJj"}}});
    }
    root.insert("items", items);
    if (failure_case == "duplicate-id")
    {
        QJsonArray order = root.value("item_order").toArray();
        order.append(order.first());
        root.insert("item_order", order);
    }
    if (failure_case == "structure") root.remove("items");
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray contents = failure_case == "json" ? QByteArray("{broken") : QJsonDocument(root).toJson();
    QCOMPARE(file.write(contents), contents.size());
    if (failure_case == "file-limit") QVERIFY(file.resize(CanvasDocumentLimits::MAX_FILE_BYTES + 1));
    file.close();
    if (failure_case == "png")
    {
        const QString png_file_path = file_path + QStringLiteral(".png");
        QVERIFY(QFile::rename(file_path, png_file_path));
        file_path = png_file_path;
    }

    // 2. 失败回包解锁菜单且明确报告原因，普通图片还应指向插入图片。
    PaintScene* original_scene = canvas._paintScene;
    const int item_count = original_scene->items().size();
    canvas._file_request_id = QStringLiteral("invalid-import");
    canvas._file_canvas_generation = canvas._canvas_generation;
    canvas._is_file_operation_running = true;
    QSignalSpy spy(canvas._canvas_file_manager, &CanvasFileManager::sigImportReady);
    canvas._canvas_file_manager->ImportCanvasAsync(file_path, canvas._file_request_id);
    if (spy.isEmpty()) QVERIFY(spy.wait(10000));
    QVERIFY(!spy.first().at(2).toString().isEmpty());
    QCOMPARE(canvas._paintScene, original_scene);
    QCOMPARE(canvas._paintScene->items().size(), item_count);
    QVERIFY(!canvas._is_file_operation_running);
    if (failure_case == "png") QVERIFY(spy.first().at(2).toString().contains(QStringLiteral("插入图片")));
}

void CanvasFileTests::CancelReplacementAndStaleImport()
{
    // 1. 自动拒绝正常文件的确认对话框，旧图元和撤销记录不应被替换。
    Canvas canvas;
    canvas.enterOfflineMode();
    AddShapeItems(canvas._paintScene);
    CanvasDocument document;
    QString error_message;
    QVERIFY(canvas._paintScene->BuildCanvasDocument(&document, &error_message));
    CanvasDocument restored_document;
    QString file_path;
    QVERIFY(ExportAndImport(document, &restored_document, &file_path));
    PaintScene* original_scene = canvas._paintScene;
    QTimer dialog_timer;
    connect(&dialog_timer, &QTimer::timeout, &canvas, [] {
        if (auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) dialog->done(QMessageBox::No);
    });
    dialog_timer.start(10);
    canvas._file_request_id = QStringLiteral("cancel-confirm");
    canvas._file_canvas_generation = canvas._canvas_generation;
    canvas._is_file_operation_running = true;
    QSignalSpy spy(canvas._canvas_file_manager, &CanvasFileManager::sigImportReady);
    canvas._canvas_file_manager->ImportCanvasAsync(file_path, canvas._file_request_id);
    QVERIFY(spy.wait(10000));
    dialog_timer.stop();
    QCOMPARE(canvas._paintScene, original_scene);
    QVERIFY(!canvas._is_file_operation_running);

    // 2. 房间已经切换时，即使旧导入结果到达，也不能再次触发替换对话框。
    canvas._file_request_id = QStringLiteral("stale-import");
    canvas._file_canvas_generation = canvas._canvas_generation;
    ++canvas._canvas_generation;
    canvas._canvas_file_manager->sigImportReady(QStringLiteral("stale-import"), document, QString());
    QCOMPARE(canvas._paintScene, original_scene);
}

void CanvasFileTests::FailedWritePreservesTarget()
{
    // 1. 非法模型写入失败时，已存在的目标内容必须保留。
    const QString file_path = _temporary_directory.filePath(QStringLiteral("existing.synccanvas"));
    QFile existing_file(file_path);
    QVERIFY(existing_file.open(QIODevice::WriteOnly));
    QCOMPARE(existing_file.write("keep-existing"), 13);
    existing_file.close();
    ImageAssetManager asset_manager;
    CanvasFileManager file_manager(&asset_manager);
    QSignalSpy spy(&file_manager, &CanvasFileManager::sigExportFinished);
    file_manager.ExportCanvasAsync(CanvasDocument(), file_path, QStringLiteral("invalid-export"));
    QVERIFY(spy.wait(10000));
    QVERIFY(!spy.first().at(1).toBool());
    QVERIFY(existing_file.open(QIODevice::ReadOnly));
    QCOMPARE(existing_file.readAll(), QByteArray("keep-existing"));
    existing_file.close();

    // 2. 图片编码或目标打开失败都返回失败，不替换已有目标。
    spy.clear();
    QImage image(3, 3, QImage::Format_ARGB32);
    image.fill(Qt::red);
    file_manager.ExportImageAsync(image, file_path, "unknown-codec", QStringLiteral("encoding-failed"));
    QVERIFY(spy.wait(10000));
    QVERIFY(!spy.first().at(1).toBool());
    QVERIFY(existing_file.open(QIODevice::ReadOnly));
    QCOMPARE(existing_file.readAll(), QByteArray("keep-existing"));
    existing_file.close();
    spy.clear();
    file_manager.ExportImageAsync(image, _temporary_directory.path() + "/missing/image.png", "png", QStringLiteral("path-failed"));
    QVERIFY(spy.wait(10000));
    QVERIFY(!spy.first().at(1).toBool());
}

void CanvasFileTests::TextEditingKeys()
{
    // 1. 通过实际工具按钮和视图点击创建文字，空新建取消后不保留图元。
    Canvas canvas;
    canvas.enterOfflineMode();
    canvas.resize(900, 650);
    canvas.show();
    auto* view = canvas.findChild<CanvasGraphicsView*>(QStringLiteral("graphicsView"));
    auto* tool = canvas.findChild<QToolButton*>(QStringLiteral("text_tool"));
    QVERIFY(view && tool);
    canvas._paintScene->setSceneRect(0, 0, 600, 400);
    QTest::mouseClick(tool, Qt::LeftButton);
    QCoreApplication::processEvents();
    const QPoint start = view->mapFromScene(QPointF(30, 30));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QVERIFY(canvas._paintScene->IsTextEditing());
    QString item_id = canvas._paintScene->ActiveTextItem()->itemId();
    QTest::keyClick(view, Qt::Key_Escape);
    QVERIFY(!canvas._paintScene->FindTextItem(item_id));
    QVERIFY(!canvas._paintScene->CanUndoLocalOperation());

    // 2. HTML 和图片同时存在时仍只粘贴纯文本，Ctrl+Z 不进入画布撤销。
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, start);
    TextItem* item = canvas._paintScene->ActiveTextItem();
    QVERIFY(item);
    item_id = item->itemId();
    auto* mime_data = new QMimeData();
    mime_data->setHtml(QStringLiteral("<b>rich</b>"));
    mime_data->setText(QStringLiteral("plain"));
    mime_data->setImageData(QImage(2, 2, QImage::Format_ARGB32));
    QApplication::clipboard()->setMimeData(mime_data);
    QSignalSpy paste_spy(view, &CanvasGraphicsView::sigPasteImageRequested);
    QTest::keyClick(view, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(item->GetTextData()._text, QStringLiteral("plain"));
    QCOMPARE(paste_spy.size(), 0);
    QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(item->GetTextData()._text, QString());
    QVERIFY(canvas._paintScene->FindTextItem(item_id));

    // 3. 中文提交和 Enter 换行由原生文档处理；候选期间不提交整个文字。
    auto* editor = dynamic_cast<QGraphicsTextItem*>(canvas._paintScene->focusItem());
    QVERIFY(editor);
    QInputMethodEvent preedit(QStringLiteral("候选"), {});
    canvas._paintScene->sendEvent(editor, &preedit);
    QTest::keyClick(view, Qt::Key_Return, Qt::ControlModifier);
    QVERIFY(canvas._paintScene->IsTextEditing());
    QInputMethodEvent chinese_input;
    chinese_input.setCommitString(QStringLiteral("中文"));
    canvas._paintScene->sendEvent(editor, &chinese_input);
    QTest::keyClick(view, Qt::Key_Return);
    QTest::keyClicks(view, "second");
    QCOMPARE(item->GetTextData()._text, QStringLiteral("中文\nsecond"));
    QTest::keyClick(view, Qt::Key_Backspace);
    QCOMPARE(item->GetTextData()._text, QStringLiteral("中文\nsecon"));
    QVERIFY(canvas._paintScene->IsTextEditing());
    QTest::keyClick(view, Qt::Key_Return, Qt::ControlModifier);
    QVERIFY(!canvas._paintScene->IsTextEditing());
    QVERIFY(canvas._paintScene->CanUndoLocalOperation());

    // 4. 完成后双击可再次编辑；Esc 只取消草稿，删除键在普通状态删除整图元。
    QTest::mouseClick(canvas.findChild<QToolButton*>("select_tool"), Qt::LeftButton);
    QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->mapFromScene(item->pos() + QPointF(5, 5)));
    QVERIFY(item->IsEditing());
    QTest::keyClicks(view, "draft");
    QTest::keyClick(view, Qt::Key_Escape);
    QCOMPARE(item->GetTextData()._text, QStringLiteral("中文\nsecon"));
    item->setSelected(true);
    const QPointF previous_position = item->pos();
    const QPoint drag_start = view->mapFromScene(item->pos() + QPointF(5, 5));
    QSignalSpy move_spy(canvas._paintScene, &PaintScene::SigTextMoved);
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, drag_start);
    QTest::mouseMove(view->viewport(), drag_start + QPoint(40, 35), 30);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, drag_start + QPoint(40, 35));
    QVERIFY(item->pos() != previous_position);
    QCOMPARE(move_spy.size(), 1);
    QVERIFY(canvas.grab().save(QCoreApplication::applicationDirPath() + QStringLiteral("/text-canvas-check.png")));
    view->setFocus();
    QTest::keyClick(view, Qt::Key_Delete);
    QVERIFY(!canvas._paintScene->FindTextItem(item_id));
    QVERIFY(!canvas._paintScene->CanUndoLocalOperation());
}

void CanvasFileTests::TextUndoAndStyle()
{
    // 1. 编辑内容和样式只登记一次，无变化及 Esc 不增加撤销记录。
    Canvas canvas;
    canvas.enterOfflineMode();
    TextItemData data{QStringLiteral("text-undo"), QString(), TextStyle(), QPointF(10, 20)};
    TextItem* item = canvas._paintScene->AddTextItem(data);
    QVERIFY(canvas._paintScene->BeginTextEditing(item));
    data._text = QStringLiteral("original");
    item->SetTextData(data);
    QVERIFY(canvas._paintScene->FinishTextEditing());
    QVERIFY(canvas._paintScene->BeginTextEditing(item));
    data._text = QStringLiteral("changed");
    data._style._is_bold = true;
    data._style._font_size = 48;
    data._style._color = Qt::red;
    item->SetTextData(data);
    QVERIFY(canvas._paintScene->FinishTextEditing());
    QVERIFY(canvas._paintScene->BeginTextEditing(item));
    QVERIFY(canvas._paintScene->FinishTextEditing());
    QVERIFY(canvas._paintScene->BeginTextEditing(item));
    data._text = QStringLiteral("cancelled");
    item->SetTextData(data);
    QVERIFY(canvas._paintScene->FinishTextEditing(true));

    // 2. 内容样式撤销保留当前位置，第二次撤销才删除最初新建。
    item->setPos(100, 120);
    canvas.OnUndoTriggered();
    QCOMPARE(item->GetTextData()._text, QStringLiteral("original"));
    QVERIFY(item->GetTextData()._style == TextStyle());
    QCOMPARE(item->pos(), QPointF(100, 120));
    canvas.OnUndoTriggered();
    QVERIFY(!canvas._paintScene->FindTextItem(data._item_id));
    QVERIFY(!canvas._paintScene->CanUndoLocalOperation());

    // 3. 控件保持已有文字颜色；字号编辑结束只生成一次修改，取消恢复样式。
    data._text = QStringLiteral("colored");
    item = canvas._paintScene->AddTextItem(data);
    item->setSelected(true);
    canvas.UpdateTextControls();
    auto* size_box = canvas.findChild<QSpinBox*>("text_size_spinbox");
    size_box->setValue(60);
    canvas.OnTextStyleChanged();
    QCOMPARE(item->GetTextData()._style._color, QColor(Qt::red));
    QCOMPARE(item->GetTextData()._style._font_size, 60);
    QVERIFY(canvas._paintScene->BeginTextEditing(item));
    size_box->setValue(90);
    canvas.OnTextStyleChanged();
    QVERIFY(item->IsEditing());
    QVERIFY(canvas._paintScene->FinishTextEditing(true));
    QCOMPARE(item->GetTextData()._style._font_size, 60);
    canvas.OnUndoTriggered();
    QCOMPARE(item->GetTextData()._style._font_size, 48);
    QVERIFY(!canvas._paintScene->CanUndoLocalOperation());

    // 4. 清空已有文字确认后按删除处理，不能撤销恢复已删除文字。
    QVERIFY(canvas._paintScene->BeginTextEditing(item));
    data._text.clear();
    item->SetTextData(data);
    QVERIFY(canvas._paintScene->FinishTextEditing());
    QVERIFY(!canvas._paintScene->FindTextItem(data._item_id));
    QVERIFY(!canvas._paintScene->CanUndoLocalOperation());
}

void CanvasFileTests::TextFileAndImageExport()
{
    // 1. 混合图元保存字体请求、透明颜色、公共变换及原绘制顺序。
    PaintScene scene;
    scene.setSceneRect(0, 0, 300, 180);
    scene.setBackgroundBrush(QColor(255, 255, 255, 80));
    AddShapeItems(&scene);
    TextItemData data{QStringLiteral("saved-text"), QStringLiteral("中文\nsecond line"),
        {QStringLiteral("missing-test-font"), 32, QColor(40, 90, 160, 140), true}, QPointF(20, 30)};
    TextItem* item = scene.AddTextItem(data);
    item->setOpacity(0.7);
    item->setZValue(3);
    item->setTransform(QTransform::fromTranslate(2, 3));
    CanvasDocument document;
    QString error_message;
    QVERIFY(scene.BuildCanvasDocument(&document, &error_message));
    CanvasDocument restored;
    QString file_path;
    QVERIFY(ExportAndImport(document, &restored, &file_path));
    PaintScene loaded;
    QVERIFY(loaded.LoadCanvasDocument(restored, &error_message));
    TextItem* loaded_item = loaded.FindTextItem(data._item_id);
    QVERIFY(loaded_item);
    QCOMPARE(loaded_item->GetTextData()._text, data._text);
    QVERIFY(loaded_item->GetTextData()._style == data._style);
    QCOMPARE(loaded_item->pos(), item->pos());
    QCOMPARE(loaded_item->transform(), item->transform());
    QCOMPARE(loaded_item->opacity(), item->opacity());
    QVERIFY(!loaded.CanUndoLocalOperation());
    CanvasDocument loaded_document;
    QVERIFY(loaded.BuildCanvasDocument(&loaded_document, &error_message));
    QCOMPARE(loaded_document._item_order, document._item_order);

    // 2. 编辑导出完成当前草稿，输出与无焦点、无选择的渲染完全相同。
    QVERIFY(loaded.BeginTextEditing(loaded_item));
    loaded_item->setSelected(true);
    const QImage first = loaded.RenderCanvasImage(&error_message);
    QVERIFY2(!first.isNull(), qPrintable(error_message));
    QVERIFY(!loaded.IsTextEditing());
    QVERIFY(loaded_item->isSelected());
    loaded.clearSelection();
    QCOMPARE(first, loaded.RenderCanvasImage(&error_message));

    // 3. 文件文字限额和样式校验在临时场景构建前拒绝非法输入。
    QJsonObject invalid_item = document._items.last();
    for (const QJsonObject& candidate : document._items)
        if (candidate.value("type").toString() == "text") invalid_item = candidate;
    invalid_item.insert("text", QString(16385, 'a'));
    QVERIFY(!CanvasItemSerializer::ValidateItem(invalid_item, {}, &error_message));
    invalid_item.insert("text", QStringLiteral("valid"));
    invalid_item.insert("font_size", 7);
    QVERIFY(!CanvasItemSerializer::ValidateItem(invalid_item, {}, &error_message));
    invalid_item.insert("font_size", 24);
    invalid_item.insert("bold", QStringLiteral("true"));
    QVERIFY(!CanvasItemSerializer::ValidateItem(invalid_item, {}, &error_message));
}

void CanvasFileTests::TextOnlineResponses()
{
    // 1. 模拟在线确认，未确认创建不能撤销或导出，确认后才登记本地记录。
    Canvas canvas;
    auto room = std::make_shared<RoomInfo>();
    room->id = QStringLiteral("123456");
    room->offline = false;
    room->connected = true;
    room->can_edit = true;
    canvas._room_info = room;
    canvas._paintScene->setEditable(true);
    canvas._paintScene->SetTextUndoEnabled(false);
    TextItemData data{QStringLiteral("online-text"), QStringLiteral("created"), TextStyle(), QPointF(10, 20)};
    TextItem* item = canvas._paintScene->AddTextItem(data);
    canvas.SubmitTextOperation(message::TEXT_CREATE, {data._item_id, QString(), data._style, data._position}, data);
    QVERIFY(!canvas._pending_text_operationMap.isEmpty());
    QVERIFY(!canvas._paintScene->CanUndoLocalOperation());
    QVERIFY(!canvas.PrepareTextExport());
    auto response_for = [&](const QString& item_id, quint64 sequence, const QString& text, message::TextOperationType type) {
        message::TextOperationRsp response;
        auto* operation = response.mutable_operation();
        operation->set_room_id("123456");
        operation->set_target_item_id(item_id.toStdString());
        operation->set_operation_type(type);
        operation->set_server_sequence(sequence);
        if (canvas._pending_text_operationMap.contains(item_id))
            operation->set_operation_id(canvas._pending_text_operationMap.value(item_id)._operation_id.toStdString());
        if (type != message::TEXT_DELETE)
        {
            auto* state = response.mutable_current_item();
            state->set_item_id(item_id.toStdString());
            state->set_text(text.toStdString());
            state->set_font_family("Microsoft YaHei");
            state->set_font_size(24);
            state->set_color(QColor(Qt::black).rgba());
            state->set_x(10);
            state->set_y(20);
        }
        return response;
    };
    canvas.ApplyTextResponse(response_for(data._item_id, 5, "created", message::TEXT_CREATE));
    QVERIFY(canvas._pending_text_operationMap.isEmpty());
    QVERIFY(canvas._paintScene->CanUndoLocalOperation());
    QVERIFY(canvas.PrepareTextExport());

    // 2. 他人修改取消本地草稿并使撤销失效，另一个文字的较小序号仍需应用。
    QVERIFY(canvas._paintScene->BeginTextEditing(item));
    TextItemData draft = data;
    draft._text = QStringLiteral("draft");
    item->SetTextData(draft);
    canvas.ApplyTextResponse(response_for(data._item_id, 7, "remote", message::TEXT_UPDATE));
    QVERIFY(!canvas._paintScene->IsTextEditing());
    QCOMPARE(item->GetTextData()._text, QStringLiteral("remote"));
    QVERIFY(!canvas._paintScene->CanUndoLocalOperation());
    canvas.ApplyTextResponse(response_for("other-text", 2, "other", message::TEXT_CREATE));
    QVERIFY(canvas._paintScene->FindTextItem("other-text"));
    canvas.ApplyTextResponse(response_for(data._item_id, 6, "stale", message::TEXT_UPDATE));
    QCOMPARE(item->GetTextData()._text, QStringLiteral("remote"));

    // 3. 待确认期间不能再次编辑，失败恢复已确认状态，旧失败不能恢复远端已删除文字。
    canvas.SubmitTextOperation(message::TEXT_UPDATE, item->GetTextData(), draft);
    QVERIFY(!canvas._paintScene->BeginTextEditing(item));
    auto failure = response_for(data._item_id, 7, "remote", message::TEXT_UPDATE);
    failure.set_error(message::TextStateConflict);
    failure.set_error_message("文字已改变");
    canvas.ApplyTextResponse(failure);
    QCOMPARE(item->GetTextData()._text, QStringLiteral("remote"));
    canvas.SubmitTextOperation(message::TEXT_UPDATE, item->GetTextData(), draft);
    failure = response_for(data._item_id, 7, "remote", message::TEXT_UPDATE);
    failure.set_error(message::TextStateConflict);
    auto deletion = response_for(data._item_id, 9, QString(), message::TEXT_DELETE);
    deletion.mutable_operation()->set_operation_id("remote-delete");
    canvas.ApplyTextResponse(deletion);
    QVERIFY(!canvas._paintScene->FindTextItem(data._item_id));
    canvas.ApplyTextResponse(failure);
    QVERIFY(!canvas._paintScene->FindTextItem(data._item_id));
    QVERIFY(canvas._pending_text_operationMap.isEmpty());

    // 4. 断线清理恢复已确认文字，收回权限取消活动草稿。
    item = canvas._paintScene->FindTextItem("other-text");
    QVERIFY(canvas._paintScene->BeginTextEditing(item));
    auto other_draft = item->GetTextData();
    other_draft._text = QStringLiteral("cancel on permission");
    item->SetTextData(other_draft);
    canvas._paintScene->setEditable(false);
    QCOMPARE(item->GetTextData()._text, QStringLiteral("other"));
    canvas._paintScene->setEditable(true);
    canvas.SubmitTextOperation(message::TEXT_UPDATE, item->GetTextData(), other_draft);
    item->SetTextData(other_draft);
    canvas.ClearTextOperations();
    QCOMPARE(item->GetTextData()._text, QStringLiteral("other"));
    QVERIFY(canvas._pending_text_operationMap.isEmpty());
}

void CanvasFileTests::TextOnlineUndo()
{
    // 1. 只有成功确认的创建和修改进入在线撤销，移动保持独立。
    Canvas canvas;
    canvas._room_info = std::make_shared<RoomInfo>();
    canvas._room_info->id = "123456";
    canvas._room_info->connected = true;
    canvas._room_info->can_edit = true;
    canvas._paintScene->setEditable(true);
    canvas._paintScene->SetTextUndoEnabled(false);
    TextItemData data{"undo-online", "original", TextStyle(), QPointF(10, 20)};
    TextItem* item = canvas._paintScene->AddTextItem(data);
    auto confirm = [&](quint64 sequence) {
        const auto pending = canvas._pending_text_operationMap.value(data._item_id);
        message::TextOperationRsp response;
        auto* operation = response.mutable_operation();
        operation->set_room_id("123456");
        operation->set_operation_id(pending._operation_id.toStdString());
        operation->set_target_item_id(data._item_id.toStdString());
        operation->set_server_sequence(sequence);
        operation->set_operation_type(pending._type);
        if (pending._type != message::TEXT_DELETE)
        {
            auto* state = response.mutable_current_item();
            state->set_item_id(data._item_id.toStdString());
            state->set_text(pending._after._text.toStdString());
            state->set_font_family(pending._after._style._font_family.toStdString());
            state->set_font_size(pending._after._style._font_size);
            state->set_color(pending._after._style._color.rgba());
            state->set_bold(pending._after._style._is_bold);
            state->set_x(pending._after._position.x());
            state->set_y(pending._after._position.y());
        }
        canvas.ApplyTextResponse(response);
    };
    canvas.SubmitTextOperation(message::TEXT_CREATE, {data._item_id, QString(), data._style, data._position}, data);
    confirm(1);
    auto changed = data;
    changed._text = "changed";
    changed._style._font_size = 48;
    canvas.SubmitTextOperation(message::TEXT_UPDATE, data, changed);
    confirm(2);
    auto moved = changed;
    moved._position = QPointF(100, 120);
    canvas.SubmitTextOperation(message::TEXT_MOVE, changed, moved);
    confirm(3);

    // 2. 修改撤销保留当前位置，并且等待确认期间不能再撤销同一文字。
    canvas.OnUndoTriggered();
    auto pending = canvas._pending_text_operationMap.value(data._item_id);
    QVERIFY(pending._is_undo);
    QCOMPARE(pending._type, message::TEXT_UPDATE);
    QCOMPARE(pending._after._position, moved._position);
    QCOMPARE(pending._after._text, data._text);
    QCOMPARE(pending._after._style._font_size, 24);
    canvas.OnUndoTriggered();
    QCOMPARE(canvas._pending_text_operationMap.value(data._item_id)._operation_id, pending._operation_id);
    confirm(4);
    QCOMPARE(item->GetTextData()._text, data._text);
    QCOMPARE(item->pos(), moved._position);

    // 3. 创建撤销发出删除，成功后不能通过旧快照恢复图元。
    canvas.OnUndoTriggered();
    pending = canvas._pending_text_operationMap.value(data._item_id);
    QVERIFY(pending._is_undo);
    QCOMPARE(pending._type, message::TEXT_DELETE);
    confirm(5);
    QVERIFY(!canvas._paintScene->FindTextItem(data._item_id));
    QVERIFY(!canvas._paintScene->CanUndoLocalOperation());
}

void CanvasFileTests::TextHistoryOrder()
{
    // 1. 房间初始化前交错收到笔画、文字和下一笔画，不能按图元类型重排。
    Canvas canvas;
    message::DrawReq draw;
    draw.set_uid(42);
    draw.set_item_id("first-pen");
    draw.set_cmd(message::CMD_START);
    draw.set_shape(message::SHAPE_PEN);
    std::string body;
    draw.SerializeToString(&body);
    canvas.slot_onDrawBroadcast(QByteArray::fromStdString(body));
    message::TextOperationRsp response;
    auto* operation = response.mutable_operation();
    operation->set_room_id("123456");
    operation->set_target_item_id("middle-text");
    operation->set_server_sequence(1);
    operation->set_operation_type(message::TEXT_CREATE);
    auto* state = response.mutable_current_item();
    state->set_item_id("middle-text");
    state->set_text("middle");
    state->set_font_family("Microsoft YaHei");
    state->set_font_size(24);
    state->set_color(QColor(Qt::black).rgba());
    response.SerializeToString(&body);
    canvas.OnTextOperationReceived(QByteArray::fromStdString(body));
    draw.set_item_id("last-pen");
    draw.SerializeToString(&body);
    canvas.slot_onDrawBroadcast(QByteArray::fromStdString(body));
    canvas._room_info = std::make_shared<RoomInfo>();
    canvas._room_info->id = "123456";
    canvas._room_info->connected = true;
    canvas.flushRemoteDrawQueue();

    // 2. 文档快照使用场景顺序，验证回放创建次序保持原位置。
    CanvasDocument document;
    QString error_message;
    QVERIFY(canvas._paintScene->BuildCanvasDocument(&document, &error_message));
    QCOMPARE(document._item_order, QStringList({"first-pen", "middle-text", "last-pen"}));
}

QTEST_MAIN(CanvasFileTests)
#include "canvasfiletests.moc"
