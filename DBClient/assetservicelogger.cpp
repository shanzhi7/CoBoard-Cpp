#include "assetservicelogger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>

#include "assetserviceprotocol.h"

namespace
{
QMutex log_mutex; // Qt 工作线程共享日志锁。
QString log_path; // 仅代理进程设置。
QtMessageHandler previous_handler = nullptr; // 恢复应用默认处理器。

void WriteMessage(QtMsgType type, const QMessageLogContext&, const QString& message) // 禁止在处理器里再次打印 Qt 日志。
{
    // 1. 所有线程串行轮转，限制单条日志避免意外的大日志。
    QMutexLocker locker(&log_mutex); // 文件轮转保护。
    const QByteArray line = (QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + " type=" + QString::number(type) + " " + message.left(4096) + '\n').toUtf8(); // 有界 UTF-8 日志。
    if (QFile(log_path).size() + line.size() > 2 * 1024 * 1024)
    {
        QFile::remove(log_path + ".2");
        if ((QFile::exists(log_path + ".1") && !QFile::rename(log_path + ".1", log_path + ".2"))
            || !QFile::rename(log_path, log_path + ".1"))
        {
            return;
        }
    }
    // 2. 写入失败不递归报告，保留正常退出能力。
    QFile file(log_path); // 当前日志文件。
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
    {
        file.write(line);
    }
}
}

void AssetServiceLogger::Install()
{
    // 1. 根目录已创建，日志只由持有独占锁的代理写入。
    log_path = QDir(AssetServiceProtocol::CacheRoot()).filePath("asset-service.log");
    previous_handler = qInstallMessageHandler(WriteMessage);
}

void AssetServiceLogger::Uninstall()
{
    // 1. 此时后台线程已退出，恢复处理器不会与文件写入竞争。
    qInstallMessageHandler(previous_handler);
}
