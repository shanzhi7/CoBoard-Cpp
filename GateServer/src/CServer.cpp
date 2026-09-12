#include "GateServer/CServer.h"
#include "GateServer/HttpConnection.h"
#include "GateServer/AsioIOServicePool.h"
#include "Logger/Logger.h"

CServer::CServer(boost::asio::io_context& ioc, short port)
	:_ioc(ioc), _acceptor(ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
{

}

// 启动服务,监听请求
void CServer::Start()
{
	auto self(shared_from_this());		//延长服务的生命周期

	//获取io_context,HttpConnection使用这个io_context处理IO
	auto& io_context = AsioIOServicePool::getInstance()->GetIOService();
	std::shared_ptr<HttpConnection> new_con = std::make_shared<HttpConnection>(io_context);
	//监听连接
	_acceptor.async_accept(new_con->GetSocket(), [this, self, new_con](boost::system::error_code ec) {
		try
		{
			if (ec)
			{
				//出错放弃连接，继续监听其他连接
                self->Start();
                LOG_ERROR_CTX("CServer::Start", "接收连接失败: " << ec.message());
				return;
			}
			//交给HttpConnection处理这个连接，进行数据的接收和发送
			new_con->Start();

			//继续监听
            self->Start();
		}
		catch (std::exception& e)
		{
            LOG_ERROR_CTX("CServer::Start", "处理连接异常: " << e.what());
			self->Start();	//继续监听
		}
		});
}

