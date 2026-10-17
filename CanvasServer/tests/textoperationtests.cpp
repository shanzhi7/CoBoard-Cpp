/***********************************************************************************
* @file         textoperationtests.cpp
* @brief        文字操作字段隔离、版本冲突和房间历史回放测试
* @author       shanzhi
* @date         2026/10/08
* @history
***********************************************************************************/
#include "CanvasServer/Room.h"
#include "CanvasServer/CSession.h"
#include "CanvasServer/TextOperationHandler.h"

#include <iostream>
#include <stdexcept>

void RunItemEditTests(); // 验证通用图元编辑和共享版本。

namespace
{
void Check(bool is_valid, const char* message)
{
	// 1. 所有构建配置均执行检查，不依赖可能被禁用的 assert。
	if (!is_valid) throw std::runtime_error(message);
}

message::TextOperation MakeOperation(const std::string& item_id, message::TextOperationType type)
{
	// 1. 准备与客户端相同的完整文字请求数据。
	message::TextOperation operation;
	operation.set_uid(7);
	operation.set_room_id("123456");
	operation.set_operation_id("test-operation");
	operation.set_target_item_id(item_id);
	operation.set_operation_type(type);
	auto* item = operation.mutable_item();
	item->set_item_id(item_id);
	item->set_text("original");
	item->set_font_family("Microsoft YaHei");
	item->set_font_size(24);
	item->set_color(0xff000000);
	item->set_x(10);
	item->set_y(20);
	return operation;
}

void TestTextStateAndHistory()
{
	// 1. 创建顺序穿插笔画，内容和位置操作不能互相覆盖。
	boost::asio::io_context io_context;
	auto room_owner = std::make_shared<Room>("123456");
	Room& room = *room_owner;
	auto owner_session = std::make_shared<CSession>(io_context);
	owner_session->SetUserId(7);
	room.Join(owner_session);
	room.SetRoomInfo("text test", 7);
	room.AppendHistory("first-pen");
	auto operation = MakeOperation("first-text", message::TEXT_CREATE);
	message::TextOperationRsp response;
	Check(room.ApplyTextOperation(operation, response), "create failed");
	Check(response.operation().server_sequence() == 1, "create sequence");
	room.AppendHistory("last-pen");
	operation.set_operation_type(message::TEXT_MOVE);
	operation.mutable_item()->set_x(100);
	operation.mutable_item()->set_y(120);
	operation.mutable_item()->set_text("must not replace content");
	Check(room.ApplyTextOperation(operation, response), "move failed");
	Check(response.current_item().text() == "original", "move changed content");
	operation.set_operation_type(message::TEXT_UPDATE);
	operation.mutable_item()->set_text(u8"中文\nsecond line");
	operation.mutable_item()->set_font_size(48);
	operation.mutable_item()->set_bold(true);
	operation.mutable_item()->set_x(999);
	Check(room.ApplyTextOperation(operation, response), "update failed");
	Check(response.current_item().x() == 100 && response.current_item().y() == 120, "update changed position");
	const auto history = room.GetHistorySnapshot();
	Check(history.size() == 3 && history[0]._body == "first-pen" && history[2]._body == "last-pen", "creation order changed");
	message::TextOperationRsp snapshot;
	Check(snapshot.ParseFromString(history[1]._body), "history parse failed");
	Check(snapshot.operation().operation_type() == message::TEXT_CREATE, "history not a create snapshot");
	Check(snapshot.current_item().text() == u8"中文\nsecond line" && snapshot.current_item().x() == 100, "history stale");

	// 2. 撤销版本冲突只返回最新状态，不能提交旧内容或推进版本。
	operation.set_expected_sequence(1);
	operation.mutable_item()->set_text("stale undo");
	Check(!room.ApplyTextOperation(operation, response), "stale undo accepted");
	Check(response.error() == message::TextStateConflict && response.operation().server_sequence() == 3, "conflict sequence missing");
	Check(response.current_item().text() == u8"中文\nsecond line", "conflict state missing");
	operation.set_expected_sequence(3);
	operation.mutable_item()->set_text("original");
	Check(room.ApplyTextOperation(operation, response), "valid undo rejected");
	Check(response.operation().server_sequence() == 4 && response.current_item().x() == 100, "undo changed position");
	operation.set_uid(8);
	operation.set_expected_sequence(0);
	Check(!room.ApplyTextOperation(operation, response), "unauthorized update accepted");
	Check(response.operation().server_sequence() == 4, "failure changed sequence");

	// 3. 删除保留目标版本并进入历史，迟到修改及清屏后旧操作不能复活文字。
	operation.set_uid(7);
	operation.set_operation_type(message::TEXT_DELETE);
	Check(room.ApplyTextOperation(operation, response), "delete failed");
	Check(!response.has_current_item() && response.operation().server_sequence() == 5, "delete state wrong");
	Check(room.GetHistorySnapshot().size() == 4, "delete not in history");
	operation.set_operation_type(message::TEXT_UPDATE);
	Check(!room.ApplyTextOperation(operation, response) && !response.has_current_item(), "deleted text resurrected");
	Check(response.operation().server_sequence() == 5, "deleted version missing");
	room.ClearHistory();
	Check(room.GetHistorySnapshot().empty(), "clear left history");
	Check(!room.ApplyTextOperation(operation, response) && !response.has_current_item(), "clear left state");
	operation.set_operation_type(message::TEXT_CREATE);
	Check(room.ApplyTextOperation(operation, response) && response.operation().server_sequence() == 6, "clear reset total sequence");
}

void TestHistoryTrimming()
{
	// 1. 最早创建条目被裁剪后仍能更新，后段文字索引必须指向正确的新下标。
	boost::asio::io_context io_context;
	auto room_owner = std::make_shared<Room>("123456");
	Room& room = *room_owner;
	auto owner_session = std::make_shared<CSession>(io_context);
	owner_session->SetUserId(7);
	room.Join(owner_session);
	room.SetRoomInfo("trim test", 7);
	message::TextOperationRsp response;
	auto early = MakeOperation("early-text", message::TEXT_CREATE);
	Check(room.ApplyTextOperation(early, response), "early create failed");
	for (int index = 0; index < 9500; ++index) room.AppendHistory("pen");
	auto late = MakeOperation("late-text", message::TEXT_CREATE);
	Check(room.ApplyTextOperation(late, response), "late create failed");
	for (int index = 0; index < 600; ++index) room.AppendHistory("pen");
	late.set_operation_type(message::TEXT_UPDATE);
	late.mutable_item()->set_text("late updated");
	Check(room.ApplyTextOperation(late, response), "late update failed");
	early.set_operation_type(message::TEXT_MOVE);
	early.mutable_item()->set_x(250);
	Check(room.ApplyTextOperation(early, response), "trimmed update failed");

	// 2. 检查两个当前文字都有完整快照，且没有误写笔画正文。
	int text_count = 0;
	for (const auto& entry : room.GetHistorySnapshot())
	{
		if (entry._msg_id != ID_TEXT_OPERATION_RSP) continue;
		message::TextOperationRsp snapshot;
		Check(snapshot.ParseFromString(entry._body) && snapshot.has_current_item(), "trimmed history corrupt");
		if (snapshot.current_item().item_id() == "late-text") Check(snapshot.current_item().text() == "late updated", "late index wrong");
		else if (snapshot.current_item().item_id() == "early-text") Check(snapshot.current_item().x() == 250, "trimmed state stale");
		else Check(false, "unexpected history item");
		++text_count;
	}
	Check(text_count == 2, "text snapshot missing after trimming");
}

std::vector<message::TextOperationRsp> ReadResponses(boost::asio::ip::tcp::socket& socket)
{
	// 1. 写入队列已由 io_context 完全处理，仅读取本轮已发送的 TCP 包。
	std::vector<message::TextOperationRsp> responses;
	while (socket.available() >= 4)
	{
		unsigned char header[4];
		boost::asio::read(socket, boost::asio::buffer(header));
		const int msg_id = header[0] * 256 + header[1];
		const int body_size = header[2] * 256 + header[3];
		std::string body(body_size, '\0');
		boost::asio::read(socket, boost::asio::buffer(&body[0], body.size()));
		if (msg_id != ID_TEXT_OPERATION_RSP) continue;
		message::TextOperationRsp response;
		Check(response.ParseFromString(body), "TCP response corrupt");
		responses.push_back(response);
	}
	return responses;
}

void TestHandlerBroadcastAndLimits()
{
	// 1. 建立本机真实 TCP 会话，复用生产发送队列，测试不启动账号和 Redis 服务。
	boost::asio::io_context io_context;
	boost::asio::ip::tcp::acceptor acceptor(io_context, {boost::asio::ip::address_v4::loopback(), 0});
	auto room = std::make_shared<Room>("123456");
	room->SetRoomInfo("wire test", 7);
	auto connect_member = [&](int uid, boost::asio::ip::tcp::socket& socket) {
		auto session = std::make_shared<CSession>(io_context);
		socket.connect(acceptor.local_endpoint());
		acceptor.accept(session->GetSocket());
		session->SetUserId(uid);
		room->Join(session);
		return session;
	};
	auto flush = [&]() { io_context.restart(); io_context.run(); };
	boost::asio::ip::tcp::socket owner_socket(io_context), member_socket(io_context), newcomer_socket(io_context);
	auto owner = connect_member(7, owner_socket);
	auto member = connect_member(8, member_socket);
	flush();
	ReadResponses(owner_socket);
	ReadResponses(member_socket);
	auto submit = [&](const std::shared_ptr<CSession>& session, const message::TextOperation& operation) {
		std::string body;
		Check(operation.SerializeToString(&body), "request serialization failed");
		TextOperationHandler::Handle(session, ID_TEXT_OPERATION_REQ, body);
		flush();
	};
	auto operation = MakeOperation("wire-text", message::TEXT_CREATE);
	submit(owner, operation);
	auto owner_responses = ReadResponses(owner_socket);
	auto member_responses = ReadResponses(member_socket);
	Check(owner_responses.size() == 1 && member_responses.size() == 1, "create not broadcast to sender and member");
	Check(owner_responses[0].error() == 0 && owner_responses[0].operation().server_sequence() == 1, "create confirmation wrong");

	// 2. 权限失败只回复请求者；授权后的修改广播，新成员收到最新创建快照。
	operation.set_uid(8);
	operation.set_operation_type(message::TEXT_UPDATE);
	operation.mutable_item()->set_text(u8"多人文字");
	submit(member, operation);
	Check(ReadResponses(owner_socket).empty(), "failure broadcast to others");
	member_responses = ReadResponses(member_socket);
	Check(member_responses.size() == 1 && member_responses[0].error() == message::NotOwner, "permission failure missing");
	Check(room->GrantEdit(8), "grant edit failed");
	submit(member, operation);
	owner_responses = ReadResponses(owner_socket);
	member_responses = ReadResponses(member_socket);
	Check(owner_responses.size() == 1 && member_responses.size() == 1 && owner_responses[0].current_item().text() == u8"多人文字", "authorized update missing");
	auto newcomer = connect_member(9, newcomer_socket);
	flush();
	auto newcomer_responses = ReadResponses(newcomer_socket);
	Check(newcomer_responses.size() == 1 && newcomer_responses[0].current_item().text() == u8"多人文字", "new member got stale text");
	Check(newcomer_responses[0].operation().operation_type() == message::TEXT_CREATE, "new member got update without create");
	ReadResponses(owner_socket);
	ReadResponses(member_socket);

	// 3. 内容限制以 UTF-8 原始字节计算，版本失败不改变当前文字。
	operation.set_uid(7);
	operation.set_expected_sequence(1);
	submit(owner, operation);
	owner_responses = ReadResponses(owner_socket);
	Check(owner_responses.size() == 1 && owner_responses[0].error() == message::TextStateConflict, "stale wire undo accepted");
	Check(owner_responses[0].operation().server_sequence() == 2 && ReadResponses(member_socket).empty(), "undo conflict changed state");
	operation.set_expected_sequence(0);
	operation.mutable_item()->set_text(std::string(16 * 1024 + 1, 'x'));
	submit(owner, operation);
	owner_responses = ReadResponses(owner_socket);
	Check(owner_responses.size() == 1 && owner_responses[0].error() == message::TextInvalidArgument, "oversized text accepted");
	operation.mutable_item()->set_text(std::string(16 * 1024, 'x'));
	submit(owner, operation);
	owner_responses = ReadResponses(owner_socket);
	member_responses = ReadResponses(member_socket);
	newcomer_responses = ReadResponses(newcomer_socket);
	Check(owner_responses.size() == 1 && member_responses.size() == 1 && newcomer_responses.size() == 1, "boundary text not broadcast");
	Check(owner_responses[0].error() == 0 && owner_responses[0].current_item().text().size() == 16 * 1024, "boundary text rejected");

	// 4. 删除广播包含发起者，后续旧内容操作只返回不存在状态。
	operation.set_operation_type(message::TEXT_DELETE);
	submit(owner, operation);
	owner_responses = ReadResponses(owner_socket);
	member_responses = ReadResponses(member_socket);
	newcomer_responses = ReadResponses(newcomer_socket);
	Check(owner_responses.size() == 1 && member_responses.size() == 1 && newcomer_responses.size() == 1, "delete not broadcast");
	Check(!owner_responses[0].has_current_item(), "delete kept current state");
	operation.set_operation_type(message::TEXT_UPDATE);
	submit(owner, operation);
	owner_responses = ReadResponses(owner_socket);
	Check(owner_responses.size() == 1 && owner_responses[0].error() != 0 && !owner_responses[0].has_current_item(), "deleted wire text resurrected");
}
}

int main()
{
	// 1. 直接验证房间状态机，无需启动 Redis 或正式业务服务。
	try
	{
		RunItemEditTests();
		TestTextStateAndHistory();
		TestHistoryTrimming();
		TestHandlerBroadcastAndLimits();
		std::cout << "TextOperationTests passed" << std::endl;
		return 0;
	} catch (const std::exception& error)
	{
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
