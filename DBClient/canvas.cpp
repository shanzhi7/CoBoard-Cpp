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
#include <QColorDialog>

Canvas::Canvas(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::Canvas)
{
    ui->setupUi(this);

    initCanvasUi();
    initToolBtn();  //初始化toolbtn

    // 为整个程序安装事件过滤器
    qApp->installEventFilter(this);

    //给graphicsView安装事件过滤器，当Canvas能够拦截graphicsView的事件

    //连接新用户加入房间信号槽函数
    connect(TcpMgr::getInstance().get(),&TcpMgr::sig_user_joined,this,&Canvas::slot_user_joined);

    //连接新用户离开房间信号槽函数
    connect(TcpMgr::getInstance().get(),&TcpMgr::sig_user_left,this,&Canvas::slot_user_leaved);

    //开启鼠标追踪，鼠标不点击也把事件传给scene
    ui->graphicsView->setMouseTracking(true);
    ui->graphicsView->viewport()->setMouseTracking(true);

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

    // 判断是不是 graphicsView 发出的事件
    if (watched == ui->graphicsView)
    {
        // 判断是不是 "鼠标离开" 事件
        if (event->type() == QEvent::Leave)
        {
            // 通知 PaintScene 隐藏光标
            if (_paintScene)
            {
                _paintScene->hideEraserCursor();
            }
        }
    }
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
    _paintScene->setBackgroundBrush(Qt::white);         //背景白色
    ui->graphicsView->setScene(_paintScene);            //为view设置舞台
    ui->graphicsView->setRenderHint(QPainter::Antialiasing);    //设置渲染质量，让线条抗锯齿（更平滑，不带狗牙）
    ui->graphicsView->ensureVisible(0, 0, 10, 10);              // 强制把镜头聚焦在画板的左上角 (0,0),保证 (0,0) 这个点附近的区域是可见的
    //初始化 paintScene(end)

    //初始化 _widthPopup(begin)
    _widthPopup = new WidthPopup(this);
    connect(_widthPopup,&WidthPopup::sigLineWidthChanged,this,[=](int width){   // 预览窗口画笔 width 同步到画笔
        _paintScene->setPenWidth(width);
    });
    //初始化 _widthPopup(end)


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
    posLabel->setMinimumWidth(150);
    bar->addWidget(posLabel); // addWidget 加在左边

    connect(_paintScene,&PaintScene::sigCursorPosChanged,this,[=](QPointF pos){
        // 更新 Label 文本
        // toPoint() 把浮点数转成整数，显示更好看
        int x = static_cast<int>(pos.x());
        int y = static_cast<int>(pos.y());
        posLabel->setText(QString("X: %1, Y: %2").arg(x).arg(y));
    });

    // 中间/右侧：缩放信息
    QLabel *zoomLabel = new QLabel("缩放：100%", this);
    zoomLabel->setStyleSheet("color: #333; font-weight: bold; font-size: 12px;");
    bar->addPermanentWidget(zoomLabel); // addPermanentWidget 加在最右边

    // 右侧：连接状态
    statusDot = new QLabel("● 未连接", this);
    statusDot->setStyleSheet("color: #ff4d4d; font-size: 12px; padding-right: 10px;"); // 红色圆点
    bar->addPermanentWidget(statusDot);
}

void Canvas::initToolBtn()
{
    _toolGroup = new QButtonGroup(this);

    //设置互斥
    _toolGroup->setExclusive(true);

    //把按钮加进去，分配ID
    _toolGroup->addButton(ui->pen_tool,Shape_Pen);
    _toolGroup->addButton(ui->eraser_tool,Shape_Eraser);
    _toolGroup->addButton(ui->rect_tool,Shape_Rect);
    _toolGroup->addButton(ui->oval_tool,Shape_Oval);

    //连接信号，拿到ID，直接设置Shape类型
    connect(_toolGroup,&QButtonGroup::idClicked,this,[=](int id){
        _paintScene->setShapeType((ShapeType)id);
    });
    ui->pen_tool->setChecked(true);

    QPixmap originMap(":/res/pen.png");
    ui->pen_tool->setIcon(QIcon(originMap));

}

void Canvas::addUser(int uid, QString name, QString avatar_url) //添加用户
{
    QTreeWidgetItem* new_item = ui->treeWidget->addUser(uid,name,avatar_url);
    _userItemMap.insert(uid,new_item);
}
void Canvas::leaveUser(int uid) //移除用户
{
    if(_userItemMap.contains(uid))
    {
        QTreeWidgetItem* item = _userItemMap.take(uid); //从 Map 中移除，并拿到指针 (一步到位)
        delete item;
    }
}


void Canvas::slot_creat_room_finish(std::shared_ptr<RoomInfo> room_info)
{
    TipWidget::showTip(ui->graphicsView,"创建房间成功");
    QString room_name = room_info->name;
    QString room_id = room_info->id;
    ui->title_label->setText(room_name + "-房间号:" + room_id);
    statusDot->setText("● 已连接");
    statusDot->setStyleSheet("color: #2ecc71; font-size: 12px; padding-right: 10px;"); // 绿色圆点

    //添加自己到 treeWidgetItem
    std::shared_ptr<const UserInfo> my_info = UserMgr::getInstance()->getMyInfo();
    addUser(my_info->_id,my_info->_name + "(房主)",my_info->_avatar);                          // 添加用户

}

void Canvas::slot_join_room_finish(std::shared_ptr<RoomInfo> room_info)
{
    TipWidget::showTip(ui->graphicsView,"创建房间成功");
    QString room_name = room_info->name;
    QString room_id = room_info->id;
    ui->title_label->setText(room_name + "-房间号:" + room_id);
    statusDot->setText("● 已连接");
    statusDot->setStyleSheet("color: #2ecc71; font-size: 12px; padding-right: 10px;");  // 绿色圆点

    std::shared_ptr<const UserInfo> my_info = UserMgr::getInstance()->getMyInfo();      // 获取个人信息

    //添加房间内其他用户到 treeWidgetItem
    const QList<UserInfo>& members= room_info->members;
    for(int i = 0;i < members.size();i++)
    {
        std::shared_ptr<UserInfo> member_info = std::make_shared<UserInfo>();
        member_info->_id = members[i]._id;
        member_info->_name = members[i]._name;
        member_info->_avatar = members[i]._avatar;
        if(member_info->_id == my_info->_id)
        {
            addUser(member_info->_id,member_info->_name + " (你)",member_info->_avatar);
            continue;
        }
        else if(member_info->_id == room_info->owner_uid)
        {
            addUser(member_info->_id,member_info->_name + " (房主)",member_info->_avatar);
        }
        else
        {
            addUser(member_info->_id,member_info->_name,member_info->_avatar);
        }
    }
}

void Canvas::slot_user_joined(UserInfo new_info)
{
    // ---------------------------------------------------------
    // 更新数据层 (Model)
    // ---------------------------------------------------------
    if(_room_info)
    {
        //去重
        bool exists = false;
        for(const auto& u : _room_info->members)
        {
            if(u._id == new_info._id)
            {
                exists = true;
                break;
            }
        }
        if(!exists)
        {
            _room_info->members.append(new_info);
        }
    }
    // ---------------------------------------------------------
    // 更新视图层 (View)
    // ---------------------------------------------------------
    addUser(new_info._id,new_info._name,new_info._avatar);  //添加用户
}

void Canvas::slot_user_leaved(int uid)
{
    // ---------------------------------------------------------
    // 更新数据层 (Model)
    // ---------------------------------------------------------
    if (_room_info)
    {
        // 遍历查找并删除
        // QList 在遍历中删除要注意迭代器失效问题，用索引或者 erase 最安全
        for (int i = 0; i < _room_info->members.size(); ++i)
        {
            if (_room_info->members[i]._id == uid)
            {
                _room_info->members.removeAt(i);
                break; // 找到了就删掉并退出，避免继续循环
            }
        }
    }
    // ---------------------------------------------------------
    // 更新视图层 (View & Map)
    // ---------------------------------------------------------
    leaveUser(uid);
}


void Canvas::on_color_tool_clicked()    //color_tool槽函数
{
    //弹出颜色选择框
    QColor color = QColorDialog::getColor(_paintScene->getPenColor(),this,"选择画笔颜色");
    if(color.isValid())
    {
        _paintScene->setPenColor(color);        //设置逻辑层颜色
        QPixmap originMap(":/res/pen.png");     //加载原图
        QPixmap tintedMap = applyColor(originMap,color);        //生成染色后的图标
        ui->pen_tool->setIcon(QIcon(tintedMap));                //设置染色后的图标给按钮
    }
}


void Canvas::on_width_tool_clicked()
{
    // 获取按钮的位置，把弹窗显示在按钮下方
    QPoint pos = ui->width_tool->mapToGlobal(QPoint(0, ui->width_tool->height()));

    // 设置当前画笔粗细值
    _widthPopup->setLineWidth(_paintScene->getPenWidth());

    _widthPopup->move(pos);
    _widthPopup->show();
}



