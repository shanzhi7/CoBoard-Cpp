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
	~RoomMgr();

	//获取房间，如果不存在则创建一个新的
	std::shared_ptr<Room> GetOrCreateRoom(const std::string& room_id);

	// 获取房间，如果不存在返回 nullptr (用于只读操作，比如查询房间人数)
	std::shared_ptr<Room> GetRoom(const std::string& room_id);

	// 移除房间 (通常在房间人数为0时调用)
	void RemoveRoom(const std::string& room_id);
private:
	RoomMgr();

	std::mutex _mutex;
	// 房间ID -> 房间对象
	std::unordered_map<std::string, std::shared_ptr<Room>> _rooms;
};