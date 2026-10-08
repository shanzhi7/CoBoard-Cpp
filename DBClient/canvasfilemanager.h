/***********************************************************************************
* @file         canvasfilemanager.h
* @brief        画布 JSON 文件异步读写及图片文件导出
* @author       shanzhi
* @date         2026/10/08
* @history
***********************************************************************************/
#pragma once

#include <QByteArray>
#include <QObject>
#include <QStringList>
#include <QThreadPool>

#include "canvasdocument.h"

class QImage;
class ImageAssetManager;

class CanvasFileManager : public QObject
{
    Q_OBJECT
public:
    explicit CanvasFileManager(ImageAssetManager* asset_manager, QObject* parent = nullptr); // 复用图片代理并初始化文件任务池。
    ~CanvasFileManager() override; // 等待文件任务结束后释放管理器。
    void ImportCanvasAsync(const QString& file_path, const QString& request_id); // 后台读取并校验画布 JSON。
    void ExportCanvasAsync(const CanvasDocument& document, const QString& file_path, const QString& request_id); // 原子写入自包含画布文件。
    void ExportImageAsync(const QImage& image, const QString& file_path, const QByteArray& format, const QString& request_id); // 原子编码并写入 PNG 或 JPG。
    void CancelFileOperation(); // 废弃文件任务和资源请求的迟到结果。

signals:
    void sigImportReady(QString request_id, CanvasDocument document, QString error_message); // 将解析结果交回 GUI 线程。
    void sigExportFinished(QString request_id, bool is_success, QString error_message); // 报告文件导出终态。

private:
    static bool ReadDocument(const QString& file_path, CanvasDocument* document, QString* error_message); // 读取、校验并解析自包含文档。
    static bool WriteDocument(const CanvasDocument& document, const QString& file_path, QString* error_message); // 校验快照并原子提交 JSON。
    void PrepareNextAsset(); // 串行读取或登记资源，完成后交付导入或开始写入。
    void WritePendingDocument(); // 在后台提交已补齐资源的导出快照。
    ImageAssetManager* _asset_manager; // Canvas 拥有的资源代理门面。
    CanvasDocument _pending_document; // 当前文件任务的独立快照。
    QStringList _pending_assets; // 尚未读取或登记的资源摘要。
    QString _pending_file_path; // 当前画布导出目标路径。
    QString _request_id; // 当前文件任务的关联 ID。
    QString _asset_request_id; // 当前资源请求的独立 ID。
    QString _active_asset_sha256; // 当前资源请求对应的摘要。
    bool _is_import = false; // 当前资源准备是否用于导入。
    quint64 _generation = 0; // 取消或换房后废弃旧后台结果。
    quint64 _active_generation = 0; // 当前任务对应的取消代次。
    QThreadPool _file_thread_pool; // 管理后台 JSON 和图片编解码任务。
};
