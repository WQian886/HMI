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
#include "../../Project.h"
#include "../../ThresholdAlarmChecker.h"
#include "../../VariableManager.h"
#include "../auth/CurrentUser.h"
#include "../auth/Permission.h"
#include "../logging/Logging.h"

#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
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
    btnRow->addWidget(saveBtn_);
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
    connect(saveBtn_,   &QPushButton::clicked, this, &AlarmConfigDialog::onSaveToProject);
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

    // 名字改了要回写草稿,否则导出时还是占位名。
    //
    // ★ QTableWidgetItem 不是 QObject,【没有 changed 信号】——
    //   (这是本文件第一版的编译错误,写的时候想当然以为有。)
    //   正确做法是连 QTableWidget::itemChanged(QTableWidgetItem*),
    //   它给的是 item 指针,要自己反查行号。
    //
    // ★ 为什么必须连在这里、而不是 onAdd() 里:
    //   上一版把它写在 onAdd() 里,于是每点一次"新增"就多挂一个 lambda。
    //   点 N 次 = N 个处理器,每次改名字都跑 N 遍 —— 越用越慢,
    //   而且这 N 个 lambda 全部捕获 this,dialog 活着的期间一直挂着。
    //   连一次就够。
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        // 程序性填表也会发这个信号 —— 那时不该回写,否则 fillFormFromRow
        // 里的 blockSignals 只挡住了表单,挡不住表格。
        if (suppressItemChanged_) return;
        if (!item || item->column() != kColName) return;
        const int row = table_->row(item);
        if (row < 0 || row >= rows_.size()) return;
        rows_[row].name = item->text();
    });

    reloadFromManager();
    setFormEnabled(false);   // 还没选中行
}

AlarmConfigDialog::~AlarmConfigDialog() = default;

// ============================================================================
// 表格 / 表单 / 按钮
// ============================================================================

void AlarmConfigDialog::setupTable() {
    // ★ objectName 不是给用户看的,是给测试找控件用的。
    //   没有它,测试只能靠 findChildren 按顺序猜哪个是哪个 ——
    //   猜错就断言到别的行上,而测试还是绿的(同 tst_AlarmConfigDialog.h 头注释
    //   里"不反射 private 成员"同一个坑的另一面)。
    table_->setObjectName(QStringLiteral("alarmConfigTable"));
    table_->setColumnCount(8);
    table_->setHorizontalHeaderLabels(QStringList()
        << tr("报警名") << tr("关联变量") << tr("等级") << tr("方向")
        << tr("阈值") << tr("最短持续(ms)") << tr("滞回") << tr("已屏蔽"));
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    // ★ 名字必须能双击改。
    //   【上一版的真实缺陷】这里设的是 NoEditTriggers,只在 onAdd 里给
    //   新增行的 nameItem 单独补了个 Qt::ItemIsEditable 标志 ——
    //   于是【从工程文件读进来的所有报警,名字都改不了】,
    //   只有本次会话新加的那条能改。界面上没有任何提示,
    //   用户只能看着自己的编辑不生效(2026-10-08 用户实测反馈)。
    //
    //   现在改成"视图允许编辑 + 只给名字列留 ItemIsEditable",
    //   其余列在填表时显式清掉该标志(见 reloadFromManager 的 put())。
    table_->setEditTriggers(QAbstractItemView::DoubleClicked
                            | QAbstractItemView::EditKeyPressed
                            | QAbstractItemView::AnyKeyPressed);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
}

void AlarmConfigDialog::setupForm() {
    varCombo_ = new QComboBox(this);
    levelCombo_ = new QComboBox(this);
    dirCombo_  = new QComboBox(this);
    varCombo_->setObjectName(QStringLiteral("alarmConfigVarCombo"));
    levelCombo_->setObjectName(QStringLiteral("alarmConfigLevelCombo"));
    dirCombo_->setObjectName(QStringLiteral("alarmConfigDirCombo"));

    for (const LevelItem& lv : kLevels) levelCombo_->addItem(tr(lv.text), QString::fromLatin1(lv.key));
    for (const DirItem& d : kDirections) dirCombo_->addItem(tr(d.text), QString::fromLatin1(d.key));

    // 下拉存的是 key(英文),显示的是 currentText()(中文)
    levelCombo_->setCurrentIndex(1);   // Warning
    dirCombo_->setCurrentIndex(0);     // Above

    thresholdSpin_ = new QDoubleSpinBox(this);
    thresholdSpin_->setRange(-kMaxMagnitude, kMaxMagnitude);
    thresholdSpin_->setDecimals(3);
    thresholdSpin_->setObjectName(QStringLiteral("alarmConfigThresholdSpin"));
    thresholdSpin_->setToolTip(tr("Above 方向:值 > 阈值触发。Below 方向:值 < 阈值触发。"));

    minDurSpin_ = new QSpinBox(this);
    minDurSpin_->setRange(0, 600000);
    minDurSpin_->setSingleStep(100);
    minDurSpin_->setValue(500);
    minDurSpin_->setObjectName(QStringLiteral("alarmConfigMinDurSpin"));
    minDurSpin_->setToolTip(tr("越线持续超过这个毫秒数才报,用来滤掉快速抖动。0 = 不防抖。"));

    hysteresisSpin_ = new QDoubleSpinBox(this);
    hysteresisSpin_->setRange(0.0, kMaxMagnitude);
    hysteresisSpin_->setDecimals(3);
    hysteresisSpin_->setObjectName(QStringLiteral("alarmConfigHysteresisSpin"));
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
    // ★ hasVariables_ = "工程里到底有没有变量" —— 这是个【静态事实】,
    //   只在这里算一次。它必须和 setFormEnabled 里的"当前能不能操作"分开:
    //   上一版把两者混成 varCombo_->isEnabled(),结果组合框被自己锁死。
    hasVariables_ = (varCombo_->count() > 0);
    if (!hasVariables_) {
        varCombo_->addItem(tr("(工程里还没有变量)"));
        varCombo_->setEnabled(false);
    }
}

void AlarmConfigDialog::setupButtons() {
    addBtn_    = new QPushButton(tr("新增报警"), this);
    removeBtn_ = new QPushButton(tr("删除选中"), this);
    exportBtn_ = new QPushButton(tr("导出草稿 JSON"), this);
    saveBtn_   = new QPushButton(tr("保存到工程文件"), this);
    closeBtn_  = new QPushButton(tr("关闭"), this);
    addBtn_->setObjectName(QStringLiteral("alarmConfigAddBtn"));
    removeBtn_->setObjectName(QStringLiteral("alarmConfigRemoveBtn"));
    exportBtn_->setObjectName(QStringLiteral("alarmConfigExportBtn"));
    closeBtn_->setObjectName(QStringLiteral("alarmConfigCloseBtn"));

    addBtn_->setToolTip(tr("在草稿里加一条新报警。\n"
                            "本对话框不改动运行中的报警 —— 应用是 F4-b 的活。"));
    removeBtn_->setToolTip(tr("从草稿里删除选中这条。\n"
                              "同样不改动运行中的报警。"));
    exportBtn_->setToolTip(tr("把当前草稿导出成 JSON。\n"
                              "字段与 Project::saveToFile 写入工程文件时完全一致。"));
    // ★ 这是真正把草稿落地的按钮 —— 导出只是"看一眼 + 复制到剪贴板"。
    saveBtn_->setObjectName(QStringLiteral("alarmConfigSaveBtn"));
    saveBtn_->setToolTip(tr("把当前草稿写进已有工程文件的 alarms 段。\n"
                            "只改报警,变量/画面/联动一律不动。\n"
                            "运行中的报警不受影响 —— 要生效需重新载入工程。"));
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
        // ★ 只有名字列可编辑,其余列一律清掉 ItemIsEditable。
        //   表格是"允许编辑"的了(见 setupTable),所以标志必须逐格收紧 ——
        //   否则用户能直接把"阈值"那一格改成乱码,而那个值不会进草稿。
        auto put = [&](int col, const QString& text) {
            auto* item = new QTableWidgetItem(text);
            Qt::ItemFlags f = item->flags();
            f &= ~Qt::ItemIsEditable;                 // 默认全部只读
            if (col == kColName && r.editable) f |= Qt::ItemIsEditable;
            item->setFlags(f);
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
    // ★ 这里曾经写的是 varCombo_->setEnabled(on && varCombo_->isEnabled())。
    //   【那是 2026-10-08 用户报的"新建报警时变量不能选"的根因】
    //   自引用 = 单向门闩:组合框一旦被禁用,就再也回不来了。
    //   而构造函数末尾会调 setFormEnabled(false)(还没选中行),
    //   于是它从打开对话框那一刻起就永久变灰 —— 选中任何一行都不会恢复。
    //   用户看到的就是:等级/方向/阈值/滞回都能改,唯独"关联变量"点不动。
    //
    //   【正确写法】把"工程里有没有变量"这个【静态事实】记在 hasVariables_,
    //   和"当前能不能操作"这个【瞬时状态】分开。两个概念混在一起必然出这种 bug。
    varCombo_->setEnabled(on && hasVariables_);
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

    // 变量。
    // ★ 这里原来判断的是 varCombo_->isEnabled(),和 setFormEnabled 是同一个坑。
    //   "下拉当前能不能操作"是【瞬时状态】(没选中行时为灰),不是"工程里有没有变量"。
    //   后果:即便组合框修好了,这里仍会因为"恰好处于禁用态"而跳过写入 ——
    //   用户选了变量,草稿却没记,存出去还是旧变量。
    //   正确的判据是 hasVariables_(静态事实)。空工程时保持原值即可。
    if (hasVariables_) {
        const QString v = varCombo_->currentText();
        // startsWith('(') 排除占位项"(工程里还没有变量)"
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

// 找一个没被占用的草稿名:"新报警"、"新报警 2"、"新报警 3"……
// 【为什么要唯一】名字是 checkers_ 的 key。三条同名报警导进工程文件后,
//   后者会覆盖前者,而【不会报任何错】—— 存的时候三条,读回来一条。
//   静默丢失比报错难查得多,所以宁可名字丑一点也不能重名。
QString AlarmConfigDialog::nextFreeDraftName() const {
    const QString base = tr("新报警");
    QSet<QString> used;
    for (const AlarmConfigRow& r : rows_) used.insert(r.name);

    if (!used.contains(base)) return base;
    for (int i = 2; i < 10000; ++i) {
        const QString candidate = QStringLiteral("%1 %2").arg(base).arg(i);
        if (!used.contains(candidate)) return candidate;
    }
    // 理论上到不了这里(要有一万个同名草稿)。返回一个一定没被用过的,
    // 宁可名字难看也不能重名。
    return QStringLiteral("%1 %2").arg(base).arg(rows_.size());
}

void AlarmConfigDialog::onAdd() {
    AlarmConfigRow row;
    // ★ 名字必须【唯一】,不能每条都叫"新报警"。
    //   名字是 AlarmManager 里 checkers_ 的 key(见 AlarmManager.h 的 shelve/find),
    //   草稿导出后 Project::loadFromFile 也按它建 checker。
    //   三条同名 → 加载时后者覆盖前者 → 存的时候看着三条,读回来只剩一条,
    //   而且【不报错】。这是最该避免的一类静默丢失。
    //   所以这里取第一个没被占用的编号,用户看到 "新报警 2" 就知道是待填的第二条。
    row.name = nextFreeDraftName();
    // ★ 新行必须带上一个【真实的】变量名。
    //   【为什么不能靠 isEnabled()】varCombo_ 的 enabled 状态跟着"有没有选中行"走
    //   (setFormEnabled)。点新增时恰好没有选中行 → 组合框是灰的 → isEnabled() 为假
    //   → 新行的 variableName 永远是空的。
    //   而空变量名的报警一旦存进工程文件,Project.cpp:211 会让【整个工程打不开】。
    //   所以这里看的是"有没有变量可选"(count),不是"现在能不能操作"。
    //
    //   【为什么不能留空让用户自己填】其实可以,但新增的行立刻就处于
    //   "存不下去"的状态,用户会以为功能坏了。给个默认值,
    //   至少新增这条路是通的;真要换,选中行后在表单里改。
    if (varCombo_->count() > 0) {
        const QString candidate = varCombo_->currentText();
        if (!candidate.isEmpty() && !candidate.startsWith('(')) {
            row.variableName = candidate;
        }
    }
    rows_.append(row);

    const int r = rows_.size() - 1;

    suppressItemChanged_ = true;   // 下面的 setItem 全是程序性的
    table_->insertRow(r);
    // ★ 只有名字可编辑 —— 与 reloadFromManager 的 put() 保持同一套规则。
    //   表格是"允许编辑"的,标志必须逐格收紧,否则用户能把阈值那格改成乱码,
    //   而那个值【不会】进草稿 —— 看着改了,导出还是老值。
    auto put = [&](int col, const QString& text) {
        auto* item = new QTableWidgetItem(text);
        Qt::ItemFlags f = item->flags();
        f &= ~Qt::ItemIsEditable;
        if (col == kColName) f |= Qt::ItemIsEditable;
        item->setFlags(f);
        table_->setItem(r, col, item);
    };
    put(kColName,       row.name);
    put(kColVariable,   row.variableName);
    put(kColLevel,      levelDisplayName(row.level));
    put(kColDir,        dirDisplayName(row.direction));
    put(kColThreshold,  QString::number(row.threshold, 'g', 6));
    put(kColMinDur,     QString::number(row.minDurationMs));
    put(kColHysteresis, QString::number(row.hysteresis, 'g', 6));
    put(kColShelved,    QString());
    suppressItemChanged_ = false;

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

// ============================================================================
// ★ F4-c:落地
// ============================================================================

bool AlarmConfigDialog::validateDraft(QString* errorOut) const {
    // 先让 Project 查名字(空 / 重名)。它是 static,纯 JSON 层面就能查。
    const QJsonArray arr = QJsonDocument::fromJson(exportDraftToJson().toUtf8())
                               .object()["alarms"].toArray();
    QString nameErr;
    if (!Project::validateAlarmsArray(arr, &nameErr)) {
        if (errorOut) *errorOut = nameErr;
        return false;
    }

    // 再查变量存在性 —— 这一条 static 方法查不了(要 VariableManager)。
    // ★ 为什么这条最要紧:Project.cpp:211 加载时遇到不存在的 variable 会
    //   return false,整个工程文件【打不开】。不是"这一条报警有问题",
    //   是用户的画面、变量、联动全部一起打不开。
    if (varMgr_) {
        for (int i = 0; i < rows_.size(); ++i) {
            const AlarmConfigRow& r = rows_.at(i);
            if (!r.editable) continue;
            if (r.variableName.isEmpty() || !varMgr_->findVariable(r.variableName)) {
                if (errorOut) {
                    *errorOut = tr("报警 \"%1\" 没有关联到有效变量 —— %2\n\n"
                                   "工程文件里报警引用了不存在的变量会导致【整个工程打不开】,"
                                   "所以这里必须先拦下来。")
                                    .arg(r.name,
                                         r.variableName.isEmpty()
                                             ? tr("关联变量是空的")
                                             : tr("变量 \"%1\" 在工程里不存在").arg(r.variableName));
                }
                return false;
            }
        }
    }

    if (errorOut) errorOut->clear();
    return true;
}

bool AlarmConfigDialog::saveDraftToProjectFile(const QString& path, QString* errorOut) {
    if (path.isEmpty()) {
        if (errorOut) *errorOut = tr("没有指定工程文件路径");
        return false;
    }

    // 存之前先校验。坏草稿绝不能落盘 ——
    // 落下去的不是"报警配错了",而是"工程打不开了"。
    QString err;
    if (!validateDraft(&err)) {
        if (errorOut) *errorOut = err;
        return false;
    }

    const QJsonArray arr = QJsonDocument::fromJson(exportDraftToJson().toUtf8())
                               .object()["alarms"].toArray();
    return Project::replaceAlarmsInFile(path, arr, errorOut);
}

void AlarmConfigDialog::onSaveToProject() {
    // QFileDialog 在 offscreen 下没法交互,所以真正的落盘走
    // saveDraftToProjectFile(路径),它才是被测的那一层。
    // ★ 注意这里【不】调 reloadFromManager():草稿已经落盘,
    //   而重读会把【运行中】的旧状态灌回来,等于自己把刚存的东西抹掉。
    const QString path = QFileDialog::getSaveFileName(
        this, tr("把报警配置存进工程文件"), QString(),
        tr("HMI 工程文件 (*.json);;所有文件 (*.*)"));
    if (path.isEmpty()) return;   // 用户按取消

    QString err;
    if (saveDraftToProjectFile(path, &err)) {
        QMessageBox::information(
            this, tr("已保存"),
            tr("报警配置已写入:\n%1\n\n"
               "只替换了 alarms 段,变量/画面/联动都没动。\n\n"
               "注意:运行中的报警【不受影响】。要让新配置生效,"
               "请在主界面重新载入这个工程文件。").arg(path));
        qCDebug(hmiAlarm) << "[F4-c] 报警草稿已写入工程文件:" << path
                          << "共" << rows_.size() << "条";
    } else {
        QMessageBox::warning(this, tr("保存失败"), err);
        qCWarning(hmiAlarm) << "[F4-c] 保存失败:" << err;
    }
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
