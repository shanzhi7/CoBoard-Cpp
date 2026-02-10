#ifndef CANVAS_H
#define CANVAS_H

#include "paintscene.h"
#include "global.h"
#include "userlisttree.h"
#include "widthpopup.h"
#include <QTreeWidgetItem>
#include <QMainWindow>
#include <QLabel>
#include <QButtonGroup>

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
    void slot_join_room_finish(std::shared_ptr<RoomInfo>);                      //加入房间完成槽函数
private slots:
    void slot_user_joined(UserInfo new_info);                                   //加入新用户槽函数 (广播)
    void slot_user_leaved(int uid);                                            //用户离开槽函数    (广播)

    void on_color_tool_clicked();                                               // color_tool槽函数，选择画笔颜色

    void on_width_tool_clicked();                                               // width_tool槽函数，选择画笔粗细

private:
    Ui::Canvas *ui;
    QLabel *statusDot;                              // 状态栏标签
    PaintScene* _paintScene;                        // Scene
    WidthPopup* _widthPopup;                        // 画笔粗细预览窗口
    std::shared_ptr<RoomInfo> _room_info;
    QMap<int,QTreeWidgetItem*> _userItemMap;        // 用户列表

    QButtonGroup* _toolGroup;                       // toolbtn按钮组

    void initCanvasUi();        //初始化ui界面
    void initToolBtn();         //初始化tool按钮
    void addUser(int uid,QString name,QString avatar_url);                      // 添加用户
    void leaveUser(int uid);                                                    // 删除用户
};

#endif // CANVAS_H
