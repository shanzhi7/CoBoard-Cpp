/***********************************************************************************
* @file         TextOperationHandler.h
* @brief        文字 TCP 操作的会话校验与房间广播入口
* @author       shanzhi
* @date         2026/10/08
* @history
***********************************************************************************/
#pragma once

#include <memory>
#include <string>

class CSession;

class TextOperationHandler
{
public:
	static void Handle(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data); // 校验文字请求并回复规范化结果。
};
