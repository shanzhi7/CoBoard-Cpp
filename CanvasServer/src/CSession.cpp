#include "CanvasServer/CSession.h"
#include "CanvasServer/Room.h"
#include "CanvasServer/LogicSystem.h" 
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
	memset(&_head_buffer, 0, sizeof(MsgHead));	//��ʼ��ͷ��������
}

CSession::~CSession()
{

}

void CSession::Start()
{
	// ������ȡѭ��
	ReadHead();
}

//�����߼����̰߳�ȫ
void CSession::Send(const std::string& msg, short msg_id)
{
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

	//���췢�ͽڵ㣬�Զ�������С�˴��
	auto send_node = std::make_shared<SendNode>(msg.c_str(), msg.length(), msg_id);

	//�������
	bool b_need_start_write = false;
	{
		std::lock_guard<std::mutex> lock(_send_mutex);
		_send_queue.push(send_node);
		// �����ǰû�����ڽ��е�д����������Ҫ����
		if (_send_queue.size() == 1)
		{
			b_need_start_write = true;
		}

	}

	// �����첽д
	if (b_need_start_write)
	{
		// ʹ�� post ȷ�� HandleWrite �� socket ���ڵ� IO �߳�ִ��
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
		std::cout << "��CSesssion:��Write Error: " << error.message() << std::endl;
		Close();
		return;
	}

	//��ȡ��ͷ����
	std::shared_ptr<SendNode> msg_node;
	{
		std::lock_guard<std::mutex> lock(_send_mutex);
		if (_send_queue.empty())
		{
			return;
		}
        msg_node = _send_queue.front();
	}

	//ִ���첽д������������
	boost::asio::async_write(_socket,
		boost::asio::buffer(msg_node->_data, msg_node->_total_len),
		[this, self, msg_node](const boost::system::error_code& ec, std::size_t)
		{
			if (ec)
			{
				HandleWrite(ec, self); // ת������
				return;
			}

			bool need_continue = false;		//���ڱ���Ƿ����д����ֹ��Դ����
			// д��һ����������
			{
				std::lock_guard<std::mutex> lock(_send_mutex);
				_send_queue.pop();
				need_continue = !_send_queue.empty();
			}
			// ������У�����д
			if (need_continue)
			{
				HandleWrite(boost::system::error_code(), self);
			}
		});
}
//��ȡͷ��
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
				// �Զ˹رջ��������
				Close();
				return;
			}

			// ����ͷ�� (������ -> ������)
			short msg_id = boost::asio::detail::socket_ops::network_to_host_short(_head_buffer.msg_id);
			short msg_len = boost::asio::detail::socket_ops::network_to_host_short(_head_buffer.msg_len);

			// ��У��
			if (msg_len > MAX_LENGTH || msg_len < 0)
			{
				std::cout << "��ReadHead��Invalid msg length: " << msg_len << std::endl;
				Close();
				return;
			}

			// �� Body
			ReadBody(msg_id, msg_len);
		});
}

//��ȡ�䳤 Body(ֱ�Ӷ��� RecvNode)
void CSession::ReadBody(short msg_id, short msg_len)
{ 
	if (_b_close) return;
	auto self = shared_from_this();

	//�����ڴ�
	auto recv_node = std::make_shared<RecvNode>(msg_len, msg_id);

	//ֱ��д��ڵ��ڴ�
	boost::asio::async_read(_socket,
		boost::asio::buffer(recv_node->_data, msg_len),
		[this, self, recv_node, msg_id](const boost::system::error_code& ec, std::size_t bytes_transferred)
		{
			if (ec)
			{
				Close();
				return;
			}

			// ����ʵ�ʳ���
			recv_node->_cur_len = bytes_transferred;
			recv_node->_data[recv_node->_cur_len] = '\0';	// ������
			if (msg_id != ID_DRAW_REQ)
				std::cout << "revc msgid is :" << msg_id << std::endl;

			// ���ķ����߼�����������
			// 
			// ��ͨ������Ƶ�滭���ݣ����� LogicQueue��ֱ�ӹ㲥
			if (msg_id == ID_DRAW_REQ)
			{
				if (_uid == 0) //����У�飺�����¼
				{
					std::cout << "[CSession] DrawReq rejected: not logged in. SessionId=" << _session_id << std::endl;
					ReadHead();
					return;
				}

				auto room = _room.lock();	//weak ptr����
				if (!room)	//�����Ѿ����뷿��
				{
					std::cout << "[CSession] DrawReq rejected: not in room. UID=" << _uid << std::endl;
					ReadHead();
					return;
				}

				//���� protobuf (����У�� uid ��ֹα��)
				// �����Ȩ�޶��ף��ͻ���ֻ��ֻ������㣬�����Ƿ��������Ʊ����ɷ�����жϡ�
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

				//��ֹα�죺req.uid ������� session uid
				if (drawReq.uid() != _uid)
				{
					std::cout << "[CSession] DrawReq uid mismatch! SessionUID=" << _uid
						<< " ReqUID=" << drawReq.uid() << " -> Close()" << std::endl;
					// ����ȫ���ȡ�ֱ�ӶϿ�����
					Close();
					return;
				}

				//�㲥����������
				std::string rawBinary(recv_node->_data, recv_node->_cur_len);

				//--д�뷿���ڴ� history,��¼�� 
				// - Pen/Eraser: START + MOVE(flush) + END , - ����ͼ��: START + END��MOVE ��Ԥ�������� history��
				{
					const auto shape = drawReq.shape();	// ͼ������
					const auto cmd = drawReq.cmd();		// ��������

					const bool is_pen_like = (shape == message::SHAPE_PEN || shape == message::SHAPE_ERASER);
					bool should_record = false;			//����Ƿ�Ӧ�ø���

					if (is_pen_like)
					{
						// �ʼ���Ҫ MOVE ���ܸ�������
						should_record = (cmd == message::CMD_START ||
							cmd == message::CMD_MOVE ||
							cmd == message::CMD_END);
					}
					else
					{
						// ���Σ�ֻ�ط����ս��
						should_record = (cmd == message::CMD_START ||
							cmd == message::CMD_END);
					}
					if (should_record)	//��Ҫ���֣����ӵ���ʷ��¼ (�ʼ����Ͷ���Ҫ��ͼ�����Ͳ���ҪMOVE��MOVE��Ԥ��)
					{
						room->AppendHistory(rawBinary);
					}
				}

				// �����㲥�������ˣ������Ը��Լ���
				room->Broadcast(rawBinary, ID_DRAW_RSP, _uid);

				ReadHead();	//��ȡ��һ����ͷ
				return;
			}
			// ��ͨ����ҵ���߼� (��¼�����뷿��)���ӽ�����
			else
			{
				LogicSystem::getInstance()->PostMsgToQue(
					std::make_shared<LogicNode>(self, recv_node)
				);
			}

			// ������ȡ��һ����
			ReadHead();
		});
}

//��Դ����
void CSession::Close()
{ 
	// atomic exchange �Ὣ _b_close ��Ϊ true��������֮ǰ��ֵ
		// ���֮ǰ�Ѿ��� true��˵������߳����ڹأ��Ҿ�ֱ�ӷ���
	bool expected = false;
	if (!_b_close.compare_exchange_strong(expected, true))
	{
		return;
	}


	// �� SessionMgr �Ƴ� (��ֹ LogicServer ����ʱ�Ҳ���)
	if (_uid != 0)
	{
		SessionMgr::getInstance()->RemoveSession(_uid);
	}

	// �ӷ����Ƴ� (֪ͨ�������������)
	if (auto room = _room.lock())
	{
		room->Leave(_uid);
	}


	// �ر� socket
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
std::shared_ptr<Room> CSession::GetRoomLocked()
{
    return this->_room.lock();
}
