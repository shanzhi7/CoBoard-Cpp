#include "CanvasServer/Room.h"
#include "CanvasServer/CSession.h"
#include "CanvasServer/const.h"

Room::Room(const std::string& room_id)
    : _room_id(room_id)
{
    std::cout << "[Room] Created: " << _room_id << std::endl;
}

Room::~Room()
{
    std::cout << "[Room] Destroyed: " << _room_id << std::endl;
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
        std::cout << "[Room] Join failed: null session\n";
        return;
    }
    if (session->IsClosed())
    {
        std::cout << "[Room] Join failed: session closed. UID: "
            << session->GetUserId() << std::endl;
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

        _sessions[uid] = session;
        session->SetRoom(shared_from_this());   //这样 Session 断开时知道通知哪个房间,session有room的弱指针

        // 只给“首次加入”的人回放历史
        if (first_join && !_history.empty())
        {
            history_snapshot = _history;
        }

        std::cout << "[Room " << _room_id << "] User joined: " << uid
            << ". Total: " << _sessions.size() << std::endl;

        // 双重保险,加入后再查一次
        // 如果刚才加入的过程中那边断开了，现在赶紧把他踢出去
        if (session->IsClosed())
        {
            _sessions.erase(uid); // 立即回滚
            return;
        }
    }

    //先回放历史 (只给加入者)
    if (first_join && !history_snapshot.empty())
    {
        auto weak_sess = std::weak_ptr<CSession>(session);

        // 投递到该 session 的 IO executor 线程串行执行
        boost::asio::post(session->GetSocket().get_executor(),
            [weak_sess, history_snapshot]() {
                auto sess = weak_sess.lock();
                if (!sess || sess->IsClosed()) return;

                for (const auto& data : history_snapshot)
                {
                    sess->Send(data, ID_DRAW_RSP);
                }
            });
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
            std::cout << "[Room " << _room_id << "] User left: " << uid
                << ". Total: " << _sessions.size() << std::endl;
            b_removed = true;

        }
    }   // 锁自动释放

    if (b_removed)
    {
        std::cout << "[Room " << _room_id << "] User left: " << uid << std::endl;

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
    return _owner_uid;
}