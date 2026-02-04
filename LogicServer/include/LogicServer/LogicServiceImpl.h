#pragma once
#include <grpcpp/grpcpp.h>
#include "LogicServer/message.grpc.pb.h"
#include "LogicServer/const.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using message::RegisterReq;
using message::RegisterRsp;
using message::LogicService;
using message::LoginReq;
using message::LoginRsp;
using message::ResetPasswordReq;
using message::ResetPasswordRsp;
using message::UpdateAvatarReq;
using message::UpdateAvatarRsp;

class LogicServiceImpl final : public LogicService::Service
{
public:
    LogicServiceImpl() = default;

    // 重写 .proto 中定义的 RegisterUser 方法
    virtual Status RegisterUser(ServerContext* context, const RegisterReq* request, RegisterRsp* reply) override;

    // 重写 .proto 中定义的 ResetPassword 方法
    virtual Status ResetPassword(ServerContext* context, const ResetPasswordReq* request, ResetPasswordRsp* reply) override;

    // 重写 .proto 中定义的 Login 方法
    virtual Status Login(ServerContext* context, const LoginReq* request, LoginRsp* reply) override;

    // 重写 .proto 中定义的 UpdateAvatar 方法
    virtual Status UpdateAvatar(ServerContext* context, const UpdateAvatarReq* request, UpdateAvatarRsp* reply) override;

    //辅助函数，获取连接数少的CanvasServerInfo
    bool GetCanvasServerInfo(std::string& host, std::string& port);
};