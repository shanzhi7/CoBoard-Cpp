#pragma once
#include <iostream>
#include <string>
#include <memory>
#include <cstring>
#include <boost/asio.hpp>
#include "const.h" // 包含 MAX_LENGTH, ID_DRAW_REQ 等定义
// 确保字节对齐，防止不同编译器补齐导致解析错误
#pragma pack(push, 1)
struct MsgHead {
    std::uint16_t msg_id;
    std::uint16_t msg_len; // body 长度
};
#pragma pack(pop)

// 基础消息节点
class MsgNode {
public:
    MsgNode(uint16_t  max_len) : _total_len(max_len), _cur_len(0)
    {
        _data = new char[max_len + 1]();
        _data[max_len] = '\0';
    }
    virtual ~MsgNode()
    {
        if (_data)
        {
            delete[] _data;
            _data = nullptr;
        }
    }

    uint16_t  _cur_len;         // 当前已接收的长度
    uint16_t  _total_len;       // 消息总长度 (期望长度),SendNode里意思不同
    char* _data;
};

// 接收节点 (用于 ReadBody)
class RecvNode : public MsgNode
{
public:
    RecvNode(uint16_t  max_len, uint16_t  msg_id) : MsgNode(max_len), _msg_id(msg_id) {}
    uint16_t  _msg_id;
};

// 4. 发送节点 (用于 Send Queue)
class SendNode : public MsgNode
{
public:
    SendNode(const char* msg, uint16_t  max_len, uint16_t  msg_id) : MsgNode(max_len + sizeof(MsgHead)), _msg_id(msg_id)
    {
        // 先写头部
        uint16_t  msg_id_host = boost::asio::detail::socket_ops::host_to_network_short(msg_id);     //转为网络字节序(大端序)
        uint16_t  msg_len_host = boost::asio::detail::socket_ops::host_to_network_short(max_len);

        memcpy(_data, &msg_id_host, sizeof(uint16_t));
        memcpy(_data + sizeof(uint16_t), &msg_len_host, sizeof(uint16_t));

        // 再写 Body
        memcpy(_data + sizeof(MsgHead), msg, max_len);
    }
    uint16_t  _msg_id;
};