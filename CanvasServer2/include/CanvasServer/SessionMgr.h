#pragma once
#include "CanvasServer/Singleton.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <iostream>
class CSession;

class SessionMgr : public Singleton<SessionMgr>
{
	friend class Singleton<SessionMgr>;
public:
	~SessionMgr();

	//添加会话，登录成功后调用
	void AddSession(int uid, std::shared_ptr<CSession> session);

	//移除会话，用户断开连接或被踢出时调用
	void RemoveSession(int uid);

	//获取会话
	std::shared_ptr<CSession> GetSession(int uid);

private:
	SessionMgr();
	std::mutex _mutex;
	std::unordered_map<int, std::shared_ptr<CSession>> _uid_to_session;
};