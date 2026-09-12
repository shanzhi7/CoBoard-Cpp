#pragma once
#include <grpcpp/grpcpp.h>
#include "GateServer/message.grpc.pb.h"
#include "GateServer/const.h"
#include "GateServer/Singleton.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <string>

using grpc::Channel;			//通道
using grpc::Status;				//状态
using grpc::ClientContext;		//客户端上下文

using message::GetVarifyReq;
using message::GetVarifyRsp;
using message::CreateVoiceTokenReq;
using message::CreateVoiceTokenRsp;
using message::VarifyService;

class RPConPool
{
public:
	RPConPool(size_t poolsize, std::string host, std::string port);

	~RPConPool();

	void Close();

	std::unique_ptr<VarifyService::Stub> getConnection();

	void returnConnection(std::unique_ptr<VarifyService::Stub> stub);

private:
	std::atomic<bool> _b_stop;
	size_t _poolSize;
	std::string _host;
	std::string _port;
	std::queue<std::unique_ptr<VarifyService::Stub>> _connections;

	std::condition_variable _cond;
	std::mutex _mutex;
};

class VerifyGrpcClient :public Singleton<VerifyGrpcClient>
{
	friend class Singleton<VerifyGrpcClient>;
public:
	~VerifyGrpcClient() {};

	GetVarifyRsp GetVarifyCode(std::string email) {	//获取验证码
		ClientContext context;	//上下文
		GetVarifyRsp reply;		//答复
		GetVarifyReq request;	//请求
		request.set_email(email);

		std::unique_ptr<VarifyService::Stub> stub = _pool->getConnection();
		Status stutas = stub->GetVarifyCode(&context, request, &reply);	//发送获取验证码请求

		if (stutas.ok())		//请求成功
		{
			_pool->returnConnection(std::move(stub));
			return reply;
		}
		else					//请求失败
		{
			_pool->returnConnection(std::move(stub));
			reply.set_error(message::ErrorCodes::RPCFailed);//设置错误码
			return reply;
		}
	}

	//获取房间语音token请求
	CreateVoiceTokenRsp CreateVoiceToken(int uid, const std::string& room_id) {
		ClientContext context;
		CreateVoiceTokenRsp reply;
		CreateVoiceTokenReq request;
		request.set_uid(uid);
		request.set_room_id(room_id);

		std::unique_ptr<VarifyService::Stub> stub = _pool->getConnection();
		if (stub == nullptr)
		{
			reply.set_error(message::ErrorCodes::RPCFailed);
			return reply;
		}

		Status status = stub->CreateVoiceToken(&context, request, &reply);
		_pool->returnConnection(std::move(stub));

		if (!status.ok())
		{
			std::cout << "[VerifyGrpcClient] CreateVoiceToken RPC failed, uid: "
				<< uid << ", room_id: " << room_id
				<< ", error: " << status.error_message() << std::endl;
			reply.Clear();
			reply.set_error(message::ErrorCodes::RPCFailed);
		}

		return reply;
	}

	//UpdateAvatarRsp 

private:
	VerifyGrpcClient();
	std::unique_ptr<RPConPool> _pool;
};

