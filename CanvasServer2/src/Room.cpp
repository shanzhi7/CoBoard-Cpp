#include "CanvasServer/Room.h"
#include "CanvasServer/ConfigMgr.h"
#include "CanvasServer/CSession.h"
#include "CanvasServer/const.h"
#include "CanvasServer/RedisMgr.h"
#include "Logger/Logger.h"

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
    std::lock_guard<std::mutex> lock(_mutex);
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
            _history.erase(_history.begin(), _history.begin() + static_cast<long>(kTrim));
        }
        // 头部删除会让所有保留条目下标前移，图片创建条目索引必须整体重建，否则变换更新会改写到错误条目。
        RebuildImageHistoryIndexLocked();
        RebuildTextHistoryIndexLocked();
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
    std::lock_guard<std::mutex> lock(_mutex);
    return _history;    // 拷贝一份，回放在锁外做
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
	*response.mutable_operation() = operation;
	const std::string item_id = operation.target_item_id();
	auto iterator = _text_itemMap.find(item_id);
	const std::uint64_t known_sequence = _text_sequenceMap.count(item_id) ? _text_sequenceMap.at(item_id) : 0;
	if (operation.operation_type() < message::TEXT_CREATE || operation.operation_type() > message::TEXT_DELETE ||
		(operation.uid() != _owner_uid && _editable_users.count(operation.uid()) == 0) ||
		(operation.expected_sequence() != 0 && operation.expected_sequence() != known_sequence) ||
		(operation.operation_type() == message::TEXT_CREATE ? iterator != _text_itemMap.end() || _text_itemMap.size() >= 10000 : iterator == _text_itemMap.end()))
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
	const std::uint64_t sequence = _text_sequence + 1;
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
	} else
	{
		auto index_iterator = _text_create_indexMap.find(item_id);
		if (index_iterator != _text_create_indexMap.end()) _history[index_iterator->second]._body = std::move(snapshot_body);
		else
		{
			_text_create_indexMap[item_id] = _history.size();
			AppendHistoryLocked(HistoryEntry{ID_TEXT_OPERATION_RSP, snapshot_body, std::string(), item_id});
		}
	}
	return true;
}

bool Room::ApplyImageOperation(message::ImageOperation& operation, std::string* normalized_body)
{
    std::lock_guard<std::mutex> lock(_mutex);

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
    }
    catch (const std::exception&)
    {
        max_image_items = MAX_IMAGE_ITEMS;
    }
    if (max_image_items == 0)
    {
        max_image_items = MAX_IMAGE_ITEMS;
    }

    // 图片操作的状态变更、重复检查和序号分配必须在同一把房间锁内完成，
    // 这样即使未来增加多个逻辑线程，也不会产生两个相同 item_id 的创建结果。
    const std::string requested_item_id = operation.item().item_id();
    std::string target_item_id = operation.target_item_id();

    if (operation.operation_type() == message::IMAGE_CREATE)
    {
        if (_image_items.size() >= max_image_items
            || requested_item_id.empty()
            || _image_items.find(requested_item_id) != _image_items.end())
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
    operation.set_server_sequence(++_image_sequence);

    if (normalized_body && !operation.SerializeToString(normalized_body))
    {
        _image_items = previous_image_items;
        _image_sequence = previous_sequence;
        normalized_body->clear();
        return false;
    }

    // 图片历史采用快照式记录：变换更新不追加移动条目，而是原位改写创建条目内的变换，
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
        else
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
    return true;
}

void Room::Join(std::shared_ptr<CSession> session)
{

    if (!session)
    {
        LOG_WARN_CTX("Room::Join", "加入房间失败: session 为空");
        return;
    }
    if (session->IsClosed())
    {
        LOG_WARN_CTX("Room::Join", "加入房间失败: session 已关闭 uid=" << session->GetUserId());
        return;
    }

    int uid = session->GetUserId();
    if (uid == 0) // 未登录用户不允许加入
    {
        return;
    }
    bool first_join = false;    // 是否是第一次加入
    std::vector<HistoryEntry> history_snapshot;

    {
        std::lock_guard<std::mutex> lock(_mutex);

        // 是否第一次加入
        if (_sessions.find(uid) == _sessions.end())
            first_join = true;

        // 只给“首次加入”的人回放历史
        if (first_join && !_history.empty())
        {
            history_snapshot = _history;
        }

        // 历史包必须在房间锁内先进入该会话的发送队列，再把会话加入房间。
        // 否则历史包通过 executor 异步投递期间，其他用户的新绘画可能先入队，
        // 接收端会先收到 MOVE/END，再收到 START，表现为加入房间后图形不完整。
        if (first_join)
        {
            for (const auto& entry : history_snapshot)
                session->Send(entry._body, entry._msg_id);
        }

        _sessions[uid] = session;
        session->SetRoom(shared_from_this());   //这样 Session 断开时知道通知哪个房间,session有room的弱指针

        LOG_INFO_CTX("Room::Join", "用户加入 room_id=" << _room_id << " uid=" << uid
            << " total=" << _sessions.size());

        // 双重保险,加入后再查一次
        // 如果刚才加入的过程中那边断开了，现在赶紧把他踢出去
        if (session->IsClosed())
        {
            _sessions.erase(uid); // 立即回滚
            return;
        }
    }

        // 只有第一次 join 才广播进入，通知其他用户，更新客户端ui
    if (first_join)
        BroadcastUserEnter(session);

}
void Room::Leave(int uid)
{
    bool b_removed = false;

    {
        std::lock_guard<std::mutex> lock(_mutex);

        //移除用户
        auto it = _sessions.find(uid);
        if (it != _sessions.end())
        {
            _sessions.erase(it);
            LOG_INFO_CTX("Room::Leave", "用户离开 room_id=" << _room_id << " uid=" << uid
                << " total=" << _sessions.size());
            b_removed = true;

        }
    }   // 锁自动释放

    if (b_removed)
    {
        // Redis 成员集合由网关用于签名授权；先移除已断开的 UID，再广播离开事件，避免短时间内继续获得房间资源权限。
        RedisMgr::getInstance()->RemoveUserFromRoom(_room_id, std::to_string(uid));
        // 【重点】广播通知其他人
        BroadcastUserLeave(uid);
    }
}

void Room::Broadcast(const std::string& data, int msg_id, int exclude_uid)
{ 
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
    message::UserLeaveRoomBroadcast msg;
    msg.set_uid(uid);

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
    std::vector<std::shared_ptr<CSession>> session_list;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        session_list.reserve(_sessions.size());

        for (const auto& [uid, session] : _sessions)
        {
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
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _sessions.find(uid);
    return it != _sessions.end() && it->second && !it->second->IsClosed();
}

bool Room::CanEdit(int uid) const
{
    // 房主天然可编辑；普通成员需要被房主加入授权集合后才可编辑。
    std::lock_guard<std::mutex> lock(_mutex);
    return uid != 0 && (uid == _owner_uid || _editable_users.count(uid) > 0);
}

bool Room::GrantEdit(int uid)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (uid == 0 || uid == _owner_uid)
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
    if (uid == 0 || uid == _owner_uid)
        return false;

    return _editable_users.erase(uid) > 0;
}
