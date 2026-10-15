#include "CanvasServer/RoomLifecycleHandler.h"
#include "CanvasServer/CSession.h"
#include "CanvasServer/Room.h"
#include "CanvasServer/RoomMgr.h"
#include "CanvasServer/SessionMgr.h"
#include "CanvasServer/RedisMgr.h"
#include "CanvasServer/ConfigMgr.h"
#include "Logger/Logger.h"
#include <algorithm>
#include <cctype>

void RoomLifecycleHandler::PruneCompleted()
{
	// 1. 完成记录只在业务线程访问，按时间和数量限制移除最早项。
	const auto now = std::chrono::steady_clock::now();
	while (!_completion_order.empty())
	{
		auto iterator = _completed.find(_completion_order.front());
		if (iterator != _completed.end() && _completed.size() <= 1024 &&
			now - iterator->second._finished_at < std::chrono::minutes(10)) break;
		if (iterator != _completed.end()) _completed.erase(iterator);
		_completion_order.pop_front();
	}
}

void RoomLifecycleHandler::Handle(std::shared_ptr<CSession> session, const short& msg_id, const std::string& body)
{
	// 1. 两种请求字段兼容，使用各自协议解析；身份只取自认证会话。
	if (!session || session->IsClosed()) return;
	std::string room_id;
	std::string request_id;
	bool parsed = false;
	if (msg_id == ID_LEAVE_ROOM_REQ)
	{
		message::LeaveRoomReq request;
		parsed = request.ParseFromString(body);
		room_id = request.room_id();
		request_id = request.request_id();
	} else
	{
		message::DestroyRoomReq request;
		parsed = request.ParseFromString(body);
		room_id = request.room_id();
		request_id = request.request_id();
	}
	const int uid = session->GetUserId();
	const short response_id = msg_id == ID_LEAVE_ROOM_REQ ? ID_LEAVE_ROOM_RSP : ID_DESTROY_ROOM_RSP;
	message::LeaveRoomRsp response;
	response.set_room_id(room_id);
	response.set_request_id(request_id);
	response.set_error(message::Error_Json);
	const bool valid = parsed && room_id.size() == 6 && !request_id.empty() && request_id.size() <= 128 &&
		std::all_of(room_id.begin(), room_id.end(), [](unsigned char value) { return std::isdigit(value) != 0; });
	// 2. 先检查当前连接及登录凭据，再读取完成缓存，旧账号不能重放新账号的操作。
	if (valid)
	{
		response.set_error(uid > 0 && SessionMgr::getInstance()->GetSession(uid) == session
			? RedisMgr::getInstance()->VerifySessionToken(uid, session->GetToken()) : message::LoginErr);
	}
	PruneCompleted();
	const std::string key = std::to_string(uid) + ":" + std::to_string(msg_id) + ":" + request_id;
	if (response.error() == message::SUCCESS)
	{
		auto completed = _completed.find(key);
		if (completed != _completed.end())
		{
			if (completed->second._room_id == room_id)
			{
				session->Send(completed->second._body, response_id);
				return;
			}
			response.set_error(message::Error_Json);
		} else
		{
			response.set_error(msg_id == ID_LEAVE_ROOM_REQ ? Leave(session, room_id) : Destroy(session, room_id));
		}
	}
	// 3. 两种响应字段编号相同但仍转换为各自类型，缓存仅保存成功终态。
	std::string response_body;
	if (msg_id == ID_DESTROY_ROOM_REQ)
	{
		message::DestroyRoomRsp destroy_response;
		destroy_response.set_error(response.error());
		destroy_response.set_room_id(room_id);
		destroy_response.set_request_id(request_id);
		destroy_response.SerializeToString(&response_body);
	} else response.SerializeToString(&response_body);
	if (response.error() == message::SUCCESS)
	{
		_completed.emplace(key, CompletedRequest{room_id, response_body, std::chrono::steady_clock::now()});
		_completion_order.push_back(key);
		PruneCompleted();
	}
	session->Send(response_body, response_id);
	LOG_INFO_CTX("RoomLifecycleHandler::Handle", "退出处理 uid=" << uid << " room_id=" << room_id << " error=" << response.error());
}

int RoomLifecycleHandler::Leave(std::shared_ptr<CSession> session, const std::string& room_id)
{
	// 1. 禁止旧请求清理当前其他房间；房主必须使用解散入口。
	auto bound_room = session->GetRoomLocked();
	if (bound_room && bound_room->GetRoomId() != room_id) return message::NotInRoom;
	auto room = RoomMgr::getInstance()->GetRoom(room_id);
	const int uid = session->GetUserId();
	if (room && room->IsOwner(uid)) return message::OwnerMustDestroyRoom;
	if (room && room->IsClosing()) return message::RoomClosing;
	if (room && room->HasMember(uid) && !room->ContainsSession(uid, session.get())) return message::NotInRoom;
	if (room && room->ContainsSession(uid, session.get())) room->BeginLeave(uid, session.get());
	// 2. 先确认 Redis 成员资格已移除；通信失败时保持退出中，重试仍使用原关联。
	if (!RedisMgr::getInstance()->RemoveUserFromRoom(room_id, std::to_string(uid))) return message::RedisErr;
	// 3. 已经离开按完成处理；实际移除才广播，避免重复通知。
	if (room && room->Leave(uid, session.get())) room->BroadcastUserLeave(uid);
	if (bound_room) session->ClearRoom(bound_room.get());
	return message::SUCCESS;
}

int RoomLifecycleHandler::Destroy(std::shared_ptr<CSession> session, const std::string& room_id)
{
	// 1. 重连后的房主可以直接继续解散，无需重新 Join 或回放历史。
	auto bound_room = session->GetRoomLocked();
	if (bound_room && bound_room->GetRoomId() != room_id) return message::NotInRoom;
	RoomInfo metadata;
	const int read_result = RedisMgr::getInstance()->ReadRoomInfo(room_id, metadata);
	if (read_result != message::SUCCESS && read_result != message::RoomNotExist) return read_result;
	auto room = RoomMgr::getInstance()->GetRoom(room_id);
	const int uid = session->GetUserId();
	if (read_result == message::SUCCESS)
	{
		if (metadata.owner_uid != uid) return message::NotOwner;
		std::string self_host = ConfigMgr::Inst()["SelfServer"]["Host"];
		if (self_host == "0.0.0.0") self_host = "127.0.0.1";
		if (metadata.host != self_host || metadata.port != std::stoi(ConfigMgr::Inst()["SelfServer"]["Port"])) return message::NotInRoom;
	}
	if (room && !room->IsOwner(uid)) return message::NotOwner;
	if (room) room->BeginClose();
	// 2. 缺失记录也执行幂等删除，Redis 失败时内存房间保持关闭中。
	const int delete_result = RedisMgr::getInstance()->DeleteRoom(room_id, uid);
	if (delete_result != message::SUCCESS) return delete_result;
	// 3. 一次清空状态，再使用快照发送结束通知，不逐个执行 Leave。
	if (room)
	{
		const auto members = room->FinishClose();
		RoomMgr::getInstance()->RemoveRoom(room_id, room.get());
		message::RoomDestroyedBroadcast notification;
		notification.set_room_id(room_id);
		notification.set_operator_uid(uid);
		notification.set_reason("房主已解散房间");
		std::string notification_body;
		if (!notification.SerializeToString(&notification_body)) return message::ServerInternalErr;
		for (const auto& member : members)
		{
			if (member && member != session && !member->IsClosed()) member->Send(notification_body, ID_ROOM_DESTROYED_BROADCAST);
		}
	}
	return message::SUCCESS;
}

void RoomLifecycleHandler::HandleDisconnected(std::shared_ptr<CSession> session)
{
	// 1. 只移除断开的具体连接，保留房主和房间元数据用于恢复。
	const int uid = session->GetUserId();
	SessionMgr::getInstance()->RemoveSession(uid, session.get());
	auto room = session->GetRoomLocked();
	if (!room || !room->Leave(uid, session.get())) return;
	// 2. 移除资源资格并通知仍在线成员，Redis 故障记录上下文，不静默吞掉。
	if (!RedisMgr::getInstance()->RemoveUserFromRoom(room->GetRoomId(), std::to_string(uid)))
		LOG_WARN_CTX("RoomLifecycleHandler::HandleDisconnected", "成员清理待重试 uid=" << uid << " room_id=" << room->GetRoomId());
	room->BroadcastUserLeave(uid);
}
