/***********************************************************************************
* @file         LogicSystem.h
* @brief        业务消息队列和房间操作回调调度
* @author       shanzhi
* @date         2026/10/08
* @history
***********************************************************************************/
#pragma once
#include "Singleton.h"
#include "CSession.h" // 包含 LogicNode 和 CSession 定义
#include "ImageOperationHandler.h"
#include "TextOperationHandler.h"
#include "ItemCreateBatchHandler.h"
#include "RoomLifecycleHandler.h"
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
	~LogicSystem(); // 停止队列并回收业务工作线程。

	// 发送消息到逻辑队列 (由 CSession 调用)
	void PostSessionClosed(std::shared_ptr<CSession> session); // 将断线清理投递到现有业务队列。
	void PostMsgToQue(std::shared_ptr<LogicNode> msg); // 将会话消息加入线程安全业务队列。

private:
	LogicSystem(); // 注册回调并启动业务工作线程。

	// 工作线程主循环
	void DealMsg(); // 按收到顺序处理业务队列，停止且队列为空时退出。

	// 注册回调
	void RegisterCallBacks(); // 登记账号、房间及图元操作处理器。

	// 处理登录 (校验 Token)
	void HandleLogin(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data); // 校验登录令牌并绑定用户会话。

	// 处理加入房间 (分配 Room)
	void HandleJoinRoom(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data); // 加入房间并回放当前历史。

	//创建房间
	void HandleCreatRoom(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data); // 创建房间并登记共享元数据。

	// 群聊消息
    void HandleChat(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data); // 校验并广播房间聊天。

	// 授权用户编辑画板
	void HandleGrantEdit(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data); // 由房主授权成员编辑。

	// 取消用户编辑权限
	void HandleRevokeEdit(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data); // 由房主收回成员编辑权限。

	RoomLifecycleHandler _room_lifecycle_handler; // 生命周期编排和有界完成记录，不创建额外线程。
	std::thread _work_thread;							// 工作线程,用于回复客户端
	std::queue<std::shared_ptr<LogicNode>> _msg_queue;	// 消息队列，存放session与recvNode
	std::mutex _mutex;								// 保护逻辑消息队列，避免网络线程并发入队
	std::condition_variable _cond;						// 有新消息或停止请求时唤醒工作线程
	bool _b_stop;									// 工作线程退出标志，析构时设置并等待线程回收

	std::map<short, FunCallBack> _fun_callbacks;		// 回调函数,打包回复数据，调用对应的session的send函数
};
