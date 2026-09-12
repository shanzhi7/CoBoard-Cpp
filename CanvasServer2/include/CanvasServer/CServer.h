#pragma once
#include <boost/asio.hpp>
#include <memory>
#include <iostream>
#include "CanvasServer/AsioIOServicePool.h"

class CSession;//前置声明
class CServer
{
public:
	CServer(boost::asio::io_context& ioc, short port);
	~CServer();

	void Start();
private:
	void StartAccept();		//接受连接
	void HandleAccept(std::shared_ptr<CSession> new_session, const boost::system::error_code& error);

	boost::asio::io_context& _io_context; // 依然是主线程的 io_context
	short _port;
	boost::asio::ip::tcp::acceptor _acceptor;
};