/***********************************************************************************
* @file         CSession.h
* @brief        TCP 会话、同步房间绑定和异步消息队列
* @author       shanzhi
* @date         2026/01/30
* @history
***********************************************************************************/
#pragma once

#include <boost/asio.hpp>
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include "MsgNode.h"

class CServer;
class Room;
class CSession : public std::enable_shared_from_this<CSession>
{
public:
	CSession(boost::asio::io_context& io_context); // 创建由指定 IO 上下文驱动的会话。
	~CSession(); // 回收套接字和未发送消息。
	boost::asio::ip::tcp::socket& GetSocket(); // 返回连接套接字。
	std::string& GetSessionId(); // 返回连接唯一标识。
	void SetUserId(int uid); // 发布通过认证的账号 ID。
	int GetUserId(); // 读取认证账号 ID，未认证为零。
	std::string GetName(); // 复制当前账号名称。
	std::string GetAvatarUrl(); // 复制头像引用。
	std::string GetToken(); // 复制认证凭据，仅供服务器内部校验。
	void SetToken(const std::string& token); // 保存已经校验的认证凭据。
	std::shared_ptr<Room> GetRoomLocked(); // 同步复制当前房间引用。
	void SetRoom(std::shared_ptr<Room> room); // 绑定房间弱引用，不拥有房间。
	void ClearRoom(const Room* expected_room); // 仅解除预期房间，避免清理旧房间时误解绑新房间。
	bool IsClosed(); // 判断关闭流程是否已经启动。
	void SetName(const std::string& name); // 保存账号名称。
	void SetAvatarUrl(const std::string& avatar_url); // 保存头像引用。
	void Start(); // 启动异步接收。
	void Send(const std::string& msg, short msg_id); // 串行入队，实际网络写入由 IO 线程完成。
	void Close(); // 幂等关闭连接，并投递业务清理。
private:
	void ReadHead(); // 读取并校验四字节帧头。
	void ReadBody(short msg_id, std::uint16_t msg_len); // 分发绘画、图元编辑快通道或普通业务。
	void HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> self); // 驱动串行发送队列。
	boost::asio::ip::tcp::socket _socket; // 当前连接套接字。
	std::string _session_id; // 连接唯一标识。
	std::atomic<int> _uid; // 认证账号 ID，跨 IO 与业务线程发布。
	std::string _name; // 账号名称。
	std::string _avatar_url; // 头像引用。
	std::string _token; // 本次登录凭据，禁止记录到日志。
	std::atomic<bool> _b_close; // 关闭操作的幂等标志。
	MsgHead _head_buffer; // 接收帧头缓存。
	std::weak_ptr<Room> _room; // 非拥有的当前房间引用。
	std::mutex _state_mutex; // 保护房间引用和认证资料。
	std::queue<std::shared_ptr<SendNode>> _send_queue; // 待发送消息队列。
	std::mutex _send_mutex; // 保护发送队列。
};

class LogicNode
{
public:
	LogicNode(std::shared_ptr<CSession> session, std::shared_ptr<RecvNode> recvnode) // 保存业务消息或断线事件。
		: _session(std::move(session)), _recvnode(std::move(recvnode))
	{
		// 1. 空接收节点表示内部断线事件，不占用客户端消息编号。
	}
	std::shared_ptr<CSession> _session; // 事件处理完成前保持会话存活。
	std::shared_ptr<RecvNode> _recvnode; // 客户端消息；内部断线事件为空。
};
