// ============================================================================
// PropertySheet 实现
// ============================================================================

#include "editor/PropertySheet.h"
#include "editor/PropertyChangeCommand.h"
#include "editor/SizeEditor.h"
#include "editor/PositionEditor.h"
#include "widgets/base/ControlBase.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaEnum>
#include <QMetaType>
#include <QPointF>
#include <QPushButton>
#include <QSet>
#include <QSizeF>
#include <QSpinBox>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

namespace qwhmi::editor {

PropertySheet::PropertySheet(QUndoStack* undoStack, QWidget* parent)
    : QWidget(parent), undoStack_(undoStack) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    form_ = new QFormLayout;
    form_->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    outer->addLayout(form_);
    outer->addStretch(1);

    // ★ 无选中时的占位
    auto* emptyLabel = new QLabel(tr("(未选中控件)"), this);
    form_->addRow(emptyLabel);
    // 注:不在这里 setEnabled(false)— 让 setTarget 决定。
    //     EditorMainWindow 默认编辑模式 + 已选中 → enabled;
    //     否则随 setTarget(nullptr) 自然 disabled。
}

PropertySheet::~PropertySheet() = default;

void PropertySheet::setTarget(qwhmi::widgets::ControlBase* target) {
    QList<qwhmi::widgets::ControlBase*> list;
    if (target) list.append(target);
    setTargets(list);
}

void PropertySheet::setTargets(const QList<qwhmi::widgets::ControlBase*>& targets) {
    if (targets_ == targets) return;

    // ★ v1 简化:不支持 target → form 自动同步(Qt 6 的 QMetaMethod 信号 + lambda
    //   重载匹配在 MSVC 上有兼容问题;取消 setTargets 不频繁,用户重选可接受)。
    //   connections_ 字段保留,留给未来实现用。

    targets_ = targets;
    setEnabled(!targets_.isEmpty());

    rebuildForm();
}

void PropertySheet::rebuildForm() {
    // ★ 清空旧表单
    while (form_->count() > 0) {
        QLayoutItem* item = form_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    metaProps_.clear();
    isCommonValue_.clear();

    if (targets_.isEmpty()) {
        form_->addRow(new QLabel(tr("(未选中控件)")));
        return;
    }

    // ★ 多选:在表单顶部加提示
    if (targets_.size() >= 2) {
        auto* banner = new QLabel(tr("(已选 %1 个控件,共同属性可批量编辑;不同值字段显示为空)")
                                      .arg(targets_.size()));
        banner->setStyleSheet("color: #0078D7; font-weight: bold;");
        form_->addRow(banner);
    }

    // ★ 收集所有 target 的可写 Q_PROPERTY 并集
    //   (基类 + 派生类一起) → 单一列表(去重 + 跨类同名合并)
    QHash<QString, QPair<QMetaProperty, QWidget*>> editorByName;  // name → (prop, 第一个创建的 editor)
    QSet<QString> seen;

    for (auto* t : targets_) {
        const QMetaObject* meta = t->metaObject();
        for (int i = 0; i < meta->propertyCount(); ++i) {
            QMetaProperty prop = meta->property(i);
            if (!prop.isWritable()) continue;
            const QString name = QString::fromUtf8(prop.name());
            if (seen.contains(name)) continue;
            seen.insert(name);

            QWidget* editor = createEditorForProperty(prop);
            if (!editor) continue;

            // ★ 共同值检查:读所有 target 的这个 prop 值
            const QVariant firstVal = t->property(prop.name());
            bool common = true;
            for (auto* other : targets_) {
                if (other == t) continue;
                if (other->property(prop.name()) != firstVal) {
                    common = false;
                    break;
                }
            }
            isCommonValue_[name] = common;

            if (common) {
                // 共同值:显示该值,可编辑
                setEditorValueFromProperty(editor, prop, firstVal);
            } else {
                // ★ 异值:显示空 + 禁用(视觉上变灰,提示用户"值不同")
                setEditorValueFromProperty(editor, prop, QVariant());   // 值置空
                editor->setEnabled(false);
            }

            connectEditorToProperty(editor, prop);
            editorByName[name] = qMakePair(prop, editor);
        }
    }

    // ★ 按属性名稳定排序后加入表单(便于多选时视觉稳定)
    QStringList sortedNames = editorByName.keys();
    sortedNames.sort();
    for (const QString& name : sortedNames) {
        const auto pair = editorByName.value(name);
        QWidget* editor = pair.second;
        QString label = name;
        if (!isCommonValue_.value(name)) {
            label += "  (异值)";
        }
        form_->addRow(label + ":", editor);
        metaProps_.append(pair.first);
    }
}

QWidget* PropertySheet::createEditorForProperty(const QMetaProperty& prop) {
    const int type = prop.userType();

    // ★ QString:可能是普通文本、文件路径(imagePath)、或枚举字符串
    if (type == QMetaType::QString) {
        auto* edit = new QLineEdit;
        edit->setObjectName(QString::fromUtf8(prop.name()));
        return edit;
    }

    // ★ QVariant:可能是 int/double/bool
    if (type == QMetaType::QVariant) {
        // 由具体控件决定类型 — 反射不到,先按最常见的 QString 处理
        auto* edit = new QLineEdit;
        edit->setObjectName(QString::fromUtf8(prop.name()));
        return edit;
    }

    // ★ double:QDoubleSpinBox
    if (type == QMetaType::Double || type == QMetaType::Float) {
        auto* spin = new QDoubleSpinBox;
        spin->setDecimals(2);
        spin->setRange(-1e9, 1e9);
        spin->setSingleStep(1.0);
        spin->setObjectName(QString::fromUtf8(prop.name()));
        return spin;
    }

    // ★ int:QSpinBox
    if (type == QMetaType::Int || type == QMetaType::UInt) {
        auto* spin = new QSpinBox;
        spin->setRange(-1000000, 1000000);
        spin->setObjectName(QString::fromUtf8(prop.name()));
        return spin;
    }

    // ★ bool:QCheckBox
    if (type == QMetaType::Bool) {
        auto* cb = new QCheckBox;
        cb->setObjectName(QString::fromUtf8(prop.name()));
        return cb;
    }

    // ★ QColor:QPushButton + QColorDialog
    if (type == QMetaType::QColor) {
        auto* btn = new QPushButton;
        btn->setObjectName(QString::fromUtf8(prop.name()));
        btn->setProperty("_kind", "color");
        return btn;
    }

    // ★ 枚举类型(用 Q_ENUM 注册):QComboBox + QMetaEnum
    if (prop.isEnumType()) {
        QMetaEnum me = prop.enumerator();
        auto* combo = new QComboBox;
        for (int i = 0; i < me.keyCount(); ++i) {
            combo->addItem(QString::fromUtf8(me.key(i)), me.value(i));
        }
        combo->setObjectName(QString::fromUtf8(prop.name()));
        combo->setProperty("_kind", "enum");
        return combo;
    }

    // ★ QStringList:用 QLineEdit + 逗号分隔(简化方案)
    if (type == QMetaType::QStringList) {
        auto* edit = new QLineEdit;
        edit->setPlaceholderText(tr("(逗号分隔)"));
        edit->setObjectName(QString::fromUtf8(prop.name()));
        edit->setProperty("_kind", "stringlist");
        return edit;
    }

    // ★ E.4c:QSizeF 复合编辑器(两个 QDoubleSpinBox 拼 W/H)
    if (type == QMetaType::QSizeF) {
        auto* e = new SizeEditor;
        e->setObjectName(QString::fromUtf8(prop.name()));
        e->setProperty("_kind", "size");
        return e;
    }

    // ★ E.4c:QPointF 复合编辑器(两个 QDoubleSpinBox 拼 X/Y)
    if (type == QMetaType::QPointF) {
        auto* e = new PositionEditor;
        e->setObjectName(QString::fromUtf8(prop.name()));
        e->setProperty("_kind", "position");
        return e;
    }

    return nullptr;
}

void PropertySheet::setEditorValueFromProperty(QWidget* editor,
                                               const QMetaProperty& prop,
                                               const QVariant& value) {
    const int type = prop.userType();

    if (auto* edit = qobject_cast<QLineEdit*>(editor)) {
        if (type == QMetaType::QStringList) {
            // QStringList → "a,b,c"
            edit->setText(value.toStringList().join(','));
        } else {
            edit->setText(value.toString());
        }
    } else if (auto* spin = qobject_cast<QDoubleSpinBox*>(editor)) {
        spin->setValue(value.toDouble());
    } else if (auto* spin = qobject_cast<QSpinBox*>(editor)) {
        spin->setValue(value.toInt());
    } else if (auto* cb = qobject_cast<QCheckBox*>(editor)) {
        cb->setChecked(value.toBool());
    } else if (auto* btn = qobject_cast<QPushButton*>(editor)) {
        // ★ QColor:QPushButton 显示色块
        QColor c = value.value<QColor>();
        if (!c.isValid()) c = QColor("#FFFFFF");
        btn->setText(c.name());
        btn->setStyleSheet(QString("background-color: %1; color: %2;")
                               .arg(c.name(),
                                    c.lightness() > 128 ? "#000000" : "#FFFFFF"));
    } else if (auto* combo = qobject_cast<QComboBox*>(editor)) {
        // ★ enum:按 value 找索引
        int v = value.toInt();
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemData(i).toInt() == v) {
                combo->setCurrentIndex(i);
                break;
            }
        }
    } else if (auto* sizeE = qobject_cast<SizeEditor*>(editor)) {
        sizeE->setValue(value.toSizeF());
    } else if (auto* posE = qobject_cast<PositionEditor*>(editor)) {
        posE->setValue(value.toPointF());
    }
}

QVariant PropertySheet::editorValueAsVariant(QWidget* editor, const QMetaProperty& prop) {
    const int type = prop.userType();

    if (auto* edit = qobject_cast<QLineEdit*>(editor)) {
        if (type == QMetaType::QStringList) {
            // "a,b,c" → QStringList
            QString text = edit->text();
            if (text.isEmpty()) return QStringList{};
            return text.split(',');
        }
        return edit->text();
    } else if (auto* spin = qobject_cast<QDoubleSpinBox*>(editor)) {
        return spin->value();
    } else if (auto* spin = qobject_cast<QSpinBox*>(editor)) {
        return spin->value();
    } else if (auto* cb = qobject_cast<QCheckBox*>(editor)) {
        return cb->isChecked();
    } else if (auto* btn = qobject_cast<QPushButton*>(editor)) {
        return QColor(btn->text());
    } else if (auto* combo = qobject_cast<QComboBox*>(editor)) {
        return combo->currentData().toInt();
    } else if (auto* sizeE = qobject_cast<SizeEditor*>(editor)) {
        return QVariant::fromValue(sizeE->value());
    } else if (auto* posE = qobject_cast<PositionEditor*>(editor)) {
        return QVariant::fromValue(posE->value());
    }
    return {};
}

void PropertySheet::connectEditorToProperty(QWidget* editor,
                                            const QMetaProperty& prop) {
    // ★ 各种 editor 的 valueChanged → applyEditorChange
    QString propName = QString::fromUtf8(prop.name());
    if (auto* edit = qobject_cast<QLineEdit*>(editor)) {
        connect(edit, &QLineEdit::editingFinished, this, [this, propName, edit]() {
            // ★ QLineEdit 用 editingFinished 触发(避免每按键 1 次 undo)
            applyEditorChange(propName, editorValueAsVariant(edit, findProp(propName)));
        });
    } else if (auto* spin = qobject_cast<QDoubleSpinBox*>(editor)) {
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this, propName, spin](double) {
            applyEditorChange(propName, spin->value());
        });
    } else if (auto* spin = qobject_cast<QSpinBox*>(editor)) {
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
                [this, propName, spin](int) {
            applyEditorChange(propName, spin->value());
        });
    } else if (auto* cb = qobject_cast<QCheckBox*>(editor)) {
        connect(cb, &QCheckBox::toggled, this, [this, propName, cb](bool) {
            applyEditorChange(propName, cb->isChecked());
        });
    } else if (auto* btn = qobject_cast<QPushButton*>(editor)) {
        connect(btn, &QPushButton::clicked, this, [this, btn, propName]() {
            QColor current = QColor(btn->text());
            QColor chosen = QColorDialog::getColor(current, this, tr("选择颜色"));
            if (!chosen.isValid()) return;
            applyEditorChange(propName, chosen);
        });
    } else if (auto* combo = qobject_cast<QComboBox*>(editor)) {
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this, propName, combo](int) {
            applyEditorChange(propName, combo->currentData().toInt());
        });
    } else if (auto* sizeE = qobject_cast<SizeEditor*>(editor)) {
        // ★ SizeEditor 自定义 valueChanged(QSizeF) → 转 QVariant 给 applyEditorChange
        connect(sizeE, &SizeEditor::valueChanged, this,
                [this, propName, sizeE](const QSizeF& s) {
            applyEditorChange(propName, QVariant::fromValue(s));
        });
    } else if (auto* posE = qobject_cast<PositionEditor*>(editor)) {
        connect(posE, &PositionEditor::valueChanged, this,
                [this, propName, posE](const QPointF& p) {
            applyEditorChange(propName, QVariant::fromValue(p));
        });
    }
}

QMetaProperty PropertySheet::findProp(const QString& propName) const {
    for (const QMetaProperty& p : metaProps_) {
        if (QString::fromUtf8(p.name()) == propName) return p;
    }
    return {};
}

void PropertySheet::applyEditorChange(const QString& propName, const QVariant& newValue) {
    if (suppressApply_) return;
    if (targets_.isEmpty() || !undoStack_) return;

    const QByteArray nameUtf8 = propName.toUtf8();

    if (targets_.size() == 1) {
        // ★ 单选:1 个 PropertyChangeCommand(走原有 Undo 链路)
        auto* t = targets_.first();
        const QVariant oldValue = t->property(nameUtf8.constData());
        if (oldValue == newValue) return;
        auto* cmd = new PropertyChangeCommand(t, propName, oldValue, newValue);
        undoStack_->push(cmd);
    } else {
        // ★ 多选:为每个 target 各 push 一个 PropertyChangeCommand
        //   (Qt UndoStack 按 push 顺序 redo/undo,200ms 内 PropertyChangeCommand
        //   会自动 mergeWith 合并为 1 个命令,user 感觉是 1 步 Ctrl+Z 全部还原)
        for (auto* t : targets_) {
            const QVariant oldValue = t->property(nameUtf8.constData());
            if (oldValue == newValue) continue;
            auto* cmd = new PropertyChangeCommand(t, propName, oldValue, newValue);
            undoStack_->push(cmd);
        }
        // ★ 共同属性标记更新:这次所有 target 都是新值了
        isCommonValue_[propName] = true;
        // ★ 取消"异值"标签(更新所有行的 label)
        syncFormFromTarget();
    }
}

void PropertySheet::onTargetPropertyChanged() {
    // ★ target 的属性变化(实时数据驱动 / undo / redo)→ 同步回表单
    syncFormFromTarget();
}

void PropertySheet::syncFormFromTarget() {
    if (targets_.isEmpty()) return;
    suppressApply_ = true;
    // ★ 多选时:对每个 prop 重新检查共同值,更新 editor 状态
    for (const QMetaProperty& prop : metaProps_) {
        const QString name = QString::fromUtf8(prop.name());
        // ★ 重新计算共同值(用户可能刚 push 了 PropertyChangeCommand 让所有 target 值变相同)
        const QVariant firstVal = targets_.first()->property(name.toUtf8().constData());
        bool common = true;
        for (auto* t : targets_) {
            if (t->property(name.toUtf8().constData()) != firstVal) {
                common = false;
                break;
            }
        }
        isCommonValue_[name] = common;

        for (int i = 0; i < form_->count(); ++i) {
            QLayoutItem* item = form_->itemAt(i);
            if (!item) continue;
            QWidget* field = item->widget();
            if (field && field->objectName() == name) {
                if (common) {
                    setEditorValueFromProperty(field, prop, firstVal);
                    field->setEnabled(true);
                } else {
                    setEditorValueFromProperty(field, prop, QVariant());
                    field->setEnabled(false);
                }
                break;
            }
        }
    }
    suppressApply_ = false;
}

} // namespace qwhmi::editor
