//CServer类，用于启动服务器，监听端口，接收客户端连接
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
class CServer : public std::enable_shared_from_this<CServer>
{ 
public:
	CServer(boost::asio::io_context& ioc, short port);	//构造函数，初始化_acceptor对象
	void Start();
private:
	boost::asio::ip::tcp::acceptor _acceptor;		//接收器对象
	boost::asio::io_context& _ioc;					//上下文
};