// HttpConnection类，用于读取http请求，回复客户端
#pragma once
#include <iostream>
#include <memory>
#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <map>
#include <unordered_map>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>

class HttpConnection : public std::enable_shared_from_this<HttpConnection>
{ 
	friend class LogicSystem;
public:
	HttpConnection(boost::asio::io_context& ioc);
	void Start();							//开始接收请求，连接在CServer启动时创建
	boost::asio::ip::tcp::socket& GetSocket();

private:

	void PreParseGetParam();									//解析get请求参数
	void WriteResponse();										//回复客户端
	void HandleReq();											//处理请求
	void CheckDeadline();										//检查请求是否超时

	boost::asio::ip::tcp::socket _socket;	//用于与客户端通信

	std::string _get_url;										//get请求的url
	std::unordered_map<std::string, std::string> _get_params;	//参数解析map

	boost::beast::flat_buffer _buffer{ 8192 };							//数据缓冲区
	boost::beast::http::request<boost::beast::http::dynamic_body> _request;			//用于接受任意类型的请求
	boost::beast::http::response<boost::beast::http::dynamic_body> _response;		//任意类型的答复
	boost::asio::steady_timer deadline_
	{
		_socket.get_executor(),std::chrono::seconds(60)			//定时器, 用于判断请求是否超时
	};
};