#include "mainwindow.h"

#include "global.h"
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

    //加载配置文件
    QString fileName = "config.ini";
    QString app_path = QCoreApplication::applicationDirPath();
    qDebug()<<"app path: "<<app_path;
    QString config_path = QDir::toNativeSeparators(app_path + QDir::separator() + fileName);
    QSettings settings(config_path,QSettings::IniFormat);
    QString gate_host = settings.value("GateServer/host").toString();
    QString gate_port = settings.value("GateServer/port").toString();

    gate_url_prefix = "http://" + gate_host + ":" + gate_port;

    MainWindow w;
    w.show();
    return a.exec();
}
