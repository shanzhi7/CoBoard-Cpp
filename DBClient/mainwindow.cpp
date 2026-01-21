#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QRandomGenerator>
#include <QTimer>
#include <QPainter>
#include <QFile>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    //初始化背景 begin
    bubble_timer = new QTimer(this);

    //连接一定时间间隔更新背景
    connect(bubble_timer, &QTimer::timeout, this, &MainWindow::updateBubbles);

    bubble_timer->start(16);   //帧率
    initBubbles(50);           //初始化气泡
    setWindowTitle("SyncCanvas");
    //初始化背景 end

    //初始化欢迎页面 begin
    welcome_widget = new WelcomeWidget(this);
    welcome_widget->setAttribute(Qt::WA_TranslucentBackground);     //设置透明背景
    this->setCentralWidget(welcome_widget);
    this->setFixedSize(welcome_widget->size());
    welcome_widget->show();
    //初始化欢迎页面 end

    //初始化登录窗口 begin
    login_widget = new LoginWidget(this);
    login_widget->setAttribute(Qt::WA_TranslucentBackground);       //设置透明背景
    login_widget->hide();
    //初始化登录窗口 end

    //初始化注册窗口 begin
    register_widget = new RegisterWidget(this);
    register_widget->setAttribute(Qt::WA_TranslucentBackground);    //设置透明背景
    register_widget->hide();

    //连接点击欢迎页切换登录窗口
    connect(welcome_widget,&WelcomeWidget::switchLogin,this,&MainWindow::slotSwitchLogin);

    //连接点击返回按钮切换欢迎页面
    connect(login_widget,&LoginWidget::switchWelcome,this,&MainWindow::slotSwitchWelcome);

    //连接点击注册按钮切换注册页面
    connect(login_widget,&LoginWidget::switchRegister,this,&MainWindow::slotSwitchRegister);

    //连接点击返回登录按钮切换登录页面
    connect(register_widget,&RegisterWidget::switchLogin,this,&MainWindow::slotSwitchLoginFromReg);
}

MainWindow::~MainWindow()
{
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
