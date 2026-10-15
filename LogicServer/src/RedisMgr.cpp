#include "LogicServer/RedisMgr.h"
#include "LogicServer/ConfigMgr.h"
#include "Logger/Logger.h"
#include "Auth/TokenRevocation.h"
#include "LogicServer/message.pb.h"

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
        // 1. 为缓存操作设置有限超时，失败时业务能够返回明确结果。
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

void RedisMgr::Close()
{
	_redis.reset();
}
int RedisMgr::RevokeToken(int uid, const std::string& token)
{
	// 1. 脚本同时校验所属账号并条件删除 UID 索引，避免旧会话误删新登录。
	try
	{
		const auto result = _redis->eval<long long>(TokenRevocationScript(),
		{std::string(TOKEN_PREFIX) + token, std::string(UID_PREFIX) + std::to_string(uid)},
		{std::to_string(uid), token});
		return result == 1 ? message::SUCCESS : message::TokenInvalid;
	} catch (const Error& error)
	{
		// 2. 通信失败不能当作注销成功，日志只记录账号及错误上下文。
		LOG_ERROR_CTX("RedisMgr::RevokeToken", "注销失败 uid=" << uid << " error=" << error.what());
		return message::RedisErr;
	}
}
