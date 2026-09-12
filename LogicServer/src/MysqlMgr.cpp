#include "LogicServer/MysqlMgr.h"

MysqlMgr::MysqlMgr()
{
    
}
MysqlMgr::~MysqlMgr()
{
    
}
// 注册
int MysqlMgr::Register(const std::string& name, const std::string& email, const std::string& password,
    int sex, const std::string& avatar, const std::string& signature)
{
    return _dao.Register(name, email, password, sex, avatar, signature);
}

//重置密码
int MysqlMgr::ResetPassword(const std::string& email, const std::string& verifycode, const std::string& password)
{
    return _dao.ResetPassword(email, verifycode, password);
}

bool MysqlMgr::CheckPassword(const std::string& email, const std::string& pwd, UserInfo& userInfo)
{
    return _dao.CheckPassword(email, pwd, userInfo);
}

bool MysqlMgr::UpdateAvatar(const int& uid, const std::string& avatar)
{
    return _dao.UpdateAvatar(uid, avatar);
}