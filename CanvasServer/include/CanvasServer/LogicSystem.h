#pragma once
#include "Singleton.h"
#include "CSession.h" // 包含 LogicNode 和 CSession 定义
#include <map>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <json/json.h>
#include <chrono>
// 定义回调函数类型
typedef std::function<void(std::shared_ptr<CSession>, const short& msg_id, const std::string& msg_data)> FunCallBack;

class LogicSystem : public Singleton<LogicSystem>
{
	friend class Singleton<LogicSystem>;
public:
	~LogicSystem();

	// 发送消息到逻辑队列 (由 CSession 调用)
	void PostMsgToQue(std::shared_ptr<LogicNode> msg);

private:
	LogicSystem();

	// 工作线程主循环
	void DealMsg();

	// 注册回调
	void RegisterCallBacks();

	// 处理登录 (校验 Token)
	void HandleLogin(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);

	// 处理加入房间 (分配 Room)
	void HandleJoinRoom(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);

	//创建房间
	void HandleCreatRoom(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);

	// 群聊消息
    void HandleChat(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);

	std::thread _work_thread;							// 工作线程,用于回复客户端
	std::queue<std::shared_ptr<LogicNode>> _msg_queue;	// 消息队列，存放session与recvNode
	std::mutex _mutex;
	std::condition_variable _cond;
	bool _b_stop;

	std::map<short, FunCallBack> _fun_callbacks;		// 回调函数,打包回复数据，调用对应的session的send函数
};