#include "GateServer/HttpConnection.h"
#include "GateServer/LogicSystem.h"

HttpConnection::HttpConnection(boost::asio::io_context& ioc) : _socket(ioc)
{

}

boost::asio::ip::tcp::socket& HttpConnection::GetSocket()
{
	return _socket;
}
void HttpConnection::PreParseGetParam()
{
	// 提取 URI  
	auto uri = _request.target();
	// 查找查询字符串的开始位置（即 '?' 的位置）  
	auto query_pos = uri.find('?');
	if (query_pos == std::string::npos)
	{
		_get_url = uri;
		return;
	}

	_get_url = uri.substr(0, query_pos);
	std::string query_string = uri.substr(query_pos + 1);
	std::string key;
	std::string value;
	size_t pos = 0;
	while ((pos = query_string.find('&')) != std::string::npos)
	{
		auto pair = query_string.substr(0, pos);
		size_t eq_pos = pair.find('=');
		if (eq_pos != std::string::npos)
		{
			key = UrlDecode(pair.substr(0, eq_pos)); // 假设有 url_decode 函数来处理URL解码  
			value = UrlDecode(pair.substr(eq_pos + 1));
			_get_params[key] = value;
		}
		query_string.erase(0, pos + 1);
	}
	// 处理最后一个参数对（如果没有 & 分隔符）  
	if (!query_string.empty())
	{
		size_t eq_pos = query_string.find('=');
		if (eq_pos != std::string::npos) {
			key = UrlDecode(query_string.substr(0, eq_pos));
			value = UrlDecode(query_string.substr(eq_pos + 1));
			_get_params[key] = value;
		}
	}
}
void HttpConnection::WriteResponse()
{
	auto self(shared_from_this());

	_response.content_length(_response.body().size());		//设置content-length
	boost::beast::http::async_write(
		_socket,
		_response,
		[self](boost::beast::error_code ec, std::size_t bytes_transferred)
		{
			try
			{
				if (ec)
				{
					//发生错误
					std::cout << "//HttpConnection::WriteResponse() 错误: " << ec.message() << std::endl;
					return;
				}
				boost::ignore_unused(bytes_transferred);	//屏蔽未使用变量警告
				self->_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
				self->deadline_.cancel();					//停止定时器
			}
			catch (const std::exception& e)
			{
				std::cout << "//HttpConnection::WriteResponse() 错误: " << ec.message() << std::endl;
			}
		}
	);
}
void HttpConnection::HandleReq()
{
	//设置版本
    _response.version(_request.version());
	_response.keep_alive(false);			//http短连接，不保持长连接

	if (_request.method() == boost::beast::http::verb::get)	//处理get请求
	{
        PreParseGetParam();		//预处理get参数
		bool success = LogicSystem::getInstance()->HandleGet(_get_url, shared_from_this());	//交给逻辑层处理get请求
		if (!success)	//success为false，返回404
		{
			_response.result(boost::beast::http::status::not_found);
			_response.set(boost::beast::http::field::content_type, "text/plain");
			boost::beast::ostream(_response.body()) << "url not found\r\n";		//写入答复内容
			WriteResponse();												//进行答复
			return;
		}
		_response.result(boost::beast::http::status::ok);
		_response.set(boost::beast::http::field::server, "GateServer");
		WriteResponse();
	}

	if (_request.method() == boost::beast::http::verb::post)	//处理post请求
	{
		bool success = LogicSystem::getInstance()->HandlePost(_request.target(), shared_from_this());
		if (!success)
		{
			_response.result(boost::beast::http::status::not_found);
			_response.set(boost::beast::http::field::content_type, "text/plain");
			boost::beast::ostream(_response.body()) << "url not found\r\n";
			WriteResponse();
			return;
		}

		_response.result(boost::beast::http::status::ok);
		_response.set(boost::beast::http::field::server, "GateServer");
		WriteResponse();
		return;
	}
}
void HttpConnection::CheckDeadline()
{
	auto self = shared_from_this();
	//定时器超时或者取消触犯回调函数，关闭套接字
	deadline_.async_wait([self](boost::beast::error_code ec) {
		if (!ec)
		{
			self->_socket.close(ec);
			return;
		}
		});
}
void HttpConnection::Start()
{ 
	auto self(shared_from_this());

	// 接收HTTP请求
    boost::beast::http::async_read(
        _socket,
        _buffer,
        _request,
        [self](boost::beast::error_code ec, std::size_t bytes_transferred)
        { 
			try
			{ 
				if (ec)
				{
					//发生错误
					std::cout << "//HttpConnection::Start() 错误: " << ec.message() << std::endl;
					return;
				}
				boost::ignore_unused(bytes_transferred);	//屏蔽未使用变量警告
				self->HandleReq();							//处理请求
				self->CheckDeadline();						//检测超时
			}
			catch (const std::exception& e)
			{
				std::cout << "//HttpConnection::Start() 异常: " << e.what() << std::endl;
			}
        }
    );
}