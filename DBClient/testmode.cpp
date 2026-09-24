#include "testmode.h"
#include "tcpmgr.h"
#include "usermgr.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

const QString LatencyTestController::TEST_ITEM_PREFIX =
    QStringLiteral("__latency_test__:");

LatencyTestController::LatencyTestController(const LatencyTestOptions& options,
                                             QObject* parent)
    : QObject(parent)
    , _options(options)
    , _warmup_timer(new QTimer(this))
    , _send_timer(new QTimer(this))
    , _stop_timer(new QTimer(this))
    , _drain_timer(new QTimer(this))
{
    // 发送定时器需要尽量减少系统定时器的合并，避免固定负载被明显拉长。
    _warmup_timer->setSingleShot(true);
    _send_timer->setTimerType(Qt::PreciseTimer);
    _stop_timer->setSingleShot(true);
    _drain_timer->setSingleShot(true);

    connect(_warmup_timer, &QTimer::timeout,
            this, &LatencyTestController::beginSenderSampling);
    connect(_send_timer, &QTimer::timeout,
            this, &LatencyTestController::sendMovePacket);
    connect(_stop_timer, &QTimer::timeout,
            this, &LatencyTestController::finishSenderSampling);
    connect(_drain_timer, &QTimer::timeout,
            this, &LatencyTestController::finishReceiverSampling);
}

LatencyTestController::~LatencyTestController()
{
    stop();
}

bool LatencyTestController::isEnabled() const
{
    return _options.enabled && _options.role != LatencyTestRole::None;
}

bool LatencyTestController::isRunning() const
{
    return _room_started && !_finished;
}

bool LatencyTestController::isTestPacket(const message::DrawReq& request) const
{
    const QString item_id = QString::fromStdString(request.item_id());
    if (!item_id.startsWith(TEST_ITEM_PREFIX))
        return false;

    if (_options.role != LatencyTestRole::Receiver || _options.run_id.isEmpty())
        return true;

    return item_id == itemIdForRun();
}

void LatencyTestController::startForRoom(const QString& room_id, int uid, bool can_edit)
{
    if (!isEnabled() || _room_started || room_id.isEmpty())
        return;

    if (_options.role == LatencyTestRole::Sender && !can_edit)
        return;

    _room_id = room_id;
    _uid = uid;
    _room_started = true;
    _sampling_started = false;
    _finished = false;
    _next_sequence = 0;
    _active_run_id = _options.run_id;
    if (_active_run_id.isEmpty() && _options.role == LatencyTestRole::Sender)
    {
        _active_run_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        _options.run_id = _active_run_id;
    }

    if (_options.output_path.isEmpty())
    {
        const QString role = roleName(_options.role);
        _options.output_path = QDir::current().filePath(
            QStringLiteral("latency_%1_%2.jsonl").arg(role, _active_run_id));
    }

    openOutputFile();
    if (!_output_file.isOpen())
    {
        _room_started = false;
        return;
    }

    _clock.start();
    writeEvent(QStringLiteral("test_start"), QString(), -1, 0);

    // 测试期间禁止 PaintScene 接收手工绘画，避免人工输入污染固定负载。
    if (_options.role == LatencyTestRole::Sender)
        _warmup_timer->start(_options.warmup_sec * 1000);
}

void LatencyTestController::recordReceived(const message::DrawReq& request,
                                           int queue_depth)
{
    if (!isEnabled() || !_room_started || _options.role != LatencyTestRole::Receiver ||
        !isTestPacket(request) || _finished)
        return;

    if (_active_run_id.isEmpty())
    {
        _active_run_id = runIdFromItemId(QString::fromStdString(request.item_id()));
        if (_active_run_id.isEmpty())
            return;
    }

    if (!_sampling_started && request.cmd() == message::CMD_START)
    {
        _sampling_started = true;
        writeEvent(QStringLiteral("sampling_start"), commandName(request.cmd()), -1,
                   queue_depth);
        _drain_timer->start((_options.duration_sec + 5) * 1000);
    }

    qint64 sequence = -1;
    decodeSequence(request, sequence);
    writeEvent(QStringLiteral("receive"), commandName(request.cmd()), sequence,
               queue_depth);
}

void LatencyTestController::recordApplied(const message::DrawReq& request,
                                          int queue_depth)
{
    if (!isEnabled() || !_room_started || !_sampling_started ||
        _options.role != LatencyTestRole::Receiver ||
        !isTestPacket(request) || _finished)
        return;

    qint64 sequence = -1;
    decodeSequence(request, sequence);
    writeEvent(QStringLiteral("apply"), commandName(request.cmd()), sequence,
               queue_depth);
}

void LatencyTestController::stop()
{
    if (!_room_started)
        return;

    _warmup_timer->stop();
    _send_timer->stop();
    _stop_timer->stop();
    _drain_timer->stop();

    if (!_finished && _options.role == LatencyTestRole::Sender && _sampling_started)
        sendPacket(message::CMD_END,
                   _next_sequence == 0 ? 0 : static_cast<qint64>(_next_sequence - 1));

    if (!_finished)
        writeEvent(QStringLiteral("test_end"), QString(), -1, 0);
    _finished = true;
    if (_output_file.isOpen())
    {
        _output_file.flush();
        _output_file.close();
    }
    _room_started = false;
    _sampling_started = false;
}

void LatencyTestController::beginSenderSampling()
{
    // 预热结束后先发 START，再由精准定时器持续发 MOVE，便于接收端识别采样边界。
    if (_finished || !_room_started || _options.role != LatencyTestRole::Sender)
        return;

    _sampling_started = true;
    _next_sequence = 0;
    sendPacket(message::CMD_START, -1);
    writeEvent(QStringLiteral("sampling_start"), commandName(message::CMD_START), -1, 0);

    const int interval_ms = qMax(1, qRound(1000.0 / _options.rate_hz));
    _send_timer->start(interval_ms);
    _stop_timer->start(_options.duration_sec * 1000);
}

void LatencyTestController::sendMovePacket()
{
    // 每次定时器触发只发送一个序号，避免在一次事件循环中批量制造突发流量。
    if (_finished || !_sampling_started || _options.role != LatencyTestRole::Sender)
        return;

    const qint64 sequence = static_cast<qint64>(_next_sequence++);
    sendPacket(message::CMD_MOVE, sequence);
}

void LatencyTestController::finishSenderSampling()
{
    // 停止固定频率发送并补发 END；最后一个 MOVE 的序号仍由前面的事件记录保留。
    if (_finished)
        return;

    _send_timer->stop();
    sendPacket(message::CMD_END,
               _next_sequence == 0 ? 0 : static_cast<qint64>(_next_sequence - 1));
    writeEvent(QStringLiteral("test_end"), commandName(message::CMD_END), -1, 0);
    _finished = true;
    if (_output_file.isOpen())
    {
        _output_file.flush();
        _output_file.close();
    }
}

void LatencyTestController::finishReceiverSampling()
{
    // 接收端在采样时长结束后额外等待，给远端网络包和本地绘制队列留出排空时间。
    if (_finished)
        return;

    writeEvent(QStringLiteral("test_end"), QString(), -1, 0);
    _finished = true;
    if (_output_file.isOpen())
    {
        _output_file.flush();
        _output_file.close();
    }
}

QString LatencyTestController::roleName(LatencyTestRole role)
{
    switch (role)
    {
        case LatencyTestRole::Sender:
            return QStringLiteral("sender");
        case LatencyTestRole::Receiver:
            return QStringLiteral("receiver");
        default:
            return QStringLiteral("none");
    }
}

QString LatencyTestController::commandName(message::DrawCmd command)
{
    switch (command)
    {
        case message::CMD_START:
            return QStringLiteral("start");
        case message::CMD_MOVE:
            return QStringLiteral("move");
        case message::CMD_END:
            return QStringLiteral("end");
        default:
            return QStringLiteral("unknown");
    }
}

bool LatencyTestController::decodeSequence(const message::DrawReq& request,
                                           qint64& sequence)
{
    // 测试序号只编码在 MOVE 的坐标字段，START/END 不参与样本对齐。
    if (request.cmd() != message::CMD_MOVE)
        return false;

    const qint64 x = qRound64(request.current_x());
    const qint64 y = qRound64(request.current_y());
    sequence = x + y * 1000;
    return sequence >= 0;
}

void LatencyTestController::openOutputFile()
{
    // 结果目录由客户端创建，文件采用截断写，保证一次进程对应一次独立运行。
    const QFileInfo file_info(_options.output_path);
    QDir().mkpath(file_info.absolutePath());
    _output_file.setFileName(_options.output_path);
    _output_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
}

void LatencyTestController::writeEvent(const QString& event,
                                       const QString& command,
                                       qint64 sequence,
                                       int queue_depth)
{
    // JSONL 每行只写一个事件，分析脚本可以在进程异常退出后读取已落盘部分。
    if (!_output_file.isOpen())
        return;

    EventRecord record;
    record.event = event;
    record.command = command;
    record.sequence = sequence;
    record.wall_ms = QDateTime::currentMSecsSinceEpoch();
    record.monotonic_ms = _clock.isValid() ? _clock.elapsed() : 0;
    record.queue_depth = queue_depth;

    QJsonObject object;
    object.insert(QStringLiteral("event"), record.event);
    object.insert(QStringLiteral("role"), roleName(_options.role));
    object.insert(QStringLiteral("run_id"), _active_run_id);
    object.insert(QStringLiteral("uid"), _uid);
    object.insert(QStringLiteral("rate_hz"), _options.rate_hz);
    object.insert(QStringLiteral("duration_sec"), _options.duration_sec);
    object.insert(QStringLiteral("warmup_sec"), _options.warmup_sec);
    object.insert(QStringLiteral("command"), record.command);
    object.insert(QStringLiteral("sequence"), record.sequence);
    object.insert(QStringLiteral("wall_ms"), record.wall_ms);
    object.insert(QStringLiteral("monotonic_ms"), record.monotonic_ms);
    object.insert(QStringLiteral("queue_depth"), record.queue_depth);

    _output_file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    _output_file.write("\n");
}

void LatencyTestController::sendPacket(message::DrawCmd command, qint64 sequence)
{
    // 复用现有 DrawReq 字段承载测试数据，不改变 TCP 包头或 Protobuf 协议。
    if (!_room_started || _uid == 0 || _active_run_id.isEmpty())
        return;

    message::DrawReq request;
    request.set_uid(_uid);
    request.set_item_id(itemIdForRun().toStdString());
    request.set_cmd(command);
    request.set_shape(message::SHAPE_LINE);
    request.set_color(static_cast<int32_t>(qRgba(30, 120, 255, 255)));
    request.set_width(2);
    // 使用远离视口边缘的固定起点，避免测试线条贴在画布顶边而难以观察。
    request.set_start_x(100.0F);
    request.set_start_y(100.0F);

    const qint64 safe_sequence = qMax<qint64>(0, sequence);
    request.set_current_x(static_cast<float>(safe_sequence % 1000));
    request.set_current_y(static_cast<float>(safe_sequence / 1000));
    request.set_send_timestamp_ms(
        static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()));

    std::string binary_data;
    if (!request.SerializeToString(&binary_data))
        return;

    writeEvent(QStringLiteral("send"), commandName(command), sequence, 0);
    TcpMgr::getInstance()->slot_send_data(
        ReqId::ID_DRAW_REQ, QByteArray::fromStdString(binary_data));
}

QString LatencyTestController::itemIdForRun() const
{
    return TEST_ITEM_PREFIX + _active_run_id;
}

QString LatencyTestController::runIdFromItemId(const QString& item_id) const
{
    if (!item_id.startsWith(TEST_ITEM_PREFIX))
        return QString();
    return item_id.mid(TEST_ITEM_PREFIX.size());
}
