#include "GateServer/RedisMgr.h"

RedisMgr::RedisMgr()
{
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
bool RedisMgr::Set(const std::string& key, const std::string& value)
{
    try
    {
        _redis->set(key, value);
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

void RedisMgr::Close()
{
	_redis.reset();
}
