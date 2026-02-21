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

    // 16ms 节流：批量发送 Pen/Eraser 的 path_points
    _strokeFlushTimer = new QTimer(this);
    _strokeFlushTimer->setInterval(16);

    connect(_strokeFlushTimer, &QTimer::timeout, this, [this]() {

        // 遍历所有正在画的 uuid，把点批量发出去
        for (auto it = _pendingPointsByUuid.begin(); it != _pendingPointsByUuid.end(); ++it)
        {
            PendingStrokePoints& pendingStroke = it.value();

            // 不在绘制中，跳过
            if (!pendingStroke.active) continue;

            // 没有新增点，跳过（避免无意义的 flush 调用）
            if (pendingStroke.points.isEmpty()) continue;

            flushStrokePoints(it.key(), false);
        }
    });

    _strokeFlushTimer->start();


    initCanvasUi();
    initToolBtn();  //初始化toolbtn

    // 为整个程序安装事件过滤器
    qApp->installEventFilter(this);

    //给graphicsView安装事件过滤器，当Canvas能够拦截graphicsView的事件

    //连接新用户加入房间信号槽函数
    connect(TcpMgr::getInstance().get(),&TcpMgr::sig_user_joined,this,&Canvas::slot_user_joined);

    //连接新用户离开房间信号槽函数
    connect(TcpMgr::getInstance().get(),&TcpMgr::sig_user_left,this,&Canvas::slot_user_leaved);

    // PaintScene -> Canvas (发送)
    connect(_paintScene, &PaintScene::sigStrokeStart, this, &Canvas::slot_onStrokeStart);
    connect(_paintScene, &PaintScene::sigStrokeMove,  this, &Canvas::slot_onStrokeMove);
    connect(_paintScene, &PaintScene::sigStrokeEnd,   this, &Canvas::slot_onStrokeEnd);

    // TcpMgr -> Canvas (接收广播)
    connect(TcpMgr::getInstance().get(), &TcpMgr::sig_draw_broadcast,
            this, &Canvas::slot_onDrawBroadcast);

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

static int32_t ToArgbInt(const QColor& c)   //Qt颜色转 int
{
    // Qt 的 rgba() 是 0xAARRGGBB
    return static_cast<int32_t>(c.rgba());
}

//画笔开始槽函数
void Canvas::slot_onStrokeStart(QString uuid, int type, QPointF startPos, QColor color, int width)
{
    if (!_room_info) return;

    // 只有 Pen/Eraser 才需要缓存点并走 16ms 批量发送
    const bool isPenLike = (type == Shape_Pen || type == Shape_Eraser ||
                            type == message::SHAPE_PEN || type == message::SHAPE_ERASER);

    if (isPenLike)
    {
        PendingStrokePoints& pendingStroke = _pendingPointsByUuid[uuid];
        pendingStroke.type = type;
        pendingStroke.points.clear();
        pendingStroke.active = true;
    }

    // ---------- 发送 START ----------
    message::DrawReq req;
    req.set_uid(UserMgr::getInstance()->getMyInfo()->_id);
    req.set_item_id(uuid.toStdString());
    req.set_cmd(message::CMD_START);
    req.set_shape(static_cast<message::ShapeType>(type));
    req.set_color(ToArgbInt(color));
    req.set_width(width);

    // START 必须带起点，远端才能 moveTo 正确位置
    req.set_start_x(startPos.x());
    req.set_start_y(startPos.y());
    req.set_current_x(startPos.x());
    req.set_current_y(startPos.y());

    std::string binaryData;
    if (!req.SerializeToString(&binaryData)) return;
    qDebug() << "[Draw] cmd=" << req.cmd()
             << " shape=" << req.shape()
             << " uid=" << req.uid()
             << " uuid=" << uuid
             << " bytes=" << (int)binaryData.size();

    TcpMgr::getInstance()->slot_send_data(ReqId::ID_DRAW_REQ, QByteArray::fromStdString(binaryData));
}

//画笔移动槽函数，Pen/Eraser “只缓存点”，几何图形仍然直接发
void Canvas::slot_onStrokeMove(QString uuid, int type, QPointF currentPos)
{
    if (!_room_info) return;

    const bool isPenLike = (type == Shape_Pen || type == Shape_Eraser ||
                            type == message::SHAPE_PEN || type == message::SHAPE_ERASER);

    // Pen/Eraser：只缓存点，不立刻发包
    if (isPenLike)
    {
        // 正常流程下 START 会创建 entry；这里再保证一下健壮性（防止乱序/极端情况）
        PendingStrokePoints& pendingStroke = _pendingPointsByUuid[uuid];
        pendingStroke.type = type;
        pendingStroke.active = true;
        pendingStroke.points.push_back(currentPos);
        return;
    }

    // 几何图形：直接发 MOVE（数据小，实时预览重要）
    message::DrawReq req;
    req.set_uid(UserMgr::getInstance()->getMyInfo()->_id);
    req.set_item_id(uuid.toStdString());
    req.set_cmd(message::CMD_MOVE);
    req.set_shape(static_cast<message::ShapeType>(type));
    req.set_current_x(currentPos.x());
    req.set_current_y(currentPos.y());

    std::string binaryData;
    if (!req.SerializeToString(&binaryData))
    {
        qDebug() << "[Draw] Serialize failed!";
        return;
    }

    // ⭐ 加在这里（发送之前）
    message::DrawReq selfCheck;
    if (!selfCheck.ParseFromString(binaryData))
    {
        qDebug() << "[Draw] Serialize self-check failed! bytes=" << binaryData.size();
    }
    else
    {
        qDebug() << "[Draw START] bytes=" << binaryData.size()
        << " uid=" << selfCheck.uid()
        << " cmd=" << selfCheck.cmd();
    }
    qDebug() << "[Draw] cmd=" << req.cmd()
             << " shape=" << req.shape()
             << " uid=" << req.uid()
             << " uuid=" << uuid
             << " bytes=" << (int)binaryData.size();

    TcpMgr::getInstance()->slot_send_data(ReqId::ID_DRAW_REQ, QByteArray::fromStdString(binaryData));
}

//画笔结束槽函数，先 flush 剩余点，再发 END，并把 active 关掉
void Canvas::slot_onStrokeEnd(QString uuid, int type, QPointF endPos)
{
    if (!_room_info) return;

    const bool isPenLike = (type == Shape_Pen || type == Shape_Eraser ||
                            type == message::SHAPE_PEN || type == message::SHAPE_ERASER);

    if (isPenLike)
    {
        // 把最后点塞进缓存，确保不丢
        PendingStrokePoints& pendingStroke = _pendingPointsByUuid[uuid];
        pendingStroke.points.push_back(endPos);

        // 立刻把剩余点打包发送（MOVE）
        flushStrokePoints(uuid, true);

        // 标记结束 + 清理 entry，防止 map 越积越大
        pendingStroke.active = false;
        _pendingPointsByUuid.remove(uuid);
    }

    // 发 END（几何图形/笔画都发，作为结束标志）
    message::DrawReq req;
    req.set_uid(UserMgr::getInstance()->getMyInfo()->_id);
    req.set_item_id(uuid.toStdString());
    req.set_cmd(message::CMD_END);
    req.set_shape(static_cast<message::ShapeType>(type));
    req.set_current_x(endPos.x());
    req.set_current_y(endPos.y());

    std::string binaryData;
    if (!req.SerializeToString(&binaryData)) return;
    qDebug() << "[Draw] cmd=" << req.cmd()
             << " shape=" << req.shape()
             << " uid=" << req.uid()
             << " uuid=" << uuid
             << " bytes=" << (int)binaryData.size();

    TcpMgr::getInstance()->slot_send_data(ReqId::ID_DRAW_REQ, QByteArray::fromStdString(binaryData));
}

//收到绘画广播
void Canvas::slot_onDrawBroadcast(QByteArray data)
{
    message::DrawReq req;
    if (!req.ParseFromArray(data.data(), data.size()))
        return;

    // 正常来说服务器会 exclude sender，所以这里一般不会收到自己
    // 但加个保险也行：
    int myUid = UserMgr::getInstance()->getMyInfo()->_id;
    if (req.uid() == myUid) return;

    _paintScene->applyRemoteDraw(req);  //调用applyRemoteDraw处理
}

void Canvas::flushStrokePoints(const QString& uuid, bool force)
{
    auto iterator = _pendingPointsByUuid.find(uuid);
    if (iterator == _pendingPointsByUuid.end())
        return;

    PendingStrokePoints& pendingStroke = iterator.value();

    // 不在绘制中就不处理
    if (!pendingStroke.active)
        return;

    // 没点就不发（force 也不发空包）
    if (pendingStroke.points.isEmpty())
        return;

    // 【保护】每个网络包最多携带的点数，避免单包过大
    static const int MAX_POINTS_PER_PACKET = 80;

    // 按批次发送：每次取出前 N 个点打包
    while (!pendingStroke.points.isEmpty())
    {
        const int pointsToSend = qMin(MAX_POINTS_PER_PACKET, pendingStroke.points.size());

        message::DrawReq req;
        req.set_uid(UserMgr::getInstance()->getMyInfo()->_id);
        req.set_item_id(uuid.toStdString());
        req.set_cmd(message::CMD_MOVE);
        req.set_shape(static_cast<message::ShapeType>(pendingStroke.type));

        // current 用这一批的最后一个点（方便远端兜底）
        const QPointF& lastPoint = pendingStroke.points[pointsToSend - 1];
        req.set_current_x(lastPoint.x());
        req.set_current_y(lastPoint.y());

        // 批量塞入 path_points
        req.mutable_path_points()->Reserve(pointsToSend);
        for (int i = 0; i < pointsToSend; ++i)
        {
            const QPointF& pt = pendingStroke.points[i];
            auto* protoPoint = req.add_path_points();
            protoPoint->set_x(pt.x());
            protoPoint->set_y(pt.y());
        }

        // 从缓存中移除已经发送的这批点
        pendingStroke.points.erase(pendingStroke.points.begin(),
                                   pendingStroke.points.begin() + pointsToSend);

        // 发送网络包
        std::string binaryData;
        if (!req.SerializeToString(&binaryData))
            return;

        TcpMgr::getInstance()->slot_send_data(ReqId::ID_DRAW_REQ, QByteArray::fromStdString(binaryData));

        // 如果不是强制 flush（force=false），最多发一包就够了，
        // 避免在一次 timer tick 里发送过多包造成“突刺”
        if (!force) // true: (END),false: (16ms time)
            break;
    }
}

