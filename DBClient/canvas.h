#ifndef CANVAS_H
#define CANVAS_H

#include "paintscene.h"
#include "drawtool.h"
#include "global.h"
#include "userlisttree.h"
#include "widthpopup.h"
#include "testmode.h"
#include "imageassetmanager.h"
#include <QTreeWidgetItem>
#include <QMainWindow>
#include <QLabel>
#include <QButtonGroup>
#include <QTimer>
#include <QHash>
#include <QVector>
#include <QQueue>
#include <QJsonObject>
#include <QPointF>
#include <QStringList>

class ImageAssetManager;
class QImage;

namespace Ui {
class Canvas;
}

class Canvas : public QMainWindow
{
    Q_OBJECT

public:
    explicit Canvas(const LatencyTestOptions& test_options = LatencyTestOptions(),
                    QWidget *parent = nullptr); // 创建画布窗口和可选测试控制器
    ~Canvas(); // 销毁画布窗口并释放语音与测试资源

    void setRoomInfo(std::shared_ptr<RoomInfo> room_info); // 设置房间信息
    void enterOfflineMode(); // 进入离线画板模式
    void resetForReconnect(); // 断线回大厅时调用
    void resumeVoice(); // 从大厅返回画板时恢复语音音频
protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override; // 事件过滤器

signals:
    void sig_return_lobby(); // 返回大厅信号

public slots:
    void slot_creat_room_finish(std::shared_ptr<RoomInfo>); // 创建房间完成槽函数
    void slot_join_room_finish(std::shared_ptr<RoomInfo>); // 加入房间完成槽函数
private slots:
    void slot_user_joined(UserInfo new_info);                                   //加入新用户槽函数 (广播)
    void slot_user_leaved(int uid);                                             //用户离开槽函数    (广播)
    void slot_permission_changed(int target_uid, bool can_edit);                //房间编辑权限变更

    void on_color_tool_clicked();                                               // color_tool槽函数，选择画笔颜色
    void on_width_tool_clicked();                                               // width_tool槽函数，选择画笔粗细
    void slot_onInputImgTriggered();                                             // 导入图片动作槽函数，离线模式创建本地图元
    void OnImageFilesDropped(QStringList file_paths, QPointF scene_pos); // 处理从资源管理器拖入画板的图片文件。
    void OnPasteImageRequested(); // 处理画布获得焦点后的 Ctrl+V 图片粘贴请求。

    // --收到paintSence发送的绘画信号对应的槽函数--
    void slot_onStrokeStart(QString uuid, int type, QPointF startPos, QColor color, int width);
    void slot_onStrokeMove(QString uuid, int type, QPointF currentPos);
    void slot_onStrokeEnd(QString uuid, int type, QPointF endPos);

    void slot_onDrawBroadcast(QByteArray data);   // 收到服务器广播
    void slot_onImageOperationBroadcast(QByteArray data); // 收到图片图元操作广播并开始资源加载
    void slot_onImageAssetHttpFinished(ReqId reqid, QString response, ErrorCodes error); // 处理图片签名接口回包
    void slot_onLocalAssetReady(QString request_id, QString file_path, QString asset_id, QString asset_ref, QString sha256, QString mime_type, QSize original_size, qint64 byte_size, QPixmap pixmap, QString local_file_path); // 处理后台本地图片校验成功并创建预览图元。
    void slot_onLocalAssetFailed(QString request_id, QString file_path, QString error_message); // 处理后台本地图片读取失败且不创建图元。
    void slot_onImageAssetReady(QString asset_id, QPixmap pixmap, QString sha256, QSize original_size, QString mime_type, QString local_file_path); // 更新图片图元的可显示像素
    void slot_onImageAssetFailed(QString asset_id, QString error_message); // 将资源加载失败转换为图元错误状态
    void slot_onImageAssetUploaded(QString asset_id, QString sha256, QString mime_type); // 上传完成后发送图片元数据操作
    void slot_onImageAssetUploadFailed(QString asset_id, QString error_message); // 上传失败时删除尚未广播的本地图元
    void slot_onImageGeometryChanged(QString item_id, QRectF scene_rect, qreal rotation, qreal scale); // 本地移动图片后发送变换操作
    void slot_onImageDeleteRequested(QString item_id); // 处理本地删除键并发送图片删除操作
    void slot_onImageRetryRequested(QString item_id); // 双击失败占位图后重新排队下载签名请求
    void slot_onImagePreviewRequested(QString item_id); // 双击已加载图片后打开原图预览窗口

    //接收消息处理函数
    void slot_onChatReceived(int uid, const QString& name,
                            const QString& avatarUrl,
                            const QString& roomId,
                            const QString& content,
                             qulonglong ts);
    void slot_onSendChatClicked();               //发送消息按钮槽函数

    void on_return_btn_clicked();                //返回大厅槽函数

private:
    Ui::Canvas* ui; // 界面对象
    QLabel* statusDot; // 状态栏标签
    PaintScene* _paintScene; // 画布场景
    WidthPopup* _widthPopup; // 画笔粗细预览窗口
    std::shared_ptr<RoomInfo> _room_info; // 当前房间信息
    QMap<int, QTreeWidgetItem*> _userItemMap; // 用户列表

    QButtonGroup* _toolGroup; // 工具按钮组

    QLabel* _zoomLabel = nullptr; // 状态栏中的缩放比例标签

    QString _selected_recording_device_id; // 当前高亮的麦克风设备 ID
    QString _selected_playout_device_id; // 当前高亮的扬声器设备 ID

    // ====== Pen/Eraser MOVE 节流缓存 ======
    struct PendingStrokePoints {
        int type = 0;                  // ShapeType
        QVector<QPointF> points;       // 待发送的增量点
        bool active = false;
    };

    QTimer* _strokeFlushTimer = nullptr; // 路径点刷新定时器
    QHash<QString, PendingStrokePoints> _pendingPointsByUuid; // UUID 对应的待处理路径点

    void flushStrokePoints(const QString& uuid, bool force); // force 为 true 时立即发送剩余点

    // ====== 远端绘画接收缓冲 ======
    // 公网环境下，TCP 包可能不是均匀到达，而是“停一下、来一批”。
    // 如果收到一包就立刻 setPath/setRect，接收端画面会出现一段一段跳动。
    // 这里先把远端 DrawReq 放入队列，再用固定间隔批量应用，用少量额外显示延迟换取更稳定的视觉刷新。
    QTimer* _remoteDrawTimer = nullptr; // 固定刷新远端绘画的定时器
    QQueue<message::DrawReq> _remoteDrawQueue; // 等待应用到 PaintScene 的远端绘画包
    void flushRemoteDrawQueue(); // 按固定节奏应用远端绘画包
    QQueue<message::ImageOperation> _remoteImageOperationQueue; // JoinRoomRsp 尚未完成时暂存的图片历史操作

    ImageAssetManager* _imageAssetManager = nullptr; // 图片资源缓存和本地文件校验管理器

    struct PendingImageUpload
    {
        QString item_id; // 本地预览图元的稳定 ID。
        QString file_path; // 待上传的本地图片路径，仅在上传阶段使用。
        QString suffix; // 网关签名需要的扩展名。
        ImageAssetInfo asset_info; // 本地校验后的资源元数据和摘要。
    };
    QHash<QString, PendingImageUpload> _pendingImageUploads; // 等待 GateServer 签名或 OSS PUT 完成的图片上传。
    QQueue<QString> _imageUploadQueue; // 上传签名请求按顺序发送，避免无上下文的 HTTP 回包串线。
    QString _activeImageUploadId; // 当前正在等待签名或 PUT 完成的图元 ID。
    struct PendingImageImport
    {
        QString _source_file_path; // 拖放或文件选择的原始路径，剪贴板图片没有原始路径。
        QPointF _anchor_scene_pos; // 用户触发导入时记录的场景锚点。
        bool _has_anchor_scene_pos = false; // 是否使用已记录的锚点而不是异步完成时的画布中心。
    };
    QHash<QString, PendingImageImport> _pending_image_imports; // 后台本地读取请求上下文，房间切换时清空以丢弃过期结果。

    struct PendingImageDownload
    {
        QString item_id; // 等待资源加载的图片图元 ID。
        QString asset_id; // 网关签名对应的稳定资源 ID。
        QString asset_ref; // 网关签名对应的稳定对象引用。
        QString asset_sha256; // 下载完成后必须匹配的 SHA-256 摘要。
        QString mime_type; // 资源声明的 MIME 类型。
    };
    QQueue<PendingImageDownload> _imageDownloadQueue; // 等待下载签名的远端图片队列。
    PendingImageDownload _activeImageDownload; // 当前正在请求签名或下载的图片上下文。
    bool _imageDownloadRequestActive = false; // 防止多个无上下文的签名请求并发返回后无法关联图元。
    bool _applyingRemoteImageOperation = false; // 应用远端变换时屏蔽本地几何信号，避免广播回环。

    // ===== 延迟测量 =====
    QList<qint64> _latencySamples; // 延迟采样值
    qint64 _latencySum = 0; // 延迟总和
    int _latencyCount = 0; // 采样计数

    void initCanvasUi(); // 初始化 UI 界面
    void initToolBtn(); // 初始化工具按钮
    void refreshCurrentUserProfile(); // 刷新当前用户头像和名称
    void applyRoomCanvasSize(); // 按房间信息应用画布尺寸
    void initMemberContextMenu(); // 初始化成员列表右键菜单
    void showMemberContextMenu(const QPoint& pos); // 显示房主授权菜单
    void addUser(int uid, QString name, QString avatar_url); // 添加用户
    void leaveUser(int uid); // 删除用户
    void refreshRoomCollaborationState(); // 刷新房间协作状态
    QString formatMemberDisplayName(const UserInfo& info) const; // 格式化成员显示名
    void startLatencyTestIfReady(); // 房间就绪后启动延迟测试
    void requestNextImageUploadToken(); // 为队列头图片请求 GateServer PUT 签名
    void requestNextImageDownloadToken(); // 为队列头图片请求 GateServer GET 签名
    void sendImageCreateOperation(const QString& item_id); // 序列化已上传图片的创建操作并发送到 CanvasServer
    void sendImageTransformOperation(const QString& item_id, const QRectF& scene_rect, qreal rotation, qreal scale); // 序列化本地变换并发送到 CanvasServer
    void sendImageDeleteOperation(const QString& item_id); // 序列化图片删除操作并发送到 CanvasServer
    void applyRemoteImageOperation(const message::ImageOperation& operation); // 在本地场景应用服务端广播的图片操作
    void updateImageItemsForAsset(const QString& asset_id, const QPixmap& pixmap, const QSize& original_size); // 同时更新引用同一资源的多个图元
    void StartFileImageImport(const QString& file_path, const QPointF& anchor_scene_pos, bool has_anchor_scene_pos); // 启动本地文件图片的异步校验和导入。
    void StartImageDataImport(const QImage& image, const QPointF& anchor_scene_pos, bool has_anchor_scene_pos); // 启动剪贴板图片的异步编码、校验和导入。
    QPointF CanvasCenterScenePos() const; // 返回当前可视画布中心对应的场景坐标。

    LatencyTestController* _latencyTestController = nullptr; // 延迟测试控制器
};

#endif // CANVAS_H
