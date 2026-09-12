#include "mainwindow.h"
#include "global.h"
#include "voicemanager.h"
#include <livekit/livekit.h>
#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QDir>

// 引入 Windows 头文件
#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

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
        MainWindow w;
        w.show();
        exit_code = a.exec();
    }

    VoiceManager::getInstance()->leaveRoom();
    VoiceManager::getInstance().reset();
    livekit::shutdown();
    return exit_code;
}
