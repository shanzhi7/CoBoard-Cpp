/***********************************************************************************
* @file         RoomLifecycleHandler.h
* @brief        房间退出、解散和断线清理编排，不拥有独立线程
* @author       shanzhi
* @date         2026/10/14
* @history
***********************************************************************************/
#pragma once

#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

class CSession;
class Room;
class RoomLifecycleHandler
{
public:
	void Handle(std::shared_ptr<CSession> session, const short& msg_id, const std::string& body); // 编排退出或解散，并幂等返回结果。
	void HandleDisconnected(std::shared_ptr<CSession> session); // 清理精确断线连接，保留房间元数据。
private:
	struct CompletedRequest
	{
		std::string _room_id; // 防止相同请求关联被用于其他目标。
		std::string _body; // 可重复返回的完成响应。
		std::chrono::steady_clock::time_point _finished_at; // 十分钟保留期限的起点。
	};
	void PruneCompleted(); // 清理过期记录和超过上限的最早记录。
	int Leave(std::shared_ptr<CSession> session, const std::string& room_id); // 普通成员退出，Redis 确认后清内存。
	int Destroy(std::shared_ptr<CSession> session, const std::string& room_id); // 房主解散，关闭中可以继续重试。
	std::unordered_map<std::string, CompletedRequest> _completed; // 仅业务线程访问的完成缓存，最多 1024 条。
	std::deque<std::string> _completion_order; // 按完成时间排列的缓存键。
};
