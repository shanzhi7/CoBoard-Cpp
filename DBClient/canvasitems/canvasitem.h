/***********************************************************************************
* @file         canvasitem.h
* @brief        画布自定义图元的公共基类和交互状态接口
* @author       shanzhi
* @date         2026/10/06
* @history
***********************************************************************************/
#pragma once

#include <QGraphicsObject>
#include <QRectF>
#include <QString>

class QKeyEvent;
class TextItem;
struct TextStyle;

// CanvasItem 是所有自定义画布图元的共同基类，统一保存可同步的图元标识。
class CanvasItem : public QGraphicsObject
{
    Q_OBJECT

public:
    explicit CanvasItem(const QString& item_id, QGraphicsItem* parent = nullptr); // 创建带有稳定 item_id 的画布图元。
    ~CanvasItem() override; // 释放图元对象，具体图片资源由资源管理器单独负责。

    QString itemId() const; // 返回用于房间同步和本地索引的图元标识。
    void setItemId(const QString& item_id); // 更新图元标识，调用方必须保证新标识在场景内唯一。
    void setInteractionEnabled(bool enabled); // 按当前房间编辑权限启用或禁用选择、移动、键盘变换和删除。
    bool isInteractionEnabled() const; // 返回图元是否允许当前用户修改。
    QPainterPath shape() const override; // 返回实际图元轮廓，封闭图形内部不参与首次选择。
    TextItem* BoundTextItem() const; // 返回父图元拥有的附属编辑组件，未创建时为空。
    void SetBoundTextData(const QString& text, const TextStyle& style); // 设置附属纯文本及样式，不创建独立画布图元。
    void UpdateBoundTextLayout(); // 更新附属文字安全区域并按需要自动增高。
    void SetBoundTextLayoutEnabled(bool enabled); // 完整快照恢复期间暂缓自动布局，防止中间状态增高。

signals:
    void sigGeometryChanged(QString item_id, QRectF scene_rect, qreal rotation, qreal scale); // 图元位置或变换变化时通知同步层。
    void sigDeleteRequested(QString item_id); // 用户按删除键后请求场景层提交删除操作。

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override; // 将 Qt 图元变换转换为可同步的几何变化信号。
    void keyPressEvent(QKeyEvent* event) override; // 处理键盘变换和删除，并把未处理按键交回 Qt。

private:
    QString _item_id; // 图元在当前房间内的稳定标识，生命周期覆盖图元历史记录。
    bool _interaction_enabled = true; // 当前房间编辑权限，权限撤销时由 PaintScene 关闭交互。
    TextItem* _bound_text_item = nullptr; // 父图元拥有的文字组件，不独立保存或同步。
    bool _is_bound_text_layout_active = false; // 防止文档尺寸与父尺寸反馈递归。
    bool _is_bound_text_layout_enabled = true; // 批量恢复状态期间暂停自动增高。
};
