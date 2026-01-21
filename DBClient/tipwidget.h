/***********************************************************************************
*
* @file         tipwidget.h
* @brief        提示窗口，提供静态函数给外部调用
*
* @author       shanzhi
* @date         2026/01/20
* @history
***********************************************************************************/
#ifndef TIPWIDGET_H
#define TIPWIDGET_H

#include <QWidget>

// 前置声明，减少头文件依赖，加快编译速度
class QLabel;

class TipWidget : public QWidget
{
    Q_OBJECT
public:
    // 构造函数
    explicit TipWidget(QWidget *parent = nullptr);

    /**
     * @brief 静态工具函数，用于在目标控件上方显示气泡提示
     * @param target 需要提示的目标控件 (例如输入框)
     * @param text   提示的文字内容
     */
    static void showTip(QWidget *target, const QString &text);

private:
    QLabel *m_label; // 内部用于显示文字的标签
};

#endif // TIPWIDGET_H
