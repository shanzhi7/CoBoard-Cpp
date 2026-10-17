#include "canvastoolbutton.h"

#include <QPainter>
#include <QPaintEvent>

CanvasToolButton::CanvasToolButton(QWidget* parent)
    : QToolButton(parent)
{
    // 1. 保持画布焦点可连续使用快捷键，并为图标和角标预留独立空间。
    setFocusPolicy(Qt::NoFocus);
    setIconSize(QSize(20, 20));
    setMinimumSize(40, 40);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
}

void CanvasToolButton::SetShortcutKey(QChar shortcut_key)
{
    // 1. 角标仅作为提示，不使用按钮全窗口 shortcut 属性抢占文字输入。
    _shortcut_key = shortcut_key.toUpper();
    update();
}

QChar CanvasToolButton::ShortcutKey() const
{
    // 1. 返回显示状态，工具映射的唯一来源仍然是注册表。
    return _shortcut_key;
}

QSize CanvasToolButton::sizeHint() const
{
    // 1. 沿用样式计算结果，同时避免小尺寸主题压缩角标区域。
    return QToolButton::sizeHint().expandedTo(QSize(40, 40));
}

void CanvasToolButton::paintEvent(QPaintEvent* event)
{
    // 1. 背景、悬停和选中状态继续由现有样式表及 Qt 绘制。
    QToolButton::paintEvent(event);
    if (_shortcut_key.isNull())
    {
        return;
    }

    // 2. 使用逻辑像素绘制角标，高清缩放由 QWidget 的设备比例自动处理。
    QPainter painter(this);
    QFont badge_font = font();
    badge_font.setPixelSize(9);
    badge_font.setBold(false);
    painter.setFont(badge_font);
    painter.setPen(isEnabled() ? (isChecked() ? QColor(0, 120, 215) : QColor(100, 116, 139))
                               : palette().color(QPalette::Disabled, QPalette::ButtonText));
    painter.drawText(rect().adjusted(0, 0, -7, -5), Qt::AlignRight | Qt::AlignBottom, QString(_shortcut_key));
}
