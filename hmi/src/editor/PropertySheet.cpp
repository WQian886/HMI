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
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
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
#include <cmath>

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
                // ★ 复合编辑器(position / size)不能整块灰 —— 下面按分量各判各的。
                //   这里若也 setEnabled(false),分量的 setXEnabled(true) 会被父控件的
                //   disabled 状态压住,修等于没修。
                const int t2 = prop.userType();
                if (t2 != QMetaType::QPointF && t2 != QMetaType::QSizeF) {
                    editor->setEnabled(false);
                }
            }

            // ★ B 组修复(见 docs/bug图/README.md):position / size 是
            //   QPointF / QSizeF 复合属性,上面那段是拿【整个值】比 ——
            //   两个控件只要有一个分量不同,整个复合编辑器就被置灰。
            //   而多选时 position 几乎必然不同值,等于"多选后永远看不了位置",
            //   图上表现就是 x/y 全灰 + 显示 0.00(异值置空后读回默认值 0)。
            //   这里改成【按分量】分别判定:同值的分量照常显示可编辑,
            //   只有真正不同的那一个分量才置灰。
            if (const int type = prop.userType(); type == QMetaType::QPointF) {
                const QPointF first = firstVal.toPointF();
                bool sameX = true, sameY = true;
                for (auto* other : targets_) {
                    if (other == t) continue;
                    const QPointF p = other->property(prop.name()).toPointF();
                    if (p.x() != first.x()) sameX = false;
                    if (p.y() != first.y()) sameY = false;
                    if (!sameX && !sameY) break;
                }
                if (auto* pe = qobject_cast<PositionEditor*>(editor)) {
                    pe->setXValid(sameX);
                    pe->setYValid(sameY);
                    pe->setXEnabled(sameX);
                    pe->setYEnabled(sameY);
                    // ★ 逐分量写值:同值的显示真值,异值的置空(由 setValue 前的
                    //   置 0 + valid 标记共同表达"别动它")
                    pe->setValue(QPointF(sameX ? first.x() : 0.0,
                                         sameY ? first.y() : 0.0));
                }
                // 两个分量全同 → 恢复整体可编辑;只有一个同 → 整个编辑器保持 enabled,
                // 由分量各自控制灰度(上面 setXEnabled/setYEnabled 已经做了)。
                // 注意:不能在这里 editor->setEnabled(common),否则会把同值分量也一起灰掉。
                isCommonValue_[name] = (sameX && sameY);
            } else if (type == QMetaType::QSizeF) {
                const QSizeF first = firstVal.toSizeF();
                bool sameW = true, sameH = true;
                for (auto* other : targets_) {
                    if (other == t) continue;
                    const QSizeF s = other->property(prop.name()).toSizeF();
                    if (s.width()  != first.width())  sameW = false;
                    if (s.height() != first.height()) sameH = false;
                    if (!sameW && !sameH) break;
                }
                if (auto* se = qobject_cast<SizeEditor*>(editor)) {
                    se->setWValid(sameW);
                    se->setHValid(sameH);
                    se->setWEnabled(sameW);
                    se->setHEnabled(sameH);
                    se->setValue(QSizeF(sameW ? first.width()  : 0.0,
                                        sameH ? first.height() : 0.0));
                }
                isCommonValue_[name] = (sameW && sameH);
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

    // ★ E.8:打开工程时底图可能是坏的(路径失效),当场给红边提示,
    //   别等用户自己发现"设了图但没变化"
    markImagePathStatus();
}

QWidget* PropertySheet::createEditorForProperty(const QMetaProperty& prop) {
    const int type = prop.userType();

    // ★ E.8:imagePath 特殊 —— 需要"浏览…"和"清除"两个按钮,
    //   放在 QString 分支【之前】判断,否则会被当普通文本框吃掉。
    //   判据是属性名而不是类型:目前只有 ControlBase::imagePath 用这个形态,
    //   日后有别的路径属性会自动一起受益(命名一致即可)。
    if (QString::fromUtf8(prop.name()) == QStringLiteral("imagePath")) {
        return createImagePathEditor(prop);
    }

    // ★ QString:可能是普通文本或枚举字符串
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
    // ★ E.8:容器拆成内部 QLineEdit,后面的 qobject_cast 链才认得出来
    editor = unwrapEditor(editor);
    if (!editor) return;
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

// ★ E.8:imagePath 编辑行 = QLineEdit + [浏览…] + [清除]
//
//   【为什么用"容器包输入框"而不是给输入框旁边塞按钮】
//   属性面板的通用路径(setEditorValueFromProperty / connectEditorToProperty /
//   editorValueAsVariant)全靠 qobject_cast 成具体类型。塞按钮会破坏这些 cast,
//   等于给 imagePath 开三条特例分支 —— 以后每改一处同步逻辑都得记得照顾它。
//   现在的做法:容器挂进表单,内部 QLineEdit 用动态属性 "_lineEdit" 指出来,
//   三个通用函数开头统一 unwrap 一次(见 unwrapEditor)。特例只有一处。
QWidget* PropertySheet::createImagePathEditor(const QMetaProperty& prop) {
    const QString propName = QString::fromUtf8(prop.name());

    auto* row = new QWidget;
    row->setObjectName(propName);
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);

    auto* edit = new QLineEdit;
    edit->setObjectName(propName + "_edit");
    edit->setPlaceholderText(tr("(未设置底图,用手绘矢量)"));
    lay->addWidget(edit, 1);

    auto* browse = new QPushButton(tr("浏览…"));
    browse->setObjectName(propName + "_browse");
    lay->addWidget(browse);

    auto* clear = new QPushButton(tr("清除"));
    clear->setObjectName(propName + "_clear");
    clear->setToolTip(tr("清空底图,恢复手绘矢量外观"));
    lay->addWidget(clear);

    // ★ 让通用函数能 qobject_cast 到内部的 QLineEdit
    edit->setProperty("_lineEdit", QVariant::fromValue(static_cast<QObject*>(edit)));

    // 浏览:选完写【相对工程目录】的路径(见 ControlBase::setBasePath 的理由)
    connect(browse, &QPushButton::clicked, this, [this, propName, edit]() {
        auto* t = firstTarget();
        const QString start = t ? t->resolvedImagePath() : QString();
        const QString chosen = QFileDialog::getOpenFileName(
            this, tr("选择底图"), start,
            tr("图片文件 (*.png *.jpg *.jpeg *.bmp *.svg);;所有文件 (*.*)"));
        if (chosen.isEmpty()) return;   // 取消 = 不改
        const QString rel = toRelativeImagePath(chosen);
        edit->setText(rel);
        applyEditorChange(propName, rel);
        markImagePathStatus();
    });

    // 清除:一键回手绘。空字符串不是"路径为空",是"明确用默认外观"
    connect(clear, &QPushButton::clicked, this, [this, propName, edit]() {
        edit->clear();
        applyEditorChange(propName, QString());
        markImagePathStatus();
    });

    return row;
}

QWidget* PropertySheet::unwrapEditor(QWidget* editor) const {
    // ★ 容器 → 内部 QLineEdit。非容器原样返回,通用路径照旧。
    if (!editor) return nullptr;
    if (auto* inner = editor->property("_lineEdit").value<QObject*>()) {
        return qobject_cast<QWidget*>(inner);
    }
    return editor;
}

QString PropertySheet::toRelativeImagePath(const QString& abs) const {
    // 存相对路径,工程文件拷走/发给同事后图片还能找到
    // (和 tst_ProjectSchema 那个硬编码绝对路径是同一类错误的反面教材)
    auto* t = firstTarget();
    const QString base = t ? t->basePath() : QString();
    if (base.isEmpty()) return abs;   // 没基准(工程还没存)→ 只能存绝对
    const QDir baseDir(base);
    if (!baseDir.exists()) return abs;
    return baseDir.relativeFilePath(abs);   // 跨盘符时它自己会返回绝对路径
}

void PropertySheet::markImagePathStatus() {
    // ★ 加载失败要有【看得见】的反馈。原 ImageControl 的问题是两种情况
    //   (没配图 / 配了但坏了)都画同一个 [Image] 灰底,用户以为"设了没生效"。
    auto* t = firstTarget();
    if (!t) return;
    auto* row = form_->findChild<QWidget*>(QStringLiteral("imagePath"));
    if (!row) return;
    auto* edit = row->findChild<QLineEdit*>(QStringLiteral("imagePath_edit"));
    if (!edit) return;

    if (t->imagePath().isEmpty()) {
        edit->setStyleSheet(QString());
        edit->setToolTip(QString());
    } else if (t->imageLoaded()) {
        edit->setStyleSheet(QString());
        edit->setToolTip(t->resolvedImagePath());
    } else {
        edit->setStyleSheet(QStringLiteral("border: 1px solid #C83C3C;"));
        edit->setToolTip(tr("底图加载失败,已回退手绘矢量。\n路径:%1").arg(t->resolvedImagePath()));
    }
}

QVariant PropertySheet::editorValueAsVariant(QWidget* editor, const QMetaProperty& prop) {
    editor = unwrapEditor(editor);   // ★ E.8
    if (!editor) return {};
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
    // ★ E.8:容器拆成内部 QLineEdit,下面的 cast 链才认得出来
    editor = unwrapEditor(editor);
    if (!editor) return;
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

    // ★ B 组修复:PositionEditor / SizeEditor 用 NaN 标记"这个分量是异值,别动它"
    //   (异值时那一格被置空成 0,直接写回去会把所有控件的该分量刷成 0)。
    //   这里按分量拆开:只有"真值"分量才写,各自算出该控件的新值。
    const int userType = findProp(propName).userType();
    if (userType == QMetaType::QPointF || userType == QMetaType::QSizeF) {
        const bool isPoint = (userType == QMetaType::QPointF);
        const double c0 = isPoint ? newValue.toPointF().x()   : newValue.toSizeF().width();
        const double c1 = isPoint ? newValue.toPointF().y()   : newValue.toSizeF().height();
        const bool c0Valid = !std::isnan(c0);
        const bool c1Valid = !std::isnan(c1);
        if (!c0Valid && !c1Valid) return;   // 两格都是异值,无事发生

        const QByteArray nameUtf8 = propName.toUtf8();
        for (auto* t : targets_) {
            const QVariant oldValue = t->property(nameUtf8.constData());
            QVariant nv = oldValue;   // ★ 各自保留自己的另一个分量
            if (isPoint) {
                QPointF p = oldValue.toPointF();
                if (c0Valid) p.setX(c0);
                if (c1Valid) p.setY(c1);
                nv = QVariant::fromValue(p);
            } else {
                QSizeF s = oldValue.toSizeF();
                if (c0Valid) s.setWidth(c0);
                if (c1Valid) s.setHeight(c1);
                nv = QVariant::fromValue(s);
            }
            if (oldValue == nv) continue;
            undoStack_->push(new PropertyChangeCommand(t, propName, oldValue, nv));
        }
        // ★ 改完之后刷新分量的"真值/异值"状态
        syncFormFromTarget();
        return;
    }

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
                // ★ B 组修复:复合属性按分量刷新,不能整块置灰
                const int type = prop.userType();
                if (type == QMetaType::QPointF) {
                    const QPointF first = firstVal.toPointF();
                    bool sameX = true, sameY = true;
                    for (auto* t : targets_) {
                        const QPointF p = t->property(name.toUtf8().constData()).toPointF();
                        if (p.x() != first.x()) sameX = false;
                        if (p.y() != first.y()) sameY = false;
                        if (!sameX && !sameY) break;
                    }
                    if (auto* pe = qobject_cast<PositionEditor*>(field)) {
                        pe->setXValid(sameX);
                        pe->setYValid(sameY);
                        pe->setXEnabled(sameX);
                        pe->setYEnabled(sameY);
                        pe->setValue(QPointF(sameX ? first.x() : 0.0,
                                             sameY ? first.y() : 0.0));
                    }
                    isCommonValue_[name] = (sameX && sameY);
                } else if (type == QMetaType::QSizeF) {
                    const QSizeF first = firstVal.toSizeF();
                    bool sameW = true, sameH = true;
                    for (auto* t : targets_) {
                        const QSizeF s = t->property(name.toUtf8().constData()).toSizeF();
                        if (s.width()  != first.width())  sameW = false;
                        if (s.height() != first.height()) sameH = false;
                        if (!sameW && !sameH) break;
                    }
                    if (auto* se = qobject_cast<SizeEditor*>(field)) {
                        se->setWValid(sameW);
                        se->setHValid(sameH);
                        se->setWEnabled(sameW);
                        se->setHEnabled(sameH);
                        se->setValue(QSizeF(sameW ? first.width()  : 0.0,
                                            sameH ? first.height() : 0.0));
                    }
                    isCommonValue_[name] = (sameW && sameH);
                } else if (common) {
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
