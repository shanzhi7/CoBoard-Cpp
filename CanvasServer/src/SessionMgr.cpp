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
	// 1. 在索引锁内替换连接，只复制旧引用。
	std::shared_ptr<CSession> previous;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		previous = _uid_to_session[uid];
		_uid_to_session[uid] = session;
	}
	// 2. 旧连接关闭在锁外执行，其延迟清理必须比较具体连接。
	if (previous && previous != session) previous->Close();
}

void SessionMgr::RemoveSession(int uid, const CSession* expected_session)
{
	// 1. 旧连接的断线事件不能移除同 UID 的新连接。
	std::lock_guard<std::mutex> lock(_mutex);
	auto iterator = _uid_to_session.find(uid);
	if (iterator != _uid_to_session.end() && iterator->second.get() == expected_session)
		_uid_to_session.erase(iterator);
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