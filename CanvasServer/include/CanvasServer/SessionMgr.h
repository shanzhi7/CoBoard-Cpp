/***********************************************************************************
* @file         SessionMgr.h
* @brief        当前账号会话索引与旧连接清理保护
* @author       shanzhi
* @date         2026/01/30
* @history
***********************************************************************************/
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
	~SessionMgr(); // 释放会话索引。

	//添加会话，登录成功后调用
	void AddSession(int uid, std::shared_ptr<CSession> session); // 替换并在锁外关闭旧连接。

	//移除会话，用户断开连接或被踢出时调用
	void RemoveSession(int uid, const CSession* expected_session); // 仅移除预期连接，保护新登录。

	//获取会话
	std::shared_ptr<CSession> GetSession(int uid); // 返回当前账号会话快照。

private:
	SessionMgr(); // 初始化会话索引。
	std::mutex _mutex; // 保护账号会话索引。
	std::unordered_map<int, std::shared_ptr<CSession>> _uid_to_session; // 当前 UID 到会话的拥有引用。
};