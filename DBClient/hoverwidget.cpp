#include "hoverwidget.h"
#include "global.h"
#include <QGraphicsColorizeEffect>
#include <QStyleOption>
#include <QPainter>
#include <QPropertyAnimation>
#include <QMouseEvent>

HoverWidget::HoverWidget(QWidget *parent)
    : QWidget(parent),_normal(""),_hover(""),_press("")
{
    setState("normal","hover","press");
}

void HoverWidget::setState(QString normal, QString hover, QString press)
{
    _normal = normal;
    _hover = hover;
    _press = press;

    setProperty("state",_normal);
    repolish(this);
}


void HoverWidget::enterEvent(QEnterEvent *event)
{
    //设置属性为悬浮
    setProperty("state", _hover);

    repolish(this);
    QWidget::enterEvent(event);
}

void HoverWidget::leaveEvent(QEvent *event)
{
    //设置属性为默认
    setProperty("state", _normal);

    repolish(this);

    QWidget::leaveEvent(event);
}

void HoverWidget::mousePressEvent(QMouseEvent *event)
{
    // 只响应左键
    if (event->button() == Qt::LeftButton)
    {
        //setProperty("state", _press); // 切换为按下样式
        style()->unpolish(this);
        style()->polish(this);
    }

    QWidget::mousePressEvent(event);
}

void HoverWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        //点击窗口发送点击信号
        if (rect().contains(event->pos()))
        {
            emit clicked(); // 发送信号！

            // 松开后，如果鼠标还在控件里，应该变回 hover 状态
            setProperty("state", _hover);
        }
        else
        {
            // 如果鼠标移出去了才松开，变回 normal 状态
            setProperty("state", _normal);
        }

        style()->unpolish(this);
        style()->polish(this);
    }

    QWidget::mouseReleaseEvent(event);
}

void HoverWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    //开启抗锯齿
    painter.setRenderHint(QPainter::Antialiasing);  //开启抗锯齿
    //让样式表以及子控件的样式表生效
    QStyleOption opt;
    opt.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &painter, this);
}
