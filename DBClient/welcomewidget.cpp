#include "welcomewidget.h"
#include "ui_welcomewidget.h"

WelcomeWidget::WelcomeWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::WelcomeWidget)
{
    ui->setupUi(this);
    //点击多人连接窗口发送切换登录窗口信号
    connect(ui->shared_widget,&HoverWidget::clicked,this,&WelcomeWidget::switchLogin);
}

WelcomeWidget::~WelcomeWidget()
{
    delete ui;
}
