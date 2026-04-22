#ifndef WELCOMEWIDGET_H
#define WELCOMEWIDGET_H

#include <QWidget>

namespace Ui {
class WelcomeWidget;
}

class WelcomeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WelcomeWidget(QWidget *parent = nullptr);
    ~WelcomeWidget();

private:
    Ui::WelcomeWidget *ui;

    void initIcons();

signals:
    void switchLogin();         //切换登录窗口信号
    void switchOfflineCanvas(); //切换离线画板信号
};

#endif // WELCOMEWIDGET_H
