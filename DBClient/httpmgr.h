/***********************************************************************************
*
* @file         httpmgr.h
* @brief        http管理类
*
* @author       shanzhi
* @date         2026/01/20
* @history
***********************************************************************************/
#ifndef HTTPMGR_H
#define HTTPMGR_H

#include "singleton.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>

class HttpMgr : public QObject,public Singleton<HttpMgr>,
                public std::enable_shared_from_this<HttpMgr>
{
    Q_OBJECT
    friend class Singleton<HttpMgr>;

public:
    ~HttpMgr();

    void postHttpRequest(QUrl url,QJsonObject json,ReqId reqid,Modules mod); //post请求
    // 上传文件专用接口 (PUT 方法直传 OSS)
    void uploadFile(QUrl url, QString filePath, ReqId reqid, Modules mod);

private:
    explicit HttpMgr();    //私有构造函数
    QNetworkAccessManager mananger;         //管理对象

signals:
    void sig_http_finished(ReqId reqid, QString res, ErrorCodes err, Modules mod);               //http请求完成信号

    void sig_reg_mod_finish(ReqId,QString,ErrorCodes);          //通知注册模块
    void sig_reset_mod_finish(ReqId,QString,ErrorCodes);        //通知重置密码模块
    void sig_login_mod_finish(ReqId,QString,ErrorCodes);        //通知登录模块
    void sig_lobby_mod_finish(ReqId,QString,ErrorCodes);        //通知大厅模块

private slots:
    void slot_http_finished(ReqId reqid, QString res, ErrorCodes err, Modules mod);              //http请求完成信号
};

#endif // HTTPMGR_H
