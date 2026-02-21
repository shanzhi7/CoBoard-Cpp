#ifndef PAINTSCENE_H
#define PAINTSCENE_H

#include <QGraphicsScene>
#include <QObject>
#include <QPainterPath>
#include <QPointF>
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

    //获取
    QColor getPenColor();
    int getPenWidth();

    void hideEraserCursor();        //供外部用来隐藏橡皮擦

    void applyRemoteDraw(const message::DrawReq& req);  //应用远端绘画，收到广播后调用

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

    // 小于这个距离的移动将被忽略，防止抖动和节省流量
    const qreal MIN_DIST_SQ = 4.0;


    //辅助函数
    void addPointToPath(const QPointF &pos);    // 添加点到路径
    void initNewItem(const QPointF& pos);       // 初始化图元通用逻辑

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
