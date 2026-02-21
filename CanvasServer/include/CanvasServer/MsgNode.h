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
    MsgNode(short max_len) : _total_len(max_len), _cur_len(0)
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

    short _cur_len;
    short _total_len;
    char* _data;
};

// 接收节点 (用于 ReadBody)
class RecvNode : public MsgNode
{
public:
    RecvNode(short max_len, short msg_id) : MsgNode(max_len), _msg_id(msg_id) {}
    short _msg_id;
};

// 4. 发送节点 (用于 Send Queue)
class SendNode : public MsgNode
{
public:
    SendNode(const char* msg, short max_len, short msg_id) : MsgNode(max_len + sizeof(MsgHead)), _msg_id(msg_id)
    {
        // 先写头部
        short msg_id_host = boost::asio::detail::socket_ops::host_to_network_short(msg_id);     //转为网络字节序(大端序)
        short msg_len_host = boost::asio::detail::socket_ops::host_to_network_short(max_len);

        memcpy(_data, &msg_id_host, sizeof(short));
        memcpy(_data + sizeof(short), &msg_len_host, sizeof(short));

        // 再写 Body
        memcpy(_data + sizeof(MsgHead), msg, max_len);
    }
    short _msg_id;
};