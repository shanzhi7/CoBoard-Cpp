#include "CanvasServer/ItemEditOperationHandler.h"

#include "CanvasServer/CSession.h"
#include "CanvasServer/Room.h"
#include "Logger/Logger.h"

void ItemEditOperationHandler::Handle(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data)
{
	// 1. 校验 TCP 绑定身份和具体房间会话，不能借用其他成员 UID 提交。
	(void)msg_id;
	if (!session || session->IsClosed())
	{
		return;
	}
	message::ItemEditOperation operation;
	if (msg_data.size() > 65535 || !operation.ParseFromString(msg_data))
	{
		LOG_WARN_CTX("ItemEditOperationHandler::Handle", "图元编辑解析失败 uid=" << session->GetUserId());
		return;
	}
	auto room = session->GetRoomLocked();
	if (!room || operation.uid() == 0 || operation.uid() != session->GetUserId() ||
		operation.room_id() != room->GetRoomId() || !room->ContainsSession(operation.uid(), session.get()))
	{
		LOG_WARN_CTX("ItemEditOperationHandler::Handle", "图元编辑身份或房间不匹配 uid=" << session->GetUserId());
		return;
	}
	// 2. 房间锁内统一接受预览和最终提交，保持快通道与历史回放的消息顺序。
	room->AcceptItemEdit(std::move(session), operation);
}
