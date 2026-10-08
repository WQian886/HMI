// ============================================================================
// 阶段 F · F4-a · 报警配置对话框实现
//
// 关联:AlarmConfigDialog.h
//
// ★ 本文件的核心约束(改代码前先读一遍)
//   它【不改运行中的任何报警状态】。只把 AlarmManager 里的配置读成草稿,
//   让人能看、能改草稿、能导出 JSON。应用回 AlarmManager 是 F4-b 的活。
//   任何时候你在这里看到 addChecker / removeChecker / config_ 被写,
//   都是 F4-b 的内容漏进来了 —— 那样这个对话框就测不了了。
// ============================================================================

#include "alarm/AlarmConfigDialog.h"

#include "../../AlarmManager.h"
#include "../../JsonSerializer.h"
#include "../../ThresholdAlarmChecker.h"
#include "../../VariableManager.h"
#include "../auth/CurrentUser.h"
#include "../auth/Permission.h"
#include "../logging/Logging.h"

#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>

namespace qwhmi::alarm {

namespace {

constexpr int kColName        = 0;
constexpr int kColVariable    = 1;
constexpr int kColLevel       = 2;
constexpr int kColDir         = 3;
constexpr int kColThreshold   = 4;
constexpr int kColMinDur      = 5;
constexpr int kColHysteresis  = 6;
constexpr int kColShelved     = 7;

// 等级下拉的显示顺序:从最轻到最重(与 Alarm.h:17 枚举顺序一致)。
// 【为什么不直接把 "Info"/"Warning" 当下拉文字】
// 工程文件里存的是英文枚举名(和 JsonSerializer.cpp:76 的输出对齐),
// 但界面给中文用户看 —— 显示中文、存英文,两件事分开。
struct LevelItem { const char* text; const char* key; };
const LevelItem kLevels[] = {
    { "信息", "Info" },
    { "警告", "Warning" },
    { "严重", "Critical" },
    { "危急", "Emergency" },
};

struct DirItem { const char* text; const char* key; };
const DirItem kDirections[] = {
    { "超上限", "Above" },
    { "低于下限", "Below" },
};

// hysteresis / threshold 的合理上限。
// 【为什么给上限】threshold 是工程量程,1e9 已经超出任何真实变送器;
// 输错一个 1e12 会让"死区"覆盖整个量程,报警变成永远不报 —— 而界面
// 上看不出异常。这类静默失效正是本项目反复吃亏的地方(见头文件)。
constexpr double kMaxMagnitude = 1.0e9;

// key(工程文件里的英文枚举名)→ 显示文字(中文)。
// 【为什么要这两个函数,而不是直接把 kLevels 里的 text 存起来】
//   草稿行存的是 key —— 导出时要喂给 JsonSerializer::toJson(cfg),
//   那边只认英文。中文只是给人看的,不能反过来成为数据源。
QString levelDisplayName(const QString& key) {
    for (const LevelItem& lv : kLevels) {
        if (key == QLatin1String(lv.key)) return QCoreApplication::translate("qwhmi::alarm", lv.text);
    }
    return key;   // 未知 key 原样返回,不静默替换成某一档
}

QString dirDisplayName(const QString& key) {
    for (const DirItem& d : kDirections) {
        if (key == QLatin1String(d.key)) return QCoreApplication::translate("qwhmi::alarm", d.text);
    }
    return key;
}

} // namespace

AlarmConfigDialog::AlarmConfigDialog(AlarmManager* mgr,
                                     VariableManager* varMgr,
                                     QWidget* parent)
    : QDialog(parent), alarmMgr_(mgr), varMgr_(varMgr) {
    setWindowTitle(tr("报警配置"));
    setModal(true);
    resize(920, 520);

    table_       = new QTableWidget(this);
    infoLabel_   = new QLabel(this);
    hiddenLabel_ = new QLabel(this);
    jsonView_    = new QTextEdit(this);
    jsonView_->setReadOnly(true);
    jsonView_->setVisible(false);

    setupTable();
    setupForm();
    setupButtons();

    // ---- 布局 ----
    auto* formRow = new QHBoxLayout;
    formRow->addWidget(new QLabel(tr("关联变量:"), this));
    formRow->addWidget(varCombo_);
    formRow->addWidget(new QLabel(tr("等级:"), this));
    formRow->addWidget(levelCombo_);
    formRow->addWidget(new QLabel(tr("方向:"), this));
    formRow->addWidget(dirCombo_);
    formRow->addWidget(new QLabel(tr("阈值:"), this));
    formRow->addWidget(thresholdSpin_);
    formRow->addWidget(new QLabel(tr("最短持续(ms):"), this));
    formRow->addWidget(minDurSpin_);
    formRow->addWidget(new QLabel(tr("滞回:"), this));
    formRow->addWidget(hysteresisSpin_);

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(addBtn_);
    btnRow->addWidget(removeBtn_);
    btnRow->addWidget(exportBtn_);
    btnRow->addWidget(infoLabel_);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn_);

    auto* root = new QVBoxLayout(this);
    root->addWidget(table_);
    root->addWidget(hiddenLabel_);
    root->addLayout(formRow);
    root->addLayout(btnRow);
    root->addWidget(jsonView_);

    // ---- 信号 ----
    connect(closeBtn_,  &QPushButton::clicked, this, &QDialog::reject);
    connect(exportBtn_, &QPushButton::clicked, this, &AlarmConfigDialog::onExport);
    connect(addBtn_,    &QPushButton::clicked, this, &AlarmConfigDialog::onAdd);
    connect(removeBtn_, &QPushButton::clicked, this, &AlarmConfigDialog::onRemove);

    // 表单 → 草稿行。
    // 【为什么不用 editingFinished 而全用 valueChanged】
    // 双击表格切换行时,表单会被 fillFormFromRow() 覆写。如果覆写也会触发
    // valueChanged → writeFormToRow,就会把【刚填进去的值】又写回【上一行】。
    // 所以这里靠 onApplyFieldEdits() 一个显式入口 + 行切换时 blockSignals,
    // 而不是让控件自己喊。见 fillFormFromRow。
    connect(varCombo_,       &QComboBox::currentTextChanged,   this, &AlarmConfigDialog::onApplyFieldEdits);
    connect(levelCombo_,     &QComboBox::currentTextChanged,   this, &AlarmConfigDialog::onApplyFieldEdits);
    connect(dirCombo_,       &QComboBox::currentTextChanged,   this, &AlarmConfigDialog::onApplyFieldEdits);
    connect(thresholdSpin_,  &QDoubleSpinBox::valueChanged,    this, &AlarmConfigDialog::onApplyFieldEdits);
    connect(minDurSpin_,     &QSpinBox::valueChanged,          this, &AlarmConfigDialog::onApplyFieldEdits);
    connect(hysteresisSpin_, &QDoubleSpinBox::valueChanged,    this, &AlarmConfigDialog::onApplyFieldEdits);

    connect(table_, &QTableWidget::itemSelectionChanged, this, [this]() {
        fillFormFromRow(table_->currentRow());
    });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
        fillFormFromRow(r);
        onApplyFieldEdits();
    });

    reloadFromManager();
    setFormEnabled(false);   // 还没选中行
}

AlarmConfigDialog::~AlarmConfigDialog() = default;

// ============================================================================
// 表格 / 表单 / 按钮
// ============================================================================

void AlarmConfigDialog::setupTable() {
    table_->setColumnCount(8);
    table_->setHorizontalHeaderLabels(QStringList()
        << tr("报警名") << tr("关联变量") << tr("等级") << tr("方向")
        << tr("阈值") << tr("最短持续(ms)") << tr("滞回") << tr("已屏蔽"));
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    // 只读:所有修改走下面的表单,避免半截的单元格编辑改坏草稿
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
}

void AlarmConfigDialog::setupForm() {
    varCombo_ = new QComboBox(this);
    levelCombo_ = new QComboBox(this);
    dirCombo_  = new QComboBox(this);

    for (const LevelItem& lv : kLevels) levelCombo_->addItem(tr(lv.text), QString::fromLatin1(lv.key));
    for (const DirItem& d : kDirections) dirCombo_->addItem(tr(d.text), QString::fromLatin1(d.key));

    // 下拉存的是 key(英文),显示的是 currentText()(中文)
    levelCombo_->setCurrentIndex(1);   // Warning
    dirCombo_->setCurrentIndex(0);     // Above

    thresholdSpin_ = new QDoubleSpinBox(this);
    thresholdSpin_->setRange(-kMaxMagnitude, kMaxMagnitude);
    thresholdSpin_->setDecimals(3);
    thresholdSpin_->setToolTip(tr("Above 方向:值 > 阈值触发。Below 方向:值 < 阈值触发。"));

    minDurSpin_ = new QSpinBox(this);
    minDurSpin_->setRange(0, 600000);
    minDurSpin_->setSingleStep(100);
    minDurSpin_->setValue(500);
    minDurSpin_->setToolTip(tr("越线持续超过这个毫秒数才报,用来滤掉快速抖动。0 = 不防抖。"));

    hysteresisSpin_ = new QDoubleSpinBox(this);
    hysteresisSpin_->setRange(0.0, kMaxMagnitude);
    hysteresisSpin_->setDecimals(3);
    // 【tooltip 为什么写这么长】
    //   hysteresis 和 threshold 量纲相同但语义相反:threshold 是"越界点",
    //   hysteresis 是"死区宽度"。用户很容易以为把 hysteresis 填成 80
    //   就是"低于 80 才恢复",而实际上是"低于 阈值-80 才恢复"。
    //   这个误解会让报警该报不报 —— 所以在这里说清楚,而不是让用户猜。
    hysteresisSpin_->setToolTip(
        tr("死区宽度,用来防止数值在阈值附近反复触发/恢复。\n"
           "Above 方向:回落到「阈值 - 滞回」才算恢复。\n"
           "Below 方向:回升到「阈值 + 滞回」才算恢复。\n"
           "填 0 = 不启用滞回(与旧版本行为一致)。\n"
           "注意:它不是阈值本身,而是阈值周围的「死区宽度」。"));

    // 变量下拉:列出 varMgr 里所有变量。没有变量可选时对话框依然能开
    // (报警可能还没配变量),只是下拉为空。
    if (varMgr_) {
        for (Variable* v : varMgr_->allVariables()) {
            if (v) varCombo_->addItem(v->name());
        }
    }
    if (varCombo_->count() == 0) {
        varCombo_->addItem(tr("(工程里还没有变量)"));
        varCombo_->setEnabled(false);
    }
}

void AlarmConfigDialog::setupButtons() {
    addBtn_    = new QPushButton(tr("新增报警"), this);
    removeBtn_ = new QPushButton(tr("删除选中"), this);
    exportBtn_ = new QPushButton(tr("导出草稿 JSON"), this);
    closeBtn_  = new QPushButton(tr("关闭"), this);

    addBtn_->setToolTip(tr("在草稿里加一条新报警。\n"
                            "本对话框不改动运行中的报警 —— 应用是 F4-b 的活。"));
    removeBtn_->setToolTip(tr("从草稿里删除选中这条。\n"
                              "同样不改动运行中的报警。"));
    exportBtn_->setToolTip(tr("把当前草稿导出成 JSON。\n"
                              "字段与 Project::saveToFile 写入工程文件时完全一致。"));
}

// ============================================================================
// 从 AlarmManager 读成草稿
// ============================================================================

void AlarmConfigDialog::reloadFromManager() {
    rows_.clear();
    int hidden = 0;

    if (alarmMgr_) {
        // checkers() 返回 QHash::values(),【顺序不确定】—— 必须排序,
        // 否则每次打开对话框行序都不同,用户没法建立"这一行还是那一条"的印象。
        // 按名字排,和 UserManagementDialog::refreshTable 的做法一致。
        QList<AlarmChecker*> checkers = alarmMgr_->checkers();
        std::sort(checkers.begin(), checkers.end(),
                  [](const AlarmChecker* a, const AlarmChecker* b) {
                      return a && b && a->name() < b->name();
                  });

        for (AlarmChecker* c : checkers) {
            if (!c) continue;
            AlarmConfigRow row;
            row.name = c->name();

            auto* tc = dynamic_cast<ThresholdAlarmChecker*>(c);
            if (!tc) {
                // 非阈值报警(WriteFailChecker)。见头文件 AlarmConfigRow::editable。
                row.editable = false;
                row.shelved  = alarmMgr_->isShelved(row.name);
                ++hidden;
                rows_.append(row);
                continue;
            }
            row.variableName = tc->variable() ? tc->variable()->name() : QString();
            const CheckerConfig& cfg = tc->config();
            row.level         = JsonSerializer::alarmLevelToString(int(cfg.level));
            row.direction     = JsonSerializer::directionToString(int(cfg.direction));
            row.threshold     = cfg.threshold;
            row.minDurationMs = cfg.minDurationMs;
            row.hysteresis    = cfg.hysteresis;
            row.shelved       = alarmMgr_->isShelved(row.name);
            rows_.append(row);
        }
    }

    // ---- 填表 ----
    // 程序性填表期间禁止 itemChanged 回写,见 suppressItemChanged_ 的说明。
    suppressItemChanged_ = true;
    table_->setRowCount(rows_.size());
    for (int i = 0; i < rows_.size(); ++i) {
        const AlarmConfigRow& r = rows_.at(i);
        auto put = [&](int col, const QString& text) {
            auto* item = new QTableWidgetItem(text);
            if (!r.editable) item->setForeground(Qt::gray);
            table_->setItem(i, col, item);
        };
        put(kColName,       r.name);
        put(kColVariable,   r.variableName);
        put(kColLevel,      r.editable ? levelDisplayName(r.level) : tr("(非阈值)"));
        put(kColDir,        r.editable ? dirDisplayName(r.direction) : QString());
        put(kColThreshold,  r.editable ? QString::number(r.threshold, 'g', 6) : QString());
        put(kColMinDur,     r.editable ? QString::number(r.minDurationMs) : QString());
        put(kColHysteresis, r.editable ? QString::number(r.hysteresis, 'g', 6) : QString());
        put(kColShelved,    r.shelved ? tr("是") : QString());
    }

    suppressItemChanged_ = false;

    // ★ 隐藏项必须说出来。
    //   只显示能编辑的那些、悄悄少掉几条,用户会以为"工程里就这几条报警"——
    //   而实际上有一条写失败报警正在生效。这正是本项目反复吃的静默失败。
    hiddenLabel_->setVisible(hidden > 0);
    if (hidden > 0) {
        hiddenLabel_->setText(
            tr("另有 %1 条非阈值报警(如写失败报警)未显示 —— 它们没有阈值可配,"
               "但正在生效。完整清单见运行主画面。").arg(hidden));
        hiddenLabel_->setStyleSheet(QStringLiteral("color:#b06000;"));
    }

    updateInfo();
}

void AlarmConfigDialog::updateInfo() {
    const int editable = static_cast<int>(std::count_if(
        rows_.cbegin(), rows_.cend(), [](const AlarmConfigRow& r) { return r.editable; }));
    const int shelved = static_cast<int>(std::count_if(
        rows_.cbegin(), rows_.cend(), [](const AlarmConfigRow& r) { return r.shelved; }));

    // 屏蔽数单独显示 —— F2-c 的硬约束之一是"屏蔽必须可见",
    // 屏幕上不能一个被屏蔽的报警都看不出来(见 AlarmManager.h:101-110)。
    infoLabel_->setText(tr("共 %1 条报警(%2 条可配置)%3")
                            .arg(rows_.size())
                            .arg(editable)
                            .arg(shelved > 0 ? tr(",%1 条已屏蔽").arg(shelved) : QString()));
}

// ============================================================================
// 表单 ↔ 草稿行
// ============================================================================

void AlarmConfigDialog::setFormEnabled(bool on) {
    varCombo_->setEnabled(on && varCombo_->isEnabled());
    levelCombo_->setEnabled(on);
    dirCombo_->setEnabled(on);
    thresholdSpin_->setEnabled(on);
    minDurSpin_->setEnabled(on);
    hysteresisSpin_->setEnabled(on);
}

void AlarmConfigDialog::fillFormFromRow(int row) {
    if (row < 0 || row >= rows_.size()) {
        table_->clearSelection();
        setFormEnabled(false);
        return;
    }
    const AlarmConfigRow& r = rows_.at(row);

    // ★ 关键:先断开信号再填。
    //   fillFormFromRow 是由"选中行变了"触发的,不是用户改的。
    //   如果不 block,下面每设一个值都会触发 onApplyFieldEdits(),
    //   而那时 rows_[row] 还是【新】行、控件里的值来自【新】行 —— 看似无害;
    //   但 add/delete 路径里表格行号与 rows_ 索引会短暂错位,
    //   那时 writeFormToRow(row) 就会把数据写进错误的行。
    //   显式 block 让"用户改"和"程序填"两条路径彻底分开。
    const QList<QObject*> widgets = { varCombo_, levelCombo_, dirCombo_,
                                     thresholdSpin_, minDurSpin_, hysteresisSpin_ };
    for (QObject* w : widgets) w->blockSignals(true);

    const int varIdx = varCombo_->findText(r.variableName);
    if (varIdx >= 0) varCombo_->setCurrentIndex(varIdx);

    const int lvlIdx = levelCombo_->findData(r.level);
    if (lvlIdx >= 0) levelCombo_->setCurrentIndex(lvlIdx);

    const int dirIdx = dirCombo_->findData(r.direction);
    if (dirIdx >= 0) dirCombo_->setCurrentIndex(dirIdx);

    thresholdSpin_->setValue(r.threshold);
    minDurSpin_->setValue(r.minDurationMs);
    hysteresisSpin_->setValue(r.hysteresis);

    for (QObject* w : widgets) w->blockSignals(false);

    // 非阈值报警:能看到,但不许改
    setFormEnabled(r.editable);
    removeBtn_->setEnabled(r.editable);
}

void AlarmConfigDialog::writeFormToRow(int row) {
    if (row < 0 || row >= rows_.size()) return;
    AlarmConfigRow& r = rows_[row];
    if (!r.editable) return;   // 非阈值报警不接受表单改动

    // 变量:(工程里还没有变量时下拉是禁用的,此时保持原值)
    if (varCombo_->isEnabled()) {
        const QString v = varCombo_->currentText();
        if (!v.isEmpty() && !v.startsWith('(')) r.variableName = v;
    }
    r.level         = levelCombo_->currentData().toString();
    r.direction     = dirCombo_->currentData().toString();
    r.threshold     = thresholdSpin_->value();
    r.minDurationMs = minDurSpin_->value();
    r.hysteresis    = hysteresisSpin_->value();

    // ★ 写回表格。
    //   不回写的话,表格显示的还是改之前的值 —— 用户改了阈值但看着没变,
    //   导出 JSON 却是新值。这种"界面和实际状态不一致"是最难查的一类 bug。
    if (row < table_->rowCount()) {
        suppressItemChanged_ = true;   // 回写是程序性的,不能反过来触发自己
        if (auto* it = table_->item(row, kColVariable))   it->setText(r.variableName);
        if (auto* it = table_->item(row, kColLevel))      it->setText(levelDisplayName(r.level));
        if (auto* it = table_->item(row, kColDir))        it->setText(dirDisplayName(r.direction));
        if (auto* it = table_->item(row, kColThreshold))  it->setText(QString::number(r.threshold, 'g', 6));
        if (auto* it = table_->item(row, kColMinDur))     it->setText(QString::number(r.minDurationMs));
        if (auto* it = table_->item(row, kColHysteresis)) it->setText(QString::number(r.hysteresis, 'g', 6));
        suppressItemChanged_ = false;
    }
}

void AlarmConfigDialog::onApplyFieldEdits() {
    // 【这条槽只在"用户真的动了控件"时被调到】
    // 程序性填充在 fillFormFromRow 里被 blockSignals 挡掉了。
    writeFormToRow(table_->currentRow());
}

// ============================================================================
// 增 / 删 / 导出
// ============================================================================

void AlarmConfigDialog::onAdd() {
    AlarmConfigRow row;
    // 默认名给一个明确的占位,而不是 "NewAlarm" —— 用户看到
    // "新报警" 就知道这是待填的,看到 "NewAlarm" 可能以为已经是正式的了。
    row.name = tr("新报警");
    if (varCombo_->count() > 0 && varCombo_->isEnabled()) {
        row.variableName = varCombo_->currentText();
    }
    rows_.append(row);

    const int r = rows_.size() - 1;
    auto* nameItem = new QTableWidgetItem(row.name);
    // 名字必须双击改 —— 它是 CheckerConfig 之外的部分,表单里没有它的位置
    nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable);

    suppressItemChanged_ = true;   // 下面的 setItem 全是程序性的
    table_->insertRow(r);
    table_->setItem(r, kColName,       nameItem);
    table_->setItem(r, kColVariable,   new QTableWidgetItem(row.variableName));
    table_->setItem(r, kColLevel,      new QTableWidgetItem(levelDisplayName(row.level)));
    table_->setItem(r, kColDir,        new QTableWidgetItem(dirDisplayName(row.direction)));
    table_->setItem(r, kColThreshold,  new QTableWidgetItem(QString::number(row.threshold, 'g', 6)));
    table_->setItem(r, kColMinDur,     new QTableWidgetItem(QString::number(row.minDurationMs)));
    table_->setItem(r, kColHysteresis, new QTableWidgetItem(QString::number(row.hysteresis, 'g', 6)));
    table_->setItem(r, kColShelved,    new QTableWidgetItem(QString()));
    suppressItemChanged_ = false;

    // 名字改了要回写草稿,否则导出时还是占位名。
    //
    // ★ QTableWidgetItem 不是 QObject,【没有 changed 信号】——
    //   (这是本文件第一版的编译错误,写的时候想当然以为有。)
    //   正确做法是连 QTableWidget::itemChanged(QTableWidgetItem*),
    //   它给的是 item 指针,要自己反查行号。
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        // 程序性填表也会发这个信号 —— 那时不该回写,否则 fillFormFromRow
        // 里的 blockSignals 只挡住了表单,挡不住表格。
        if (suppressItemChanged_) return;
        if (!item || item->column() != kColName) return;
        const int row = table_->row(item);
        if (row < 0 || row >= rows_.size()) return;
        rows_[row].name = item->text();
    });

    table_->selectRow(r);
    fillFormFromRow(r);
    updateInfo();

    qCDebug(hmiAlarm) << "[F4-a] 新增报警草稿,当前共" << rows_.size() << "条";
}

void AlarmConfigDialog::onRemove() {
    const int row = table_->currentRow();
    if (row < 0 || row >= rows_.size()) {
        QMessageBox::information(this, tr("未选择"), tr("请先在表格里选中一行"));
        return;
    }
    const AlarmConfigRow& r = rows_.at(row);
    if (!r.editable) {
        QMessageBox::information(this, tr("无法删除"),
                                 tr("'%1' 是非阈值报警,不在本对话框的管辖范围内。\n"
                                    "它由系统自动管理。").arg(r.name));
        return;
    }
    if (QMessageBox::question(this, tr("确认删除"),
                               tr("从草稿里删除报警 '%1'?\n"
                                  "运行中的报警不受影响 —— 应用是后续步骤的事。").arg(r.name))
        != QMessageBox::Yes) {
        return;
    }
    rows_.removeAt(row);
    // removeRow 会让后面的行号整体前移,草稿和表格的索引必须【同时】动,
    // 所以先记住要删的行,再让表格删 —— 顺序反了会错位一行。
    table_->removeRow(row);

    table_->clearSelection();
    setFormEnabled(false);
    updateInfo();
}

void AlarmConfigDialog::onExport() {
    const QString json = exportDraftToJson();
    jsonView_->setPlainText(json);
    jsonView_->setVisible(!jsonView_->isVisible());

    // 同时进剪贴板 —— F4-b/F4-c 之前的过渡期,
    // 用户要验证草稿时最省事的动作就是粘回 JSON 文件对照。
    if (QClipboard* clip = QGuiApplication::clipboard()) {
        clip->setText(json);
    }
    qCDebug(hmiAlarm) << "[F4-a] 导出草稿 JSON,共" << rows_.size() << "条,已复制到剪贴板";
}

QString AlarmConfigDialog::exportDraftToJson() const {
    // ★ 复用 JsonSerializer,而不是自己拼 QJsonObject。
    //   Project::saveToFile(Project.cpp:320) 存报警走的是同一个 toJson(cfg)。
    //   这里用同一个函数,才能保证"对话框里看到的字段"和
    //   "存进工程文件的字段"永远一致 —— 否则改一处漏一处,
    //   用户会在这里配好、存进文件就丢。
    QJsonArray arr;
    for (const AlarmConfigRow& r : rows_) {
        if (!r.editable) continue;   // 非阈值报警本来就存不进 alarms[]

        CheckerConfig cfg;
        cfg.level         = JsonSerializer::alarmLevelFromString(r.level);
        cfg.direction     = JsonSerializer::directionFromString(r.direction);
        cfg.threshold     = r.threshold;
        cfg.minDurationMs = r.minDurationMs;
        cfg.hysteresis    = r.hysteresis;

        QJsonObject obj = JsonSerializer::toJson(cfg);
        obj["name"]     = r.name;
        obj["variable"] = r.variableName;
        arr.append(obj);
    }
    QJsonObject root;
    root["alarms"] = arr;
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

// ============================================================================
// 权限
// ============================================================================

int AlarmConfigDialog::exec() {
    // ★ EditProject —— Permission.h:35 定义了但全仓没有第二个使用者。
    //   选它而不是新增一个权限:改报警配置就是改工程行为,语义上正是它。
    //   Operator 能不能看报警列表?能(运行时那套)。
    //   但能不能【改】一条报警的阈值 —— 不能,那是工程师的活。
    if (!qwhmi::auth::checkPermission(qwhmi::auth::CurrentUser::instance().role(),
                                      qwhmi::auth::Permission::EditProject)) {
        QMessageBox::warning(this, tr("权限不足"), tr("只有工程师/管理员才能修改报警配置"));
        return QDialog::Rejected;
    }
    reloadFromManager();
    return QDialog::exec();
}

// ============================================================================
// 只读访问器(F4-a 的验收面)
// ============================================================================

int AlarmConfigDialog::rowCount() const { return static_cast<int>(rows_.size()); }

QString AlarmConfigDialog::nameAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).name : QString();
}

QString AlarmConfigDialog::variableAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).variableName : QString();
}

QString AlarmConfigDialog::levelAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).level : QString();
}

QString AlarmConfigDialog::directionAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).direction : QString();
}

double AlarmConfigDialog::thresholdAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).threshold : 0.0;
}

int AlarmConfigDialog::minDurationMsAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).minDurationMs : 0;
}

double AlarmConfigDialog::hysteresisAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).hysteresis : 0.0;
}

bool AlarmConfigDialog::shelvedAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).shelved : false;
}

bool AlarmConfigDialog::editableAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).editable : false;
}

// ============================================================================
// 小工具
// ============================================================================

} // namespace qwhmi::alarm
