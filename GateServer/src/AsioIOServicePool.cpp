#include "GateServer/AsioIOServicePool.h"
#include <iostream>

AsioIOServicePool::AsioIOServicePool(std::size_t size) :_ioServices(size),
_workGuards(size), _nextIOService(0)
{
	//将IOService绑定到WorkGuard,使得IOService在没有任务时不会自动退出
	for (size_t i = 0; i < size; i++)
	{
		_workGuards[i] = std::unique_ptr<WorkGuard>(new WorkGuard(_ioServices[i].get_executor()));
	}

	//遍历ioservice,创建多线程,每个线程内部启动ioservice
	for (std::size_t i = 0; i < size; i++)
	{
		_threads.push_back(std::thread([this, i]() {
			_ioServices[i].run();
			}));
	}
}

AsioIOServicePool::~AsioIOServicePool()
{
	Stop();
	std::cout << "AsioIOServicePool destruct" << std::endl;
}

IOService& AsioIOServicePool::GetIOService()
{
	auto& ioservice = _ioServices[_nextIOService++];
	if (_nextIOService == _ioServices.size())
	{
		_nextIOService = 0;
	}
	return ioservice;
}

void AsioIOServicePool::Stop()
{
	//取消workguard,让io_context能自动退出run()
	for (auto& workGuard : _workGuards)
	{
		workGuard->get_executor().context().stop();
		workGuard->reset(); // 释放 keep-alive
	}

	// 确保 io_context 自己也 stop 掉（防止有其他阻塞任务）
	//for (auto& ioc : _ioServices)
	//{
	//	ioc.stop();
	//}

	for (auto& t : _threads)
	{
		t.join();
	}
}