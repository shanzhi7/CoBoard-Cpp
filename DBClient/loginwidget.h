/***********************************************************************************
*
* @file         loginwidget.h
* @brief        登录窗口
*
* @author       shanzhi
* @date         2026/01/20
* @history
***********************************************************************************/
#pragma once

#include <QWidget>
#include <QLineEdit>
#include "TipWidget.h"
#include "global.h"

namespace Ui {
class LoginWidget;
}

class LoginWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LoginWidget(QWidget *parent = nullptr); // 初始化登录界面和网络通知。
    ~LoginWidget(); // 释放登录界面。
    void ClearInputs(); // 清空上一次登录留下的账号和密码。

protected:
    virtual QSize sizeHint() const override; // 返回登录页建议尺寸。

private:
    Ui::LoginWidget *ui; // 登录界面对象。
    QMap<ReqId,std::function<void(QJsonObject)>> _handlers_map;        //服务器回包处理函数存储


    QList<std::function<bool()>> _validators;           // 存储所有的验证函数

    // 通用绑定函数
    void bindValidator(QLineEdit *input, const QString &pattern, const QString &errMsg); // 绑定输入校验和失焦提示。

    // 初始化函数
    void initValidator(); // 初始化邮箱和密码校验规则。
    void initHandlerMap();                                              //初始化回包处理函数


signals:
    void switchWelcome();   //发送切换欢迎页面信号
    void switchRegister();  //发送切换注册页面信号
    void switchReset();     //发送切换重置密码页面信号
    void switchLobby();    //发送切换Lobby页面信号

    void sig_connect_tcp(ServerInfo si);    //发送连接CanvasServer信号
private slots:
    void on_login_btn_clicked(); // 校验输入并提交登录请求。

    void slot_login_mod_finish(ReqId reqid,QString res,ErrorCodes err); // 登录http请求完成，获取token
    void slot_tcp_con_finish(bool bsuccess);                            // tcp连接成功槽函数，正式尝试登录
};
