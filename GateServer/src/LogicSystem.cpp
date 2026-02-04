#include "GateServer/LogicSystem.h"
#include "GateServer/HttpConnection.h"
#include "GateServer/const.h"
#include "GateServer/VerifyGrpcClient.h"
#include "GateServer/LogicGrpcClient.h"
#include "GateServer//message.pb.h"
#include "GateServer//message.grpc.pb.h"
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <boost/beast/core/detail/base64.hpp>
#include <sstream>
#include <iomanip>
#include <ctime>
#include "GateServer/ConfigMgr.h" // 确保包含你的配置管理器

// HMAC-SHA1 加密
std::string HmacSha1(const std::string& key, const std::string& data)
{
	unsigned char* result;
	unsigned int len = 20;
	unsigned char hash[20];

	// OpenSSL 的 HMAC 函数
	result = HMAC(EVP_sha1(), key.c_str(), key.length(),
		(unsigned char*)data.c_str(), data.length(), hash, &len);

	return std::string((char*)hash, len);
}

// Base64 编码 (利用 Boost.Beast)
std::string Base64Encode(const std::string& input)
{
	std::string output;
	output.resize(boost::beast::detail::base64::encoded_size(input.size()));
	auto result = boost::beast::detail::base64::encode(&output[0], input.data(), input.size());
	output.resize(result);
	return output;
}

LogicSystem::LogicSystem()	//构造函数
{
	//测试get请求，用不上
	RegGet("/get_test", [](std::shared_ptr<HttpConnection> connection) {
		boost::beast::ostream(connection->_response.body()) << "receive get_test request";
		int i = 0;
		for (auto& elem : connection->_get_params)
		{
			i++;
			boost::beast::ostream(connection->_response.body()) << " param" << i << ": key is " << elem.first;
			boost::beast::ostream(connection->_response.body()) << " param" << i << ": value is " << elem.second << std::endl;
		}
		});

	//注册获取验证码请求
	RegPost("/get_verifycode", [](std::shared_ptr<HttpConnection> connection) {
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());	//获取请求body
		std::cout << "receive body is " << body_str << std::endl;
		connection->_response.set(boost::beast::http::field::content_type,"application/json");
		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;
		bool parse_success = reader.parse(body_str, src_root);

		if (!parse_success)	//解析失败
		{
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = message::ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			boost::beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		auto email = src_root["email"].asString();
		auto name = src_root["user"].asString();
		auto pwd = src_root["password"].asString();
		auto confirm = src_root["confirm"].asString();

		if (pwd != confirm)	//判断两次输入的密码是否一致
		{
			std::cout << "两次密码不一致" << std::endl;
			root["error"] = message::ErrorCodes::PasswdErr;
			std::string jsonstr = root.toStyledString();
			boost::beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}
		if (!src_root.isMember("email"))
		{
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = message::ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			boost::beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}
		std::cout << "client email is " << email << std::endl;

		//向grpc服务发送获取验证码请求
		GetVarifyRsp rsp = VerifyGrpcClient::getInstance()->GetVarifyCode(email);

		if (rsp.error() != 0)//验证码发送失败
		{
			std::cout << "get Verification code is Failed " << std::endl;
			std::cout << "the error code is " << rsp.error() << std::endl;
		}
		else
		{
			std::cout << "grpcServer: Verification code sent successfully, the target email is: " << rsp.email() << std::endl;
		}
		root["error"] = rsp.error();											  //将错误发送给客户端
		root["email"] = src_root["email"];
		root["server"] = "GateServer";
		std::string jsonstr = root.toStyledString();
		boost::beast::ostream(connection->_response.body()) << jsonstr;
		return true;
		});

    //注册注册请求
    RegPost("/user_register", [](std::shared_ptr<HttpConnection> connection) { 
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
        std::cout << "receive body is " << body_str << std::endl;
        connection->_response.set(boost::beast::http::field::content_type,"application/json");
        Json::Value root;
        Json::Reader reader;
        Json::Value src_root;
        bool parse_success = reader.parse(body_str, src_root);

		// 只有解析成功了，才往下走
		if (!parse_success)
		{
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = message::ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			boost::beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// 判断是否缺少必要字段
		if(!src_root.isMember("email") || !src_root.isMember("name") || !src_root.isMember("password")
			|| !src_root.isMember("confirm") || !src_root.isMember("verifycode"))
		{
			std::cout << "[GateServer] Missing required JSON fields!" << std::endl;
			root["error"] = message::ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			boost::beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

        auto email = src_root["email"].asString();
        auto name = src_root["name"].asString();
        auto pwd = src_root["password"].asString();
        auto confirm = src_root["confirm"].asString();
        if (pwd != confirm)
        {
            std::cout << "两次密码不一致" << std::endl;
            root["error"] = message::ErrorCodes::PasswdErr;
            std::string jsonstr = root.toStyledString();
            boost::beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }

		std::cout << "[GateServer] Client email is " << email << std::endl;
        RegisterReq req;
        req.set_email(email);
        req.set_name(name);
        req.set_passwd(pwd);
		req.set_confirm_pwd(confirm);
        req.set_varifycode(src_root["verifycode"].asString());
        RegisterRsp rsp = LogicGrpcClient::getInstance()->RegisterUser(req);
        root["error"] = rsp.error();
        root["email"] = src_root["email"];
        root["server"] = "GateServer";
        std::string jsonstr = root.toStyledString();
        boost::beast::ostream(connection->_response.body()) << jsonstr;
        return true;

		});

	//注册重置密码请求
    RegPost("/reset_password", [](std::shared_ptr<HttpConnection> connection) { 
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
        std::cout << "receive body is " << body_str << std::endl;
        connection->_response.set(boost::beast::http::field::content_type,"application/json");
        Json::Value root;
        Json::Reader reader;
        Json::Value src_root;
        bool parse_success = reader.parse(body_str, src_root);
        if (!parse_success)
		{
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = message::ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			boost::beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}
		if (!src_root.isMember("email") || !src_root.isMember("verifycode") ||
			!src_root.isMember("password") || !src_root.isMember("confirm"))
		{
			std::cout << "[GateServer] Missing required JSON fields!" << std::endl;
			root["error"] = message::ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			boost::beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

        auto email = src_root["email"].asString();
        auto verifycode = src_root["verifycode"].asString();
        auto pwd = src_root["password"].asString();
        auto confirm = src_root["confirm"].asString();
        if (pwd != confirm)
        {
            std::cout << "两次密码不一致" << std::endl;
            root["error"] = message::ErrorCodes::PasswdErr;
            std::string jsonstr = root.toStyledString();
            boost::beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }
		std::cout << "[GateServer] Client email is " << email << std::endl;
        ResetPasswordReq req;
        req.set_email(email);
        req.set_varifycode(verifycode);
        req.set_passwd(pwd);
        req.set_confirm_pwd(confirm);
        ResetPasswordRsp rsp = LogicGrpcClient::getInstance()->ResetPassword(req);
        root["error"] = rsp.error();
        root["email"] = src_root["email"];
        root["server"] = "GateServer";
        std::string jsonstr = root.toStyledString();
        boost::beast::ostream(connection->_response.body()) << jsonstr;
        return true;
		});

	// 注册登录请求
	RegPost("/user_login", [](std::shared_ptr<HttpConnection> connection) {
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "receive body is " << body_str << std::endl;
		connection->_response.set(boost::beast::http::field::content_type, "application/json");
		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;
		bool parse_success = reader.parse(body_str, src_root);

		if (!parse_success)
		{
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = message::ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			boost::beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// 校验必要字段
		if (!src_root.isMember("email") || !src_root.isMember("password"))
		{
			std::cout << "[GateServer] Login missing required JSON fields!" << std::endl;
			root["error"] = message::ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			boost::beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		auto email = src_root["email"].asString();
		auto pwd = src_root["password"].asString();

		std::cout << "[GateServer] Client login email is " << email << std::endl;

		// 构造 gRPC 请求
		LoginReq req;
		req.set_email(email);
		req.set_passwd(pwd);

		// 发送 gRPC 请求给 LogicServer
		LoginRsp rsp = LogicGrpcClient::getInstance()->Login(req);

		// 构造返回给 Qt 的 JSON
		root["error"] = rsp.error();
		root["email"] = email;
		root["server"] = "GateServer";

		if (rsp.error() == message::ErrorCodes::SUCCESS)
		{
			// 登录成功，返回核心数据
			root["uid"] = rsp.uid();
			root["token"] = rsp.token();
			root["name"] = rsp.name();
			root["avatar"] = rsp.avatar(); // 把头像也返回去
			root["host"] = rsp.host();     // CanvasServer IP
			root["port"] = rsp.port();     // CanvasServer Port
			std::cout << "[GateServer] User login success: " << email << " -> " << rsp.host() << ":" << rsp.port() << std::endl;
		}
		else
		{
			std::cout << "[GateServer] User login failed: " << email << " ErrorCode: " << rsp.error() << std::endl;
		}

		std::string jsonstr = root.toStyledString();
		boost::beast::ostream(connection->_response.body()) << jsonstr;
		return true;
		});

	// 注册获取 OSS 上传签名的接口
	RegPost("/get_oss_token", [](std::shared_ptr<HttpConnection> connection) {
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());

		//读取配置文件 (使用你的 ConfigMgr)
		auto& cfg = ConfigMgr::Inst();
		std::string access_id = cfg["AliyunOSS"]["AccessKeyId"];
		std::string access_secret = cfg["AliyunOSS"]["AccessKeySecret"];
		std::string bucket = cfg["AliyunOSS"]["BucketName"];
		std::string host = cfg["AliyunOSS"]["Host"]; // http://bucket.endpoint

		//解析客户端请求
		connection->_response.set(boost::beast::http::field::content_type, "application/json");
		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;
		if (!reader.parse(body_str, src_root))
		{
			root["error"] = message::ErrorCodes::Error_Json;
			boost::beast::ostream(connection->_response.body()) << root.toStyledString();
			return true;
		}

		//准备文件名和过期时间
		std::string uid = src_root.get("uid", "0").asString();
		std::string suffix = src_root.get("suffix", "jpg").asString();
		// 必须和 Qt 客户端上传时设置的 Header 一模一样！
		std::string content_type = "image/jpeg";
		if (suffix == "png")
		{
			content_type = "image/png";
		}
		std::time_t now = std::time(nullptr);
		std::time_t expire_time = now + 600; // 10分钟后过期

		//生成文件名：avatars/uid_时间戳.jpg
		std::string object_name = "avatars/" + uid + "_" + std::to_string(now) + "." + suffix;

		//构造待签名字符串 (String To Sign)
		// 格式：PUT + \n + Content-MD5(空) + \n + Content-Type + \n + Expires + \n + CanonicalizedResource
		std::string string_to_sign = "PUT\n\n" + content_type + "\n" + std::to_string(expire_time) + "\n" + "/" + bucket + "/" + object_name;
		std::cout << "[OSS Debug] StringToSign:\n" << string_to_sign << std::endl;

		//计算签名 (HMAC-SHA1 -> Base64 -> UrlEncode)
		std::string signature = Base64Encode(HmacSha1(access_secret, string_to_sign));
		std::string encoded_signature = UrlEncode(signature); // 使用你现有的 UrlEncode

		//拼接最终 URL
		std::stringstream ss;
		ss << host << "/" << object_name
			<< "?OSSAccessKeyId=" << access_id
			<< "&Expires=" << expire_time
			<< "&Signature=" << encoded_signature;

		std::string public_url = host + "/" + object_name;		//公共链接

		//返回给 Qt
		root["error"] = message::ErrorCodes::SUCCESS;
		root["url"] = ss.str();				// 上传用的 URL
		root["public_url"] = public_url;	// 公开链接
		root["oss_key"] = object_name;		// 文件路径 key

		//打印日志方便调试
		std::cout << "[OSS] Generated URL for uid " << uid << std::endl;

		boost::beast::ostream(connection->_response.body()) << root.toStyledString();
		return true;
		});

	RegPost("/save_avator", [](std::shared_ptr<HttpConnection> connection) {
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());

		// 【调试日志 1】确认 GateServer 是否真正收到了请求
		std::cout << "------------------------------------------------" << std::endl;
		std::cout << "[GateServer] Receive HTTP Post: /save_avator" << std::endl;
		std::cout << "[GateServer] Body: " << body_str << std::endl;

		connection->_response.set(boost::beast::http::field::content_type, "application/json");
		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;

		//解析参数
		if (!reader.parse(body_str, src_root))
		{
			// 【调试日志 2】JSON 解析失败
			std::cout << "[GateServer] Error: Failed to parse JSON!" << std::endl;
			root["error"] = message::ErrorCodes::Error_Json;
			boost::beast::ostream(connection->_response.body()) << root.toStyledString();
			return true;
		}

		//必须要有 uid，不然不知道给谁存
		if (!src_root.isMember("uid") || !src_root.isMember("public_url"))
		{
			// 【调试日志 3】字段缺失
			std::cout << "[GateServer] Error: Missing 'uid' or 'public_url' field!" << std::endl;
			root["error"] = message::ErrorCodes::Error_Json;
			boost::beast::ostream(connection->_response.body()) << root.toStyledString();
			return true;
		}

		int uid = src_root["uid"].asInt();
		std::string public_url = src_root["public_url"].asString();

		// 【调试日志 4】确认解析出的数据是否正确
		std::cout << "[GateServer] Parsed UID: " << uid << std::endl;
		std::cout << "[GateServer] Parsed URL: " << public_url << std::endl;

		//构造 gRPC 请求
		message::UpdateAvatarReq req;
		req.set_uid(uid);
		req.set_avatar_url(public_url);

		//呼叫 LogicServer
		message::UpdateAvatarRsp rsp = LogicGrpcClient::getInstance()->UpdateAvatar(req);

		// 【调试日志 5】gRPC 调用返回
		std::cout << "[GateServer] LogicServer Response ErrorCode: " << rsp.error() << std::endl;

		//返回结果给 Qt
		root["error"] = rsp.error();
		root["uid"] = rsp.uid();
		if (rsp.error() == message::ErrorCodes::SUCCESS)	//// 只有 rsp.error() == 0 时，客户端才敢用这个地址刷新 UI
		{
			root["public_url"] = public_url;
		}

		std::string jsonstr = root.toStyledString();
		boost::beast::ostream(connection->_response.body()) << jsonstr;
		return true;
		});

}

void LogicSystem::RegGet(std::string url, HttpHandler handler)
{
	_get_handlers.insert(make_pair(url, handler));	//将url对应的处理函数插入到map中
}

void LogicSystem::RegPost(std::string url, HttpHandler handler)
{
	_post_handlers.insert(make_pair(url, handler));	//将url对应的处理函数插入到map中
}

bool LogicSystem::HandleGet(std::string path, std::shared_ptr<HttpConnection> con)
{
	if (_get_handlers.find(path) == _get_handlers.end())
	{
		return false;
	}
	//调用处理函数
	_get_handlers[path](con);
	return true;
}

bool LogicSystem::HandlePost(std::string path, std::shared_ptr<HttpConnection> con)
{
	if (_post_handlers.find(path) == _post_handlers.end())
	{
		return false;
	}
	//调用处理函数
	_post_handlers[path](con);
	return true;
}