#include "usermgr.h"
#include <QCoreApplication>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDir>

UserMgr::UserMgr()
    :token(""),_my_info(nullptr)
{
    _netMgr = new QNetworkAccessManager(this);  //初始化http管理对象
}

QString UserMgr::getToken()
{
    return this->token;
}

int UserMgr::getUid()
{
    return this->_my_info->_id;
}

QString UserMgr::getAvatar()
{
    return this->_my_info->_avatar;
}

QString UserMgr::getName()
{
    return this->_my_info->_name;
}


void UserMgr::setToken(QString &token)
{
    this->token = token;
}

void UserMgr::setMyInfo(std::shared_ptr<UserInfo> userInfo)
{
    this->_my_info = userInfo;
}

void UserMgr::setAvatar(QString avatar)
{
    this->_my_info->_avatar = avatar;
}

void UserMgr::loadAvatar(const QString &url, QLabel *label)
{
    if(url.isEmpty() || label == nullptr)
    {
        qDebug()<<"url或label为空";
        return;
    }
    QString fileName = url.section('/',-1);     //获取文件名字

    //本地缓存目录，路径拼接 (使用 Qt 推荐的拼接方式，跨平台)
    QString appPath = QCoreApplication::applicationDirPath();
    QString cacheDirPath = appPath + QDir::separator() + "avatar_cache";

    // QDir::cleanPath 会把路径里的 "//" 或 "\/" 整理干净
    QString localPath = QDir::cleanPath(cacheDirPath + QDir::separator() + fileName);

    QDir dir(cacheDirPath);
    if(!dir.exists())   //不存在，创建目录
    {
        dir.mkpath(".");
    }

    if(QFile::exists(localPath)) //如果本地已经有这个文件了
    {
        QPixmap pix(localPath);
        label->setPixmap(pix.scaled(label->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
        qDebug()<<"加载头像(命中缓存)";
        return;
    }

    //本地没有，发起网络请求下载
    qDebug() << "本地无缓存，开始下载头像:" << url;
    QNetworkRequest request(url);
    QNetworkReply* reply = _netMgr->get(request);

    //异步等待下载结束
    connect(reply, &QNetworkReply::finished, [=]() {
        if (reply->error() == QNetworkReply::NoError)
        {
            QByteArray data = reply->readAll();

            // 显示图片
            QPixmap pix;
            if (pix.loadFromData(data))
            {
                label->setPixmap(pix.scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

                // 保存到本地缓存，下次就不用下载了
                QFile file(localPath);
                if (file.open(QIODevice::WriteOnly))
                {
                    file.write(data);
                    file.close();
                }
            }
        }
        else
        {
            qDebug() << "头像下载失败:" << reply->errorString();
        }
        reply->deleteLater();
    });
}

void UserMgr::loadAvatar(const QString &url, QTreeWidgetItem *item)
{
    if(url.isEmpty() || item == nullptr) return;

    QString fileName = url.section('/', -1);
    QString appPath = QCoreApplication::applicationDirPath();
    QString cacheDirPath = appPath + QDir::separator() + "avatar_cache";
    QString localPath = QDir::cleanPath(cacheDirPath + QDir::separator() + fileName);

    // 检查缓存
    if(QFile::exists(localPath))
    {
        QPixmap pix(localPath);
        // 设置 Icon，不是 setPixmap
        item->setIcon(0, QIcon(pix));
        return;
    }

    // 没缓存，下载
    QNetworkRequest request(url);
    QNetworkReply* reply = _netMgr->get(request);

    connect(reply, &QNetworkReply::finished, [=]() {
        if (reply->error() == QNetworkReply::NoError)
        {
            QByteArray data = reply->readAll();
            QPixmap pix;
            if (pix.loadFromData(data))
            {
                // 设置 Icon
                item->setIcon(0, QIcon(pix));

                // 存缓存
                QFile file(localPath);
                if (file.open(QIODevice::WriteOnly))
                {
                    file.write(data);
                    file.close();
                }
            }
        }
        reply->deleteLater();
    });
}

std::shared_ptr<const UserInfo> UserMgr::getMyInfo()
{
    return this->_my_info;
}
