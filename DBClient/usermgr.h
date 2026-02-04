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
#include <memory>
class UserMgr : public Singleton<UserMgr>,public std::enable_shared_from_this<UserMgr>
{
public:
    UserMgr();

    std::shared_ptr<UserInfo> getMyInfo();
    QString getToken();
    int getUid();

    void setToken(QString& token);
    void setMyInfo(std::shared_ptr<UserInfo> userInfo);
    void setAvatar(QString avatar);

private:
    std::shared_ptr<UserInfo> _my_info;         //当前客户端用户信息
    QString token;
};

#endif // USERMGR_H
