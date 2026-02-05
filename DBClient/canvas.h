#ifndef CANVAS_H
#define CANVAS_H

#include "paintscene.h"
#include "global.h"
#include "userlisttree.h"
#include <QTreeWidgetItem>
#include <QMainWindow>
#include <QLabel>

namespace Ui {
class Canvas;
}

class Canvas : public QMainWindow
{
    Q_OBJECT

public:
    explicit Canvas(QWidget *parent = nullptr);
    ~Canvas();

    void setRoomInfo(std::shared_ptr<RoomInfo> room_info);                      //设置房间信息
protected:
    virtual bool eventFilter(QObject* watched,QEvent* event) override;          //事件过滤器

public slots:
    void slot_creat_room_finish(std::shared_ptr<RoomInfo>);                     //创建房间完成槽函数
private slots:

private:
    Ui::Canvas *ui;
    QLabel *statusDot;           // 状态栏标签
    PaintScene* _paintScene;     // Scene
    std::shared_ptr<RoomInfo> _room_info;
    QMap<int,QTreeWidgetItem*> _userItemMap;        //用户列表

    void initCanvasUi();        //初始化ui界面
    void addUser(int uid,QString name,QString avatar_url);                      //添加童虎
};

#endif // CANVAS_H
