#include "CanvasServer/Room.h"
#include "CanvasServer/CSession.h"
#include "CanvasServer/const.h"
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
void Room::AppendHistory(const std::string& raw_drawreq)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _history.emplace_back(raw_drawreq);

    // 简单裁切: 超过上限丢掉最老的部分 (避免vector频繁erase头部)
    if (_history.size() > MAX_HISTORY_OPS)
    {
        const size_t kTrim = MAX_HISTORY_OPS / 10 + 1; // 每次裁剪约10%
        if (kTrim < _history.size())
        {
            _history.erase(_history.begin(), _history.begin() + static_cast<long>(kTrim));
        }
    }
}

// 实现：获取历史记录快照
std::vector<std::string> Room::GetHistorySnapshot()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _history;    // 拷贝一份，回放在锁外做
}

// 实现：清空历史记录
void Room::ClearHistory()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _history.clear();
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
    std::vector<std::string> history_snapshot;

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
            for (const auto& data : history_snapshot)
                session->Send(data, ID_DRAW_RSP);
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
