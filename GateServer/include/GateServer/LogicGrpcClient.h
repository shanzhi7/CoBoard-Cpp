#pragma once
#include <grpcpp/grpcpp.h>
#include "GateServer/message.grpc.pb.h"
#include "GateServer/const.h"
#include "GateServer/Singleton.h"
#include <queue>
#include <mutex>
#include <condition_variable>

using grpc::Channel;			//通道
using grpc::Status;				//状态
using grpc::ClientContext;		//上下文

using message::RegisterReq;
using message::RegisterRsp;
using message::LogicService;
using message::ResetPasswordReq;
using message::ResetPasswordRsp;
using message::LoginReq;
using message::LoginRsp;
using message::UpdateAvatarReq;
using message::UpdateAvatarRsp;

class LogicGrpcClient : public Singleton<LogicGrpcClient>
{
    friend class Singleton<LogicGrpcClient>;
public:
    ~LogicGrpcClient() {};      //析构函数

    RegisterRsp RegisterUser(RegisterReq req);              //注册用户
    ResetPasswordRsp ResetPassword(ResetPasswordReq req);   //重置密码
    LoginRsp Login(LoginReq req);                           //登录
    UpdateAvatarRsp UpdateAvatar(UpdateAvatarReq req);      //更新头像

private:
    LogicGrpcClient();           //私有化构造函数

    //Stub
    std::unique_ptr<LogicService::Stub> _stub;
};