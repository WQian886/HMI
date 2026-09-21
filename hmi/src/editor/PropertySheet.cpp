// ============================================================================
// PropertySheet 实现
// ============================================================================

#include "editor/PropertySheet.h"
#include "editor/PropertyChangeCommand.h"
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
#include <QPushButton>
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
    setEnabled(false);
}

PropertySheet::~PropertySheet() = default;

void PropertySheet::setTarget(qwhmi::widgets::ControlBase* target) {
    if (target_ == target) return;

    // ★ v1 简化:不支持 target → form 自动同步(Qt 6 的 QMetaMethod 信号 + lambda
    //   重载匹配在 MSVC 上有兼容问题;取消 setTarget 不频繁,用户重选可接受)。
    //   connections_ 字段保留,留给未来实现用。

    target_ = target;
    setEnabled(target_ != nullptr);

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

    if (!target_) {
        form_->addRow(new QLabel(tr("(未选中控件)")));
        return;
    }

    const QMetaObject* meta = target_->metaObject();
    for (int i = 0; i < meta->propertyCount(); ++i) {
        QMetaProperty prop = meta->property(i);
        if (!prop.isWritable()) continue;       // 跳过 displayText 这类只读

        QWidget* editor = createEditorForProperty(prop);
        if (!editor) continue;                  // 不支持的类型静默跳过

        // ★ 初值:target_->property(prop.name())
        const QVariant val = target_->property(prop.name());
        setEditorValueFromProperty(editor, prop, val);

        // ★ 修改触发:applyEditorChange
        connectEditorToProperty(editor, prop);

        form_->addRow(QString::fromUtf8(prop.name()) + ":", editor);
        metaProps_.append(prop);
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

    // ★ QPointF/QSizeF:暂不支持(E.4b 8 handle 拖拽阶段一并做)
    //   这里静默跳过,后续 TODO
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
    if (!target_ || !undoStack_) return;

    const QByteArray nameUtf8 = propName.toUtf8();
    const QVariant oldValue = target_->property(nameUtf8.constData());
    if (oldValue == newValue) return;            // 没变,不进 undo

    auto* cmd = new PropertyChangeCommand(target_, propName, oldValue, newValue);
    undoStack_->push(cmd);
}

void PropertySheet::onTargetPropertyChanged() {
    // ★ target 的属性变化(实时数据驱动 / undo / redo)→ 同步回表单
    syncFormFromTarget();
}

void PropertySheet::syncFormFromTarget() {
    if (!target_) return;
    suppressApply_ = true;
    for (const QMetaProperty& prop : metaProps_) {
        const QString name = QString::fromUtf8(prop.name());
        for (int i = 0; i < form_->count(); ++i) {
            QLayoutItem* item = form_->itemAt(i);
            if (!item) continue;
            QWidget* field = item->widget();
            if (field && field->objectName() == name) {
                setEditorValueFromProperty(field, prop, target_->property(prop.name()));
                break;
            }
        }
    }
    suppressApply_ = false;
}

} // namespace qwhmi::editor
