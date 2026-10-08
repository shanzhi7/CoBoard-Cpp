/***********************************************************************************
*
* @file         usermgr.h
* @brief        当前用户管理类
*
* @author       shanzhi
* @date         2026/01/22
* @history
***********************************************************************************/
#pragma once

#include "userdata.h"
#include "singleton.h"
#include <QLabel>
#include <memory>
#include <QObject>
#include <QNetworkAccessManager>
#include <QTreeWidgetItem>
class UserMgr : public QObject, public Singleton<UserMgr>, public std::enable_shared_from_this<UserMgr>
{
    Q_OBJECT
public:
    UserMgr(); // 创建用户资料与头像下载管理器。

    std::shared_ptr<const UserInfo> getMyInfo(); // 返回当前用户资料。
    QString getToken(); // 返回当前登录令牌。
    int getUid(); // 返回已登录用户 ID。
    QString getAvatar(); // 返回已登录用户头像地址。
    QString getName(); // 返回已登录用户昵称。
    bool IsHaveRoom(); // 返回当前用户是否已加入房间。

    void setToken(QString& token); // 保存登录令牌。
    void setMyInfo(std::shared_ptr<UserInfo> userInfo); // 保存当前用户资料。
    void setAvatar(QString avatar); // 更新当前用户头像地址。
    void setIsHaveRoom(bool f); // 更新用户房间标志。
    void ClearSession(); // 清除当前登录会话、房间标志和用户资料。

    void loadAvatar(const QString& url,QLabel* label);              //从OSS或本地加载头像
    void loadAvatar(const QString& url,QTreeWidgetItem* item);      //加载头像

private:
    std::shared_ptr<UserInfo> _my_info;         //当前客户端用户信息
    QString token; // 当前登录令牌。
    QNetworkAccessManager* _netMgr;             //Qhttp管理类
    bool isHaveRoom; // 当前用户是否已有房间。
};
