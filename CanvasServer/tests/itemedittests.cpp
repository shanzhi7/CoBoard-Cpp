/***********************************************************************************
* @file         itemedittests.cpp
* @brief        图元编辑版本、字段隔离、完整历史和旧协议兼容测试
* @author       shanzhi
* @date         2026/10/15
* @history
***********************************************************************************/
#include "CanvasServer/Room.h"
#include "CanvasServer/CSession.h"

#include <array>
#include <chrono>
#include <limits>
#include <stdexcept>

namespace
{
void CheckEdit(bool valid, const char* reason)
{
	// 1. 测试断言在 Release 构建中仍执行。
	if (!valid) throw std::runtime_error(reason);
}

void SendDraw(Room& room, const std::shared_ptr<CSession>& session, const std::string& id,
	message::ShapeType shape, message::DrawCmd command)
{
	// 1. 使用真实 START/MOVE/END 协议登记几何和笔迹历史。
	message::DrawReq request;
	request.set_uid(7);
	request.set_item_id(id);
	request.set_shape(shape);
	request.set_cmd(command);
	request.set_start_x(10);
	request.set_start_y(20);
	request.set_current_x(100);
	request.set_current_y(120);
	CheckEdit(room.AcceptDraw(session, request, request.SerializeAsString()), "draw rejected");
}

message::ItemEditOperation EditFor(Room& room, const std::string& id, const std::string& operation_id)
{
	// 1. 请求基于服务端最新完整状态构造，严格携带当前版本。
	message::ItemEditOperationRsp response;
	response.mutable_operation()->set_target_item_id(id);
	room.FillItemEditState(response);
	message::ItemEditOperation operation;
	operation.set_uid(7);
	operation.set_room_id("123456");
	operation.set_operation_id(operation_id);
	operation.set_target_item_id(id);
	operation.set_expected_revision(response.operation().revision());
	operation.set_fields(1);
	if (response.has_current_state()) *operation.mutable_state() = response.current_state();
	else operation.mutable_state()->set_item_id(id);
	return operation;
}

void TestEditStateAndHistory()
{
	// 1. 创建已完成矩形；文字与自动增长的几何由同一次编辑确认。
	boost::asio::io_context io;
	auto room = std::make_shared<Room>("123456");
	auto owner = std::make_shared<CSession>(io);
	owner->SetUserId(7);
	room->Join(owner);
	room->SetRoomInfo("edit tests", 7);
	SendDraw(*room, owner, "rect", message::SHAPE_RECT, message::CMD_START);
	auto unfinished = EditFor(*room, "rect", "unfinished");
	message::ItemEditOperationRsp response;
	CheckEdit(!room->ApplyItemEditOperation(unfinished, response), "unfinished shape edited");
	SendDraw(*room, owner, "rect", message::SHAPE_RECT, message::CMD_END);
	auto operation = EditFor(*room, "rect", "label");
	operation.set_fields(3);
	operation.mutable_state()->set_text(u8"第一行\n第二行");
	operation.mutable_state()->set_geometry(3, 180);
	CheckEdit(room->ApplyItemEditOperation(operation, response), "bound text edit failed");
	const auto first_revision = response.operation().revision();
	CheckEdit(response.current_state().text() == u8"第一行\n第二行" && response.current_state().geometry(3) == 180, "text and height not atomic");
	auto obsolete_session = std::make_shared<CSession>(io);
	obsolete_session->SetUserId(7);
	CheckEdit(!room->ApplyItemEditOperation(EditFor(*room, "rect", "obsolete-session"), response, obsolete_session.get()), "replaced session accepted edit");
	CheckEdit(room->ApplyItemEditOperation(operation, response) && response.operation().revision() == first_revision, "duplicate advanced revision");
	auto reused_id = operation;
	reused_id.mutable_state()->set_x(12);
	CheckEdit(!room->ApplyItemEditOperation(reused_id, response) && response.error() == message::ItemEditInvalidArgument, "operation id reused with different content");
	// 2. 几何更新忽略请求中的旧文字，冲突返回完整已确认状态。
	auto moved = EditFor(*room, "rect", "move");
	moved.mutable_state()->set_x(50);
	moved.mutable_state()->set_rotation(37);
	moved.mutable_state()->set_text("must not overwrite label");
	CheckEdit(room->ApplyItemEditOperation(moved, response), "move failed");
	CheckEdit(response.current_state().text() == u8"第一行\n第二行", "geometry overwrote label");
	operation.set_operation_id("stale-undo");
	CheckEdit(!room->ApplyItemEditOperation(operation, response) && response.error() == message::ItemEditConflict &&
		response.current_state().x() == 50, "stale undo accepted");
	auto invalid = EditFor(*room, "rect", "invalid");
	invalid.mutable_state()->set_rotation(std::numeric_limits<double>::infinity());
	CheckEdit(!room->ApplyItemEditOperation(invalid, response) && response.error() == message::ItemEditInvalidArgument, "nonfinite transform accepted");
	invalid = EditFor(*room, "rect", "no-permission");
	invalid.set_uid(8);
	CheckEdit(!room->ApplyItemEditOperation(invalid, response), "nonmember edited shape");
	// 3. 任意次拖动仅保留一个最终快照，基础 START/END 与最新状态顺序稳定。
	const auto history = room->GetHistorySnapshot();
	CheckEdit(history.size() == 3 && history[0]._msg_id == ID_DRAW_RSP && history[1]._msg_id == ID_DRAW_RSP &&
		history[2]._msg_id == ID_ITEM_EDIT_RSP, "edit history order or compaction wrong");
	message::ItemEditOperationRsp snapshot;
	CheckEdit(snapshot.ParseFromString(history[2]._body) && snapshot.current_state().rotation() == 37 &&
		snapshot.current_state().text() == u8"第一行\n第二行", "edit snapshot stale");
	SendDraw(*room, owner, "eraser", message::SHAPE_ERASER, message::CMD_START);
	SendDraw(*room, owner, "eraser", message::SHAPE_ERASER, message::CMD_END);
	CheckEdit(!room->ApplyItemEditOperation(EditFor(*room, "eraser", "eraser-edit"), response), "eraser accepted edit");
	room->ClearHistory();
	CheckEdit(!room->ApplyItemEditOperation(moved, response) && !response.has_current_state(), "clear kept editable state");
}

void TestLegacySharedRevision()
{
	// 1. 文字沿用旧创建和移动接口，通用旋转和旧内容修改共享同一版本。
	boost::asio::io_context io;
	auto room = std::make_shared<Room>("123456");
	auto owner = std::make_shared<CSession>(io);
	owner->SetUserId(7);
	room->Join(owner);
	room->SetRoomInfo("legacy edits", 7);
	message::TextOperation text;
	text.set_uid(7);
	text.set_room_id("123456");
	text.set_target_item_id("text");
	text.set_operation_type(message::TEXT_CREATE);
	text.mutable_item()->set_item_id("text");
	text.mutable_item()->set_text("original");
	text.mutable_item()->set_font_family("Microsoft YaHei");
	text.mutable_item()->set_font_size(24);
	text.mutable_item()->set_color(0xff000000);
	message::TextOperationRsp text_response;
	CheckEdit(room->ApplyTextOperation(text, text_response), "legacy text create failed");
	auto edit = EditFor(*room, "text", "text-rotate");
	CheckEdit(edit.expected_revision() == text_response.operation().server_sequence(), "text create revisions differ");
	edit.mutable_state()->set_rotation(30);
	edit.mutable_state()->set_geometry(0, 200);
	edit.mutable_state()->set_geometry(1, 80);
	message::ItemEditOperationRsp response;
	CheckEdit(room->ApplyItemEditOperation(edit, response), "text rotate failed");
	const auto edit_revision = response.operation().revision();
	text.set_operation_type(message::TEXT_UPDATE);
	text.mutable_item()->set_text("changed later");
	CheckEdit(room->ApplyTextOperation(text, text_response) && text_response.operation().server_sequence() > edit_revision, "legacy text did not advance shared version");
	edit = EditFor(*room, "text", "text-undo");
	CheckEdit(edit.state().rotation() == 30 && edit.state().text() == "changed later", "legacy text discarded layout");
	edit.set_expected_revision(edit_revision);
	CheckEdit(!room->ApplyItemEditOperation(edit, response), "legacy text update not protected from undo");
	// 2. 图片资源仍属于原类型存储，通用位置编辑推进其后续旧协议版本。
	message::ImageOperation image;
	image.set_uid(7);
	image.set_operation_type(message::IMAGE_CREATE);
	image.mutable_item()->set_item_id("image");
	image.mutable_item()->set_asset_id("asset");
	auto* transform = image.mutable_item()->mutable_transform();
	transform->set_width(320);
	transform->set_height(240);
	transform->set_scale_x(1);
	transform->set_scale_y(1);
	std::string body;
	CheckEdit(room->ApplyImageOperation(image, &body), "legacy image create failed");
	edit = EditFor(*room, "image", "image-move");
	CheckEdit(edit.expected_revision() == image.server_sequence(), "image create revisions differ");
	edit.mutable_state()->set_x(70);
	CheckEdit(room->ApplyItemEditOperation(edit, response), "generic image move failed");
	image.set_operation_type(message::IMAGE_UPDATE_TRANSFORM);
	image.mutable_item()->mutable_transform()->set_x(90);
	CheckEdit(room->ApplyImageOperation(image, &body) && image.server_sequence() > response.operation().revision(), "legacy image version regressed");
	edit = EditFor(*room, "image", "image-read");
	CheckEdit(edit.state().x() == 90, "legacy image state diverged");
}


void TestPreviewStateAndFinalCommit()
{
	// 1. 同一版本接受任意数量预览，规范状态、历史和幂等确认缓存都保持未提交基线。
	boost::asio::io_context io;
	auto room = std::make_shared<Room>("123456");
	auto owner = std::make_shared<CSession>(io);
	owner->SetUserId(7);
	room->Join(owner);
	room->SetRoomInfo("preview tests", 7);
	SendDraw(*room, owner, "rect", message::SHAPE_RECT, message::CMD_START);
	SendDraw(*room, owner, "rect", message::SHAPE_RECT, message::CMD_END);
	auto operation = EditFor(*room, "rect", "one-drag");
	const auto initial = operation.state();
	const auto base_revision = operation.expected_revision();
	const auto history_size = room->GetHistorySnapshot().size();
	operation.set_preview(true);
	message::ItemEditOperationRsp response;
	for (int index = 1; index <= 20; ++index)
	{
		operation.mutable_state()->set_x(index * 10);
		operation.mutable_state()->set_rotation(index * 3);
		CheckEdit(room->ApplyItemEditOperation(operation, response, owner.get()), "continuous preview rejected");
		CheckEdit(response.operation().preview() && response.operation().revision() == base_revision &&
			response.current_state().x() == index * 10 && response.confirmed_state().SerializeAsString() == initial.SerializeAsString(), "preview advanced baseline");
		CheckEdit(room->GetHistorySnapshot().size() == history_size, "preview persisted in history");
		const auto canonical = EditFor(*room, "rect", "read-preview");
		CheckEdit(canonical.expected_revision() == base_revision && canonical.state().SerializeAsString() == initial.SerializeAsString(), "preview changed canonical state");
	}

	// 2. 最终提交复用手势标识且只推进一次版本，重试幂等，旧版本预览不再被接受。
	operation.set_preview(false);
	CheckEdit(room->ApplyItemEditOperation(operation, response), "final commit failed after previews");
	const auto final_revision = response.operation().revision();
	CheckEdit(final_revision > base_revision && response.current_state().x() == 200 && !response.has_confirmed_state(), "final version not confirmed");
	CheckEdit(room->ApplyItemEditOperation(operation, response) && response.operation().revision() == final_revision, "final retry advanced version");
	CheckEdit(room->GetHistorySnapshot().size() == history_size + 1, "final history snapshot missing");
	operation.set_preview(true);
	CheckEdit(!room->ApplyItemEditOperation(operation, response) && response.error() == message::ItemEditConflict, "stale preview accepted");

	// 3. 拒绝非法几何、未授权和旧连接，撤权后仅允许广播取消所需的服务器基线。
	auto member = std::make_shared<CSession>(io);
	member->SetUserId(8);
	room->Join(member);
	operation = EditFor(*room, "rect", "member-preview");
	operation.set_uid(8);
	operation.set_preview(true);
	CheckEdit(!room->ApplyItemEditOperation(operation, response, member.get()), "readonly preview accepted");
	CheckEdit(room->GrantEdit(8), "preview permission grant failed");
	operation.mutable_state()->set_x(400);
	CheckEdit(room->ApplyItemEditOperation(operation, response, member.get()), "authorized preview rejected");
	operation.mutable_state()->set_rotation(std::numeric_limits<double>::infinity());
	CheckEdit(!room->ApplyItemEditOperation(operation, response), "invalid preview accepted");
	CheckEdit(room->RevokeEdit(8), "preview permission revoke failed");
	operation.set_cancel_preview(true);
	operation.clear_state();
	operation.set_fields(0);
	CheckEdit(room->ApplyItemEditOperation(operation, response, member.get()) && response.current_state().x() == 200 &&
		response.operation().revision() == final_revision, "revoked member could not cancel preview safely");
	operation.mutable_state()->set_x(999);
	CheckEdit(!room->ApplyItemEditOperation(operation, response), "cancel accepted client state");
}

std::vector<message::ItemEditOperationRsp> ReadEditResponses(boost::asio::ip::tcp::socket& socket)
{
	// 1. 仅读取已写完的 TCP 帧，过滤成员通知和绘画创建消息。
	std::vector<message::ItemEditOperationRsp> responses;
	while (socket.available() >= 4)
	{
		unsigned char header[4];
		boost::asio::read(socket, boost::asio::buffer(header));
		const int msg_id = header[0] * 256 + header[1];
		std::string body(header[2] * 256 + header[3], '\0');
		boost::asio::read(socket, boost::asio::buffer(&body[0], body.size()));
		if (msg_id != ID_ITEM_EDIT_RSP)
		{
			continue;
		}
		message::ItemEditOperationRsp response;
		CheckEdit(response.ParseFromString(body), "edit TCP response corrupt");
		responses.push_back(response);
	}
	return responses;
}

void TestPreviewWireFastPath()
{
	// 1. 启动真实会话接收，直接发 TCP 包验证快通道，测试不启动 LogicSystem 或 Redis。
	boost::asio::io_context io;
	boost::asio::ip::tcp::acceptor acceptor(io, {boost::asio::ip::address_v4::loopback(), 0});
	auto room = std::make_shared<Room>("123456");
	room->SetRoomInfo("preview wire", 7);
	auto connect_member = [&](int uid, boost::asio::ip::tcp::socket& socket) {
		auto session = std::make_shared<CSession>(io);
		socket.connect(acceptor.local_endpoint());
		socket.set_option(boost::asio::ip::tcp::no_delay(true));
		acceptor.accept(session->GetSocket());
		session->SetUserId(uid);
		room->Join(session);
		session->Start();
		return session;
	};
	auto flush = [&]() { io.restart(); io.run_for(std::chrono::milliseconds(25)); };
	boost::asio::ip::tcp::socket owner_socket(io), member_socket(io), newcomer_socket(io);
	auto owner = connect_member(7, owner_socket);
	auto member = connect_member(8, member_socket);
	SendDraw(*room, owner, "wire-rect", message::SHAPE_RECT, message::CMD_START);
	SendDraw(*room, owner, "wire-rect", message::SHAPE_RECT, message::CMD_END);
	flush();
	ReadEditResponses(owner_socket);
	ReadEditResponses(member_socket);
	auto send = [&](const message::ItemEditOperation& operation) {
		const auto body = operation.SerializeAsString();
		const unsigned char header[] = {static_cast<unsigned char>(ID_ITEM_EDIT_REQ >> 8), static_cast<unsigned char>(ID_ITEM_EDIT_REQ & 255),
			static_cast<unsigned char>(body.size() >> 8), static_cast<unsigned char>(body.size() & 255)};
		const std::array<boost::asio::const_buffer, 2> buffers = {boost::asio::buffer(header), boost::asio::buffer(body)};
		boost::asio::write(owner_socket, buffers);
		flush();
	};

	// 2. 预览只广播给其他成员，不产生发起者 ACK；入房历史不包含未提交的位置。
	auto operation = EditFor(*room, "wire-rect", "wire-drag");
	operation.set_preview(true);
	for (int index = 1; index <= 3; ++index)
	{
		operation.mutable_state()->set_x(index * 30);
		send(operation);
		CheckEdit(ReadEditResponses(owner_socket).empty(), "preview sent ACK to sender");
		const auto responses = ReadEditResponses(member_socket);
		CheckEdit(responses.size() == 1 && responses[0].operation().preview() && responses[0].current_state().x() == index * 30,
			"preview did not arrive through session fast path");
	}
	auto newcomer = connect_member(9, newcomer_socket);
	flush();
	CheckEdit(ReadEditResponses(newcomer_socket).empty(), "uncommitted preview leaked into join history");
	ReadEditResponses(owner_socket);
	ReadEditResponses(member_socket);

	// 3. 最终提交向所有成员发送同一确认版本，预览结束后只有一个规范快照。
	operation.set_preview(false);
	operation.mutable_state()->set_x(135);
	send(operation);
	const auto own = ReadEditResponses(owner_socket);
	const auto other = ReadEditResponses(member_socket);
	const auto joined = ReadEditResponses(newcomer_socket);
	CheckEdit(own.size() == 1 && other.size() == 1 && joined.size() == 1 && !own[0].operation().preview() &&
		own[0].operation().revision() > operation.expected_revision() && own[0].current_state().x() == 135, "final fast path confirmation missing");
	CheckEdit(own[0].SerializeAsString() == other[0].SerializeAsString() && own[0].SerializeAsString() == joined[0].SerializeAsString(), "members saw different final state");
}

void TestCompleteDrawPruning()
{
	// 1. 让一条完整笔迹跨越历史裁剪边界，不能留下孤立 MOVE 或 END。
	boost::asio::io_context io;
	auto room = std::make_shared<Room>("123456");
	auto owner = std::make_shared<CSession>(io);
	owner->SetUserId(7);
	room->Join(owner);
	room->SetRoomInfo("prune tests", 7);
	SendDraw(*room, owner, "pen", message::SHAPE_PEN, message::CMD_START);
	SendDraw(*room, owner, "pen", message::SHAPE_PEN, message::CMD_MOVE);
	SendDraw(*room, owner, "pen", message::SHAPE_PEN, message::CMD_END);
	for (int index = 0; index < 10001; ++index) room->AppendHistory("other");
	for (const auto& entry : room->GetHistorySnapshot()) CheckEdit(entry._draw_item_id != "pen", "partial pen survived pruning");
	message::ItemEditOperationRsp response;
	CheckEdit(!room->ApplyItemEditOperation(EditFor(*room, "pen", "after-prune"), response), "pruned target accepted edit");
}
}

void RunItemEditTests()
{
	// 1. 复用现有无外部服务测试入口，覆盖状态原子性、版本和历史。
	TestPreviewStateAndFinalCommit();
	TestPreviewWireFastPath();
	TestEditStateAndHistory();
	TestLegacySharedRevision();
	TestCompleteDrawPruning();
}
