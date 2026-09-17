/*
***********************************************************************************
*
* @file         voicemanager.h
* @brief        LiveKit 房间语音管理类
*
* @details      负责获取短期语音 Token、连接 LiveKit 房间、发布麦克风音轨，
*               并为后续语音 UI 提供状态和开关信号。
*
***********************************************************************************
*/
#pragma once

#include "singleton.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>

#include <atomic>
#include <memory>

#include <livekit/livekit.h>

class VoiceManager : public QObject,
                     public Singleton<VoiceManager>,
                     public livekit::RoomDelegate
{
    Q_OBJECT
    friend class Singleton<VoiceManager>;

public:
    // 音频设备的显示名称和稳定 ID，UI 应保存 id，不要保存设备序号。
    struct AudioDevice
    {
        QString name;
        QString id;
    };

    // 语音房间当前连接状态，UI 可根据该状态更新提示文字或图标。
    enum class State
    {
        Disconnected,
        RequestingToken,
        Connecting,
        Connected,
        Reconnecting,
        Error
    };
    Q_ENUM(State)

    void joinRoom(const QString& room_id);       // 请求 Token 并加入语音房间
    void leaveRoom();                            // 断开语音房间并释放音频资源
    void suspendAudio();                         // 暂停大厅期间的麦克风和远端音频
    void resumeAudio();                          // 恢复大厅前保存的音频状态

    QVector<AudioDevice> recordingDevices() const; // 获取可用麦克风列表
    QVector<AudioDevice> playoutDevices() const;   // 获取可用扬声器/耳机列表
    bool setRecordingDevice(const QString& device_id); // 切换麦克风，不重连房间
    bool setPlayoutDevice(const QString& device_id);   // 切换扬声器/耳机，不重连房间

    void setMicrophoneEnabled(bool enabled);     // 打开或关闭本地麦克风
    void setSpeakerEnabled(bool enabled);        // 打开或关闭远端语音接收

    bool microphoneEnabled() const;               // 获取麦克风开关状态
    bool speakerEnabled() const;                  // 获取听筒开关状态
    State state() const;                          // 获取当前连接状态

    ~VoiceManager() override;                     // 释放房间和平台音频资源

signals:
    void sig_state_changed(VoiceManager::State state); // 状态变化通知
    void sig_error(const QString& message);             // 连接或音频初始化失败
    void sig_microphone_changed(bool enabled);          // 麦克风状态变化通知
    void sig_speaker_changed(bool enabled);             // 听筒状态变化通知
    void sig_active_speakers_changed(const QStringList& identities); // 正在说话的成员
    void sig_audio_device_changed(bool recording, const QString& device_id); // 设备切换成功通知

private:
    VoiceManager();

    void requestVoiceToken(const QString& room_id);     // 向 GateServer 请求短期 Token
    void connectLiveKit(const QString& url, const QString& token); // 在工作线程连接房间
    void handleConnected();                             // 在 Qt 线程完成音频初始化
    void publishMicrophone();                          // 创建并发布平台麦克风
    void applySpeakerState();                           // 应用远端音轨订阅状态
    void setState(State state);                        // 更新状态并发出信号

    void onConnectionStateChanged(
        livekit::Room& room,
        const livekit::ConnectionStateChangedEvent& event) override;
    void onDisconnected(
        livekit::Room& room,
        const livekit::DisconnectedEvent& event) override;
    void onReconnecting(
        livekit::Room& room,
        const livekit::ReconnectingEvent& event) override;
    void onReconnected(
        livekit::Room& room,
        const livekit::ReconnectedEvent& event) override;
    void onTrackPublished(
        livekit::Room& room,
        const livekit::TrackPublishedEvent& event) override;
    void onTrackSubscribed(
        livekit::Room& room,
        const livekit::TrackSubscribedEvent& event) override;
    void onActiveSpeakersChanged(
        livekit::Room& room,
        const livekit::ActiveSpeakersChangedEvent& event) override;

private:
    QNetworkAccessManager* _network_manager = nullptr; // 语音 Token HTTP 请求管理器
    QNetworkReply* _token_reply = nullptr;              // 当前 Token 请求，用于离开时取消
    QThread* _connect_thread = nullptr;                // 避免 Room::connect 阻塞 UI 线程

    std::unique_ptr<livekit::Room> _room;               // LiveKit 房间对象
    std::unique_ptr<livekit::PlatformAudio> _platform_audio; // 系统麦克风和扬声器设备
    std::shared_ptr<livekit::PlatformAudioSource> _audio_source; // 麦克风采集源
    std::shared_ptr<livekit::LocalAudioTrack> _audio_track; // 本地发布的语音轨道

    QString _room_id;                                  // 当前语音房间 ID
    std::atomic<State> _state{State::Disconnected};    // 当前状态
    std::atomic<bool> _microphone_enabled{true};       // 默认打开麦克风
    std::atomic<bool> _speaker_enabled{true};          // 默认打开听筒
    std::atomic<bool> _audio_suspended{false};          // 是否因暂离大厅而暂停音频
    bool _microphone_before_suspend = true;             // 暂停前的麦克风状态
    bool _speaker_before_suspend = true;                // 暂停前的听筒状态

    mutable QMap<QString,AudioDevice> _cached_recording_devices;    //缓存麦克风设备
    mutable QMap<QString,AudioDevice> _cached_playout_devices;      //缓存扬声器设备
};
