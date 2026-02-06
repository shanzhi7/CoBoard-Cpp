#include "loginwidget.h"
#include "ui_loginwidget.h"
#include "httpmgr.h"
#include "tcpmgr.h"
#include "usermgr.h"
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QJsonObject>


LoginWidget::LoginWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LoginWidget)
{
    ui->setupUi(this);
    ui->email_edit->setPlaceholderText("请输入您的邮箱");
    ui->password_edit->setPlaceholderText("请输入您的密码");
    ui->password_edit->setEchoMode(QLineEdit::Password);

    // 在构造函数中初始化验证器
    initValidator();
    initHandlerMap();       //初始化回包处理函数

    //点击返回按钮发送切换欢迎界面信号
    connect(ui->cannel_btn,&QPushButton::clicked,this,&LoginWidget::switchWelcome);

    //点击注册按钮发送切换注册页面信号
    connect(ui->register_btn,&QPushButton::clicked,this,&LoginWidget::switchRegister);

    //点击忘记密码按钮发送切换注册页面信号
    connect(ui->reset_btn,&QPushButton::clicked,this,&LoginWidget::switchReset);

    //连接http请求完成信号
    connect(HttpMgr::getInstance().get(),&HttpMgr::sig_login_mod_finish,this,&LoginWidget::slot_login_mod_finish);

    //连接tcp连接请求信号槽函数
    connect(this,&LoginWidget::sig_connect_tcp,TcpMgr::getInstance().get(),&TcpMgr::slot_tcp_connect);

    //CanvasServer连接成功，使用token发送登录请求
    connect(TcpMgr::getInstance().get(),&TcpMgr::sig_con_success,this,&LoginWidget::slot_tcp_con_finish);

    //tcp登录成功，连接发送切换canvas信号
    connect(TcpMgr::getInstance().get(),&TcpMgr::sig_switch_canvas,this,&LoginWidget::switchLobby);
}

LoginWidget::~LoginWidget()
{
    delete ui;
}

QSize LoginWidget::sizeHint() const
{
    return QSize(this->rect().size());
}

void LoginWidget::on_login_btn_clicked()    //登录按钮槽函数
{
    // 遍历所有验证器
    for(auto &validator : _validators)
    {
        // 只要有一个验证不通过，直接返回，不再继续执行登录逻辑
        // 因为 validator() 内部已经调用了 TipWidget::showTip，所以这里不需要再弹窗
        if(!validator())
        {
            return;
        }
    }

    //密码加密
    QByteArray passwordByte;
    QString pas= ui->password_edit->text();
    passwordByte.append(pas.toUtf8());
    QByteArray hash = QCryptographicHash::hash(passwordByte,QCryptographicHash::Sha512);
    QString password_sha512 = hash.toHex();

    QJsonObject json_obj;
    json_obj["email"] = ui->email_edit->text();
    json_obj["password"] = password_sha512;
    HttpMgr::getInstance()->postHttpRequest(gate_url_prefix + "/user_login",
                                        json_obj,ReqId::ID_LOGIN,Modules::MOD_LOGIN);
}

void LoginWidget::slot_login_mod_finish(ReqId reqid, QString res, ErrorCodes err)
{
    if(err != ErrorCodes::SUCCESS)
    {
        TipWidget::showTip(ui->draw_label,tr("网络请求失败"));
        return;
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

void LoginWidget::slot_tcp_con_finish(bool bsuccess)
{
    if(bsuccess)
    {
        TipWidget::showTip(ui->draw_label,"服务器连接成功，正在登录...");

        //序列化发送内容
        QJsonObject jsonObj;
        jsonObj["uid"] = UserMgr::getInstance()->getMyInfo()->_id;
        jsonObj["token"] = UserMgr::getInstance()->getToken();
        jsonObj["name"] = UserMgr::getInstance()->getName();
        jsonObj["avatar"] = UserMgr::getInstance()->getAvatar();

        QJsonDocument jsonDoc(jsonObj);
        QByteArray jsonString = jsonDoc.toJson(QJsonDocument::Indented);

        //发送信号交给tcp管理者处理
        emit TcpMgr::getInstance().get()->sig_send_data(ReqId::ID_CANVAS_LOGIN_REQ,jsonString);
    }
    else
    {
        TipWidget::showTip(ui->draw_label,"网络错误");
    }
}

void LoginWidget::bindValidator(QLineEdit *input, const QString &pattern, const QString &errMsg)
{
    // 定义校验逻辑 (Lambda)
    auto validator = [=]() -> bool
    {
        QString text = input->text();

        // 空值检查
        if (text.isEmpty())
        {
            TipWidget::showTip(input, "输入不能为空");
            return false;
        }

        // 正则匹配
        QRegularExpression rx(pattern);
        if (!rx.match(text).hasMatch())
        {
            TipWidget::showTip(input, errMsg);
            return false;
        }

        return true;
    };

    // 存入列表 (供登录按钮统查)
    _validators.append(validator);

    // 绑定信号 (失焦时自查，提升用户体验)
    connect(input, &QLineEdit::editingFinished, this, [=](){
        validator();
    });
}

void LoginWidget::initValidator()
{
    // 清空旧的验证器列表（防止重复初始化导致累积）
    _validators.clear();

    //邮箱验证
    bindValidator(ui->email_edit,
                  "^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$",
                  "邮箱格式不正确");

    //密码验证
    bindValidator(ui->password_edit,
                  "^[a-zA-Z0-9!@#$%^&*?]{6,15}$",
                  "密码格式错误 (需6-15位)");

}

void LoginWidget::initHandlerMap()
{
    //注册用户回包逻辑
    _handlers_map.insert(ReqId::ID_LOGIN,[this](QJsonObject jsonObj){
        int error = jsonObj["error"].toInt();
        if(error != ErrorCodes::SUCCESS)    //错误
        {
            TipWidget::showTip(ui->draw_label,"错误！请检查输入的内容");
            return;
        }
        TipWidget::showTip(ui->draw_label,"登录成功");
        auto email = jsonObj["email"].toString();
        auto uid = jsonObj["uid"].toInt();
        auto name = jsonObj["name"].toString();
        auto avator = jsonObj["avatar"].toString();
        auto host = jsonObj["host"].toString();
        auto port = jsonObj["port"].toString();

        std::shared_ptr<UserInfo> info = std::make_shared<UserInfo>(uid,name,email,0,avator,"");
        UserMgr::getInstance()->setMyInfo(info);        //设置客户端用户信息

        QString fromServer = jsonObj["server"].toString();
        qDebug()<<"email is "<<email<<"  from server: "<<fromServer<<"reqid is "<<"user login";
        qDebug()<<"uid is "<<uid<<" name is "<<name<<"avatar is "<<avator<<"host is "<<host<<"port is "<<port;

        //todo... 连接canvasServer,跳转页面
        ServerInfo si;          //存储准备连接的CanvasServer信息
        si.Host = host;
        si.Port = port;
        si.Uid = uid;
        si.Token = jsonObj["token"].toString();

        UserMgr::getInstance()->setToken(si.Token);

        emit sig_connect_tcp(si);

    });
}

