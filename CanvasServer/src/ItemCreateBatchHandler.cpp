#include "CanvasServer/ItemCreateBatchHandler.h"

#include "CanvasServer/CSession.h"
#include "CanvasServer/Room.h"
#include "Logger/Logger.h"

void ItemCreateBatchHandler::Handle(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data)
{
	// 1. 解析有上限的 protobuf 正文，避免异常载荷进入房间状态机。
	(void)msg_id;
	if (!session || session->IsClosed() || msg_data.size() > MAX_LENGTH)
	{
		return;
	}
	message::ItemCreateBatch batch;
	if (!batch.ParseFromString(msg_data))
	{
		LOG_WARN_CTX("ItemCreateBatchHandler::Handle", "图元批量创建解析失败 uid=" << session->GetUserId());
		return;
	}

	// 2. 身份与房间均以 TCP 会话绑定关系为准，不能相信客户端字段。
	auto room = session->GetRoomLocked();
	if (!room || batch.uid() != session->GetUserId() || batch.room_id() != room->GetRoomId() ||
		!room->ContainsSession(batch.uid(), session.get()))
	{
		LOG_WARN_CTX("ItemCreateBatchHandler::Handle", "图元批量创建身份或房间不匹配 uid=" << session->GetUserId());
		return;
	}

	// 3. 房间在单一锁中执行整批校验、历史写入和广播入队。
	room->AcceptItemCreateBatch(std::move(session), batch);
}
