#include "textitem.h"

#include <cmath>

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsTextItem>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QInputMethod>
#include <QPainter>
#include <QMenu>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextOption>

namespace
{
class TextEditorItem : public QGraphicsTextItem
{
public:
    explicit TextEditorItem(TextItem* owner) : QGraphicsTextItem(owner), _owner(owner) // 保存父图元，编辑器不独立管理场景。
    {
        // 1. 文本布局不自动换行，支持原生输入法。
        document()->setDocumentMargin(0);
        QTextOption option = document()->defaultTextOption();
        option.setWrapMode(QTextOption::NoWrap);
        document()->setDefaultTextOption(option);
        setAcceptDrops(false);
    }
protected:
    void keyPressEvent(QKeyEvent* event) override // 文本快捷键只作用于字符，不进入画布操作。
    {
        // 1. 完成和取消交给场景，普通 Enter 继续交给 Qt 输入法和换行。
        if (event->key() == Qt::Key_Escape ||
            ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
             event->modifiers().testFlag(Qt::ControlModifier) && !_has_preedit))
        {
            emit _owner->SigFinishRequested(event->key() == Qt::Key_Escape);
            event->accept();
            return;
        }
        // 2. 粘贴只读取纯文本，不能把 HTML 样式或图片带入文档。
        if (event->matches(QKeySequence::Paste))
        {
            PastePlainText();
            event->accept();
            return;
        }
        if (event->matches(QKeySequence::Bold) || event->matches(QKeySequence::Italic) || event->matches(QKeySequence::Underline))
        {
            event->accept();
            return;
        }
        // 3. 普通按键、文本撤销及输入法由 Qt 处理；最终提交再次检查完整草稿。
        QGraphicsTextItem::keyPressEvent(event);
    }
    void inputMethodEvent(QInputMethodEvent* event) override // 候选确认保持原生输入法流程。
    {
        // 1. 候选中的 Ctrl+Enter 不能被解释为完成整段文字。
        _has_preedit = !event->preeditString().isEmpty();
        QGraphicsTextItem::inputMethodEvent(event);
    }
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override // 右键粘贴同样遵守纯文本约束。
    {
        // 1. 只在活动编辑器显示文本命令，普通状态由场景处理。
        if (!_owner->IsEditing())
        {
            event->ignore();
            return;
        }
        QMenu menu;
        auto* undo_action = menu.addAction(QStringLiteral("撤销"), this, [this]() { document()->undo(); });
        undo_action->setEnabled(document()->isUndoAvailable());
        auto* redo_action = menu.addAction(QStringLiteral("重做"), this, [this]() { document()->redo(); });
        redo_action->setEnabled(document()->isRedoAvailable());
        menu.addSeparator();
        auto* cut_action = menu.addAction(QStringLiteral("剪切"), this, [this]() {
            QTextCursor cursor = textCursor();
            QApplication::clipboard()->setText(cursor.selectedText().replace(QChar::ParagraphSeparator, '\n'));
            cursor.removeSelectedText();
            setTextCursor(cursor);
        });
        auto* copy_action = menu.addAction(QStringLiteral("复制"), this, [this]() {
            QApplication::clipboard()->setText(textCursor().selectedText().replace(QChar::ParagraphSeparator, '\n'));
        });
        cut_action->setEnabled(textCursor().hasSelection());
        copy_action->setEnabled(textCursor().hasSelection());
        menu.addAction(QStringLiteral("粘贴"), this, [this]() { PastePlainText(); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("全选"), this, [this]() {
            QTextCursor cursor = textCursor();
            cursor.select(QTextCursor::Document);
            setTextCursor(cursor);
        });
        // 2. 菜单返回后编辑仍继续，不把右键命令作为画布提交。
        menu.exec(event->screenPos());
        event->accept();
    }
private:
    void PastePlainText() // 键盘和右键菜单统一使用纯文本粘贴及长度检查。
    {
        // 1. 先计算替换选区后的 UTF-8 长度，超限时保持草稿原样。
        QTextCursor cursor = textCursor();
        const QString text = QApplication::clipboard()->text();
        QString proposed = toPlainText();
        proposed.replace(cursor.selectionStart(), cursor.selectionEnd() - cursor.selectionStart(), text);
        if (proposed.toUtf8().size() > TextLimits::MAX_TEXT_BYTES)
        {
            emit _owner->SigTextError(QStringLiteral("文字最多支持 16 KiB UTF-8 内容"));
            return;
        }
        // 2. 不读取 HTML 和图片 MIME，粘贴仍使用原生文本撤销。
        cursor.insertText(text);
        setTextCursor(cursor);
    }
    TextItem* _owner; // 父图元拥有本编辑器，指针生命周期一致。
    bool _has_preedit = false; // 是否存在尚未确认的输入法候选。
};
}

bool TextStyle::operator==(const TextStyle& other) const
{
    // 1. 比较保存属性，不受本机字体替代结果影响。
    return _font_family == other._font_family && _font_size == other._font_size &&
           _color == other._color && _is_bold == other._is_bold;
}

TextItem::TextItem(const QString& item_id, QGraphicsItem* parent)
    : CanvasItem(item_id, parent), _editor(new TextEditorItem(this))
{
    // 1. 普通状态不让子图元抢走父图元的选择和拖动。
    _editor->setTextInteractionFlags(Qt::NoTextInteraction);
    _editor->setAcceptedMouseButtons(Qt::NoButton);
    connect(_editor->document(), &QTextDocument::contentsChanged, this, &TextItem::UpdateTextGeometry);
    connect(_editor->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
            this, [this](const QSizeF&) { UpdateTextGeometry(); });
    SetTextStyle(_text_style);
    UpdateTextGeometry();
}

QRectF TextItem::boundingRect() const
{
    // 1. 留出选择框的绘制边缘。
    return _text_rect.adjusted(-1, -1, 1, 1);
}

void TextItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    // 1. 字体由子图元原生绘制，父图元只绘制可排除的选择反馈。
    if (isSelected() && !_is_editing)
    {
        painter->save();
        painter->setPen(QPen(QColor(40, 120, 220), 1, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(_text_rect);
        painter->restore();
    }
}

void TextItem::UpdateTextGeometry()
{
    // 1. 文档布局变化先通知场景，再更新父图元边界。
    const QSizeF size = _editor->document()->size();
    prepareGeometryChange();
    _text_rect = QRectF(0, 0, qMax(12.0, size.width()), qMax(qreal(_text_style._font_size), size.height()));
    update();
}

void TextItem::SetTextStyle(const TextStyle& style)
{
    // 1. 保存请求字体，渲染字体缺失时使用 Qt 默认值。
    _text_style = style;
    QFont font = QApplication::font();
    if (QFontDatabase::families().contains(style._font_family)) font.setFamily(style._font_family);
    font.setPixelSize(style._font_size);
    font.setBold(style._is_bold);
    _editor->setFont(font);
    _editor->setDefaultTextColor(style._color);
    UpdateTextGeometry();
}

TextItemData TextItem::GetTextData() const
{
    // 1. 位置使用原始图元位置，不能使用变换后的包围盒。
    return {itemId(), _editor->toPlainText(), _text_style, pos()};
}

void TextItem::SetTextData(const TextItemData& data)
{
    // 1. 值数据恢复不会改变旋转、缩放、透明度或层级。
    _editor->setPlainText(data._text);
    SetTextStyle(data._style);
    setPos(data._position);
}

bool TextItem::IsValidData(const TextItemData& data, bool allow_empty)
{
    // 1. 客户端、文件读取共用有限坐标和文本样式约束。
    return !data._item_id.isEmpty() && (allow_empty || !data._text.trimmed().isEmpty()) &&
           data._text.toUtf8().size() <= TextLimits::MAX_TEXT_BYTES &&
           !data._style._font_family.isEmpty() && data._style._font_family.toUtf8().size() <= 512 &&
           data._style._font_size >= TextLimits::MIN_FONT_SIZE && data._style._font_size <= TextLimits::MAX_FONT_SIZE &&
           data._style._color.isValid() && std::isfinite(data._position.x()) && std::isfinite(data._position.y());
}

void TextItem::BeginEditing()
{
    // 1. 保存单次编辑快照，整个文档统一格式并使用原生撤销。
    if (_is_editing || !isInteractionEnabled()) return;
    _text_before_edit = GetTextData();
    _is_editing = true;
    setFlag(ItemIsMovable, false);
    _editor->document()->clearUndoRedoStacks();
    _editor->setTextInteractionFlags(Qt::TextEditorInteraction);
    _editor->setAcceptedMouseButtons(Qt::LeftButton);
    _editor->setFocus(Qt::MouseFocusReason);
    QTextCursor cursor = _editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    _editor->setTextCursor(cursor);
    update();
}

void TextItem::EndEditing()
{
    // 1. 清除选区和光标，导出与普通浏览不包含编辑反馈。
    _is_editing = false;
    QTextCursor cursor = _editor->textCursor();
    cursor.clearSelection();
    _editor->setTextCursor(cursor);
    _editor->setTextInteractionFlags(Qt::NoTextInteraction);
    _editor->setAcceptedMouseButtons(Qt::NoButton);
    _editor->clearFocus();
    update();
}

bool TextItem::CommitEditing()
{
    // 1. 提交输入法中的最终内容，超限草稿继续保留供删减或取消。
    QApplication::inputMethod()->commit();
    if (!IsValidData(GetTextData(), true))
    {
        emit SigTextError(QStringLiteral("文字内容或样式无效，最多支持 16 KiB UTF-8 内容"));
        _editor->setFocus();
        return false;
    }
    // 2. 仅结束编辑，提交和图元删除由场景决定。
    EndEditing();
    return true;
}

void TextItem::CancelEditing()
{
    // 1. 结束输入并恢复编辑前值，不提交网络操作。
    QApplication::inputMethod()->reset();
    EndEditing();
    SetTextData(_text_before_edit);
}

bool TextItem::IsEditing() const
{
    // 1. 供视图和场景分发快捷键。
    return _is_editing;
}

void TextItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    // 1. 编辑由场景统一开启，避免同时存在多个编辑器。
    emit SigEditRequested(itemId());
    event->accept();
}
