/***********************************************************************************
* @file         servicecommandclient.h
* @brief        UI 进程共享的图片代理连接、自启动和异步请求关联
* @author       shanzhi
* @date         2026/10/04
* @history
***********************************************************************************/
#pragma once

#include "assetserviceprotocol.h"

#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QTimer>

#include "singleton.h"

class ServiceCommandClient : public QObject, public Singleton<ServiceCommandClient>
{
    Q_OBJECT
    friend class Singleton<ServiceCommandClient>;
public:
    ~ServiceCommandClient() override; // UI 停止前断开连接。
    void EnsureService(); // 异步探测并按需启动代理。
    QString SendCommand(const QString& command, const QJsonObject& payload = QJsonObject(), const QByteArray& binary = QByteArray()); // 返回 UUID，结果总是异步到达。
    void CancelRequest(const QString& request_id); // 取消排队或服务中的请求。
    void Disconnect(); // 拒绝重连并结束本进程请求。
signals:
    void sigResponseReady(QString request_id, QJsonObject response, QByteArray binary); // 回显请求关联。
    void sigServiceReady(QString instance_id); // 握手成功且初始化完成。
    void sigServiceDisconnected(); // 已有连接失效，旧句柄不可再使用。
private:
    struct PendingRequest
    {
        QJsonObject _message; // 不写入日志的控制请求。
        QByteArray _binary; // 有界编码图片。
        QElapsedTimer _elapsed; // 两分钟请求期限。
        bool _is_sent = false; // 连接失败不自动重放。
    };
    ServiceCommandClient(); // 由仓库单例模板创建。
    void ConnectAttempt(); // 创建全新套接字，避免旧缓冲重放。
    void SendHello(); // 校验服务和缓存身份。
    void ReceiveResponse(const QJsonObject& response, const QByteArray& binary); // 关联握手或业务结果。
    void ConnectionFailed(const QString& reason, bool can_retry = true); // 明确失败并按需重连。
    void PumpRequests(); // 就绪后发送尚未提交的指令。
    void Maintain(); // 探测、初始化和请求期限。
    void LaunchService(); // 启动同一程序后台模式。
    void FailPending(const QString& code, const QString& reason); // 失败所有未完成请求。
    QPointer<AssetIpcChannel> _channel; // 每次连接重新创建。
    QLocalSocket* _socket = nullptr; // 当前连接套接字。
    QHash<QString, PendingRequest> _pending_requests; // 请求 UUID 映射。
    QString _connection_state = "Disconnected"; // 当前启动及握手阶段。
    QString _service_instance_id; // 代理重启识别。
    QString _hello_request_id; // 握手独立关联。
    QTimer _retry_timer; // 探测或初始化重试。
    QTimer _timeout_timer; // 非阻塞期限检查。
    QElapsedTimer _connection_elapsed; // 十秒启动上限。
    QElapsedTimer _probe_elapsed; // 五百毫秒单次探测。
    QElapsedTimer _initialization_elapsed; // 六十秒迁移上限。
    bool _wants_service = false; // UI 主动关闭后禁止重连。
    bool _has_launched = false; // 每个启动周期仅拉起一次候选代理。
};
