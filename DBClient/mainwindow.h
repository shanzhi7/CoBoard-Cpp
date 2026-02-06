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
#include "resetwidget.h"
#include "canvas.h"
#include "lobbywidget.h"

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
    ResetWidget* reset_widget;                      //重置窗口
    LobbyWidget* lobby_widget;                      //大厅窗口
    Canvas* canvas;                                 //画布窗口


    void initBubbles(int count);                    //初始化气泡


    QTimer* bubble_timer;                           //气泡定时器
    std::vector<std::shared_ptr<Bubble>> bubbles;    //气泡数组

private slots:
    void updateBubbles();                           //更新气泡

    void slotSwitchLogin();                         //切换登录窗口槽函数(欢迎->login)
    void slotSwitchWelcome();                       //切换欢迎页面槽函数(login->欢迎)
    void slotSwitchRegister();                      //切换注册页面槽函数(login->register)
    void slotSwitchLoginFromReg();                  //注册界面切换登录界面(register->login)
    void slotSwitchLoginFromReset();                //重置密码页面切换登录界面(reset->login)
    void slotSwitchResetFromLogin();                //切换重置密码槽函数(login->reset)
    void slotSwitchLobby();                         //切换Lobby页面(login->Lobby)
    void slotSwitchCanvas(std::shared_ptr<RoomInfo> room_info);     //切换canvas页面(lobby -> canvas)
    void slotSwitchCanvasJoin(std::shared_ptr<RoomInfo> room_info); //切换canvas页面 join
};
#endif // MAINWINDOW_H
