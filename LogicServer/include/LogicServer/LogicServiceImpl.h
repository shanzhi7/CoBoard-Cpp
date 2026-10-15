/***********************************************************************************
* @file         LogicServiceImpl.h
* @brief        账号业务 gRPC 服务及登录凭据撤销
* @author       shanzhi
* @date         2026/01/22
* @history
***********************************************************************************/
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
using message::VerifyTokenReq;
using message::VerifyTokenRsp;
using message::ResetPasswordReq;
using message::ResetPasswordRsp;
using message::UpdateAvatarReq;
using message::UpdateAvatarRsp;

class LogicServiceImpl final : public LogicService::Service
{
public:
    Status Logout(ServerContext* context, const message::LogoutReq* request, message::LogoutRsp* reply) override; // 原子撤销本次登录。
    LogicServiceImpl() = default; // 初始化账号业务服务。

    // 重写 .proto 中定义的 RegisterUser 方法
    virtual Status RegisterUser(ServerContext* context, const RegisterReq* request, RegisterRsp* reply) override; // 校验验证码并注册账号。

    // 重写 .proto 中定义的 ResetPassword 方法
    virtual Status ResetPassword(ServerContext* context, const ResetPasswordReq* request, ResetPasswordRsp* reply) override; // 更新账号密码。

    // 重写 .proto 中定义的 Login 方法
    virtual Status Login(ServerContext* context, const LoginReq* request, LoginRsp* reply) override; // 签发账号登录凭据。

    // 重写 .proto 中定义的 VerifyToken 方法，供网关和 CanvasServer 校验登录凭证
    virtual Status VerifyToken(ServerContext* context, const VerifyTokenReq* request, VerifyTokenRsp* reply) override; // 校验 Token 所属账号。

    // 重写 .proto 中定义的 UpdateAvatar 方法
    virtual Status UpdateAvatar(ServerContext* context, const UpdateAvatarReq* request, UpdateAvatarRsp* reply) override; // 更新头像引用。

    //辅助函数，获取连接数少的CanvasServerInfo
    bool GetCanvasServerInfo(std::string& host, std::string& port); // 返回画板服务地址。
};
