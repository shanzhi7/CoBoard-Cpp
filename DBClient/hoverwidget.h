/***********************************************************************************
*
* @file         hoverwidget.h
* @brief        欢迎页面的子widegt
*
* @author       shanzhi
* @date         2026/01/20
* @history
***********************************************************************************/
#ifndef HOVERWIDGET_H
#define HOVERWIDGET_H

#include <QWidget>
#include <QPropertyAnimation>

class HoverWidget : public QWidget
{
    Q_OBJECT

public:
    explicit HoverWidget(QWidget *parent = nullptr);
    void setState(QString normal,QString hover,QString _press);     //初始化成员变量


protected:
    virtual void enterEvent(QEnterEvent* event) override;           //鼠标进入事件
    virtual void leaveEvent(QEvent* event) override;                //鼠标离开事件
    virtual void mousePressEvent(QMouseEvent *event) override;      //鼠标点击事件
    virtual void mouseReleaseEvent(QMouseEvent *event) override;    //鼠标释放事件
    virtual void paintEvent(QPaintEvent *event) override;           //重绘事件


private:

    QString _normal;
    QString _hover;
    QString _press;

signals:
    void clicked();          //点击信号
};

#endif // HOVERWIDGET_H
