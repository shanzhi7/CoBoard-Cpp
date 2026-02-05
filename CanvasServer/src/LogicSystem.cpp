#include "CanvasServer/LogicSystem.h"
#include "CanvasServer/SessionMgr.h"
#include "CanvasServer/RoomMgr.h"
#include "CanvasServer/RedisMgr.h"
#include "CanvasServer/const.h"      // 包含 MSG_IDS 定义
#include "CanvasServer/message.pb.h"
#include "CanvasServer/RoomMgr.h"
#include "CanvasServer/Room.h"
#include "CanvasServer/ConfigMgr.h"
#include <iostream>
#include <random>

// 辅助函数：生成 6 位随机数
std::string GenRandomId()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);
    return std::to_string(dis(gen));
}

LogicSystem::LogicSystem() : _b_stop(false)
{
	RegisterCallBacks();	// 注册回调函数
	_work_thread = std::thread(&LogicSystem::DealMsg, this);	//启动工作线程
}

LogicSystem::~LogicSystem()
{
    _b_stop = true;
    _cond.notify_one();
    if (_work_thread.joinable())
    {
        _work_thread.join();
    }
}

void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> msg)
{
    // 生产者写法
    std::unique_lock<std::mutex> lock(_mutex);
    _msg_queue.push(msg);
    if (_msg_queue.size() == 1)
    {
        lock.unlock(); // 提早解锁
        _cond.notify_one();
    }
}

void LogicSystem::DealMsg()
{
    while (true)    // 死循环,处理需要发送的消息
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _cond.wait(lock, [this](){
            return _b_stop || !_msg_queue.empty();
            });

        if (_b_stop && _msg_queue.empty())
        {
            break;
        }

        //取出头部消息
        auto msg_node = _msg_queue.front();
        _msg_queue.pop();
        lock.unlock();  // 处理消息时释放锁，允许新消息入队

        //查找回调函数
        auto it = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
        if (it != _fun_callbacks.end())
        {
            // 执行业务逻辑
            // RecvNode 里的 _data 是 char*，转 string 传给业务层方便解析 Json
            it->second(msg_node->_session,
                        msg_node->_recvnode->_msg_id,
                        std::string(msg_node->_recvnode->_data,msg_node->_recvnode->_cur_len));
        }
        else
        {
            std::cout << "【LogicSystem】: Unknown msg id " << msg_node->_recvnode->_msg_id << std::endl;
        }
    }
}

void LogicSystem::RegisterCallBacks()
{
    // 注册登录
    _fun_callbacks[ID_CANVAS_LOGIN_REQ] = std::bind(&LogicSystem::HandleLogin, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

    // 注册进房
    _fun_callbacks[ID_JOIN_ROOM_REQ] = std::bind(&LogicSystem::HandleJoinRoom, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

    //注册创建房间
    _fun_callbacks[ID_CREAT_ROOM_REQ] = std::bind(&LogicSystem::HandleCreatRoom, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

    //注册加入房间
    _fun_callbacks[ID_JOIN_ROOM_REQ] = std::bind(&LogicSystem::HandleJoinRoom, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

    //不需要注册 ID_DRAW_REQ (画画请求)
    // 画画请求在 CSession 层直接被拦截转发了，不会进这个队列
}

void LogicSystem::HandleLogin(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data)
{
    Json::Reader reader;
    Json::Value root;
    Json::Value rsp;

    rsp["error"] = message::ErrorCodes::Error_Json; // 默认错误码

    //作用域结束发送回包
    Defer defer([this, session, &rsp]() {
        std::string return_str = rsp.toStyledString();
        session->Send(return_str, ID_CANVAS_LOGIN_RSP);

        // 如果是严重错误（如认证失败），发完包后可以考虑打印日志
        int err = rsp["error"].asInt();
        if (err != message::ErrorCodes::SUCCESS) {
            std::cout << "[CanvasServer] Login Error Response sent. Code: " << err << std::endl;
        }
        });


        if (!reader.parse(msg_data, root))
        {
            // 解析失败，直接 return
            // 此时会触发 Defer，发送 Error_Json 给客户端
            std::cout << "[CanvasServer] Login parse json failed." << std::endl;
            return;
        }
        if (!root.isMember("uid") || !root.isMember("token"))   //检测字段是否存在
        {
            rsp["error"] = message::ErrorCodes::Error_Json;
            std::cout << "[CanvasServer] Login Error: Missing field." << std::endl;
            return;
        }

        int client_uid = root["uid"].asInt();
        std::string client_token = root["token"].asString();

        //构造redis key
        std::string token_key = TOKEN_PREFIX + client_token;

        //查找redis，有没有该token对应的uid
        std::string valid_uid_str;
        bool b_exist = RedisMgr::getInstance()->Get(token_key, valid_uid_str);

        //token过期或者错误
        if (!b_exist)
        {
            rsp["error"] = message::ErrorCodes::TokenInvalid;
            std::cout << "[CanvasServer] Token not found in Redis. Client UID: " << client_uid << std::endl;
            return; //触发Defer发送 TokenInvalid
        }

        //token存在，但是uid是别人的
        if (std::stoi(valid_uid_str) != client_uid)
        {
            rsp["error"] = message::ErrorCodes::TokenInvalid;
            std::cout << "[CanvasServer] Token mismatch! Redis UID: " << valid_uid_str << " Client UID: " << client_uid << std::endl;
            return; // 触发 Defer 发送 TokenInvalid
        }

        // 校验成功！
        session->SetUserId(client_uid);     //绑定UID到Session

        SessionMgr::getInstance()->AddSession(client_uid, session);  //注册到全局管理器(SessionMgr)

        // 构造成功回包
        rsp["error"] = message::ErrorCodes::SUCCESS;
        rsp["uid"] = client_uid;

        std::cout << "[CanvasServer] User Login Success: " << client_uid << std::endl;

        // 函数结束，Defer 自动析构，发送 SUCCESS 包
}

void LogicSystem::HandleCreatRoom(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data)
{
    Json::Reader reader;
    Json::Value root;
    Json::Value rsp;

    rsp["error"] = message::ErrorCodes::Error_Json;
    Defer defer([this, session, &rsp]() {
        std::string return_str = rsp.toStyledString();
        session->Send(return_str, ID_CREAT_ROOM_RSP);
        });

    if (!reader.parse(msg_data, root))  //解析失败
    {
        std::cout << "[CanvasServer] Login parse json failed." << std::endl;
        return;
    }

    // 提取参数
    if (!root.isMember("room_name") || !root.isMember("owner_uid"))
    {
        std::cout << "[CanvasServer] Missing fields in CreateRoom." << std::endl;
        return;
    }

    std::string room_name = root["room_name"].asString();
    int owner_uid = root["owner_uid"].asInt();

    //生成唯一的RoomId (防碰撞逻辑)
    std::string room_id;
    int retry_count = 0;    // 重试次数
    bool is_unique = false;

    //最多尝试5次，防止redis 挂掉
    while (retry_count < 5)
    {
        room_id = GenRandomId();
        std::string room_id_key = ROOM_PREFIX + room_id;
        // 检查room_id是否已经存在
        if (!RedisMgr::getInstance()->ExistsKey(room_id_key))
        {
            is_unique = true;
            break;
        }
        retry_count++;
    }

    if (!is_unique)
    {
        rsp["error"] = message::ErrorCodes::RoomCreateFailed; // 生成 ID 失败
        std::cout << "[LogicSystem] Failed to generate unique Room ID after retries." << std::endl;
        return;
    }

    //内存创建房间
    auto room = RoomMgr::getInstance()->GetOrCreateRoom(room_id);
    if (!room)
    {
        rsp["error"] = message::ErrorCodes::RoomCreateFailed;
        return;
    }
    room->SetRoomInfo(room_name, owner_uid);

    //Redis注册房间
    auto& cfg = ConfigMgr::Inst();
    std::string self_host = cfg["SelfServer"]["Host"];
    if (self_host == "0.0.0.0")
    {
        self_host = "127.0.0.1";        //修改...
    }
    int self_port = std::stoi(cfg["SelfServer"]["Port"]);

    RoomInfo room_info;
    room_info.id = room_id;
    room_info.name = room_name;
    room_info.owner_uid = owner_uid;
    room_info.host = self_host;
    room_info.port = self_port;
    room_info.width = root["width"].asInt();
    room_info.height = root["height"].asInt();

    //写入redis
    bool b_redis = RedisMgr::getInstance()->CreateRoom(room_id, room_info); //CreateRoom 内部使用了 pipeline 和 expire
    if (!b_redis)
    {
        rsp["error"] = message::ErrorCodes::RoomCreateFailed;
        std::cout << "[LogicSystem] Failed to register room in Redis." << std::endl;
        return;
    }

    //成功返回
    rsp["error"] = message::ErrorCodes::SUCCESS;
    rsp["room_id"] = room_id;
    rsp["room_name"] = room_name;
    rsp["room_host"] = self_host;
    rsp["room_port"] = self_port;
    rsp["owner_uid"] = owner_uid;
    rsp["width"] = room_info.width;
    rsp["height"] = room_info.height;

    std::cout << "[CanvasServer] Create Room Success! ID: " << room_id
        << " Name: " << room_name
        << " Owner: " << owner_uid << std::endl;
}

void LogicSystem::HandleJoinRoom(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data)
{
    //解析请求包
    message::JoinRoomReq req;
    message::JoinRoomRsp rsp;
    rsp.set_error(message::ErrorCodes::Error_Json); //默认错误，解析失败

    Defer defer([session, &rsp]() {                 //回包
        std::string sendData;
        if (rsp.SerializeToString(&sendData))
        {
            session->Send(sendData,ID_JOIN_ROOM_RSP);
        }
        else
        {
            std::cerr << "[HandleJoinRoom]: Serialize JoinRoomRsp failed!" << std::endl;
        }
    });
    if (!req.ParseFromString(msg_data))
    {
        std::cerr << "[HandleJoinRoom]: Parse error" << std::endl;
        return;
    }

    std::string uid = std::to_string(req.uid());    //加入者id
    std::string room_id = req.room_id();
    std::cout << "Recv JoinRoomReq: User[" << uid << "] -> Room[" << room_id << "]" << std::endl;

    //查redis
    RoomInfo room_info;
    bool b_redis = RedisMgr::getInstance()->GetRoomInfo(room_id, room_info);

    //房间不存在
    if (!b_redis)
    {
        rsp.set_error(message::ErrorCodes::RoomNotExist);
        return;
    }

    //读取本机地址并且比较
    std::string self_host = ConfigMgr::Inst()["SelfServer"]["Host"];
    std::string self_port = ConfigMgr::Inst()["SelfServer"]["Port"];
    if (self_host == "0.0.0.0")
    {
        self_host = "127.0.0.1";
    }
    if (room_info.host != self_host || std::to_string(room_info.port) != self_port)
    {
        std::cout << "[HandleJoinRoom]:Redirect: Target is [" << room_info.host << ":" << room_info.port
            << "] Self is [" << self_host << ":" << self_host << "]" << std::endl;

        rsp.set_error(message::ErrorCodes::NeedRedirect);   //设置重定向错误

        //将必要的重定向数据发给客户端
        rsp.set_redirect_host(room_info.host);
        rsp.set_redirect_port(room_info.port);
        rsp.set_room_id(room_id);
        return;
    }

    //正常加入，房主跟加入者在本机
    bool addSuccess = RedisMgr::getInstance()->AddUserToRoom(room_id, uid);
    if (!addSuccess)
    {
        rsp.set_error(message::ErrorCodes::RoomJoinFailed);
        return;
    }
    auto room = RoomMgr::getInstance()->GetRoom(room_id);               //获取房间
    if (!room)      //房间不存在RoomMgr
    {
        std::cerr << "[HandleJoinRoom] Critical: Room exists in Redis but failed to load in Memory!" << std::endl;
        rsp.set_error(message::ErrorCodes::ServerInternalErr);
        return;
    }
    auto user_session = SessionMgr::getInstance()->GetSession(std::stoi(uid));
    if (user_session)
    {
        room->Join(user_session);       //内存中加入房间
    }
    else
    {
        // 极端情况：处理请求时 Session 断了
        std::cerr << "[HandleJoinRoom] User session not found" << std::endl;
        rsp.set_error(message::ErrorCodes::LoginErr);
        return;
    }


    //回包

    //todo... 返回房间用户列表
    rsp.set_error(message::ErrorCodes::SUCCESS);
    rsp.set_room_name(room_info.name);
    rsp.set_owner_uid(room_info.owner_uid);
    rsp.set_canvas_width(room_info.width);
    rsp.set_canvas_height(room_info.height);
    rsp.set_room_id(room_id);
    rsp.set_redirect_host(room_info.host);
    rsp.set_redirect_port(room_info.port);

    std::cout << "Join Success! User[" << uid << "] entered Room[" << room_id << "]" << std::endl;
}