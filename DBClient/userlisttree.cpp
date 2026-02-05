#include "userlisttree.h"
#include <QStyleOption>

UserListTree::UserListTree(QWidget* parent)
    :QTreeWidget(parent)
{
    setIconSize(QSize(40, 40));
}

QTreeWidgetItem* UserListTree::addUser(int uid,QString name,QString avatar_url)
{
    QTreeWidgetItem *item = new QTreeWidgetItem(this);
    item->setText(0,name);                      //设置名字
    item->setData(0,Qt::UserRole,uid);          //设置一个uid
    UserMgr::getInstance()->loadAvatar(avatar_url,item);    //加载头像
    return item;
}
