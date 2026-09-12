#include "CanvasServer/SessionMgr.h"
#include "CanvasServer/CSession.h"

SessionMgr::SessionMgr()
{

}

SessionMgr::~SessionMgr()
{
    // 析构时清空所有会话
    std::lock_guard<std::mutex> lock(_mutex);
    _uid_to_session.clear();
}

void SessionMgr::AddSession(int uid, std::shared_ptr<CSession> session)
{
    std::lock_guard<std::mutex> lock(_mutex);

    // 如果该 UID 已经存在，覆盖它 (顶号逻辑的一种简单实现)
    // 同时也意味着旧的 session 引用计数会 -1
    _uid_to_session[uid] = session;

    std::cout << "[SessionMgr] User " << uid << " registered. Total: " << _uid_to_session.size() << std::endl;
}

void SessionMgr::RemoveSession(int uid)
{
    std::lock_guard<std::mutex> lock(_mutex);

    // 查找并删除
    auto it = _uid_to_session.find(uid);
    if (it != _uid_to_session.end())
    {
        _uid_to_session.erase(it);
        std::cout << "[SessionMgr] User " << uid << " removed. Total: " << _uid_to_session.size() << std::endl;
    }
}

std::shared_ptr<CSession> SessionMgr::GetSession(int uid)
{
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _uid_to_session.find(uid);
    if (it != _uid_to_session.end())
    {
        return it->second;
    }

    return nullptr; // 没找到返回空指针
}