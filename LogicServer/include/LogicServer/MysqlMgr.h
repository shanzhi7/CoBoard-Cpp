#pragma once
#include"LogicServer/MysqlDao.h"
#include"LogicServer/Singleton.h"
class MysqlMgr : public Singleton<MysqlMgr>
{
	friend class Singleton<MysqlMgr>;
public:
	~MysqlMgr();		//公有析构函数

	//注册
	int Register(const std::string& name, const std::string& email, const std::string& password,
		int sex = 0, const std::string& avatar = "", const std::string& signature = "");
	//重置密码
	int ResetPassword(const std::string& email, const std::string& verifycode, const std::string& password);

	//校验密码
	bool CheckPassword(const std::string& email, const std::string& pwd, UserInfo& userInfo);

	//更新头像
	bool UpdateAvatar(const int& uid, const std::string& avatar);
private:
	MysqlMgr();			//私有构造函数
	MysqlDao _dao;		//数据访问对象
};