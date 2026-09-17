#include "voicemanager.h"

#include "global.h"
#include "usermgr.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

// VoiceManager 只保存客户端拿到的短期 Token，不保存 LiveKit API Secret。
VoiceManager::VoiceManager()
    : _network_manager(new QNetworkAccessManager(this))
{
}

VoiceManager::~VoiceManager()
{
    leaveRoom();
}

void VoiceManager::joinRoom(const QString& room_id)
{
    // 房间加入成功后才请求语音 Token，避免离线画板触发网络请求。
    qInfo() << "[VoiceManager] request room:" << room_id;
    if (room_id.isEmpty())
    {
        emit sig_error(QStringLiteral("语音房间号为空"));
        setState(State::Error);
        return;
    }

    if (_state.load() != State::Disconnected && _room_id == room_id)
        return;

    // 切换房间前先清理旧连接，保证同一客户端只保留一个语音会话。
    leaveRoom();
    _room_id = room_id;
    setState(State::RequestingToken);
    requestVoiceToken(room_id);
}

void VoiceManager::leaveRoom()
{
    // 先清空房间标识，让尚未投递的旧请求和旧回调失效。
    _room_id.clear();

    // 取消尚未完成的 Token 请求，避免离开房间后继续创建新的 Room。
    if (_token_reply)
    {
        _token_reply->abort();
        _token_reply = nullptr;
    }

    // Room::connect 可能仍在工作线程中阻塞，先等待线程结束再销毁 Room。
    if (_connect_thread)
    {
        _connect_thread->wait();
        _connect_thread->deleteLater();
        _connect_thread = nullptr;
    }

    if (_room)
    {
        // 先注销回调，再断开连接，避免断开过程中继续访问 VoiceManager。
        _room->setDelegate(nullptr);
        // disconnect 可以从非 delegate 线程调用；这里不在 SDK 回调内部执行。
        _room->disconnect();
    }

    _audio_track.reset();
    _audio_source.reset();
    _platform_audio.reset();
    _room.reset();
    _room_id.clear();
    _audio_suspended.store(false);

    // 顺手清空设备缓存，避免下次进房持有旧会话的脏数据
    _cached_recording_devices.clear();
    _cached_playout_devices.clear();

    setState(State::Disconnected);
}

void VoiceManager::suspendAudio()
{
    // 大厅和画板共用同一个 VoiceManager，暂离时只暂停音频，不销毁 Room。
    // 这样返回房间无需重新请求 Token、建立 WebRTC 连接和初始化平台音频。
    if (_audio_suspended.exchange(true))
        return;

    _microphone_before_suspend = _microphone_enabled.load();
    _speaker_before_suspend = _speaker_enabled.load();
    setMicrophoneEnabled(false);
    setSpeakerEnabled(false);
}

void VoiceManager::resumeAudio()
{
    // 没有执行过 suspendAudio 时不改变用户原本设置，避免重复进入房间时误打开设备。
    if (!_audio_suspended.exchange(false))
        return;

    setMicrophoneEnabled(_microphone_before_suspend);
    setSpeakerEnabled(_speaker_before_suspend);
}

QVector<VoiceManager::AudioDevice> VoiceManager::recordingDevices() const
{
    QVector<AudioDevice> devices;
    if (!_platform_audio)
        return devices;

    try
    {
        _cached_recording_devices.clear();
        for (const auto& device : _platform_audio->recordingDevices())
        {
            AudioDevice dev{QString::fromStdString(device.name), QString::fromStdString(device.id)};
            devices.append(dev);
            _cached_recording_devices.insert(dev.id,dev);
        }
    }
    catch (const std::exception& exception)
    {
        qWarning() << "[VoiceManager] enumerate recording devices failed:"
                   << QString::fromUtf8(exception.what());
    }
    return devices;
}

QVector<VoiceManager::AudioDevice> VoiceManager::playoutDevices() const
{
    QVector<AudioDevice> devices;
    if (!_platform_audio)
        return devices;

    try
    {
        _cached_playout_devices.clear();
        for (const auto& device : _platform_audio->playoutDevices())
        {
            AudioDevice dev{QString::fromStdString(device.name),QString::fromStdString(device.id)};
            devices.append(dev);
            _cached_playout_devices.insert(dev.id,dev);
        }
    }
    catch (const std::exception& exception)
    {
        qWarning() << "[VoiceManager] enumerate playout devices failed:"
                   << QString::fromUtf8(exception.what());
    }
    return devices;
}

bool VoiceManager::setRecordingDevice(const QString& device_id)
{
    if (!_platform_audio || device_id.isEmpty())
        return false;

    // 1. 如果缓存为空，或者找不到该 ID，先主动刷新一次（覆盖冷启动与新插设备两种情况）
    if (_cached_recording_devices.isEmpty() || !_cached_recording_devices.contains(device_id))
    {
        recordingDevices();
    }

    // 2. 纯内存校验：确保设备物理在线
    if (!_cached_recording_devices.contains(device_id))
    {
        qWarning() << "[VoiceManager] 指定的麦克风不存在或已断开:" << device_id;
        emit sig_error(QStringLiteral("所选麦克风不存在或已断开"));
        return false;
    }

    // 3. 校验通过，安全下发到底层
    try
    {
        _platform_audio->setRecordingDevice(device_id.toStdString());
        emit sig_audio_device_changed(true, device_id);
        return true;
    }
    catch (const std::exception& exception)
    {
        const QString message = QString::fromUtf8(exception.what());
        qWarning() << "[VoiceManager] set recording device failed:" << message;
        emit sig_error(QStringLiteral("切换麦克风失败: ") + message);
        return false;
    }
}

bool VoiceManager::setPlayoutDevice(const QString& device_id)
{
    if (!_platform_audio || device_id.isEmpty())
        return false;


    // 1. 如果缓存为空，或者找不到该 ID，先主动刷新一次（覆盖冷启动与新插设备两种情况）
    if (_cached_playout_devices.isEmpty() || !_cached_playout_devices.contains(device_id))
    {
        playoutDevices();
    }

    // 2. 纯内存校验：确保设备物理在线
    if (!_cached_playout_devices.contains(device_id))
    {
        qWarning() << "[VoiceManager] 指定的扬声器不存在或已断开:" << device_id;
        emit sig_error(QStringLiteral("所选扬声器不存在或已断开"));
        return false;
    }

    // 3. 校验通过，安全下发到底层
    try
    {
        _platform_audio->setPlayoutDevice(device_id.toStdString());
        emit sig_audio_device_changed(false, device_id);
        return true;
    }
    catch (const std::exception& exception)
    {
        const QString message = QString::fromUtf8(exception.what());
        qWarning() << "[VoiceManager] set playout device failed:" << message;
        emit sig_error(QStringLiteral("切换扬声器失败: ") + message);
        return false;
    }
}

void VoiceManager::setMicrophoneEnabled(bool enabled)
{
    _microphone_enabled.store(enabled);
    // 音轨尚未发布时只记录状态，连接成功后 publishMicrophone 会再次应用。
    if (_audio_track)
    {
        if (enabled)
            _audio_track->unmute();
        else
            _audio_track->mute();
    }
    emit sig_microphone_changed(enabled);
}

void VoiceManager::setSpeakerEnabled(bool enabled)
{
    _speaker_enabled.store(enabled);
    applySpeakerState();
    emit sig_speaker_changed(enabled);
}

bool VoiceManager::microphoneEnabled() const
{
    return _microphone_enabled.load();
}

bool VoiceManager::speakerEnabled() const
{
    return _speaker_enabled.load();
}

VoiceManager::State VoiceManager::state() const
{
    return _state.load();
}

void VoiceManager::requestVoiceToken(const QString& room_id)
{
    // GateServer 会先校验 app_token，再向 VerifyServer 请求 LiveKit 短期 Token。
    qInfo() << "[VoiceManager] POST /voice_token room:" << room_id;
    QJsonObject request_body;
    request_body[QStringLiteral("uid")] = UserMgr::getInstance()->getUid();
    request_body[QStringLiteral("room_id")] = room_id;
    request_body[QStringLiteral("app_token")] = UserMgr::getInstance()->getToken();

    QNetworkRequest request(QUrl(gate_url_prefix + QStringLiteral("/voice_token")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply* reply = _network_manager->post(
        request,
        QJsonDocument(request_body).toJson(QJsonDocument::Compact));
    _token_reply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, room_id]() {
        const QByteArray response_data = reply->readAll();
        const QString network_error = reply->errorString();
        const bool request_succeeded = reply->error() == QNetworkReply::NoError;
        if (_token_reply == reply)
            _token_reply = nullptr;
        reply->deleteLater();

        // 离开房间或切换房间后，忽略旧请求的响应，避免误连到旧房间。
        if (_room_id != room_id)
            return;

        if (!request_succeeded)
        {
            qWarning() << "[VoiceManager] token network error:" << network_error;
            emit sig_error(QStringLiteral("获取语音 Token 失败: ") + network_error);
            setState(State::Error);
            return;
        }

        QJsonParseError parse_error;
        const QJsonDocument document = QJsonDocument::fromJson(response_data, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            emit sig_error(QStringLiteral("语音 Token 响应格式错误"));
            setState(State::Error);
            return;
        }

        const QJsonObject response = document.object();
        const int error_code = response.value(QStringLiteral("error")).toInt(-1);
        const QString url = response.value(QStringLiteral("url")).toString();
        const QString token = response.value(QStringLiteral("token")).toString();
        qInfo() << "[VoiceManager] token response error:" << error_code
                << "url empty:" << url.isEmpty()
                << "token empty:" << token.isEmpty();
        if (error_code != 0 || url.isEmpty() || token.isEmpty())
        {
            emit sig_error(QStringLiteral("语音 Token 无效，错误码: ") + QString::number(error_code));
            setState(State::Error);
            return;
        }

        connectLiveKit(url, token);
    });
}

void VoiceManager::connectLiveKit(const QString& url, const QString& token)
{
    // LiveKit 的 connect 是阻塞调用，必须放到独立线程，不能阻塞 Qt UI 线程。
    qInfo() << "[VoiceManager] connecting to LiveKit";
    _room = std::make_unique<livekit::Room>();
    _room->setDelegate(this);       //设置livekit代理对象
    setState(State::Connecting);

    const std::string server_url = url.toStdString();
    const std::string access_token = token.toStdString();
    _connect_thread = QThread::create([this, server_url, access_token]() {
        livekit::RoomOptions options;
        // 自动订阅远端轨道，否则不会收到远端语音。
        options.auto_subscribe = true;

        try
        {
            const bool connected = _room->connect(server_url, access_token, options);   //触发onCnnectionStateChanged回调
            qInfo() << "[VoiceManager] Room::connect returned:" << connected;
            if (!connected)
            {
                QMetaObject::invokeMethod(this, [this]() {
                    emit sig_error(QStringLiteral("LiveKit 房间连接失败"));
                    setState(State::Error);
                }, Qt::QueuedConnection);
            }
        }
        catch (const std::exception& exception)
        {
            const QString message = QString::fromUtf8(exception.what());
            QMetaObject::invokeMethod(this, [this, message]() { //投递错误到UI线程处理
                emit sig_error(QStringLiteral("LiveKit 连接异常: ") + message);
                setState(State::Error);
            }, Qt::QueuedConnection);
        }
    });

    connect(_connect_thread, &QThread::finished, this, [this]() {
        _connect_thread = nullptr;
    });
    connect(_connect_thread, &QThread::finished,
            _connect_thread, &QObject::deleteLater);
    _connect_thread->start();
}

void VoiceManager::publishMicrophone()
{
    if (!_room || !_platform_audio) //livekit房间对象或者声卡硬件层未初始化
        return;

    try
    {
        // PlatformAudio 使用 WebRTC 系统音频设备，并自带回声消除、降噪和自动增益。
        _audio_source = _platform_audio->createAudioSource();           //直接启动采集源，默认绑定操作系统的默认通信设备
        _audio_track = livekit::LocalAudioTrack::createLocalAudioTrack( //将音频源包装为 RTC 本地音轨(在 WebRTC 层创建了 AudioTrackInterface)
            "microphone", _audio_source);

        //轨道元数据与推流通道建立
        livekit::TrackPublishOptions options;
        options.source = livekit::TrackSource::SOURCE_MICROPHONE;   //明确轨道类型为麦克风而非屏幕共享音频（SOURCE_SCREENSHARE_AUDIO）

        const auto participant = _room->localParticipant().lock();  //安全获取本地参与者的强引用
        if (!participant)
            throw std::runtime_error("本地参与者不可用");

        participant->publishTrack(_audio_track, options);           //将本地音轨正式推流到 LiveKit 房间(触发底层 WebRTC 的 PeerConnection::AddTrack)
        setMicrophoneEnabled(_microphone_enabled.load());           //恢复并应用预设的静音开关状态
        qInfo() << "[VoiceManager] microphone published";
    }
    catch (const std::exception& exception)
    {
        qWarning() << "[VoiceManager] microphone init failed:"
                   << QString::fromUtf8(exception.what());
        emit sig_error(QStringLiteral("麦克风初始化失败: ") + QString::fromUtf8(exception.what()));
    }
}

void VoiceManager::handleConnected()
{
    if (!_room || _room_id.isEmpty())
        return;

    try
    {
        // delegate 回调必须尽快返回，音频设备初始化放到 Qt 线程异步执行。
        _platform_audio = std::make_unique<livekit::PlatformAudio>();
        publishMicrophone();    //发布麦克风音轨
        applySpeakerState();    //订阅远端音频
        setState(State::Connected);
        qInfo() << "[VoiceManager] voice connected, audio initialized";
    }
    catch (const std::exception& exception)
    {
        qWarning() << "[VoiceManager] platform audio init failed:"
                   << QString::fromUtf8(exception.what());
        emit sig_error(QStringLiteral("平台音频初始化失败: ") + QString::fromUtf8(exception.what()));
        setState(State::Error);
    }
}

void VoiceManager::applySpeakerState()
{
    if (!_room)
        return;

    // SDK 没有全局扬声器静音接口，这里通过订阅/取消订阅远端音频轨实现听筒开关。
    for (const auto& weak_participant : _room->remoteParticipants())
    {
        const auto participant = weak_participant.lock();
        if (!participant)
            continue;

        //内层遍历：成员发布的媒体轨道,每个参会人可能同时发布多个媒体流（如：麦克风音频、摄像头视频、屏幕共享流、屏幕声音）
        for (const auto& item : participant->trackPublications())
        {
            const auto& publication = item.second;
            if (publication && publication->kind() == livekit::TrackKind::KIND_AUDIO)   //只争对音频轨道进行开关
                publication->setSubscribed(_speaker_enabled.load());
        }
    }
}

void VoiceManager::setState(State state)
{
    if (_state.exchange(state) == state)
        return;
    emit sig_state_changed(state);
}

void VoiceManager::onConnectionStateChanged(
    livekit::Room&, const livekit::ConnectionStateChangedEvent& event)
{
    // 连接回调来自 LiveKit 线程，不能在这里初始化音频设备或发布轨道，交给UI线程初始化，防止阻塞过长导致连接超时。
    if (event.state == livekit::ConnectionState::Connected)
    {
        QMetaObject::invokeMethod(this, &VoiceManager::handleConnected,
                                  Qt::QueuedConnection);
    }
    else if (event.state == livekit::ConnectionState::Reconnecting)
    {
        setState(State::Reconnecting);
    }
    else
    {
        setState(State::Disconnected);
    }
}

void VoiceManager::onDisconnected(livekit::Room&, const livekit::DisconnectedEvent&)
{
    setState(State::Disconnected);
}

void VoiceManager::onReconnecting(livekit::Room&, const livekit::ReconnectingEvent&)
{
    setState(State::Reconnecting);
}

void VoiceManager::onReconnected(livekit::Room&, const livekit::ReconnectedEvent&)
{
    setState(State::Connected);
}

void VoiceManager::onTrackPublished(
    livekit::Room&, const livekit::TrackPublishedEvent& event)
{
    // 听筒关闭期间，新发布的远端音频轨也要立即取消订阅。(!_speaker_enabled.load())
    if (!_speaker_enabled.load() && event.publication &&
        event.publication->kind() == livekit::TrackKind::KIND_AUDIO)
    {
        event.publication->setSubscribed(false);
    }
}

void VoiceManager::onTrackSubscribed(
    livekit::Room&, const livekit::TrackSubscribedEvent& event)
{
    if (!_speaker_enabled.load() && event.publication &&
        event.publication->kind() == livekit::TrackKind::KIND_AUDIO)
    {
        event.publication->setSubscribed(false);
    }
}

void VoiceManager::onActiveSpeakersChanged(
    livekit::Room&, const livekit::ActiveSpeakersChangedEvent& event)
{
    // Participant 指针由 Room 管理，只在回调内转换为身份字符串，不跨线程保存。
    QStringList identities;
    for (livekit::Participant* participant : event.speakers)
    {
        if (participant)
            identities.append(QString::fromStdString(participant->identity()));
    }

    emit sig_active_speakers_changed(identities);
}
