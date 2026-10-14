#include "CanvasServer/TextOperationHandler.h"

#include <cmath>

#include "CanvasServer/CSession.h"
#include "CanvasServer/Room.h"
#include "Logger/Logger.h"

void TextOperationHandler::Handle(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data)
{
	// 1. 绑定会话身份与房间，解析失败不进入房间状态处理。
	(void)msg_id;
	if (!session || session->IsClosed()) return;
	message::TextOperation operation;
	if (!operation.ParseFromString(msg_data))
	{
		LOG_WARN_CTX("TextOperationHandler::Handle", "文字操作解析失败");
		return;
	}
	auto room = session->GetRoomLocked();
	if (!room || session->GetUserId() == 0 || operation.uid() != session->GetUserId() ||
		operation.room_id() != room->GetRoomId())
	{
		LOG_WARN_CTX("TextOperationHandler::Handle", "文字会话身份或房间不匹配 uid=" << session->GetUserId());
		return;
	}

	// 2. 只检查协议必要字段和统一内容限额，不引入外部字体或资源加载。
	const auto& item = operation.item();
	const bool is_content_operation = operation.operation_type() == message::TEXT_CREATE || operation.operation_type() == message::TEXT_UPDATE;
	const bool is_valid = !operation.operation_id().empty() && operation.operation_id().size() <= 128 &&
		!operation.target_item_id().empty() && operation.target_item_id().size() <= 128 &&
		operation.operation_type() >= message::TEXT_CREATE && operation.operation_type() <= message::TEXT_DELETE &&
		(operation.operation_type() == message::TEXT_DELETE || (operation.has_item() &&
		item.item_id() == operation.target_item_id() && std::isfinite(item.x()) && std::isfinite(item.y()))) &&
		(!is_content_operation || (!item.text().empty() && item.text().size() <= 16 * 1024 &&
		!item.font_family().empty() && item.font_family().size() <= 512 && item.font_size() >= 8 && item.font_size() <= 144));
	message::TextOperationRsp response;
	*response.mutable_operation() = operation;
	if (!is_valid || !room->CanEdit(session->GetUserId()))
	{
		response.set_error(is_valid ? message::NotOwner : message::TextInvalidArgument);
		response.set_error_message(is_valid ? "当前没有编辑权限" : "文字内容或样式无效");
		room->FillTextState(response);
	} else
	{
		room->ApplyTextOperation(operation, response);
	}

	// 3. 成功广播包括发起者，失败只回复发起者；房间状态锁在此时已释放。
	std::string body;
	if (!response.SerializeToString(&body))
	{
		LOG_WARN_CTX("TextOperationHandler::Handle", "文字响应序列化失败 uid=" << session->GetUserId());
		return;
	}
	if (response.error() == 0) room->Broadcast(body, ID_TEXT_OPERATION_RSP);
	else session->Send(body, ID_TEXT_OPERATION_RSP);
}
