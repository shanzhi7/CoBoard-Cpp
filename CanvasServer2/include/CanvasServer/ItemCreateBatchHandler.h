/***********************************************************************************
* @file         ItemCreateBatchHandler.h
* @brief        复制粘贴图元批量创建协议处理器
* @author       shanzhi
* @date         2026/10/23
* @history
***********************************************************************************/
#pragma once

#include <memory>
#include <string>

class CSession;

class ItemCreateBatchHandler
{
public:
	static void Handle(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data); // 校验会话和批量请求后提交至房间。
};
