#include "tipwidget.h"
#include <QLabel>
#include <QHBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QTimer>
#include <QApplication>
#include <QDebug>
#include <QPointer>
QPointer<TipWidget> TipWidget::s_currentTip = nullptr;
// 构造函数实现
TipWidget::TipWidget(QWidget *parent) : QWidget(parent)
{
    // 1. 提示只展示反馈，不能抢走菜单关闭后画布或文字输入的键盘焦点。
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);

    // 2. 提示内容与布局由窗口拥有，统一保持既有显示样式。
    QHBoxLayout *layout = new QHBoxLayout(this);
    // 留出边距给阴影显示，防止阴影被切掉
    layout->setContentsMargins(10, 10, 10, 10);

    // 初始化标签
    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignCenter);

    // 设置 QSS 样式 (黑底金边风格)
    m_label->setStyleSheet(
        "QLabel {"
        "   background-color: #1e1e1e;"
        "   border: 1px solid #d4b574;"
        "   border-radius: 4px;"
        "   color: #ffffff;"
        "   padding: 8px 15px;"
        "   font-family: 'Microsoft YaHei';"
        "   font-size: 14px;"
        "}"
        );

    // 3. 阴影跟随标签释放，不参与输入或画布交互。
    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(10);              // 阴影模糊半径
    shadow->setColor(QColor(0, 0, 0, 100)); // 阴影颜色 (半透明黑)
    shadow->setOffset(0, 2);                // 阴影偏移 (向下一点)
    m_label->setGraphicsEffect(shadow);

    // 将标签加入布局
    layout->addWidget(m_label);

    // 4. 两秒后自动关闭，避免提示窗口残留遮挡操作区域。
    QTimer::singleShot(2000, this, &TipWidget::close);
}

// 静态函数实现
void TipWidget::showTip(QWidget *target, const QString &text)
{
    if (!target) return;
    //如果旧的气泡还存在，立即关闭它
    if (s_currentTip)
    {
        s_currentTip->close();
    }

    // 创建提示窗口，父对象设为 target 的顶层窗口，防止内存泄漏
    // (虽然有 DeleteOnClose，但指定父对象是好习惯)

    TipWidget *tip = new TipWidget(target->window());

    // 记录为当前气泡
    s_currentTip = tip;

    // 设置文字
    tip->m_label->setText(text);

    // 调整大小以适应文字
    tip->adjustSize();

    // --- 计算显示位置 (核心逻辑)

    // 获取目标控件在屏幕上的绝对坐标
    QPoint globalPos = target->mapToGlobal(QPoint(0, 0));

    // 计算 X 轴：居中对齐
    // 目标左边 + (目标宽度 - 气泡宽度) / 2
    int x = globalPos.x() + (target->width() - tip->width()) / 2;

    // 计算 Y 轴：显示在目标上方
    // 目标顶边 - 气泡高度 + 10 (稍微覆盖一点或者留空隙，这里 +10 是为了抵消上面的 margin)
    // 如果觉得太高，可以减小偏移量，比如 - tip->height()
    int y = globalPos.y() - tip->height() + 10;

    // 移动并显示
    tip->move(x, y);
    tip->show();
}
