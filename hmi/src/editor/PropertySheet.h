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
