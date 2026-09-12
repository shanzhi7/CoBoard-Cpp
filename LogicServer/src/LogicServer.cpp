// LogicServer.cpp: 定义应用程序的入口点。
//

#include "LogicServer/LogicServer.h"
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <clocale>
#include <grpcpp/grpcpp.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "LogicServer/LogicServiceImpl.h"
#include "LogicServer/ConfigMgr.h"
#include "LogicServer/RedisMgr.h"
#include "LogicServer/MysqlMgr.h" // 假设你封装了 MysqlMgr 单例
#include "Logger/Logger.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

static void ConfigureConsoleUtf8()
{
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#else
    std::setlocale(LC_ALL, "C.UTF-8");
#endif
}

// 启动grpc服务函数
void RunServer()
{
    // 读取配置文件
    auto& cfg = ConfigMgr::Inst();
    std::string host = cfg["LogicServer"]["Host"];
    std::string port = cfg["LogicServer"]["Port"];
    std::string server_address = host + ":" + port;

    // 创建服务
    LogicServiceImpl service;               // 实例化服务类
    ServerBuilder builder;                  // 创建服务构建器
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());    // 监听端口 (不使用 SSL 证书)
    builder.RegisterService(&service);      // 注册服务

    // 启动服务
    std::unique_ptr<Server> server(builder.BuildAndStart());
    LOG_INFO_CTX("LogicServer::RunServer", "gRPC 服务监听 address=" << server_address);

    //阻塞等待，直到服务器被关闭
    server->Wait();
}

int main()
{
    ConfigureConsoleUtf8();
    Logger::Init("LogicServer", "logs/LogicServer.log");
    LOG_INFO_CTX("LogicServer::main", "服务启动");
    try
    {
        //初始化各种单例类，确保它们在程序运行期间可用
        ConfigMgr::Inst();
        LOG_INFO_CTX("LogicServer::main", "配置加载完成");
        MysqlMgr::getInstance();
        LOG_INFO_CTX("LogicServer::main", "MysqlMgr 初始化完成");
        RedisMgr::getInstance();
        LOG_INFO_CTX("LogicServer::main", "RedisMgr 初始化完成");

        // 启动grpc服务
        RunServer();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR_CTX("LogicServer::main", "服务异常: " << e.what());
        std::cerr << "[LogicServer] Crashed with exception: " << e.what() << std::endl;
		Logger::Shutdown();
        return -1;
    }
    catch (...)
    {
        LOG_ERROR_CTX("LogicServer::main", "服务发生未知异常");
        std::cerr << "[LogicServer] Crashed with unknown exception." << std::endl;
		Logger::Shutdown();
        return -1;
    }
	Logger::Shutdown();
	return 0;
}
