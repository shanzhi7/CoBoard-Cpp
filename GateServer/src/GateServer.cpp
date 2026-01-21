#include <iostream>
#include <string>
#include <memory>

// 1. Boost
#include <boost/asio.hpp>
// 2. Redis++
#include <sw/redis++/redis++.h>
// 3. JsonCpp
#include <json/json.h>
// 4. MySQL
#include <mysql/jdbc.h> 
// 5. gRPC
#include <grpcpp/grpcpp.h>
#include <memory>
#include "GateServer/GateServer.h"
#include "GateServer/ConfigMgr.h"
#include "GateServer/CServer.h"


int main()
{
	// 设置控制台输入输出为 UTF-8
	//SetConsoleOutputCP(65001);
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
		std::cout << "gateServer listen on " << port << std::endl;
		ioc.run();
	}
	catch (std::exception const& exp)
	{
		std::cerr << "Error: " << exp.what() << std::endl;
		return EXIT_FAILURE;
	}
	return 0;
}