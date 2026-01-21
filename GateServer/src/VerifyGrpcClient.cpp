#include "GateServer/VerifyGrpcClient.h"
#include "GateServer/ConfigMgr.h"

VerifyGrpcClient::VerifyGrpcClient()
{
	auto& gCfgMgr = ConfigMgr::Inst();
	std::string host = gCfgMgr["VarifyServer"]["Host"];
	std::string port = gCfgMgr["VarifyServer"]["Port"];
	_pool.reset(new RPConPool(5, host, port));
}

RPConPool::RPConPool(size_t poolsize, std::string host, std::string port) :	//初始化连接池
	_poolSize(poolsize), _host(host), _port(port), _b_stop(false)
{
	for (size_t i = 0; i < _poolSize; i++)
	{
		std::shared_ptr<Channel> channel = grpc::CreateChannel(host + ":" + port, //监听ip与端口
			grpc::InsecureChannelCredentials());					//通道,用于与grpc服务端通信(grpc::InsecureChannelCredentials() 使用不安全证书)
		_connections.push(VarifyService::NewStub(channel));
	}
}

RPConPool::~RPConPool()		//析构函数关闭连接，通知所有线程
{
	std::lock_guard<std::mutex> lock(_mutex);	//上锁保证线程安全
	Close();
	while (_connections.empty() == false)
	{
		_connections.pop();
	}
}

std::unique_ptr<VarifyService::Stub> RPConPool::getConnection()	//获取 VarifyService::Stub 连接
{
	std::unique_lock<std::mutex> lock(_mutex);
	_cond.wait(lock, [this]() {
		if (_b_stop)
		{
			return true;
		}
		return !_connections.empty();
		});

	if (_b_stop)
	{
		return nullptr;
	}
	std::unique_ptr<VarifyService::Stub> stub = std::move(_connections.front());
	_connections.pop();
	return stub;

}

void RPConPool::returnConnection(std::unique_ptr<VarifyService::Stub> stub)	//使用完毕后归还到连接池
{
	std::lock_guard<std::mutex> lock(_mutex);
	if (_b_stop)
	{
		return;
	}
	_connections.push(std::move(stub));
	_cond.notify_one();
}

void RPConPool::Close()		//关闭连接，告知所有线程
{
	_b_stop = true;
	_cond.notify_all();
}