/***********************************************************************************
* @file         textitem.h
* @brief        纯文本图元、统一样式和编辑草稿生命周期
* @author       shanzhi
* @date         2026/10/08
* @history
***********************************************************************************/
#pragma once

#include "canvasitem.h"

#include <QColor>
#include <QPointF>

class QGraphicsTextItem;
class QGraphicsSceneMouseEvent;

struct TextStyle
{
    QString _font_family = QStringLiteral("Microsoft YaHei"); // 保存请求字体，缺失时使用 Qt 默认字体。
    int _font_size = 24; // 场景像素字号，与视口缩放无关。
    QColor _color = Qt::black; // 整个文字图元统一颜色。
    bool _is_bold = false; // 整个文字图元统一加粗状态。
    bool operator==(const TextStyle& other) const; // 比较可保存的全部样式。
};

struct TextItemData
{
    QString _item_id; // 稳定文字图元标识。
    QString _text; // 带换行的纯文本，不保存 HTML。
    TextStyle _style; // 全文统一样式。
    QPointF _position; // 图元在场景中的左上角位置。
};

namespace TextLimits
{
constexpr int MAX_TEXT_BYTES = 16 * 1024; // 单个文字允许的 UTF-8 字节数。
constexpr int MIN_FONT_SIZE = 8; // 最小场景像素字号。
constexpr int MAX_FONT_SIZE = 144; // 最大场景像素字号。
}

class TextItem : public CanvasItem
{
    Q_OBJECT
public:
    explicit TextItem(const QString& item_id, QGraphicsItem* parent = nullptr); // 创建场景拥有的文字和原生编辑子图元。
    QRectF boundingRect() const override; // 返回随文本排版变化的局部边界。
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override; // 仅绘制本地选择反馈，文本由子图元绘制。
    void BeginEditing(); // 保存编辑前数据并启用输入法和文本撤销。
    bool CommitEditing(); // 校验草稿并结束编辑，超限时保留编辑状态。
    void CancelEditing(); // 恢复编辑前内容和样式，不产生提交。
    bool IsEditing() const; // 返回是否正在编辑草稿。
    TextItemData GetTextData() const; // 读取当前内容、样式和实际位置。
    void SetTextData(const TextItemData& data); // 恢复纯文本和统一样式，不改图元公共变换。
    void SetTextStyle(const TextStyle& style); // 修改全文样式并保留编辑光标和撤销记录。
    static bool IsValidData(const TextItemData& data, bool allow_empty = false); // 校验内容长度、字号、颜色和位置。

signals:
    void SigFinishRequested(bool is_cancel); // 编辑器通过 Ctrl+Enter 或 Esc 请求场景结束编辑。
    void SigEditRequested(QString item_id); // 双击文字后请求场景开始编辑。
    void SigTextError(QString message); // 草稿超限时提供明确提示。

protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override; // 由场景统一管理单个活动编辑器。

private:
    void EndEditing(); // 关闭文本交互并清除输入光标及选区。
    void UpdateTextGeometry(); // 在文档布局变化时更新场景几何索引。
    QGraphicsTextItem* _editor = nullptr; // 父图元拥有的 Qt 原生文字编辑器。
    TextStyle _text_style; // 当前全文样式。
    TextItemData _text_before_edit; // Esc 恢复的编辑前数据。
    QRectF _text_rect; // 场景索引使用的最新文字边界。
    bool _is_editing = false; // 当前是否接受文本编辑事件。
};
Q_DECLARE_METATYPE(TextItemData)
