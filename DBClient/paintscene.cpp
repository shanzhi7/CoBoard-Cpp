#include "paintscene.h"
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsPathItem>
#include <QUuid>

PaintScene::PaintScene(QObject *parent)
    : QGraphicsScene{parent}
    , _currShapeType(Shape_Pen) // 默认钢笔
    , _currPathItem(nullptr)
    , _currRectItem(nullptr)
    , _currOvalItem(nullptr)
    , _penColor(Qt::black)
    , _penWidth(3)

{
    // 初始化橡皮擦光标 (begin)
    _eraserCursorItem = new QGraphicsEllipseItem();
    // 样式：黑色细边框
    _eraserCursorItem->setPen(QPen(Qt::black, 1));
    // 样式：极淡的半透明灰色 (方便看清范围)
    _eraserCursorItem->setBrush(QBrush(QColor(200, 200, 200, 50)));

    // 层级：保证永远浮在所有画作上面
    _eraserCursorItem->setZValue(9999);

    // 关键：不接受鼠标点击，让鼠标事件能穿透它传给底下的图元
    _eraserCursorItem->setAcceptedMouseButtons(Qt::NoButton);
    _eraserCursorItem->setAcceptHoverEvents(false);

    // 默认隐藏
    _eraserCursorItem->hide();

    // 添加到场景
    addItem(_eraserCursorItem);
    // 初始化橡皮擦光标 (end)
}
void PaintScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    // 只允许左键绘画
    if (event->button() != Qt::LeftButton)
    {
        // 如果是右键，可以把事件传给父类处理，或者直接 return
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    _currUuid = QUuid::createUuid().toString();
    _startPos = event->scenePos(); // 记录绝对起点(用于矩形/圆)
    _lastPoint = _startPos;        // 记录上一笔点(用于画笔)

    // 根据当前类型创建不同的 Item
    switch (_currShapeType)
    {
        case Shape_Pen:
        case Shape_Eraser:
        {
            _currPath = QPainterPath();
            _currPath.moveTo(_startPos);

            _currPathItem = new QGraphicsPathItem();

            // 橡皮擦逻辑：颜色白色，宽度稍大
            QColor color = (_currShapeType == Shape_Eraser) ? Qt::white : _penColor;
            int width = (_currShapeType == Shape_Eraser) ? (_penWidth * 3) : _penWidth;

            QPen pen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin); //设置圆角笔触，拐弯更圆润
            _currPathItem->setPen(pen);
            _currPathItem->setPath(_currPath);

            // 橡皮擦需要设置 ZValue 高一点，或者设置组合模式(但在 Scene 里直接覆盖白色最简单)
            //if (_currShapeType == Shape_Eraser) _currPathItem->setZValue(100);

            addItem(_currPathItem);
            break;
        }
        case Shape_Rect:    //矩形模式
        {
            _currRectItem = new QGraphicsRectItem();
            QPen pen(_penColor, _penWidth, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
            _currRectItem->setPen(pen);
            // 初始矩形大小为 0
            _currRectItem->setRect(QRectF(_startPos, _startPos));
            addItem(_currRectItem);
            break;
        }
        case Shape_Oval:    //椭圆模式
        {
            _currOvalItem = new QGraphicsEllipseItem();
            QPen pen(_penColor, _penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            _currOvalItem->setPen(pen);
            _currOvalItem->setRect(QRectF(_startPos, _startPos));
            addItem(_currOvalItem);
            break;
        }
        default:
            break;
    }

    // 发送开始信号
    // emit sigStrokeStart(_currUuid, _currShapeType, _startPos, _penColor, _penWidth);
}

void PaintScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    emit sigCursorPosChanged(event->scenePos());     // 发送鼠标坐标信号

    if (_currShapeType == Shape_Eraser)//如果是橡皮擦模式，更新光标位置
    {
        updateEraserCursor(event->scenePos());
    }

    // 1. 【关键】必须按住左键移动才算数
    if (!(event->buttons() & Qt::LeftButton)) return;

    QPointF currPos = event->scenePos();

    switch (_currShapeType)
    {
        case Shape_Pen:
        case Shape_Eraser:
        {
            if (!_currPathItem) return;

            // 距离检测 (防抖)
            qreal dx = currPos.x() - _lastPoint.x();
            qreal dy = currPos.y() - _lastPoint.y();
            if ((dx * dx + dy * dy) < MIN_DIST_SQ) return;

            addPointToPath(currPos);
            _lastPoint = currPos;

            // 发送移动信号 (发送的是新增点)
            // emit sigStrokeMove(_currUuid, _currShapeType, currPos);
            break;
        }
        case Shape_Rect:
        {
            if (!_currRectItem) return;
            // 实时更新矩形大小
            // normalized() 非常重要！它支持反向拖拽（从右下往左上画）
            QRectF rect(_startPos, currPos);
            _currRectItem->setRect(rect.normalized());

            // 节流发送移动信号 (发送的是当前鼠标位置，用于预览)
            // emit sigStrokeMove(_currUuid, _currShapeType, currPos);
            break;
        }
        case Shape_Oval:
        {
            if (!_currOvalItem) return;
            QRectF rect(_startPos, currPos);
            _currOvalItem->setRect(rect.normalized());

            // emit sigStrokeMove(_currUuid, _currShapeType, currPos);
            break;
        }
    }
}
void PaintScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;

    switch (_currShapeType)
    {
        case Shape_Pen:
        case Shape_Eraser:
        {
            if (_currPathItem)
            {
                addPointToPath(event->scenePos()); // 补上最后一点
                _currPathItem = nullptr;
            }
            break;
        }

        case Shape_Rect:
        {
            if (_currRectItem)
            {
                // 确保最后形状正确
                QRectF rect(_startPos, event->scenePos());
                _currRectItem->setRect(rect.normalized());
                _currRectItem = nullptr;
            }
            break;
        }

        case Shape_Oval:
        {
            if (_currOvalItem)
            {
                QRectF rect(_startPos, event->scenePos());
                _currOvalItem->setRect(rect.normalized());
                _currOvalItem = nullptr;
            }
            break;
        }
    }

    // emit sigStrokeEnd(_currUuid, _currShapeType);
    _currUuid = "";
}

void PaintScene::addPointToPath(const QPointF &pos)
{
    _currPath.lineTo(pos);
    if (_currPathItem) {
        _currPathItem->setPath(_currPath);
    }
}

void PaintScene::initNewItem(const QPointF &pos)
{

}

void PaintScene::updateEraserCursor(const QPointF &pos)
{
    if(!_eraserCursorItem)
        return;

    int eraserWidth = _penWidth * 3;
    qreal radius = eraserWidth / 2.0;

    // (鼠标中心x - 半径, 鼠标中心y - 半径)
    _eraserCursorItem->setRect(pos.x() - radius, pos.y() - radius, eraserWidth, eraserWidth);

    if (!_eraserCursorItem->isVisible())    //显示
    {
        _eraserCursorItem->show();
    }
}

void PaintScene::setPenColor(const QColor &color)
{
    _penColor = color;
}

void PaintScene::setPenWidth(int width)
{
    _penWidth = width;
}

void PaintScene::setShapeType(ShapeType type)
{
    _currShapeType = type;
    // 控制橡皮擦显隐
    if (_currShapeType == Shape_Eraser)
    {
        _eraserCursorItem->show();
    } else
    {
        _eraserCursorItem->hide();
    }
}

QColor PaintScene::getPenColor()
{
    return _penColor;
}

int PaintScene::getPenWidth()
{
    return _penWidth;
}

void PaintScene::hideEraserCursor()
{
    if (_eraserCursorItem)
    {
        _eraserCursorItem->hide();
    }
}
