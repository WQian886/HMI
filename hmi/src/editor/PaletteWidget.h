// ============================================================================
// 阶段 E · E.4a · 控件调色板 PaletteWidget
//
// 文件职责:左侧 dock,列出所有已注册控件类型,QListWidget 可拖到画布。
//   - 启动时遍历 ControlBase::registeredTypeNames() 填项
//   - startDragOverride 把选中项的 type 字符串塞进 QMimeData
//
// 关键设计:
//   - 用静态工厂字符串("Bar"/"LED"/...)作为 mime type,
//     EditorMainWindow::view dropEvent 读出来直接 ControlBase::create()
//   - 关闭 E.4b 阶段时,这里可以加 8 handle 缩放图标(QPixmap 缓存)
//
// 关联:ControlBase.h, EditorMainWindow.h
// ============================================================================

#pragma once

#include <QListWidget>

class QMimeData;

namespace qwhmi::editor {

class PaletteWidget : public QListWidget {
    Q_OBJECT
public:
    explicit PaletteWidget(QWidget* parent = nullptr);
    ~PaletteWidget() override;

    // ★ 自定义 mime type — 拖入事件识别
    static QString controlMimeType() { return QStringLiteral("application/x-hmi-control-type"); }

protected:
    // ★ 覆盖拖拽源:把选中项 type 字符串塞进 QMimeData
    void startDrag(Qt::DropActions supportedActions) override;
};

} // namespace qwhmi::editor
