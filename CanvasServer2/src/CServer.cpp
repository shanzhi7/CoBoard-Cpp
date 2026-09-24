#include "CanvasServer/CServer.h"
#include "CanvasServer/CSession.h"
#include "Logger/Logger.h"

CServer::CServer(boost::asio::io_context& ioc, short port)
	:_io_context(ioc), _port(port),_acceptor(ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
{
	LOG_INFO_CTX("CServer::CServer", "服务器初始化 port=" << _port);
	Start();	// Start
}
CServer::~CServer()
{
	LOG_INFO_CTX("CServer::~CServer", "服务器停止");
}
void CServer::Start()
{
	StartAccept();	//开始监听新连接
}
void CServer::StartAccept()
{ 
	auto& worker_io_context = AsioIOServicePool::getInstance()->GetIOService();	// 获取io_context
	// 创建一个Session
	std::shared_ptr<CSession> new_session = std::make_shared<CSession>(worker_io_context);

	//异步接受新连接
	_acceptor.async_accept(new_session->GetSocket(),
		[this, new_session](const boost::system::error_code& error)
		{
			HandleAccept(new_session, error);
		});

}
void CServer::HandleAccept(std::shared_ptr<CSession> new_session, const boost::system::error_code& error)
{
	if (!error)
	{
		boost::system::error_code option_ec;
		new_session->GetSocket().set_option(boost::asio::ip::tcp::no_delay(true), option_ec);
		if (option_ec)
		{
			LOG_WARN_CTX("CServer::HandleAccept", "设置 TCP_NODELAY 失败: " << option_ec.message());
		}

		// 启动Session
		new_session->Start();
	}
	else
	{
		LOG_ERROR_CTX("CServer::HandleAccept", "接收连接失败: " << error.message());
	}

	// 继续监听新连接
	StartAccept();
}
