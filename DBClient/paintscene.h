#ifndef PAINTSCENE_H
#define PAINTSCENE_H

#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QObject>
#include <QPainterPath>
#include <QPointF>
#include <QHash>
#include <QStack>
#include "global.h"
#include "message.pb.h"

class PaintScene : public QGraphicsScene
{
    Q_OBJECT
public:
    explicit PaintScene(QObject *parent = nullptr);     //构造函数

    //设置画笔颜色和粗细的接口，给外部ui调用
    void setPenColor(const QColor& color);
    void setPenWidth(int width);
    void setShapeType(ShapeType type);      //设置当前工具
    void setEditable(bool editable);        //设置权限
    bool isEditable() const;                //是否有编辑权限

    //获取
    QColor getPenColor();
    int getPenWidth();

    void hideEraserCursor();        //供外部用来隐藏橡皮擦

    void applyRemoteDraw(const message::DrawReq& req);  //应用远端绘画，收到广播后调用

    void resetScene();              //清空所有图元 + 远端缓存

    bool canUndoLocal() const;      //是否存在可撤销的本地图元
    void undoLastLocalItem();       //撤销最后一个本地图元

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    // --- 当前绘画状态 ---
    ShapeType _currShapeType;     //当前工具类型
    QPointF _lastPoint;           // 上一个点 (用于距离检测)
    QString _currUuid;            // 当前笔画的唯一ID
    QPointF _startPos;            //起始点

    // -- 图元指针 ---
    QGraphicsPathItem* _currPathItem;           // 画笔
    QGraphicsRectItem* _currRectItem;           // 矩形
    QGraphicsEllipseItem* _currOvalItem;        // 椭圆
    QGraphicsLineItem* _currLineItem;           // 直线
    QGraphicsEllipseItem* _eraserCursorItem;    // 橡皮擦的光标圆圈 (仅显示)

    QPainterPath _currPath; // 路径数据 (Pen/Eraser 用)

    // --- 画笔配置 ---
    QColor _penColor;
    int _penWidth;
    bool _editable = true;       // 是否允许本地鼠标绘制

    // 小于这个距离的移动将被忽略，防止抖动和节省流量
    const qreal MIN_DIST_SQ = 4.0;


    //辅助函数
    void addPointToPath(const QPointF &pos);    // 添加点到路径
    void initNewItem(const QPointF& pos);       // 初始化图元通用逻辑
    void recordFinishedLocalItem(const QString& itemId, int shape, QGraphicsItem* item); //记录完成的本地图元

    // 橡皮擦位置更新逻辑
    void updateEraserCursor(const QPointF& pos);

    // --远端图元管理--
    struct RemoteItem {
        int shape = 0;
        QPointF start;
        QColor color;
        int width = 1;

        QGraphicsPathItem* pathItem = nullptr;
        QPainterPath path;

        QGraphicsRectItem* rectItem = nullptr;
        QGraphicsEllipseItem* ovalItem = nullptr;
        QGraphicsLineItem* lineItem = nullptr;
    };
    QHash<QString, RemoteItem> _remoteItems;

    // 本地图元记录使用 itemId 作为索引，后续联机撤销也可以复用这套映射关系。
    struct DrawItemRecord {
        QString itemId;
        int shape = 0;
        QGraphicsItem* item = nullptr;
    };
    QStack<DrawItemRecord> _localUndoStack;         //本地可撤销图元栈
    QHash<QString, QGraphicsItem*> _localItems;     //哈希表，uuid-item

signals:
    // --- 网络同步信号 ---
    // start: 发送UUID，颜色，线宽，起点
    void sigStrokeStart(QString uuid,int type,QPointF startPos,QColor color,int width);

    // move: 发送UUID，当前点，接收端接收到后执行lineTo
    void sigStrokeMove(QString uuid,int type,QPointF currentPos);

    // end: 发送UUID，表示这一笔画完了
    void sigStrokeEnd(QString uuid,int type,QPointF endPos);

    // 鼠标位置改变信号
    void sigCursorPosChanged(QPointF pos);

};

#endif // PAINTSCENE_H
