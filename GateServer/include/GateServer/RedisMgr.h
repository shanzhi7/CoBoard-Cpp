#pragma once
#include "Singleton.h"
#include <sw/redis++/redis++.h>
#include <string>
#include <iostream>
#include <memory>

using namespace sw::redis;

class RedisMgr : public Singleton<RedisMgr>
{
	friend class Singleton<RedisMgr>;
public:
	~RedisMgr();

	//初始化连接
	bool Connect(const std::string& host, int port,const std::string& pwd);

	bool Get(const std::string& key, std::string& value);			//获取key对应的value
    bool Set(const std::string& key, const std::string& value);		//设置key对应的value
    bool Del(const std::string& key);								//删除key
    bool ExistsKey(const std::string& key);							//判断key是否存在

	//List操作
	bool LPush(const std::string& key, const std::string& value);	//添加元素
	bool LPop(const std::string& key, std::string& value);			//获取元素，删除元素

	//HSet操作
	bool HSet(const std::string& key, const std::string& hkey, const std::string& value);	//设置key对应的hkey对应的value
    bool HGet(const std::string& key, const std::string& hkey, std::string& value);			//获取key对应的hkey对应的value

	void Close();

private:
    RedisMgr();
	std::unique_ptr<Redis> _redis;
};
