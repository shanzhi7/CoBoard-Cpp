/***********************************************************************************
* @file         canvasitemeditcodec.h
* @brief        图元编辑值状态与网络协议之间的转换
* @author       shanzhi
* @date         2026/10/15
* @history
***********************************************************************************/
#pragma once

#include "canvasitemeditstate.h"
#include "message.pb.h"

namespace CanvasItemEditCodec
{
message::ItemEditState Encode(const CanvasItemEditState& state); // 将完整编辑快照转换为不含图片和路径二进制的协议。
bool Decode(const message::ItemEditState& input, CanvasItemEditState* state); // 校验并恢复服务端确认的编辑快照。
quint32 ChangedFields(const CanvasItemEditState& before, const CanvasItemEditState& after); // 返回几何和文字字段掩码，避免无关字段被覆盖。
}
