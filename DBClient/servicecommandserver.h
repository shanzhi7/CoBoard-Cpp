/***********************************************************************************
* @file         servicecommandserver.h
* @brief        图片缓存代理的独占启动、IPC 指令和客户端生命周期管理
* @author       shanzhi
* @date         2026/10/04
* @history
***********************************************************************************/
#pragma once

#include "assetserviceprotocol.h"

#include <memory>
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTimer>

class QLocalServer;
class QLockFile;
class AssetTransferManager;
enum class ServiceStartResult // 区分已有掌控者和启动失败。
{
    Started, // 已持锁并开始监听。
    AlreadyRunning, // 其他实例已持锁或端点已有监听者。
    Error // 初始化前启动失败。
};
class ServiceCommandServer : public QObject
{
    Q_OBJECT
public:
    explicit ServiceCommandServer(QObject* parent = nullptr); // 服务模式创建。
    ~ServiceCommandServer() override; // 保证锁在 SQL 和传输之后释放。
    ServiceStartResult Start(); // 取得独占锁后监听和初始化。
    void Stop(); // 停止监听、传输、数据库及锁。
private:
    struct ClientSession
    {
        QString _client_id; // 服务分配连接身份。
        AssetIpcChannel* _channel = nullptr; // 独立连接缓冲。
        QSet<QString> _requests; // 活动请求 ID。
        bool _has_hello = false; // 握手完成前禁止业务指令。
    };
    struct PendingRequest
    {
        QString _client_id; // 响应所属客户端。
        QString _request_id; // 回显 UUID。
        QString _command; // 控制响应是否携带图片或句柄。
        qsizetype _input_bytes = 0; // 全局输入预算。
        QElapsedTimer _elapsed; // 服务超时及日志耗时。
    };
    struct AssetHandle
    {
        QString _client_id; // 句柄不可跨连接使用。
        QString _request_id; // 导入响应与取消命令竞争时仍可回收句柄。
        AssetCacheResult _asset; // 不含二进制的保护资源。
    };
    void AcceptClients(); // 为每条连接创建独立上下文。
    void DispatchCommand(const QString& client_id, const QJsonObject& request, const QByteArray& binary); // 校验及调度指令。
    bool SendResponse(const QString& client_id, const QJsonObject& response, const QByteArray& binary = QByteArray()); // 超预算返回 Busy，响应仅发所属连接。
    void CompleteRequest(const QString& job_id, AssetCacheResult result); // 关联后台结果与请求。
    void ReleaseClientResources(const QString& client_id); // 取消任务并释放句柄。
    void UpdateIdleState(); // 活动任务结束后重新计时。
    void Maintain(); // 超时和五秒清理维护。
    QLocalServer* _server = nullptr; // 服务线程监听对象。
    std::unique_ptr<QLockFile> _ownership_lock; // 整个服务生命周期持锁。
    AssetTransferManager* _transfer_manager = nullptr; // 完整图片代理。
    QHash<QString, ClientSession> _client_sessions; // 每连接独立上下文。
    QHash<QString, PendingRequest> _pending_requests; // client_id/request_id 组合键。
    QHash<QString, AssetHandle> _asset_handles; // 不透明引用句柄。
    QTimer _idle_timer; // 最后客户端离开后的三十秒宽限。
    QTimer _maintenance_timer; // 请求超时和缓存维护。
    QString _service_state = "Initializing"; // Initializing、Ready 或 Stopping。
    QString _service_instance_id; // 重启后旧句柄失效。
    qsizetype _pending_input_bytes = 0; // 所有任务二进制预算。
    int _maintenance_ticks = 0; // 每五秒触发数据库清理。
    bool _is_started = false; // 停止流程幂等状态。
};
