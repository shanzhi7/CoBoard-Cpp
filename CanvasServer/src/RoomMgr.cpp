#include "CanvasServer/RoomMgr.h"
#include "CanvasServer/Room.h"
#include "Logger/Logger.h"

RoomMgr::RoomMgr()
{
}

RoomMgr::~RoomMgr()
{
	std::lock_guard<std::mutex> lock(_mutex);
	_rooms.clear();
}

std::shared_ptr<Room> RoomMgr::GetOrCreateRoom(const std::string& room_id)
{
	std::lock_guard<std::mutex> lock(_mutex);

	//先查找是否存在这个房间
	auto it = _rooms.find(room_id);
    if (it != _rooms.end())
    {
        return it->second;
    }

	//不存在则创建
	auto new_room = std::make_shared<Room>(room_id);
	_rooms[room_id] = new_room;
	LOG_INFO_CTX("RoomMgr::GetOrCreateRoom", "创建房间 room_id=" << room_id << " total=" << _rooms.size());
	return new_room;
}

std::shared_ptr<Room> RoomMgr::GetRoom(const std::string& room_id)
{ 
	std::lock_guard<std::mutex> lock(_mutex);

	auto it = _rooms.find(room_id);
	if (it != _rooms.end())
	{
		return it->second;
	}

	return nullptr;
}

void RoomMgr::RemoveRoom(const std::string& room_id)
{
	std::lock_guard<std::mutex> lock(_mutex);

	auto it = _rooms.find(room_id);
	if (it != _rooms.end())
	{
		_rooms.erase(it);
		LOG_INFO_CTX("RoomMgr::RemoveRoom", "移除房间 room_id=" << room_id << " total=" << _rooms.size());
	}
}
