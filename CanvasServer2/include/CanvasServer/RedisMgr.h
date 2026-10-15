/***********************************************************************************
* @file         RedisMgr.h
* @brief        房间共享元数据、成员集合和会话认证查询
* @author       shanzhi
* @date         2026/01/30
* @history
***********************************************************************************/
#pragma once
#include "CanvasServer/Singleton.h"
#include "CanvasServer/const.h"
#include <sw/redis++/redis++.h>
#include <string>
#include <iostream>
#include <memory>

using namespace sw::redis;

class RedisMgr : public Singleton<RedisMgr>
{
	friend class Singleton<RedisMgr>;
public:
	~RedisMgr(); // 释放 Redis 连接池。

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

	//创建房间信息(写入Hash)
	bool CreateRoom(const std::string& room_id, const RoomInfo& room_info); // 写入共享房间元数据。

	//获取房间信息(读取Hash)
	bool GetRoomInfo(const std::string& room_id, RoomInfo& room_info); // 读取房间元数据的兼容布尔入口。

	//用户加入房间(写入Hash)
	bool AddUserToRoom(const std::string& room_id, const std::string& uid); // 登记成员资源访问资格。
	bool RemoveUserFromRoom(const std::string& room_id, const std::string& uid); // 用户离开房间时移除成员，防止离线用户继续通过网关获取资源签名。

	int ReadRoomInfo(const std::string& room_id, RoomInfo& room_info); // 区分房间不存在与 Redis 故障。
	int DeleteRoom(const std::string& room_id, int owner_uid); // 校验房主并原子删除房间及成员集合。
	int VerifySessionToken(int uid, const std::string& token); // 重新验证低频退出请求的认证凭据。
	void Close(); // 释放 Redis 客户端。

private:
    RedisMgr(); // 从配置初始化 Redis。
	std::unique_ptr<Redis> _redis; // 独占 Redis 连接池。
};
