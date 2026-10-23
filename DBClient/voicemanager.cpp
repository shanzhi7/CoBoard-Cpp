#include "voicemanager.h"

#include "global.h"
#include "usermgr.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>

// VoiceManager 只保存客户端拿到的短期 Token，不保存 LiveKit API Secret。
VoiceManager::VoiceManager()
    : _network_manager(new QNetworkAccessManager(this))
{
    // 1. Token 请求与状态交付由 Qt 线程的事件循环驱动。
}

VoiceManager::~VoiceManager()
{
    // 1. 应用关闭必须等待全部后台线程，防止 SDK 释放后继续访问资源。
    leaveRoom();
}

void VoiceManager::joinRoom(const QString& room_id)
{
    // 1. 房间确认后才请求语音；同一会话不重复初始化设备。
    if (room_id.isEmpty())
    {
        emit sig_error(QStringLiteral("语音房间号为空"));
        setState(State::Error);
        return;
    }
    if (_state.load() != State::Disconnected && _room_id == room_id) return;

    // 2. 旧连接及平台音频先异步释放，防止同一设备被两个会话同时占用。
    LeaveRoomAsync();
    if (!_cleanup_threads.isEmpty())
    {
        _pending_join_room_id = room_id;
        return;
    }
    _room_id = room_id;
    setState(State::RequestingToken);
    requestVoiceToken(room_id);
}

void VoiceManager::leaveRoom()
{
    // 1. 仅在应用关闭时同步回收线程，正常退房使用 LeaveRoomAsync。
    LeaveRoomAsync();
    const auto cleanup_threads = _cleanup_threads;
    for (QThread* cleanup_thread : cleanup_threads)
    {
        disconnect(cleanup_thread, nullptr, this, nullptr);
        cleanup_thread->wait();
        delete cleanup_thread;
    }
    _cleanup_threads.clear();
}

void VoiceManager::LeaveRoomAsync()
{
    // 1. 先废弃所有旧回调和待加入目标，取消 Token 请求不触发新的连接。
    ++_voice_generation;
    _room_id.clear();
    _pending_join_room_id.clear();
    if (_token_reply)
    {
        QNetworkReply* reply = _token_reply;
        _token_reply = nullptr;
        reply->abort();
    }

    // 2. 立即停止麦克风并移交资源，连接线程仍只访问自己捕获的 Room。
    if (_audio_track) _audio_track->mute();
    if (_room) _room->setDelegate(nullptr);
    QThread* connect_thread = _connect_thread;
    _connect_thread = nullptr;
    if (connect_thread) disconnect(connect_thread, nullptr, nullptr, nullptr);
    _audio_suspended.store(false);
    _cached_recording_devices.clear();
    _cached_playout_devices.clear();
    setState(State::Disconnected);
    if (!_room && !_platform_audio && !connect_thread)
    {
        if (_cleanup_threads.isEmpty()) emit SigRoomLeft();
        return;
    }

    // 3. 耗时 wait、disconnect 和设备析构在后台完成；线程终止后由 Qt 线程回收。
    QThread* cleanup_thread = QThread::create([
        connect_thread, room = std::move(_room), audio_track = std::move(_audio_track),
        audio_source = std::move(_audio_source), platform_audio = std::move(_platform_audio)]() mutable {
        if (connect_thread)
        {
            connect_thread->wait();
            delete connect_thread;
        }
        try
        {
            if (room) room->disconnect();
        } catch (const std::exception& exception)
        {
            qWarning() << "[VoiceManager] LeaveRoomAsync disconnect failed:" << exception.what();
        }
        audio_track.reset();
        audio_source.reset();
        platform_audio.reset();
        room.reset();
    });
    _cleanup_threads.append(cleanup_thread);
    connect(cleanup_thread, &QThread::finished, this, [this, cleanup_thread]() {
        _cleanup_threads.removeOne(cleanup_thread);
        cleanup_thread->deleteLater();
        if (!_cleanup_threads.isEmpty()) return;
        const QString pending_room = _pending_join_room_id;
        _pending_join_room_id.clear();
        emit SigRoomLeft();
        if (!pending_room.isEmpty()) joinRoom(pending_room);
    });
    cleanup_thread->start();
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
    // 1. 只在连接及音频初始化成功后枚举，避免访问连接中或失败会话的 ADM。
    QVector<AudioDevice> devices;
    if (_state.load() != State::Connected || !_platform_audio)
        return devices;

    // 2. 刷新稳定 ID 缓存，枚举失败时返回空列表。
    try
    {
        _cached_recording_devices.clear();
        for (const auto& device : _platform_audio->recordingDevices())
        {
            AudioDevice dev{QString::fromStdString(device.name), QString::fromStdString(device.id)};
            devices.append(dev);
            _cached_recording_devices.insert(dev.id,dev);
        }
    } catch (const std::exception& exception)
    {
        qWarning() << "[VoiceManager] enumerate recording devices failed:"
                   << QString::fromUtf8(exception.what());
    }
    return devices;
}

QVector<VoiceManager::AudioDevice> VoiceManager::playoutDevices() const
{
    // 1. 连接失败或正在重建时不进入 SDK，旧会话设备不跨退出保留。
    QVector<AudioDevice> devices;
    if (_state.load() != State::Connected || !_platform_audio)
        return devices;

    // 2. 刷新输出设备及稳定 ID 缓存，枚举失败时返回空列表。
    try
    {
        _cached_playout_devices.clear();
        for (const auto& device : _platform_audio->playoutDevices())
        {
            AudioDevice dev{QString::fromStdString(device.name),QString::fromStdString(device.id)};
            devices.append(dev);
            _cached_playout_devices.insert(dev.id,dev);
        }
    } catch (const std::exception& exception)
    {
        qWarning() << "[VoiceManager] enumerate playout devices failed:"
                   << QString::fromUtf8(exception.what());
    }
    return devices;
}

bool VoiceManager::setRecordingDevice(const QString& device_id)
{
    // 1. 菜单中已经排队的点击也必须检查状态，不能重连失败或正在清理的会话。
    if (_state.load() != State::Connected || !_platform_audio || device_id.isEmpty())
        return false;

    // 2. 缓存为空或未命中时重新枚举，覆盖设备插拔。
    if (_cached_recording_devices.isEmpty() || !_cached_recording_devices.contains(device_id))
    {
        recordingDevices();
    }

    // 3. 校验设备存在，不向 SDK 提交已经失效的设备标识。
    if (!_cached_recording_devices.contains(device_id))
    {
        qWarning() << "[VoiceManager] 指定的麦克风不存在或已断开:" << device_id;
        emit sig_error(QStringLiteral("所选麦克风不存在或已断开"));
        return false;
    }

    // 4. 保存选择并异步重建语音连接，旧资源释放完成后再初始化设备。
    try
    {
        // 1.10.x 的 C++ SDK 只提供初始化前的 setRecordingDevice，
        // 音频已经启动后直接调用会返回 Device not found。保存选择并完整重连，
        // 让设备在创建音频源和发布麦克风轨道之前生效，避免重复发布音轨。
        _preferred_recording_device_id = device_id;
        const QString room_id = _room_id;
        if (!_room || room_id.isEmpty())
            return false;

        LeaveRoomAsync();
        joinRoom(room_id);
        return true;
    } catch (const std::exception& exception)
    {
        const QString message = QString::fromUtf8(exception.what());
        qWarning() << "[VoiceManager] set recording device failed:" << message;
        emit sig_error(QStringLiteral("切换麦克风失败: ") + message);
        return false;
    }
}

bool VoiceManager::setPlayoutDevice(const QString& device_id)
{
    // 1. 仅允许操作已经连接的音频，拒绝失败或正在清理的会话。
    if (_state.load() != State::Connected || !_platform_audio || device_id.isEmpty())
        return false;

    // 2. 缓存为空或未命中时重新枚举，覆盖设备插拔。
    if (_cached_playout_devices.isEmpty() || !_cached_playout_devices.contains(device_id))
    {
        playoutDevices();
    }

    // 3. 校验设备存在，不向 SDK 提交已经失效的设备标识。
    if (!_cached_playout_devices.contains(device_id))
    {
        qWarning() << "[VoiceManager] 指定的扬声器不存在或已断开:" << device_id;
        emit sig_error(QStringLiteral("所选扬声器不存在或已断开"));
        return false;
    }

    // 4. 保存选择并异步重建语音连接，旧资源释放完成后再初始化设备。
    try
    {
        // 与麦克风相同，输出设备必须在 ADM 开始播放前设置。
        _preferred_playout_device_id = device_id;
        const QString room_id = _room_id;
        if (!_room || room_id.isEmpty())
            return false;

        LeaveRoomAsync();
        joinRoom(room_id);
        return true;
    } catch (const std::exception& exception)
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
    // 1. GateServer 会先校验 app_token，再向 VerifyServer 请求 LiveKit 短期 Token。
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

    // 2. 同一房间重新加入也必须拒绝旧代次的请求结果。
    const quint64 generation = _voice_generation.load();
    connect(reply, &QNetworkReply::finished, this, [this, reply, room_id, generation]() {
        const QByteArray response_data = reply->readAll();
        const QString network_error = reply->errorString();
        const bool request_succeeded = reply->error() == QNetworkReply::NoError;
        if (_token_reply == reply)
            _token_reply = nullptr;
        reply->deleteLater();

        // 离开房间或切换房间后，忽略旧请求的响应，避免误连到旧房间。
        if (_room_id != room_id || _voice_generation.load() != generation)
            return;

        if (!request_succeeded)
        {
            qWarning() << "[VoiceManager] token network error:" << network_error;
            HandleConnectionFailure(QStringLiteral("获取语音 Token 失败: ") + network_error);
            return;
        }

        QJsonParseError parse_error;
        const QJsonDocument document = QJsonDocument::fromJson(response_data, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            HandleConnectionFailure(QStringLiteral("语音 Token 响应格式错误"));
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
            HandleConnectionFailure(QStringLiteral("语音 Token 无效，错误码: ") + QString::number(error_code));
            return;
        }

        connectLiveKit(url, token);
    });
}

void VoiceManager::connectLiveKit(const QString& url, const QString& token)
{
    // 1. LiveKit 的 connect 是阻塞调用，必须放到独立线程，不能阻塞 Qt UI 线程。
    qInfo() << "[VoiceManager] connecting to LiveKit";
    try
    {
        // 必须在 Room::connect 前创建 ADM 并设置设备；连接后切换会被
        // WebRTC ADM 拒绝为 Device not found。
        _platform_audio = std::make_unique<livekit::PlatformAudio>();
        applyPreferredAudioDevices();
        _room = std::make_unique<livekit::Room>();
        _room->setDelegate(this);
    } catch (const std::exception& exception)
    {
        qWarning() << "[VoiceManager] voice init failed:"
                   << QString::fromUtf8(exception.what());
        HandleConnectionFailure(QStringLiteral("语音初始化失败: ") + QString::fromUtf8(exception.what()));
        return;
    }

    // 2. 捕获本轮 Room 和代次，后台连接不访问可能已被移交的成员。
    setState(State::Connecting);

    const std::string server_url = url.toStdString();
    const std::string access_token = token.toStdString();
    livekit::Room* connecting_room = _room.get();
    const quint64 generation = _voice_generation.load();
    _connect_thread = QThread::create([this, server_url, access_token, connecting_room, generation]() {
        // 1. 独立线程只处理当前捕获的 Room，阻塞连接不会占用 UI 事件循环。
        livekit::RoomOptions options;
        // 自动订阅远端轨道，否则不会收到远端语音。
        options.auto_subscribe = true;

        bool connected = false;
        QString error_message = QStringLiteral("语音连接失败，请检查网络或联系管理员确认服务额度");
        try
        {
            connected = connecting_room->connect(server_url, access_token, options);
            qInfo() << "[VoiceManager] Room::connect returned:" << connected;
        } catch (const std::exception& exception)
        {
            error_message = QStringLiteral("语音连接异常: ") + QString::fromUtf8(exception.what());
        }

        // 2. connect 完成后才能使用参与者和音频，结果与失败清理交回 Qt 线程。
        QMetaObject::invokeMethod(this, [this, connecting_room, generation, connected, error_message]() {
            // 1. 旧连接的成功或失败结果均不能操作新会话。
            if (_voice_generation.load() != generation || _room.get() != connecting_room) return;
            if (!connected)
            {
                HandleConnectionFailure(error_message);
                return;
            }
            handleConnected();
        }, Qt::QueuedConnection);
    });

    // 3. 已完成的连接线程仅由当前拥有者回收，清理线程接手后不重复删除。
    QThread* connect_thread = _connect_thread;
    connect(connect_thread, &QThread::finished, this, [this, connect_thread]() {
        if (_connect_thread != connect_thread) return;
        _connect_thread = nullptr;
        connect_thread->deleteLater();
    });
    _connect_thread->start();
}

void VoiceManager::HandleConnectionFailure(const QString& message)
{
    // 1. 先作废代次并移交资源，清理线程等待 connect 返回后再断开和销毁 SDK 对象。
    Q_ASSERT(QThread::currentThread() == thread());
    LeaveRoomAsync();

    // 2. 保持失败状态且不自动重试，避免额度耗尽时重复连接和访问旧设备。
    setState(State::Error);
    emit sig_error(message);
}

void VoiceManager::applyPreferredAudioDevices()
{
    if (!_platform_audio)
        return;

    if (!_preferred_recording_device_id.isEmpty())
    {
        try
        {
            const auto devices = _platform_audio->recordingDevices();
            const auto found = std::find_if(
                devices.cbegin(), devices.cend(), [this](const auto& device) {
                    return QString::fromStdString(device.id) == _preferred_recording_device_id;
                });
            if (found != devices.cend())
            {
                _platform_audio->setRecordingDevice(_preferred_recording_device_id.toStdString());
                emit sig_audio_device_changed(true, _preferred_recording_device_id);
            }
        } catch (const std::exception& exception)
        {
            qWarning() << "[VoiceManager] apply recording device failed:"
                       << QString::fromUtf8(exception.what());
        }
    }

    if (!_preferred_playout_device_id.isEmpty())
    {
        try
        {
            const auto devices = _platform_audio->playoutDevices();
            const auto found = std::find_if(
                devices.cbegin(), devices.cend(), [this](const auto& device) {
                    return QString::fromStdString(device.id) == _preferred_playout_device_id;
                });
            if (found != devices.cend())
            {
                _platform_audio->setPlayoutDevice(_preferred_playout_device_id.toStdString());
                emit sig_audio_device_changed(false, _preferred_playout_device_id);
            }
        } catch (const std::exception& exception)
        {
            qWarning() << "[VoiceManager] apply playout device failed:"
                       << QString::fromUtf8(exception.what());
        }
    }
}

void VoiceManager::publishMicrophone()
{
    // 1. 当前 Room 只发布一条麦克风轨道，自动重连复用现有音轨。
    if (!_room || !_platform_audio) //livekit房间对象或者声卡硬件层未初始化
        return;

    // LiveKit 在首次连接和自动重连恢复时都可能回调 Connected。
    // 同一个 Room 只能保留一条麦克风轨道，否则远端会听到重复声音。
    if (_audio_track)
    {
        setMicrophoneEnabled(_microphone_enabled.load());
        return;
    }

    try
    {
        // 2. 当前连接已就绪，再启动采集并发布唯一的本地音轨。
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
    } catch (const std::exception& exception)
    {
        // 3. 发布失败先释放半成品，再统一回收房间，避免继续暴露失效设备。
        _audio_track.reset();
        _audio_source.reset();
        qWarning() << "[VoiceManager] microphone init failed:"
                   << QString::fromUtf8(exception.what());
        HandleConnectionFailure(QStringLiteral("麦克风初始化失败: ") + QString::fromUtf8(exception.what()));
    }
}

void VoiceManager::handleConnected()
{
    // 1. 只为当前房间初始化或恢复音频，重复 Connected 不重复发布。
    if (!_room || _room_id.isEmpty())
        return;

    try
    {
        const bool audio_initialized = _platform_audio && _audio_track;
        // delegate 回调必须尽快返回，音频设备初始化放到 Qt 线程异步执行。
        if (!_platform_audio)
            _platform_audio = std::make_unique<livekit::PlatformAudio>();

        if (audio_initialized)
        {
            // 重连成功会再次触发 Connected，但原有音轨会由 SDK 自动恢复发布。
            // 这里只重新应用听筒状态，不能再次创建或发布本地音轨。
            applySpeakerState();
            setState(State::Connected);
            qInfo() << "[VoiceManager] duplicate Connected ignored; audio already initialized";
            return;
        }

        // 2. 初始化失败可能已经移交资源，不能再将失败会话标记为 Connected。
        publishMicrophone();
        if (!_audio_track) return;
        applySpeakerState();    //订阅远端音频
        setState(State::Connected);
        qInfo() << "[VoiceManager] voice connected, audio initialized";
    } catch (const std::exception& exception)
    {
        qWarning() << "[VoiceManager] platform audio init failed:"
                   << QString::fromUtf8(exception.what());
        HandleConnectionFailure(QStringLiteral("平台音频初始化失败: ") + QString::fromUtf8(exception.what()));
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
    livekit::Room& room, const livekit::ConnectionStateChangedEvent& event)
{
    // 1. SDK 回调只排队，状态和音频都在 Qt 线程验证当前 Room 及代次。
    livekit::Room* source_room = &room;
    const quint64 generation = _voice_generation.load();
    const auto connection_state = event.state;
    QMetaObject::invokeMethod(this, [this, source_room, generation, connection_state]() {
        if (_voice_generation.load() != generation || _room.get() != source_room) return;
        // 首次连接以 connect 的返回结果为准，不能在参与者尚未就绪时初始化音频。
        if (_state.load() == State::Connecting) return;
        if (connection_state == livekit::ConnectionState::Connected) handleConnected();
        else if (connection_state == livekit::ConnectionState::Reconnecting) setState(State::Reconnecting);
        else setState(State::Disconnected);
    }, Qt::QueuedConnection);
}

void VoiceManager::onDisconnected(livekit::Room& room, const livekit::DisconnectedEvent&)
{
    // 1. 已退出房间的回调不能覆盖新房间，首次连接失败由 connect 的返回结果处理。
    livekit::Room* source_room = &room;
    const quint64 generation = _voice_generation.load();
    QMetaObject::invokeMethod(this, [this, source_room, generation]() {
        if (_voice_generation.load() != generation || _room.get() != source_room) return;
        if (_state.load() == State::Connecting) return;
        // 2. 最终断开后释放设备，旧菜单动作不能再访问失效的 SDK 会话。
        HandleConnectionFailure(QStringLiteral("语音连接已断开"));
    }, Qt::QueuedConnection);
}

void VoiceManager::onReconnecting(livekit::Room& room, const livekit::ReconnectingEvent&)
{
    // 1. 只更新仍被管理器拥有的语音连接。
    livekit::Room* source_room = &room;
    const quint64 generation = _voice_generation.load();
    QMetaObject::invokeMethod(this, [this, source_room, generation]() {
        if (_voice_generation.load() == generation && _room.get() == source_room) setState(State::Reconnecting);
    }, Qt::QueuedConnection);
}

void VoiceManager::onReconnected(livekit::Room& room, const livekit::ReconnectedEvent&)
{
    // 1. 恢复音频前确认本轮房间，旧重连不能重新发布麦克风。
    livekit::Room* source_room = &room;
    const quint64 generation = _voice_generation.load();
    QMetaObject::invokeMethod(this, [this, source_room, generation]() {
        if (_voice_generation.load() == generation && _room.get() == source_room) handleConnected();
    }, Qt::QueuedConnection);
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
    livekit::Room& room, const livekit::ActiveSpeakersChangedEvent& event)
{
    // 1. Participant 指针由 Room 管理，只在回调内转换为身份字符串，不跨线程保存。
    QStringList identities;
    for (livekit::Participant* participant : event.speakers)
    {
        if (participant)
            identities.append(QString::fromStdString(participant->identity()));
    }

    // 2. 旧 Room 的成员列表不能覆盖新房间 UI。
    livekit::Room* source_room = &room;
    const quint64 generation = _voice_generation.load();
    QMetaObject::invokeMethod(this, [this, source_room, generation, identities]() {
        if (_voice_generation.load() == generation && _room.get() == source_room) emit sig_active_speakers_changed(identities);
    }, Qt::QueuedConnection);
}
