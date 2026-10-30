#include "CanvasServer/Room.h"
#include "CanvasServer/CSession.h"

#include "Logger/Logger.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <json/json.h>

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
	_batch_responses.clear();
	_batch_response_order.clear();
	_batch_create_index.clear();
}

namespace
{
bool ReadFiniteJsonNumber(const Json::Value& value, double* result)
{
	// 1. 只接受 JSON 数值和有限场景坐标，避免客户端 JSON 把 NaN 隐式转换为零。
	if (!value.isNumeric() || !std::isfinite(value.asDouble()) || std::fabs(value.asDouble()) > 1000000.0)
	{
		return false;
	}
	*result = value.asDouble();
	return true;
}

bool ReadJsonPair(const Json::Value& value, double* x, double* y)
{
	// 1. 图元位置和局部尺寸统一使用明确的二维数组。
	return value.isArray() && value.size() == 2 &&
		ReadFiniteJsonNumber(value[0], x) && ReadFiniteJsonNumber(value[1], y);
}

bool ReadJsonComponents(const Json::Value& value, int count, std::vector<double>* values)
{
	// 1. 变换数组必须是固定长度，缺项不能按 protobuf 默认值补齐。
	if (!value.isArray() || static_cast<int>(value.size()) != count)
	{
		return false;
	}
	values->clear();
	for (int index = 0; index < count; ++index)
	{
		double component = 0.0;
		if (!ReadFiniteJsonNumber(value[index], &component)) return false;
		values->push_back(component);
	}
	return true;
}

bool KindForJsonType(const std::string& type, message::ItemKind* kind)
{
	// 1. 服务端类型映射与客户端 CanvasItemSerializer 的稳定字符串保持一致。
	if (type == "pen") *kind = message::ITEM_PEN;
	else if (type == "rect") *kind = message::ITEM_RECT;
	else if (type == "oval") *kind = message::ITEM_OVAL;
	else if (type == "line") *kind = message::ITEM_LINE;
	else if (type == "arrow") *kind = message::ITEM_ARROW;
	else if (type == "diamond") *kind = message::ITEM_DIAMOND;
	else if (type == "image") *kind = message::ITEM_IMAGE;
	else if (type == "text") *kind = message::ITEM_TEXT;
	else return false;
	return true;
}

bool ReadBatchText(const Json::Value& value, message::ItemEditState* state)
{
	// 1. 文字和附属文字保留同一套样式，颜色只接受序列化器输出的 ARGB 格式。
	if (!value["text"].isString() || !value["font_family"].isString() || !value["font_size"].isUInt() ||
		!value["text_color"].isString() || !value["bold"].isBool()) return false;
	const std::string color = value["text_color"].asString();
	if (color.size() != 9 || color.front() != '#' ||
		!std::all_of(color.begin() + 1, color.end(), [](unsigned char ch) { return std::isxdigit(ch) != 0; })) return false;
	state->set_text(value["text"].asString());
	state->set_font_family(value["font_family"].asString());
	state->set_font_size(value["font_size"].asUInt());
	state->set_color(static_cast<std::uint32_t>(std::stoul(color.substr(1), nullptr, 16)));
	state->set_bold(value["bold"].asBool());
	return ValidateEditState(*state, 2U);
}

bool MakeBatchItemState(const message::ItemCreateItem& item, message::ItemEditState* state, Json::Value* item_data)
{
	// 1. 完整 JSON 仍是历史权威；服务端仅抽取通用编辑所需的公共变换状态。
	Json::CharReaderBuilder builder;
	Json::Value root;
	std::string errors;
	std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	if (!reader->parse(item.item_json().data(), item.item_json().data() + item.item_json().size(), &root, &errors) || !root.isObject())
	{
		return false;
	}
	message::ItemKind kind = message::ITEM_UNKNOWN;
	if (!root["item_id"].isString() || root["item_id"].asString() != item.item_id() ||
		!root["type"].isString() || !KindForJsonType(root["type"].asString(), &kind) || kind != item.kind())
	{
		return false;
	}
	double x = 0.0;
	double y = 0.0;
	double origin_x = 0.0;
	double origin_y = 0.0;
	double rotation = 0.0;
	double scale = 0.0;
	std::vector<double> matrix;
	if (!ReadJsonPair(root["position"], &x, &y) || !ReadJsonPair(root["transform_origin"], &origin_x, &origin_y) ||
		!ReadFiniteJsonNumber(root["rotation"], &rotation) || !ReadFiniteJsonNumber(root["scale"], &scale) || scale <= 0.0 ||
		!ReadJsonComponents(root["transform"], 9, &matrix))
	{
		return false;
	}
	*state = MakeInitialState(item.item_id(), kind);
	state->set_x(x);
	state->set_y(y);
	state->set_origin_x(origin_x);
	state->set_origin_y(origin_y);
	state->set_rotation(rotation);
	state->set_scale(scale);
	state->clear_matrix();
	for (double component : matrix) state->add_matrix(component);
	// 2. 编辑几何必须与客户端原始路径映射和文字布局一致，移动不能重置这些扩展。
	if (kind == message::ITEM_IMAGE)
	{
		double width = 0.0;
		double height = 0.0;
		if (!ReadJsonPair(root["display_size"], &width, &height) || width <= 0.0 || height <= 0.0 ||
			!root["asset_id"].isString() || !root["asset_ref"].isString() || !root["asset_sha256"].isString() ||
			root["asset_ref"].asString().empty() || root["asset_ref"].asString().size() > 1024 ||
			!root["mime_type"].isString() || !root["original_size"].isArray() || root["original_size"].size() != 2 ||
			!root["original_size"][0].isUInt() || !root["original_size"][1].isUInt() ||
			root["original_size"][0].asUInt() == 0 || root["original_size"][1].asUInt() == 0) return false;
		state->add_geometry(width);
		state->add_geometry(height);
	}
	else if (kind == message::ITEM_TEXT)
	{
		std::vector<double> values{0.0, 0.0};
		if (!ReadBatchText(root, state) || (root.isMember("text_layout") && !ReadJsonComponents(root["text_layout"], 2, &values))) return false;
		for (double value : values) state->add_geometry(value);
	}
	else if (kind == message::ITEM_PEN)
	{
		if (!root["path"].isArray() || root["path"].size() > 1000000U) return false;
		std::vector<double> values{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
		if (root.isMember("path_transform") && !ReadJsonComponents(root["path_transform"], 6, &values)) return false;
		for (double value : values) state->add_geometry(value);
	}
	else
	{
		const Json::Value& geometry = (kind == message::ITEM_LINE || kind == message::ITEM_ARROW) ? root["line"] : root["rect"];
		std::vector<double> values;
		if (!ReadJsonComponents(geometry, 4, &values)) return false;
		for (double value : values) state->add_geometry(value);
	}
	// 3. 附属文字随父图元提交，后续纯几何编辑继续保留复制时的文本。
	if (root.isMember("bound_text") && (!root["bound_text"].isObject() || !ReadBatchText(root["bound_text"], state))) return false;
	*item_data = std::move(root);
	return ValidateEditState(*state, 1U);
}
}

bool Room::AcceptItemCreateBatch(std::shared_ptr<CSession> session, const message::ItemCreateBatch& batch)
{
	// 1. 在同一把房间锁中验证整批载荷，任一条失败均不修改状态或历史。
	if (!session || session->IsClosed() || batch.uid() != session->GetUserId()) return false;
	std::lock_guard<std::mutex> lock(_mutex);
	message::ItemCreateBatchRsp response;
	*response.mutable_batch() = batch;
	auto reject = [&](int error, const char* reason) {
		response.set_error(error);
		response.set_error_message(reason);
		std::string body;
		if (response.SerializeToString(&body)) session->Send(body, ID_ITEM_CREATE_BATCH_RSP);
		return false;
	};
	if (!IsActiveMemberLocked(batch.uid()) || batch.room_id() != _room_id ||
		_sessions.at(batch.uid()).get() != session.get()) return reject(_is_closing ? message::RoomClosing : message::NotInRoom, "当前不在活动房间中");
	if (batch.uid() != _owner_uid && !_editable_users.count(batch.uid())) return reject(message::NotOwner, "当前没有编辑权限");
	if (batch.operation_id().empty() || batch.operation_id().size() > 128 || batch.payload_version() != 1 ||
		batch.server_sequence() != 0 || batch.items_size() <= 0 || batch.items_size() > 64 || batch.ByteSizeLong() > MAX_LENGTH) return reject(message::ItemEditInvalidArgument, "图元批量创建字段无效");
	const std::string cache_key = std::to_string(batch.uid()) + ":" + batch.operation_id();
	const auto duplicate = _batch_responses.find(cache_key);
	if (duplicate != _batch_responses.end())
	{
		message::ItemCreateBatch previous = duplicate->second.batch();
		previous.set_server_sequence(0);
		if (previous.SerializeAsString() != batch.SerializeAsString()) return reject(message::ItemEditInvalidArgument, "重复操作标识对应不同内容");
		std::string body;
		if (duplicate->second.SerializeToString(&body)) session->Send(body, ID_ITEM_CREATE_BATCH_RSP);
		return true;
	}
	std::unordered_set<std::string> ids;
	std::vector<message::ItemEditState> states;
	std::vector<Json::Value> item_data;
	states.reserve(batch.items_size());
	for (int index = 0; index < batch.items_size(); ++index)
	{
		const auto& item = batch.items(index);
		message::ItemEditState state;
		Json::Value data;
		if (item.item_id().empty() || item.item_id().size() > 128 || item.item_json().empty() || item.item_json().size() > 60000 ||
			item.stack_order() != static_cast<unsigned int>(index) || !ids.insert(item.item_id()).second ||
			_item_revisions.count(item.item_id()) || _draw_started.count(item.item_id()) || !MakeBatchItemState(item, &state, &data))
		{
			return reject(message::ItemEditInvalidArgument, "图元批量创建包含无效或重复图元");
		}
		states.push_back(std::move(state));
		item_data.push_back(std::move(data));
	}
	const auto image_count = std::count_if(states.begin(), states.end(), [](const message::ItemEditState& state) { return state.kind() == message::ITEM_IMAGE; });
	if (_image_items.size() + image_count > MAX_IMAGE_ITEMS) return reject(message::ItemEditInvalidArgument, "房间图片数量超过限制");

	// 2. 全部校验通过后一次分配顺序，写入可编辑状态并保存单条回放记录。
	const std::uint64_t sequence = _edit_sequence + 1;
	response.set_error(message::SUCCESS);
	response.mutable_batch()->set_server_sequence(sequence);
	std::string body;
	if (!response.SerializeToString(&body) || body.size() >= MAX_LENGTH) return reject(message::ItemEditInvalidArgument, "图元批量创建响应超过协议包长");
	for (std::size_t index = 0; index < states.size(); ++index)
	{
		const auto& state = states[index];
		const auto& data = item_data[index];
		_item_edit_states[state.item_id()] = state;
		_item_revisions[state.item_id()] = sequence;
		_batch_create_index[state.item_id()] = _history.size();
		// 旧图片和文字入口也要能找到副本，通用移动不能对缺失索引调用 at。
		if (state.kind() == message::ITEM_TEXT)
		{
			auto& text = _text_itemMap[state.item_id()];
			text.set_item_id(state.item_id());
			text.set_x(state.x());
			text.set_y(state.y());
			text.set_text(state.text());
			text.set_font_family(state.font_family());
			text.set_font_size(state.font_size());
			text.set_color(state.color());
			text.set_bold(state.bold());
			_text_sequenceMap[state.item_id()] = sequence;
		} else if (state.kind() == message::ITEM_IMAGE)
		{
			auto& image = _image_items[state.item_id()];
			image.set_item_id(state.item_id());
			image.set_asset_id(data["asset_id"].asString());
			image.set_asset_ref(data["asset_ref"].asString());
			image.set_asset_sha256(data["asset_sha256"].asString());
			image.set_mime_type(data["mime_type"].asString());
			image.set_original_width(data["original_size"][0].asUInt());
			image.set_original_height(data["original_size"][1].asUInt());
			auto* transform = image.mutable_transform();
			transform->set_x(static_cast<float>(state.x()));
			transform->set_y(static_cast<float>(state.y()));
			transform->set_width(static_cast<float>(state.geometry(0)));
			transform->set_height(static_cast<float>(state.geometry(1)));
			transform->set_rotation(static_cast<float>(state.rotation()));
			transform->set_scale_x(1.0F);
			transform->set_scale_y(1.0F);
		}
	}
	_edit_sequence = sequence;
	AppendHistoryLocked(HistoryEntry{ID_ITEM_CREATE_BATCH_RSP, body});
	_batch_responses[cache_key] = response;
	_batch_response_order.push_back(cache_key);
	if (_batch_response_order.size() > 512)
	{
		_batch_responses.erase(_batch_response_order.front());
		_batch_response_order.pop_front();
	}

	// 3. 在房间锁内按提交顺序入队，发起者也必须收到确认以解除本地 pending 状态。
	for (const auto& member : _sessions)
	{
		if (member.second && !member.second->IsClosed()) member.second->Send(body, ID_ITEM_CREATE_BATCH_RSP);
	}
	return true;
}

void Room::RebuildDrawHistoryIndexLocked()
{
	// 1. 只索引完整笔迹的结束消息；尚未完成的路径不能被编辑。
	_draw_end_index.clear();
	_batch_create_index.clear();
	for (std::size_t index = 0; index < _history.size(); ++index)
	{
		const auto& entry = _history[index];
		if (entry._msg_id == ID_ITEM_CREATE_BATCH_RSP)
		{
			message::ItemCreateBatchRsp batch;
			if (batch.ParseFromString(entry._body))
			{
				for (const auto& item : batch.batch().items()) _batch_create_index[item.item_id()] = index;
			}
			continue;
		}
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
	// 批量图元在回放快照中逐个附加最终状态，不能生成第二条旧类型创建记录。
	if (_batch_create_index.count(item_id)) return;
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

void Room::RememberItemEditResponseLocked(const std::string& key, const message::ItemEditOperationRsp& response)
{
	// 1. 更新和删除共用有界成功缓存，重复请求不能再次分配版本。
	_edit_responses[key] = response;
	_edit_response_order.push_back(key);
	if (_edit_response_order.size() > 512)
	{
		_edit_responses.erase(_edit_response_order.front());
		_edit_response_order.pop_front();
	}
}

void Room::DeleteItemLocked(const std::string& item_id, std::uint64_t revision)
{
	// 1. 删除各来源共享的状态，版本墓碑阻止旧请求重新创建同一图元。
	_item_edit_states.erase(item_id);
	_image_items.erase(item_id);
	_text_itemMap.erase(item_id);
	_text_sequenceMap[item_id] = revision;
	_draw_started.erase(item_id);
	_item_revisions[item_id] = revision;
	_edit_sequence = revision;
	_image_sequence = std::max(_image_sequence, revision);
	_text_sequence = std::max(_text_sequence, revision);

	// 2. 普通创建历史完整移除；批次只移除该成员，保留其余层叠顺序。
	const auto batch_index = _batch_create_index.find(item_id);
	if (batch_index != _batch_create_index.end())
	{
		auto& entry = _history[batch_index->second];
		message::ItemCreateBatchRsp batch;
		if (batch.ParseFromString(entry._body))
		{
			const auto saved = batch.batch().items();
			batch.mutable_batch()->clear_items();
			for (const auto& item : saved)
			{
				if (item.item_id() == item_id)
				{
					continue;
				}
				auto* retained = batch.mutable_batch()->add_items();
				*retained = item;
				retained->set_stack_order(batch.batch().items_size() - 1);
			}
			entry._body = batch.batch().items_size() == 0 ? std::string() : batch.SerializeAsString();
		}
	}
	_history.erase(std::remove_if(_history.begin(), _history.end(), [&](const HistoryEntry& entry) {
		return entry._image_item_id == item_id || entry._text_item_id == item_id ||
			entry._draw_item_id == item_id || (entry._msg_id == ID_ITEM_CREATE_BATCH_RSP && entry._body.empty());
	}), _history.end());

	// 3. 删除使历史下标前移，所有类型统一重建，避免后续编辑覆盖错误条目。
	RebuildImageHistoryIndexLocked();
	RebuildTextHistoryIndexLocked();
	RebuildDrawHistoryIndexLocked();
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
		if (operation.operation_type() != message::ITEM_EDIT_UPDATE || !operation.preview() || operation.operation_id().empty() || operation.operation_id().size() > 128 ||
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
	const bool is_delete = operation.operation_type() == message::ITEM_EDIT_DELETE;
	const bool valid_payload = is_delete
		? !operation.has_state() && operation.fields() == 0 && !operation.preview()
		: operation.operation_type() == message::ITEM_EDIT_UPDATE && operation.has_state() &&
			operation.state().item_id() == operation.target_item_id() && operation.fields() != 0 && (operation.fields() & ~3U) == 0;
	if (operation.operation_id().empty() || operation.operation_id().size() > 128 ||
		operation.target_item_id().empty() || operation.target_item_id().size() > 128 ||
		!valid_payload || operation.revision() != 0 || operation.ByteSizeLong() > 65535)
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
	// 3. 删除与更新共享版本和幂等校验，仅在成功事务中清理内容与历史。
	if (is_delete)
	{
		const std::uint64_t revision = _edit_sequence + 1;
		DeleteItemLocked(item_id, revision);
		response.mutable_operation()->set_revision(revision);
		RememberItemEditResponseLocked(cache_key, response);
		return true;
	}
	if (operation.state().kind() != current->second.kind() || current->second.kind() == message::ITEM_ERASER ||
		!ValidateEditState(operation.state(), operation.fields()))
	{
		return reject(message::ItemEditInvalidArgument, "图元几何、文字或样式无效");
	}
	// 4. 将明确修改的字段合并至最新状态，文字及自动增高在同一次请求原子生效。
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
	// 5. 预览仅供远端显示，不修改版本、历史或幂等缓存，也不要求逐帧确认。
	if (operation.preview())
	{
		return true;
	}

	// 6. 最终提交同步旧类型字段并只分配一次版本，撤销基于该确认结果。
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
	// 7. 有界缓存仅记录最终成功请求，重试不会再次推进版本或覆盖后续编辑。
	RememberItemEditResponseLocked(cache_key, response);
	return true;
}
