/***********************************************************************************
* @file         canvastcpworker.h
* @brief        独立线程中的画板 TCP 连接、分帧和入站协议解码
* @author       shanzhi
* @date         2026/10/22
* @history
***********************************************************************************/
#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QObject>

#include "decodedtcpmessage.h"

class QTcpSocket;

class CanvasTcpWorker : public QObject
{
    Q_OBJECT
public:
    explicit CanvasTcpWorker(QObject* parent = nullptr); // 仅初始化对象，套接字在线程启动后创建。
    ~CanvasTcpWorker() override; // 在线程结束的延迟删除阶段释放网络资源。

public slots:
    void ConnectToServer(QString host, quint16 port, quint64 generation); // 替换旧连接并重置分帧缓存。
    void SendData(quint16 message_id, QByteArray body, quint64 generation); // 为当前连接封包并异步发送。
    void DisconnectFromServer(quint64 generation); // 作废旧连接并在所属线程关闭套接字。

signals:
    void SigConnected(quint64 generation); // 当前代次连接已经建立。
    void SigDisconnected(quint64 generation); // 当前代次连接已经断开。
    void SigSocketError(quint64 generation, QAbstractSocket::SocketError error, QString detail); // 交回连接错误。
    void SigMessageDecoded(quint64 generation, DecodedTcpMessage decoded); // 按 TCP 到达顺序交付解码结果。
    void SigDecodeFailed(quint64 generation, quint16 message_id); // 丢弃无效包并通知控制器结束等待状态。

private:
    void CloseSocket(); // 解除旧连接回调、关闭套接字并清空帧缓存。
    void ReadFrames(); // 保留分片头和分片体，连续处理完整帧。
    void DecodeMessage(quint16 message_id, const QByteArray& body); // 按消息编号解码 JSON 或 Protobuf。

    QTcpSocket* _socket = nullptr; // worker 拥有的套接字，只能在所属线程操作。
    QByteArray _buffer; // 当前连接尚未组成完整帧的数据。
    quint64 _generation = 0; // 连接代次，隔离重定向、重连和注销前的事件。
};
