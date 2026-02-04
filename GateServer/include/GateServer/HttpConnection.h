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

//char 转为16进制
inline unsigned char ToHex(unsigned char x)
{
	return  x > 9 ? x + 55 : x + 48;
}

//char 转化为10进制
inline unsigned char FromHex(unsigned char x)
{
	unsigned char y;
	if (x >= 'A' && x <= 'Z') y = x - 'A' + 10;
	else if (x >= 'a' && x <= 'z') y = x - 'a' + 10;
	else if (x >= '0' && x <= '9') y = x - '0';
	else assert(0);
	return y;
}

//url编码
inline std::string UrlEncode(const std::string& str)
{
	std::string strTemp = "";
	size_t length = str.length();
	for (size_t i = 0; i < length; i++)
	{
		//判断是否仅有数字和字母构成
		if (isalnum((unsigned char)str[i]) ||
			(str[i] == '-') ||
			(str[i] == '_') ||
			(str[i] == '.') ||
			(str[i] == '~'))
			strTemp += str[i];
		else if (str[i] == ' ') //为空字符
			strTemp += "+";
		else
		{
			//其他字符需要提前加%并且高四位和低四位分别转为16进制
			strTemp += '%';
			strTemp += ToHex((unsigned char)str[i] >> 4);
			strTemp += ToHex((unsigned char)str[i] & 0x0F);
		}
	}
	//std::cout << "//url编码: " << strTemp << std::endl;
	return strTemp;
}

//url解码
inline std::string UrlDecode(const std::string& str)
{
	std::string strTemp = "";
	size_t length = str.length();
	for (size_t i = 0; i < length; i++)
	{
		//还原+为空
		if (str[i] == '+') strTemp += ' ';
		//遇到%将后面的两个字符从16进制转为char再拼接
		else if (str[i] == '%')
		{
			assert(i + 2 < length);
			unsigned char high = FromHex((unsigned char)str[++i]);
			unsigned char low = FromHex((unsigned char)str[++i]);
			strTemp += high * 16 + low;
		}
		else strTemp += str[i];
	}
	//std::cout << "//url解码: " << strTemp << std::endl;
	return strTemp;
}

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