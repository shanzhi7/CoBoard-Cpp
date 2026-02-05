#include "canvas.h"
#include "NewRoomDialog.h"
#include "ui_canvas.h"
#include "usermgr.h"
#include "tcpmgr.h"
#include "tipwidget.h"
#include <QMouseEvent>
#include <QApplication>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>

Canvas::Canvas(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::Canvas)
{
    ui->setupUi(this);

    initCanvasUi();

    // 为整个程序安装事件过滤器
    qApp->installEventFilter(this);

    //连接创建房间成功信号
    //connect(TcpMgr::getInstance().get(),&TcpMgr::sig_creat_room_finish,this,&Canvas::slot_creat_room_finish);

}

Canvas::~Canvas()
{
    qApp->removeEventFilter(this);  // 移除事件过滤器
    delete ui;
}

void Canvas::setRoomInfo(std::shared_ptr<RoomInfo> room_info)
{
    this->_room_info = room_info;
}

bool Canvas::eventFilter(QObject *watched, QEvent *event)
{
    // 只关心鼠标按下
    if (event->type() == QEvent::MouseButtonPress)
    {

        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        QPoint globalPos = mouseEvent->globalPos();

        // 计算 TreeWidget 的区域
        QRect treeRect(ui->treeWidget->mapToGlobal(QPoint(0, 0)), ui->treeWidget->size());

        // 判断点击位置

        if (treeRect.contains(globalPos))   //点击在treeWidget内部
        {
            QPoint localPos = ui->treeWidget->mapFromGlobal(globalPos);
            QTreeWidgetItem *item = ui->treeWidget->itemAt(localPos);

            // 点了内部空白 -> 清除选中
            if (item == nullptr)
            {
                ui->treeWidget->clearSelection();
                return true; // 拦截，因为处理了空白点击，不需要默认处理了
            }

            // 点了已选中的 Item -> 反选
            if (item->isSelected())
            {
                item->setSelected(false);
                return true; // 拦截！防止默认行为又把它选上
            }

            return false; // 放行，让 QTreeWidget 默认逻辑去选中它
        }
        else
        {
            ui->treeWidget->clearSelection();
            return false;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void Canvas::initCanvasUi()
{
    ui->tool_dock->setWindowTitle("工具栏");
    ui->chat_dock->setWindowTitle("聊天室");
    ui->user_dock->setWindowTitle("在线用户");

    //初始化 paintScene(begin)
    _paintScene = new PaintScene(this);
    _paintScene->setSceneRect(0, 0, 5000, 5000);        //大小
    //_paintScene->setBackgroundBrush(Qt::white);         //背景白色
    ui->graphicsView->setScene(_paintScene);            //为view设置舞台
    ui->graphicsView->setRenderHint(QPainter::Antialiasing);    //设置渲染质量，让线条抗锯齿（更平滑，不带狗牙）
    ui->graphicsView->ensureVisible(0, 0, 10, 10);              // 强制把镜头聚焦在画板的左上角 (0,0),保证 (0,0) 这个点附近的区域是可见的
    //初始化 paintScene(end)


    this->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);    //强制所有DockWidget标签页显示在顶部

    //使用空widget替换tool_dock的标题栏
    QWidget* emptyTitleTool = new QWidget();
    ui->tool_dock->setTitleBarWidget(emptyTitleTool);

    this->tabifyDockWidget(ui->chat_dock,ui->user_dock);                    // 两个dock叠在一起

    //菜单栏
    ui->input_img->setIcon(style()->standardIcon(QStyle::SP_FileIcon));     // 设置action图标
    ui->menubar->setVisible(false);                                         //将菜单栏设置为不可见
    ui->file_btn->setMenu(ui->menu_F); // 直接把原来的菜单对象赋给按钮！

    //状态栏
    QStatusBar *bar = this->statusBar();    //获取状态栏
    // 左侧：坐标信息 (新建一个 Label)
    QLabel *posLabel = new QLabel("X: 0, Y: 0", this);
    posLabel->setStyleSheet("color: #666; font-size: 12px; padding-left: 10px;");
    bar->addWidget(posLabel); // addWidget 加在左边

    // 中间/右侧：缩放信息
    QLabel *zoomLabel = new QLabel("缩放：100%", this);
    zoomLabel->setStyleSheet("color: #333; font-weight: bold; font-size: 12px;");
    bar->addPermanentWidget(zoomLabel); // addPermanentWidget 加在最右边

    // 右侧：连接状态
    statusDot = new QLabel("● 未连接", this);
    statusDot->setStyleSheet("color: #ff4d4d; font-size: 12px; padding-right: 10px;"); // 红色圆点
    bar->addPermanentWidget(statusDot);
}

void Canvas::addUser(int uid, QString name, QString avatar_url) //添加用户
{
    QTreeWidgetItem* new_item = ui->treeWidget->addUser(uid,name,avatar_url);
    _userItemMap.insert(uid,new_item);
}


void Canvas::slot_creat_room_finish(std::shared_ptr<RoomInfo> room_info)
{
    TipWidget::showTip(ui->graphicsView,"创建房间成功");
    QString room_name = room_info->name;
    QString room_id = room_info->id;
    ui->title_label->setText(room_name + "-房间号:" + room_id);
    statusDot->setText("● 已连接");
    statusDot->setStyleSheet("color: #2ecc71; font-size: 12px; padding-right: 10px;"); // 绿色圆点
    std::shared_ptr<const UserInfo> my_info = UserMgr::getInstance()->getMyInfo();
    addUser(my_info->_id,my_info->_name + "(房主)",my_info->_avatar);                          // 添加用户
}

