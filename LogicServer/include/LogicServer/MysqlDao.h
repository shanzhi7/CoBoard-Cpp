#pragma once
#include "LogicServer/MysqlPool.h"
#include "LogicServer/data.h"

class MysqlDao
{
public:
	MysqlDao();
	~MysqlDao();

	//注册
	int Register(const std::string& name, const std::string& email, const std::string& password,
		int sex = 0, const std::string& avatar = "", const std::string& signature = "");

	//重置密码
	int ResetPassword(const std::string& email, const std::string& verifycode, const std::string& password);

	//校验密码
	bool CheckPassword(const std::string& email, const std::string& pwd, UserInfo& userInfo);

	//更新头像
	int UpdateAvatar(const int& uid, const std::string& avatar);



private:
	std::unique_ptr<MysqlPool> _mysqlPool;
};