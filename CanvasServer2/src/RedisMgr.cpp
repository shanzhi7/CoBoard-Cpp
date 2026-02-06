#include "CanvasServer/RedisMgr.h"
#include "CanvasServer/ConfigMgr.h"
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

        ConnectionPoolOptions pool_options;
        pool_options.size = 5; // 默认池大小

        // 使用 std::unique_ptr 管理指针，C++11 标准写法
        _redis.reset(new Redis(connection_options, pool_options));

        _redis->ping();
        std::cout << "Redis Connected successfully." << std::endl;
        return true;
    }
    catch (const Error& e)
    {
        std::cerr << "Redis Connect Error: " << e.what() << std::endl;
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
        std::cerr << "Redis Get Error: " << e.what() << std::endl;
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
        std::cerr << "Redis Set Error: " << e.what() << std::endl;
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
        std::cerr << "Redis Del Error: " << e.what() << std::endl;
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
        std::cerr << "Redis Exists Error: " << e.what() << std::endl;
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
        std::cerr << "Redis LPush Error: " << e.what() << std::endl;
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
        std::cerr << "Redis LPop Error: " << e.what() << std::endl;
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
        std::cerr << "Redis HSet Error: " << e.what() << std::endl;
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
        std::cerr << "Redis HGet Error: " << e.what() << std::endl;
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
    try
    {
        std::string key = ROOM_PREFIX + room_id;
        std::unordered_map<std::string, std::string> result;    //定义返回结果

        _redis->hgetall(key, std::inserter(result,result.begin()));
        if (result.empty())
        {
            std::cout << "[RedisMgr] GetRoomInfo Error: room not exists" << std::endl;
            return false;
        }

        if (result.count("width"))
        {
            room_info.width = std::stoi(result["width"]);
        }
        if (result.count("height"))
        {
            room_info.height = std::stoi(result["height"]);
        }
        if (result.count("owner_uid"))
        {
            room_info.owner_uid = std::stoi(result["owner_uid"]);
        }
        if (result.count("host"))
        {
            room_info.host = result["host"];
        }
        if (result.count("port"))
        {
            room_info.port = std::stoi(result["port"]);
        }
        room_info.name = result["name"];
        return true;
    }
    catch (const Error& e)
    {
        std::cerr << "[RedisMgr] GetRoomInfo Error: " << e.what() << std::endl;
        return false;
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

void RedisMgr::Close()
{
	_redis.reset();
}
