/***********************************************************************************
* @file         canvastoolbutton.h
* @brief        保留 Qt 工具按钮样式并在右下角绘制快捷键标记
* @author       shanzhi
* @date         2026/10/15
* @history
***********************************************************************************/
#pragma once

#include <QToolButton>

class QPaintEvent;

class CanvasToolButton : public QToolButton
{
    Q_OBJECT

public:
    explicit CanvasToolButton(QWidget* parent = nullptr); // 初始化统一尺寸，点击工具不抢走画布键盘焦点。
    void SetShortcutKey(QChar shortcut_key); // 设置右下角显示的字母，不注册全窗口快捷键。
    QChar ShortcutKey() const; // 返回实际显示的快捷键，供无障碍描述和界面检查使用。
    QSize sizeHint() const override; // 保留图标、角标和触摸点击区域所需空间。

protected:
    void paintEvent(QPaintEvent* event) override; // 先绘制 Qt 原生状态，再绘制独立于图标的快捷键标记。

private:
    QChar _shortcut_key; // 当前工具的单字母快捷键标记。
};
