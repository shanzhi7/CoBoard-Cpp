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
    QString PostImageSignature(const QUrl& url, const QJsonObject& json, ReqId reqid); // 图片签名独立关联，不与房间切换后的请求混淆。
    void CancelImageSignature(const QString& request_id); // 撤销旧房间签名请求。

private:
    explicit HttpMgr();    //私有构造函数
    QNetworkAccessManager mananger;         //管理对象
    QHash<QString, QNetworkReply*> _image_signature_replies; // 每个签名请求独立上下文。

signals:
    void sig_http_finished(ReqId reqid, QString res, ErrorCodes err, Modules mod);               //http请求完成信号

    void sig_reg_mod_finish(ReqId,QString,ErrorCodes);          //通知注册模块
    void sig_reset_mod_finish(ReqId,QString,ErrorCodes);        //通知重置密码模块
    void sig_login_mod_finish(ReqId,QString,ErrorCodes);        //通知登录模块
    void sig_lobby_mod_finish(ReqId,QString,ErrorCodes);        //通知大厅模块
    void sigImageSignatureFinished(QString request_id, ReqId reqid, QString response, ErrorCodes error); // 图片签名专用回调。

private slots:
    void slot_http_finished(ReqId reqid, QString res, ErrorCodes err, Modules mod);              //http请求完成信号
};

#endif // HTTPMGR_H
