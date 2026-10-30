/***********************************************************************************
* @file         canvaseditcontroller.h
* @brief        画布菜单与快捷键共用的编辑命令及目标上下文
* @author       shanzhi
* @date         2026/10/25
* @history
***********************************************************************************/
#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>

#include "canvasclipboardpayload.h"

class QAction;
class QMenu;
class PaintScene;
class CanvasGraphicsView;

class CanvasEditController : public QObject
{
    Q_OBJECT

public:
    CanvasEditController(PaintScene* scene, CanvasGraphicsView* view, QObject* parent = nullptr); // 绑定当前场景的三个编辑命令。
    ~CanvasEditController() override; // 关闭菜单并回收视图拥有的弹出窗口。
    void CloseMenu(); // 切换房间或场景时立即废弃菜单上下文。

signals:
    void SigCopyRequested(QStringList item_ids); // 复制固定目标集合，窗口复用原序列化逻辑。
    void SigPasteRequested(QPointF scene_pos, CanvasPasteAnchor anchor); // 按触发时落点和锚定方式执行粘贴。
    void SigDeleteRequested(QStringList item_ids); // 删除固定目标集合，在线由同步组件确认。
    void SigError(QString message); // 由画布窗口展示剪贴板错误。

private:
    QStringList TargetIds() const; // 返回仍在当前场景的菜单目标或键盘选择。
    void RefreshActions(); // 根据目标、剪贴板、权限和等待状态刷新三个动作。
    void RefreshClipboard(); // 剪贴板变更后缓存一次无副作用识别结果。
    void ShowMenu(const QPointF& scene_pos, const QPoint& screen_pos); // 只更新选择和目标，不启动图元手势。
    void Copy(); // 校验后转发复制共用入口。
    void Paste(); // 校验后转发固定落点的粘贴入口。
    void Delete(); // 校验整组选中目标后提交删除。

    QPointer<PaintScene> _scene; // 场景销毁时自动失效，不拥有内容对象。
    QPointer<CanvasGraphicsView> _view; // 接收画布焦点内的输入并恢复工具光标。
    QPointer<QMenu> _menu; // 视图拥有的非阻塞弹出菜单。
    QAction* _copy_action = nullptr; // 控制器拥有的复制命令。
    QAction* _paste_action = nullptr; // 控制器拥有的粘贴命令。
    QAction* _delete_action = nullptr; // 控制器拥有的删除命令。
    QStringList _context_ids; // 菜单打开时固定的目标 ID，空白菜单为空。
    QPointF _paste_position; // 本次键盘或菜单触发时记录的场景坐标。
    CanvasPasteAnchor _paste_anchor = CanvasPasteAnchor::Center; // 菜单使用左上角，键盘沿用中心。
    CanvasClipboardContent _clipboard_content; // 最近一次系统剪贴板识别结果。
    bool _has_context = false; // 菜单关闭前使用固定目标，禁止回退到旧选择。
};
