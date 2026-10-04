#include "httpmgr.h"
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QTimer>


HttpMgr::HttpMgr()
{
    connect(this,&HttpMgr::sig_http_finished,this,&HttpMgr::slot_http_finished);
}
HttpMgr::~HttpMgr()
{
    std::cout<<"this is httpMgr destroyed"<<std::endl;
}

void HttpMgr::postHttpRequest(QUrl url, QJsonObject json, ReqId reqid, Modules mod)
{
    //将json对象转化为字节数组
    QByteArray data = QJsonDocument(json).toJson();

    //创建一个http post请求，设置请求头和请求体

    //设置请求头
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");       //设置Content-Type字段
    request.setHeader(QNetworkRequest::ContentLengthHeader,data.length());          //设置Content-Length字段

    //发送请求，处理请求响应
    QNetworkReply* reply = mananger.post(request,data);
    connect(reply,&QNetworkReply::finished,this,[this,reply,reqid,mod](){
        //如果出错
        if(reply->error() != QNetworkReply::NoError)
        {
            qDebug()<<reply->errorString();

            //发送信号通知完成
            emit this->sig_http_finished(reqid,"",ErrorCodes::ERR_NETWORK,mod);
            reply->deleteLater();
            return;
        }

        //无错误,读取回复
        QString res = QString::fromUtf8(reply->readAll());
        //发送信号通知完成
        emit this->sig_http_finished(reqid,res,ErrorCodes::SUCCESS,mod);
        reply->deleteLater();
    });

}

void HttpMgr::uploadFile(QUrl url, QString filePath, ReqId reqid, Modules mod)
{
    // 尝试打开文件
    QFile *file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        qDebug() << "File open failed:" << filePath;
        delete file;
        // 发送打开文件失败的错误
        emit this->sig_http_finished(reqid, "", ErrorCodes::ERR_NETWORK, mod);
        return;
    }

    QNetworkRequest request(url);       //构造请求

    QString suffix = QFileInfo(filePath).suffix().toLower();        //根据后缀简单判断类型
    if (suffix == "png")
    {
        request.setHeader(QNetworkRequest::ContentTypeHeader, "image/png");     //设置 Content-Type
    }
    else
    {
        request.setHeader(QNetworkRequest::ContentTypeHeader, "image/jpeg");
    }
    // 设置文件大小
    request.setHeader(QNetworkRequest::ContentLengthHeader, file->size());
    // 送 PUT 请求 (OSS 直传通常使用 PUT)
    QNetworkReply* reply = mananger.put(request, file);
    file->setParent(reply);     //让reply管理file的生命周期

    connect(reply, &QNetworkReply::finished, this, [this, reply, reqid, mod](){
        if (reply->error() != QNetworkReply::NoError)
        {
            qDebug() << "Upload Error:" << reply->errorString();
            // 打印详细服务端返回 (OSS 403时很有用)
            qDebug() << "Server Response:" << reply->readAll();

            emit this->sig_http_finished(reqid, "", ErrorCodes::ERR_NETWORK, mod);
            reply->deleteLater();
            return;
        }

        // 上传成功，OSS 通常返回空 body 或者 XML，我们这里只需通知成功即可
        // 将 "Success" 作为 res 传回去，表示没报错
        emit this->sig_http_finished(reqid, "Upload Success", ErrorCodes::SUCCESS, mod);
        reply->deleteLater();
    });
}

QString HttpMgr::PostImageSignature(const QUrl& url, const QJsonObject& json, ReqId reqid)
{
    // 1. 每个签名请求分配 UUID，响应不能误用于其他房间或图元。
    const QString request_id = QUuid::createUuid().toString(QUuid::WithoutBraces); // 本地关联，不发送登录日志。
    QNetworkRequest request(url); // 网关 HTTP 请求。
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(30000);
    auto* reply = mananger.post(request, QJsonDocument(json).toJson(QJsonDocument::Compact)); // 仅网关接收登录 Token。
    _image_signature_replies.insert(request_id, reply);
    auto* timeout = new QTimer(reply); // 绝对签名期限。
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout->start(30000);
    connect(reply, &QNetworkReply::finished, this, [this, reply, request_id, reqid] {
        // 2. 被取消的请求丢弃结果，禁止打印签名响应或完整 URL。
        if (_image_signature_replies.remove(request_id))
        {
            const bool is_success = reply->error() == QNetworkReply::NoError && reply->bytesAvailable() <= 64 * 1024; // 有界控制响应。
            const QString response = is_success ? QString::fromUtf8(reply->readAll()) : QString(); // 不记录正文。
            if (!is_success)
            {
                qWarning() << "HttpMgr PostImageSignature request=" << request_id << "error=" << reply->error();
            }
            emit sigImageSignatureFinished(request_id, reqid, response, is_success ? ErrorCodes::SUCCESS : ErrorCodes::ERR_NETWORK);
        }
        reply->deleteLater();
    });
    return request_id;
}

void HttpMgr::CancelImageSignature(const QString& request_id)
{
    // 1. 先移除上下文，abort 的同步完成信号不会再交付结果。
    if (auto* reply = _image_signature_replies.take(request_id))
    {
        reply->abort();
    }
}

void HttpMgr::slot_http_finished(ReqId reqid, QString res, ErrorCodes err, Modules mod)
{
    //发送信号通知指定模块http响应结束
    if(mod == Modules::MOD_REGISTER)
    {
        emit sig_reg_mod_finish(reqid,res,err);
    }
    if(mod == Modules::MOD_RESET)
    {
        emit sig_reset_mod_finish(reqid,res,err);
    }
    if(mod == Modules::MOD_LOGIN)
    {
        emit sig_login_mod_finish(reqid,res,err);
    }
    else if(mod == Modules::MOD_LOBBY)
    {
        emit sig_lobby_mod_finish(reqid, res, err);
    }
}
