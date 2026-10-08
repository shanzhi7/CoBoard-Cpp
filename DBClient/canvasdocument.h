/***********************************************************************************
* @file         canvasdocument.h
* @brief        画布文件值类型、资源描述和导入限制
* @author       shanzhi
* @date         2026/10/08
* @history
***********************************************************************************/
#pragma once

#include <QColor>
#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QMetaType>
#include <QMap>
#include <QSize>
#include <QStringList>
#include <QVector>

struct CanvasAssetData
{
    QString _asset_sha256; // 资源内容的 SHA-256。
    QString _mime_type; // 资源实际 MIME 类型。
    QSize _original_size; // 原始像素尺寸。
    QByteArray _data; // 原始编码图片数据。
    QImage _image; // 后台解码的像素，在 GUI 线程构造图片图元。
};

using CanvasItemData = QJsonObject; // 每种图元按类型保存 JSON 属性。

struct CanvasDocument
{
    int _schema_version = 1; // 当前支持的画布文件版本。
    QSize _canvas_size; // 画布场景宽高。
    QColor _background; // 画布背景颜色及透明度。
    QVector<CanvasItemData> _items; // 按实际堆叠顺序排列的图元描述。
    QStringList _item_order; // 从底层到顶层的图元 ID。
    QMap<QString, CanvasAssetData> _asset_dataMap; // 以内容摘要索引的内嵌图片资源。
};

struct CanvasImportResult
{
    CanvasDocument _document; // 已校验的临时画布模型。
    QString _error_message; // 为空表示解析和资源准备成功。
};

namespace CanvasDocumentLimits
{
constexpr qint64 MAX_FILE_BYTES = 256LL * 1024 * 1024; // 画布 JSON 文件大小上限。
constexpr int MAX_ITEMS = 10000; // 文件内图元数量上限。
constexpr int MAX_PATH_ELEMENTS = 1000000; // 全部路径图元的元素数量上限。
constexpr qint64 MAX_ASSET_BYTES = 100LL * 1024 * 1024; // 图片资源原始字节总上限。
constexpr qint64 MAX_IMAGE_PIXELS = 64LL * 1000 * 1000; // 唯一资源解码总量和单张导出图片像素上限。
}

Q_DECLARE_METATYPE(CanvasDocument)
