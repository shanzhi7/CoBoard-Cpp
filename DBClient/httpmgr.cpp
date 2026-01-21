#include "httpmgr.h"


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

void HttpMgr::slot_http_finished(ReqId reqid, QString res, ErrorCodes err, Modules mod)
{
    if(mod == Modules::MOD_REGISTER)
    {
        //发送信号通知指定模块http响应结束
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
}
