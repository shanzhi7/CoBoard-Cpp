#pragma once

#include <memory>
#include <string>

class CSession;

class ImageOperationHandler
{
public:
	static void Handle(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data); // 校验、应用并广播一条图片图元操作
};
