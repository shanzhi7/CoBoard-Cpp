#pragma once
#include "GateServer/Singleton.h"
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
class HttpConnection;

typedef std::function<void(std::shared_ptr<HttpConnection>)>HttpHandler;

class LogicSystem :public Singleton<LogicSystem>
{
	friend class Singleton<LogicSystem>;

public:

	~LogicSystem() {};
	bool HandleGet(std::string path, std::shared_ptr<HttpConnection> con);				//处理get请求
	bool HandlePost(std::string path, std::shared_ptr<HttpConnection> con);				//处理post请求
	void RegGet(std::string url, HttpHandler handler);									//注册get请求
	void RegPost(std::string url, HttpHandler handler);									//注册post请求

private:

	LogicSystem();
	std::map<std::string, HttpHandler> _post_handlers;		//存储post请求处理函数
	std::map<std::string, HttpHandler> _get_handlers;		//存储get请求处理函数
};
