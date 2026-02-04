/***********************************************************************************
*
* @file         global.h
* @brief        全局公共类
*
* @author       shanzhi
* @date         2026/01/18
* @history
***********************************************************************************/
#ifndef GLOBAL_H
#define GLOBAL_H

#include <QColor>
#include <QPixmap>
#include <QPainter>
#include <functional>
#include <QWidget>
#include <QStyle>
#include <QLineEdit>
struct Bubble{
    double x;           // X坐标
    double y;           // Y坐标
    double radius;      // 半径
    double speed;       // 上升速度
    double h_speed;     // 水平速度
    QColor color;       // 颜色
};

extern std::function<void(QWidget*)> repolish;  //用于刷新qss样式

extern std::function<QPixmap(const QPixmap& src, const QColor& color)> applyColor;   //改变png图标颜色


enum ReqId{
    ID_GET_VERIFY_CODE = 1001,                  //获取验证码
    ID_REGISTER = 1002,                         //注册账号
    ID_RESET_PWD = 1003,                        //重置密码
    ID_LOGIN = 1004,                            //登录请求
    ID_JOIN_ROOM_REQ_DEL = 1005,
    ID_DRAW_REQ_DEL = 1006,
    ID_CANVAS_LOGIN_REQ = 1007,                 //登录到CanvasServer
    ID_CANVAS_LOGIN_RSP = 1008,                 //登录CanvasServer回包
    ID_CREAT_ROOM_REQ = 1009,                   //创建房间请求
    ID_CREAT_ROOM_RSP = 1010,                   //创建房间回包
    ID_JOIN_ROOM_REQ = 1011,                    //加入房间请求
    ID_JOIN_ROOM_RSP = 1012,                    //加入房间回包
    ID_GET_OSS_TOKEN = 1013,                    //获取OSS上传签名
    ID_UPLOAD_IMAGE = 1014,                     //上传图片到OSS
    ID_SAVE_IMAGE = 1015,                       //将图片保存到数据库
};

enum ErrorCodes{
    SUCCESS = 0,        //成功
    ERR_JSON = 1,       //json解析失败
    ERR_NETWORK = 2,    //网络问题
};

enum Modules{
    MOD_REGISTER = 0,   //注册模块
    MOD_RESET = 1,      //重置密码模块
    MOD_LOGIN = 2,       //登录模块
    MOD_LOBBY = 3,       //大厅模块
};

struct ServerInfo{
    QString Host;
    QString Port;
    QString Token;
    int Uid;
};

// 定义房间信息结构体
struct RoomInfo {
    QString id;
    QString name;
    int owner_uid;
    QString host; // CanvasServer IP
    int port;         // CanvasServer Port
    int width = 1920;
    int height = 1080;
};

extern QString gate_url_prefix;


#endif // GLOBAL_H
