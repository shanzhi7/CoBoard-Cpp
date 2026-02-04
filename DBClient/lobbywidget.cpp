#include "lobbywidget.h"
#include "httpmgr.h"
#include "ui_lobbywidget.h"
#include "joinroomdialog.h"
#include "hoverwidget.h"
#include "newroomdialog.h"
#include "usermgr.h"
#include "tcpmgr.h"
#include "tipwidget.h"
#include "message.pb.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStyleOption>
#include <QPainter>

LobbyWidget::LobbyWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LobbyWidget)
{
    ui->setupUi(this);
    //连接点击，弹出创建房间对话框
    connect(ui->create_widget,&HoverWidget::clicked,this,&LobbyWidget::slot_create_clicked);

    //连接点击加入房间，弹出加入房间对话框
    connect(ui->join_widget,&HoverWidget::clicked,this,&LobbyWidget::slot_join_clicked);

    //连接创建房间成功，切换canvas页面
    connect(TcpMgr::getInstance().get(),&TcpMgr::sig_create_room_finish,this,&LobbyWidget::slot_create_room_finish);

    //连接加入房间成功，切换canvas页面
    connect(TcpMgr::getInstance().get(),&TcpMgr::sig_join_room_finish,this,&LobbyWidget::slot_join_room_finish);

    //连接http请求完成信号
    connect(HttpMgr::getInstance().get(),&HttpMgr::sig_lobby_mod_finish,this,&LobbyWidget::slot_lobby_mod_finish);

    initIcons();            //初始化图标
    initHandles_map();      //初始化回包处理函数

}

LobbyWidget::~LobbyWidget()
{
    delete ui;
}

void LobbyWidget::paintEvent(QPaintEvent *event)
{
    //使样式正常显示
    Q_UNUSED(event);
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void LobbyWidget::initIcons()
{
    ui->avator_lbl->setScaledContents(false);
    QPixmap avator = QPixmap(":/res/deflaut_head.jpg");     //更改
    avator = avator.scaled(ui->avator_lbl->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation);
    ui->avator_lbl->setPixmap(avator);

    // 加载原始黑色图标
    QPixmap iconCreate(":/res/offline.png");
    QPixmap iconJoin(":/res/shared.png");


    // 变成蓝色
    QColor targetColor("#4ea5f9");

    QPixmap newCreate = applyColor(iconCreate, targetColor);
    QPixmap newJoin = applyColor(iconJoin, targetColor);

    // 设置给 Label
    ui->create_head_lbl->setPixmap(newCreate);
    ui->join_head_lbl->setPixmap(newJoin);

    // 置图片自动缩放，防止变形
    ui->create_head_lbl->setScaledContents(true);
    ui->join_head_lbl->setScaledContents(true);
}

void LobbyWidget::initHandles_map()
{
    //获取OSS签名回包逻辑
    _handlers_map.insert(ReqId::ID_GET_OSS_TOKEN,[this](QJsonObject jsonObj){
        int error = jsonObj["error"].toInt();
        if(error != ErrorCodes::SUCCESS)    //错误
        {
            TipWidget::showTip(ui->draw_label,"错误！网络请求失败");
            return;
        }
        QString signedUrl = jsonObj["url"].toString();              //签名Url
        _pendingOssKey = jsonObj["oss_key"].toString();             //OSS文件路径
        _pendingPublicUrl = jsonObj["public_url"].toString();       //OSS公开头像地址
        qDebug() << "拿到上传权限，准备上传。最终地址将是:" << _pendingPublicUrl;

        HttpMgr::getInstance()->uploadFile(QUrl(signedUrl),_uploadingPath,ID_UPLOAD_IMAGE,MOD_LOBBY);   //上传文件到OSS
    });

    //todo... 上传图片回包逻辑
    _handlers_map.insert(ReqId::ID_UPLOAD_IMAGE,[this](QJsonObject jsonObj){
        int error = jsonObj["error"].toInt();
        if(error != ErrorCodes::SUCCESS)    //错误
        {
            TipWidget::showTip(ui->draw_label,"错误！网络请求失败");
            return;
        }

        // 使用刚才暂存的地址
        if(!_pendingPublicUrl.isEmpty())
        {
            //todo... 通知 GateServer->LogicServer更新数据库
            QJsonObject json_obj;
            json_obj["public_url"] = _pendingPublicUrl;
            json_obj["uid"] = UserMgr::getInstance()->getUid();
            HttpMgr::getInstance()->postHttpRequest(gate_url_prefix + "/save_avator",
                                                    json_obj,ReqId::ID_SAVE_IMAGE,Modules::MOD_LOBBY);
            qDebug() << "通知服务器准备更新头像为:" << _pendingPublicUrl;
        }
    });

    //todo... 图片保存成功回包函数
    _handlers_map.insert(ReqId::ID_SAVE_IMAGE,[this](QJsonObject jsonObj){
        int error = jsonObj["error"].toInt();
        if(error != message::ErrorCodes::SUCCESS)
        {
            TipWidget::showTip(ui->draw_label, "保存头像失败，请重试");
            return;
        }
        // 从服务器回包里拿 URL (最权威的数据)
        QString server_confirmed_url = jsonObj["public_url"].toString();

        // 提示成功
        TipWidget::showTip(ui->draw_label, "头像修改成功！");

        // 更新 UserMgr 单例 (内存数据)
        UserMgr::getInstance()->setAvatar(server_confirmed_url);

        // 更新 UI 显示
        // 此时可以用本地文件 m_uploadingPath 来显示（为了不用再下载一次，体验更好）
        // 但必须确保 UserMgr 里存的是 server_confirmed_url
        if(!_uploadingPath.isEmpty())
        {
            QPixmap pix(_uploadingPath);
            ui->avator_lbl->setPixmap(pix.scaled(ui->avator_lbl->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }

        qDebug() << "流程结束：头像已最终同步至服务器:" << server_confirmed_url;

        // 清理暂存变量
        _pendingPublicUrl.clear();
        _uploadingPath.clear();
    });
}

void LobbyWidget::slot_create_clicked()
{
    NewRoomDialog dlg(this);
    if(dlg.exec() == QDialog::Accepted) //点击的是创建按钮
    {
        QString roomName = dlg.getRoomName();       //获取房间名字
        QSize canvasSize = dlg.getSelectedSize();   //获取用户选择的画布尺寸

        //todo... 发送网络请求
        QJsonObject json_obj;
        json_obj["room_name"] = roomName;                   //房间名
        auto info = UserMgr::getInstance()->getMyInfo();    //房主id
        json_obj["width"] = canvasSize.width();   //画布尺寸
        json_obj["height"] = canvasSize.height();

        if (info)
        {
            json_obj["owner_uid"] = info->_id;
        } else
        {
            qDebug() << "Error: UserInfo is null!";
            return;
        }
        QJsonDocument jsonDoc(json_obj);
        QByteArray jsonString = jsonDoc.toJson();
        TcpMgr::getInstance()->sig_send_data(ReqId::ID_CREAT_ROOM_REQ,jsonString);      //发送TCP包

    }

}

void LobbyWidget::slot_join_clicked()
{
    JoinRoomDialog dlg(this);
    if(dlg.exec() == QDialog::Accepted)
    {
        QString room_id = dlg.getRoomId();

        TipWidget::showTip(ui->draw_label,"正在加入房间...");
        message::JoinRoomReq req;
        req.set_room_id(room_id.toStdString());        //加入的房间号
        req.set_uid(UserMgr::getInstance()->getMyInfo()->_id);  //加入者(自己)

        std::string binaryData;
        if(req.SerializeToString(&binaryData))
        {
            // 将 std::string 转回 Qt 用的 QByteArray
            QByteArray sendData = QByteArray::fromStdString(binaryData);
            emit TcpMgr::getInstance()->sig_send_data(ReqId::ID_JOIN_ROOM_REQ, sendData);
        }
        else
        {
            TipWidget::showTip(ui->draw_label,"加入房间失败");
        }
    }
}

void LobbyWidget::slot_create_room_finish(std::shared_ptr<RoomInfo> room_info)
{
    emit sig_switchCanvas(room_info);
}

void LobbyWidget::slot_join_room_finish(std::shared_ptr<RoomInfo> room_info)
{
    emit sig_switchCanvas(room_info);
}

void LobbyWidget::on_upload_btn_clicked()
{
    QString path = QFileDialog::getOpenFileName(this, "选择头像", "", "Images (*.png *.jpg)");
    if(path.isEmpty()) return;

    _uploadingPath = path; // 记录一下，方便本地预览

    // 构造请求，告诉 GateServer 我是 uid=1001，我要传 jpg
    QJsonObject json;
    json["uid"] = QString::number(UserMgr::getInstance()->getUid());
    json["suffix"] = QFileInfo(path).suffix();

    // 发送请求：获取签名
    HttpMgr::getInstance()->postHttpRequest(
        QUrl(gate_url_prefix + "/get_oss_token"),
        json,
        ID_GET_OSS_TOKEN,
        MOD_LOBBY
        );
}

void LobbyWidget::slot_lobby_mod_finish(ReqId reqid, QString res, ErrorCodes err)
{
    if(err != ErrorCodes::SUCCESS)
    {
        // 如果是上传图片失败，这里 res 可能包含阿里云的 XML 错误信息，打印出来调试
        if(reqid == ID_UPLOAD_IMAGE)
        {
            qDebug() << "OSS Error XML:" << res;
        }
        TipWidget::showTip(ui->draw_label,tr("网络请求失败"));
        return;
    }

    // ================== 【新增】特殊处理 OSS 上传回包 ==================
    if(reqid == ID_UPLOAD_IMAGE)
    {
        // 阿里云上传成功通常返回空包，不需要解析 JSON
        // 直接构造一个伪造的 JSON 对象传给 handler，或者直接在这里处理
        QJsonObject dummyJson;
        dummyJson["error"] = ErrorCodes::SUCCESS; // 手动标记为成功

        if(_handlers_map.contains(reqid))
        {
            _handlers_map[reqid](dummyJson);
        }
        return; // 【关键】直接返回，不走下面的通用 JSON 解析
    }

    //解析json字符串，res转换成QByteArray
    QByteArray data = res.toUtf8();
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

    //json解析错误
    if(jsonDoc.isNull())
    {
        TipWidget::showTip(ui->draw_label,tr("json解析错误"));
        return;
    }
    QJsonObject jsonObject = jsonDoc.object();
    //调用对应的处理函数
    _handlers_map[reqid](jsonObject);
}

