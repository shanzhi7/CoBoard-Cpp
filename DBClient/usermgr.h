/***********************************************************************************
*
* @file         usermgr.h
* @brief        当前用户管理类
*
* @author       shanzhi
* @date         2026/01/22
* @history
***********************************************************************************/
#ifndef USERMGR_H
#define USERMGR_H

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
    UserMgr();

    std::shared_ptr<const UserInfo> getMyInfo();
    QString getToken();
    int getUid();
    QString getAvatar();

    void setToken(QString& token);
    void setMyInfo(std::shared_ptr<UserInfo> userInfo);
    void setAvatar(QString avatar);

    void loadAvatar(const QString& url,QLabel* label);              //从OSS或本地加载头像
    void loadAvatar(const QString& url,QTreeWidgetItem* item);      //加载头像

private:
    std::shared_ptr<UserInfo> _my_info;         //当前客户端用户信息
    QString token;
    QNetworkAccessManager* _netMgr;             //Qhttp管理类
};

#endif // USERMGR_H
