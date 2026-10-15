#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "tcpmgr.h"
#include "httpmgr.h"
#include "voicemanager.h"
#include "sessioncontroller.h"
#include "usermgr.h"
#include <QMessageBox>
#include <QStatusBar>
#include <QRandomGenerator>
#include <QTimer>
#include <QPainter>
#include <QFile>

MainWindow::MainWindow(const LatencyTestOptions& test_options, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    // 1. 初始化主窗口界面。
    ui->setupUi(this);

    // 2. 初始化背景动画。
    bubble_timer = new QTimer(this);

    //连接一定时间间隔更新背景
    connect(bubble_timer, &QTimer::timeout, this, &MainWindow::updateBubbles);

    bubble_timer->start(16);   //帧率
    initBubbles(50);           //初始化气泡
    setWindowTitle("SyncCanvas");
    //初始化背景 end

    // 3. 初始化欢迎页面并作为默认中央部件。
    welcome_widget = new WelcomeWidget(this);
    welcome_widget->setAttribute(Qt::WA_TranslucentBackground);     //设置透明背景
    this->setCentralWidget(welcome_widget);
    this->setFixedSize(welcome_widget->size());
    welcome_widget->show();
    //初始化欢迎页面 end

    // 4. 初始化登录页面。
    login_widget = new LoginWidget(this);
    login_widget->setAttribute(Qt::WA_TranslucentBackground);       //设置透明背景
    login_widget->hide();
    //初始化登录窗口 end

    // 5. 初始化注册页面。
    register_widget = new RegisterWidget(this);
    register_widget->setAttribute(Qt::WA_TranslucentBackground);    //设置透明背景
    register_widget->hide();
    //初始化注册窗口 end

    // 6. 初始化重置密码页面。
    reset_widget = new ResetWidget(this);
    reset_widget->setAttribute(Qt::WA_TranslucentBackground);       //设置透明背景
    reset_widget->hide();
    //初始化重置密码窗口 end

    // 7. 初始化大厅页面。
    lobby_widget = new LobbyWidget(this);
    //lobby_widget->setAttribute(Qt::WA_TranslucentBackground);       //设置透明背景
    lobby_widget->hide();
    //初始化大厅窗口 end

    // 8. 初始化独立画布窗口。
    canvas = new Canvas(test_options);
    _session_controller = new SessionController(this);
    BindSessionController();

    // 9. 绑定页面导航、房间切换和账号退出通知。
    connect(welcome_widget,&WelcomeWidget::switchLogin,this,&MainWindow::slotSwitchLogin);

    //连接点击欢迎页离线模式，直接进入本地画板
    connect(welcome_widget,&WelcomeWidget::switchOfflineCanvas,this,&MainWindow::slotSwitchOfflineCanvas);

    //连接点击返回按钮切换欢迎页面
    connect(login_widget,&LoginWidget::switchWelcome,this,&MainWindow::slotSwitchWelcome);

    //连接点击注册按钮切换注册页面
    connect(login_widget,&LoginWidget::switchRegister,this,&MainWindow::slotSwitchRegister);

    //连接点击返回登录按钮切换登录页面
    connect(register_widget,&RegisterWidget::switchLogin,this,&MainWindow::slotSwitchLoginFromReg);

    //连接重置密码界面点击返回登录页面
    connect(reset_widget,&ResetWidget::switchLogin,this,&MainWindow::slotSwitchLoginFromReset);

    //连接登录界面点击切换重置密码页面
    connect(login_widget,&LoginWidget::switchReset,this,&MainWindow::slotSwitchResetFromLogin);

    //连接登录成功切换 lobby页面
    connect(login_widget,&LoginWidget::switchLobby,this,&MainWindow::slotSwitchLobby);

    //lobby create房间 切换 canvas页面
    connect(lobby_widget,&LobbyWidget::sig_switchCanvas,this,&MainWindow::slotSwitchCanvas);

    //lobby join房间 切换 canvas页面
    connect(lobby_widget,&LobbyWidget::sig_switchCanvas_join,this,&MainWindow::slotSwitchCanvasJoin);

    //连接房间返回大厅
    connect(canvas,&Canvas::sig_return_lobby,this,&MainWindow::slotSwitchLobbyFromCanvas);

    //连接大厅返回房间
    connect(lobby_widget,&LobbyWidget::sig_returnRoom,this,&MainWindow::slotLobbyReturnRoom);

    //连接大厅退出账号
    connect(lobby_widget, &LobbyWidget::SigLogoutRequested, this, &MainWindow::OnLogoutRequested);

    //断线: 回到大厅
    connect(TcpMgr::getInstance().get(),&TcpMgr::sig_go_lobby,this,[this](QString tip){
        canvas->resetForReconnect();
        this->show();
        if (canvas) canvas->hide();
        qDebug() << "[UI] go lobby:" << tip;
    });

    // 掉线恢复：Join成功后切回Canvas（复用现有逻辑）
    connect(TcpMgr::getInstance().get(), &TcpMgr::sig_resume_join_finish,
            this, &MainWindow::slotSwitchCanvasJoin);

    // --- 测试 ---
    // std::shared_ptr<RoomInfo> room_info = std::make_shared<RoomInfo>();
    // UserInfo user_info;
    // std::shared_ptr<UserInfo> myInfo = std::make_shared<UserInfo>(user_info);
    // UserMgr::getInstance()->setMyInfo(myInfo);
    // user_info._id = 3;
    // user_info._avatar = "111";
    // user_info._name = "shanzhi";
    // room_info->id = "123456";
    // room_info->name = "shanzhi7";
    // room_info->owner_uid = 3;
    // room_info->host = "127.0.0.1";
    // room_info->port = 8092;
    // room_info->width = 1920;
    // room_info->height = 1080;s
    // room_info->members.append(user_info);

    // emit lobby_widget->sig_switchCanvas(room_info);

}

MainWindow::~MainWindow()
{
    // 1. 独立画布由主窗口统一拥有，关闭时先释放场景和异步任务。
    delete _session_controller;
    _session_controller = nullptr;
    delete canvas;
    // 2. Qt 继续回收主窗口拥有的页面及控制器。
    delete ui;
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);

    QColor background(9, 20, 40);
    // 变暗
    QColor fixedColor = background.darker(150);
    painter.fillRect(this->rect(),fixedColor);

    // 开启抗锯齿，圆形边缘更平滑 (稍吃性能)
    painter.setRenderHint(QPainter::Antialiasing);

    //去除边框
    painter.setPen(Qt::NoPen);

    //绘制气泡
    for(const auto& bubble : bubbles)
    {
        painter.setBrush(bubble->color);
        painter.drawEllipse(QPointF(bubble->x,bubble->y),bubble->radius,bubble->radius);
    }
}

//初始化气泡
void MainWindow::initBubbles(int count)
{
    bubbles.clear();        //清除
    bubbles.reserve(count);   //预分配容量

    //初始化100个气泡
    for(int i = 0;i < count;i++)
    {
        std::shared_ptr<Bubble> bubble = std::make_shared<Bubble>();

        // 随机初始化位置
        bubble->x = QRandomGenerator::global()->bounded(width());
        bubble->y = QRandomGenerator::global()->bounded(height());

        // 随机大小 (5 ~ 25)
        bubble->radius = QRandomGenerator::global()->bounded(5, 25);

        //随机速度
        bubble->speed = 0.5 + QRandomGenerator::global()->bounded(2.0);

        bubble->h_speed = -0.5 + QRandomGenerator::global()->bounded(2.0);

        // 随机颜色 (带透明度看起来更像气泡/背景)
        int red = QRandomGenerator::global()->bounded(256);
        int green = QRandomGenerator::global()->bounded(256);
        int blue = QRandomGenerator::global()->bounded(256);
        bubble->color = QColor(red, green, blue, 50); //透明度 (0-255)

        //插入数组
        bubbles.push_back(bubble);
    }
}

void MainWindow::updateBubbles()
{
    int w = width();        //窗口宽度
    int h = height();       //窗口高度

    for (auto &bubble : bubbles)
    {
        // 移动位置
        bubble->y -= bubble->speed;
        bubble->x += bubble->h_speed;

        // 边界检测: 如果完全飘出屏幕上方
        if (bubble->y + bubble->radius < 0) {
            // 重置到底部下面一点点
            bubble->y = h + bubble->radius;
            // X轴随机位置
            bubble->x = QRandomGenerator::global()->bounded(w);
        }
    }

    // 触发重绘
    update();
}

void MainWindow::slotSwitchLogin()
{
    // 取出当前中央部件（welcome_widget）并保存，不销毁
    welcome_widget = qobject_cast<WelcomeWidget*>(takeCentralWidget());
    if (welcome_widget)
    {
        welcome_widget->hide(); // 隐藏欢迎窗口
    }
    // 设置登录窗口为新的中央部件
    setCentralWidget(login_widget);
    this->setFixedSize(login_widget->size());
    login_widget->show();
}

void MainWindow::slotSwitchWelcome()
{
    // 取出当前中央部件（login_widget）并保存，不销毁
    login_widget = qobject_cast<LoginWidget*>(takeCentralWidget());
    if (login_widget)
    {
        login_widget->hide(); // 隐藏登录窗口
    }
    // 设置欢迎窗口为新的中央部件
    setCentralWidget(welcome_widget);
    this->setFixedSize(welcome_widget->size());
    welcome_widget->show();
}

void MainWindow::slotSwitchRegister()
{
    // 取出当前中央部件（login_widget）并保存，不销毁
    login_widget = qobject_cast<LoginWidget*>(takeCentralWidget());
    if (login_widget)
    {
        login_widget->hide(); // 隐藏登录窗口
    }
    // 设置注册窗口为新的中央部件
    setCentralWidget(register_widget);
    this->setFixedSize(register_widget->size());
    register_widget->show();
}

void MainWindow::slotSwitchLoginFromReg()
{
    // 取出当前中央部件（register_widget）并保存，不销毁
    register_widget = qobject_cast<RegisterWidget*>(takeCentralWidget());
    if (register_widget)
    {
        register_widget->hide(); // 隐藏注册窗口
    }
    // 设置登录窗口为新的中央部件
    setCentralWidget(login_widget);
    this->setFixedSize(login_widget->size());
    login_widget->show();
}

void MainWindow::slotSwitchLoginFromReset()
{
    // 取出当前中央部件（reset_widget）并保存，不销毁
    reset_widget = qobject_cast<ResetWidget*>(takeCentralWidget());
    if (reset_widget)
    {
        reset_widget->hide(); // 隐藏注册窗口
    }
    // 设置登录密码窗口为新的中央部件
    setCentralWidget(login_widget);
    this->setFixedSize(login_widget->size());
    login_widget->show();
}

void MainWindow::slotSwitchResetFromLogin()
{
    // 取出当前中央部件（login_widget）并保存，不销毁
    login_widget = qobject_cast<LoginWidget*>(takeCentralWidget());
    if (login_widget)
    {
        login_widget->hide(); // 隐藏注册窗口
    }
    // 设置重置密码窗口为新的中央部件
    setCentralWidget(reset_widget);
    this->setFixedSize(reset_widget->size());
    reset_widget->show();
}

void MainWindow::slotSwitchLobby()
{
    // 1. 登录完成后建立独立账号上下文，退出事务不沿用旧账号关联。
    _session_controller->SetAccount(UserMgr::getInstance()->getUid(), UserMgr::getInstance()->getToken());
    lobby_widget->SetExitState(false, false);
    lobby_widget->RefreshRoomActions(false);
    // 2. 保留登录部件后显示大厅，避免页面切换释放可复用窗口。
    login_widget = qobject_cast<LoginWidget*>(takeCentralWidget());
    if (login_widget)
    {
        login_widget->hide(); // 隐藏注册窗口
    }
    // 设置大厅窗口为新的中央部件
    setCentralWidget(lobby_widget);
    this->setFixedSize(lobby_widget->size());
    lobby_widget->show();
}

void MainWindow::slotSwitchOfflineCanvas()
{
    // 离线模式不进入登录/大厅流程，直接打开本地单人画板。
    canvas->enterOfflineMode();
    canvas->show();
    this->hide();
}

void MainWindow::slotSwitchCanvas(std::shared_ptr<RoomInfo> room_info)  //切换画布页面
{
    // 1. 先记录业务身份，意外断线只清画面，不丢失退出目标。
    _session_controller->SetRoom(room_info);
    // 2. 初始化创建成功的场景并显示独立画布。
    canvas->setRoomInfo(room_info);
    canvas->slot_creat_room_finish(room_info);
    lobby_widget->RefreshRoomActions(true);
    canvas->show();
    this->hide();
}

void MainWindow::slotSwitchCanvasJoin(std::shared_ptr<RoomInfo> room_info)
{
    // 1. 首次加入和普通重连共用业务身份，退出专用连接不会触发此入口。
    _session_controller->SetRoom(room_info);
    // 2. 应用服务端成员和历史上下文后显示画布。
    canvas->setRoomInfo(room_info);
    canvas->slot_join_room_finish(room_info);
    lobby_widget->RefreshRoomActions(true);
    canvas->show();
    this->hide();
}

void MainWindow::slotSwitchLobbyFromCanvas()
{
    this->show();
    canvas->hide();
}

void MainWindow::slotLobbyReturnRoom()
{
    // 1. 未完成退出不能从大厅返回画板或恢复音频。
    if (_session_controller->IsBusy()) return;
    bool isInRoom = UserMgr::getInstance()->IsHaveRoom();
    if(!isInRoom)
    {
        return;
    }

    // 2. 暂回大厅只暂停音频，返回时恢复同一语音会话。
    canvas->resumeVoice();
    canvas->show();
    this->hide();
}

void MainWindow::OnLogoutRequested()
{
    // 1. 未确认的入房可能已经在服务器完成，先等待结果，不能跳过房间清理。
    if (TcpMgr::getInstance()->IsRoomRequestPending())
    {
        lobby_widget->ShowMessage(QStringLiteral("正在确认加入或创建结果，请稍后退出账号"));
        return;
    }
    // 2. 房主注销包含解散，首次操作需明确告知；失败重试沿用已确认的目标。
    if (!_session_controller->IsBusy() && _session_controller->IsRoomOwner() &&
        QMessageBox::question(this, QStringLiteral("退出账号"),
            QStringLiteral("退出账号将解散当前房间，所有成员都会退出。是否继续？")) != QMessageBox::Yes) return;
    _session_controller->RequestLogout();
}

void MainWindow::OnRoomExitRequested()
{
    // 1. 普通成员直接退房，房主首次解散需确认，重试不会生成新请求。
    if (!_session_controller->IsBusy() && _session_controller->IsRoomOwner() &&
        QMessageBox::question(canvas->isVisible() ? static_cast<QWidget*>(canvas) : this,
            QStringLiteral("解散房间"), QStringLiteral("解散后所有成员都会退出房间。是否继续？")) != QMessageBox::Yes) return;
    // 2. 控制器统一选择普通退出或房主解散，包括暂回大厅后的操作。
    _session_controller->RequestRoomExit();
}

void MainWindow::OnLogoutFinished()
{
    // 1. 停止 TCP 重连并取消未完成的请求，旧回包不能再次建立账号会话。
    TcpMgr::getInstance()->Logout();
    HttpMgr::getInstance()->CancelPendingRequests();
    VoiceManager::getInstance()->LeaveRoomAsync();
    UserMgr::getInstance()->ClearSession();

    // 2. 清除房间、画布和账号界面状态，下次登录重新加载资料。
    if (canvas)
    {
        canvas->ClearSession();
        canvas->hide();
    }
    lobby_widget->ClearSession();
    login_widget->ClearInputs();

    // 3. 先取出并保留大厅部件，再切换到登录页，防止 setCentralWidget 删除大厅。
    if (QWidget* previous_widget = takeCentralWidget())
        previous_widget->hide();
    setCentralWidget(login_widget);
    setFixedSize(login_widget->size());
    login_widget->show();
    show();
}

void MainWindow::BindSessionController()
{
    // 1. 控制器只编排业务，传输组件负责鉴权、请求发送和结果解析。
    auto* controller = _session_controller;
    auto* tcp = TcpMgr::getInstance().get();
    auto* http = HttpMgr::getInstance().get();
    auto* voice = VoiceManager::getInstance().get();
    connect(controller, &SessionController::SigExitPendingChanged, tcp, &TcpMgr::SetExitPending);
    connect(controller, &SessionController::SigPrepareExitConnection, tcp, &TcpMgr::PrepareExitConnection);
    connect(controller, &SessionController::SigSendRoomRequest, tcp, &TcpMgr::slot_send_data);
    connect(controller, &SessionController::SigSendLogoutRequest, http, &HttpMgr::RequestLogout);
    connect(tcp, &TcpMgr::SigExitConnectionReady, controller, &SessionController::OnExitConnectionReady);
    connect(tcp, &TcpMgr::SigRoomExitResponse, controller, &SessionController::OnRoomResponse);
    connect(tcp, &TcpMgr::SigRoomDestroyed, controller, &SessionController::OnRoomDestroyed);
    connect(tcp, &TcpMgr::SigExitTransportFailed, controller, &SessionController::OnTransportFailure);
    connect(http, &HttpMgr::SigLogoutFinished, controller, &SessionController::OnLogoutResponse);

    // 2. 确认结束后各模块清理自己拥有的资源，语音回收完成再推进注销。
    connect(controller, &SessionController::SigRoomCleanupRequested, this, [this, tcp]() {
        canvas->ClearRoom();
        tcp->ClearRoomRecovery();
        UserMgr::getInstance()->setIsHaveRoom(false);
        lobby_widget->RefreshRoomActions(false);
    });
    connect(controller, &SessionController::SigLeaveVoiceRequested, voice, &VoiceManager::LeaveRoomAsync);
    connect(voice, &VoiceManager::SigRoomLeft, controller, &SessionController::OnVoiceLeft);

    // 3. 页面仅响应已确认状态；等待及失败重试都禁止创建、加入和返回房间。
    connect(canvas, &Canvas::SigRoomExitRequested, this, &MainWindow::OnRoomExitRequested);
    connect(controller, &SessionController::SigBusyChanged, this, [this](bool busy, bool can_retry) {
        canvas->SetExitState(busy, can_retry);
        lobby_widget->SetExitState(busy, can_retry);
    });
    connect(controller, &SessionController::SigFailure, this, [this](QString message) {
        lobby_widget->ShowMessage(message);
        if (canvas->isVisible()) canvas->statusBar()->showMessage(message);
    });
    connect(controller, &SessionController::SigShowLobby, this, [this](QString message) {
        slotSwitchLobbyFromCanvas();
        lobby_widget->ShowMessage(message);
    });
    connect(controller, &SessionController::SigLogoutFinished, this, &MainWindow::OnLogoutFinished);
}
