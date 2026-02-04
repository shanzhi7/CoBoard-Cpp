#include "LogicServer/MysqlDao.h"
#include "LogicServer/ConfigMgr.h"
#include "LogicServer/message.pb.h"

MysqlDao::MysqlDao()
{
	auto& cfg = ConfigMgr::Inst();
	const auto& host = cfg["Mysql"]["Host"];
	const auto& port = cfg["Mysql"]["Port"];
	const auto& pwd = cfg["Mysql"]["Password"];
	const auto& user = cfg["Mysql"]["User"];
	const auto& schema = cfg["Mysql"]["Schema"];
	std::cout << "读取到的Mysql Host: " << host << std::endl;
	std::cout << "读取到的Mysql port: " << port << std::endl;
	std::cout << "读取到的Mysql password: " << pwd << std::endl;
	std::cout << "读取到的Mysql user: " << user << std::endl;
	std::cout << "读取到的Mysql schema: " << schema << std::endl;
	_mysqlPool.reset(new MysqlPool("tcp://" + host + ":" + port, user, pwd, schema, 5));	// 5为连接池大小
}

MysqlDao::~MysqlDao()
{

}

//注册
int MysqlDao::Register(const std::string& name, const std::string& email, const std::string& password,
	int sex, const std::string& avatar, const std::string& signature)
{
	try
	{
		auto con = _mysqlPool->getConnection();
		if (!con)
		{
			std::cout << "Register Error: 获取连接失败" << std::endl;
			return -1;
		}
		// defer
		Defer defer([&]() {
			_mysqlPool->returnConnection(std::move(con)); 
			});

		std::unique_ptr<sql::PreparedStatement> pstmt(
			con->prepareStatement(
				"INSERT INTO user(name, email, passwd, sex, avatar, signature) VALUES (?, ?, ?, ?, ?, ?)"
			)
		);

		// 绑定 6 个参数
		pstmt->setString(1, name);
		pstmt->setString(2, email);
		pstmt->setString(3, password);
		pstmt->setInt(4, sex);
		pstmt->setString(5, avatar);
		pstmt->setString(6, signature);

		// 执行
		pstmt->executeUpdate();

		//获取自增id
		std::unique_ptr<sql::Statement> stmt(con->createStatement());	// 创建Statement
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT LAST_INSERT_ID()"));	// 执行查询
		if (res->next())
        {
            int uid = res->getInt(1);
            std::cout << "Register Success: uid = " << uid << std::endl;
            return uid;
        }
		std::cout << "Register Error: 获取自增id失败" << std::endl;
		return -1;
	}
	catch (sql::SQLException& e)
	{

		if (e.getErrorCode() == 1062)// 1062 是 MySQL Duplicate entry(重复条目) 的错误码
		{
			std::cout << "Register Error: 邮箱已存在 (Duplicate entry)" << std::endl;
			return message::ErrorCodes::UserExist;
		}
		std::cerr << "SQLException: " << e.what();
		std::cerr << "(MYSQL error code: )" << e.getErrorCode();
		std::cerr << ",SQLState: " << e.getSQLState() << ")" << std::endl;
		return -1;
	}
}

int MysqlDao::ResetPassword(const std::string& email, const std::string& verifycode, const std::string& password)
{
    try
    { 
		auto con = _mysqlPool->getConnection();
		if (!con)
		{
            std::cout << "ResetPassword Error: 获取连接失败" << std::endl;
			return message::ErrorCodes::RPCFailed;
		}
		// RAII: 无论后续发生什么，确保连接被归还到连接池
        Defer defer([&]() {		//回收
            _mysqlPool->returnConnection(std::move(con)); 
            });

		//准备update语句
		std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
			"UPDATE user SET passwd = ? WHERE email = ?"
		));

		// 绑定参数
		pstmt->setString(1, password);
        pstmt->setString(2, email);

		int rows = pstmt->executeUpdate();	//更新行数

		if (rows == 0)
		{
			// 如果影响行数为 0，说明要么邮箱不存在，要么新密码和老密码完全一样
			std::cout << "ResetPassword Warning: 邮箱不存在或新旧密码相同. email: " << email << std::endl;
			return message::ErrorCodes::UserNotExist;
		}
		std::cout << "ResetPassword Success: 密码已成功重置. email: " << email << std::endl;
		return message::ErrorCodes::SUCCESS;	//成功
    }
	catch (sql::SQLException& e)
	{
		std::cerr << "ResetPassword SQLException: " << e.what()
			<< " (MySQL error code: " << e.getErrorCode()
			<< ", SQLState: " << e.getSQLState() << ")" << std::endl;
		return message::ErrorCodes::PasswdUpFailed; // 对应 ErrorCodes::PasswdUpFailed
	}
	catch (std::exception& e)
	{
		std::cerr << "ResetPassword std::exception: " << e.what() << std::endl;
		return message::ErrorCodes::PasswdUpFailed; // 对应 ErrorCodes::PasswdUpFailed
	}
}

bool MysqlDao::CheckPassword(const std::string& email, const std::string& pwd, UserInfo& userInfo)
{
	auto con = _mysqlPool->getConnection();
	if (con == nullptr)
	{
		return false;
	}

	Defer defer([this, &con](){
		_mysqlPool->returnConnection(std::move(con));
		});

	try {
		// 准备 SQL: 查询 uid, name, passwd , avatar
		std::unique_ptr<sql::PreparedStatement> pstmt(
			con->prepareStatement("SELECT id, name, email, passwd, avatar FROM user WHERE email = ?"));

		pstmt->setString(1, email);

		// 执行查询
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

		if (res->next())
		{
			// 取出数据库存的密码
			std::string db_pwd = res->getString("passwd");
			if (pwd != db_pwd)
			{
				return false; // 密码不匹配
			}

			// 密码匹配，填充 userInfo
			userInfo.uid = res->getInt("id");
			userInfo.name = res->getString("name");
			userInfo.email = res->getString("email");
			userInfo.password = db_pwd;
			userInfo.avatar = res->getString("avatar"); // 获取头像

			return true;
		}
		return false; // 用户不存在
	}
	catch (sql::SQLException& e)
	{
		std::cerr << "[MysqlDao] CheckPassword error: " << e.what() << std::endl;
		return false;
	}
}
int MysqlDao::UpdateAvatar(const int& uid, const std::string& avatar)
{
	try
	{
		auto con = _mysqlPool->getConnection();
		if (!con)
		{
			std::cout << "[MysqlDao] UpdateAvatar Error: 获取连接失败" << std::endl;
			return message::ErrorCodes::RPCFailed;
		}
		// RAII: 无论后续发生什么，确保连接被归还到连接池
		Defer defer([&]() {		//回收
			_mysqlPool->returnConnection(std::move(con));
			});

		//准备update语句
		std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
			"UPDATE user SET avatar = ? WHERE id = ?"
		));

		// 绑定参数
		pstmt->setString(1, avatar);
		pstmt->setInt(2, uid);

		int rows = pstmt->executeUpdate();	//更新行数

		if (rows == 0)
		{
			// 如果影响行数为 0，说明要么邮箱不存在，要么新密码和老密码完全一样
			std::cout << "[MysqlDao] UpdateAvatar Error:  " << avatar << std::endl;
			return message::ErrorCodes::UserNotExist;
		}
		std::cout << "[MysqlDao] UpdateAvatar 成功 " << avatar << std::endl;
		return message::ErrorCodes::SUCCESS;	//成功
	}
	catch (sql::SQLException& e)
	{
		std::cerr << "UpdateAvatar SQLException: " << e.what()
			<< " (MySQL error code: " << e.getErrorCode()
			<< ", SQLState: " << e.getSQLState() << ")" << std::endl;
		return message::ErrorCodes::PasswdUpFailed; // 对应 ErrorCodes::PasswdUpFailed
	}
	catch (std::exception& e)
	{
		std::cerr << "UpdateAvatar std::exception: " << e.what() << std::endl;
		return message::ErrorCodes::PasswdUpFailed; // 对应 ErrorCodes::PasswdUpFailed
	}
}