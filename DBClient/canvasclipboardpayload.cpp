#include "canvasclipboardpayload.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <memory>

#include "canvasitemserializer.h"
#include "canvasitems/canvasitem.h"
#include "canvasitems/imageitem.h"

namespace
{
constexpr int CLIPBOARD_VERSION = 1;
constexpr int MAX_ITEMS = 64;
constexpr qint64 MAX_IMAGE_BYTES = 16LL * 1024 * 1024;

bool EncodePng(const QImage& image, QByteArray* data)
{
    // 1. 统一写无损 PNG，跨平台粘贴不依赖原本地路径或图片编码插件。
    QBuffer buffer(data);
    return !image.isNull() && buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG");
}
}

QString CanvasClipboardPayload::MimeType()
{
    // 1. 固定 MIME 用于与系统文件、像素图片剪贴板数据区分。
    return QStringLiteral("application/x-syncanvas-itemset");
}

bool CanvasClipboardPayload::Encode(const QVector<CanvasItem*>& items, QByteArray* data, QString* error_message)
{
    // 1. 复制前完整序列化，任何一个图元或图片资源不完整均不覆盖系统剪贴板。
    if (!data || items.isEmpty() || items.size() > MAX_ITEMS)
    {
        if (error_message) *error_message = QStringLiteral("没有可复制的图元或选择数量超过限制");
        return false;
    }
    CanvasDocument document;
    QVector<CanvasItem*> ordered_items = items;
    std::stable_sort(ordered_items.begin(), ordered_items.end(), [](CanvasItem* left, CanvasItem* right) {
        // 2. 同组复制按当前从底到顶的 z 值写入，粘贴后保留内部层叠关系。
        return left && right ? left->zValue() < right->zValue() : left != nullptr;
    });
    QJsonArray json_items;
    QStringList order;
    QPointF anchor;
    bool has_anchor = false;
    for (CanvasItem* item : ordered_items)
    {
        QJsonObject item_data;
        if (!CanvasItemSerializer::SerializeItem(item, &item_data, &document, error_message)) return false;
        const QPointF position = item->sceneBoundingRect().topLeft();
        if (!has_anchor)
        {
            anchor = position;
            has_anchor = true;
        }
        else
        {
            anchor.setX(qMin(anchor.x(), position.x()));
            anchor.setY(qMin(anchor.y(), position.y()));
        }
        json_items.append(item_data);
        order.append(item->itemId());
        if (auto* image_item = dynamic_cast<ImageItem*>(item))
        {
            CanvasAssetData& asset = document._asset_dataMap[image_item->assetSha256()];
            if (!EncodePng(image_item->pixmap().toImage(), &asset._data) || asset._data.size() > MAX_IMAGE_BYTES)
            {
                if (error_message) *error_message = QStringLiteral("图片像素无法写入内部剪贴板");
                return false;
            }
            asset._image = image_item->pixmap().toImage();
        }
    }

    // 2. 资源按摘要去重，图元按场景从底到顶的调用顺序保存。
    QJsonArray assets;
    for (auto iterator = document._asset_dataMap.cbegin(); iterator != document._asset_dataMap.cend(); ++iterator)
    {
        const CanvasAssetData& asset = iterator.value();
        assets.append(QJsonObject{{QStringLiteral("sha256"), asset._asset_sha256},
                                  {QStringLiteral("mime_type"), asset._mime_type},
                                  {QStringLiteral("width"), asset._original_size.width()},
                                  {QStringLiteral("height"), asset._original_size.height()},
                                  {QStringLiteral("png"), QString::fromLatin1(asset._data.toBase64())}});
    }
    const QJsonObject root{{QStringLiteral("magic"), QStringLiteral("SyncCanvasItemSet")},
                           {QStringLiteral("version"), CLIPBOARD_VERSION},
                           {QStringLiteral("anchor"), QJsonArray{anchor.x(), anchor.y()}},
                           {QStringLiteral("items"), json_items},
                           {QStringLiteral("order"), QJsonArray::fromStringList(order)},
                           {QStringLiteral("assets"), assets}};
    *data = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return true;
}

bool CanvasClipboardPayload::Decode(const QByteArray& data, CanvasDocument* document, QPointF* anchor, QString* error_message)
{
    // 1. 先验证封装版本与根类型，内部 MIME 损坏时调用方必须停止外部图片回退。
    if (!document || !anchor || data.isEmpty() || data.size() > MAX_IMAGE_BYTES * 2)
    {
        if (error_message) *error_message = QStringLiteral("内部图元剪贴板数据无效");
        return false;
    }
    QJsonParseError parse_error;
    const QJsonDocument json_document = QJsonDocument::fromJson(data, &parse_error);
    const QJsonObject root = json_document.object();
    if (parse_error.error != QJsonParseError::NoError || !json_document.isObject() ||
        root.value(QStringLiteral("magic")).toString() != QStringLiteral("SyncCanvasItemSet") ||
        root.value(QStringLiteral("version")).toInt() != CLIPBOARD_VERSION || !root.value(QStringLiteral("items")).isArray())
    {
        if (error_message) *error_message = QStringLiteral("内部图元剪贴板版本或格式无效");
        return false;
    }
    const QJsonArray anchor_array = root.value(QStringLiteral("anchor")).toArray();
    if (anchor_array.size() != 2 || !anchor_array.at(0).isDouble() || !anchor_array.at(1).isDouble())
    {
        if (error_message) *error_message = QStringLiteral("内部图元剪贴板锚点无效");
        return false;
    }
    CanvasDocument result;
    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue& value : assets)
    {
        const QJsonObject asset_object = value.toObject();
        const QByteArray png = QByteArray::fromBase64(asset_object.value(QStringLiteral("png")).toString().toLatin1());
        QImage image;
        if (asset_object.value(QStringLiteral("sha256")).toString().isEmpty() || png.isEmpty() || png.size() > MAX_IMAGE_BYTES || !image.loadFromData(png, "PNG"))
        {
            if (error_message) *error_message = QStringLiteral("内部图元剪贴板图片资源无效");
            return false;
        }
        CanvasAssetData asset;
        asset._asset_sha256 = asset_object.value(QStringLiteral("sha256")).toString();
        asset._mime_type = asset_object.value(QStringLiteral("mime_type")).toString();
        asset._original_size = image.size();
        asset._data = png;
        asset._image = image;
        result._asset_dataMap.insert(asset._asset_sha256, asset);
    }
    const QJsonArray items = root.value(QStringLiteral("items")).toArray();
    const QJsonArray order = root.value(QStringLiteral("order")).toArray();
    if (items.isEmpty() || items.size() > MAX_ITEMS || order.size() != items.size())
    {
        if (error_message) *error_message = QStringLiteral("内部图元剪贴板图元数量或层级无效");
        return false;
    }
    QSet<QString> ids;
    for (int index = 0; index < items.size(); ++index)
    {
        const QJsonObject item_data = items.at(index).toObject();
        const QString item_id = item_data.value(QStringLiteral("item_id")).toString();
        if (item_id.isEmpty() || ids.contains(item_id) || order.at(index).toString() != item_id ||
            !CanvasItemSerializer::ValidateItem(item_data, result._asset_dataMap, error_message)) return false;
        ids.insert(item_id);
        result._items.append(item_data);
        result._item_order.append(item_id);
    }
    *document = result;
    *anchor = QPointF(anchor_array.at(0).toDouble(), anchor_array.at(1).toDouble());
    return true;
}

bool CanvasClipboardPayload::CloneForPaste(const CanvasDocument& source, const QPointF& scene_pos, CanvasDocument* document, QString* error_message)
{
    // 1. 先完整恢复临时图元以计算真实显示范围，旋转、缩放、路径及文字布局均参与定位。
    if (!document || source._items.isEmpty() || source._items.size() != source._item_order.size())
    {
        if (error_message) *error_message = QStringLiteral("没有可粘贴的内部图元");
        return false;
    }
    QRectF bounds;
    bool has_bounds = false;
    for (const CanvasItemData& original : source._items)
    {
        const std::unique_ptr<CanvasItem> item(CanvasItemSerializer::DeserializeItem(original, source._asset_dataMap, error_message));
        if (!item) return false;
        bounds = has_bounds ? bounds.united(item->sceneBoundingRect()) : item->sceneBoundingRect();
        has_bounds = true;
    }

    // 2. 整组使用同一个平移量，将显示中心对齐鼠标，保留内部相对位置和原始几何。
    const QPointF offset = scene_pos - bounds.center();
    CanvasDocument result = source;
    result._items.clear();
    result._item_order.clear();
    for (const CanvasItemData& original : source._items)
    {
        CanvasItemData item_data = original;
        const QJsonArray position = item_data.value(QStringLiteral("position")).toArray();
        if (position.size() != 2)
        {
            if (error_message) *error_message = QStringLiteral("内部图元位置无效");
            return false;
        }
        const QString item_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        item_data.insert(QStringLiteral("item_id"), item_id);
        item_data.insert(QStringLiteral("position"), QJsonArray{position.at(0).toDouble() + offset.x(), position.at(1).toDouble() + offset.y()});
        result._items.append(item_data);
        result._item_order.append(item_id);
    }
    // 3. 所有副本生成新 ID 并构造成功后再交给调用方，失败不改变输出文档。
    *document = result;
    return true;
}
