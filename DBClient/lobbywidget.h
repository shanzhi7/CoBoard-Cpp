#ifndef LOBBYWIDGET_H
#define LOBBYWIDGET_H

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
    explicit LobbyWidget(QWidget *parent = nullptr);
    ~LobbyWidget();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Ui::LobbyWidget *ui;

    void initIcons();
    void initHandles_map();                                             //初始化回包函数

    QMap<ReqId,std::function<void(QJsonObject)>> _handlers_map;        //服务器回包处理函数存储
    QString _uploadingPath; // 暂存本地路径，用于预览
    QString _pendingPublicUrl; //暂存 GateServer 发回来的公开头像地址
    QString _pendingOssKey; // 暂存 GateServer 发回来的 oss_key

signals:
    void sig_switchCanvas(std::shared_ptr<RoomInfo> room_info);             //发送切换画布页面

private slots:
    void slot_create_clicked();                                             //点击创建房间窗口
    void slot_join_clicked();                                               //点击加入房间窗口
    void slot_create_room_finish(std::shared_ptr<RoomInfo> room_info);      //创建房间完成槽函数
    void slot_join_room_finish(std::shared_ptr<RoomInfo> room_info);        //加入房间完成槽函数

    void on_upload_btn_clicked();                                           //上传头像按钮槽函数

    void slot_lobby_mod_finish(ReqId reqid,QString res,ErrorCodes err);
};

#endif // LOBBYWIDGET_H
