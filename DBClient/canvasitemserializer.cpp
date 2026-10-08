#include "canvasitemserializer.h"

#include <cmath>

#include <QJsonArray>
#include <QPainterPath>
#include <QTransform>

#include "canvasitems/arrowitem.h"
#include "canvasitems/canvasitem.h"
#include "canvasitems/diamonditem.h"
#include "canvasitems/eraseritem.h"
#include "canvasitems/imageitem.h"
#include "canvasitems/lineitem.h"
#include "canvasitems/ovalitem.h"
#include "canvasitems/pathitem.h"
#include "canvasitems/penitem.h"
#include "canvasitems/rectitem.h"

namespace
{
QJsonArray PointToJson(const QPointF& point)
{
    // 1. 坐标按固定两个分量保存。
    return {point.x(), point.y()};
}

QJsonArray RectToJson(const QRectF& rect)
{
    // 1. 保留局部矩形原点和尺寸。
    return {rect.x(), rect.y(), rect.width(), rect.height()};
}

QJsonArray LineToJson(const QLineF& line)
{
    // 1. 线段端点有方向，不能排序或规范化。
    return {line.x1(), line.y1(), line.x2(), line.y2()};
}

QJsonObject PenToJson(const QPen& pen)
{
    // 1. 保存图元当前画笔和虚线参数。
    QJsonArray dash_pattern;
    for (qreal segment : pen.dashPattern()) dash_pattern.append(segment);
    return {{QStringLiteral("color"), pen.color().name(QColor::HexArgb)},
            {QStringLiteral("width"), pen.widthF()},
            {QStringLiteral("style"), static_cast<int>(pen.style())},
            {QStringLiteral("cap"), static_cast<int>(pen.capStyle())},
            {QStringLiteral("join"), static_cast<int>(pen.joinStyle())},
            {QStringLiteral("dash_pattern"), dash_pattern},
            {QStringLiteral("dash_offset"), pen.dashOffset()},
            {QStringLiteral("miter_limit"), pen.miterLimit()},
            {QStringLiteral("cosmetic"), pen.isCosmetic()}};
}

bool IsNumber(const QJsonValue& value)
{
    // 1. JSON 整数和浮点数均可读，非有限值和字符串均拒绝。
    return value.isDouble() && std::isfinite(value.toDouble());
}

bool ReadPen(const QJsonObject& object, QPen* pen)
{
    // 1. 只接受 Qt 支持的画笔枚举及有效颜色、线宽。
    const QColor color(object.value(QStringLiteral("color")).toString());
    const int style = object.value(QStringLiteral("style")).toInt(-1);
    const int cap = object.value(QStringLiteral("cap")).toInt(-1);
    const int join = object.value(QStringLiteral("join")).toInt(-1);
    const qreal width = object.value(QStringLiteral("width")).toDouble(-1.0);
    if (!color.isValid() || !IsNumber(object.value("width")) || width < 0.0 ||
        style < static_cast<int>(Qt::NoPen) || style > static_cast<int>(Qt::CustomDashLine) ||
        (cap != Qt::FlatCap && cap != Qt::SquareCap && cap != Qt::RoundCap) ||
        (join != Qt::MiterJoin && join != Qt::BevelJoin && join != Qt::RoundJoin && join != Qt::SvgMiterJoin))
    {
        return false;
    }

    *pen = QPen(color, width, static_cast<Qt::PenStyle>(style),
                static_cast<Qt::PenCapStyle>(cap), static_cast<Qt::PenJoinStyle>(join));
    // 2. 自定义虚线必须保留完整周期，其余补充样式也参与往返。
    const QJsonArray dash_pattern = object.value("dash_pattern").toArray();
    if (style == Qt::CustomDashLine)
    {
        QList<qreal> segments;
        for (const QJsonValue& segment : dash_pattern)
        {
            if (!IsNumber(segment) || segment.toDouble() <= 0.0) return false;
            segments.append(segment.toDouble());
        }
        if (segments.isEmpty() || segments.size() % 2 != 0) return false;
        pen->setDashPattern(segments);
    }
    pen->setDashOffset(object.value("dash_offset").toDouble());
    pen->setMiterLimit(object.value("miter_limit").toDouble(2.0));
    pen->setCosmetic(object.value("cosmetic").toBool());
    return true;
}

QJsonArray TransformToJson(const QTransform& transform)
{
    // 1. 完整矩阵支持非等比缩放，不与视图缩放混用。
    return {transform.m11(), transform.m12(), transform.m13(),
            transform.m21(), transform.m22(), transform.m23(),
            transform.m31(), transform.m32(), transform.m33()};
}

bool ReadPair(const QJsonValue& value, QPointF* point)
{
    // 1. 读取两个明确的数值分量。
    const QJsonArray array = value.toArray();
    if (array.size() != 2 || !IsNumber(array.at(0)) || !IsNumber(array.at(1)))
    {
        return false;
    }
    *point = QPointF(array.at(0).toDouble(), array.at(1).toDouble());
    return true;
}

bool ReadRect(const QJsonValue& value, QRectF* rect)
{
    // 1. 保留局部矩形范围，零尺寸几何也可能来自正常的点击。
    const QJsonArray array = value.toArray();
    if (array.size() != 4)
    {
        return false;
    }
    for (const QJsonValue& component : array)
    {
        if (!IsNumber(component))
        {
            return false;
        }
    }
    *rect = QRectF(array.at(0).toDouble(), array.at(1).toDouble(),
                   array.at(2).toDouble(), array.at(3).toDouble());
    return true;
}

bool ReadLine(const QJsonValue& value, QLineF* line)
{
    // 1. 线段保持起点终点的原始方向。
    const QJsonArray array = value.toArray();
    if (array.size() != 4)
    {
        return false;
    }
    for (const QJsonValue& component : array)
    {
        if (!IsNumber(component))
        {
            return false;
        }
    }
    *line = QLineF(array.at(0).toDouble(), array.at(1).toDouble(),
                   array.at(2).toDouble(), array.at(3).toDouble());
    return true;
}

bool ReadPath(const QJsonValue& value, QPainterPath* path)
{
    // 1. 校验路径元素和贝塞尔控制点，局部坐标直接恢复。
    const QJsonArray elements = value.toArray();
    if (elements.size() > CanvasDocumentLimits::MAX_PATH_ELEMENTS)
    {
        return false;
    }

    QPainterPath result;
    for (int index = 0; index < elements.size(); ++index)
    {
        const QJsonArray element = elements.at(index).toArray();
        if (element.size() != 3 || !IsNumber(element.at(1)) || !IsNumber(element.at(2)))
        {
            return false;
        }
        const int type = element.at(0).toInt(-1);
        const QPointF point(element.at(1).toDouble(), element.at(2).toDouble());
        if (index == 0 && type != QPainterPath::MoveToElement) return false;
        if (type == QPainterPath::MoveToElement)
        {
            result.moveTo(point);
        } else if (type == QPainterPath::LineToElement)
        {
            result.lineTo(point);
        } else if (type == QPainterPath::CurveToElement && index + 2 < elements.size())
        {
            const QJsonArray control_array = elements.at(index + 1).toArray();
            const QJsonArray end_array = elements.at(index + 2).toArray();
            if (control_array.size() != 3 || end_array.size() != 3 ||
                control_array.at(0).toInt(-1) != QPainterPath::CurveToDataElement ||
                end_array.at(0).toInt(-1) != QPainterPath::CurveToDataElement ||
                !IsNumber(control_array.at(1)) || !IsNumber(control_array.at(2)) ||
                !IsNumber(end_array.at(1)) || !IsNumber(end_array.at(2)))
            {
                return false;
            }
            result.cubicTo(point,
                           QPointF(control_array.at(1).toDouble(), control_array.at(2).toDouble()),
                           QPointF(end_array.at(1).toDouble(), end_array.at(2).toDouble()));
            index += 2;
        } else
        {
            return false;
        }
    }
    *path = result;
    return true;
}

QString TypeForItem(CanvasItem* item)
{
    // 1. 文件类型使用稳定字符串，独立于网络枚举编号。
    if (dynamic_cast<PenItem*>(item)) return QStringLiteral("pen");
    if (dynamic_cast<EraserItem*>(item)) return QStringLiteral("eraser");
    if (dynamic_cast<RectItem*>(item)) return QStringLiteral("rect");
    if (dynamic_cast<OvalItem*>(item)) return QStringLiteral("oval");
    if (dynamic_cast<LineItem*>(item)) return QStringLiteral("line");
    if (dynamic_cast<ArrowItem*>(item)) return QStringLiteral("arrow");
    if (dynamic_cast<DiamondItem*>(item)) return QStringLiteral("diamond");
    if (dynamic_cast<ImageItem*>(item)) return QStringLiteral("image");
    return QString();
}

}

bool CanvasItemSerializer::ValidateItem(const QJsonObject& item_data,
                                        const QMap<QString, CanvasAssetData>& asset_dataMap,
                                        QString* error_message)
{
    // 1. 后台只解析值对象，不创建 GUI 图元。
    QPointF position;
    QPointF origin;
    QPen pen;
    const QString type = item_data.value("type").toString();
    const QJsonArray matrix = item_data.value("transform").toArray();
    bool is_valid = !item_data.value("item_id").toString().isEmpty() &&
                    ReadPair(item_data.value("position"), &position) &&
                    ReadPair(item_data.value("transform_origin"), &origin) && matrix.size() == 9;
    for (const QJsonValue& component : matrix) is_valid = is_valid && IsNumber(component);
    for (const QString& field : {QString("rotation"), QString("scale"), QString("opacity"), QString("z_value")})
    {
        is_valid = is_valid && IsNumber(item_data.value(field));
    }
    is_valid = is_valid && item_data.value("opacity").toDouble() >= 0.0 && item_data.value("opacity").toDouble() <= 1.0;
    if (type != "image") is_valid = is_valid && ReadPen(item_data.value("pen").toObject(), &pen);

    // 2. 检查对应几何及图片资源元数据，防止隐式默认值掩盖非法文件。
    if (type == "pen" || type == "eraser")
    {
        QPainterPath path;
        is_valid = is_valid && item_data.value("path").isArray() && ReadPath(item_data.value("path"), &path);
        const int fill_rule = item_data.value("fill_rule").toInt(-1);
        is_valid = is_valid && (fill_rule == Qt::OddEvenFill || fill_rule == Qt::WindingFill);
    } else if (type == "rect" || type == "oval" || type == "diamond")
    {
        QRectF rect;
        is_valid = is_valid && ReadRect(item_data.value("rect"), &rect) && rect.width() >= 0.0 && rect.height() >= 0.0;
    } else if (type == "line" || type == "arrow")
    {
        QLineF line;
        is_valid = is_valid && ReadLine(item_data.value("line"), &line);
    } else if (type == "image")
    {
        QPointF original_size;
        QPointF display_size;
        const auto iterator = asset_dataMap.constFind(item_data.value("asset_sha256").toString());
        is_valid = is_valid && iterator != asset_dataMap.cend() &&
                   ReadPair(item_data.value("original_size"), &original_size) &&
                   ReadPair(item_data.value("display_size"), &display_size) && display_size.x() >= 1.0 && display_size.y() >= 1.0;
        if (is_valid)
        {
            is_valid = original_size.x() == iterator->_original_size.width() &&
                       original_size.y() == iterator->_original_size.height() &&
                       item_data.value("mime_type").toString() == iterator->_mime_type;
        }
    } else
    {
        is_valid = false;
    }
    if (!is_valid && error_message) *error_message = QStringLiteral("图元类型、属性、几何或资源引用无效");
    return is_valid;
}

bool CanvasItemSerializer::SerializeItem(CanvasItem* item,
                                         QJsonObject* item_data,
                                         CanvasDocument* document,
                                         QString* error_message)
{
    // 1. 类型未知或 ID 为空时拒绝保存会话辅助图元。
    if (!item || !item_data || !document || item->itemId().isEmpty())
    {
        if (error_message) *error_message = QStringLiteral("画布中存在无法保存的图元");
        return false;
    }

    // 2. 保存公共变换和样式，再追加具体几何。
    const QString type = TypeForItem(item);
    if (type.isEmpty())
    {
        if (error_message) *error_message = QStringLiteral("画布包含不支持的图元类型");
        return false;
    }
    QJsonObject data{{QStringLiteral("item_id"), item->itemId()},
                     {QStringLiteral("type"), type},
                     {QStringLiteral("position"), PointToJson(item->pos())},
                     {QStringLiteral("transform"), TransformToJson(item->transform())},
                     {QStringLiteral("transform_origin"), PointToJson(item->transformOriginPoint())},
                     {QStringLiteral("rotation"), item->rotation()},
                     {QStringLiteral("scale"), item->scale()},
                     {QStringLiteral("opacity"), item->opacity()},
                     {QStringLiteral("z_value"), item->zValue()}};

    if (auto* path_item = dynamic_cast<PathItem*>(item))
    {
        const QPainterPath path = path_item->path();
        QJsonArray elements;
        for (int index = 0; index < path.elementCount(); ++index)
        {
            const QPainterPath::Element element = path.elementAt(index);
            elements.append(QJsonArray{element.type, element.x, element.y});
        }
        data.insert(QStringLiteral("path"), elements);
        data.insert(QStringLiteral("fill_rule"), static_cast<int>(path.fillRule()));
        data.insert(QStringLiteral("pen"), PenToJson(path_item->pen()));
    } else if (auto* rect_item = dynamic_cast<RectItem*>(item))
    {
        data.insert(QStringLiteral("rect"), RectToJson(rect_item->rect()));
        data.insert(QStringLiteral("pen"), PenToJson(rect_item->pen()));
    } else if (auto* oval_item = dynamic_cast<OvalItem*>(item))
    {
        data.insert(QStringLiteral("rect"), RectToJson(oval_item->rect()));
        data.insert(QStringLiteral("pen"), PenToJson(oval_item->pen()));
    } else if (auto* line_item = dynamic_cast<LineItem*>(item))
    {
        data.insert(QStringLiteral("line"), LineToJson(line_item->line()));
        data.insert(QStringLiteral("pen"), PenToJson(line_item->pen()));
    } else if (auto* arrow_item = dynamic_cast<ArrowItem*>(item))
    {
        data.insert(QStringLiteral("line"), LineToJson(arrow_item->Line()));
        data.insert(QStringLiteral("pen"), PenToJson(arrow_item->Pen()));
    } else if (auto* diamond_item = dynamic_cast<DiamondItem*>(item))
    {
        data.insert(QStringLiteral("rect"), RectToJson(diamond_item->Rect()));
        data.insert(QStringLiteral("pen"), PenToJson(diamond_item->Pen()));
    } else if (auto* image_item = dynamic_cast<ImageItem*>(item))
    {
        if (image_item->loadState() != ImageItem::ImageLoadState::Ready || image_item->pixmap().isNull())
        {
            if (error_message) *error_message = QStringLiteral("图片尚未加载完成，请等待后再导出");
            return false;
        }
        const QString sha256 = image_item->assetSha256();
        CanvasAssetData& asset = document->_asset_dataMap[sha256];
        if (asset._asset_sha256.isEmpty())
        {
            asset._asset_sha256 = sha256;
            asset._mime_type = image_item->mimeType();
            asset._original_size = image_item->originalSize();
        }
        data.insert(QStringLiteral("asset_sha256"), sha256);
        data.insert(QStringLiteral("asset_id"), image_item->assetId());
        data.insert(QStringLiteral("asset_ref"), image_item->assetRef());
        data.insert(QStringLiteral("mime_type"), image_item->mimeType());
        data.insert(QStringLiteral("original_size"), QJsonArray{image_item->originalSize().width(), image_item->originalSize().height()});
        data.insert(QStringLiteral("display_size"), QJsonArray{image_item->displaySize().width(), image_item->displaySize().height()});
    }

    *item_data = data;
    return true;
}

CanvasItem* CanvasItemSerializer::DeserializeItem(const QJsonObject& item_data,
                                                 const QMap<QString, CanvasAssetData>& asset_dataMap,
                                                 QString* error_message)
{
    // 1. 读取稳定 ID、类型、绘画样式和公共变换。
    const QString item_id = item_data.value(QStringLiteral("item_id")).toString();
    const QString type = item_data.value(QStringLiteral("type")).toString();
    QPointF position;
    QPen pen;
    if (!ValidateItem(item_data, asset_dataMap, error_message) ||
        !ReadPair(item_data.value(QStringLiteral("position")), &position))
    {
        if (error_message) *error_message = QStringLiteral("图元属性不完整或画笔枚举无效");
        return nullptr;
    }
    if (type != "image") ReadPen(item_data.value("pen").toObject(), &pen);

    const QJsonArray matrix = item_data.value(QStringLiteral("transform")).toArray();
    if (matrix.size() != 9)
    {
        if (error_message) *error_message = QStringLiteral("图元变换数据无效");
        return nullptr;
    }
    for (const QJsonValue& value : matrix)
    {
        if (!IsNumber(value))
        {
            if (error_message) *error_message = QStringLiteral("图元变换数据无效");
            return nullptr;
        }
    }
    auto* item = [&]() -> CanvasItem* {
        if (type == "pen") return new PenItem(item_id);
        if (type == "eraser") return new EraserItem(item_id);
        if (type == "rect") return new RectItem(item_id);
        if (type == "oval") return new OvalItem(item_id);
        if (type == "line") return new LineItem(item_id);
        if (type == "arrow") return new ArrowItem(item_id);
        if (type == "diamond") return new DiamondItem(item_id);
        if (type == "image") return new ImageItem(item_id);
        return nullptr;
    }();
    if (!item)
    {
        if (error_message) *error_message = QStringLiteral("文件包含不支持的图元类型");
        return nullptr;
    }

    // 2. 按具体图元类型恢复完整几何，并在失败时释放尚未加入场景的对象。
    bool is_valid = true;
    if (auto* path_item = dynamic_cast<PathItem*>(item))
    {
        QPainterPath path;
        is_valid = ReadPath(item_data.value(QStringLiteral("path")), &path);
        if (is_valid)
        {
            path.setFillRule(static_cast<Qt::FillRule>(item_data.value("fill_rule").toInt()));
            path_item->setPath(path);
            path_item->setPen(pen);
        }
    } else if (auto* rect_item = dynamic_cast<RectItem*>(item))
    {
        QRectF rect;
        is_valid = ReadRect(item_data.value(QStringLiteral("rect")), &rect);
        if (is_valid)
        {
            rect_item->setRect(rect);
            rect_item->setPen(pen);
        }
    } else if (auto* oval_item = dynamic_cast<OvalItem*>(item))
    {
        QRectF rect;
        is_valid = ReadRect(item_data.value(QStringLiteral("rect")), &rect);
        if (is_valid)
        {
            oval_item->setRect(rect);
            oval_item->setPen(pen);
        }
    } else if (auto* line_item = dynamic_cast<LineItem*>(item))
    {
        QLineF line;
        is_valid = ReadLine(item_data.value(QStringLiteral("line")), &line);
        if (is_valid)
        {
            line_item->setLine(line);
            line_item->setPen(pen);
        }
    } else if (auto* arrow_item = dynamic_cast<ArrowItem*>(item))
    {
        QLineF line;
        is_valid = ReadLine(item_data.value(QStringLiteral("line")), &line);
        if (is_valid)
        {
            arrow_item->SetLine(line);
            arrow_item->SetPen(pen);
        }
    } else if (auto* diamond_item = dynamic_cast<DiamondItem*>(item))
    {
        QRectF rect;
        is_valid = ReadRect(item_data.value(QStringLiteral("rect")), &rect);
        if (is_valid)
        {
            diamond_item->SetRect(rect);
            diamond_item->SetPen(pen);
        }
    } else if (auto* image_item = dynamic_cast<ImageItem*>(item))
    {
        QPointF original_dimensions;
        QPointF display_dimensions;
        const QString sha256 = item_data.value(QStringLiteral("asset_sha256")).toString();
        const auto asset_iterator = asset_dataMap.constFind(sha256);
        is_valid = ReadPair(item_data.value(QStringLiteral("original_size")), &original_dimensions) &&
                   ReadPair(item_data.value(QStringLiteral("display_size")), &display_dimensions) &&
                   asset_iterator != asset_dataMap.cend();
        if (is_valid)
        {
            image_item->setAssetMetadata(item_data.value(QStringLiteral("asset_id")).toString(),
                                         item_data.value(QStringLiteral("asset_ref")).toString(), sha256,
                                         item_data.value(QStringLiteral("mime_type")).toString());
            image_item->setOriginalSize(QSize(qRound(original_dimensions.x()), qRound(original_dimensions.y())));
            image_item->setDisplaySize(QSizeF(display_dimensions.x(), display_dimensions.y()));
            image_item->setPixmap(QPixmap::fromImage(asset_iterator->_image));
            is_valid = !image_item->pixmap().isNull();
        }
    }
    if (!is_valid)
    {
        delete item;
        if (error_message) *error_message = QStringLiteral("图元几何或图片资源无法读取");
        return nullptr;
    }

    // 3. 公共变换在局部几何恢复后应用，保持路径与图元坐标一致。
    item->setPos(position);
    QPointF origin;
    ReadPair(item_data.value("transform_origin"), &origin);
    item->setTransformOriginPoint(origin);
    item->setTransform(QTransform(matrix.at(0).toDouble(), matrix.at(1).toDouble(), matrix.at(2).toDouble(),
                                   matrix.at(3).toDouble(), matrix.at(4).toDouble(), matrix.at(5).toDouble(),
                                   matrix.at(6).toDouble(), matrix.at(7).toDouble(), matrix.at(8).toDouble()));
    item->setRotation(item_data.value(QStringLiteral("rotation")).toDouble());
    item->setScale(item_data.value(QStringLiteral("scale")).toDouble(1.0));
    item->setOpacity(item_data.value(QStringLiteral("opacity")).toDouble(1.0));
    item->setZValue(item_data.value(QStringLiteral("z_value")).toDouble());
    return item;
}
