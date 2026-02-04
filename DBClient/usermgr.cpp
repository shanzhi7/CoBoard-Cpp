#include "usermgr.h"

UserMgr::UserMgr()
    :token(""),_my_info(nullptr)
{

}

QString UserMgr::getToken()
{
    return this->token;
}

int UserMgr::getUid()
{
    return this->_my_info->_id;
}

void UserMgr::setToken(QString &token)
{
    this->token = token;
}

void UserMgr::setMyInfo(std::shared_ptr<UserInfo> userInfo)
{
    this->_my_info = userInfo;
}

void UserMgr::setAvatar(QString avatar)
{
    this->_my_info->_avatar = avatar;
}

std::shared_ptr<UserInfo> UserMgr::getMyInfo()
{
    return this->_my_info;
}
