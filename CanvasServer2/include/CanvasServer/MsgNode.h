/***********************************************************************************
* @file         MsgNode.h
* @brief        TCP 帧头与收发缓冲区，缓冲区长度包含四字节帧头
* @author       shanzhi
* @date         2026/10/25
* @history
***********************************************************************************/
#pragma once
#include <cstddef>
#include <iostream>
#include <string>
#include <memory>
#include <cstring>
#include <boost/asio.hpp>
#include "const.h" // 包含 MAX_LENGTH, ID_DRAW_REQ 等定义
// 确保字节对齐，防止不同编译器补齐导致解析错误
#pragma pack(push, 1)
struct MsgHead
{
	std::uint16_t msg_id; // 网络字节序消息编号。
	std::uint16_t msg_len; // 网络字节序正文长度，不含帧头。
};
#pragma pack(pop)

// 基础消息节点
class MsgNode
{
public:
	MsgNode(std::size_t max_len) : _cur_len(0), _total_len(max_len) // 分配能够容纳正文及帧头的完整缓冲区。
	{
		// 1. 最大正文加帧头超出 uint16_t，容量使用 size_t 防止截断后越界写。
		_data = new char[max_len + 1]();
		_data[max_len] = '\0';
	}
	virtual ~MsgNode() // 释放节点独占的收发缓冲区。
	{
		// 1. 节点离开发送队列后统一释放缓冲区。
		if (_data)
		{
			delete[] _data;
			_data = nullptr;
		}
	}

	std::size_t _cur_len; // 已收发的字节数。
	std::size_t _total_len; // 缓冲区完整长度，发送时包含帧头。
	char* _data; // 节点独占的收发缓冲区。
};

// 接收节点 (用于 ReadBody)
class RecvNode : public MsgNode
{
public:
	RecvNode(uint16_t max_len, uint16_t msg_id) : MsgNode(max_len), _msg_id(msg_id) // 接收节点只分配正文长度。
	{
		// 1. 保存已经解析的网络消息编号。
	}
	uint16_t _msg_id; // 当前正文所属消息编号。
};

// 4. 发送节点 (用于 Send Queue)
class SendNode : public MsgNode
{
public:
	SendNode(const char* msg, uint16_t max_len, uint16_t msg_id) : MsgNode(max_len + sizeof(MsgHead)), _msg_id(msg_id) // 打包帧头与完整正文。
	{
		// 1. 帧头仍使用 uint16_t 正文长度，缓冲区容量独立使用 size_t。
		uint16_t  msg_id_host = boost::asio::detail::socket_ops::host_to_network_short(msg_id);     //转为网络字节序(大端序)
		uint16_t  msg_len_host = boost::asio::detail::socket_ops::host_to_network_short(max_len);

		memcpy(_data, &msg_id_host, sizeof(uint16_t));
		memcpy(_data + sizeof(uint16_t), &msg_len_host, sizeof(uint16_t));

		// 2. 正文写在四字节帧头后，不能因容量截断覆盖其他房间状态。
		memcpy(_data + sizeof(MsgHead), msg, max_len);
	}
	uint16_t _msg_id; // 待发送消息编号。
};
