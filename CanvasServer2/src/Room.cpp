#include "CanvasServer/Room.h"
#include "CanvasServer/ConfigMgr.h"
#include "CanvasServer/CSession.h"
#include "CanvasServer/const.h"
#include "Logger/Logger.h"

#include <algorithm>

Room::Room(const std::string& room_id)
    : _room_id(room_id)
{
    LOG_INFO_CTX("Room::Room", "创建房间 room_id=" << _room_id);
}

Room::~Room()
{
    LOG_INFO_CTX("Room::~Room", "销毁房间 room_id=" << _room_id);
    // 可以在这里清空 _sessions，但智能指针会自动处理
}

// 实现：加入历史记录
void Room::AppendHistory(const std::string& raw_body, short msg_id)
{
    // 1. 关闭后禁止历史写入，活动房间沿用统一裁剪规则。
    std::lock_guard<std::mutex> lock(_mutex);
    if (_is_closing) return;
    // 公共入口只负责加锁；绘画路径和图片路径共用同一裁剪规则，锁内逻辑统一走 AppendHistoryLocked。
    AppendHistoryLocked(HistoryEntry{ msg_id, raw_body, std::string() });
}

void Room::AppendHistoryLocked(HistoryEntry entry)
{
    // 1. 保存消息 ID 与正文的配对关系，文字、图片和绘画回放保持原消息类型。
    // 调用方必须已持有 _mutex：图片操作在 ApplyImageOperation 的锁内直接记录历史，拆出加锁版本会造成死锁。
    _history.push_back(std::move(entry));

    // 2. 超限时裁剪最早的条目，随后重建图片和文字历史下标。
    if (_history.size() > MAX_HISTORY_OPS)
    {
        const size_t kTrim = MAX_HISTORY_OPS / 10 + 1; // 每次裁剪约10%
        if (kTrim < _history.size())
        {
            // 一旦淘汰笔迹的任一消息，同时移除同一笔迹的其余消息，禁止半笔迹回放。
            std::unordered_set<std::string> removed_draw_ids;
            for (std::size_t index = 0; index < kTrim; ++index)
            {
                if (!_history[index]._draw_item_id.empty()) removed_draw_ids.insert(_history[index]._draw_item_id);
                if (_history[index]._msg_id == ID_ITEM_CREATE_BATCH_RSP)
                {
                    message::ItemCreateBatchRsp batch;
                    if (batch.ParseFromString(_history[index]._body))
                    {
                        for (const auto& item : batch.batch().items()) removed_draw_ids.insert(item.item_id());
                    }
                }
            }
            _history.erase(_history.begin(), _history.begin() + static_cast<long>(kTrim));
            _history.erase(std::remove_if(_history.begin(), _history.end(), [&](const HistoryEntry& saved) {
                return removed_draw_ids.count(saved._draw_item_id) != 0;
            }), _history.end());
            for (const auto& item_id : removed_draw_ids)
            {
                _item_edit_states.erase(item_id);
                _draw_started.erase(item_id);
                _draw_end_index.erase(item_id);
                _image_items.erase(item_id);
                _text_itemMap.erase(item_id);
            }
        }
        // 头部删除会让所有保留条目下标前移，图片创建条目索引必须整体重建，否则变换更新会改写到错误条目。
        RebuildImageHistoryIndexLocked();
        RebuildTextHistoryIndexLocked();
        RebuildDrawHistoryIndexLocked();
    }
}

void Room::RebuildImageHistoryIndexLocked()
{
    // 只有图片创建条目携带非空 item_id，删除条目和绘画条目为空；扫描重建保证下标与当前 vector 完全一致。
    _image_create_index.clear();
    for (std::size_t index = 0; index < _history.size(); ++index)
    {
        if (!_history[index]._image_item_id.empty())
        {
            _image_create_index[_history[index]._image_item_id] = index;
        }
    }
}

// 实现：获取历史记录快照
std::vector<Room::HistoryEntry> Room::GetHistorySnapshot()
{
    // 1. 公共入口加锁，加入房间时复用同一个持锁快照实现。
    std::lock_guard<std::mutex> lock(_mutex);
    return GetHistorySnapshotLocked();
}

std::vector<Room::HistoryEntry> Room::GetHistorySnapshotLocked()
{
	// 1. 批量创建保留原几何与顺序；已删除条目过滤后重新编号，不能在重进房间时复活。
	std::vector<HistoryEntry> snapshot;
	for (const auto& entry : _history)
	{
		if (entry._msg_id == ID_ITEM_CREATE_BATCH_RSP)
		{
			message::ItemCreateBatchRsp response;
			if (!response.ParseFromString(entry._body)) continue;
			const auto items = response.batch().items();
			response.mutable_batch()->clear_items();
			for (const auto& item : items)
			{
				if (!_item_edit_states.count(item.item_id())) continue;
				auto* saved = response.mutable_batch()->add_items();
				*saved = item;
				saved->set_stack_order(response.batch().items_size() - 1);
			}
			if (response.batch().items_size() == 0) continue;
			snapshot.push_back(HistoryEntry{ID_ITEM_CREATE_BATCH_RSP, response.SerializeAsString()});

			// 2. 同一批内各图元独立推进版本，逐个回放最终状态，不能只保留最后移动的图元。
			for (const auto& item : response.batch().items())
			{
				const auto& state = _item_edit_states.at(item.item_id());
				message::ItemEditOperationRsp edit;
				edit.mutable_operation()->set_room_id(_room_id);
				edit.mutable_operation()->set_target_item_id(item.item_id());
				edit.mutable_operation()->set_revision(_item_revisions.at(item.item_id()));
				const auto kind = state.kind();
				edit.mutable_operation()->set_fields(kind == message::ITEM_TEXT || kind == message::ITEM_RECT ||
					kind == message::ITEM_OVAL || kind == message::ITEM_DIAMOND ? 3 : 1);
				*edit.mutable_current_state() = state;
				snapshot.push_back(HistoryEntry{ID_ITEM_EDIT_RSP, edit.SerializeAsString()});
			}
			continue;
		}
		snapshot.push_back(entry);
		if (!entry._edit_body.empty()) snapshot.push_back(HistoryEntry{ID_ITEM_EDIT_RSP, entry._edit_body});
	}
	return snapshot;
}

// 实现：清空历史记录
void Room::ClearHistory()
{
    // 1. 清屏同步移除文字、图片和创建历史索引，操作总序号继续递增。
    std::lock_guard<std::mutex> lock(_mutex);
    _history.clear();
    // 清屏必须同时清除服务端维护的图片索引，否则后续复用 item_id 会被错误判定为重复图元。
    _image_items.clear();
    // 历史已清空，创建条目索引全部失效，必须同步清空避免改写到悬空下标。
    _image_create_index.clear();
    _text_itemMap.clear();
    _text_create_indexMap.clear();
    _text_sequenceMap.clear();
    ClearItemEditStateLocked();
}

void Room::RebuildTextHistoryIndexLocked()
{
	// 1. 历史裁剪改变下标，按保留下来的文字创建条目重新建立索引。
	_text_create_indexMap.clear();
	for (std::size_t index = 0; index < _history.size(); ++index)
	{
		if (!_history[index]._text_item_id.empty()) _text_create_indexMap[_history[index]._text_item_id] = index;
	}
}

void Room::FillTextStateLocked(message::TextOperationRsp& response)
{
	// 1. 返回目标当前状态或不存在状态，同时提供该图元独立版本。
	const auto& item_id = response.operation().target_item_id();
	response.clear_current_item();
	auto item_iterator = _text_itemMap.find(item_id);
	if (item_iterator != _text_itemMap.end()) *response.mutable_current_item() = item_iterator->second;
	auto sequence_iterator = _text_sequenceMap.find(item_id);
	response.mutable_operation()->set_server_sequence(sequence_iterator == _text_sequenceMap.end() ? 0 : sequence_iterator->second);
}

void Room::FillTextState(message::TextOperationRsp& response)
{
	// 1. 权限或字段失败时也提供已确认状态供客户端恢复。
	std::lock_guard<std::mutex> lock(_mutex);
	FillTextStateLocked(response);
}

bool Room::ApplyTextOperation(const message::TextOperation& operation, message::TextOperationRsp& response)
{
	// 1. 同一房间锁内校验目标版本及权限，普通请求按服务端接收顺序生效。
	std::lock_guard<std::mutex> lock(_mutex);
	response.Clear();
	if (!IsActiveMemberLocked(operation.uid()))
	{
		response.set_error(_is_closing ? message::RoomClosing : message::NotInRoom);
		*response.mutable_operation() = operation;
		FillTextStateLocked(response);
		return false;
	}
	*response.mutable_operation() = operation;
	const std::string item_id = operation.target_item_id();
	auto iterator = _text_itemMap.find(item_id);
	const std::uint64_t known_sequence = _text_sequenceMap.count(item_id) ? _text_sequenceMap.at(item_id) : 0;
	if (operation.operation_type() < message::TEXT_CREATE || operation.operation_type() > message::TEXT_DELETE ||
	(operation.uid() != _owner_uid && _editable_users.count(operation.uid()) == 0) ||
	(operation.expected_sequence() != 0 && operation.expected_sequence() != known_sequence) ||
	(operation.operation_type() == message::TEXT_CREATE ? iterator != _text_itemMap.end() || _text_itemMap.size() >= 10000 ||
	_item_edit_states.count(item_id) || _draw_started.count(item_id) : iterator == _text_itemMap.end()))
	{
		response.set_error(message::TextStateConflict);
		response.set_error_message("文字状态已改变、已删除或没有编辑权限");
		FillTextStateLocked(response);
		return false;
	}

	// 2. 更新内容样式与位置分别处理，防止编辑草稿覆盖同时提交的移动。
	message::TextItem state;
	if (operation.operation_type() == message::TEXT_CREATE) state = operation.item();
	else state = iterator->second;
	if (operation.operation_type() == message::TEXT_UPDATE)
	{
		state.set_text(operation.item().text());
		state.set_font_family(operation.item().font_family());
		state.set_font_size(operation.item().font_size());
		state.set_color(operation.item().color());
		state.set_bold(operation.item().bold());
	} else if (operation.operation_type() == message::TEXT_MOVE)
	{
		state.set_x(operation.item().x());
		state.set_y(operation.item().y());
	}
	const bool is_delete = operation.operation_type() == message::TEXT_DELETE;
	const std::uint64_t sequence = std::max(_text_sequence, _edit_sequence) + 1;
	response.set_error(0);
	response.mutable_operation()->set_server_sequence(sequence);
	*response.mutable_operation()->mutable_item() = state;
	if (!is_delete) *response.mutable_current_item() = state;
	std::string body;
	message::TextOperationRsp snapshot = response;
	snapshot.mutable_operation()->set_operation_type(message::TEXT_CREATE);
	std::string snapshot_body;
	if (!response.SerializeToString(&body) || (!is_delete && !snapshot.SerializeToString(&snapshot_body)))
	{
		response.set_error(message::ServerInternalErr);
		response.set_error_message("文字状态编码失败");
		FillTextStateLocked(response);
		return false;
	}
	_text_sequence = sequence;
	_text_sequenceMap[item_id] = sequence;
	if (is_delete) _text_itemMap.erase(item_id);
	else _text_itemMap[item_id] = state;

	// 3. 创建顺序保留，修改原位重写最新创建快照；删除追加历史以保证回放结果一致。
	if (is_delete)
	{
		AppendHistoryLocked(HistoryEntry{ID_TEXT_OPERATION_RSP, body, std::string(), std::string()});
		_text_create_indexMap.erase(item_id);
	} else if (!_batch_create_index.count(item_id))
	{
		auto index_iterator = _text_create_indexMap.find(item_id);
		if (index_iterator != _text_create_indexMap.end()) _history[index_iterator->second]._body = std::move(snapshot_body);
		else
		{
			_text_create_indexMap[item_id] = _history.size();
			AppendHistoryLocked(HistoryEntry{ID_TEXT_OPERATION_RSP, snapshot_body, std::string(), item_id});
		}
	}
	SyncLegacyTextLocked(operation, state, sequence);
	return true;
}

bool Room::ApplyImageOperation(message::ImageOperation& operation, std::string* normalized_body)
{
    // 1. 房间关闭和成员退出必须与图元提交使用同一接受边界。
    std::lock_guard<std::mutex> lock(_mutex);
    if (!IsActiveMemberLocked(operation.uid()) || (operation.uid() != _owner_uid && !_editable_users.count(operation.uid()))) return false;

    // 序列化失败虽然通常只会在内存分配异常时发生，但仍需保留旧索引和序号，
    // 确保失败操作不会造成“房间状态已变更但没有广播”的隐性分叉。
    const auto previous_image_items = _image_items;
    const std::uint64_t previous_sequence = _image_sequence;

    std::size_t max_image_items = MAX_IMAGE_ITEMS;
    try
    {
        const std::string configured = ConfigMgr::Inst()["ImageOperation"]["MaxImageItemsPerRoom"];
        if (!configured.empty())
        {
            max_image_items = static_cast<std::size_t>(std::stoull(configured));
        }
    } catch (const std::exception&)
    {
        max_image_items = MAX_IMAGE_ITEMS;
    }
    if (max_image_items == 0)
    {
        max_image_items = MAX_IMAGE_ITEMS;
    }

    // 2. 图片操作的状态变更、重复检查和序号分配必须在同一把房间锁内完成，
    // 这样即使未来增加多个逻辑线程，也不会产生两个相同 item_id 的创建结果。
    const std::string requested_item_id = operation.item().item_id();
    std::string target_item_id = operation.target_item_id();

    if (operation.operation_type() == message::IMAGE_CREATE)
    {
        if (_image_items.size() >= max_image_items
        || requested_item_id.empty()
        || _image_items.find(requested_item_id) != _image_items.end()
        || _item_edit_states.count(requested_item_id) || _draw_started.count(requested_item_id))
        {
            return false;
        }

        _image_items.emplace(requested_item_id, operation.item());
        target_item_id = requested_item_id;
    }
    else if (operation.operation_type() == message::IMAGE_UPDATE_TRANSFORM)
    {
        if (target_item_id.empty())
        {
            target_item_id = requested_item_id;
        }

        auto item_it = _image_items.find(target_item_id);
        if (item_it == _image_items.end() || !operation.item().has_transform())
        {
            return false;
        }

        // 更新只允许改变几何变换，资源元数据继续使用首次创建时经过校验的值。
        item_it->second.mutable_transform()->CopyFrom(operation.item().transform());
        operation.mutable_item()->CopyFrom(item_it->second);
    }
    else if (operation.operation_type() == message::IMAGE_DELETE)
    {
        if (target_item_id.empty())
        {
            target_item_id = requested_item_id;
        }

        auto item_it = _image_items.find(target_item_id);
        if (item_it == _image_items.end())
        {
            return false;
        }

        // 广播删除操作时保留被删除图元的完整元数据，客户端可以据此安全清理对应资源引用。
        operation.mutable_item()->CopyFrom(item_it->second);
        _image_items.erase(item_it);
    }
    else
    {
        return false;
    }

    operation.set_target_item_id(target_item_id);
    _image_sequence = std::max(_image_sequence, _edit_sequence) + 1;
    operation.set_server_sequence(_image_sequence);

    if (normalized_body && !operation.SerializeToString(normalized_body))
    {
        _image_items = previous_image_items;
        _image_sequence = previous_sequence;
        normalized_body->clear();
        return false;
    }

    // 3. 图片历史采用快照式记录：变换更新不追加移动条目，而是原位改写创建条目内的变换，
    // 新成员回放时图片直接出现在最新位置，不会重放整段移动轨迹。
    // 本函数已持有 _mutex，必须走不加锁的 AppendHistoryLocked，不能调用公共入口 AppendHistory。
    switch (operation.operation_type())
    {
    case message::IMAGE_CREATE:
        // 记录 item_id 与创建条目下标，后续变换更新据此定位并改写该条目。
        _image_create_index[target_item_id] = _history.size();
        AppendHistoryLocked(HistoryEntry{ ID_IMAGE_OPERATION_RSP,
            normalized_body ? *normalized_body : std::string(),
            target_item_id });
        break;

    case message::IMAGE_UPDATE_TRANSFORM:
        {
            auto index_it = _image_create_index.find(target_item_id);
            if (index_it != _image_create_index.end())
            {
                // 解析旧创建正文，只替换变换字段后重新序列化；元数据与创建者信息保持原样，
                // 回放侧拿到的仍是合法 CREATE 操作，只是几何状态为最新值。
                message::ImageOperation stored_operation;
                if (stored_operation.ParseFromString(_history[index_it->second]._body))
                {
                    *stored_operation.mutable_item()->mutable_transform() = operation.item().transform();
                    std::string rewritten_body;
                    if (stored_operation.SerializeToString(&rewritten_body))
                    {
                        _history[index_it->second]._body = std::move(rewritten_body);
                    }
                    // 重新序列化失败时保留旧正文：仅影响回放位置精度，不值得为此回滚已生效的在线广播。
                }
            }
            else if (!_batch_create_index.count(target_item_id))
            {
                // 创建条目已被历史裁剪淘汰时重新注入一条创建记录，避免图元仍存在但新成员永远收不到它。
                // operation.item() 在更新分支已复制为完整最新状态，直接改写类型即为合法 CREATE。
                message::ImageOperation create_operation = operation;
                create_operation.set_operation_type(message::IMAGE_CREATE);
                std::string create_body;
                if (create_operation.SerializeToString(&create_body))
                {
                    _image_create_index[target_item_id] = _history.size();
                    AppendHistoryLocked(HistoryEntry{ ID_IMAGE_OPERATION_RSP, create_body, target_item_id });
                }
            }
            break;
        }

    case message::IMAGE_DELETE:
        // 删除必须追加进历史：新成员需要知道图元已被移除；同时清除索引防止后续改写悬空条目。
        _image_create_index.erase(target_item_id);
        AppendHistoryLocked(HistoryEntry{ ID_IMAGE_OPERATION_RSP,
            normalized_body ? *normalized_body : std::string(),
            std::string() });
        break;

    default:
        break;
    }
    // 4. 旧图片入口同步通用版本，使后续拖动和撤销使用同一接受边界。
    SyncLegacyImageLocked(operation);
    return true;
}

void Room::Join(std::shared_ptr<CSession> session)
{
    // 1. 兼容旧调用入口，新的业务处理器检查 TryJoin 返回结果。
    TryJoin(std::move(session));
}

bool Room::TryJoin(std::shared_ptr<CSession> session)
{
	// 1. 房间锁内确认活动状态，同一连接重复加入不重复回放。
	if (!session || session->IsClosed() || session->GetUserId() == 0) return false;
	const int uid = session->GetUserId();
	bool first_join = false;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_is_closing || _leaving_users.count(uid)) return false;
		auto iterator = _sessions.find(uid);
		first_join = iterator == _sessions.end() || iterator->second != session;
		// 2. 历史先入发送队列再发布成员；这里只入队，网络写入由 executor 执行。
		if (first_join)
		{
			for (const auto& entry : GetHistorySnapshotLocked())
			{
				session->Send(entry._body, entry._msg_id);
			}
		}
		_sessions[uid] = session;
		session->SetRoom(shared_from_this());
		if (session->IsClosed())
		{
			_sessions.erase(uid);
			return false;
		}
	}
	// 3. 新成员通知在房间锁外序列化并发送。
	if (first_join) BroadcastUserEnter(session);
	return true;
}

void Room::Leave(int uid)
{
    // 1. 保留旧内存接口，生命周期处理使用带具体会话的重载。
    Leave(uid, nullptr);
}

bool Room::Leave(int uid, const CSession* expected_session)
{
	// 1. 只移除预期成员，旧连接的关闭事件不能清理新连接。
	std::shared_ptr<CSession> removed;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		auto iterator = _sessions.find(uid);
		if (iterator == _sessions.end() || (expected_session && iterator->second.get() != expected_session)) return false;
		removed = iterator->second;
		_sessions.erase(iterator);
		_editable_users.erase(uid);
		_leaving_users.erase(uid);
	}
	// 2. 在房间锁外解除绑定；Redis 与广播由生命周期处理器统一编排。
	if (removed) removed->ClearRoom(this);
	return true;
}

void Room::Broadcast(const std::string& data, int msg_id, int exclude_uid)
{
    // 1. 关闭中不再产生业务广播，结束通知直接使用清理快照。
    if (IsClosing()) return;
    auto sessions = GetMemberSessionSnapshot(exclude_uid);     //获取快照(除自己),避免锁外操作

    // 遍历发送
    for (auto& session : sessions)
    {
        if (!session || session->IsClosed())
        continue;

        // 发送
        session->Send(data, msg_id);
    }
}

// 实现：广播有人进入
void Room::BroadcastUserEnter(std::shared_ptr<CSession> session)
{
    message::UserJoinRoomBroadcast msg;

    //获取msg里UserInfo的指针
    message::UserInfo* user_info = msg.mutable_user_info();

    //填充数据
    user_info->set_uid(session->GetUserId());
    user_info->set_name(session->GetName());
    user_info->set_avatar_url(session->GetAvatarUrl());

    //转换为二进制数据
    std::string sendData;
    if (msg.SerializeToString(&sendData))
    {
        //广播给房间内所有用户
        Broadcast(sendData, ID_USER_JOIN_BROADCAST,session->GetUserId());
    }
}

// 实现：广播有人离开
void Room::BroadcastUserLeave(int uid)
{
    // 1. 广播携带房间标识，客户端可以丢弃旧房间的迟到通知。
    message::UserLeaveRoomBroadcast msg;
    msg.set_uid(uid);
    msg.set_room_id(_room_id);

    std::string sendData;
    if (msg.SerializeToString(&sendData))
    {
        //广播给房间内所有用户
        Broadcast(sendData, ID_USER_LEAVE_BROADCAST);
    }
}

// 实现：获取房间内所有成员的快照（复制一份，防止期间有 加入/离开 引起的线程安全）
std::vector<message::UserInfo> Room::GetMemberSnapshot()
{
    std::lock_guard<std::mutex> lock(_mutex);

    std::vector<message::UserInfo> member_list;
    member_list.reserve(_sessions.size());

    for (const auto& pair : _sessions)
    {
        std::shared_ptr<CSession> session = pair.second;
        if(!session) continue;
        if(session->IsClosed()) continue;

        //构造UserInfo
        message::UserInfo user_info;
        user_info.set_uid(session->GetUserId());
        user_info.set_name(session->GetName());
        user_info.set_avatar_url(session->GetAvatarUrl());

        member_list.emplace_back(user_info);
    }
    return member_list;
}

// 实现：获取房间内所有成员的Session快照（复制一份，防止期间有 添加/删除 引起的线程安全）
std::vector<std::shared_ptr<CSession>> Room::GetMemberSessionSnapshot(int exclude_uid)
{
    // 1. 在房间锁内复制会话引用，排除目标 UID 后在锁外发送。
    std::vector<std::shared_ptr<CSession>> session_list;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        session_list.reserve(_sessions.size());

        for (const auto& member : _sessions)
        {
            const int uid = member.first;
            const auto& session = member.second;
            if (uid == exclude_uid)
            {
                continue;
            }
            if (!session)
            {
                continue;
            }
            session_list.emplace_back(session); //拷贝指针,保证锁外安全使用
        }
    }
    return session_list;
}

//获取房间ID
std::string Room::GetRoomId() const
{
    return _room_id;
}

void Room::SetRoomInfo(const std::string& name, int owner_uid)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _name = name;
    _owner_uid = owner_uid;
}
int Room::GetOwnerUid() const //获取房主ID
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _owner_uid;
}

bool Room::IsOwner(int uid) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return uid != 0 && uid == _owner_uid;
}

bool Room::HasMember(int uid) const
{
    // 1. 退出中成员不能继续使用房间业务资格。
    std::lock_guard<std::mutex> lock(_mutex);
    return IsActiveMemberLocked(uid);
}

bool Room::CanEdit(int uid) const
{
    // 1. 房主也必须仍是活动成员，旧授权不能跨退出生效。
    std::lock_guard<std::mutex> lock(_mutex);
    return IsActiveMemberLocked(uid) && (uid == _owner_uid || _editable_users.count(uid) > 0);
}

bool Room::GrantEdit(int uid)
{
    std::lock_guard<std::mutex> lock(_mutex);
    // 1. 关闭或退出中的用户不能获得新授权。
    if (!IsActiveMemberLocked(uid) || uid == _owner_uid)
    return false;

    auto it = _sessions.find(uid);
    if (it == _sessions.end() || !it->second || it->second->IsClosed())
    return false;

    _editable_users.insert(uid);
    return true;
}

bool Room::RevokeEdit(int uid)
{
    std::lock_guard<std::mutex> lock(_mutex);
    // 1. 房间关闭后禁止继续修改权限。
    if (_is_closing || uid == 0 || uid == _owner_uid)
    return false;

    return _editable_users.erase(uid) > 0;
}

bool Room::IsActiveMemberLocked(int uid) const
{
	// 1. 调用者持有房间锁，统一校验生命周期、成员及连接状态。
	auto iterator = _sessions.find(uid);
	return !_is_closing && !_leaving_users.count(uid) && iterator != _sessions.end() &&
	iterator->second && !iterator->second->IsClosed();
}

bool Room::ContainsSession(int uid, const CSession* expected_session) const
{
	// 1. 此比较也允许关闭中的会话，用于精确清理原成员。
	std::lock_guard<std::mutex> lock(_mutex);
	auto iterator = _sessions.find(uid);
	return iterator != _sessions.end() && iterator->second.get() == expected_session;
}

bool Room::BeginLeave(int uid, const CSession* expected_session)
{
	// 1. 封住成员的操作资格，Redis 超时后保持可重试状态。
	std::lock_guard<std::mutex> lock(_mutex);
	auto iterator = _sessions.find(uid);
	if (_is_closing || iterator == _sessions.end() || iterator->second.get() != expected_session) return false;
	_leaving_users.insert(uid);
	return true;
}

bool Room::BeginClose()
{
	// 1. 已经关闭的房间不能重新开启，关闭中重复请求允许继续清理。
	std::lock_guard<std::mutex> lock(_mutex);
	if (_is_closed) return false;
	_is_closing = true;
	return true;
}

bool Room::IsClosing() const
{
	// 1. 使用同一房间锁观察操作接受边界。
	std::lock_guard<std::mutex> lock(_mutex);
	return _is_closing;
}

std::vector<std::shared_ptr<CSession>> Room::FinishClose()
{
	// 1. Redis 删除已确认后，在锁内清空成员、权限和全部图元状态。
	std::vector<std::shared_ptr<CSession>> members;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_is_closing = true;
		_is_closed = true;
		for (const auto& member : _sessions) members.push_back(member.second);
		_sessions.clear();
		_editable_users.clear();
		_leaving_users.clear();
		_history.clear();
		_image_items.clear();
		_image_create_index.clear();
		_text_itemMap.clear();
		_text_create_indexMap.clear();
		_text_sequenceMap.clear();
		ClearItemEditStateLocked();
	}
	// 2. 解绑和结束通知使用成员快照，不重新访问已经清空的容器。
	for (const auto& member : members) if (member) member->ClearRoom(this);
	return members;
}

bool Room::AcceptDraw(std::shared_ptr<CSession> session, const message::DrawReq& request, const std::string& body)
{
	// 1. 在同一房间锁内验证具体连接及编辑资格，避免校验后被退出或解散。
	std::lock_guard<std::mutex> lock(_mutex);
	const int uid = session->GetUserId();
	auto iterator = _sessions.find(uid);
	if (!IsActiveMemberLocked(uid) || iterator->second != session || request.uid() != uid ||
	(uid != _owner_uid && !_editable_users.count(uid))) return false;
	// 2. 清屏同时清理图片和文字；其他绘画沿用已有历史采样规则。
	if (request.cmd() == message::CMD_CLEAR)
	{
		_history.clear();
		_image_items.clear();
		_image_create_index.clear();
		_text_itemMap.clear();
		_text_create_indexMap.clear();
		_text_sequenceMap.clear();
		ClearItemEditStateLocked();
	}
	if (request.cmd() != message::CMD_CLEAR && !RegisterDrawLocked(request)) return false;
	const bool is_pen = request.shape() == message::SHAPE_PEN || request.shape() == message::SHAPE_ERASER;
	if (request.cmd() == message::CMD_START || request.cmd() == message::CMD_END ||
	(is_pen && request.cmd() == message::CMD_MOVE))
	{
		AppendHistoryLocked(HistoryEntry{ID_DRAW_RSP, body, std::string(), std::string(), request.item_id()});
		if (request.cmd() == message::CMD_END && _item_edit_states.count(request.item_id()))
		{
			_draw_end_index[request.item_id()] = _history.size() - 1;
		}
	}
	// 3. 发送队列入队属于接受事务；Send 只入队，实际网络写入在 IO 线程执行。
	for (const auto& member : _sessions)
	{
		if (member.first != uid && member.second && !member.second->IsClosed()) member.second->Send(body, ID_DRAW_RSP);
	}
	return true;
}
