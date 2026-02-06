#include "CanvasServer/CSession.h"
#include "CanvasServer/Room.h"
#include "CanvasServer/LogicSystem.h" 
#include "CanvasServer/SessionMgr.h"

CSession::CSession(boost::asio::io_context& io_context)
	:_socket(io_context),
	_session_id(""),
	_uid(0),
	_b_close(false)
{
	boost::uuids::uuid a_uuid = boost::uuids::random_generator()();
	_session_id = boost::uuids::to_string(a_uuid);
	memset(&_head_buffer, 0, sizeof(MsgHead));	//初始化头部缓冲区
}

CSession::~CSession()
{

}

void CSession::Start()
{
	// 启动读取循环
	ReadHead();
}

//发送逻辑，线程安全
void CSession::Send(const std::string& msg, short msg_id)
{
	if (_b_close) return;
	//构造发送节点，自动处理大小端打包
	auto send_node = std::make_shared<SendNode>(msg.c_str(), msg.length(), msg_id);

	//加锁入队
	bool b_need_start_write = false;
	{
		std::lock_guard<std::mutex> lock(_send_mutex);
		_send_queue.push(send_node);
		// 如果当前没有正在进行的写操作，则需要触发
		if (_send_queue.size() == 1)
		{
			b_need_start_write = true;
		}

	}

	// 触发异步写
	if (b_need_start_write)
	{
		// 使用 post 确保 HandleWrite 在 socket 所在的 IO 线程执行
		auto self = shared_from_this();
		boost::asio::post(_socket.get_executor(), [this, self]() {
			HandleWrite(boost::system::error_code(), self);
			});
	}
}

void CSession::HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> self)
{
	if (error)
	{
		std::cout << "【CSesssion:】Write Error: " << error.message() << std::endl;
		Close();
		return;
	}

	//获取队头数据
	std::shared_ptr<SendNode> msg_node;
	{
		std::lock_guard<std::mutex> lock(_send_mutex);
		if (_send_queue.empty())
		{
			return;
		}
        msg_node = _send_queue.front();
	}

	//执行异步写操作发送数据
	boost::asio::async_write(_socket,
		boost::asio::buffer(msg_node->_data, msg_node->_total_len),
		[this, self, msg_node](const boost::system::error_code& ec, std::size_t)
		{
			if (ec)
			{
				HandleWrite(ec, self); // 转发错误
				return;
			}

			// 写完一个包，弹出
			{
				std::lock_guard<std::mutex> lock(_send_mutex);
				_send_queue.pop();
				// 如果还有，继续写
				if (!_send_queue.empty())
				{
					HandleWrite(boost::system::error_code(), self);
				}
			}
		});
}
//读取头部
void CSession::ReadHead()
{
	if (_b_close)
	{
		return;
	}
	auto self = shared_from_this();

	boost::asio::async_read(_socket,
		boost::asio::buffer(&_head_buffer, sizeof(MsgHead)),
		[this, self](const boost::system::error_code& ec, std::size_t)
		{
			if (ec)
			{
				// 对端关闭或网络错误
				Close();
				return;
			}

			// 解析头部 (网络序 -> 主机序)
			short msg_id = boost::asio::detail::socket_ops::network_to_host_short(_head_buffer.msg_id);
			short msg_len = boost::asio::detail::socket_ops::network_to_host_short(_head_buffer.msg_len);

			// 简单校验
			if (msg_len > MAX_LENGTH || msg_len < 0)
			{
				std::cout << "【ReadHead】Invalid msg length: " << msg_len << std::endl;
				Close();
				return;
			}

			// 读 Body
			ReadBody(msg_id, msg_len);
		});
}

//读取变长 Body(直接读入 RecvNode)
void CSession::ReadBody(short msg_id, short msg_len)
{ 
	if (_b_close) return;
	auto self = shared_from_this();

	//申请内存
	auto recv_node = std::make_shared<RecvNode>(msg_len, msg_id);

	//直接写入节点内存
	boost::asio::async_read(_socket,
		boost::asio::buffer(recv_node->_data, msg_len),
		[this, self, recv_node, msg_id](const boost::system::error_code& ec, std::size_t bytes_transferred)
		{
			if (ec)
			{
				Close();
				return;
			}

			// 设置实际长度
			recv_node->_cur_len = bytes_transferred;
			recv_node->_data[recv_node->_cur_len] = '\0';	// 结束符
			std::cout<<"revc msgid is :"<<msg_id<<std::endl;

			//todo...

			// 核心分流逻辑：快慢分离
			// 
			// A. 快通道：高频绘画数据，不进 LogicQueue，直接广播
			if (msg_id == ID_DRAW_REQ_DEL)
			{
				// 如果加入了房间，直接广播
				if (auto room = _room.lock())
				{
					// 转成 string 只是为了方便 Broadcast 接口，实际内部也是 memcpy
					// 如果追求极致，Room::Broadcast 可以接收 RecvNode 指针
					//room->Broadcast(std::string(recv_node->_data, recv_node->_cur_len), _uid);
				}
			}
			// B. 慢通道：业务逻辑 (登录、加入房间)，扔进队列
			else
			{
				LogicSystem::getInstance()->PostMsgToQue(
					std::make_shared<LogicNode>(self, recv_node)
				);
			}

			// 继续读取下一个包
			ReadHead();
		});
}

//资源清理
void CSession::Close()
{ 
	// atomic exchange 会将 _b_close 设为 true，并返回之前的值
		// 如果之前已经是 true，说明别的线程正在关，我就直接返回
	bool expected = false;
	if (!_b_close.compare_exchange_strong(expected, true))
	{
		return;
	}


	// 从 SessionMgr 移除 (防止 LogicServer 踢人时找不到)
	if (_uid != 0)
	{
		SessionMgr::getInstance()->RemoveSession(_uid);
	}

	// 从房间移除 (通知房间里的其他人)
	if (auto room = _room.lock())
	{
		room->Leave(_uid);
	}


	// 关闭 socket
	boost::system::error_code ec;
	_socket.close(ec);

	std::cout << "Session closed, UID: " << _uid << std::endl;
}

boost::asio::ip::tcp::socket& CSession::GetSocket()
{
	return this->_socket;
}

std::string& CSession::GetSessionId()
{
	return this->_session_id;
}

void CSession::SetUserId(int uid)
{
	this->_uid = uid;
}

int CSession::GetUserId()
{
	return this->_uid;
}

void CSession::SetRoom(std::shared_ptr<Room> room)
{
	this->_room = room;
}
bool CSession::IsClosed()
{
	return _b_close;
}

std::string CSession::GetName()
{
	return _name;
}
void CSession::SetName(const std::string& name)
{
	_name = name;
}

void CSession::SetAvatarUrl(const std::string& avatar_url)
{
	_avatar_url = avatar_url;
}

std::string CSession::GetAvatarUrl()
{
	return this->_avatar_url;
}