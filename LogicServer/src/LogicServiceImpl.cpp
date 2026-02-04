#include "LogicServer/LogicServiceImpl.h"
#include "LogicServer/RedisMgr.h"
#include "LogicServer/MysqlMgr.h"
#include "LogicServer/ConfigMgr.h"
#include "LogicServer/data.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <iostream>

// 辅助函数：字符串分割
std::vector<std::string> split(const std::string& s, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter))
    {
        tokens.push_back(token);
    }
    return tokens;
}

// 注册用户函数
Status LogicServiceImpl::RegisterUser(ServerContext* context, const RegisterReq* request, RegisterRsp* reply)
{
	//获取参数
    std::string name = request->name();
    std::string email = request->email();
    std::string passwd = request->passwd();
    std::string confirm_pwd = request->confirm_pwd();
    std::string varifycode = request->varifycode();

    std::cout << "[LogicServer] 收到注册请求: email=" << email << ", name=" << name << std::endl;

    //验证验证码
    std::string redis_key = CODEPREFIX + email;
    std::string redis_code;
    // 从redis中获取验证码
    bool get_success = RedisMgr::getInstance()->Get(redis_key, redis_code);
    if (!get_success)
    {
        std::cout<<"[LogicServer] 验证码已过期或不存在"<<std::endl;
        reply->set_error(message::ErrorCodes::VarifyExpired);
        return Status::OK;
    }
    if (redis_code != varifycode)
    {
        std::cout << "[LogicServer] 验证码错误: " << email <<std::endl;
        reply->set_error(message::ErrorCodes::VarifyCodeErr);
        return Status::OK;
    }

    //成功，写入mysql
    int result = MysqlMgr::getInstance()->Register(name, email, passwd);
    if (result == message::ErrorCodes::UserExist)
    {
        // 用户名/邮箱已存在
        std::cout << "[LogicServer] 注册失败，用户已存在: " << email << std::endl;
        reply->set_error(message::ErrorCodes::UserExist);
        return Status::OK;
    }
    if (result <= 0)
    {
        // 数据库内部错误 (连接失败、SQL语法错等)
        std::cout << "[LogicServer] 注册失败，数据库内部错误" << std::endl;
        reply->set_error(message::ErrorCodes::RPCFailed); // 或者定义一个 DBError
        return Status::OK;
    }

    // 注册成功
    RedisMgr::getInstance()->Del(redis_key);    // 删除验证码
    std::cout << "[LogicServer] 注册成功: " << email << ", uid=" << result << std::endl;
    reply->set_error(message::ErrorCodes::SUCCESS);
    reply->set_uid(result); // 将生成的 UID 返回给前端

    return Status::OK;
}

Status LogicServiceImpl::ResetPassword(ServerContext* context, const ResetPasswordReq* request, ResetPasswordRsp* reply)
{ 
    // 获取参数
    std::string email = request->email();
    std::string varifycode = request->varifycode();
    std::string passwd = request->passwd();
    std::string confirm_pwd = request->confirm_pwd();
    std::cout << "[LogicServer] 密码重置请求: email=" << email << std::endl;
    if (passwd != confirm_pwd)
    {
        std::cout << "[LogicServer] 密码不一致" << std::endl;
        reply->set_error(message::ErrorCodes::PasswdInvalid);
        return Status::OK;
    }
    
    // 验证验证码
    std::string redis_key = CODEPREFIX + email;
    std::string redis_code;
    bool get_success = RedisMgr::getInstance()->Get(redis_key, redis_code);
    if (!get_success)
    {
        std::cout << "[LogicServer] 验证码已过期或不存在" << std::endl;
        reply->set_error(message::ErrorCodes::VarifyExpired);
        return Status::OK;
    }
    if (redis_code != varifycode)
    {
        std::cout << "[LogicServer] 验证码错误: " << email << std::endl;
        reply->set_error(message::ErrorCodes::VarifyCodeErr);
        return Status::OK;
    }
    int result = MysqlMgr::getInstance()->ResetPassword(email, varifycode, passwd); // 调用 MysqlMgr
    if (result != message::ErrorCodes::SUCCESS)
    {
        std::cout << "[LogicServer] 密码重置失败，数据库内部错误" << std::endl;
        reply->set_error(message::ErrorCodes::RPCFailed);
        return Status::OK;
    }

    RedisMgr::getInstance()->Del(redis_key);    // 删除验证码

    std::cout << "[LogicServer] 密码重置成功: " << email << std::endl;
    return Status::OK;
}
bool LogicServiceImpl::GetCanvasServerInfo(std::string& host, std::string& port)
{
    // 获取CanvasServer信息
    auto& configMgr = ConfigMgr::Inst();
    std::string serverNamesStr = configMgr["CanvasServers"]["Name"];
    if (serverNamesStr.empty())
    {
        return false;
    }
    //分割服务器名字
    std::vector<std::string> serverNames = split(serverNamesStr, ',');
    if (serverNames.empty())
    {
        return false;
    }

    //简单的随机负载均衡，//返回服务器信息
    srand(time(0));
    int index = rand() % serverNames.size();
    std::string selectedServer = serverNames[index];
    host = configMgr[selectedServer]["Host"];
    port = configMgr[selectedServer]["Port"];
    if (host.empty() || port.empty())
    {
        return false;
    }
    return true;
}

Status LogicServiceImpl::Login(ServerContext* context, const LoginReq* request, LoginRsp* reply)
{
    if (request->email().empty() || request->passwd().empty())  // 参数校验
    {
        reply->set_error(message::ErrorCodes::PasswdErr);
        return Status::OK;
    }

    //邮箱账号密码
    UserInfo userInfo;
    bool isPasswordValid = MysqlMgr::getInstance()->CheckPassword(request->email(), request->passwd(),userInfo);

    if (!isPasswordValid)
    {
        std::cout << "[LogicServer] Login failed: Password error or user not found. Email: " << request->email() << std::endl;
        reply->set_error(message::ErrorCodes::PasswdErr);
        return Status::OK;
    }
    std::string uid_str = std::to_string(userInfo.uid); //获取用户uid
    std::string uid_key = UID_PREFIX + uid_str; // key: uid_token_1001
    std::string old_token;

    bool has_login = RedisMgr::getInstance()->Get(uid_key, old_token);  //尝试获取该用户旧的 Token

    if (has_login)
    {
        // 如果存在旧 Token，把旧 Token 的认证 Key 删掉 (踢下线)
        std::string old_token_key = TOKEN_PREFIX + old_token;       //value 为 uid
        RedisMgr::getInstance()->Del(old_token_key);

        std::cout << "[LogicServer] Kick out old token: " << old_token << " for uid: " << uid_str << std::endl;
    }

    //生成token
    boost::uuids::random_generator gen;
    boost::uuids::uuid id = gen();
    std::string token = boost::uuids::to_string(id);        // 生成随机的 Token

    //存储token,双向存储
    std::string token_key = TOKEN_PREFIX + token;           // utoken_f4521......
    // A. 存认证 Key (Token -> Uid)
    bool setToken = RedisMgr::getInstance()->Set(token_key, uid_str, 86400);    //id为value
    // B. 存管理 Key (Uid -> Token) - 也要设置过期时间，保持一致
    bool setUid = RedisMgr::getInstance()->Set(uid_key, token, 86400);          //key: uid_token_1001 ，value为token
    if (!setToken || !setUid)
    {
        reply->set_error(message::ErrorCodes::LoginErr);
        return Status::OK;
    }

    //分配CanvasServer
    std::string canvasHost, canvasPort;
    if (!GetCanvasServerInfo(canvasHost, canvasPort))
    {
        std::cout << "[LogicServer] Login error: No CanvasServer available." << std::endl;
        reply->set_error(message::ErrorCodes::RPCFailed);
        return Status::OK;
    }

    //构造返回包
    reply->set_error(message::ErrorCodes::SUCCESS);
    reply->set_uid(userInfo.uid);
    reply->set_token(token);
    reply->set_name(userInfo.name);
    reply->set_avatar(userInfo.avatar); // 把头像也返回给前端
    reply->set_host(canvasHost);
    reply->set_port(canvasPort);

    std::cout << "[LogicServer] Login success! UID: " << userInfo.uid << " -> " << canvasHost << ":" << canvasPort << std::endl;

    return Status::OK;
}

Status LogicServiceImpl::UpdateAvatar(ServerContext* context, const UpdateAvatarReq* request, UpdateAvatarRsp* reply)
{
    if (request->uid() == 0 || request->avatar_url().empty())  // 参数校验
    {
        reply->set_error(message::ErrorCodes::RPCFailed);
        return Status::OK;
    }
    int err_code = MysqlMgr::getInstance()->UpdateAvatar(request->uid(), request->avatar_url());

    if (err_code != message::ErrorCodes::SUCCESS) // 更新数据库失败
    {
        reply->set_error(err_code);
        return Status::OK;
    }

    reply->set_error(message::ErrorCodes::SUCCESS);
    reply->set_uid(request->uid());
    return Status::OK;
}