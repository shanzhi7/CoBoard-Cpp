#pragma once

#include "global.h"
#include "message.pb.h"
#include <QObject>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>

enum class LatencyTestRole
{
    None,
    Sender,
    Receiver
};

struct LatencyTestOptions
{
    bool enabled = false; // 是否启用延迟测试模式
    LatencyTestRole role = LatencyTestRole::None; // 当前客户端测试角色
    int rate_hz = 60; // 发送端每秒发送的 MOVE 数量
    int duration_sec = 120; // 正式采样持续时间
    int warmup_sec = 30; // 正式采样前的预热时间
    QString output_path; // JSONL 测试结果路径
    QString run_id; // 测试运行 ID，接收端为空时自动识别
};

class LatencyTestController : public QObject
{
    Q_OBJECT

public:
    explicit LatencyTestController(const LatencyTestOptions& options,
                                   QObject* parent = nullptr); // 创建延迟测试控制器
    ~LatencyTestController() override; // 停止测试并关闭结果文件

    bool isEnabled() const; // 返回是否启用了测试模式
    bool isRunning() const; // 返回当前是否已成功绑定并运行测试
    bool isTestPacket(const message::DrawReq& request) const; // 判断消息是否属于当前测试
    void startForRoom(const QString& room_id, int uid, bool can_edit); // 房间就绪后启动测试
    void recordReceived(const message::DrawReq& request, int queue_depth); // 记录 TCP 收包阶段
    void recordApplied(const message::DrawReq& request, int queue_depth); // 记录实际绘制阶段
    void stop(); // 停止定时器并写入测试结束事件

private slots:
    void beginSenderSampling(); // 预热结束后开始发送采样包
    void sendMovePacket(); // 按固定频率发送一个 MOVE 包
    void finishSenderSampling(); // 发送端采样结束
    void finishReceiverSampling(); // 接收端排空队列后结束

private:
    struct EventRecord
    {
        QString event; // 事件名称
        QString command; // DrawReq 命令名称
        qint64 sequence = -1; // 测试序号，非数据包事件为 -1
        qint64 wall_ms = 0; // 墙上时间戳
        qint64 monotonic_ms = 0; // 进程内单调时间
        int queue_depth = 0; // 事件发生时的接收/绘制队列长度
    };

    static const QString TEST_ITEM_PREFIX; // 测试图元 ID 的保留前缀
    static QString roleName(LatencyTestRole role); // 返回角色名称
    static QString commandName(message::DrawCmd command); // 返回绘画命令名称
    static bool decodeSequence(const message::DrawReq& request, qint64& sequence); // 从坐标解码序号

    void openOutputFile(); // 创建测试结果文件
    void writeEvent(const QString& event, const QString& command,
                    qint64 sequence, int queue_depth); // 写入一条 JSONL 事件
    void sendPacket(message::DrawCmd command, qint64 sequence); // 组包并发送测试消息
    QString itemIdForRun() const; // 生成当前测试图元 ID
    QString runIdFromItemId(const QString& item_id) const; // 从图元 ID 中提取运行 ID

    LatencyTestOptions _options; // 测试启动参数
    QFile _output_file; // JSONL 输出文件
    QElapsedTimer _clock; // 进程内单调计时器
    QTimer* _warmup_timer = nullptr; // 预热定时器
    QTimer* _send_timer = nullptr; // 固定频率发送定时器
    QTimer* _stop_timer = nullptr; // 发送端采样结束定时器
    QTimer* _drain_timer = nullptr; // 接收端排空结束定时器
    QString _room_id; // 当前测试房间 ID
    QString _active_run_id; // 实际接收或发送的运行 ID
    int _uid = 0; // 当前用户 UID
    quint64 _next_sequence = 0; // 下一个 MOVE 序号
    bool _room_started = false; // 是否已经绑定房间
    bool _sampling_started = false; // 是否已经开始正式采样
    bool _finished = false; // 是否已经写入结束事件
};
