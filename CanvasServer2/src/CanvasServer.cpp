// CanvasServer.cpp: 定义应用程序的入口点。
//

#include "CanvasServer/CanvasServer.h"
#include "CanvasServer/CServer.h"
#include "CanvasServer/ConfigMgr.h"
#include "CanvasServer/RedisMgr.h"
#include "CanvasServer/LogicSystem.h"
#include "CanvasServer/SessionMgr.h"
#include "CanvasServer/RoomMgr.h"
#include "CanvasServer/AsioIOServicePool.h"
#include "Logger/Logger.h"

#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <clocale>
#include <boost/asio.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;

static void ConfigureConsoleUtf8()
{
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#else
    std::setlocale(LC_ALL, "C.UTF-8");
#endif
}

int main()
{
    ConfigureConsoleUtf8();
    Logger::Init("CanvasServer2", "logs/CanvasServer2.log");
    LOG_INFO_CTX("CanvasServer2::main", "服务启动");
    try
    {
        // 初始化配置
        auto& cfg = ConfigMgr::Inst();
        LOG_INFO_CTX("CanvasServer2::main", "配置加载完成");

        // 初始化 Redis
        RedisMgr::getInstance();
        LOG_INFO_CTX("CanvasServer2::main", "RedisMgr 初始化完成");

        // 初始化线程池 (AsioIOServicePool)
        // 要在 Server 启动前把 IO 线程跑起来
        AsioIOServicePool::getInstance();
        LOG_INFO_CTX("CanvasServer2::main", "IO 线程池初始化完成");

        // 初始化业务逻辑系统 (LogicSystem 构造时会启动业务处理线程)
        LogicSystem::getInstance();
        LOG_INFO_CTX("CanvasServer2::main", "LogicSystem 初始化完成");

        // 初始化管理器
        SessionMgr::getInstance();
        RoomMgr::getInstance();
        LOG_INFO_CTX("CanvasServer2::main", "SessionMgr 和 RoomMgr 初始化完成");

        // 准备网络环境
        std::string host = cfg["SelfServer"]["Host"];
        int port = std::stoi(cfg["SelfServer"]["Port"]);

        // 创建主线程的 io_context，专门给 CServer 的 Acceptor 用
        boost::asio::io_context io_context;

        // 注册信号处理 (Ctrl+C 优雅退出)
        // 防止强制关闭导致单例析构异常
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&io_context](const boost::system::error_code& error, int signal_number)
            {
                if (!error)
                {
                    LOG_INFO_CTX("CanvasServer2::main", "收到停止信号 signal=" << signal_number);

                    // 停止 Accept
                    io_context.stop();

                    // 停止 IO 线程池
                    AsioIOServicePool::getInstance()->Stop();
                }
            });

        // 启动 TCP 服务器
        // CServer 构造函数里已经写了 StartAccept()，所以实例化就会开始监听
        CServer server(io_context, port);
        LOG_INFO_CTX("CanvasServer2::main", "TCP 服务监听 port=" << port);

        // 阻塞主线程，处理连接请求
        // 之后的 Read/Write 操作会由 AsioIOServicePool 里的线程去跑，不占用这里
        io_context.run();

        LOG_INFO_CTX("CanvasServer2::main", "服务正常停止");
    }
    catch (const std::exception& e)
    {
		LOG_ERROR_CTX("CanvasServer2::main", "服务异常: " << e.what());
        Logger::Shutdown();
        return -1;
    }
    catch (...)
    {
		LOG_ERROR_CTX("CanvasServer2::main", "服务发生未知异常");
        Logger::Shutdown();
        return -1;
    }
	Logger::Shutdown();
	return 0;
}
