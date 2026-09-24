#include "mainwindow.h"
#include "global.h"
#include "voicemanager.h"
#include <livekit/livekit.h>
#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QDir>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QCoreApplication>
#include <QUuid>

// 引入 Windows 头文件
#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 测试模式只通过命令行显式开启，避免正常启动时改变登录和画布行为。
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("SyncCanvas 客户端"));
    parser.addHelpOption();
    const QCommandLineOption test_mode_option(
        QStringLiteral("test-mode"),
        QStringLiteral("启用延迟测试模式，角色为 sender 或 receiver"),
        QStringLiteral("role"));
    const QCommandLineOption test_rate_option(
        QStringLiteral("test-rate"),
        QStringLiteral("测试发送频率（Hz）"),
        QStringLiteral("hz"), QStringLiteral("60"));
    const QCommandLineOption test_duration_option(
        QStringLiteral("test-duration"),
        QStringLiteral("正式采样持续时间（秒）"),
        QStringLiteral("seconds"), QStringLiteral("120"));
    const QCommandLineOption test_warmup_option(
        QStringLiteral("test-warmup"),
        QStringLiteral("正式采样前的预热时间（秒）"),
        QStringLiteral("seconds"), QStringLiteral("30"));
    const QCommandLineOption test_output_option(
        QStringLiteral("test-output"),
        QStringLiteral("JSONL 测试结果路径"),
        QStringLiteral("path"));
    const QCommandLineOption test_run_id_option(
        QStringLiteral("test-run-id"),
        QStringLiteral("测试运行 ID，发送端和接收端应保持一致"),
        QStringLiteral("id"));

    parser.addOption(test_mode_option);
    parser.addOption(test_rate_option);
    parser.addOption(test_duration_option);
    parser.addOption(test_warmup_option);
    parser.addOption(test_output_option);
    parser.addOption(test_run_id_option);
    parser.process(a);

    LatencyTestOptions test_options;
    const QString test_role = parser.value(test_mode_option).trimmed().toLower();
    if (!test_role.isEmpty())
    {
        if (test_role == QStringLiteral("sender"))
            test_options.role = LatencyTestRole::Sender;
        else if (test_role == QStringLiteral("receiver"))
            test_options.role = LatencyTestRole::Receiver;
        else
        {
            qCritical() << "--test-mode 只能是 sender 或 receiver";
            return 1;
        }

        bool rate_ok = false;
        bool duration_ok = false;
        bool warmup_ok = false;
        test_options.rate_hz = parser.value(test_rate_option).toInt(&rate_ok);
        test_options.duration_sec = parser.value(test_duration_option).toInt(&duration_ok);
        test_options.warmup_sec = parser.value(test_warmup_option).toInt(&warmup_ok);
        if (!rate_ok || test_options.rate_hz < 1 || test_options.rate_hz > 1000)
        {
            qCritical() << "--test-rate 必须在 1 到 1000 之间";
            return 1;
        }
        if (!duration_ok || test_options.duration_sec < 1)
        {
            qCritical() << "--test-duration 必须是正整数";
            return 1;
        }
        if (!warmup_ok || test_options.warmup_sec < 0)
        {
            qCritical() << "--test-warmup 必须是非负整数";
            return 1;
        }

        test_options.enabled = true;
        test_options.output_path = parser.value(test_output_option);
        test_options.run_id = parser.value(test_run_id_option).trimmed();
        if (test_options.role == LatencyTestRole::Sender && test_options.run_id.isEmpty())
            test_options.run_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    // LiveKit 必须在其他 SDK API 之前初始化，并在 QApplication 退出后释放。
    livekit::initialize();

    //加载配置文件
    QString fileName = "config.ini";
    QString app_path = QCoreApplication::applicationDirPath();
    qDebug()<<"app path: "<<app_path;
    QString config_path = QDir::toNativeSeparators(app_path + QDir::separator() + fileName);
    QSettings settings(config_path,QSettings::IniFormat);
    QString gate_host = settings.value("GateServer/host").toString();
    QString gate_port = settings.value("GateServer/port").toString();

    gate_url_prefix = "http://" + gate_host + ":" + gate_port;

    int exit_code = 0;
    {
        MainWindow w(test_options);
        w.show();
        exit_code = a.exec();
    }

    VoiceManager::getInstance()->leaveRoom();
    VoiceManager::getInstance().reset();
    livekit::shutdown();
    return exit_code;
}
