/***********************************************************************************
*
* @file         userdata.h
* @brief        用户数据文件,friend,applyfriend,user等
*
* @author       shanzhi
* @date         2026/01/22
* @history
***********************************************************************************/
#ifndef USERDATA_H
#define USERDATA_H

#include <QString>
class UserInfo
{
public:
    UserInfo();
    UserInfo(int id,QString name,QString email,int sex,QString avatar,QString signature)
        :_id(id),_name(name),_email(email),_sex(sex),_avatar(avatar),_signature(signature)
    {}
    int _id;                 //用户唯一id
    QString _name;           //昵称
    QString _email;          //邮箱
    //QString passwd;
    int _sex;                //性别
    QString _avatar;         //头像
    QString _signature;      //个性签名
};

#endif // USERDATA_H
