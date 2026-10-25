#include "CanvasServer/CSession.h"
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <iostream>
#include "CanvasServer/Room.h"
#include "CanvasServer/LogicSystem.h"
#include "CanvasServer/ItemEditOperationHandler.h"
#include "CanvasServer/SessionMgr.h"
#include "CanvasServer/const.h"
#include "CanvasServer/message.pb.h"

static void DumpHex(const char* data, int len, int max_dump = 128)
{
	int n = std::min(len, max_dump);
	for (int i = 0; i < n; ++i)
	{
		unsigned char c = static_cast<unsigned char>(data[i]);
		std::cout << std::hex << (int)c << " ";
	}
	if (len > n) std::cout << "...";
	std::cout << std::dec << std::endl;
}

static void DumpHexTail(const char* data, int len, int tail = 32)
{
	int start = std::max(0, len - tail);
	for (int i = start; i < len; ++i)
	{
		unsigned char c = static_cast<unsigned char>(data[i]);
		std::cout << std::hex << (int)c << " ";
	}
	std::cout << std::dec << std::endl;
}

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
	// 1. 高频小包立即写出，避免 Nagle 合并拖动预览并引入额外显示抖动。
	boost::system::error_code error;
	_socket.set_option(boost::asio::ip::tcp::no_delay(true), error);
	if (error)
	{
		std::cout << "[CanvasServer] CSession::Start TCP_NODELAY failed uid=" << GetUserId()
			<< " error=" << error.message() << std::endl;
	}
	// 2. 选项失败仍沿用原连接接收流程，由正常网络错误路径负责关闭。
	ReadHead();
}

//发送逻辑，线程安全
void CSession::Send(const std::string& msg, short msg_id)
{
	// 1. 仅在关闭和协议长度校验通过后窄化正文长度，缓冲区容量保持 size_t。
	if (_b_close) return;
	if (msg.size() > 0xFFFF)
	{
		std::cout << "[CSession] Send too large, drop. size=" << msg.size()
			<< " msg_id=" << msg_id << std::endl;
		return;
	}

	if (msg.size() > 30000)
	{
		std::cout << "[CSession] huge send size=" << msg.size()
			<< " msg_id=" << msg_id << std::endl;
	}

	// 2. 创建完整发送节点并在互斥量保护下入队，帧头继续使用十六位长度。
	auto send_node = std::make_shared<SendNode>(msg.data(), static_cast<std::uint16_t>(msg.size()), msg_id);

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

	// 3. 只启动队首的异步写，后续节点由完成回调串行发送。
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

			bool need_continue = false;		//标记是否还有写操作，避免重复触发
			// 写完一个包，弹出
			{
				std::lock_guard<std::mutex> lock(_send_mutex);
				_send_queue.pop();
				need_continue = !_send_queue.empty();
			}
			// 如果还有，继续写
			if (need_continue)
			{
				HandleWrite(boost::system::error_code(), self);
			}
		});
}
//读取头部
void CSession::ReadHead()
{
	// 1. 关闭后不再分配新的异步读取，回调由共享会话保证生命周期。
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

			// 2. 正文长度是无符号十六位整数，允许完整的 64 KiB 协议范围。
			short msg_id = boost::asio::detail::socket_ops::network_to_host_short(_head_buffer.msg_id);
			const std::uint16_t msg_len = boost::asio::detail::socket_ops::network_to_host_short(_head_buffer.msg_len);

			// 简单校验
			if (msg_len > MAX_LENGTH)
			{
				std::cout << "【ReadHead】Invalid msg length: " << msg_len << std::endl;
				Close();
				return;
			}

			// 3. 完整校验帧头后再按声明长度读取正文。
			ReadBody(msg_id, msg_len);
		});
}

//读取变长 Body(直接读入 RecvNode)
void CSession::ReadBody(short msg_id, std::uint16_t msg_len)
{
	if (_b_close) return;
	auto self = shared_from_this();

	// 1. 每个包持有独立节点，异步读取完成后再按消息职责选择快慢通道。
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
			if (msg_id != ID_DRAW_REQ && msg_id != ID_ITEM_EDIT_REQ)
				std::cout << "revc msgid is :" << msg_id << std::endl;

			// 2. 绘画在房间内验证并提交，其他消息按编辑频率选择处理通道。
			//
			// 快通道：高频绘画数据，不进 LogicQueue，直接广播
			if (msg_id == ID_DRAW_REQ)
			{
				if (_uid == 0) //身份校验：未登录
				{
					std::cout << "[CSession] DrawReq rejected: not logged in. SessionId=" << _session_id << std::endl;
					ReadHead();
					return;
				}

				auto room = GetRoomLocked();	//weak ptr获取
				if (!room)	//用户还没加入房间
				{
					std::cout << "[CSession] DrawReq rejected: not in room. UID=" << _uid << std::endl;
					ReadHead();
					return;
				}

				//解析 protobuf (服务端校验 uid 防止伪造)
				// 做了权限兜底：客户端虽然只有画布可见，但是否有编辑权由服务端判断。
				if (!room->CanEdit(_uid))
				{
					std::cout << "[CSession] DrawReq rejected: no edit permission. UID=" << _uid
						<< " RoomId=" << room->GetRoomId() << std::endl;
					ReadHead();
					return;
				}

				message::DrawReq drawReq;
				if (!drawReq.ParseFromArray(recv_node->_data, recv_node->_cur_len))
				{
					std::cout << "[CSession] DrawReq parse failed. UID=" << _uid << std::endl;
					ReadHead();
					return;
				}

				//防止伪造：req.uid 必须等于 session uid
				if (drawReq.uid() != _uid)
				{
					std::cout << "[CSession] DrawReq uid mismatch! SessionUID=" << _uid
						<< " ReqUID=" << drawReq.uid() << " -> Close()" << std::endl;
					// 出于安全考虑，直接断开连接
					Close();
					return;
				}

				// 房间在同一锁内接受绘画、写历史并入发送队列，关闭后不会再接受旧操作。
				const std::string body(recv_node->_data, recv_node->_cur_len);
				room->AcceptDraw(self, drawReq, body);

				ReadHead();	//读取下一个消息头
				return;
			}
			// 图元预览和最终提交同走快通道，不能因业务队列排队降低拖动帧率。
			else if (msg_id == ID_ITEM_EDIT_REQ)
			{
				ItemEditOperationHandler::Handle(self, msg_id, std::string(recv_node->_data, recv_node->_cur_len));
			}
			// 登录、房间、聊天、权限和旧图片等低频业务继续进入 LogicSystem 队列。
			// 图片包不能在 Asio 读取线程直接修改 Room，否则会绕过队列顺序并与其他房间操作竞争。
			else
			{
				LogicSystem::getInstance()->PostMsgToQue(
					std::make_shared<LogicNode>(self, recv_node)
				);
			}

			// 3. 本次分发完成后继续读取下一个消息头。
			ReadHead();
		});
}

//资源清理
void CSession::Close()
{
	// 1. 原子标记关闭，重复回调不会重复投递清理事件。
	if (_b_close.exchange(true)) return;
	// 2. 房间和账号索引统一在业务队列中清理，IO 线程不访问业务容器。
	LogicSystem::getInstance()->PostSessionClosed(shared_from_this());
	// 3. 实际套接字关闭在它所属的 executor 执行，Token 不随 TCP 断开撤销。
	auto self = shared_from_this();
	boost::asio::post(_socket.get_executor(), [self]() {
		boost::system::error_code error;
		self->_socket.close(error);
	});
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
	// 1. 使用会话状态锁同步业务线程与 IO 线程。
	std::lock_guard<std::mutex> lock(_state_mutex);
	_room = room;
}
bool CSession::IsClosed()
{
	return _b_close;
}

std::string CSession::GetName()
{
	// 1. 在短锁内复制状态，不向调用者暴露受保护引用。
	std::lock_guard<std::mutex> lock(_state_mutex);
	return _name;
}
void CSession::SetName(const std::string& name)
{
	// 1. 使用会话状态锁同步业务线程与 IO 线程。
	std::lock_guard<std::mutex> lock(_state_mutex);
	_name = name;
}

void CSession::SetAvatarUrl(const std::string& avatar_url)
{
	// 1. 使用会话状态锁同步业务线程与 IO 线程。
	std::lock_guard<std::mutex> lock(_state_mutex);
	_avatar_url = avatar_url;
}

std::string CSession::GetAvatarUrl()
{
	// 1. 在短锁内复制状态，不向调用者暴露受保护引用。
	std::lock_guard<std::mutex> lock(_state_mutex);
	return _avatar_url;
}
std::shared_ptr<Room> CSession::GetRoomLocked()
{
	// 1. 在短锁内复制状态，不向调用者暴露受保护引用。
	std::lock_guard<std::mutex> lock(_state_mutex);
	return _room.lock();
}

void CSession::ClearRoom(const Room* expected_room)
{
	// 1. 延迟清理只解绑原房间，不影响后续绑定。
	std::lock_guard<std::mutex> lock(_state_mutex);
	if (_room.lock().get() == expected_room) _room.reset();
}

std::string CSession::GetToken()
{
	// 1. 内部认证校验只取得副本，凭据不进入日志。
	std::lock_guard<std::mutex> lock(_state_mutex);
	return _token;
}

void CSession::SetToken(const std::string& token)
{
	// 1. 只保存登录 handler 已验证的凭据。
	std::lock_guard<std::mutex> lock(_state_mutex);
	_token = token;
}
