#pragma once
/*
	iocontext 线程池，一个io_context对应一个线程，处理io的任务
*/
#include <vector>
#include <thread>
#include <memory>
#include <boost/asio.hpp>
#include "GateServer/Singleton.h"

using IOService = boost::asio::io_context;										//用于处理异步读写
using WorkGuard = boost::asio::executor_work_guard<IOService::executor_type>;	//用于阻止io_context退出
using WorkGuardPtr = std::unique_ptr<WorkGuard>;								//指针

class AsioIOServicePool : public Singleton<AsioIOServicePool>
{

	friend Singleton<AsioIOServicePool>;
public:

	~AsioIOServicePool();

	AsioIOServicePool(const AsioIOServicePool&) = delete;

	AsioIOServicePool& operator=(const AsioIOServicePool&) = delete;

	// 使用 round-robin 的方式返回一个 io_service(io_context)
	IOService& GetIOService();

	void Stop();

private:

	explicit AsioIOServicePool(std::size_t size = 2); // 默认两个线程

	std::vector<IOService> _ioServices;			//存储io_context的数组
	std::vector<WorkGuardPtr> _workGuards;		//存储WorkGuardPtr的数组
	std::vector<std::thread> _threads;			//存储std::thread的数组
	std::size_t _nextIOService = 0;				//下一个io_context
};