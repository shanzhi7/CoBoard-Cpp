/***********************************************************************************
* @file         RedisMgr.h
* @brief        Redis 连接、账号缓存及原子 Token 撤销
* @author       shanzhi
* @date         2026/01/22
* @history
***********************************************************************************/
#pragma once
#include "LogicServer/Singleton.h"
#include <sw/redis++/redis++.h>
#include <string>
#include <iostream>
#include <memory>

using namespace sw::redis;

class RedisMgr : public Singleton<RedisMgr>
{
	friend class Singleton<RedisMgr>;
public:
	~RedisMgr(); // 回收 Redis 连接池。

	//初始化连接
	bool Connect(const std::string& host, int port,const std::string& pwd); // 建立有限超时的连接池。

	bool Get(const std::string& key, std::string& value);			//获取key对应的value
	bool Set(const std::string& key, const std::string& value, int timeout = 0);	//设置key对应的value默认为 0（代表永不过期）
    bool Del(const std::string& key);								//删除key
    bool ExistsKey(const std::string& key);							//判断key是否存在

	//List操作
	bool LPush(const std::string& key, const std::string& value);	//添加元素
	bool LPop(const std::string& key, std::string& value);			//获取元素，删除元素

	//HSet操作
	bool HSet(const std::string& key, const std::string& hkey, const std::string& value);	//设置key对应的hkey对应的value
    bool HGet(const std::string& key, const std::string& hkey, std::string& value);			//获取key对应的hkey对应的value

	int RevokeToken(int uid, const std::string& token); // 原子撤销登录，返回公共错误码。
	void Close(); // 释放 Redis 客户端。

private:
    RedisMgr(); // 初始化配置中的 Redis 连接。
	std::unique_ptr<Redis> _redis; // 独占 Redis 客户端。
};
