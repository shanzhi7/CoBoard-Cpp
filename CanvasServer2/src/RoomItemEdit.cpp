#include "CanvasServer/Room.h"
#include "CanvasServer/CSession.h"

#include "Logger/Logger.h"

#include <algorithm>
#include <cmath>

namespace
{
message::ItemEditState MakeInitialState(const std::string& item_id, message::ItemKind kind)
{
	// 1. 公共变换使用显式单位矩阵，协议默认零不能解释为有效缩放。
	message::ItemEditState state;
	state.set_item_id(item_id);
	state.set_kind(kind);
	state.set_scale(1.0);
	for (int index = 0; index < 9; ++index)
	{
		state.add_matrix(index % 4 == 0 ? 1.0 : 0.0);
	}
	state.set_font_family("Microsoft YaHei");
	state.set_font_size(24);
	state.set_color(0xff000000U);
	return state;
}

bool IsBoundedNumber(double value)
{
	// 1. 拒绝非有限坐标和极端矩阵，保护客户端场景索引。
	return std::isfinite(value) && std::fabs(value) <= 1000000.0;
}

bool IsTextKind(message::ItemKind kind)
{
	// 1. 独立文字及三种封闭图元支持文本，开放路径不能绑定文字。
	return kind == message::ITEM_TEXT || kind == message::ITEM_RECT ||
		kind == message::ITEM_OVAL || kind == message::ITEM_DIAMOND;
}

bool ValidateEditState(const message::ItemEditState& state, std::uint32_t fields)
{
	// 1. 几何请求携带完整绝对变换，矩阵仅接受可逆仿射变换。
	if ((fields & 1U) != 0)
	{
		if (!IsBoundedNumber(state.x()) || !IsBoundedNumber(state.y()) ||
			!IsBoundedNumber(state.origin_x()) || !IsBoundedNumber(state.origin_y()) ||
			!IsBoundedNumber(state.rotation()) || !IsBoundedNumber(state.scale()) ||
			state.scale() <= 0.0 || state.matrix_size() != 9)
		{
			return false;
		}
		for (double value : state.matrix())
		{
			if (!IsBoundedNumber(value))
			{
				return false;
			}
		}
		if (std::fabs(state.matrix(2)) > 1e-9 || std::fabs(state.matrix(5)) > 1e-9 ||
			std::fabs(state.matrix(8) - 1.0) > 1e-9 ||
			std::fabs(state.matrix(0) * state.matrix(4) - state.matrix(1) * state.matrix(3)) < 1e-12)
		{
			return false;
		}
		for (double value : state.geometry())
		{
			if (!IsBoundedNumber(value))
			{
				return false;
			}
		}
		const auto kind = state.kind();
		const int expected_size = kind == message::ITEM_PEN ? 6 :
			(kind == message::ITEM_IMAGE || kind == message::ITEM_TEXT ? 2 : 4);
		if (state.geometry_size() != expected_size)
		{
			return false;
		}
		if (kind == message::ITEM_IMAGE && (state.geometry(0) <= 0.0 || state.geometry(1) <= 0.0))
		{
			return false;
		}
		// 文字零宽高表示沿用自动布局，移动和撤销必须保留该既有状态。
		if (kind == message::ITEM_TEXT && (state.geometry(0) < 0.0 || state.geometry(1) < 0.0))
		{
			return false;
		}
		if ((kind == message::ITEM_RECT || kind == message::ITEM_OVAL || kind == message::ITEM_DIAMOND) &&
			(state.geometry(2) < 0.0 || state.geometry(3) < 0.0))
		{
			return false;
		}
		if (kind == message::ITEM_PEN &&
			std::fabs(state.geometry(0) * state.geometry(3) - state.geometry(1) * state.geometry(2)) < 1e-12)
		{
			return false;
		}
	}
	// 2. 文本长度按 UTF-8 字节限制；空内容用于清除图形内标签。
	if ((fields & 2U) != 0)
	{
		if (!IsTextKind(state.kind()) || state.text().size() > 16 * 1024 ||
			state.font_family().empty() || state.font_family().size() > 512 ||
			state.font_size() < 8 || state.font_size() > 144)
		{
			return false;
		}
	}
	return true;
}

void CopyText(const message::ItemEditState& source, message::ItemEditState& target)
{
	// 1. 字段掩码隔离内容和几何，移动不能覆盖其他成员的新文本。
	target.set_text(source.text());
	target.set_font_family(source.font_family());
	target.set_font_size(source.font_size());
	target.set_color(source.color());
	target.set_bold(source.bold());
}
}

void Room::FillItemEditStateLocked(message::ItemEditOperationRsp& response)
{
	// 1. 缺失目标不构造空图元；墓碑版本仍用于客户端识别迟到请求。
	const std::string& item_id = response.operation().target_item_id();
	response.clear_current_state();
	const auto state = _item_edit_states.find(item_id);
	if (state != _item_edit_states.end())
	{
		*response.mutable_current_state() = state->second;
	}
	const auto revision = _item_revisions.find(item_id);
	response.mutable_operation()->set_revision(revision == _item_revisions.end() ? 0 : revision->second);
}

void Room::FillItemEditState(message::ItemEditOperationRsp& response)
{
	// 1. 失败确认与正常修改读取同一份受锁保护的状态。
	std::lock_guard<std::mutex> lock(_mutex);
	FillItemEditStateLocked(response);
}

void Room::ClearItemEditStateLocked()
{
	// 1. 清屏和房间销毁同时清除编辑、历史关联和重试缓存，房间总版本继续递增。
	_item_edit_states.clear();
	_item_revisions.clear();
	_draw_started.clear();
	_draw_end_index.clear();
	_edit_responses.clear();
	_edit_response_order.clear();
}

void Room::RebuildDrawHistoryIndexLocked()
{
	// 1. 只索引完整笔迹的结束消息；尚未完成的路径不能被编辑。
	_draw_end_index.clear();
	for (std::size_t index = 0; index < _history.size(); ++index)
	{
		const auto& entry = _history[index];
		if (entry._draw_item_id.empty())
		{
			continue;
		}
		message::DrawReq request;
		if (request.ParseFromString(entry._body) && request.cmd() == message::CMD_END)
		{
			_draw_end_index[entry._draw_item_id] = index;
		}
	}
}

bool Room::RegisterDrawLocked(const message::DrawReq& request)
{
	// 1. 限制稳定 ID 与坐标，重复 START 不能覆盖已存在或已完成的图元。
	const std::string& item_id = request.item_id();
	if (item_id.empty() || item_id.size() > 128 || request.shape() < message::SHAPE_PEN ||
		request.shape() > message::SHAPE_DIAMOND || !IsBoundedNumber(request.current_x()) ||
		!IsBoundedNumber(request.current_y()) || !IsBoundedNumber(request.start_x()) ||
		!IsBoundedNumber(request.start_y()))
	{
		return false;
	}
	for (const auto& point : request.path_points())
	{
		if (!IsBoundedNumber(point.x()) || !IsBoundedNumber(point.y()))
		{
			return false;
		}
	}
	if (request.cmd() == message::CMD_START)
	{
		if (_item_revisions.count(item_id) || _draw_started.count(item_id) || _draw_started.size() >= MAX_HISTORY_OPS)
		{
			return false;
		}
		_draw_started[item_id] = request;
		_draw_started[item_id].clear_path_points(); // 起点索引不复制整包路径点。
		return true;
	}
	const auto started = _draw_started.find(item_id);
	if (started == _draw_started.end() || started->second.uid() != request.uid() ||
		started->second.shape() != request.shape())
	{
		return false;
	}
	if (request.cmd() == message::CMD_MOVE)
	{
		return true;
	}
	if (request.cmd() != message::CMD_END)
	{
		return false;
	}
	// 2. 使用已接受 START 的起点构建几何，END 不能偷偷更换类型或起点。
	const auto initial = started->second;
	_draw_started.erase(started);
	_item_revisions[item_id] = 0;
	if (request.shape() == message::SHAPE_ERASER)
	{
		return true;
	}
	message::ItemEditState state = MakeInitialState(item_id, static_cast<message::ItemKind>(request.shape()));
	if (request.shape() == message::SHAPE_PEN)
	{
		for (double value : {1.0, 0.0, 0.0, 1.0, 0.0, 0.0})
		{
			state.add_geometry(value);
		}
	} else if (request.shape() == message::SHAPE_LINE || request.shape() == message::SHAPE_ARROW)
	{
		state.add_geometry(initial.start_x());
		state.add_geometry(initial.start_y());
		state.add_geometry(request.current_x());
		state.add_geometry(request.current_y());
	} else
	{
		state.add_geometry(std::min(initial.start_x(), request.current_x()));
		state.add_geometry(std::min(initial.start_y(), request.current_y()));
		state.add_geometry(std::fabs(request.current_x() - initial.start_x()));
		state.add_geometry(std::fabs(request.current_y() - initial.start_y()));
	}
	_item_edit_states[item_id] = std::move(state);
	return true;
}

void Room::UpdateItemEditHistoryLocked(const std::string& item_id)
{
	// 1. 编辑快照附着于图元原创建位置，历史不会为每个拖动采样持续增长。
	const auto state = _item_edit_states.find(item_id);
	if (state == _item_edit_states.end())
	{
		return;
	}
	std::size_t index = _history.size();
	if (state->second.kind() == message::ITEM_IMAGE)
	{
		const auto found = _image_create_index.find(item_id);
		if (found != _image_create_index.end())
		{
			index = found->second;
		}
	} else if (state->second.kind() == message::ITEM_TEXT)
	{
		const auto found = _text_create_indexMap.find(item_id);
		if (found != _text_create_indexMap.end())
		{
			index = found->second;
		}
	} else
	{
		const auto found = _draw_end_index.find(item_id);
		if (found != _draw_end_index.end())
		{
			index = found->second;
		}
	}
	// 2. 图片和文字保留规范状态，创建记录被裁剪后可重新注入完整创建消息。
	if (index >= _history.size())
	{
		std::string body;
		if (state->second.kind() == message::ITEM_IMAGE && _image_items.count(item_id))
		{
			message::ImageOperation creation;
			creation.set_room_id(_room_id);
			creation.set_operation_type(message::IMAGE_CREATE);
			creation.set_target_item_id(item_id);
			creation.set_server_sequence(_item_revisions[item_id]);
			*creation.mutable_item() = _image_items.at(item_id);
			creation.SerializeToString(&body);
			AppendHistoryLocked(HistoryEntry{ID_IMAGE_OPERATION_RSP, body, item_id});
			index = _history.size() - 1;
			_image_create_index[item_id] = index;
		} else if (state->second.kind() == message::ITEM_TEXT && _text_itemMap.count(item_id))
		{
			message::TextOperationRsp creation;
			creation.mutable_operation()->set_room_id(_room_id);
			creation.mutable_operation()->set_operation_type(message::TEXT_CREATE);
			creation.mutable_operation()->set_target_item_id(item_id);
			creation.mutable_operation()->set_server_sequence(_item_revisions[item_id]);
			*creation.mutable_current_item() = _text_itemMap.at(item_id);
			creation.SerializeToString(&body);
			AppendHistoryLocked(HistoryEntry{ID_TEXT_OPERATION_RSP, body, std::string(), item_id});
			index = _history.size() - 1;
			_text_create_indexMap[item_id] = index;
		} else
		{
			return;
		}
	}
	// 3. 回放先恢复基础图元，再用完整编辑状态恢复旋转、标签与文字布局。
	message::ItemEditOperationRsp snapshot;
	snapshot.mutable_operation()->set_room_id(_room_id);
	snapshot.mutable_operation()->set_target_item_id(item_id);
	snapshot.mutable_operation()->set_revision(_item_revisions[item_id]);
	snapshot.mutable_operation()->set_fields(IsTextKind(state->second.kind()) ? 3 : 1);
	*snapshot.mutable_current_state() = state->second;
	snapshot.SerializeToString(&_history[index]._edit_body);
}

void Room::SyncLegacyTextLocked(const message::TextOperation& operation, const message::TextItem& text, std::uint64_t revision)
{
	// 1. 旧文字操作也推进共享版本，通用撤销不能越过已确认的内容修改。
	const std::string& item_id = operation.target_item_id();
	_edit_sequence = std::max(_edit_sequence, revision);
	_item_revisions[item_id] = revision;
	if (operation.operation_type() == message::TEXT_DELETE)
	{
		_item_edit_states.erase(item_id);
		return;
	}
	const bool had_state = _item_edit_states.count(item_id) != 0;
	auto state = had_state ? _item_edit_states.at(item_id) : MakeInitialState(item_id, message::ITEM_TEXT);
	if (!had_state)
	{
		state.add_geometry(0.0); // 旧创建消息没有布局尺寸，由客户端按原生文字自动测量。
		state.add_geometry(0.0);
	}
	state.set_x(text.x());
	state.set_y(text.y());
	state.set_text(text.text());
	state.set_font_family(text.font_family());
	state.set_font_size(text.font_size());
	state.set_color(text.color());
	state.set_bold(text.bold());
	_item_edit_states[item_id] = state;
	// 2. 已有通用扩展快照时原位更新；普通旧文字创建仍只保存原有创建消息。
	const auto index = _text_create_indexMap.find(item_id);
	if (index != _text_create_indexMap.end() && !_history[index->second]._edit_body.empty())
	{
		UpdateItemEditHistoryLocked(item_id);
	}
}

void Room::SyncLegacyImageLocked(const message::ImageOperation& operation)
{
	// 1. 旧图片操作与通用编辑共享目标版本，删除后保留版本墓碑。
	const std::string& item_id = operation.target_item_id();
	_edit_sequence = std::max(_edit_sequence, operation.server_sequence());
	_item_revisions[item_id] = operation.server_sequence();
	if (operation.operation_type() == message::IMAGE_DELETE)
	{
		_item_edit_states.erase(item_id);
		return;
	}
	const bool had_state = _item_edit_states.count(item_id) != 0;
	auto state = had_state ? _item_edit_states.at(item_id) : MakeInitialState(item_id, message::ITEM_IMAGE);
	const auto& transform = operation.item().transform();
	state.set_x(transform.x());
	state.set_y(transform.y());
	state.set_rotation(transform.rotation());
	state.set_scale(1.0);
	state.clear_geometry();
	state.add_geometry(transform.width());
	state.add_geometry(transform.height());
	state.set_matrix(0, transform.scale_x());
	state.set_matrix(4, transform.scale_y());
	_item_edit_states[item_id] = state;
	// 2. 如果存在扩展变换，更新附着快照避免回放旧版本覆盖新位置。
	const auto index = _image_create_index.find(item_id);
	if (index != _image_create_index.end() && !_history[index->second]._edit_body.empty())
	{
		UpdateItemEditHistoryLocked(item_id);
	}
}

bool Room::ApplyItemEditOperation(const message::ItemEditOperation& operation, message::ItemEditOperationRsp& response, const CSession* expected_session)
{
	// 1. 直接调用与网络快通道复用同一状态机，测试和兼容入口均保留原子校验。
	std::lock_guard<std::mutex> lock(_mutex);
	return ApplyItemEditOperationLocked(operation, response, expected_session);
}

bool Room::AcceptItemEdit(std::shared_ptr<CSession> session, const message::ItemEditOperation& operation)
{
	// 1. 具体会话校验、状态更新和消息入队共用房间锁，保证预览不会越过最终提交。
	if (!session || session->IsClosed() || operation.uid() != session->GetUserId())
	{
		return false;
	}
	std::lock_guard<std::mutex> lock(_mutex);
	message::ItemEditOperationRsp response;
	ApplyItemEditOperationLocked(operation, response, session.get());
	std::string body;
	if (!response.SerializeToString(&body) || body.size() > 65535)
	{
		LOG_WARN_CTX("Room::AcceptItemEdit", "图元编辑响应编码失败 room_id=" << _room_id << " uid=" << operation.uid());
		return false;
	}

	// 2. 失败只回复发起者；预览不回 ACK，最终状态包含发起者以确认版本和撤销。
	if (response.error() != 0)
	{
		session->Send(body, ID_ITEM_EDIT_RSP);
		return false;
	}
	for (const auto& member : _sessions)
	{
		if ((!operation.preview() || member.first != operation.uid()) && member.second && !member.second->IsClosed())
		{
			// Send 只入队，实际网络写入由 IO executor 执行，与 AcceptDraw 保持相同顺序约束。
			member.second->Send(body, ID_ITEM_EDIT_RSP);
		}
	}
	return true;
}

bool Room::ApplyItemEditOperationLocked(const message::ItemEditOperation& operation, message::ItemEditOperationRsp& response, const CSession* expected_session)
{
	// 1. 房间关闭、旧会话或已退出成员不能发送任何编辑消息。
	response.Clear();
	*response.mutable_operation() = operation;
	auto reject = [&](int error, const char* reason) {
		response.set_error(error);
		response.set_error_message(reason);
		response.mutable_operation()->clear_state(); // 失败只保留关联字段，不能回显超长非法草稿。
		FillItemEditStateLocked(response);
		return false;
	};
	if (!IsActiveMemberLocked(operation.uid()) || operation.room_id() != _room_id ||
		(expected_session && _sessions.at(operation.uid()).get() != expected_session))
	{
		return reject(_is_closing ? message::RoomClosing : message::NotInRoom, "当前不在活动房间中");
	}
	// 2. 取消消息只广播服务端已提交状态，不接受客户端几何；撤权后仍能清除自己的预览。
	if (operation.cancel_preview())
	{
		if (!operation.preview() || operation.operation_id().empty() || operation.operation_id().size() > 128 ||
			operation.target_item_id().empty() || operation.target_item_id().size() > 128 ||
			operation.has_state() || operation.fields() != 0 || operation.revision() != 0 || operation.ByteSizeLong() > 65535)
		{
			return reject(message::ItemEditInvalidArgument, "图元预览取消字段无效");
		}
		FillItemEditStateLocked(response);
		return true;
	}
	if (operation.uid() != _owner_uid && !_editable_users.count(operation.uid()))
	{
		return reject(message::NotOwner, "当前没有编辑权限");
	}
	if (operation.operation_id().empty() || operation.operation_id().size() > 128 ||
		operation.target_item_id().empty() || operation.target_item_id().size() > 128 ||
		!operation.has_state() || operation.state().item_id() != operation.target_item_id() ||
		operation.revision() != 0 || operation.fields() == 0 || (operation.fields() & ~3U) != 0 ||
		operation.ByteSizeLong() > 65535)
	{
		return reject(message::ItemEditInvalidArgument, "图元编辑字段无效");
	}
	const std::string cache_key = std::to_string(operation.uid()) + ":" + operation.operation_id();
	const auto duplicate = _edit_responses.find(cache_key);
	if (!operation.preview() && duplicate != _edit_responses.end())
	{
		auto previous = duplicate->second.operation();
		previous.set_revision(0);
		if (previous.SerializeAsString() != operation.SerializeAsString())
		{
			return reject(message::ItemEditInvalidArgument, "重复操作标识对应不同内容");
		}
		response = duplicate->second;
		return true;
	}
	const std::string& item_id = operation.target_item_id();
	const auto current = _item_edit_states.find(item_id);
	if (current == _item_edit_states.end() || operation.expected_revision() != _item_revisions[item_id])
	{
		return reject(message::ItemEditConflict, "图元已由其他成员修改或已不存在");
	}
	if (operation.state().kind() != current->second.kind() || current->second.kind() == message::ITEM_ERASER ||
		!ValidateEditState(operation.state(), operation.fields()))
	{
		return reject(message::ItemEditInvalidArgument, "图元几何、文字或样式无效");
	}
	// 3. 将明确修改的字段合并至最新状态，文字及自动增高在同一次请求原子生效。
	message::ItemEditState state = current->second;
	if ((operation.fields() & 1U) != 0)
	{
		state = operation.state();
		CopyText(current->second, state);
	}
	if ((operation.fields() & 2U) != 0)
	{
		CopyText(operation.state(), state);
	}
	const std::uint64_t revision = operation.preview() ? _item_revisions[item_id] : _edit_sequence + 1;
	response.mutable_operation()->set_revision(revision);
	*response.mutable_current_state() = state;
	if (operation.preview())
	{
		*response.mutable_confirmed_state() = current->second;
	}
	if (response.ByteSizeLong() > 65535)
	{
		return reject(message::ItemEditInvalidArgument, "图元编辑响应超过协议包长");
	}
	// 4. 预览仅供远端显示，不修改版本、历史或幂等缓存，也不要求逐帧确认。
	if (operation.preview())
	{
		return true;
	}

	// 5. 最终提交同步旧类型字段并只分配一次版本，撤销基于该确认结果。
	if (state.kind() == message::ITEM_TEXT)
	{
		auto& text = _text_itemMap.at(item_id);
		text.set_x(state.x());
		text.set_y(state.y());
		text.set_text(state.text());
		text.set_font_family(state.font_family());
		text.set_font_size(state.font_size());
		text.set_color(state.color());
		text.set_bold(state.bold());
		_text_sequenceMap[item_id] = revision;
		_text_sequence = std::max(_text_sequence, revision);
	} else if (state.kind() == message::ITEM_IMAGE)
	{
		auto* transform = _image_items.at(item_id).mutable_transform();
		transform->set_x(static_cast<float>(state.x()));
		transform->set_y(static_cast<float>(state.y()));
		transform->set_width(static_cast<float>(state.geometry(0)));
		transform->set_height(static_cast<float>(state.geometry(1)));
		transform->set_rotation(static_cast<float>(state.rotation()));
	}
	_edit_sequence = revision;
	_item_revisions[item_id] = revision;
	_item_edit_states[item_id] = state;
	UpdateItemEditHistoryLocked(item_id);
	// 6. 有界缓存仅记录最终成功请求，重试不会再次推进版本或覆盖后续编辑。
	_edit_responses[cache_key] = response;
	_edit_response_order.push_back(cache_key);
	if (_edit_response_order.size() > 512)
	{
		_edit_responses.erase(_edit_response_order.front());
		_edit_response_order.pop_front();
	}
	return true;
}
