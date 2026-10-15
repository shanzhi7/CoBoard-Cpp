#include "CanvasServer/RedisMgr.h"
#include "CanvasServer/ConfigMgr.h"
#include "Logger/Logger.h"
#include "CanvasServer/message.pb.h"
#include <unordered_map>
#include <iterator>

RedisMgr::RedisMgr()
{
    // 自动读取配置并连接，防止后续空指针
    auto& cfg = ConfigMgr::Inst();
    std::string host = cfg["Redis"]["Host"];
    int port = std::stoi(cfg["Redis"]["Port"]);
    std::string pwd = cfg["Redis"]["Password"];

    Connect(host, port, pwd);
}
RedisMgr::~RedisMgr()
{
}
//初始化连接
bool RedisMgr::Connect(const std::string& host, int port, const std::string& pwd)
{
    try
    {
        ConnectionOptions connection_options;
        connection_options.host = host;
        connection_options.port = port;
        connection_options.password = pwd;
        connection_options.keep_alive = true;
        // 1. Redis 故障必须在有限时间内返回，生命周期请求保留重试状态。
        connection_options.connect_timeout = std::chrono::milliseconds(1000);
        connection_options.socket_timeout = std::chrono::milliseconds(2000);

        ConnectionPoolOptions pool_options;
        pool_options.size = 5; // 默认池大小

        // 使用 std::unique_ptr 管理指针，C++11 标准写法
        _redis.reset(new Redis(connection_options, pool_options));

        _redis->ping();
        LOG_INFO_CTX("RedisMgr::Connect", "Redis 连接成功");
        return true;
    } catch (const Error& e)
    {
        LOG_ERROR_CTX("RedisMgr::Connect", "Redis 连接失败: " << e.what());
        return false;
    }
}

//获取key对应的value
bool RedisMgr::Get(const std::string& key, std::string& value)			
{
    try
    {
        // redis++ 的 get 返回的是 OptionalString (类似指针)
        auto val = _redis->get(key);
        if (val)
        {
            value = *val; // 解引用获取值
            return true;
        }
        return false; // key 不存在
    }
    catch (const Error& e)
    {
        LOG_ERROR_CTX("RedisMgr::Get", "Redis 查询失败: " << e.what());
        return false;
    }
}

//设置key对应的value
bool RedisMgr::Set(const std::string& key, const std::string& value, int timeout)
{
    try
    {
        if (timeout > 0)
        {
            // 有过期时间：SET key value EX timeout
            // redis-plus-plus 要求时间参数必须是 std::chrono 类型
            _redis->set(key, value, std::chrono::seconds(timeout));
        }
        else
        {
            // 无过期时间：SET key value
            _redis->set(key, value);
        }
        return true;
    }
    catch (const Error& e)
    {
        LOG_ERROR_CTX("RedisMgr::Set", "Redis 写入失败: " << e.what());
        return false;
    }
}

//删除key
bool RedisMgr::Del(const std::string& key)
{
    try
    {
        return _redis->del(key) > 0;
    }
    catch (const Error& e)
    {
        LOG_ERROR_CTX("RedisMgr::Del", "Redis 删除失败: " << e.what());
        return false;
    }
}

//判断key是否存在
bool RedisMgr::ExistsKey(const std::string& key)
{
    try
    {
        return _redis->exists(key);
    }
    catch (const Error& e)
    {
        LOG_ERROR_CTX("RedisMgr::ExistsKey", "Redis 判断 key 失败: " << e.what());
        return false;
    }
}

//List操作

//添加元素
bool RedisMgr::LPush(const std::string& key, const std::string& value)
{
    try
    {
        _redis->lpush(key, value);
        return true;
    }
    catch (const Error& e)
    {
        LOG_ERROR_CTX("RedisMgr::LPush", "Redis LPush 失败: " << e.what());
        return false;
    }
}

//获取元素，删除元素
bool RedisMgr::LPop(const std::string& key, std::string& value)	
{
    try
    {
        auto val = _redis->lpop(key);
        if (val)
        {
            value = *val;
            return true;
        }
        return false;
    }
    catch (const Error& e)
    {
        LOG_ERROR_CTX("RedisMgr::LPop", "Redis LPop 失败: " << e.what());
        return false;
    }
}

//HSet操作
//设置key对应的hkey对应的value
bool RedisMgr::HSet(const std::string& key, const std::string& hkey, const std::string& value)
{
    try
    {
        _redis->hset(key, hkey, value);
        return true;
    }
    catch (const Error& e)
    {
        LOG_ERROR_CTX("RedisMgr::HSet", "Redis HSet 失败: " << e.what());
        return false;
    }
}
//获取key对应的hkey对应的value
bool RedisMgr::HGet(const std::string& key, const std::string& hkey, std::string& value)
{
    try
    {
        auto val = _redis->hget(key, hkey);
        if (val)
        {
            value = *val;
            return true;
        }
        return false;
    }
    catch (const Error& e)
    {
        LOG_ERROR_CTX("RedisMgr::HGet", "Redis HGet 失败: " << e.what());
        return false;
    }
}
//创建房间信息
bool RedisMgr::CreateRoom(const std::string& room_id, const RoomInfo& room_info)
{
    if (!_redis) return false;
    try
    {
        std::string key = ROOM_PREFIX + room_id;

        // 使用 pipeline 管道技术，减少网络往返，一次性写入
        auto pipe = _redis->pipeline();

        pipe.hset(key, "name", room_info.name);
        pipe.hset(key, "owner_uid", std::to_string(room_info.owner_uid));
        pipe.hset(key, "host", room_info.host);
        pipe.hset(key, "port", std::to_string(room_info.port));
        pipe.hset(key, "width", std::to_string(room_info.width));
        pipe.hset(key, "height", std::to_string(room_info.height));

        // 设置 24 小时过期，防止僵尸房间占用 Redis
        pipe.expire(key, std::chrono::hours(24));

        pipe.exec(); // 提交执行

        std::cout << "[RedisMgr] Room info saved: " << room_id << std::endl;
        return true;
    }
    catch (const Error& e)
    {
        std::cerr << "[RedisMgr] CreateRoom Error: " << e.what() << std::endl;
        return false;
    }
}

bool RedisMgr::GetRoomInfo(const std::string& room_id, RoomInfo& room_info)
{
    // 1. 兼容已有读取入口；需要区分错误的生命周期流程使用 ReadRoomInfo。
    return ReadRoomInfo(room_id, room_info) == message::SUCCESS;
}

int RedisMgr::ReadRoomInfo(const std::string& room_id, RoomInfo& room_info)
{
	// 1. 读取完整元数据，空记录与通信失败分别返回。
	try
	{
		std::unordered_map<std::string, std::string> values;
		_redis->hgetall(std::string(ROOM_PREFIX) + room_id, std::inserter(values, values.begin()));
		if (values.empty()) return message::RoomNotExist;
		// 2. 恢复必须包含归属地址和房主，损坏记录不能被当作合法房间。
		room_info.id = room_id;
		room_info.name = values.at("name");
		room_info.owner_uid = std::stoi(values.at("owner_uid"));
		room_info.host = values.at("host");
		room_info.port = std::stoi(values.at("port"));
		if (values.count("width")) room_info.width = std::stoi(values.at("width"));
		if (values.count("height")) room_info.height = std::stoi(values.at("height"));
		return message::SUCCESS;
	} catch (const Error& error)
	{
		LOG_ERROR_CTX("RedisMgr::ReadRoomInfo", "读取失败 room_id=" << room_id << " error=" << error.what());
		return message::RedisErr;
	} catch (const std::exception& error)
	{
		LOG_ERROR_CTX("RedisMgr::ReadRoomInfo", "房间数据损坏 room_id=" << room_id << " error=" << error.what());
		return message::ServerInternalErr;
	}
}

bool RedisMgr::AddUserToRoom(const std::string& room_id, const std::string& uid)
{
    try
    {
        // key 建议设计成 "room_users:房间号"
        std::string key = ROOM_USERS_PREFIX + room_id;

        // SADD 命令：向集合添加元素
        _redis->sadd(key, uid);

        return true;
    }
    catch (const Error& e)
    {
        std::cerr << "Redis AddUserToRoom Error: " << e.what() << std::endl;
        return false;
    }
}

bool RedisMgr::RemoveUserFromRoom(const std::string& room_id, const std::string& uid)
{
    try
    {
        // 成员集合用于 GateServer 的只读授权判断；离开时执行 SREM，避免连接断开后继续持有房间资格。
        const std::string key = ROOM_USERS_PREFIX + room_id;
        _redis->srem(key, uid);
        return true;
    }
    catch (const Error& e)
    {
        std::cerr << "Redis RemoveUserFromRoom Error: " << e.what() << std::endl;
        return false;
    }
}

void RedisMgr::Close()
{
	_redis.reset();
}
int RedisMgr::VerifySessionToken(int uid, const std::string& token)
{
	// 1. 退出是低频操作，重新检查凭据，旧登录不能操作新登录创建的房间。
	try
	{
		const auto value = _redis->get(std::string(TOKEN_PREFIX) + token);
		return value && *value == std::to_string(uid) ? message::SUCCESS : message::TokenInvalid;
	} catch (const Error& error)
	{
		LOG_ERROR_CTX("RedisMgr::VerifySessionToken", "认证查询失败 uid=" << uid << " error=" << error.what());
		return message::RedisErr;
	}
}

int RedisMgr::DeleteRoom(const std::string& room_id, int owner_uid)
{
	// 1. 校验共享记录仍属于该房主，再一次删除元数据和成员资格。
	const std::string script = R"lua(
local owner = redis.call('HGET', KEYS[1], 'owner_uid')
if owner and owner ~= ARGV[1] then return -1 end
redis.call('DEL', KEYS[1], KEYS[2])
return 1
)lua";
	try
	{
		const auto result = _redis->eval<long long>(script,
		{std::string(ROOM_PREFIX) + room_id, std::string(ROOM_USERS_PREFIX) + room_id}, {std::to_string(owner_uid)});
		return result == 1 ? message::SUCCESS : message::NotOwner;
	} catch (const Error& error)
	{
		// 2. 超时可能已经执行，返回可重试错误，内存房间继续保持关闭中。
		LOG_ERROR_CTX("RedisMgr::DeleteRoom", "删除失败 room_id=" << room_id << " uid=" << owner_uid << " error=" << error.what());
		return message::RedisErr;
	}
}
