/***********************************************************************************
* @file         ItemEditOperationHandler.h
* @brief        通用图元编辑消息鉴权、确认和房间广播
* @author       shanzhi
* @date         2026/10/15
* @history
***********************************************************************************/
#pragma once

#include <memory>
#include <string>

class CSession;

class ItemEditOperationHandler
{
public:
	static void Handle(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data); // 验证会话并转交房间原子编辑。
};
