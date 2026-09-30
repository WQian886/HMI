// ============================================================================
// 阶段 E · E.2 · 属性面板 PropertySheet(Q_PROPERTY 反射)
//
// 文件职责:基于 QMetaObject/QMetaProperty 反射遍历 ControlBase 派生类的
//   Q_PROPERTY,自动生成对应类型的编辑 widget(QDoubleSpinBox/QComboBox/...),绑到
//   QUndoStack 实现可撤销编辑。
//
// 关键设计:
//   - 完全反射驱动:新增控件零代码接入,只要它的 Q_PROPERTY 用标准类型
//   - 类型 → widget 映射表固定(见 .cpp 节 createEditorForProperty)
//   - 不支持类型(QFont/QPolygonF 等)静默跳过,不报错
//   - 单向同步 target → form(避免回灌 undo)
//
// 关联:PropertyChangeCommand.h/.cpp, ControlBase.h, EditorMainWindow(E.2 周 11)
// ============================================================================

#pragma once

#include <QWidget>

#include <QList>
#include <QMetaObject>
#include <QMetaProperty>

class QFormLayout;
class QUndoStack;

namespace qwhmi::widgets { class ControlBase; }

namespace qwhmi::editor {

class PropertySheet : public QWidget {
    Q_OBJECT
public:
    explicit PropertySheet(QUndoStack* undoStack, QWidget* parent = nullptr);
    ~PropertySheet() override;

    // ★ 单选:选中单个控件时调(向后兼容,内部转 setTargets({target}))
    void setTarget(qwhmi::widgets::ControlBase* target);

    // ★ E.4d-续-2:多选 — 设 0 个 = 空,1 个 = 单选(同 setTarget),>=2 = 多选共同属性
    void setTargets(const QList<qwhmi::widgets::ControlBase*>& targets);

    // ★ 单选时返回唯一 target,多选/空时返回 nullptr
    qwhmi::widgets::ControlBase* target() const {
        return targets_.size() == 1 ? targets_.first() : nullptr;
    }
    int targetsCount() const { return targets_.size(); }

private slots:
    // ★ v1 暂不实现 target → form 自动同步(Qt 6 QMetaMethod 重载匹配问题)
    //   接口保留,未来 E.4c 集成时再启用
    void onTargetPropertyChanged();

private:
    void rebuildForm();                            // 清空 + 重建
    QWidget* createEditorForProperty(const QMetaProperty& prop);
    void     setEditorValueFromProperty(QWidget* editor,
                                        const QMetaProperty& prop,
                                        const QVariant& value);
    QVariant editorValueAsVariant(QWidget* editor, const QMetaProperty& prop);
    void     connectEditorToProperty(QWidget* editor, const QMetaProperty& prop);
    void     applyEditorChange(const QString& propName, const QVariant& newValue);
    void     syncFormFromTarget();                // 不重建,只把 target 当前值刷回表单
    QMetaProperty findProp(const QString& propName) const;

    // ★ E.8:imagePath 的"浏览…/清除"按钮组。
    //   返回的 widget 是个容器(QLineEdit + 两个 QPushButton),
    //   但 editorByName["imagePath"] 存的仍是【内部的 QLineEdit】——
    //   setEditorValueFromProperty / connectEditorToProperty 走的都是那一套通用路径,
    //   换成容器就全断了。同步/回写因此不需要为它开特例。
    QWidget* createImagePathEditor(const QMetaProperty& prop);
    //   容器 → 内部 QLineEdit。非容器原样返回。
    QWidget* unwrapEditor(QWidget* editor) const;
    //   绝对路径 → 相对工程目录;无基准时原样返回
    QString toRelativeImagePath(const QString& abs) const;
    //   加载失败时给输入框描红边 + tooltip,别让"设了没生效"变成无反馈
    void     markImagePathStatus();
    //   从多选里挑一个控件当"当前上下文"(按钮操作总是作用在全部 targets 上)
    qwhmi::widgets::ControlBase* firstTarget() const { return targets_.isEmpty() ? nullptr : targets_.first(); }

    QFormLayout* form_ = nullptr;
    QList<qwhmi::widgets::ControlBase*> targets_;   // ★ E.4d-续-2:取代 target_,支持 0/1/N 个
    QUndoStack*  undoStack_ = nullptr;

    // ★ 抑制表单 → target 回写(target → form 单向同步时设 true,避免 undo 回灌)
    bool suppressApply_ = false;

    // ★ 反射生成时记下的 metaProperty,供 onTargetPropertyChanged 用 name → index
    QList<QMetaProperty> metaProps_;

    // ★ E.4d-续-2:多选时记录每个 property 是否所有 target 值都相同
    QHash<QString, bool> isCommonValue_;

    // ★ 保存所有 connect() 返回的 QMetaObject::Connection 句柄,
    //   setTarget 切换时统一 disconnect
    QList<QMetaObject::Connection> connections_;
};

} // namespace qwhmi::editor
