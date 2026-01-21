/***********************************************************************************
*
* @file         registerwidget.h
* @brief        注册窗口
*
* @author       shanzhi
* @date         2026/01/20
* @history
***********************************************************************************/
#ifndef REGISTERWIDGET_H
#define REGISTERWIDGET_H

#include "global.h"
#include <QWidget>
#include <QLineEdit>
#include <QMap>

namespace Ui {
class RegisterWidget;
}

class RegisterWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RegisterWidget(QWidget *parent = nullptr);
    ~RegisterWidget();

private:
    Ui::RegisterWidget *ui;

    // 定义一个列表，存储所有返回 bool 的校验函数
    QList<std::function<bool()>> _validators;
    QMap<ReqId,std::function<void(QJsonObject)>> _handlers_map;        //服务器回包处理函数存储

    void bindValidator(QLineEdit *input, const QString &pattern, const QString &errMsg);    //绑定验证器
    void initValidator();       //初始化正则表达式验证器，用于判断输入的合法性
    void initToolButton();      //初始化ui的两个toolBtn
    void initHandersMap();      //初始化服务器回包函数

signals:
    void switchLogin();     //发送切换登录界面按钮
private slots:
    void on_get_verify_btn_clicked();
    void on_register_btn_clicked();

    void slot_reg_mod_finish(ReqId,QString,ErrorCodes);     //回包槽函数
};

#endif // REGISTERWIDGET_H
