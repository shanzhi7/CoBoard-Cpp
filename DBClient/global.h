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

enum class ReqId{
    ID_GET_VERIFY_CODE = 1001,                  //获取验证码
    ID_REGISTER = 1002,                         //注册账号
    ID_RESET_PWD = 1003,                        //重置密码
    ID_LOGIN = 1004,                            //dengl
};

enum ErrorCodes{
    SUCCESS = 0,        //成功
    ERR_JSON = 1,       //json解析失败
    ERR_NETWORK = 2,    //网络问题
};

enum Modules{
    MOD_REGISTER = 0,   //注册模块
    MOD_RESET = 1,      //重置密码模块
    MOD_LOGIN = 2       //登录模块
};

extern QString gate_url_prefix;


#endif // GLOBAL_H
