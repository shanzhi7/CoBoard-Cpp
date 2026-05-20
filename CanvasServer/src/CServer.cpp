#include "CanvasServer/CServer.h"
#include "CanvasServer/CSession.h"

CServer::CServer(boost::asio::io_context& ioc, short port)
	:_io_context(ioc), _port(port),_acceptor(ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
{
	std::cout << "[CServer] Server initialized on port: " << _port << std::endl;
	Start();
}

CServer::~CServer()
{
	std::cout << "[CServer] Server stopped." << std::endl;
}

void CServer::Start()
{
	StartAccept();
}

void CServer::StartAccept()
{
	auto& worker_io_context = AsioIOServicePool::getInstance()->GetIOService();
	std::shared_ptr<CSession> new_session = std::make_shared<CSession>(worker_io_context);

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
			std::cout << "[CServer] set TCP_NODELAY failed: " << option_ec.message() << std::endl;
		}

		new_session->Start();
	}
	else
	{
		std::cout << "[CServer] Accept error: " << error.message() << std::endl;
	}

	StartAccept();
}
