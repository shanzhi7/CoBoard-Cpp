#include "loginwidget.h"
#include "ui_loginwidget.h"

LoginWidget::LoginWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LoginWidget)
{
    ui->setupUi(this);
    ui->email_edit->setPlaceholderText("请输入您的邮箱");
    ui->passworld_edit->setPlaceholderText("请输入您的密码");

    //点击返回按钮发送切换欢迎界面信号
    connect(ui->cannel_btn,&QPushButton::clicked,this,&LoginWidget::switchWelcome);

    //点击注册按钮发送切换注册页面信号
    connect(ui->register_btn,&QPushButton::clicked,this,&LoginWidget::switchRegister);
}

LoginWidget::~LoginWidget()
{
    delete ui;
}

QSize LoginWidget::sizeHint() const
{
    return QSize(this->rect().size());
}
