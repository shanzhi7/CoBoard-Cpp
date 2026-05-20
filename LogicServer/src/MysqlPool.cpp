#include "LogicServer/MysqlPool.h"

#include <jdbc/cppconn/statement.h>

MysqlPool::MysqlPool(std::string url, std::string user, std::string password, std::string schema, int poolSize)
    : _url(std::move(url)),
      _user(std::move(user)),
      _password(std::move(password)),
      _schema(std::move(schema)),
      _poolSize(poolSize),
      _isClosed(false)
{
    try
    {
        for (int i = 0; i < _poolSize; ++i)
        {
            _pool.push(createConnection());
        }
    }
    catch (const sql::SQLException& e)
    {
        std::cerr << "[MysqlPool] init failed: " << e.what() << std::endl;
    }
}

MysqlPool::~MysqlPool()
{
    std::lock_guard<std::mutex> lock(_mutex);
    while (!_pool.empty())
    {
        _pool.pop();
    }
}

std::unique_ptr<sql::Connection> MysqlPool::createConnection()
{
    sql::mysql::MySQL_Driver* driver = sql::mysql::get_driver_instance();
    std::unique_ptr<sql::Connection> conn(driver->connect(_url, _user, _password));
    conn->setSchema(_schema);
    return conn;
}

bool MysqlPool::isConnectionAlive(sql::Connection* conn)
{
    if (conn == nullptr)
    {
        return false;
    }

    try
    {
        if (conn->isClosed())
        {
            return false;
        }

        std::unique_ptr<sql::Statement> stmt(conn->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT 1"));
        return res && res->next();
    }
    catch (const sql::SQLException& e)
    {
        std::cerr << "[MysqlPool] stale connection detected: " << e.what() << std::endl;
        return false;
    }
}

void MysqlPool::close()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _isClosed = true;
    }
    _cond.notify_all();
}

std::unique_ptr<sql::Connection> MysqlPool::getConnection()
{
    while (true)
    {
        std::unique_ptr<sql::Connection> conn;

        {
            std::unique_lock<std::mutex> lock(_mutex);
            _cond.wait(lock, [this] {
                return _isClosed || !_pool.empty();
            });

            if (_isClosed)
            {
                return nullptr;
            }

            conn = std::move(_pool.front());
            _pool.pop();
        }

        if (isConnectionAlive(conn.get()))
        {
            return conn;
        }

        try
        {
            std::cout << "[MysqlPool] reconnecting stale MySQL connection." << std::endl;
            return createConnection();
        }
        catch (const sql::SQLException& e)
        {
            std::cerr << "[MysqlPool] reconnect failed: " << e.what() << std::endl;
            return nullptr;
        }
    }
}

void MysqlPool::returnConnection(std::unique_ptr<sql::Connection> conn)
{
    if (conn == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    if (_isClosed)
    {
        return;
    }

    _pool.push(std::move(conn));
    _cond.notify_one();
}
