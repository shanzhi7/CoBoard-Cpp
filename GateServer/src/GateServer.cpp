#include <iostream>
#include <string>
#include <memory>
#include <clocale>

// 1. Boost
#include <boost/asio.hpp>
// 2. Redis++
#include <sw/redis++/redis++.h>
// 3. JsonCpp
#include <json/json.h>
// 4. gRPC
#include <grpcpp/grpcpp.h>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#endif
#include "GateServer/GateServer.h"
#include "GateServer/ConfigMgr.h"
#include "GateServer/CServer.h"
#include "Logger/Logger.h"


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
	Logger::Init("GateServer", "logs/GateServer.log");
	LOG_INFO_CTX("GateServer::main", "服务启动");
	//TestRedisMgr();
	auto& gCfgMgr = ConfigMgr::Inst();
	std::string gate_port_str = gCfgMgr["GateServer"]["Port"];
	unsigned short gate_port = std::stoi(gate_port_str);

	try
	{
		unsigned short port = static_cast<unsigned short>(gate_port);
		boost::asio::io_context ioc{ 1 };
		boost::asio::signal_set signals{ ioc,SIGINT,SIGTERM };
		signals.async_wait([&ioc](const boost::system::error_code& error, int siganl_numbel) {
			if (error)
			{
				return;
			}
			ioc.stop();
			});
		std::make_shared<CServer>(ioc, port)->Start();
		LOG_INFO_CTX("GateServer::main", "监听端口 port=" << port);
		ioc.run();
	}
	catch (std::exception const& exp)
	{
		LOG_ERROR_CTX("GateServer::main", "服务异常: " << exp.what());
		std::cerr << "Error: " << exp.what() << std::endl;
		Logger::Shutdown();
		return EXIT_FAILURE;
	}
	Logger::Shutdown();
	return 0;
}
