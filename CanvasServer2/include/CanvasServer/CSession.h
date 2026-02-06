#pragma once
#include <boost/asio.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <queue>
#include <mutex>
#include <memory>
#include <atomic>
#include "MsgNode.h"

class CServer;
class Room;
class CSession : public std::enable_shared_from_this<CSession>
{ 
public:
	CSession(boost::asio::io_context& io_context);		// 构造函数
	~CSession();

	boost::asio::ip::tcp::socket& GetSocket();			// 获取socket
	std::string& GetSessionId();						// 获取sessionId
	void SetUserId(int uid);							// 设置用户id
	int GetUserId();									// 获取用户id
	std::string GetName();								// 获取用户名
	std::string GetAvatarUrl();							// 获取用户头像
	bool IsClosed();									// 判断是否关闭

	// 绑定房间 (使用 weak_ptr 防止循环引用)
	void SetRoom(std::shared_ptr<Room> room);
	void SetName(const std::string& name);
    void SetAvatarUrl(const std::string& avatar_url);

	void Start();
	void Send(const std::string& msg, short msg_id);
	void Close();
private:


	// 核心读取逻辑
	void ReadHead();
	void ReadBody(short msg_id, short msg_len);
	void HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> self);// 异步写回调

	boost::asio::ip::tcp::socket _socket;	// 套接字
	std::string _session_id;				// session id
	int _uid;								// 用户id
	std::string _name;						// 用户名
	std::string _avatar_url;				// 用户头像
	std::atomic<bool> _b_close;							// 是否关闭

	// 头部缓存
	MsgHead _head_buffer;

	// 房间引用
	std::weak_ptr<Room> _room;

	// 发送队列(线程安全守护)
	std::queue<std::shared_ptr<SendNode>> _send_queue;
	std::mutex _send_mutex;
};

// 逻辑节点 (用于投递给 LogicSystem)
class LogicNode
{
public:
	LogicNode(std::shared_ptr<CSession> session, std::shared_ptr<RecvNode> recvnode)
		: _session(session), _recvnode(recvnode)
	{
	}
	std::shared_ptr<CSession> _session;
	std::shared_ptr<RecvNode> _recvnode;
};