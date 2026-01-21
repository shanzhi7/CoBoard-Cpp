#include "global.h"
#include "tipwidget.h"
#include <QRegularExpression>
#include <QRegularExpressionValidator>

QString gate_url_prefix = "";

std::function<void(QWidget*)> repolish = [](QWidget *w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);

};


std::function<QPixmap (const QPixmap& src, const QColor &color)> applyColor = [](const QPixmap& src, const QColor &color)
{
    if (src.isNull())
        return src;

    // 复制源图片，作为画布的目标
    QPixmap result = src;

    // QPainter
    QPainter painter(&result);

    // 设置合成模式为 SourceIn
    // 含义:即将在上面画的内容（Source，即颜色）只会显示在
    // 原图（Destination）非透明像素所在的地方。
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);

    // 用新的颜色填充整个图片区域
    painter.fillRect(result.rect(), color);

    // 结束绘制
    painter.end();

    return result;
};

