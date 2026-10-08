/***********************************************************************************
* @file         lobbywidget.h
* @brief        大厅房间入口、账号资料和退出账号通知
* @author       shanzhi
* @date         2026/10/08
* @history
***********************************************************************************/
#pragma once

#include <QWidget>
#include <QFileInfo>
#include <QFile>
#include <QFileDialog>
#include "global.h"

namespace Ui {
class LobbyWidget;
}

class LobbyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LobbyWidget(QWidget *parent = nullptr); // 初始化大厅界面和网络回调。
    ~LobbyWidget(); // 释放大厅界面。
    void ClearSession(); // 清除旧账号的头像显示和上传暂存状态。

protected:
    void paintEvent(QPaintEvent *event) override; // 绘制大厅样式背景。

private:
    Ui::LobbyWidget *ui; // 大厅界面对象。

    void initIcons(); // 初始化默认头像和房间入口图标。
    void initHandles_map();                                             //初始化回包函数

    QMap<ReqId,std::function<void(QJsonObject)>> _handlers_map;        //服务器回包处理函数存储
    QString _uploadingPath; // 暂存本地路径，用于预览
    QString _pendingPublicUrl; //暂存 GateServer 发回来的公开头像地址
    QString _pendingOssKey; // 暂存 GateServer 发回来的 oss_key

signals:
    void sig_switchCanvas(std::shared_ptr<RoomInfo> room_info);             //发送切换画布页面 create
    void sig_switchCanvas_join(std::shared_ptr<RoomInfo> room_info);        //发送切换画布页面 join
    void sig_returnRoom();                                                  //返回房间信号
    void SigLogoutRequested();                                               //请求退出当前账号。

private slots:
    void slot_create_clicked();                                             //点击创建房间窗口
    void slot_join_clicked();                                               //点击加入房间窗口
    void slot_create_room_finish(std::shared_ptr<RoomInfo> room_info);      //创建房间完成槽函数
    void slot_join_room_finish(std::shared_ptr<RoomInfo> room_info);        //加入房间完成槽函数
    void slot_load_info();                                                  //登录成功，加载用户信息

    void on_upload_btn_clicked();                                           //上传头像按钮槽函数

    void slot_lobby_mod_finish(ReqId reqid,QString res,ErrorCodes err);     //http请求完成槽函数
    void slot_go_lobby(QString tip);                                        //收到tcpmgr发来的掉线通知
    void on_retRoom_btn_clicked();                                          //返回房间按钮槽函数
    void OnLogoutTriggered(); // 将退出账号请求交给主窗口处理。
};
