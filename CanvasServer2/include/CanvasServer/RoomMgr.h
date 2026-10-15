/***********************************************************************************
* @file         RoomMgr.h
* @brief        本机房间索引与条件移除
* @author       shanzhi
* @date         2026/01/30
* @history
***********************************************************************************/
#pragma once
#include "CanvasServer/Singleton.h"
#include <mutex>
#include <unordered_map>
#include <memory>
#include <string>

//前置声明 Room
class Room;

class RoomMgr : public Singleton<RoomMgr>
{
	friend class Singleton<RoomMgr>;
public:
	~RoomMgr(); // 回收本机房间引用。

	//获取房间，如果不存在则创建一个新的
	std::shared_ptr<Room> GetOrCreateRoom(const std::string& room_id); // 查找或创建本机房间。

	// 获取房间，如果不存在返回 nullptr (用于只读操作，比如查询房间人数)
	std::shared_ptr<Room> GetRoom(const std::string& room_id); // 查找本机房间，不创建。

	// 移除房间 (通常在房间人数为0时调用)
	void RemoveRoom(const std::string& room_id, const Room* expected_room); // 仅移除已完成清理的预期房间。
private:
	RoomMgr(); // 初始化房间索引。

	std::mutex _mutex; // 保护本机房间索引。
	// 房间ID -> 房间对象
	std::unordered_map<std::string, std::shared_ptr<Room>> _rooms; // 房间号到本机房间的拥有引用。
};