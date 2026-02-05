#ifndef USERLISTTREE_H
#define USERLISTTREE_H

#include "usermgr.h"
#include <QObject>
#include <QTreeWidget>

class UserListTree : public QTreeWidget
{
public:
    explicit UserListTree(QWidget* parent = nullptr);
    QTreeWidgetItem* addUser(int uid,QString name,QString avatar_url);  //添加用户item
protected:
};

#endif // USERLISTTREE_H
