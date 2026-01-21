#include "registerwidget.h"
#include "ui_registerwidget.h"
#include "global.h"
#include "tipwidget.h"
#include "httpmgr.h"
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QUrl>
#include <QFile>

RegisterWidget::RegisterWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::RegisterWidget)
{
    ui->setupUi(this);
    //连接点击返回登录按钮发送切换登录界面信号
    connect(ui->cannel_btn,&QPushButton::clicked,this,&RegisterWidget::switchLogin);

    //连接服务器回复信号槽函数
    connect(HttpMgr::getInstance().get(),&HttpMgr::sig_reg_mod_finish,this,&RegisterWidget::slot_reg_mod_finish);

    //设置占位字符
    ui->nick_edit->setPlaceholderText("请输入您的昵称");
    ui->email_edit->setPlaceholderText("请输入您的邮箱 (例如: name@example.com)");
    ui->password_edit->setPlaceholderText("请输入您的密码");
    ui->confirm_edit->setPlaceholderText("请再次输入您的密码");
    ui->verifycode_edit->setPlaceholderText("请输入您的验证码");

    // 设置工具提示，鼠标放上去会显示
    ui->email_edit->setToolTip("只能包含字母、数字、下划线，且必须包含 @ 和域名后缀");
    initValidator();        //注册验证器

    initToolButton();       //初始化toolbtn

    initHandersMap();       //初始化回包处理函数
}

RegisterWidget::~RegisterWidget()
{
    delete ui;
}

void RegisterWidget::initToolButton()
{
    ui->passworld_tool->setIcon(QIcon(":/res/visible.png"));
    ui->confirm_tool->setIcon(QIcon(":/res/visible.png"));

    connect(ui->passworld_tool,&QToolButton::clicked,this,[=](){
        if (ui->password_edit->echoMode() == QLineEdit::Password)
        {
            // 切换为明文
            ui->password_edit->setEchoMode(QLineEdit::Normal);
            ui->passworld_tool->setIcon(QIcon(":/res/visible.png")); // 换成睁眼
        }
        else
        {
            // 切换为密文
            ui->password_edit->setEchoMode(QLineEdit::Password);
            ui->passworld_tool->setIcon(QIcon(":/res/unvisible.png")); // 换成闭眼
        }
    });

    connect(ui->confirm_tool,&QToolButton::clicked,this,[=](){

        if (ui->confirm_edit->echoMode() == QLineEdit::Password)
        {
            // 切换为明文
            ui->confirm_edit->setEchoMode(QLineEdit::Normal);
            ui->confirm_tool->setIcon(QIcon(":/res/visible.png")); // 换成睁眼
        }
        else
        {
            // 切换为密文
            ui->confirm_edit->setEchoMode(QLineEdit::Password);
            ui->confirm_tool->setIcon(QIcon(":/res/unvisible.png")); // 换成闭眼
        }
    });
}

void RegisterWidget::initHandersMap()
{
    _handlers_map.insert(ReqId::ID_GET_VERIFY_CODE,[this](QJsonObject jsonObj){
        int error = jsonObj["error"].toInt();
        if(error != ErrorCodes::SUCCESS)    //错误
        {
            TipWidget::showTip(ui->title_lbl,"错误！请检查输入的内容");
            return;
        }
        auto email = jsonObj["email"].toString();
        QString fromServer = jsonObj["server"].toString();
        qDebug()<<"email is "<<email<<"  from server: "<<fromServer;

        //todo 切换登录页面
    });
}

void RegisterWidget::bindValidator(QLineEdit *input, const QString &pattern, const QString &errMsg)
{
    // 1. 定义校验逻辑 (Lambda)
    auto validator = [=]() -> bool {
        QString text = input->text();

        // 空值检查 (如果必填)
        if (text.isEmpty()) {
            TipWidget::showTip(input, "输入不能为空");
            return false;
        }

        // 正则匹配
        QRegularExpression rx(pattern);
        if (!rx.match(text).hasMatch()) {
            // 只弹窗，不改样式
            TipWidget::showTip(input, errMsg);
            return false;
        }

        return true;
    };

    // 2. 存入列表 (供注册按钮统查)
    _validators.append(validator);

    // 3. 绑定信号 (失焦时自查)
    connect(input, &QLineEdit::editingFinished, this, [=](){
        validator();
    });
}


//点击获取验证码按钮槽函数
void RegisterWidget::on_get_verify_btn_clicked()
{
    //邮箱正则表达式
    static const QRegularExpression regex("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$");
    QString email = ui->email_edit->text();         //获取用户输入的邮箱
    bool match = regex.match(email).hasMatch();     //判断是否匹配

    if(match)//格式正确
    {
        TipWidget::showTip(ui->title_lbl, "尝试获取验证码...");
        //发送http请求
        QJsonObject json_obj;
        json_obj["email"] = email;
        HttpMgr::getInstance()->postHttpRequest(QUrl(gate_url_prefix + "/get_verifycode"),json_obj,
                                                ReqId::ID_GET_VERIFY_CODE,Modules::MOD_REGISTER);
    }
    else
    {
        //邮箱格式不正确
        TipWidget::showTip( ui->email_edit,tr("邮箱格式不正确"));
    }
}


void RegisterWidget::on_register_btn_clicked()
{
    // 遍历检查所有规则
    for (const auto &validator : _validators)
    {
        if (!validator())
        {
            // 如果有一个失败，validator() 内部已经弹窗了
            // 直接中断，不发送请求
            return;
        }
    }
}

void RegisterWidget::slot_reg_mod_finish(ReqId reqid, QString res, ErrorCodes err)
{
    if(err != ErrorCodes::SUCCESS)
    {
        TipWidget::showTip(ui->title_lbl,tr("网络请求失败"));
        return;
    }

    //解析json字符串，res转换成QByteArray
    QByteArray data = res.toUtf8();
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

    //json解析错误
    if(jsonDoc.isNull())
    {
        TipWidget::showTip(ui->title_lbl,tr("json解析错误"));
        return;
    }
    QJsonObject jsonObject = jsonDoc.object();
    //调用对应的处理函数
    _handlers_map[reqid](jsonObject);
}

//初始化输入验证器
void RegisterWidget::initValidator()
{
    // 1. 昵称 (Nick)
    // 规则：允许中文、字母、数字、下划线，长度 2-10 位
    // ==========================================
    // \p{Han} 代表所有汉字 (包括生僻字)
    bindValidator(ui->nick_edit,
                  "^[\\p{Han}a-zA-Z0-9_]{2,10}$",
                  "昵称格式错误 (2-10位，支持中英数_)");

    // ==========================================
    // 2. 邮箱 (Email)
    // 规则：标准邮箱格式
    // ==========================================
    bindValidator(ui->email_edit,
                  "^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$",
                  "邮箱格式不正确");

    // ==========================================
    // 3. 密码 (Password)
    // 规则：6-15位，包含字母、数字或常见符号
    // ==========================================
    bindValidator(ui->password_edit,
                  "^[a-zA-Z0-9!@#$%^&*?]{6,15}$",
                  "密码长度需为6-15位");

    // ==========================================
    // 4. 验证码 (Verify Code)
    // 规则：假设是 4-6 位字母或数字 (根据你的业务调整)
    // ==========================================
    bindValidator(ui->verifycode_edit,
                  "^[a-zA-Z0-9]{4,6}$",
                  "验证码错误 (请输入4-6位字符)");

    // ==========================================
    // 5. 【特殊】确认密码 (Confirm Password)
    // 规则：必须等于 password_edit 的内容
    // 这里的逻辑特殊，我们需要手动连接信号，不能直接用 bindValidator 传正则
    // ==========================================
    auto confirmValidator = [=]() -> bool
    {
        // 检查空
        if (ui->confirm_edit->text().isEmpty())
        {
            TipWidget::showTip(ui->confirm_edit, "请确认密码");
            return false;
        }

        // 检查一致性
        if (ui->confirm_edit->text() != ui->password_edit->text()) {
            TipWidget::showTip(ui->confirm_edit, "两次密码输入不一致");
            return false;
        }

        return true;
    };
    _validators.append(confirmValidator);
    connect(ui->confirm_edit, &QLineEdit::editingFinished, confirmValidator);
}

