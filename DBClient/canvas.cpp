#include "canvas.h"
#include "ui_canvas.h"
#include "usermgr.h"
#include "tcpmgr.h"
#include "voicemanager.h"
#include "tipwidget.h"
#include "imageassetmanager.h"
#include "httpmgr.h"
#include "canvasitems/imageitem.h"
#include "imagepreviewdialog.h"
#include "canvasfilemanager.h"
#include <QMouseEvent>
#include <QApplication>
#include <QClipboard>
#include <QImage>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>
#include <QColorDialog>
#include <QScrollBar>
#include <QMenu>
#include <QAction>
#include <QKeySequence>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QMimeData>
#include <QMessageBox>
#include <QUuid>
#include <QtMath>

#include <algorithm>

namespace
{
constexpr int SELECT_TOOL_ID = 100; // 本地鼠标模式按钮 ID，与协议 ShapeType 分离。
constexpr int HAND_TOOL_ID = 101; // 本地视口平移按钮 ID，不能用于绘图工厂。

QString ImageSuffixForMimeType(const QString& mime_type)
{
    // 上传签名只需要稳定的扩展名，统一从已经校验过的 MIME 类型推导，避免依赖原始文件名。
    const QString normalized_type = mime_type.trimmed().toLower();
    if (normalized_type == QStringLiteral("image/jpeg"))
    {
        return QStringLiteral("jpg");
    }
    if (normalized_type == QStringLiteral("image/webp"))
    {
        return QStringLiteral("webp");
    }
    return QStringLiteral("png");
}
}

Canvas::Canvas(const LatencyTestOptions& test_options, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::Canvas)
{
    // 1. 初始化界面及窗口拥有的资源和文件管理器。
    ui->setupUi(this);

    // 图片资源管理器只使用 Qt 异步网络和本地缓存，不连接数据库或 Redis。
    _imageAssetManager = new ImageAssetManager(this);
    _canvas_file_manager = new CanvasFileManager(_imageAssetManager, this);
    connect(_canvas_file_manager, &CanvasFileManager::sigImportReady, this,
            [this](const QString& request_id, const CanvasDocument& document, const QString& error_message) {
        // 1. 只处理当前画布发起的任务，换房后的结果不会触及新画布。
        if (request_id != _file_request_id || _file_canvas_generation != _canvas_generation) return;
        if (!error_message.isEmpty())
        {
            _is_file_operation_running = false;
            UpdateCanvasFileActions();
            TipWidget::showTip(ui->graphicsView, error_message);
            return;
        }
        // 2. 完成资源准备后再确认替换，拒绝或临时场景失败都保留原画布。
        const auto answer = QMessageBox::question(this, QStringLiteral("导入画布"),
            QStringLiteral("将替换当前离线画布，旧画布的撤销记录也会清空。是否继续？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (request_id != _file_request_id || _file_canvas_generation != _canvas_generation) return;
        QString import_error;
        if (answer == QMessageBox::Yes && !ApplyImportedCanvas(document, &import_error))
        {
            TipWidget::showTip(ui->graphicsView, import_error);
        }
        _is_file_operation_running = false;
        UpdateCanvasFileActions();
    });
    connect(_canvas_file_manager, &CanvasFileManager::sigExportFinished, this,
            [this](const QString& request_id, bool is_success, const QString& error_message) {
        // 1. 导出终态恢复菜单，失败信息由文件管理器提供。
        if (request_id != _file_request_id) return;
        _is_file_operation_running = false;
        UpdateCanvasFileActions();
        TipWidget::showTip(ui->graphicsView, is_success ? QStringLiteral("导出成功") : error_message);
    });

    // 2. 保留绘画节流和远端平滑应用定时器。
    // 16ms 节流：批量发送 Pen/Eraser 的 path_points
    _strokeFlushTimer = new QTimer(this);
    _strokeFlushTimer->setInterval(16);

    connect(_strokeFlushTimer, &QTimer::timeout, this, [this]() {

        // 遍历所有正在画的 uuid，把点批量发出去
        for (auto it = _pendingPointsByUuid.begin(); it != _pendingPointsByUuid.end(); ++it)
        {
            PendingStrokePoints& pendingStroke = it.value();

            // 不在绘制中，跳过
            if (!pendingStroke.active) continue;

            // 没有新增点，跳过（避免无意义的 flush 调用）
            if (pendingStroke.points.isEmpty()) continue;

            flushStrokePoints(it.key(), false);
        }
    });

    _strokeFlushTimer->start();

    // 远端绘画刷新缓冲：网络包到达可能有抖动，但 UI 绘制尽量保持固定节奏。
    // 16ms 约等于 60 FPS，额外引入的显示延迟通常在 0~16ms 之间；
    // 如果公网仍然卡顿，可以把这里调成 25 或 33，用更高延迟换更平滑的画面。
    _remoteDrawTimer = new QTimer(this);
    _remoteDrawTimer->setInterval(16);
    connect(_remoteDrawTimer, &QTimer::timeout, this, &Canvas::flushRemoteDrawQueue);
    _remoteDrawTimer->start();

    initCanvasUi();
    if (_paintScene)
        _paintScene->setEditable(false);
    initToolBtn();  //初始化toolbtn
    initMemberContextMenu();

    // 为整个程序安装事件过滤器
    qApp->installEventFilter(this);

    //给graphicsView安装事件过滤器，当Canvas能够拦截graphicsView的事件

    //连接新用户加入房间信号槽函数
    connect(TcpMgr::getInstance().get(),&TcpMgr::sig_user_joined,this,&Canvas::slot_user_joined);

    //连接新用户离开房间信号槽函数
    connect(TcpMgr::getInstance().get(),&TcpMgr::sig_user_left,this,&Canvas::slot_user_leaved);

    //连接房间编辑权限变更广播
    connect(TcpMgr::getInstance().get(),&TcpMgr::sig_permission_changed,this,&Canvas::slot_permission_changed);

    // 3. 场景连接集中维护，以便导入成功后重新接入原有业务槽。
    ConnectPaintSceneSignals();
    UpdateCanvasFileActions();

    //连接接收群聊消息
    connect(TcpMgr::getInstance().get(),&TcpMgr::sig_chat_received,this,&Canvas::slot_onChatReceived);
    connect(ui->input_edit,&QLineEdit::returnPressed,this,&Canvas::slot_onSendChatClicked);
    connect(ui->send_btn,&QPushButton::clicked,this,&Canvas::slot_onSendChatClicked);

    // TcpMgr -> Canvas (接收广播)
    connect(TcpMgr::getInstance().get(), &TcpMgr::sig_draw_broadcast,
            this, &Canvas::slot_onDrawBroadcast);
    connect(TcpMgr::getInstance().get(), &TcpMgr::sig_image_operation_broadcast,
            this, &Canvas::slot_onImageOperationBroadcast);

    // 图片签名接口复用 HttpMgr 的大厅模块回调；请求严格串行，回包才能对应到当前资源上下文。
    connect(HttpMgr::getInstance().get(), &HttpMgr::sigImageSignatureFinished,
            this, &Canvas::HandleImageSignatureFinished);
    connect(_imageAssetManager, &ImageAssetManager::sigAssetReady,
            this, &Canvas::slot_onImageAssetReady);
    connect(_imageAssetManager, &ImageAssetManager::sigAssetFailed,
            this, &Canvas::slot_onImageAssetFailed);
    connect(_imageAssetManager, &ImageAssetManager::sigLocalAssetReady,
            this, &Canvas::slot_onLocalAssetReady);
    connect(_imageAssetManager, &ImageAssetManager::sigLocalAssetFailed,
            this, &Canvas::slot_onLocalAssetFailed);
    connect(_imageAssetManager, &ImageAssetManager::sigAssetUploaded,
            this, &Canvas::slot_onImageAssetUploaded);
    connect(_imageAssetManager, &ImageAssetManager::sigAssetUploadFailed,
            this, &Canvas::slot_onImageAssetUploadFailed);
    connect(_imageAssetManager, &ImageAssetManager::sigAssetCacheMiss,
            this, &Canvas::RequestImageDownloadToken);
    connect(_imageAssetManager, &ImageAssetManager::sigServiceLost,
            this, &Canvas::HandleAssetServiceLost);

    if (test_options.enabled)
        _latencyTestController = new LatencyTestController(test_options, this);

    //开启鼠标追踪，鼠标不点击也把事件传给scene
    ui->graphicsView->setMouseTracking(true);
    ui->graphicsView->viewport()->setMouseTracking(true);

}

Canvas::~Canvas()
{
    // 1. 先回收文件任务，再释放它依赖的图片资源门面和界面。
    delete _canvas_file_manager;
    _canvas_file_manager = nullptr;
    CancelImageRequests();
    if (_latencyTestController)
        _latencyTestController->stop();
    VoiceManager::getInstance()->leaveRoom();
    qApp->removeEventFilter(this);  // 移除事件过滤器
    delete ui;
}

void Canvas::setRoomInfo(std::shared_ptr<RoomInfo> room_info)
{
    // 1. 换房使旧文件任务失效，保留首次加入时已收到的远端队列。
    CancelCanvasFileOperation();
    // 首次进入房间时，JoinRoomRsp 后面的历史绘画包可能已经先到达并进入队列。
    // 此时 _room_info 为空，不能清队列，否则会丢掉房主已有的绘画内容。
    // 从已有房间切换到另一个房间时仍需清理旧队列，避免旧房间图元串到新房间。
    if (_room_info)
    {
        CancelImageRequests();
        _pendingImageUploads.clear();
        _imageUploadQueue.clear();
        _activeImageUploadId.clear();
        _imageDownloadQueue.clear();
        _imageDownloadRequestActive = false;
        _activeImageDownload = PendingImageDownload();
        _remoteDrawQueue.clear();
        _remoteImageOperationQueue.clear();
        _pending_image_imports.clear();
    }

    // 2. 应用新房间元数据并恢复标准背景及菜单状态。
    this->_room_info = room_info;
    _paintScene->setBackgroundBrush(Qt::white);
    // 每个新房间从统一的 100% 视图状态开始，缩放比例不属于房间共享状态。
    if (ui->graphicsView)
        ui->graphicsView->resetZoom();
    if (_strokeFlushTimer && !_strokeFlushTimer->isActive())
        _strokeFlushTimer->start();
    if (_remoteDrawTimer && !_remoteDrawTimer->isActive())
        _remoteDrawTimer->start();
    applyRoomCanvasSize();
    refreshRoomCollaborationState();
    refreshCurrentUserProfile();
    UpdateCanvasFileActions();
}

void Canvas::enterOfflineMode()
{
    // 1. 取消旧房间的文件和图片任务。
    CancelCanvasFileOperation();
    CancelImageRequests();
    if (_latencyTestController)
        _latencyTestController->stop();
    VoiceManager::getInstance()->leaveRoom();

    // 2. 清理远端队列和原有场景，离线操作只更新本地状态。
    _pendingPointsByUuid.clear();
    _remoteDrawQueue.clear();
    _remoteImageOperationQueue.clear();
    _imageDownloadQueue.clear();
    _imageDownloadRequestActive = false;
    _activeImageDownload = PendingImageDownload();
    _imageUploadQueue.clear();
    _pendingImageUploads.clear();
    _activeImageUploadId.clear();
    _pending_image_imports.clear();
    if (_strokeFlushTimer)
        _strokeFlushTimer->stop();
    if (_remoteDrawTimer)
        _remoteDrawTimer->stop();

    _userItemMap.clear();
    if (ui && ui->treeWidget)
        ui->treeWidget->clear();

    if (_paintScene)
    {
        _paintScene->resetScene();
        _paintScene->setBackgroundBrush(Qt::white);
        _paintScene->setEditable(true);
    }

    // 离线画布也是独立的本地画布，不继承上一个房间的视图缩放比例。
    if (ui->graphicsView)
        ui->graphicsView->resetZoom();

    // 3. 创建离线元数据并更新导入菜单。
    _room_info = std::make_shared<RoomInfo>();
    _room_info->id = QStringLiteral("offline");
    _room_info->name = QStringLiteral("离线画板");
    _room_info->width = 1920;
    _room_info->height = 1080;
    _room_info->connected = false;
    _room_info->is_owner = true;
    _room_info->can_edit = true;
    _room_info->offline = true;

    applyRoomCanvasSize();
    UpdateCanvasFileActions();

    ui->title_label->setText(QStringLiteral("离线画板"));
    if (statusDot)
    {
        statusDot->setText(QStringLiteral("● 离线模式 / 可编辑"));
        statusDot->setStyleSheet("color: #4CAF50; font-size: 12px; padding-right: 10px;");
    }
}

void Canvas::resumeVoice()
{
    // 返回大厅期间只暂停了音频，回到原房间时恢复暂停前的麦克风和听筒状态。
    // 不重复执行 Canvas 加入流程，也不重新请求 LiveKit Token。
    VoiceManager::getInstance()->resumeAudio();
}

void Canvas::resetForReconnect()    //断线回大厅时调用，清空canvas画布
{
    // 1. 取消当前文件和图片任务，回大厅后不再接收导入结果。
    CancelCanvasFileOperation();
    CancelImageRequests();
    if (_latencyTestController)
        _latencyTestController->stop();

    // 2. 停止绘画定时器并清空请求和统计。
    if (_strokeFlushTimer)
        _strokeFlushTimer->stop();
    if (_remoteDrawTimer)
        _remoteDrawTimer->stop();

    // 清空待发送点缓存。
    _pendingPointsByUuid.clear();
    _remoteDrawQueue.clear();
    _remoteImageOperationQueue.clear();
    _imageDownloadQueue.clear();
    _imageDownloadRequestActive = false;
    _activeImageDownload = PendingImageDownload();
    _imageUploadQueue.clear();
    _pendingImageUploads.clear();
    _activeImageUploadId.clear();
    _pending_image_imports.clear();

    // 重置延迟测量统计。
    _latencySamples.clear();
    _latencySum = 0;
    _latencyCount = 0;

    // 3. 清空画面和房间状态。
    if (_paintScene)
    {
        _paintScene->setEditable(false);
        _paintScene->resetScene();
    }

    // 断线回大厅时清理本地视图状态，下一次进入画布从 100% 开始。
    if (ui->graphicsView)
        ui->graphicsView->resetZoom();

    // 清空用户列表 UI 和索引。
    _userItemMap.clear();
    if (ui && ui->treeWidget)
    {
        ui->treeWidget->clear();
    }

    // 清空房间信息。
    if (_room_info)
        _room_info->connected = false;
    _room_info.reset();
    UpdateCanvasFileActions();
}

void Canvas::ClearSession()
{
    // 1. 复用场景清理，停止绘画定时器并取消文件和图片任务。
    resetForReconnect();

    // 2. 清除旧房间聊天和用户资料，下一次登录不显示前一个账号的信息。
    ui->chat_textBrowser->clear();
    ui->input_edit->clear();
    ui->username_label->clear();
    ui->avator_label->clear();
    ui->title_label->clear();
}

bool Canvas::eventFilter(QObject *watched, QEvent *event)
{

    // 判断是不是 graphicsView 发出的事件
    if (watched == ui->graphicsView)
    {
        // 判断是不是 "鼠标离开" 事件
        if (event->type() == QEvent::Leave)
        {
            // 通知 PaintScene 隐藏光标
            if (_paintScene)
            {
                _paintScene->hideEraserCursor();
            }
        }
    }
    // 只关心鼠标按下
    if (event->type() == QEvent::MouseButtonPress)
    {
        // 全局事件过滤器会收到 QMenu 的点击事件。
        // 这里只处理成员列表本身，避免右键菜单里的 QAction 第一次点击被成员列表逻辑干扰。
        if (watched != ui->treeWidget && watched != ui->treeWidget->viewport())
            return QMainWindow::eventFilter(watched, event);

        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);

        // 右键用于成员列表授权菜单，不走左键选择/反选逻辑。
        if (mouseEvent->button() == Qt::RightButton)
            return false;

        QPoint globalPos = mouseEvent->globalPos();

        // 计算 TreeWidget 的区域
        QRect treeRect(ui->treeWidget->mapToGlobal(QPoint(0, 0)), ui->treeWidget->size());

        // 判断点击位置

        if (treeRect.contains(globalPos))   //点击在treeWidget内部
        {
            QPoint localPos = ui->treeWidget->mapFromGlobal(globalPos);
            QTreeWidgetItem *item = ui->treeWidget->itemAt(localPos);

            // 点了内部空白 -> 清除选中
            if (item == nullptr)
            {
                ui->treeWidget->clearSelection();
                return true; // 拦截，因为处理了空白点击，不需要默认处理了
            }

            // 点了已选中的 Item -> 反选
            if (item->isSelected())
            {
                item->setSelected(false);
                return true; // 拦截！防止默认行为又把它选上
            }

            return false; // 放行，让 QTreeWidget 默认逻辑去选中它
        }
        else
        {
            ui->treeWidget->clearSelection();
            return false;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void Canvas::initCanvasUi()
{
    // 1. 初始化工具停靠窗口和绘图场景。
    ui->tool_dock->setWindowTitle("工具栏");
    ui->chat_dock->setWindowTitle("聊天室");
    ui->user_dock->setWindowTitle("在线用户");

    //初始化 paintScene(begin)
    // PaintScene 和 Canvas 通过同一工厂获取工具能力，避免维护两份类型列表。
    _paintScene = new PaintScene(this);
    _paintScene->setSceneRect(0, 0, 5000, 5000);        // 默认占位尺寸，进入房间后会按房间信息重新设置
    _paintScene->setBackgroundBrush(Qt::white);         //背景白色
    ui->graphicsView->setScene(_paintScene);            //为view设置舞台
    ui->graphicsView->setFocusPolicy(Qt::StrongFocus); // 图片快捷键需要由画布视图接收焦点
    ui->graphicsView->setRenderHint(QPainter::Antialiasing);    //设置渲染质量，让线条抗锯齿（更平滑，不带狗牙）
    ui->graphicsView->ensureVisible(0, 0, 10, 10);              // 强制把镜头聚焦在画板的左上角 (0,0),保证 (0,0) 这个点附近的区域是可见的
    ui->graphicsView->resetZoom();                              // 初始化视图缩放状态，状态栏从 100% 开始显示
    connect(ui->graphicsView, &CanvasGraphicsView::sigImageFilesDropped,
            this, &Canvas::OnImageFilesDropped);
    connect(ui->graphicsView, &CanvasGraphicsView::sigPasteImageRequested,
            this, &Canvas::OnPasteImageRequested);
    //初始化 paintScene(end)

    //初始化 _widthPopup(begin)
    _widthPopup = new WidthPopup(this);
    connect(_widthPopup,&WidthPopup::sigLineWidthChanged,this,[=](int width){   // 预览窗口画笔 width 同步到画笔
        _paintScene->setPenWidth(width);
    });
    //初始化 _widthPopup(end)


    this->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);    //强制所有DockWidget标签页显示在顶部

    //使用空widget替换tool_dock的标题栏
    QWidget* emptyTitleTool = new QWidget();
    ui->tool_dock->setTitleBarWidget(emptyTitleTool);

    this->tabifyDockWidget(ui->chat_dock,ui->user_dock);                    // 两个dock叠在一起

    // 2. 文件菜单区分结构文件、图片导出和插入图片。
    ui->input_img->setIcon(style()->standardIcon(QStyle::SP_FileIcon));     // 设置action图标
    ui->menubar->setVisible(false);                                         //将菜单栏设置为不可见
    ui->file_btn->setMenu(ui->menu_F); // 直接把原来的菜单对象赋给按钮！
    connect(ui->input_img, &QAction::triggered,
            this, &Canvas::slot_onInputImgTriggered);
    connect(ui->import_canvas_action, &QAction::triggered, this, &Canvas::OnImportCanvasTriggered);
    connect(ui->export_canvas_action, &QAction::triggered, this, &Canvas::OnExportCanvasTriggered);
    connect(ui->export_image_action, &QAction::triggered, this, &Canvas::OnExportImageTriggered);
    ui->menu_F->setToolTipsVisible(true);

    //新建编辑菜单
    QMenu* editMenu = new QMenu(this);

    //添加册小action，并且限制只有离线模式可以使用
    QAction* undoAction = editMenu->addAction(QStringLiteral("撤销"));
    QAction* reset_zoom_action = editMenu->addAction(QStringLiteral("重置缩放（100%）"));
    reset_zoom_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    ui->edit_btn->setMenu(editMenu);
    connect(undoAction, &QAction::triggered, this, [this]() {
        if (!_room_info || !_room_info->offline)
        {
            TipWidget::showTip(ui->graphicsView, QStringLiteral("当前仅支持离线模式撤销"));
            return;
        }
        if (!_paintScene || !_paintScene->canUndoLocal())
        {
            TipWidget::showTip(ui->graphicsView, QStringLiteral("没有可撤销的操作"));
            return;
        }

        // 第一版只撤销离线本地图元；联机撤销以后需要走服务端校验和广播。
        _paintScene->undoLastLocalItem();
    });
    connect(reset_zoom_action, &QAction::triggered, this, [this]() {
        if (ui->graphicsView)
            ui->graphicsView->resetZoom();
    });

    // 3. 状态栏标签由窗口持有，场景替换后可以继续接收坐标。
    QStatusBar *bar = this->statusBar();    //获取状态栏
    // 左侧：坐标信息 (新建一个 Label)
    _cursor_position_label = new QLabel("X: 0, Y: 0", this);
    _cursor_position_label->setStyleSheet("color: #666; font-size: 12px; padding-left: 10px;");
    _cursor_position_label->setMinimumWidth(150);
    bar->addWidget(_cursor_position_label);

    // 中间/右侧：缩放信息，显示值始终来自 QGraphicsView 的实际变换矩阵。
    _zoomLabel = new QLabel("缩放：100%", this);
    _zoomLabel->setStyleSheet("color: #333; font-weight: bold; font-size: 12px;");
    bar->addPermanentWidget(_zoomLabel); // addPermanentWidget 加在最右边
    connect(ui->graphicsView, &CanvasGraphicsView::zoomChanged, this,
            [this](qreal zoom_factor) {
        if (_zoomLabel)
            _zoomLabel->setText(QStringLiteral("缩放：%1%")
                                .arg(qRound(zoom_factor * 100.0)));
    });

    // 右侧：连接状态
    statusDot = new QLabel("● 未连接", this);
    statusDot->setStyleSheet("color: #ff4d4d; font-size: 12px; padding-right: 10px;"); // 红色圆点
    bar->addPermanentWidget(statusDot);
}

void Canvas::ConnectPaintSceneSignals()
{
    // 1. 重用原有业务槽，旧场景销毁后 Qt 自动移除其连接。
    connect(_paintScene, &PaintScene::sigStrokeStart, this, &Canvas::slot_onStrokeStart);
    connect(_paintScene, &PaintScene::sigStrokeMove, this, &Canvas::slot_onStrokeMove);
    connect(_paintScene, &PaintScene::sigStrokeEnd, this, &Canvas::slot_onStrokeEnd);
    connect(_paintScene, &PaintScene::sigImageGeometryChanged, this, &Canvas::slot_onImageGeometryChanged);
    connect(_paintScene, &PaintScene::sigImageDeleteRequested, this, &Canvas::slot_onImageDeleteRequested);
    connect(_paintScene, &PaintScene::sigImageRetryRequested, this, &Canvas::slot_onImageRetryRequested);
    connect(_paintScene, &PaintScene::sigImagePreviewRequested, this, &Canvas::slot_onImagePreviewRequested);

    // 2. 手形模式的坐标来自视图，其余坐标来自场景，均接入同一标签。
    connect(ui->graphicsView, &CanvasGraphicsView::SigCursorScenePositionChanged,
            _paintScene, &PaintScene::sigCursorPosChanged);
    connect(_paintScene, &PaintScene::sigCursorPosChanged, this, [this](QPointF pos) {
        _cursor_position_label->setText(QStringLiteral("X: %1, Y: %2")
            .arg(static_cast<int>(pos.x())).arg(static_cast<int>(pos.y())));
    });
}

void Canvas::UpdateCanvasFileActions()
{
    // 1. 导入仅限离线；导出读取当前客户端，任何编辑权限均可使用。
    const bool has_canvas = _room_info && _paintScene;
    const bool is_available = has_canvas && !_is_file_operation_running;
    ui->import_canvas_action->setEnabled(is_available && _room_info->offline);
    ui->import_canvas_action->setToolTip(has_canvas && !_room_info->offline
        ? QStringLiteral("当前仅支持离线导入画布") : QStringLiteral("导入 .synccanvas 并替换当前画布"));
    ui->export_canvas_action->setEnabled(is_available);
    ui->export_image_action->setEnabled(is_available);
    ui->input_img->setEnabled(is_available);
}

void Canvas::CancelCanvasFileOperation()
{
    // 1. 房间切换后后台结果不得修改当前画布。
    ++_canvas_generation;
    _canvas_file_manager->CancelFileOperation();
    _file_request_id.clear();
    _is_file_operation_running = false;
    UpdateCanvasFileActions();
}

void Canvas::DrainRemoteCanvasOperations()
{
    // 1. 在线快照先应用已收到的队列，不向服务端请求新快照。
    if (!_room_info || _room_info->offline || !_room_info->connected) return;
    while (!_remoteDrawQueue.isEmpty() || !_remoteImageOperationQueue.isEmpty())
    {
        flushRemoteDrawQueue();
    }
}

bool Canvas::ApplyImportedCanvas(const CanvasDocument& document, QString* error_message)
{
    // 1. GUI 图元和 QPixmap 只在主线程构建，失败时仅销毁临时场景。
    auto imported_scene = std::make_unique<PaintScene>();
    if (!imported_scene->LoadCanvasDocument(document, error_message)) return false;
    imported_scene->setPenColor(_paintScene->getPenColor());
    imported_scene->setPenWidth(_paintScene->getPenWidth());
    imported_scene->setEditable(true);

    // 2. 资源已准备且新场景可用，才取消旧图片请求并一次性切换场景。
    CancelImageRequests();
    _pending_image_imports.clear();
    PaintScene* old_scene = _paintScene;
    _paintScene = imported_scene.release();
    _paintScene->setParent(this);
    ui->graphicsView->setScene(_paintScene);
    ConnectPaintSceneSignals();
    SetCanvasTool(_toolGroup->checkedId());
    delete old_scene;
    ++_canvas_generation;

    // 3. 同步离线尺寸并恢复 100% 和左上角，导入内容不进入撤销记录。
    _room_info->width = document._canvas_size.width();
    _room_info->height = document._canvas_size.height();
    ui->graphicsView->resetZoom();
    ui->graphicsView->horizontalScrollBar()->setValue(ui->graphicsView->horizontalScrollBar()->minimum());
    ui->graphicsView->verticalScrollBar()->setValue(ui->graphicsView->verticalScrollBar()->minimum());
    _cursor_position_label->setText(QStringLiteral("X: 0, Y: 0"));
    return true;
}

void Canvas::OnImportCanvasTriggered()
{
    // 1. 在线导入需要服务端批处理，初版仅在离线模式提供入口。
    if (_is_file_operation_running || !_room_info) return;
    if (!_room_info->offline)
    {
        TipWidget::showTip(ui->graphicsView, QStringLiteral("当前仅支持离线导入画布"));
        return;
    }
    const QString file_path = QFileDialog::getOpenFileName(this, QStringLiteral("导入画布"), QString(),
        QStringLiteral("SyncCanvas 画布 (*.synccanvas);;所有文件 (*)"));
    if (file_path.isEmpty()) return;

    // 2. 后台完成 JSON 和资源准备后才询问是否替换。
    _is_file_operation_running = true;
    _file_request_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    _file_canvas_generation = _canvas_generation;
    UpdateCanvasFileActions();
    _canvas_file_manager->ImportCanvasAsync(file_path, _file_request_id);
}

void Canvas::OnExportCanvasTriggered()
{
    // 1. 选择结构文件目标路径，未填写扩展名时补齐。
    if (_is_file_operation_running || !_room_info) return;
    QString file_path = QFileDialog::getSaveFileName(this, QStringLiteral("导出画布"),
        QStringLiteral("canvas.synccanvas"), QStringLiteral("SyncCanvas 画布 (*.synccanvas)"));
    if (file_path.isEmpty()) return;
    const QString suffix = QFileInfo(file_path).suffix().toLower();
    if (suffix.isEmpty()) file_path += QStringLiteral(".synccanvas");
    else if (suffix != QStringLiteral("synccanvas"))
    {
        TipWidget::showTip(ui->graphicsView, QStringLiteral("请使用 .synccanvas 扩展名重新选择文件"));
        return;
    }

    // 2. 捕获全部本地和远端图元，后台补齐资源并提交原子文件。
    DrainRemoteCanvasOperations();
    CanvasDocument document;
    QString error_message;
    if (!_paintScene->BuildCanvasDocument(&document, &error_message))
    {
        TipWidget::showTip(ui->graphicsView, error_message);
        return;
    }
    _is_file_operation_running = true;
    _file_request_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    _file_canvas_generation = _canvas_generation;
    UpdateCanvasFileActions();
    _canvas_file_manager->ExportCanvasAsync(document, file_path, _file_request_id);
}

void Canvas::OnExportImageTriggered()
{
    // 1. 根据所选过滤器补齐扩展名，明确填写的 PNG/JPG 扩展名决定编码。
    if (_is_file_operation_running || !_room_info) return;
    QString selected_filter;
    QString file_path = QFileDialog::getSaveFileName(this, QStringLiteral("导出图片"),
        QStringLiteral("canvas"), QStringLiteral("PNG 图片 (*.png);;JPG 图片 (*.jpg *.jpeg)"), &selected_filter);
    if (file_path.isEmpty()) return;
    QString suffix = QFileInfo(file_path).suffix().toLower();
    if (suffix.isEmpty())
    {
        suffix = selected_filter.startsWith(QStringLiteral("JPG")) ? QStringLiteral("jpg") : QStringLiteral("png");
        file_path += QStringLiteral(".") + suffix;
    }
    if (suffix != "png" && suffix != "jpg" && suffix != "jpeg")
    {
        TipWidget::showTip(ui->graphicsView, QStringLiteral("请使用 .png 或 .jpg 扩展名重新选择文件"));
        return;
    }

    // 2. GUI 线程渲染场景，图片编码和原子提交交由文件任务池。
    DrainRemoteCanvasOperations();
    QString error_message;
    const QImage image = _paintScene->RenderCanvasImage(&error_message);
    if (image.isNull())
    {
        TipWidget::showTip(ui->graphicsView, error_message);
        return;
    }
    _is_file_operation_running = true;
    _file_request_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    _file_canvas_generation = _canvas_generation;
    UpdateCanvasFileActions();
    _canvas_file_manager->ExportImageAsync(image, file_path, suffix == "png" ? QByteArray("png") : QByteArray("jpg"), _file_request_id);
}

void Canvas::SetCanvasTool(int tool_id)
{
    // 1. 只接受已注册按钮，按钮 ID 不直接作为绘图协议类型使用。
    QAbstractButton* tool_button = _toolGroup ? _toolGroup->button(tool_id) : nullptr;
    if (!tool_button || !_paintScene || !ui->graphicsView)
    {
        return;
    }

    // 2. 场景先完成旧操作；视图随后切换交互能力，全程保留缩放和滚动位置。
    const CanvasInteractionMode mode = tool_id == SELECT_TOOL_ID
                                           ? CanvasInteractionMode::Select
                                           : (tool_id == HAND_TOOL_ID
                                                  ? CanvasInteractionMode::Pan
                                                  : CanvasInteractionMode::Draw);
    _paintScene->SetInteractionMode(mode);
    if (mode == CanvasInteractionMode::Draw)
    {
        _paintScene->setShapeType(static_cast<ShapeType>(tool_id));
    }
    ui->graphicsView->SetInteractionMode(mode);

    // 3. 初始化与用户点击走同一入口，保证按钮高亮和实际模式始终一致。
    tool_button->setChecked(true);
}

void Canvas::initToolBtn()
{
    // 1. 本地交互工具与绘图工具加入同一互斥组，颜色和线宽按钮保持独立。
    _toolGroup = new QButtonGroup(this);

    //开启所有工具按钮的 Checkable 属性
    ui->pen_tool->setCheckable(true);
    ui->eraser_tool->setCheckable(true);
    ui->line_tool->setCheckable(true);
    ui->rect_tool->setCheckable(true);
    ui->oval_tool->setCheckable(true);
    ui->arrow_tool->setCheckable(true);
    ui->diamond_tool->setCheckable(true);
    ui->select_tool->setCheckable(true);
    ui->hand_tool->setCheckable(true);

    //设置互斥
    _toolGroup->setExclusive(true);

    //把按钮加进去，分配ID
    _toolGroup->addButton(ui->pen_tool,Shape_Pen);
    _toolGroup->addButton(ui->eraser_tool,Shape_Eraser);
    _toolGroup->addButton(ui->line_tool,Shape_Line);
    _toolGroup->addButton(ui->rect_tool,Shape_Rect);
    _toolGroup->addButton(ui->oval_tool,Shape_Oval);
    _toolGroup->addButton(ui->arrow_tool, Shape_Arrow);
    _toolGroup->addButton(ui->diamond_tool, Shape_Diamond);
    _toolGroup->addButton(ui->select_tool, SELECT_TOOL_ID);
    _toolGroup->addButton(ui->hand_tool, HAND_TOOL_ID);

    // 2. 显式连接统一切换槽，避免将鼠标和手形 ID 强制转换为 ShapeType。
    connect(_toolGroup, &QButtonGroup::idClicked, this, &Canvas::SetCanvasTool);
    SetCanvasTool(Shape_Pen);

    // 3. 新工具图标与 UI 资源路径一致，沿用工具栏现有的 checked 高亮样式。
    ui->select_tool->setIcon(QIcon(QStringLiteral(":/res/select_cursor.svg")));
    ui->hand_tool->setIcon(QIcon(QStringLiteral(":/res/hand_pan.svg")));
    ui->arrow_tool->setIcon(QIcon(QStringLiteral(":/res/shape_arrow.svg")));
    ui->diamond_tool->setIcon(QIcon(QStringLiteral(":/res/shape_diamond.svg")));

    QPixmap originMap(":/res/pen.png");
    ui->pen_tool->setIcon(QIcon(originMap));

    // 4. 语音初始化继续保持原有流程，工具切换槽不触碰语音或房间状态。
    const QPixmap microphone_icon = applyColor(
        QPixmap(":/res/microphoneopen.png"), Qt::white);
    const QPixmap speaker_icon = applyColor(
        QPixmap(":/res/Speakeropen.png"), Qt::white);
    ui->mic_toolBtn->setIcon(QIcon(microphone_icon));
    ui->speaker_toolBtn->setIcon(QIcon(speaker_icon));

    // 覆盖系统原生菜单按钮绘制，避免蓝色背景和箭头遮挡语音图标。
    const QString voice_tool_style = QStringLiteral(
        "QToolButton { padding-right: 8px; }"
        "QToolButton::menu-button {"
        "  width: 10px; border: none; background: transparent; }"
        "QToolButton::menu-arrow {"
        "  image: url(:/res/voice_arrow_down.svg);"
        "  width: 8px; height: 5px; }"
    );
    ui->mic_toolBtn->setStyleSheet(voice_tool_style);
    ui->speaker_toolBtn->setStyleSheet(voice_tool_style);

    // 根据当前麦克风状态切换白色的开启/关闭图标。
    auto update_microphone_icon = [this]() {
        const QString icon_path = VoiceManager::getInstance()->microphoneEnabled()
                                      ? QStringLiteral(":/res/microphoneopen.png")
                                      : QStringLiteral(":/res/microphoneclose.png");
        ui->mic_toolBtn->setIcon(QIcon(applyColor(QPixmap(icon_path), Qt::white)));
    };
    // 根据当前扬声器状态切换白色的开启/关闭图标。
    auto update_speaker_icon = [this]() {
        const QString icon_path = VoiceManager::getInstance()->speakerEnabled()
                                      ? QStringLiteral(":/res/Speakeropen.png")
                                      : QStringLiteral(":/res/Speakerclose.png");
        ui->speaker_toolBtn->setIcon(QIcon(applyColor(QPixmap(icon_path), Qt::white)));
    };

    update_microphone_icon();   // 更新麦克风图标
    update_speaker_icon();      // 更新听筒图标

    // VoiceManager 可能因大厅恢复或其他逻辑改变状态，UI 需要同步刷新图标。
    connect(VoiceManager::getInstance().get(), &VoiceManager::sig_microphone_changed,
            this, [update_microphone_icon](bool) { update_microphone_icon(); });
    connect(VoiceManager::getInstance().get(), &VoiceManager::sig_speaker_changed,
            this, [update_speaker_icon](bool) { update_speaker_icon(); });
    connect(VoiceManager::getInstance().get(), &VoiceManager::sig_audio_device_changed,
            this, [this](bool recording, const QString& device_id) {
        if (recording)
            _selected_recording_device_id = device_id;
        else
            _selected_playout_device_id = device_id;
    });

    // 点击按钮主体只切换开关，点击右侧菜单箭头则由设备菜单处理。
    connect(ui->mic_toolBtn, &QToolButton::clicked, this, [this, update_microphone_icon]() {
        VoiceManager* voice_manager = VoiceManager::getInstance().get();
        const bool enabled = !voice_manager->microphoneEnabled();
        voice_manager->setMicrophoneEnabled(enabled);
        update_microphone_icon();
        qDebug() << "[Voice UI] microphone enabled:" << enabled;
    });
    connect(ui->speaker_toolBtn, &QToolButton::clicked, this, [this, update_speaker_icon]() {
        VoiceManager* voice_manager = VoiceManager::getInstance().get();
        const bool enabled = !voice_manager->speakerEnabled();
        voice_manager->setSpeakerEnabled(enabled);
        update_speaker_icon();
        qDebug() << "[Voice UI] speaker enabled:" << enabled;
    });

    // 菜单每次展开时重新枚举设备，避免设备插拔后仍显示旧列表。
    auto* microphone_menu = new QMenu(ui->mic_toolBtn);
    microphone_menu->setTitle(QStringLiteral("选择麦克风"));
    microphone_menu->setStyleSheet(QStringLiteral(
        "QMenu::item:checked { background-color: #c4c4c4; color: #202020; }"
        "QMenu::item:checked:selected { background-color: #aaaaaa; color: #101010; }"));
    connect(microphone_menu, &QMenu::aboutToShow, this, [this, microphone_menu]() {
        microphone_menu->clear();
        const auto devices = VoiceManager::getInstance()->recordingDevices();
        if (devices.isEmpty())
        {
            microphone_menu->addAction(QStringLiteral("没有可用麦克风"))->setEnabled(false);
            return;
        }
        const bool recording_selected = std::any_of(
            devices.cbegin(), devices.cend(), [this](const auto& device) {
                return device.id == _selected_recording_device_id;
            });
        if (!recording_selected)
            _selected_recording_device_id = devices.first().id;
        for (const auto& device : devices)
        {
            QAction* action = microphone_menu->addAction(device.name);
            action->setCheckable(true);
            action->setChecked(device.id == _selected_recording_device_id);
            QObject::connect(action, &QAction::triggered, microphone_menu,
                             [this, device]() {
                const bool success = VoiceManager::getInstance()->setRecordingDevice(device.id);
                if (success)
                    _selected_recording_device_id = device.id;
                qDebug() << "[Voice UI] recording device:" << device.name
                         << "id:" << device.id << "success:" << success;
            });
        }
    });
    ui->mic_toolBtn->setMenu(microphone_menu);

    // 扬声器菜单与麦克风菜单保持相同的动态刷新策略。
    auto* speaker_menu = new QMenu(ui->speaker_toolBtn);
    speaker_menu->setTitle(QStringLiteral("选择扬声器"));
    speaker_menu->setStyleSheet(QStringLiteral(
        "QMenu::item:checked { background-color: #c4c4c4; color: #202020; }"
        "QMenu::item:checked:selected { background-color: #aaaaaa; color: #101010; }"));
    connect(speaker_menu, &QMenu::aboutToShow, this, [this, speaker_menu]() {
        speaker_menu->clear();
        const auto devices = VoiceManager::getInstance()->playoutDevices();
        if (devices.isEmpty())
        {
            speaker_menu->addAction(QStringLiteral("没有可用扬声器"))->setEnabled(false);
            return;
        }
        const bool playout_selected = std::any_of(
            devices.cbegin(), devices.cend(), [this](const auto& device) {
                return device.id == _selected_playout_device_id;
            });
        if (!playout_selected)
            _selected_playout_device_id = devices.first().id;
        for (const auto& device : devices)
        {
            QAction* action = speaker_menu->addAction(device.name);
            action->setCheckable(true);
            action->setChecked(device.id == _selected_playout_device_id);
            QObject::connect(action, &QAction::triggered, speaker_menu,
                             [this, device]() {
                const bool success = VoiceManager::getInstance()->setPlayoutDevice(device.id);
                if (success)
                    _selected_playout_device_id = device.id;
                qDebug() << "[Voice UI] playout device:" << device.name
                         << "id:" << device.id << "success:" << success;
            });
        }
    });
    ui->speaker_toolBtn->setMenu(speaker_menu);

}

void Canvas::refreshCurrentUserProfile()
{
    // Canvas 在登录前就创建，构造函数阶段拿不到当前用户信息；
    // 进入房间后再刷新，避免工具栏一直显示 UI 文件中的默认头像和名称。
    const auto my_info = UserMgr::getInstance()->getMyInfo();
    if (!my_info)
        return;

    ui->username_label->setText(my_info->_name);
    UserMgr::getInstance()->loadAvatar(my_info->_avatar, ui->avator_label);
}

void Canvas::applyRoomCanvasSize()
{
    if (!_paintScene || !_room_info)
        return;

    // 创建/加入房间成功后，使用服务端返回的房间画布尺寸覆盖初始化占位尺寸。
    if (_room_info->width <= 0 || _room_info->height <= 0)
        return;

    _paintScene->setSceneRect(0, 0, _room_info->width, _room_info->height);
    ui->graphicsView->ensureVisible(0, 0, 10, 10);
}

void Canvas::initMemberContextMenu()
{
    // 成员列表右键菜单只作为房主授权入口；普通成员不会弹出菜单。
    ui->treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->treeWidget, &QWidget::customContextMenuRequested,
            this, &Canvas::showMemberContextMenu);
}

void Canvas::showMemberContextMenu(const QPoint& pos)
{
    if (!_room_info || !_room_info->is_owner)
        return;

    QTreeWidgetItem* item = ui->treeWidget->itemAt(pos);
    if (!item)
        return;

    bool ok = false;
    const int targetUid = item->data(0, Qt::UserRole).toInt(&ok);
    if (!ok || targetUid == 0)
        return;

    const int currentUid = UserMgr::getInstance()->getUid();
    if (targetUid == currentUid)
        return;

    QMenu menu(this);
    QAction* grantAction = menu.addAction(QStringLiteral("授权编辑"));
    QAction* revokeAction = menu.addAction(QStringLiteral("取消编辑权限"));

    QAction* selectedAction = menu.exec(ui->treeWidget->viewport()->mapToGlobal(pos));
    if (selectedAction == grantAction)
    {
        // 发送授权请求，最终是否生效以服务端校验和广播为准。
        TcpMgr::getInstance()->slot_grant_edit(_room_info->id, targetUid);
        TipWidget::showTip(ui->graphicsView, QStringLiteral("已发送授权编辑请求"));
    }
    else if (selectedAction == revokeAction)
    {
        // 发送取消授权请求，目标用户收到广播后会切回只读。
        TcpMgr::getInstance()->slot_revoke_edit(_room_info->id, targetUid);
        TipWidget::showTip(ui->graphicsView, QStringLiteral("已发送取消编辑权限请求"));
    }
}

void Canvas::addUser(int uid, QString name, QString avatar_url) //添加用户
{
    // 已存在：更新，不新增
    if (_userItemMap.contains(uid))
    {
        QTreeWidgetItem* item = _userItemMap.value(uid);
        if (item)
        {
            item->setText(0, name);
            item->setData(0, Qt::UserRole, uid);
            UserMgr::getInstance()->loadAvatar(avatar_url, item);
        }
        return;
    }

    // 不存在：新增
    QTreeWidgetItem* new_item = ui->treeWidget->addUser(uid,name,avatar_url);
    _userItemMap.insert(uid,new_item);
}
void Canvas::leaveUser(int uid) //移除用户
{
    if(_userItemMap.contains(uid))
    {
        QTreeWidgetItem* item = _userItemMap.take(uid); //从 Map 中移除，并拿到指针 (一步到位)
        delete item;
    }
}


void Canvas::refreshRoomCollaborationState()    // 刷新房间协作状态
{
    if (!_room_info)
        return;

    const int currentUid = UserMgr::getInstance()->getUid();    //获取当前客户端用户id
    _room_info->connected = true;                               //设置已经连接到房间
    _room_info->is_owner = (currentUid != 0 && currentUid == _room_info->owner_uid);    //是否为房主

    // 第一版默认只有房主可编辑，后续房主授权时只需要扩展这里的判断。
    _room_info->can_edit = _room_info->is_owner;

    if (_paintScene)
        _paintScene->setEditable(_room_info->can_edit);     //设置画布是否可编辑状态

    if (statusDot)  //设置状态栏
    {
        const QString stateText = _room_info->can_edit
                                      ? QStringLiteral("● 已连接 / 可编辑")
                                      : QStringLiteral("● 已连接 / 只读");
        statusDot->setText(stateText);
        statusDot->setStyleSheet("color: #2ecc71; font-size: 12px; padding-right: 10px;");
    }
}

QString Canvas::formatMemberDisplayName(const UserInfo& info) const // 格式化成员显示名
{
    QString displayName = info._name;
    if (!_room_info)
        return displayName;

    const int currentUid = UserMgr::getInstance()->getUid();
    if (info._id == currentUid)
        displayName += QStringLiteral(" (我)");
    if (info._id == _room_info->owner_uid)
        displayName += QStringLiteral(" (房主)");
    return displayName;
}

void Canvas::slot_creat_room_finish(std::shared_ptr<RoomInfo> room_info)
{
    _room_info = room_info;
    refreshCurrentUserProfile();
    refreshRoomCollaborationState();

    if (room_info && !room_info->offline)
        VoiceManager::getInstance()->joinRoom(room_info->id);

    TipWidget::showTip(ui->graphicsView, QStringLiteral("创建房间成功"));
    QString room_name = room_info->name;
    QString room_id = room_info->id;
    ui->title_label->setText(room_name + QStringLiteral("-房间号:") + room_id);

    std::shared_ptr<const UserInfo> my_info = UserMgr::getInstance()->getMyInfo();
    UserInfo selfInfo;
    selfInfo._id = my_info->_id;
    selfInfo._name = my_info->_name;
    selfInfo._avatar = my_info->_avatar;
    addUser(selfInfo._id, formatMemberDisplayName(selfInfo), selfInfo._avatar);
    UserMgr::getInstance()->setIsHaveRoom(true);

    if (_strokeFlushTimer && !_strokeFlushTimer->isActive())
        _strokeFlushTimer->start(16);

    startLatencyTestIfReady();

}

void Canvas::slot_join_room_finish(std::shared_ptr<RoomInfo> room_info)
{
    _room_info = room_info;
    refreshCurrentUserProfile();
    refreshRoomCollaborationState();

    if (room_info && !room_info->offline)
        VoiceManager::getInstance()->joinRoom(room_info->id);

    TipWidget::showTip(ui->graphicsView, QStringLiteral("加入房间成功"));
    QString room_name = room_info->name;
    QString room_id = room_info->id;
    ui->title_label->setText(room_name + QStringLiteral("-房间号:") + room_id);

    UserMgr::getInstance()->setIsHaveRoom(true);

    const QList<UserInfo>& members= room_info->members;
    for(int i = 0;i < members.size();i++)
    {
        addUser(members[i]._id, formatMemberDisplayName(members[i]), members[i]._avatar);
    }
    if (_strokeFlushTimer && !_strokeFlushTimer->isActive())
        _strokeFlushTimer->start(16);

    startLatencyTestIfReady();
}

void Canvas::startLatencyTestIfReady()
{
    if (!_latencyTestController || !_room_info || !_room_info->connected)
        return;

    // 测试发送端必须拥有编辑权限，接收端则只需要成功加入房间。
    _latencyTestController->startForRoom(
        _room_info->id,
        UserMgr::getInstance()->getUid(),
        _room_info->can_edit);

    // 测试期间固定关闭人工绘画输入，避免污染发送频率和接收队列。
    if (_latencyTestController->isRunning() && _paintScene)
        _paintScene->setEditable(false);
}

void Canvas::slot_user_joined(UserInfo new_info)
{
    // ---------------------------------------------------------
    // 更新数据层 (Model)
    // ---------------------------------------------------------
    if(_room_info)
    {
        //去重
        bool exists = false;
        for(const auto& u : _room_info->members)
        {
            if(u._id == new_info._id)
            {
                exists = true;
                break;
            }
        }
        if(!exists)
        {
            _room_info->members.append(new_info);
        }
    }
    // ---------------------------------------------------------
    // 更新视图层 (View)
    // ---------------------------------------------------------
    addUser(new_info._id,formatMemberDisplayName(new_info),new_info._avatar);  //添加用户
}

void Canvas::slot_user_leaved(int uid)
{
    // ---------------------------------------------------------
    // 更新数据层 (Model)
    // ---------------------------------------------------------
    if (_room_info)
    {
        // 遍历查找并删除
        // QList 在遍历中删除要注意迭代器失效问题，用索引或者 erase 最安全
        for (int i = 0; i < _room_info->members.size(); ++i)
        {
            if (_room_info->members[i]._id == uid)
            {
                _room_info->members.removeAt(i);
                break; // 找到了就删掉并退出，避免继续循环
            }
        }
    }
    // ---------------------------------------------------------
    // 更新视图层 (View & Map)
    // ---------------------------------------------------------
    leaveUser(uid);
}

void Canvas::slot_permission_changed(int target_uid, bool can_edit)     //权限变更处理函数
{
    if (!_room_info)
        return;

    const int currentUid = UserMgr::getInstance()->getUid();
    if (target_uid != currentUid)
        return;

    // 房主始终保留编辑权限；普通成员跟随服务端广播的授权状态。
    _room_info->can_edit = _room_info->is_owner || can_edit;

    if (_paintScene)
        _paintScene->setEditable(_room_info->can_edit);

    if (statusDot)
    {
        const QString stateText = _room_info->can_edit
                                      ? QStringLiteral("● 已连接 / 可编辑")
                                      : QStringLiteral("● 已连接 / 只读");
        statusDot->setText(stateText);
        statusDot->setStyleSheet("color: #2ecc71; font-size: 12px; padding-right: 10px;");
    }

    TipWidget::showTip(ui->graphicsView,
                       _room_info->can_edit ? QStringLiteral("房主已授权你编辑画板")
                                            : QStringLiteral("房主已取消你的编辑权限"));
}


void Canvas::on_color_tool_clicked()    //color_tool槽函数
{
    //弹出颜色选择框
    QColor color = QColorDialog::getColor(_paintScene->getPenColor(),this,"选择画笔颜色");
    if(color.isValid())
    {
        _paintScene->setPenColor(color);        //设置逻辑层颜色
        QPixmap originMap(":/res/pen.png");     //加载原图
        QPixmap tintedMap = applyColor(originMap,color);        //生成染色后的图标
        ui->pen_tool->setIcon(QIcon(tintedMap));                //设置染色后的图标给按钮
    }
}


void Canvas::on_width_tool_clicked()
{
    // 获取按钮的位置，把弹窗显示在按钮下方
    QPoint pos = ui->width_tool->mapToGlobal(QPoint(0, ui->width_tool->height()));

    // 设置当前画笔粗细值
    _widthPopup->setLineWidth(_paintScene->getPenWidth());

    _widthPopup->move(pos);
    _widthPopup->show();
}

void Canvas::slot_onInputImgTriggered()
{
    // 槽名不使用 on_<object>_<signal> 自动连接模式，避免与 connectSlotsByName 重复连接导致每次触发执行两遍。
    // 没有房间或当前成员没有编辑权限时不能创建图片，服务端也会执行同样的权限约束。
    if (!_room_info || !_paintScene || !_imageAssetManager ||
        (!_room_info->offline && !_room_info->can_edit))
    {
        TipWidget::showTip(ui->graphicsView,
                           QStringLiteral("当前没有图片编辑权限"));
        return;
    }

    const QString file_path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择图片"),
        QString(),
        QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.webp)"));
    if (file_path.isEmpty())
    {
        // 用户取消选择不是错误，不弹提示也不改变当前画布状态。
        return;
    }

    StartFileImageImport(file_path, QPointF(), false);
}

void Canvas::OnImageFilesDropped(QStringList file_paths, QPointF scene_pos)
{
    if (!_room_info || !_paintScene || !_imageAssetManager ||
        (!_room_info->offline && !_room_info->can_edit))
    {
        TipWidget::showTip(ui->graphicsView, QStringLiteral("当前没有图片编辑权限"));
        return;
    }

    // 多文件拖放共享同一鼠标锚点，通过固定偏移避免异步完成后图元完全重叠。
    for (int index = 0; index < file_paths.size(); ++index)
    {
        const QPointF offset(static_cast<qreal>(index * 20),
                             static_cast<qreal>(index * 20));
        StartFileImageImport(file_paths.at(index), scene_pos + offset, true);
    }
}

void Canvas::OnPasteImageRequested()
{
    if (!_room_info || !_paintScene || !_imageAssetManager ||
        (!_room_info->offline && !_room_info->can_edit))
    {
        TipWidget::showTip(ui->graphicsView, QStringLiteral("当前没有图片编辑权限"));
        return;
    }

    const QMimeData* mime_data = QApplication::clipboard()->mimeData();
    if (!mime_data)
    {
        TipWidget::showTip(ui->graphicsView, QStringLiteral("剪贴板中没有可插入的图片"));
        return;
    }

    const QPointF center_pos = CanvasCenterScenePos();
    if (mime_data->hasUrls())
    {
        QStringList file_paths;
        for (const QUrl& url : mime_data->urls())
        {
            if (!url.isLocalFile())
            {
                continue;
            }
            const QFileInfo file_info(url.toLocalFile());
            if (file_info.exists() && file_info.isFile())
            {
                file_paths.append(file_info.absoluteFilePath());
            }
        }
        if (!file_paths.isEmpty())
        {
            for (int index = 0; index < file_paths.size(); ++index)
            {
                const QPointF offset(static_cast<qreal>(index * 20),
                                     static_cast<qreal>(index * 20));
                StartFileImageImport(file_paths.at(index), center_pos + offset, true);
            }
            return;
        }
    }

    if (mime_data->hasImage())
    {
        const QImage image = QApplication::clipboard()->image();
        if (!image.isNull())
        {
            StartImageDataImport(image, center_pos, true);
            return;
        }
    }

    TipWidget::showTip(ui->graphicsView, QStringLiteral("剪贴板中没有可插入的图片"));
}

void Canvas::StartFileImageImport(const QString& file_path,
                                  const QPointF& anchor_scene_pos,
                                  bool has_anchor_scene_pos)
{
    if (file_path.isEmpty() || !_imageAssetManager)
    {
        return;
    }

    // 请求上下文保存触发时的锚点，避免用户拖放后切换视图导致图片跳到新的中心位置。
    const QString request_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    PendingImageImport pending_import;
    pending_import._source_file_path = file_path;
    pending_import._anchor_scene_pos = anchor_scene_pos;
    pending_import._has_anchor_scene_pos = has_anchor_scene_pos;
    _pending_image_imports.insert(request_id, pending_import);
    _imageAssetManager->prepareLocalAssetAsync(file_path, request_id);
    TipWidget::showTip(ui->graphicsView, QStringLiteral("图片正在读取，请稍候"));
}

void Canvas::StartImageDataImport(const QImage& image,
                                  const QPointF& anchor_scene_pos,
                                  bool has_anchor_scene_pos)
{
    if (image.isNull() || !_imageAssetManager)
    {
        return;
    }

    // 剪贴板没有稳定的文件路径，仍使用同一请求表以复用房间切换时的过期结果保护。
    const QString request_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    PendingImageImport pending_import;
    pending_import._anchor_scene_pos = anchor_scene_pos;
    pending_import._has_anchor_scene_pos = has_anchor_scene_pos;
    _pending_image_imports.insert(request_id, pending_import);
    _imageAssetManager->PrepareImageDataAsync(image, request_id);
    TipWidget::showTip(ui->graphicsView, QStringLiteral("剪贴板图片正在处理，请稍候"));
}

QPointF Canvas::CanvasCenterScenePos() const
{
    if (!ui || !ui->graphicsView || !ui->graphicsView->viewport())
    {
        return QPointF();
    }
    return ui->graphicsView->mapToScene(
        ui->graphicsView->viewport()->rect().center());
}

void Canvas::slot_onLocalAssetReady(QString request_id,
                                    QString file_path,
                                    ImageAssetInfo asset_info,
                                    QPixmap pixmap)
{
    // 1. 过期结果解除代理保护，不插入其他房间。
    if (!_pending_image_imports.contains(request_id) || !_room_info || !_paintScene)
    {
        _imageAssetManager->ReleaseAsset(asset_info._asset_handle);
        return;
    }
    const PendingImageImport pending_import = _pending_image_imports.take(request_id);
    if (!pending_import._source_file_path.isEmpty() &&
        pending_import._source_file_path != file_path)
    {
        _imageAssetManager->ReleaseAsset(asset_info._asset_handle);
        return;
    }

    // 2. 用 UUID 区分同一资源的多次放置。
    const QString item_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QSizeF max_display_size(800.0, 600.0);
    const QSizeF display_size = QSizeF(asset_info.original_size)
                                    .scaled(max_display_size,
                                            Qt::KeepAspectRatio);
    const QPointF anchor_pos = pending_import._has_anchor_scene_pos
                                   ? pending_import._anchor_scene_pos
                                   : CanvasCenterScenePos();
    const QPointF scene_pos(anchor_pos.x() - display_size.width() / 2.0,
                            anchor_pos.y() - display_size.height() / 2.0);

    // PaintScene 负责图元所有权和撤销记录；Canvas 只传递资源元数据与初始几何。
    if (!_paintScene->addImageItem(item_id,
                                   asset_info.asset_id,
                                   asset_info.asset_ref,
                                   asset_info.asset_sha256,
                                   asset_info.mime_type,
                                   asset_info.original_size,
                                   pixmap,
                                   scene_pos,
                                   display_size,
                                   _room_info->offline))
    {
        _imageAssetManager->ReleaseAsset(asset_info._asset_handle);
        TipWidget::showTip(ui->graphicsView, QStringLiteral("图片图元创建失败"));
        return;
    }

    if (_room_info->offline)
    {
        // 3. 离线图片像素已驻留 UI，立即解除文件保护。
        _imageAssetManager->ReleaseAsset(asset_info._asset_handle);
        TipWidget::showTip(ui->graphicsView, QStringLiteral("图片已插入离线画布"));
        return;
    }

    // 4. 在线上传从签名等待到 PUT 完成持续持有句柄。
    PendingImageUpload pending_upload;
    pending_upload.item_id = item_id;
    pending_upload.suffix = ImageSuffixForMimeType(asset_info.mime_type);
    pending_upload.asset_info = asset_info;

    //双容器做法，增加查询效率
    _pendingImageUploads.insert(item_id, pending_upload);   //哈希表存真实数据
    _imageUploadQueue.enqueue(item_id);                     //队列只存ID
    requestNextImageUploadToken();
    TipWidget::showTip(ui->graphicsView,
                       QStringLiteral("图片正在上传，完成后同步到房间"));
}

void Canvas::slot_onLocalAssetFailed(QString request_id,
                                     QString file_path,
                                     QString error_message)
{
    // 失败只清理对应请求，不影响已插入的其他图元或正在上传的资源。
    if (!_pending_image_imports.contains(request_id))
    {
        return;
    }
    const PendingImageImport pending_import = _pending_image_imports.take(request_id);
    if (!pending_import._source_file_path.isEmpty() &&
        pending_import._source_file_path != file_path)
    {
        return;
    }
    TipWidget::showTip(ui->graphicsView,
                       error_message.isEmpty()
                           ? QStringLiteral("图片读取失败")
                           : error_message);
}

void Canvas::requestNextImageUploadToken()
{
    // 1. 已在等待签名或 PUT 的资源不能被新导入重复发起签名。
    if (!_activeImageUploadId.isEmpty())
    {
        return;
    }
    if (_activeImageUploadId.isEmpty())
    {
        while (!_imageUploadQueue.isEmpty())
        {
            const QString candidate_id = _imageUploadQueue.dequeue();
            // 如果这个号在寄存柜里找不到了，说明用户在这期间把图删了
            if (_pendingImageUploads.contains(candidate_id))
            {
                _activeImageUploadId = candidate_id;
                break;
            }
        }
    }
    if (_activeImageUploadId.isEmpty())
    {
        return;
    }

    const PendingImageUpload& pending_upload = _pendingImageUploads.value(_activeImageUploadId);
    const auto user_info = UserMgr::getInstance()->getMyInfo();
    if (!user_info || !_room_info || _room_info->offline)
    {
        return;
    }

    // 2. 登录凭证仅用于网关签名，随后由后台代理上传图片。
    QJsonObject request;
    request[QStringLiteral("uid")] = user_info->_id;
    request[QStringLiteral("token")] = UserMgr::getInstance()->getToken();
    request[QStringLiteral("room_id")] = _room_info->id;
    request[QStringLiteral("suffix")] = pending_upload.suffix;
    request[QStringLiteral("mime_type")] = pending_upload.asset_info.mime_type;
    request[QStringLiteral("file_size")] = static_cast<qint64>(pending_upload.asset_info.byte_size);
    request[QStringLiteral("width")] = pending_upload.asset_info.original_size.width();
    request[QStringLiteral("height")] = pending_upload.asset_info.original_size.height();
    _upload_signature_request_id = HttpMgr::getInstance()->PostImageSignature(
        QUrl(gate_url_prefix + QStringLiteral("/get_image_upload_token")),
        request,
        ReqId::ID_GET_IMAGE_UPLOAD_TOKEN);
}

void Canvas::requestNextImageDownloadToken()
{
    if (_imageDownloadRequestActive || _imageDownloadQueue.isEmpty() ||
        !_room_info || _room_info->offline)
    {
        return;
    }

    _activeImageDownload = _imageDownloadQueue.dequeue();
    _imageDownloadRequestActive = true;
    // 1. 先异步查缓存，命中不访问网关或 OSS。
    _imageAssetManager->LoadAssetAsync(_activeImageDownload.asset_id,
        _activeImageDownload.asset_sha256, _activeImageDownload.mime_type);
}

void Canvas::RequestImageDownloadToken(QString asset_id)
{
    // 1. 只为仍然活动的缓存未命中资源申请签名。
    if (!_imageDownloadRequestActive || _activeImageDownload.asset_id != asset_id || !_room_info || _room_info->offline)
    {
        return;
    }
    const auto user_info = UserMgr::getInstance()->getMyInfo();
    if (!user_info)
    {
        _imageDownloadRequestActive = false;
        return;
    }

    // 2. 签名绑定房间与对象引用，登录 Token 只交给网关。
    QJsonObject request;
    request[QStringLiteral("uid")] = user_info->_id;
    request[QStringLiteral("token")] = UserMgr::getInstance()->getToken();
    request[QStringLiteral("room_id")] = _room_info->id;
    request[QStringLiteral("asset_id")] = _activeImageDownload.asset_id;
    request[QStringLiteral("asset_ref")] = _activeImageDownload.asset_ref;
    _download_signature_request_id = HttpMgr::getInstance()->PostImageSignature(
        QUrl(gate_url_prefix + QStringLiteral("/get_image_download_token")),
        request,
        ReqId::ID_GET_IMAGE_DOWNLOAD_TOKEN);
}

void Canvas::sendImageCreateOperation(const QString& item_id)
{
    // 1. 上传完成也可能晚于权限撤销或断线，发送前再次检查当前房间状态。
    if (!_room_info || _room_info->offline || !_room_info->connected ||
        !_room_info->can_edit || !_paintScene || !_paintScene->isEditable())
    {
        return;
    }

    ImageItem* image_item = _paintScene->findImageItem(item_id);
    if (!image_item || image_item->assetId().isEmpty() || image_item->assetRef().isEmpty())
    {
        return;
    }

    const auto user_info = UserMgr::getInstance()->getMyInfo();
    if (!user_info)
    {
        return;
    }

    // 2. 创建包使用图元当前变换，预览期间的移动无需提前发送更新包。
    message::ImageOperation operation;
    operation.set_uid(user_info->_id);
    operation.set_room_id(_room_info->id.toStdString());
    operation.set_operation_id(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
    operation.set_operation_type(message::IMAGE_CREATE);
    operation.set_server_sequence(0);

    message::ImageItem* proto_item = operation.mutable_item();
    proto_item->set_item_id(item_id.toStdString());
    proto_item->set_asset_id(image_item->assetId().toStdString());
    proto_item->set_asset_ref(image_item->assetRef().toStdString());
    proto_item->set_asset_sha256(image_item->assetSha256().toStdString());
    proto_item->set_mime_type(image_item->mimeType().toStdString());
    proto_item->set_original_width(static_cast<uint32_t>(image_item->originalSize().width()));
    proto_item->set_original_height(static_cast<uint32_t>(image_item->originalSize().height()));
    message::ImageTransform* transform = proto_item->mutable_transform();
    transform->set_x(static_cast<float>(image_item->pos().x()));
    transform->set_y(static_cast<float>(image_item->pos().y()));
    transform->set_width(static_cast<float>(image_item->displaySize().width()));
    transform->set_height(static_cast<float>(image_item->displaySize().height()));
    transform->set_scale_x(static_cast<float>(image_item->transform().m11()));
    transform->set_scale_y(static_cast<float>(image_item->transform().m22()));
    transform->set_rotation(static_cast<float>(image_item->rotation()));

    // 3. 沿用创建协议发送初始状态，服务端仍执行会话及房间权限校验。
    std::string serialized_operation;
    if (!operation.SerializeToString(&serialized_operation))
    {
        // 序列化失败时不从本地场景删除预览，让用户仍能看到图片并可重新触发上传。
        return;
    }
    TcpMgr::getInstance()->slot_send_data(
        ReqId::ID_IMAGE_OPERATION_REQ,
        QByteArray::fromStdString(serialized_operation));
}

void Canvas::sendImageTransformOperation(const QString& item_id,
                                         const QRectF& scene_rect,
                                         qreal rotation,
                                         qreal scale)
{
    Q_UNUSED(scene_rect);
    // 1. 仅允许仍连接且拥有编辑权限的在线成员提交变换。
    if (!_room_info || _room_info->offline || !_room_info->connected ||
        !_room_info->can_edit || !_paintScene || !_paintScene->isEditable() ||
        _pendingImageUploads.contains(item_id))
    {
        return;
    }
    ImageItem* image_item = _paintScene->findImageItem(item_id);
    const auto user_info = UserMgr::getInstance()->getMyInfo();
    if (!image_item || !user_info || image_item->assetId().isEmpty())
    {
        return;
    }

    // 2. 变换广播只携带元数据，使用当前局部尺寸和位置重建可重复的场景状态。
    message::ImageOperation operation;
    operation.set_uid(user_info->_id);
    operation.set_room_id(_room_info->id.toStdString());
    operation.set_operation_id(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
    operation.set_operation_type(message::IMAGE_UPDATE_TRANSFORM);
    operation.set_server_sequence(0);
    message::ImageItem* proto_item = operation.mutable_item();
    proto_item->set_item_id(item_id.toStdString());
    proto_item->set_asset_id(image_item->assetId().toStdString());
    proto_item->set_asset_ref(image_item->assetRef().toStdString());
    proto_item->set_asset_sha256(image_item->assetSha256().toStdString());
    proto_item->set_mime_type(image_item->mimeType().toStdString());
    proto_item->set_original_width(static_cast<uint32_t>(image_item->originalSize().width()));
    proto_item->set_original_height(static_cast<uint32_t>(image_item->originalSize().height()));
    message::ImageTransform* transform = proto_item->mutable_transform();
    transform->set_x(static_cast<float>(image_item->pos().x()));
    transform->set_y(static_cast<float>(image_item->pos().y()));
    transform->set_width(static_cast<float>(image_item->displaySize().width()));
    transform->set_height(static_cast<float>(image_item->displaySize().height()));
    transform->set_scale_x(static_cast<float>(image_item->transform().m11()));
    transform->set_scale_y(static_cast<float>(image_item->transform().m22()));
    transform->set_rotation(static_cast<float>(rotation));
    if (transform->scale_x() <= 0.0F)
    {
        transform->set_scale_x(static_cast<float>(scale > 0.0 ? scale : 1.0));
    }
    if (transform->scale_y() <= 0.0F)
    {
        transform->set_scale_y(static_cast<float>(scale > 0.0 ? scale : 1.0));
    }

    // 3. 沿用图片协议和服务端权限校验，不将视口滚动或交互模式写入消息。
    std::string serialized_operation;
    if (!operation.SerializeToString(&serialized_operation))
    {
        return;
    }
    TcpMgr::getInstance()->slot_send_data(
        ReqId::ID_IMAGE_OPERATION_REQ,
        QByteArray::fromStdString(serialized_operation));
}

void Canvas::sendImageDeleteOperation(const QString& item_id)
{
    if (!_room_info || _room_info->offline || item_id.isEmpty())
    {
        return;
    }

    const auto user_info = UserMgr::getInstance()->getMyInfo();
    if (!user_info)
    {
        return;
    }

    // 删除操作只提交稳定 item_id；服务端从房间索引取出资源元数据，避免客户端伪造资源引用。
    message::ImageOperation operation;
    operation.set_uid(user_info->_id);
    operation.set_room_id(_room_info->id.toStdString());
    operation.set_operation_id(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
    operation.set_operation_type(message::IMAGE_DELETE);
    operation.set_target_item_id(item_id.toStdString());

    std::string serialized_operation;
    if (!operation.SerializeToString(&serialized_operation))
    {
        // 序列化失败时保留本地图元，避免用户界面先消失而服务端仍保留图片。
        return;
    }
    TcpMgr::getInstance()->slot_send_data(
        ReqId::ID_IMAGE_OPERATION_REQ,
        QByteArray::fromStdString(serialized_operation));
}

void Canvas::applyRemoteImageOperation(const message::ImageOperation& operation)
{
    if (!_paintScene || !_room_info || _room_info->offline)
    {
        return;
    }

    // 服务端已经完成权限检查，客户端仍拒绝明显不完整的数据，防止损坏历史破坏本地场景。
    if (operation.room_id() != _room_info->id.toStdString())
    {
        return;
    }

    if (operation.operation_type() == message::IMAGE_CREATE)
    {
        if (!operation.has_item() || !operation.item().has_transform() ||
            operation.item().item_id().empty() || operation.item().asset_id().empty() ||
            operation.item().asset_ref().empty())
        {
            return;
        }

        const message::ImageItem& proto_item = operation.item();
        const message::ImageTransform& transform = proto_item.transform();
        const QSize original_size(static_cast<int>(proto_item.original_width()),
                                  static_cast<int>(proto_item.original_height()));
        const QSizeF display_size(transform.width(), transform.height());
        const QPointF scene_pos(transform.x(), transform.y());
        ImageItem* image_item = _paintScene->addImageItem(
            QString::fromStdString(proto_item.item_id()),
            QString::fromStdString(proto_item.asset_id()),
            QString::fromStdString(proto_item.asset_ref()),
            QString::fromStdString(proto_item.asset_sha256()),
            QString::fromStdString(proto_item.mime_type()),
            original_size,
            QPixmap(),  //传入占位图片
            scene_pos,
            display_size,
            false);
        if (!image_item)
        {
            return;
        }
        _applyingRemoteImageOperation = true;
        _paintScene->updateImageTransform(QString::fromStdString(proto_item.item_id()),
                                          scene_pos,
                                          display_size,
                                          transform.scale_x() > 0.0F ? transform.scale_x() : 1.0,
                                          transform.scale_y() > 0.0F ? transform.scale_y() : 1.0,
                                          transform.rotation());
        _applyingRemoteImageOperation = false;

        // 图片二进制不经过 CanvasServer，接收端使用 GateServer 签名后从 OSS 异步下载。
        _imageDownloadQueue.enqueue(PendingImageDownload{
            QString::fromStdString(proto_item.item_id()),
            QString::fromStdString(proto_item.asset_id()),
            QString::fromStdString(proto_item.asset_ref()),
            QString::fromStdString(proto_item.asset_sha256()),
            QString::fromStdString(proto_item.mime_type())});
        requestNextImageDownloadToken();
        return;
    }

    const QString target_item_id = operation.operation_type() == message::IMAGE_DELETE
                                       ? QString::fromStdString(operation.target_item_id())
                                       : (operation.has_item()
                                              ? QString::fromStdString(operation.item().item_id())
                                              : QString());
    if (target_item_id.isEmpty())
    {
        return;
    }
    if (operation.operation_type() == message::IMAGE_DELETE)
    {
        _paintScene->removeImageItem(target_item_id);
        return;
    }
    if (operation.operation_type() == message::IMAGE_UPDATE_TRANSFORM &&
        operation.has_item() && operation.item().has_transform())
    {
        const message::ImageTransform& transform = operation.item().transform();
        _applyingRemoteImageOperation = true;
        _paintScene->updateImageTransform(
            target_item_id,
            QPointF(transform.x(), transform.y()),
            QSizeF(transform.width(), transform.height()),
            transform.scale_x() > 0.0F ? transform.scale_x() : 1.0,
            transform.scale_y() > 0.0F ? transform.scale_y() : 1.0,
            transform.rotation());
        _applyingRemoteImageOperation = false;
    }
}

void Canvas::slot_onImageOperationBroadcast(QByteArray data)
{
    if (_room_info && _room_info->offline)
    {
        return;
    }

    message::ImageOperation operation;
    if (!operation.ParseFromArray(data.constData(), data.size()))
    {
        qWarning() << "[Canvas] 图片操作 protobuf 解析失败";
        return;
    }
    if (operation.uid() == UserMgr::getInstance()->getUid())
    {
        // 本地预览已经存在，忽略自己的广播可以避免重复下载和重复创建。
        return;
    }
    if (!_room_info || !_room_info->connected)
    {
        // JoinRoomRsp 之后的历史操作可能先于 UI 房间状态信号到达，暂存后由定时器在 connected 后回放。
        _remoteImageOperationQueue.enqueue(operation);
        return;
    }
    applyRemoteImageOperation(operation);
}

void Canvas::slot_onImageAssetHttpFinished(ReqId reqid,
                                           QString response,
                                           ErrorCodes error)
{
    if (reqid != ReqId::ID_GET_IMAGE_UPLOAD_TOKEN &&
        reqid != ReqId::ID_GET_IMAGE_DOWNLOAD_TOKEN)
    {
        return;
    }

    QJsonParseError parse_error;
    const QJsonDocument response_document = QJsonDocument::fromJson(response.toUtf8(), &parse_error);
    const bool response_valid = parse_error.error == QJsonParseError::NoError &&
                                response_document.isObject();
    const QJsonObject response_object = response_valid ? response_document.object() : QJsonObject();
    const bool request_success = error == ErrorCodes::SUCCESS &&
                                 response_valid &&
                                 response_object.value(QStringLiteral("error")).toInt(-1) == 0;

    if (reqid == ReqId::ID_GET_IMAGE_UPLOAD_TOKEN)
    {
        if (_activeImageUploadId.isEmpty() || !_pendingImageUploads.contains(_activeImageUploadId))
        {
            return;
        }
        const QString item_id = _activeImageUploadId;
        PendingImageUpload& pending_upload = _pendingImageUploads[item_id];
        if (!request_success ||
            !response_object.contains(QStringLiteral("url")) ||
            !response_object.contains(QStringLiteral("asset_id")) ||
            !response_object.contains(QStringLiteral("asset_ref")))
        {
            _imageAssetManager->ReleaseAsset(pending_upload.asset_info._asset_handle);
            _paintScene->removeImageItem(item_id);
            _pendingImageUploads.remove(item_id);
            _activeImageUploadId.clear();
            requestNextImageUploadToken();
            return;
        }

        const QString asset_id = response_object.value(QStringLiteral("asset_id")).toString();
        const QString asset_ref = response_object.value(QStringLiteral("asset_ref")).toString();
        pending_upload.asset_info.asset_id = asset_id;
        pending_upload.asset_info.asset_ref = asset_ref;
        if (ImageItem* image_item = _paintScene->findImageItem(item_id))
        {
            image_item->setAssetMetadata(asset_id,
                                         asset_ref,
                                         pending_upload.asset_info.asset_sha256,
                                         pending_upload.asset_info.mime_type);
        }
        _imageAssetManager->uploadAsset(asset_id,
                                        pending_upload.asset_info._asset_handle,
                                        QUrl(response_object.value(QStringLiteral("url")).toString()),
                                        pending_upload.asset_info.mime_type,
                                        pending_upload.asset_info.asset_sha256,
                                        pending_upload.asset_info.byte_size);
        return;
    }

    if (!_imageDownloadRequestActive)
    {
        return;
    }
    if (!request_success || !response_object.contains(QStringLiteral("url")))
    {
        if (ImageItem* image_item = _paintScene->findImageItem(_activeImageDownload.item_id))
        {
            image_item->setErrorMessage(QStringLiteral("无法获取图片下载地址"));
            image_item->setLoadState(ImageItem::ImageLoadState::Failed);
        }
        _imageDownloadRequestActive = false;
        requestNextImageDownloadToken();
        return;
    }

    // 代理下载前复查缓存，合并跨窗口下载。
    _imageAssetManager->downloadAsset(
        _activeImageDownload.asset_id,
        QUrl(response_object.value(QStringLiteral("url")).toString()),
        _activeImageDownload.asset_sha256,
        _activeImageDownload.mime_type);
}

void Canvas::slot_onImageAssetReady(QString asset_id,
                                    QPixmap pixmap,
                                    QString sha256,
                                    QSize original_size,
                                    QString mime_type)
{
    // 1. UI 只持有像素和元数据，不保存缓存路径。
    if (!_paintScene)
    {
        return;
    }
    updateImageItemsForAsset(asset_id, pixmap, original_size);
    if (_imageDownloadRequestActive && _activeImageDownload.asset_id == asset_id)
    {
        _imageDownloadRequestActive = false;
        _activeImageDownload = PendingImageDownload();
        requestNextImageDownloadToken();
    }
    Q_UNUSED(sha256);
    Q_UNUSED(mime_type);
}

void Canvas::slot_onImageAssetFailed(QString asset_id, QString error_message)
{
    if (_paintScene)
    {
        const QList<QGraphicsItem*> scene_items = _paintScene->items();
        for (QGraphicsItem* graphics_item : scene_items)
        {
            auto* image_item = dynamic_cast<ImageItem*>(graphics_item);
            if (image_item && image_item->assetId() == asset_id)
            {
                image_item->setErrorMessage(error_message);
                image_item->setLoadState(ImageItem::ImageLoadState::Failed);
            }
        }
    }
    if (_imageDownloadRequestActive && _activeImageDownload.asset_id == asset_id)
    {
        _imageDownloadRequestActive = false;
        _activeImageDownload = PendingImageDownload();
        requestNextImageDownloadToken();
    }
}

void Canvas::slot_onImageRetryRequested(QString item_id)
{
    // 离线房间不发起任何网络请求；失败占位在离线模式只出现在本地读取失败场景，不提供重试入口。
    if (!_paintScene || !_room_info || _room_info->offline)
    {
        return;
    }

    ImageItem* image_item = _paintScene->findImageItem(item_id);
    if (!image_item || image_item->assetId().isEmpty() || image_item->assetRef().isEmpty())
    {
        // 元数据不完整的图元无法通过网关校验，重试只会产生一次注定失败的签名请求。
        return;
    }

    // 签名接口严格串行，同一图元重复入队只会让失败图元多次排队，先做请求级去重。
    if (_activeImageDownload.item_id == item_id)
    {
        return;
    }
    for (const PendingImageDownload& pending : _imageDownloadQueue)
    {
        if (pending.item_id == item_id)
        {
            return;
        }
    }

    // 先回到 Loading 占位给用户即时反馈，再走与首次下载完全相同的签名、下载和哈希校验链路。
    image_item->setLoadState(ImageItem::ImageLoadState::Loading);
    _imageDownloadQueue.enqueue(PendingImageDownload{
        item_id,
        image_item->assetId(),
        image_item->assetRef(),
        image_item->assetSha256(),
        image_item->mimeType()});
    requestNextImageDownloadToken();
}

void Canvas::slot_onImagePreviewRequested(QString item_id)
{
    if (!_paintScene)
    {
        return;
    }

    ImageItem* image_item = _paintScene->findImageItem(item_id);
    if (!image_item || image_item->loadState() != ImageItem::ImageLoadState::Ready)
    {
        // 只有完整加载并校验通过的图元才允许预览，避免对 Loading/Failed 状态创建空窗口。
        return;
    }

    const QPixmap pixmap = image_item->pixmap();
    if (pixmap.isNull())
    {
        return;
    }

    // 预览窗口只持有 QPixmap 副本，关闭对话框后不会改变画布图元和资源缓存的生命周期。
    _imageAssetManager->TouchAssetAsync(image_item->assetSha256());
    ImagePreviewDialog preview_dialog(pixmap, this);
    preview_dialog.exec();
}

void Canvas::slot_onImageAssetUploaded(QString asset_id, QString sha256, QString mime_type)
{
    for (auto iterator = _pendingImageUploads.begin(); iterator != _pendingImageUploads.end(); ++iterator)
    {
        if (iterator.value().asset_info.asset_id != asset_id)
        {
            continue;
        }
        const QString item_id = iterator.key();
        if (ImageItem* image_item = _paintScene->findImageItem(item_id))
        {
            image_item->setAssetMetadata(asset_id,
                                         iterator.value().asset_info.asset_ref,
                                         sha256,
                                         mime_type);
        }
        sendImageCreateOperation(item_id);
        _imageAssetManager->ReleaseAsset(iterator.value().asset_info._asset_handle);
        _pendingImageUploads.erase(iterator);
        _activeImageUploadId.clear();
        requestNextImageUploadToken();
        return;
    }
}

void Canvas::slot_onImageAssetUploadFailed(QString asset_id, QString error_message)
{
    Q_UNUSED(error_message);
    for (auto iterator = _pendingImageUploads.begin(); iterator != _pendingImageUploads.end(); ++iterator)
    {
        if (iterator.value().asset_info.asset_id != asset_id)
        {
            continue;
        }
        const QString item_id = iterator.key();
        _imageAssetManager->ReleaseAsset(iterator.value().asset_info._asset_handle);
        _paintScene->removeImageItem(item_id);
        _pendingImageUploads.erase(iterator);
        _activeImageUploadId.clear();
        requestNextImageUploadToken();
        return;
    }
}

void Canvas::slot_onImageDeleteRequested(QString item_id)
{
    if (!_paintScene || item_id.isEmpty() || !_room_info || !_room_info->can_edit)
    {
        return;
    }

    // 上传尚未完成的本地预览没有进入房间历史，只清理队列和图元，不发送无效删除包。
    if (_pendingImageUploads.contains(item_id))
    {
        _imageAssetManager->CancelAssetUpload(_pendingImageUploads.value(item_id).asset_info._asset_handle);
        _pendingImageUploads.remove(item_id);
        if (_activeImageUploadId == item_id)
        {
            HttpMgr::getInstance()->CancelImageSignature(_upload_signature_request_id);
            _upload_signature_request_id.clear();
            _activeImageUploadId.clear();
            requestNextImageUploadToken();
        }
        _paintScene->removeImageItem(item_id);
        return;
    }

    if (_room_info->offline)
    {
        _paintScene->removeImageItem(item_id);
        return;
    }

    // 先发送稳定删除操作再移除本地图元，保持失败时的诊断依据和服务端状态一致性。
    sendImageDeleteOperation(item_id);
    _paintScene->removeImageItem(item_id);
}

void Canvas::slot_onImageGeometryChanged(QString item_id,
                                         QRectF scene_rect,
                                         qreal rotation,
                                         qreal scale)
{
    // 1. 远端回放不能再次作为本地编辑广播；权限和上传状态由发送入口二次检查。
    if (_applyingRemoteImageOperation || !_room_info || _room_info->offline)
    {
        return;
    }
    sendImageTransformOperation(item_id, scene_rect, rotation, scale);
}

void Canvas::HandleAssetServiceLost()
{
    // 1. 已显示图元保留 QPixmap，未完成上传不重放失效句柄。
    HttpMgr::getInstance()->CancelImageSignature(_upload_signature_request_id);
    _upload_signature_request_id.clear();
    for (const auto& pending : std::as_const(_pendingImageUploads))
    {
        _paintScene->removeImageItem(pending.item_id);
    }
    _pendingImageUploads.clear();
    _imageUploadQueue.clear();
    _activeImageUploadId.clear();
    qWarning() << "Canvas HandleAssetServiceLost: pending uploads canceled";
}

void Canvas::CancelImageRequests()
{
    // 1. 撤销签名，旧房间 HTTP 回包不会匹配新房间请求。
    HttpMgr::getInstance()->CancelImageSignature(_upload_signature_request_id);
    HttpMgr::getInstance()->CancelImageSignature(_download_signature_request_id);
    _upload_signature_request_id.clear();
    _download_signature_request_id.clear();
    // 2. 取消本画板的代理任务和句柄。
    _imageAssetManager->CancelAll();
}

void Canvas::HandleImageSignatureFinished(QString request_id, ReqId reqid, QString response, ErrorCodes error)
{
    // 1. UUID 匹配后才允许签名更新当前图片资源。
    if (reqid == ReqId::ID_GET_IMAGE_UPLOAD_TOKEN && request_id == _upload_signature_request_id)
    {
        _upload_signature_request_id.clear();
    } else if (reqid == ReqId::ID_GET_IMAGE_DOWNLOAD_TOKEN && request_id == _download_signature_request_id)
    {
        _download_signature_request_id.clear();
    } else
    {
        return;
    }
    // 2. 复用既有网关响应校验，不更改远端业务协议。
    slot_onImageAssetHttpFinished(reqid, response, error);
}

void Canvas::updateImageItemsForAsset(const QString& asset_id,
                                      const QPixmap& pixmap,
                                      const QSize& original_size)
{
    if (!_paintScene || pixmap.isNull())
    {
        return;
    }
    const QList<QGraphicsItem*> scene_items = _paintScene->items();
    for (QGraphicsItem* graphics_item : scene_items)
    {
        auto* image_item = dynamic_cast<ImageItem*>(graphics_item);
        if (!image_item || image_item->assetId() != asset_id)
        {
            continue;
        }
        image_item->setOriginalSize(original_size);
        image_item->setPixmap(pixmap);
    }
}

static int32_t ToArgbInt(const QColor& c)   //Qt颜色转 int
{
    // Qt 的 rgba() 是 0xAARRGGBB
    return static_cast<int32_t>(c.rgba());
}

//画笔开始槽函数
void Canvas::slot_onStrokeStart(QString uuid, int type, QPointF startPos, QColor color, int width)
{
    if (!_room_info) return;
    if (_room_info->offline) return;    // 离线模式只本地绘制，不发送 START 网络包

    if (_strokeFlushTimer && !_strokeFlushTimer->isActive())    // flush启动定时器
        _strokeFlushTimer->start(16);

    // 路径型工具通过工厂声明，网络层不再重复维护 Pen/Eraser 类型判断。
    const bool isPathBased = DrawToolFactory::isPathBased(static_cast<ShapeType>(type));

    if (isPathBased)
    {
        PendingStrokePoints& pendingStroke = _pendingPointsByUuid[uuid];
        pendingStroke.type = type;
        pendingStroke.points.clear();
        pendingStroke.active = true;
    }

    // ---------- 发送 START ----------
    message::DrawReq req;
    req.set_uid(UserMgr::getInstance()->getMyInfo()->_id);
    req.set_item_id(uuid.toStdString());
    req.set_cmd(message::CMD_START);
    req.set_shape(static_cast<message::ShapeType>(type));
    req.set_color(ToArgbInt(color));
    req.set_width(width);

    // START 必须带起点，远端才能 moveTo 正确位置
    req.set_start_x(startPos.x());
    req.set_start_y(startPos.y());
    req.set_current_x(startPos.x());
    req.set_current_y(startPos.y());
    req.set_send_timestamp_ms(static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch()));  //发送当前时间戳

    std::string binaryData;
    if (!req.SerializeToString(&binaryData)) return;
    qDebug() << "[Draw] cmd=" << req.cmd()
             << " shape=" << req.shape()
             << " uid=" << req.uid()
             << " uuid=" << uuid
             << " bytes=" << (int)binaryData.size();

    TcpMgr::getInstance()->slot_send_data(ReqId::ID_DRAW_REQ, QByteArray::fromStdString(binaryData));
}

//画笔移动槽函数，Pen/Eraser “只缓存点”，几何图形仍然直接发
void Canvas::slot_onStrokeMove(QString uuid, int type, QPointF currentPos)
{
    if (!_room_info) return;
    if (_room_info->offline) return;    // 离线模式只本地绘制，不发送 MOVE 网络包

    // 路径型工具只缓存点，几何工具继续直接发送预览位置。
    const bool isPathBased = DrawToolFactory::isPathBased(static_cast<ShapeType>(type));

    if (isPathBased)
    {
        // 正常流程下 START 会创建 entry；这里再保证一下健壮性（防止乱序/极端情况）
        PendingStrokePoints& pendingStroke = _pendingPointsByUuid[uuid];
        pendingStroke.type = type;
        pendingStroke.active = true;
        pendingStroke.points.push_back(currentPos);
        return;
    }

    // 几何图形：直接发 MOVE（数据小，实时预览重要）
    message::DrawReq req;
    req.set_uid(UserMgr::getInstance()->getMyInfo()->_id);
    req.set_item_id(uuid.toStdString());
    req.set_cmd(message::CMD_MOVE);
    req.set_shape(static_cast<message::ShapeType>(type));
    req.set_current_x(currentPos.x());
    req.set_current_y(currentPos.y());
    req.set_send_timestamp_ms(static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch()));  //发送当前时间戳（几何图形）

    std::string binaryData;
    if (!req.SerializeToString(&binaryData))
    {
        qDebug() << "[Draw] Serialize failed!";
        return;
    }

    // 输出调试
    // message::DrawReq selfCheck;
    // if (!selfCheck.ParseFromString(binaryData))
    // {
    //     qDebug() << "[Draw] Serialize self-check failed! bytes=" << binaryData.size();
    // }
    // else
    // {
    //     qDebug() << "[Draw START] bytes=" << binaryData.size()
    //     << " uid=" << selfCheck.uid()
    //     << " cmd=" << selfCheck.cmd();
    // }
    // qDebug() << "[Draw] cmd=" << req.cmd()
    //          << " shape=" << req.shape()
    //          << " uid=" << req.uid()
    //          << " uuid=" << uuid
    //          << " bytes=" << (int)binaryData.size();

    TcpMgr::getInstance()->slot_send_data(ReqId::ID_DRAW_REQ, QByteArray::fromStdString(binaryData));
}

//画笔结束槽函数，先 flush 剩余点，再发 END，并把 active 关掉
void Canvas::slot_onStrokeEnd(QString uuid, int type, QPointF endPos)
{
    if (!_room_info) return;
    if (_room_info->offline) return;    // 离线模式只本地绘制，不发送 END 网络包

    // 结束时仍通过工厂识别路径型工具，以确保最后一个点先被 flush。
    const bool isPathBased = DrawToolFactory::isPathBased(static_cast<ShapeType>(type));

    if (isPathBased)
    {
        // 把最后点塞进缓存，确保不丢
        PendingStrokePoints& pendingStroke = _pendingPointsByUuid[uuid];
        pendingStroke.points.push_back(endPos);

        // 立刻把剩余点打包发送（MOVE）
        flushStrokePoints(uuid, true);

        // 标记结束 + 清理 entry，防止 map 越积越大
        pendingStroke.active = false;
        _pendingPointsByUuid.remove(uuid);
    }

    // 发 END（几何图形/笔画都发，作为结束标志）
    message::DrawReq req;
    req.set_uid(UserMgr::getInstance()->getMyInfo()->_id);
    req.set_item_id(uuid.toStdString());
    req.set_cmd(message::CMD_END);
    req.set_shape(static_cast<message::ShapeType>(type));
    req.set_current_x(endPos.x());
    req.set_current_y(endPos.y());
    req.set_send_timestamp_ms(static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch()));  //发送当前时间戳

    std::string binaryData;
    if (!req.SerializeToString(&binaryData)) return;

    TcpMgr::getInstance()->slot_send_data(ReqId::ID_DRAW_REQ, QByteArray::fromStdString(binaryData));
}

//收到绘画广播
void Canvas::slot_onDrawBroadcast(QByteArray data)
{
    if (_room_info && _room_info->offline)
        return;

    message::DrawReq req;
    if (!req.ParseFromArray(data.data(), data.size()))
        return;

    //不收自己发送的
    int myUid = UserMgr::getInstance()->getMyInfo()->_id;
    if (req.uid() == myUid) return;

    const bool is_test_packet = _latencyTestController &&
                                _latencyTestController->isTestPacket(req);

    if (is_test_packet)
    {
        // 测试消息在入队前记录，便于和实际 apply 阶段区分网络/事件循环延迟。
        _latencyTestController->recordReceived(req, _remoteDrawQueue.size());
    }

    // ===== 延迟测量 =====
    if (!is_test_packet && req.send_timestamp_ms() > 0)
    {
        quint64 now = QDateTime::currentMSecsSinceEpoch();      //当前时间戳
        qint64 latency = now - static_cast<qint64>(req.send_timestamp_ms());    //延迟，现在 - 发送

        // if (_latencyCount < 20 || _latencyCount % 100 == 0)
        // {
        //     qDebug() << "[Latency raw]"
        //              << "send=" << req.send_timestamp_ms()
        //              << "now=" << now
        //              << "latency=" << latency
        //              << "uid=" << req.uid()
        //              << "cmd=" << req.cmd()
        //              << "shape=" << req.shape();
        // }

        // 丢弃明显异常值（时钟不同步等导致的负数或超大值）
        if (latency >= 0 && latency < 10000)
        {
            _latencySamples.append(latency);
            _latencySum += latency;
            _latencyCount++;

            // 每收到 100 个采样，打印一次统计
            if (_latencyCount % 100 == 0)
            {
                // 计算 P50 / P99
                QList<qint64> sorted = _latencySamples;
                std::sort(sorted.begin(), sorted.end());
                qint64 p50 = sorted[sorted.size() / 2];
                qint64 p99 = sorted[static_cast<int>(sorted.size() * 0.99)];
                double avg = static_cast<double>(_latencySum) / _latencyCount;

                qDebug() << "========== 绘画同步延迟统计 ==========";
                qDebug() << "  采样数:" << _latencyCount;
                qDebug() << "  平均延迟:" << QString::number(avg, 'f', 1) << "ms";
                qDebug() << "  P50:" << p50 << "ms";
                qDebug() << "  P99:" << p99 << "ms";
                qDebug() << "  最小:" << sorted.first() << "ms";
                qDebug() << "  最大:" << sorted.last() << "ms";
                qDebug() << "=====================================";

                // 保留最近 1000 个样本，避免内存无限增长
                if (_latencySamples.size() > 1000)
                    _latencySamples = _latencySamples.mid(_latencySamples.size() - 500);
            }
        }
    }

    // 不直接绘制，而是先进入远端绘画队列。
    // 原因：公网下包到达不稳定，可能 80ms 没包、随后连续到好几个包；
    // 如果这里立刻 applyRemoteDraw，会在 UI 上表现为“停一下、突然跳一段”。
    // 入队后由 flushRemoteDrawQueue() 按固定节奏应用，视觉上会更平滑。
    _remoteDrawQueue.enqueue(req);
}

void Canvas::flushRemoteDrawQueue()
{
    if (!_paintScene)
        return;
    // JoinRoomRsp 到达后，Canvas 还需要完成房间状态和画布尺寸初始化。
    // 历史绘画包先保留在队列里，等 connected 为 true 后再应用，避免初始化时序导致丢图。
    if (!_room_info || !_room_info->connected)
        return;
    if (_room_info && _room_info->offline)
    {
        _remoteDrawQueue.clear();
        _remoteImageOperationQueue.clear();
        return;
    }

    // 图片历史不进入 DrawReq 队列，但使用同一刷新节奏，确保 JoinRoomRsp 后的元数据不会抢在画布初始化前创建。
    int maxImageOperationsThisFrame = 4;
    if (_remoteImageOperationQueue.size() > 12)
    {
        maxImageOperationsThisFrame = 8;
    }
    int applied_image_operations = 0;
    while (!_remoteImageOperationQueue.isEmpty() &&
           applied_image_operations < maxImageOperationsThisFrame)
    {
        const message::ImageOperation operation = _remoteImageOperationQueue.dequeue();
        if (operation.uid() != UserMgr::getInstance()->getUid())
        {
            applyRemoteImageOperation(operation);
        }
        ++applied_image_operations;
    }

    // 每帧最多处理的包数。这个值太小会导致队列越积越多，太大又会退化成“一批包一次性画完”。
    // 4 包/帧是一个比较保守的起点：正常网络下足够跟上，公网抖动时也能避免 UI 线程瞬间处理太多 setPath。
    int maxPacketsThisFrame = 4;

    // 如果公网突然抖动导致积压很多包，临时增加本帧处理量，避免队列越排越长。
    // 这里仍然限制最大值，防止某一帧处理过多图元导致 UI 卡住。
    if (_remoteDrawQueue.size() > 30)
    {
        maxPacketsThisFrame = 12;
    }
    else if (_remoteDrawQueue.size() > 12)
    {
        maxPacketsThisFrame = 8;
    }

    int handled = 0;
    while (!_remoteDrawQueue.isEmpty() && handled < maxPacketsThisFrame)
    {
        message::DrawReq request = _remoteDrawQueue.dequeue();
        _paintScene->applyRemoteDraw(request);
        if (_latencyTestController && _latencyTestController->isTestPacket(request))
        {
            // 测试消息在实际应用后记录，队列长度反映本地显示排队情况。
            _latencyTestController->recordApplied(request, _remoteDrawQueue.size());
        }
        ++handled;
    }
}

// 接收群聊消息槽函数
void Canvas::slot_onChatReceived(int uid, const QString &name, const QString &avatarUrl, const QString &roomId, const QString &content, qulonglong ts)
{
    QString safe = content.toHtmlEscaped();
    ui->chat_textBrowser->append(QString("<b>%1</b>(%2): %3")
                             .arg(name)
                             .arg(uid)
                             .arg(safe));
    // 自动滚到底部
    auto* bar = ui->chat_textBrowser->verticalScrollBar();
    bar->setValue(bar->maximum());
}

//发送消息槽函数
void Canvas::slot_onSendChatClicked()
{
    QString text = ui->input_edit->text().trimmed();
    if (text.isEmpty()) return;
    if (!_room_info) return;
    if (_room_info->offline)
    {
        // 离线模式没有房间会话，先保留输入框内容，提醒用户当前不走网络聊天。
        TipWidget::showTip(ui->graphicsView, QStringLiteral("离线模式暂不支持房间聊天"));
        return;
    }

    message::ChatReq req;
    req.set_uid(UserMgr::getInstance()->getUid());                 // uid
    req.set_room_id(_room_info->id.toStdString());                 // room_id
    req.set_content(text.toStdString());
    req.set_client_ts(QDateTime::currentMSecsSinceEpoch());        // 时间戳

    std::string out;
    if (!req.SerializeToString(&out)) return;

    QByteArray bytes(out.data(), (int)out.size());


    TcpMgr::getInstance()->sig_send_data(ReqId::ID_CHAT_REQ,bytes);      // 发送给服务器
    ui->input_edit->clear();
}

void Canvas::flushStrokePoints(const QString& uuid, bool force)
{
    if (_room_info && _room_info->offline)
        return;

    auto iterator = _pendingPointsByUuid.find(uuid);
    if (iterator == _pendingPointsByUuid.end())
        return;

    PendingStrokePoints& pendingStroke = iterator.value();

    // 不在绘制中就不处理
    if (!pendingStroke.active)
        return;

    // 没点就不发（force 也不发空包）
    if (pendingStroke.points.isEmpty())
        return;

    // 【保护】每个网络包最多携带的点数，避免单包过大
    static const int MAX_POINTS_PER_PACKET = 80;

    // 按批次发送：每次取出最大 MAX_POINTS_PER_PACKET 个点发出
    while (!pendingStroke.points.isEmpty())
    {
        const int pointsToSend = qMin(MAX_POINTS_PER_PACKET, pendingStroke.points.size());

        message::DrawReq req;
        req.set_uid(UserMgr::getInstance()->getMyInfo()->_id);
        req.set_item_id(uuid.toStdString());
        req.set_cmd(message::CMD_MOVE);
        req.set_shape(static_cast<message::ShapeType>(pendingStroke.type));

        // current 用这一批的最后一个点（方便远端兜底）
        const QPointF& lastPoint = pendingStroke.points[pointsToSend - 1];
        req.set_current_x(lastPoint.x());
        req.set_current_y(lastPoint.y());

        req.set_send_timestamp_ms(static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch()));  //发送当前时间戳

        // 批量塞入 path_points
        req.mutable_path_points()->Reserve(pointsToSend);
        for (int i = 0; i < pointsToSend; ++i)
        {
            const QPointF& pt = pendingStroke.points[i];
            auto* protoPoint = req.add_path_points();
            protoPoint->set_x(pt.x());
            protoPoint->set_y(pt.y());
        }

        // 从缓存中移除已经发送的这批点
        pendingStroke.points.erase(pendingStroke.points.begin(),
                                   pendingStroke.points.begin() + pointsToSend);

        // 发送网络包
        std::string binaryData;
        if (!req.SerializeToString(&binaryData))
            return;

        TcpMgr::getInstance()->slot_send_data(ReqId::ID_DRAW_REQ, QByteArray::fromStdString(binaryData));

        // 如果不是强制 flush（force=false），最多发一包就够了，
        // 避免在一次 timer tick 里发送过多包造成“突刺”
        if (!force) // true: (END),false: (16ms time)
            break;
    }
}



void Canvas::on_return_btn_clicked()    //返回大厅
{
    if (_room_info && !_room_info->offline)
    {
        // 在线房间保留 LiveKit Room，只暂停音频，返回房间时可以立即恢复通话。
        VoiceManager::getInstance()->suspendAudio();
    }
    else
    {
        // 离线画板没有语音连接，沿用完整释放流程清理可能残留的语音资源。
        VoiceManager::getInstance()->leaveRoom();
    }

    // 离线画板没有大厅房间状态，返回时直接清空本地画布和离线房间信息。
    if (_room_info && _room_info->offline)
        resetForReconnect();

    emit sig_return_lobby();            //发送信号给mainWindow接收
}
