#include "canvasfilemanager.h"

#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <QtConcurrent>

#include "assetserviceprotocol.h"
#include "canvasitemserializer.h"
#include "imageassetmanager.h"

namespace
{
QJsonArray ColorToJson(const QColor& color)
{
    // 1. 将纯色背景和透明度保存为整数通道。
    return {color.red(), color.green(), color.blue(), color.alpha()};
}

bool IsSupportedImageMime(const QString& mime_type)
{
    // 1. 只接受图片代理已经支持的编码类型。
    return mime_type == QStringLiteral("image/png") ||
           mime_type == QStringLiteral("image/jpeg") ||
           mime_type == QStringLiteral("image/webp");
}

bool ValidateDocument(const CanvasDocument& document, QString* error_message)
{
    // 1. 文件级画布与数量限制先挡住异常的大尺寸和容器数量。
    if (document._schema_version != 1 || !document._canvas_size.isValid() || document._canvas_size.isEmpty() ||
        !document._background.isValid() || document._items.size() > CanvasDocumentLimits::MAX_ITEMS ||
        document._item_order.size() != document._items.size())
    {
        if (error_message) *error_message = QStringLiteral("画布尺寸、版本或图元数量无效");
        return false;
    }

    // 2. 图元 ID 和顺序必须一一对应，路径元素使用统一总预算。
    QSet<QString> item_ids;
    int path_elements = 0;
    for (const QJsonObject& item : document._items)
    {
        const QString item_id = item.value(QStringLiteral("item_id")).toString();
        if (item_id.isEmpty() || item_ids.contains(item_id))
        {
            if (error_message) *error_message = QStringLiteral("画布文件包含空 ID 或重复图元 ID");
            return false;
        }
        item_ids.insert(item_id);
        path_elements += item.value(QStringLiteral("path")).toArray().size();
        if (path_elements > CanvasDocumentLimits::MAX_PATH_ELEMENTS)
        {
            if (error_message) *error_message = QStringLiteral("画布路径数据超出限制");
            return false;
        }
    }
    QSet<QString> order_ids;
    for (const QString& item_id : document._item_order)
    {
        if (!item_ids.contains(item_id) || order_ids.contains(item_id))
        {
            if (error_message) *error_message = QStringLiteral("图元顺序与图元列表不一致");
            return false;
        }
        order_ids.insert(item_id);
    }

    // 3. 所有内嵌资源按字节、解码像素和内容摘要检查。
    qint64 asset_bytes = 0;
    qint64 image_pixels = 0;
    for (auto iterator = document._asset_dataMap.cbegin(); iterator != document._asset_dataMap.cend(); ++iterator)
    {
        const CanvasAssetData& asset = iterator.value();
        asset_bytes += asset._data.size();
        image_pixels += static_cast<qint64>(asset._original_size.width()) * asset._original_size.height();
        if (asset_bytes > CanvasDocumentLimits::MAX_ASSET_BYTES || image_pixels > CanvasDocumentLimits::MAX_IMAGE_PIXELS ||
            asset._data.size() > AssetServiceProtocol::MAX_IMAGE_BYTES ||
            asset._original_size.width() <= 0 || asset._original_size.height() <= 0 ||
            asset._original_size.width() > AssetServiceProtocol::MAX_IMAGE_DIMENSION ||
            asset._original_size.height() > AssetServiceProtocol::MAX_IMAGE_DIMENSION)
        {
            if (error_message) *error_message = QStringLiteral("画布图片资源总量或单张尺寸超出限制");
            return false;
        }
        const AssetCacheResult image_result = AssetServiceProtocol::ValidateImage(asset._data);
        if (image_result._status != "ok" || image_result._record._asset_sha256 != asset._asset_sha256 ||
            image_result._record._mime_type != asset._mime_type || image_result._record._original_size != asset._original_size)
        {
            if (error_message) *error_message = QStringLiteral("画布图片数据与资源描述不匹配");
            return false;
        }
    }

    for (const QJsonObject& item : document._items)
    {
        if (!CanvasItemSerializer::ValidateItem(item, document._asset_dataMap, error_message)) return false;
    }
    return true;
}

QJsonObject DocumentToJson(const CanvasDocument& document)
{
    // 1. 图元顺序独立保存，资源通过摘要去重并内嵌原始编码数据。
    QJsonArray items;
    for (const QJsonObject& item : document._items) items.append(item);
    QJsonArray item_order;
    for (const QString& item_id : document._item_order) item_order.append(item_id);
    QJsonArray assets;
    for (auto iterator = document._asset_dataMap.cbegin(); iterator != document._asset_dataMap.cend(); ++iterator)
    {
        const CanvasAssetData& asset = iterator.value();
        assets.append(QJsonObject{
            {QStringLiteral("sha256"), asset._asset_sha256},
            {QStringLiteral("mime_type"), asset._mime_type},
            {QStringLiteral("width"), asset._original_size.width()},
            {QStringLiteral("height"), asset._original_size.height()},
            {QStringLiteral("byte_size"), asset._data.size()},
            {QStringLiteral("data"), QStringLiteral("data:%1;base64,%2").arg(asset._mime_type, QString::fromLatin1(asset._data.toBase64()))}});
    }
    return {{QStringLiteral("format"), QStringLiteral("SyncCanvas")},
            {QStringLiteral("schema_version"), document._schema_version},
            {QStringLiteral("canvas"), QJsonObject{
                 {QStringLiteral("width"), document._canvas_size.width()},
                 {QStringLiteral("height"), document._canvas_size.height()},
                 {QStringLiteral("background_rgba"), ColorToJson(document._background)}}},
            {QStringLiteral("items"), items},
            {QStringLiteral("item_order"), item_order},
            {QStringLiteral("connections"), QJsonArray{}},
            {QStringLiteral("assets"), assets}};
}
}

CanvasFileManager::CanvasFileManager(ImageAssetManager* asset_manager, QObject* parent)
    : QObject(parent)
    , _asset_manager(asset_manager)
{
    // 1. 文件任务复用既有图片缓存代理读取和登记编码资源。
    _file_thread_pool.setMaxThreadCount(2);
    connect(_asset_manager, &ImageAssetManager::sigAssetDataReady,
            this, [this](const QString& request_id, const QByteArray& data) {
        if (request_id != _asset_request_id || _active_generation != _generation) return;
        if (_is_import)
        {
            // 2. 内嵌资源已通过代理登记；释放请求上下文后继续准备下一项。
            _asset_request_id.clear();
            _active_asset_sha256.clear();
            PrepareNextAsset();
            return;
        }
        _pending_document._asset_dataMap[_active_asset_sha256]._data = data;
        _asset_request_id.clear();
        _active_asset_sha256.clear();
        PrepareNextAsset();
    });
    connect(_asset_manager, &ImageAssetManager::sigAssetDataFailed,
            this, [this](const QString& request_id, const QString& error_message) {
        if (request_id != _asset_request_id || _active_generation != _generation) return;
        const QString operation_id = _request_id;
        const bool is_import = _is_import;
        CancelFileOperation();
        if (is_import)
        {
            emit sigImportReady(operation_id, CanvasDocument(), error_message);
        } else
        {
            emit sigExportFinished(operation_id, false,
                                   QStringLiteral("图片资源无法从缓存读取，请重新加载图片后导出"));
        }
    });
}

CanvasFileManager::~CanvasFileManager()
{
    // 1. 所有 worker 仅捕获值对象，等待结束后 watcher 可随 QObject 安全销毁。
    CancelFileOperation();
    _file_thread_pool.waitForDone();
}

void CanvasFileManager::ImportCanvasAsync(const QString& file_path, const QString& request_id)
{
    // 1. 扩展名错误立即返回，普通图片明确引导到插入图片入口。
    if (QFileInfo(file_path).suffix().compare(QStringLiteral("synccanvas"), Qt::CaseInsensitive) != 0)
    {
        const QStringList image_suffixes{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                        QStringLiteral("webp"), QStringLiteral("bmp"), QStringLiteral("gif")};
        const QString error = image_suffixes.contains(QFileInfo(file_path).suffix().toLower())
            ? QStringLiteral("这是图片文件，请使用“插入图片”") : QStringLiteral("请选择 .synccanvas 画布文件");
        emit sigImportReady(request_id, CanvasDocument(), error);
        return;
    }

    // 2. 文件读取、JSON 解析和资源解码放在文件任务池完成。
    CancelFileOperation();
    _request_id = request_id;
    _is_import = true;
    _pending_file_path = file_path;
    _active_generation = _generation;
    auto* watcher = new QFutureWatcher<CanvasImportResult>(this);
    const quint64 generation = _active_generation;
    connect(watcher, &QFutureWatcher<CanvasImportResult>::finished, this, [this, watcher, request_id, generation] {
        const CanvasImportResult result = watcher->result();
        watcher->deleteLater();
        if (generation != _generation || request_id != _request_id) return;
        if (!result._error_message.isEmpty())
        {
            const QString operation_id = _request_id;
            CancelFileOperation();
            emit sigImportReady(operation_id, CanvasDocument(), result._error_message);
            return;
        }
        _pending_document = result._document;
        _pending_assets = _pending_document._asset_dataMap.keys();
        PrepareNextAsset();
    });
    watcher->setFuture(QtConcurrent::run(&_file_thread_pool, [file_path] {
        CanvasImportResult result;
        ReadDocument(file_path, &result._document, &result._error_message);
        return result;
    }));
}

void CanvasFileManager::ExportCanvasAsync(const CanvasDocument& document,
                                         const QString& file_path,
                                         const QString& request_id)
{
    // 1. 缓存读取成功后才写入单文件，资源缺失则终止整个导出。
    CancelFileOperation();
    _pending_document = document;
    _pending_file_path = file_path;
    _request_id = request_id;
    _is_import = false;
    _active_generation = _generation;
    _pending_assets.clear();
    for (auto iterator = _pending_document._asset_dataMap.begin(); iterator != _pending_document._asset_dataMap.end(); ++iterator)
    {
        if (iterator.value()._data.isEmpty()) _pending_assets.append(iterator.key());
    }
    PrepareNextAsset();
}

void CanvasFileManager::CancelFileOperation()
{
    // 1. 先递增代次，使文件解析和代理回包不再提交到当前画布。
    ++_generation;
    if (!_asset_request_id.isEmpty() && _asset_manager)
    {
        _asset_manager->CancelAssetDataRequest(_asset_request_id);
    }
    _asset_request_id.clear();
    _active_asset_sha256.clear();
    _pending_assets.clear();
    _pending_document = CanvasDocument();
    _pending_file_path.clear();
    _request_id.clear();
}

void CanvasFileManager::PrepareNextAsset()
{
    // 1. 资源按摘要串行复用 IPC，确保取消和失败都能关联到整份文件。
    if (_pending_assets.isEmpty())
    {
        if (_is_import)
        {
            const QString request_id = _request_id;
            const CanvasDocument document = _pending_document;
            CancelFileOperation();
            emit sigImportReady(request_id, document, QString());
        } else
        {
            WritePendingDocument();
        }
        return;
    }
    _active_asset_sha256 = _pending_assets.takeFirst();
    _asset_request_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const CanvasAssetData asset = _pending_document._asset_dataMap.value(_active_asset_sha256);
    if (_is_import)
    {
        _asset_manager->ImportAssetDataAsync(asset._data, _asset_request_id);
    } else
    {
        _asset_manager->ReadAssetDataAsync(_active_asset_sha256, _asset_request_id);
    }
}

void CanvasFileManager::WritePendingDocument()
{
    // 1. 捕获完整快照后在后台校验并原子写入 JSON。
    const CanvasDocument document = _pending_document;
    const QString file_path = _pending_file_path;
    const QString request_id = _request_id;
    const quint64 generation = _active_generation;
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, request_id, generation] {
        const QString error_message = watcher->result();
        watcher->deleteLater();
        if (generation != _generation || request_id != _request_id) return;
        CancelFileOperation();
        emit sigExportFinished(request_id, error_message.isEmpty(), error_message);
    });
    watcher->setFuture(QtConcurrent::run(&_file_thread_pool, [document, file_path] {
        QString error_message;
        WriteDocument(document, file_path, &error_message);
        return error_message;
    }));
}

void CanvasFileManager::ExportImageAsync(const QImage& image,
                                        const QString& file_path,
                                        const QByteArray& format,
                                        const QString& request_id)
{
    // 1. 编码和原子提交在后台运行，不阻塞画布交互。
    CancelFileOperation();
    _request_id = request_id;
    const quint64 generation = _generation;
    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, request_id, generation] {
        const bool is_success = watcher->result();
        watcher->deleteLater();
        if (generation != _generation || request_id != _request_id) return;
        CancelFileOperation();
        emit sigExportFinished(request_id, is_success,
                               is_success ? QString() : QStringLiteral("图片文件保存失败，请检查路径和磁盘空间"));
    });
    watcher->setFuture(QtConcurrent::run(&_file_thread_pool, [image, file_path, format] {
        // 2. JPG 不支持透明度，将完整画布合成到白底后以固定质量编码。
        QImage output_image = image;
        if (format == "jpg")
        {
            output_image = QImage(image.size(), QImage::Format_RGB32);
            if (output_image.isNull()) return false;
            output_image.fill(Qt::white);
            QPainter painter(&output_image);
            painter.drawImage(0, 0, image);
        }
        QSaveFile output_file(file_path);
        if (!output_file.open(QIODevice::WriteOnly) || !output_image.save(&output_file, format.constData(), format == "jpg" ? 90 : -1))
        {
            output_file.cancelWriting();
            return false;
        }
        return output_file.commit();
    }));
}

bool CanvasFileManager::ReadDocument(const QString& file_path,
                                     CanvasDocument* document,
                                     QString* error_message)
{
    // 1. 先限制文件大小并严格解析 JSON 根对象。
    QFile input_file(file_path);
    if (!input_file.open(QIODevice::ReadOnly) || input_file.size() <= 0 || input_file.size() > CanvasDocumentLimits::MAX_FILE_BYTES)
    {
        if (error_message) *error_message = QStringLiteral("画布文件无法读取或超过 256 MiB");
        return false;
    }
    QJsonParseError parse_error;
    const QJsonDocument json_document = QJsonDocument::fromJson(input_file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !json_document.isObject())
    {
        if (error_message) *error_message = QStringLiteral("画布 JSON 格式无效");
        return false;
    }
    const QJsonObject root = json_document.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("SyncCanvas") ||
        root.value(QStringLiteral("schema_version")).toInt() != 1)
    {
        if (error_message) *error_message = QStringLiteral("画布文件格式或版本不受支持");
        return false;
    }

    // 2. 解出尺寸、背景、图元和顺序，当前版本只接受空连接表。
    const QJsonObject canvas = root.value(QStringLiteral("canvas")).toObject();
    const int width = canvas.value(QStringLiteral("width")).toInt();
    const int height = canvas.value(QStringLiteral("height")).toInt();
    const QJsonArray background = canvas.value(QStringLiteral("background_rgba")).toArray();
    const QJsonArray items = root.value(QStringLiteral("items")).toArray();
    const QJsonArray order = root.value(QStringLiteral("item_order")).toArray();
    if (!root.value("canvas").isObject() || !root.value("items").isArray() ||
        !root.value("item_order").isArray() || !root.value("assets").isArray() ||
        items.size() > CanvasDocumentLimits::MAX_ITEMS || order.size() != items.size() ||
        background.size() != 4 || !root.value(QStringLiteral("connections")).isArray())
    {
        if (error_message) *error_message = QStringLiteral("画布结构不完整或数量超出限制");
        return false;
    }
    if (!root.value(QStringLiteral("connections")).toArray().isEmpty())
    {
        if (error_message) *error_message = QStringLiteral("此版本暂不支持恢复图元连接关系");
        return false;
    }
    document->_schema_version = 1;
    document->_canvas_size = QSize(width, height);
    document->_background = QColor(background.at(0).toInt(-1), background.at(1).toInt(-1),
                                   background.at(2).toInt(-1), background.at(3).toInt(-1));
    for (const QJsonValue& item : items)
    {
        if (!item.isObject())
        {
            if (error_message) *error_message = QStringLiteral("图元数据格式无效");
            return false;
        }
        document->_items.append(item.toObject());
    }
    for (const QJsonValue& item_id : order)
    {
        if (!item_id.isString())
        {
            if (error_message) *error_message = QStringLiteral("图元顺序数据格式无效");
            return false;
        }
        document->_item_order.append(item_id.toString());
    }

    // 3. Base64 资源恢复后用同一图片代理校验器检查哈希、MIME 和像素尺寸。
    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue& asset_value : assets)
    {
        const QJsonObject asset_json = asset_value.toObject();
        const QString sha256 = asset_json.value(QStringLiteral("sha256")).toString();
        const QString mime_type = asset_json.value(QStringLiteral("mime_type")).toString();
        const QString data_url = asset_json.value(QStringLiteral("data")).toString();
        const QString prefix = QStringLiteral("data:%1;base64,").arg(mime_type);
        if (sha256.isEmpty() || !IsSupportedImageMime(mime_type) || !data_url.startsWith(prefix) || document->_asset_dataMap.contains(sha256))
        {
            if (error_message) *error_message = QStringLiteral("图片资源描述无效");
            return false;
        }
        CanvasAssetData asset;
        asset._asset_sha256 = sha256;
        asset._mime_type = mime_type;
        asset._original_size = QSize(asset_json.value(QStringLiteral("width")).toInt(), asset_json.value(QStringLiteral("height")).toInt());
        asset._data = QByteArray::fromBase64(data_url.mid(prefix.size()).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (asset._data.size() != asset_json.value(QStringLiteral("byte_size")).toInt())
        {
            if (error_message) *error_message = QStringLiteral("图片资源字节长度不匹配");
            return false;
        }
        document->_asset_dataMap.insert(sha256, asset);
    }
    if (!ValidateDocument(*document, error_message)) return false;
    for (auto iterator = document->_asset_dataMap.begin(); iterator != document->_asset_dataMap.end(); ++iterator)
    {
        iterator.value()._image = QImage::fromData(iterator.value()._data);
        if (iterator.value()._image.isNull())
        {
            if (error_message) *error_message = QStringLiteral("画布图片无法解码");
            return false;
        }
    }
    return true;
}

bool CanvasFileManager::WriteDocument(const CanvasDocument& document,
                                     const QString& file_path,
                                     QString* error_message)
{
    // 1. 统一校验快照，序列化为 UTF-8 JSON 并检查单文件上限。
    if (!ValidateDocument(document, error_message)) return false;
    const QByteArray json = QJsonDocument(DocumentToJson(document)).toJson(QJsonDocument::Indented);
    if (json.size() > CanvasDocumentLimits::MAX_FILE_BYTES)
    {
        if (error_message) *error_message = QStringLiteral("画布文件超过 256 MiB");
        return false;
    }

    // 2. 临时文件完整写入并成功提交后才替换目标文件。
    QSaveFile output_file(file_path);
    if (!output_file.open(QIODevice::WriteOnly) || output_file.write(json) != json.size() || !output_file.commit())
    {
        output_file.cancelWriting();
        if (error_message) *error_message = QStringLiteral("画布文件提交失败");
        return false;
    }
    return true;
}
