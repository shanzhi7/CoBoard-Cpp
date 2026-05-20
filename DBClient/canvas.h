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
#include <QTimer>
#include <QHash>
#include <QVector>
#include <QQueue>

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
    void enterOfflineMode();                                                    //进入离线画板模式
    void resetForReconnect();                                                   //断线回大厅时调用
protected:
    virtual bool eventFilter(QObject* watched,QEvent* event) override;          //事件过滤器

signals:
    void sig_return_lobby();                                                    //返回大厅信号

public slots:
    void slot_creat_room_finish(std::shared_ptr<RoomInfo>);                     //创建房间完成槽函数
    void slot_join_room_finish(std::shared_ptr<RoomInfo>);                      //加入房间完成槽函数
private slots:
    void slot_user_joined(UserInfo new_info);                                   //加入新用户槽函数 (广播)
    void slot_user_leaved(int uid);                                             //用户离开槽函数    (广播)
    void slot_permission_changed(int target_uid, bool can_edit);                //房间编辑权限变更

    void on_color_tool_clicked();                                               // color_tool槽函数，选择画笔颜色
    void on_width_tool_clicked();                                               // width_tool槽函数，选择画笔粗细

    // --收到paintSence发送的绘画信号对应的槽函数--
    void slot_onStrokeStart(QString uuid, int type, QPointF startPos, QColor color, int width);
    void slot_onStrokeMove(QString uuid, int type, QPointF currentPos);
    void slot_onStrokeEnd(QString uuid, int type, QPointF endPos);

    void slot_onDrawBroadcast(QByteArray data);   // 收到服务器广播

    //接收消息处理函数
    void slot_onChatReceived(int uid, const QString& name,
                            const QString& avatarUrl,
                            const QString& roomId,
                            const QString& content,
                             qulonglong ts);
    void slot_onSendChatClicked();               //发送消息按钮槽函数

    void on_return_btn_clicked();                //返回大厅槽函数

private:
    Ui::Canvas *ui;
    QLabel *statusDot;                              // 状态栏标签
    PaintScene* _paintScene;                        // Scene
    WidthPopup* _widthPopup;                        // 画笔粗细预览窗口
    std::shared_ptr<RoomInfo> _room_info;
    QMap<int,QTreeWidgetItem*> _userItemMap;        // 用户列表

    QButtonGroup* _toolGroup;                       // toolbtn按钮组

    // ====== Pen/Eraser MOVE 节流缓存 ======
    struct PendingStrokePoints {
        int type = 0;                  // ShapeType
        QVector<QPointF> points;       // 待发送的增量点
        bool active = false;
    };

    QTimer* _strokeFlushTimer = nullptr;    // 定时器
    QHash<QString, PendingStrokePoints> _pendingPointsByUuid;   // 存储uuid对应的 PendingStrokePoints 待处理点

    void flushStrokePoints(const QString& uuid, bool force); // force=true: 立即把剩余点发出去

    // ====== 远端绘画接收缓冲 ======
    // 公网环境下，TCP 包可能不是均匀到达，而是“停一下、来一批”。
    // 如果收到一包就立刻 setPath/setRect，接收端画面会出现一段一段跳动。
    // 这里先把远端 DrawReq 放入队列，再用固定间隔批量应用，用少量额外显示延迟换取更稳定的视觉刷新。
    QTimer* _remoteDrawTimer = nullptr;          // 固定刷新远端绘画的定时器
    QQueue<message::DrawReq> _remoteDrawQueue;  // 等待应用到 PaintScene 的远端绘画包
    void flushRemoteDrawQueue();                // 按固定节奏应用远端绘画包

    // ===== 延迟测量 =====
    QList<qint64> _latencySamples;   // 延迟采样值
    qint64 _latencySum = 0;          // 延迟总和
    int _latencyCount = 0;           // 采样计数

    void initCanvasUi();        //初始化ui界面
    void initToolBtn();         //初始化tool按钮
    void applyRoomCanvasSize();                                           // 按房间信息应用画布尺寸
    void initMemberContextMenu();                                          // 初始化成员列表右键菜单
    void showMemberContextMenu(const QPoint& pos);                         // 显示房主授权菜单
    void addUser(int uid,QString name,QString avatar_url);                      // 添加用户
    void leaveUser(int uid);                                                    // 删除用户
    void refreshRoomCollaborationState();                                       // 刷新房间协作状态
    QString formatMemberDisplayName(const UserInfo& info) const;                // 格式化成员显示名
};

#endif // CANVAS_H
