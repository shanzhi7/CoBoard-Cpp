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
	~RedisMgr();

	//初始化连接
	bool Connect(const std::string& host, int port,const std::string& pwd);

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
	bool CreateRoom(const std::string& room_id, const RoomInfo& room_info);

	//获取房间信息(读取Hash)
	bool GetRoomInfo(const std::string& room_id, RoomInfo& room_info);

	//用户加入房间(写入Hash)
	bool AddUserToRoom(const std::string& room_id, const std::string& uid);

	void Close();

private:
    RedisMgr();
	std::unique_ptr<Redis> _redis;
};
