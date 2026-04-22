#include "welcomewidget.h"
#include "ui_welcomewidget.h"
#include "global.h"

WelcomeWidget::WelcomeWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::WelcomeWidget)
{
    ui->setupUi(this);
    //点击多人连接窗口发送切换登录窗口信号
    connect(ui->shared_widget,&HoverWidget::clicked,this,&WelcomeWidget::switchLogin);
    //点击离线模式窗口，直接进入本地单人画板
    connect(ui->offline_widget,&HoverWidget::clicked,this,&WelcomeWidget::switchOfflineCanvas);

    initIcons();
}

WelcomeWidget::~WelcomeWidget()
{
    delete ui;
}

void WelcomeWidget::initIcons()
{
    // 加载原始黑色图标
    QPixmap iconCreate(":/res/offline.png");
    QPixmap iconJoin(":/res/shared.png");


    // 变成白色
    QColor targetColor("white");

    QPixmap newCreate = applyColor(iconCreate, targetColor);
    QPixmap newJoin = applyColor(iconJoin, targetColor);

    // 设置给 Label
    ui->offline_head_lbl->setPixmap(newCreate);
    ui->shared_head_lbl->setPixmap(newJoin);

    // 置图片自动缩放，防止变形
    ui->offline_head_lbl->setScaledContents(true);
    ui->shared_head_lbl->setScaledContents(true);
}
