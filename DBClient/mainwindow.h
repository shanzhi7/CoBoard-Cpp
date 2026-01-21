/***********************************************************************************
*
* @file         mainwindow.h
* @brief        主窗口
*
* @author       shanzhi
* @date         2026/01/20
* @history
***********************************************************************************/
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QObject>
#include <memory>
#include "global.h"
#include "welcomewidget.h"
#include "loginwidget.h"
#include "registerwidget.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void paintEvent(QPaintEvent* event) override;   //重绘事件，绘制背景

private:
    Ui::MainWindow *ui;
    WelcomeWidget* welcome_widget;                  //欢迎窗口
    LoginWidget* login_widget;                      //登录窗口
    RegisterWidget* register_widget;                //注册窗口


    void initBubbles(int count);                    //初始化气泡


    QTimer* bubble_timer;                           //气泡定时器
    std::vector<std::shared_ptr<Bubble>> bubbles;    //气泡数组

private slots:
    void updateBubbles();                           //更新气泡

    void slotSwitchLogin();                         //切换登录窗口槽函数
    void slotSwitchWelcome();                       //切换欢迎页面槽函数
    void slotSwitchRegister();                      //切换注册页面槽函数
    void slotSwitchLoginFromReg();                  //注册界面切换登录界面
};
#endif // MAINWINDOW_H
