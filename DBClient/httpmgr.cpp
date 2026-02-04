#include "httpmgr.h"
#include <QFile>
#include <QFileInfo>


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
