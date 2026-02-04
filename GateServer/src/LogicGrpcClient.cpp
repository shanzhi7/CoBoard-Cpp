#include"GateServer/LogicGrpcClient.h"
#include"GateServer/ConfigMgr.h"

LogicGrpcClient::LogicGrpcClient()
{ 
    //从配置文件读取 LogicServer 的地址
    auto& gCfgMgr = ConfigMgr::Inst();
    std::string host = gCfgMgr["LogicServer"]["Host"];
    std::string port = gCfgMgr["LogicServer"]["Port"];
    std::string address = host + ":" + port;

    //创建 gRPC 通道，Insecure 表示不使用 SSL 证书
    std::shared_ptr<Channel> channel =
        grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    //创建 stub，并绑定到通道
    _stub = LogicService::NewStub(channel);

}

// 注册业务方法的实现
RegisterRsp LogicGrpcClient::RegisterUser(RegisterReq req)
{
    ClientContext context;  // 上下文：用于设置超时、获取元数据等
    RegisterRsp rsp;        // 响应：用于接收 LogicServer 的返回值
    grpc::Status status;    // 状态：用于判断 RPC 调用本身是否成功

    // 调用 LogicServer 的 RegisterUser 方法
    status = _stub->RegisterUser(&context, req, &rsp);

    if (status.ok())
    {
        // RPC 调用成功，直接返回 LogicServer 给出的结果
        return rsp;
    }
    else
    {
        // RPC 调用失败（网络问题、服务器挂了等）
        // 设置一个错误码，告诉上层是 RPC 这一层出问题了
        rsp.set_error(message::ErrorCodes::RPCFailed);
        return rsp;
    }
}

ResetPasswordRsp LogicGrpcClient::ResetPassword(ResetPasswordReq req)
{
    ClientContext context;  // 上下文：用于设置超时、获取元数据等
    ResetPasswordRsp rsp;        // 响应：用于接收 LogicServer 的返回值
    grpc::Status status;    // 状态：用于判断 RPC 调用本身是否成功

    // 调用 LogicServer 的 ResetPassword 方法
    status = _stub->ResetPassword(&context, req, &rsp);
    if (status.ok())
    {
        // RPC 调用成功，直接返回 LogicServer 给出的结果
        return rsp;
    }
    else
    {
        // RPC 调用失败（网络问题、服务器挂了等）
        // 设置一个错误码，告诉上层是 RPC 这一层出问题了
        rsp.set_error(message::ErrorCodes::RPCFailed);
        return rsp;
    }
}

LoginRsp LogicGrpcClient::Login(LoginReq req)
{
    ClientContext context;
    LoginRsp reply;

    Status status = _stub->Login(&context, req, &reply);  // 调用 LogicServer 的 Login 方法
    if (status.ok())
    {
        // RPC 调用成功，直接返回 LogicServer 给出的结果
        return reply;
    }
    else
    {
        // RPC 调用失败（网络问题、服务器挂了等）
        // 创建一个错误码，告诉上层是 RPC 这一层出问题了
        reply.set_error(message::ErrorCodes::RPCFailed);
        std::cout << "[GateServer] Call LogicServer Login Failed. Error: "
            << status.error_code() << ": " << status.error_message() << std::endl;
        return reply;
    }
}

UpdateAvatarRsp LogicGrpcClient::UpdateAvatar(UpdateAvatarReq req)
{
    ClientContext context;
    UpdateAvatarRsp rsp;
    Status status = _stub->UpdateAvatar(&context, req, &rsp);

    if (status.ok())
    {
        // RPC 调用成功，直接返回 LogicServer 给出的结果
        return rsp;
    }
    else
    {
        // RPC 调用失败（网络问题、服务器挂了等）
        // 创建一个错误码，告诉上层是 RPC 这一层出问题了
        rsp.set_error(message::ErrorCodes::RPCFailed);
        std::cout << "[GateServer] Call LogicServer UpdateAvatar Failed. Error: "
            << status.error_code() << ": " << status.error_message() << std::endl;
        return rsp;
    }
}