/***********************************************************************************
*
* @file         loginwidget.h
* @brief        登录窗口
*
* @author       shanzhi
* @date         2026/01/20
* @history
***********************************************************************************/
#ifndef LOGINWIDGET_H
#define LOGINWIDGET_H

#include <QWidget>

namespace Ui {
class LoginWidget;
}

class LoginWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LoginWidget(QWidget *parent = nullptr);
    ~LoginWidget();

protected:
    virtual QSize sizeHint() const override;

private:
    Ui::LoginWidget *ui;

signals:
    void switchWelcome();   //发送切换欢迎页面信号

    void switchRegister();  //发送切换注册页面信号
};

#endif // LOGINWIDGET_H
