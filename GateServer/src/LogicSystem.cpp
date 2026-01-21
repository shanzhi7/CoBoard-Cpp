#include "GateServer/LogicSystem.h"
#include "GateServer/HttpConnection.h"
#include "GateServer/const.h"
#include "GateServer/VerifyGrpcClient.h"
#include "GateServer//message.pb.h"
#include "GateServer//message.grpc.pb.h"

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

	RegPost("/get_verifycode", [](std::shared_ptr<HttpConnection> connection) {
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());	//获取请求body
		std::cout << "receive body is " << body_str << std::endl;
		connection->_response.set(boost::beast::http::field::content_type,"application/json");
		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;
		bool parse_success = reader.parse(body_str, src_root);

		auto email = src_root["email"].asString();
		auto name = src_root["user"].asString();
		auto pwd = src_root["password"].asString();
		auto confirm = src_root["confirm"].asString();

		if (pwd != confirm)	//判断两次输入的密码是否一致
		{
			std::cout << "两次密码不一致" << std::endl;
			root["error"] = ErrorCodes::PasswdErr;
			std::string jsonstr = root.toStyledString();
			boost::beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		if (!parse_success)	//解析失败
		{
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			boost::beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}
		if (!src_root.isMember("email"))
		{
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			boost::beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}
		std::cout << "client email is " << email << std::endl;
		GetVarifyRsp rsp = VerifyGrpcClient::getInstance()->GetVarifyCode(email); //向grpc服务发送获取验证码请求

		//std::cout << "grpcServer: Verification code sent successfully, the target email is: " << rsp.email()<<std::endl;
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