#pragma once

#include <jdbc/mysql_connection.h>
#include <jdbc/mysql_driver.h>
#include <jdbc/cppconn/exception.h>
#include <jdbc/cppconn/prepared_statement.h>

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

class MysqlPool
{
public:
    MysqlPool(std::string url, std::string user, std::string password, std::string schema, int poolSize);
    ~MysqlPool();

    std::unique_ptr<sql::Connection> getConnection();
    void returnConnection(std::unique_ptr<sql::Connection> con);
    void close();

private:
    std::unique_ptr<sql::Connection> createConnection();
    bool isConnectionAlive(sql::Connection* conn);

    std::string _url;
    std::string _user;
    std::string _password;
    std::string _schema;
    int _poolSize;
    std::queue<std::unique_ptr<sql::Connection>> _pool;

    std::mutex _mutex;
    std::condition_variable _cond;
    bool _isClosed;
};
