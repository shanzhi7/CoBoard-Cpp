/***********************************************************************************
* @file         canvasclipboardpayload.h
* @brief        画布剪贴板识别、版本化序列化及粘贴定位接口
* @author       shanzhi
* @date         2026/10/23
* @history
***********************************************************************************/
#pragma once

#include <QByteArray>
#include <QPointF>
#include <QImage>
#include <QStringList>
#include <QString>
#include <QVector>

#include "canvasdocument.h"

class CanvasItem;
class QMimeData;

enum class CanvasPasteAnchor
{
    Center, // 副本集合中心对齐触发坐标。
    TopLeft // 副本集合左上角对齐触发坐标。
};

struct CanvasClipboardContent
{
    enum class Kind
    {
        None, // 剪贴板没有支持的数据。
        Internal, // 已解析的内部图元集合。
        ImageFiles, // 可识别的本地图片文件。
        ImagePixels // 外部图片像素。
    };
    Kind _kind = Kind::None; // 统一识别结果，内部损坏时不回退。
    CanvasDocument _document; // 内部图元及图片资源快照。
    QStringList _file_paths; // 已通过图片格式探测的本地文件。
    QImage _image; // 外部图片像素快照。
    QString _error_message; // 格式损坏或没有支持数据的原因。
};

class CanvasClipboardPayload
{
public:
    static CanvasClipboardContent Resolve(const QMimeData* mime_data); // 无副作用识别剪贴板，供菜单状态与实际粘贴共用。
    static QString MimeType(); // 返回内部图元剪贴板 MIME 类型。
    static bool Encode(const QVector<CanvasItem*>& items, QByteArray* data, QString* error_message); // 序列化选中图元及图片像素兜底数据。
    static bool Decode(const QByteArray& data, CanvasDocument* document, QPointF* anchor, QString* error_message); // 严格解析内部载荷并构造临时文档。
    static bool CloneForPaste(const CanvasDocument& source, const QPointF& scene_pos, CanvasDocument* document, QString* error_message, CanvasPasteAnchor anchor = CanvasPasteAnchor::Center); // 重分配 ID 并按指定锚点定位副本集合。
    static constexpr int MAX_ITEMS = 64; // 一次内部复制粘贴支持的图元上限。
};
