/***********************************************************************************
* @file         decodedtcpmessage.h
* @brief        画板 TCP 入站消息的强类型跨线程载荷
* @author       shanzhi
* @date         2026/10/22
* @history
***********************************************************************************/
#pragma once

#include <variant>

#include <QJsonObject>
#include <QMetaType>

#include "message.pb.h"

struct DecodedTcpMessage
{
    using Payload = std::variant<QJsonObject, message::JoinRoomRsp, message::LeaveRoomRsp,
        message::DestroyRoomRsp, message::RoomDestroyedBroadcast, message::UserJoinRoomBroadcast,
        message::UserLeaveRoomBroadcast, message::DrawReq, message::ImageOperation,
        message::ItemEditOperationRsp, message::TextOperationRsp, message::GrantEditRsp,
        message::RevokeEditRsp, message::PermissionChangedBroadcast, message::ChatRsp>;

    quint16 _message_id = 0; // 原始 TCP 消息编号，与载荷类型对应。
    Payload _payload; // 已解码的独立值，排队交付后不再访问 worker 的缓冲区。
};

Q_DECLARE_METATYPE(DecodedTcpMessage)
