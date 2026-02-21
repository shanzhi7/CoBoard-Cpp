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

void Room::Join(std::shared_ptr<CSession> session)
{

    if (session->IsClosed())
    {
        std::cout << "[Room] Join failed: Session is closed. UID: " << session->GetUserId() << std::endl;
        return;
    }

    int uid = session->GetUserId();
    if (uid == 0) // 未登录用户不允许加入
    {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _sessions[uid] = session;
        session->SetRoom(shared_from_this());   //这样 Session 断开时知道通知哪个房间,session有room的弱指针

        std::cout << "[Room " << _room_id << "] User joined: " << uid
            << ". Total: " << _sessions.size() << std::endl;
    }

    // 双重保险,加入后再查一次
    // 如果刚才加入的过程中那边断开了，现在赶紧把他踢出去
    if (session->IsClosed())
    {
        _sessions.erase(uid); // 立即回滚
        return;
    }

    // 通知其他用户，更新客户端ui
    BroadcastUserEnter(session);

    //todo...
    // 让他看到之前别人画的东西
    //if (!_history.empty())
    //{
    //    // 这里的 ID_DRAW_REQ 假设是画画的数据包 ID
    //    // 在实际业务中，可能需要把多条历史打包成一个包发送，这里简单起见逐条发
    //    for (const auto& draw_data : _history)
    //    {
    //        session->Send(draw_data, ID_DRAW_REQ);
    //    }
    //}
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

            //todo...
            //通知其他用户，更新客户端ui
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
    std::lock_guard<std::mutex> lock(_mutex);

    //todo...(后续阶段)：房间历史记录
        // 现在先不存，先把实时同步跑通
    // if (msg_id == ID_DRAW_RSP) { ... }

    // 遍历发送
    for (auto& pair : _sessions)
    {
        int target_uid = pair.first;
        auto target_session = pair.second;

        // 跳过发送者自己 (因为他本地已经画出来了，不需要服务器回显)
        if (target_uid == exclude_uid) continue;
        if (!target_session) continue;
        if (target_session->IsClosed()) continue;

        // 发送
        target_session->Send(data, msg_id);
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