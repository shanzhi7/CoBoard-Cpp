/***********************************************************************************
* @file         canvasclipboardpayload.h
* @brief        画布图元内部剪贴板的版本化序列化接口
* @author       shanzhi
* @date         2026/10/23
* @history
***********************************************************************************/
#pragma once

#include <QByteArray>
#include <QPointF>
#include <QString>
#include <QVector>

#include "canvasdocument.h"

class CanvasItem;

class CanvasClipboardPayload
{
public:
    static QString MimeType(); // 返回内部图元剪贴板 MIME 类型。
    static bool Encode(const QVector<CanvasItem*>& items, QByteArray* data, QString* error_message); // 序列化选中图元及图片像素兜底数据。
    static bool Decode(const QByteArray& data, CanvasDocument* document, QPointF* anchor, QString* error_message); // 严格解析内部载荷并构造临时文档。
    static bool CloneForPaste(const CanvasDocument& source, const QPointF& scene_pos, CanvasDocument* document, QString* error_message); // 重分配 ID 并将整组显示中心移动到目标场景坐标。
};
