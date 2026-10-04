/***********************************************************************************
* @file         assettransfermanager.h
* @brief        后台图片导入、共享下载和受保护的 OSS 上传代理
* @author       shanzhi
* @date         2026/10/04
* @history
***********************************************************************************/
#pragma once

#include "assetserviceprotocol.h"

#include <functional>
#include <QFuture>
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QThreadPool>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class AssetTransferManager : public QObject
{
    Q_OBJECT
public:
    explicit AssetTransferManager(QObject* parent = nullptr); // 创建服务线程的网络管理器。
    void LoadAsset(const QString& job_id, const QString& sha256); // 异步读取缓存。
    void PrepareLocalAsset(const QString& job_id, const QString& file_path); // 后台导入原始文件。
    void PrepareImageData(const QString& job_id, const QByteArray& data); // 后台校验并登记图片。
    void DownloadAsset(const QString& job_id, const QString& sha256, const QUrl& url); // 摘要合并 GET 请求。
    void UploadAsset(const QString& job_id, const AssetCacheResult& asset, const QUrl& url); // 持有保护引用执行 PUT。
    void GetCacheStats(const QString& job_id); // 异步读取容量统计。
    void CancelRequest(const QString& job_id); // 移除等待者并中止独占任务。
    bool HasActiveTasks() const; // 包括已取消但尚未退出的后台任务。
    void Shutdown(); // 中止传输并回收导入线程。
signals:
    void sigFinished(QString job_id, AssetCacheResult result); // 仅返回所属请求结果。
    void sigActivityChanged(); // 服务重新判断空闲退出。
private:
    struct DownloadContext
    {
        QString _sha256; // 合并键。
        QUrl _url; // 只在内存保留的签名地址。
        QSet<QString> _waiters; // 尚未取消的任务。
        QByteArray _data; // 有界下载缓冲。
        QNetworkReply* _reply = nullptr; // 服务线程回复。
        bool _is_over_limit = false; // 超长响应立即中止。
    };
    void Observe(const QString& job_id, QFuture<AssetCacheResult> future); // 绑定后台任务完成回调。
    void Finish(const QString& job_id, AssetCacheResult result); // 忽略已取消请求。
    void FinishDownload(const QSharedPointer<DownloadContext>& context, AssetCacheResult result); // 共享结果分发。
    void StartDownload(const QSharedPointer<DownloadContext>& context); // 占用网络并行槽。
    void DrainNetworkQueue(); // 最大四个并行 GET 或 PUT。
    QNetworkAccessManager* _network_manager = nullptr; // 只在服务事件线程使用。
    QThreadPool _prepare_thread_pool; // 有界导入任务池。
    QSet<QString> _jobs; // 有效请求集合。
    QHash<QString, QSharedPointer<DownloadContext>> _download_contexts; // 按摘要合并下载。
    QHash<QString, QNetworkReply*> _upload_contexts; // 独占上传回复。
    QQueue<std::function<void()>> _network_queue; // 等待网络槽的启动动作。
    int _active_network_count = 0; // 四个传输槽共享计数。
    int _active_worker_count = 0; // Future 未完成数量。
    bool _is_stopping = false; // 停止后不启动新传输。
};
