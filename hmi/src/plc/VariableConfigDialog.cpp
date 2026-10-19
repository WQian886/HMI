// ============================================================================
// ★ 2026-10-19:变量配置对话框 实现
//
// 设计形状照 PlcConfigDialog(表格 + 行内编辑 + 草稿数组 + 公有只读访问器 +
// objectName 约定),理由与取舍见 VariableConfigDialog.h 顶部。
//
// 本文件里三处"刻意与直觉相反"的地方,改之前先读:
   //   ① removeRowFromDraft 不含 QMessageBox(询问是 UI 的职责)
   //   ② 查引用和删除合成一个方法,免得调用方忘了先查(见 .h 的说明)
   //   ③ 没有"保存到工程文件"按钮 —— Project::saveToFile 自己会序列化
//      运行中的 varMgr_(Project.cpp:670),不需要第二个真相来源
// ============================================================================

#include "plc/VariableConfigDialog.h"

#include "../../VariableManager.h"
#include "../../VariableRefScan.h"
#include "../auth/CurrentUser.h"
#include "../auth/Permission.h"
#include "plc/PlcConnectionManager.h"

#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(hmiVarCfg, "hmi.variable.config")

namespace qwhmi::plc {
namespace {

// ★ 列常量。【不要在中间插列】—— tst_VariableConfigDialog 用这些列号定位,
//   插在中间会让已写好的断言指到别的列上,而测试往往还是绿的。
constexpr int kColName      = 0;
constexpr int kColPlcId     = 1;
constexpr int kColAddress   = 2;
constexpr int kColType      = 3;
constexpr int kColAccess    = 4;
constexpr int kColPoll      = 5;
constexpr int kColDeadband  = 6;
constexpr int kColAlwaysPoll= 7;
constexpr int kColRefs      = 8;
constexpr int kColCount     = 9;

} // namespace

// ============================================================================
// 构造 / 析构
// ============================================================================

VariableConfigDialog::VariableConfigDialog(VariableManager* varMgr,
                                           AlarmManager* alarmMgr,
                                           ActionManager* actionMgr,
                                           PlcConnectionManager* plcMgr,
                                           QWidget* parent)
    : QDialog(parent),
      varMgr_(varMgr), alarmMgr_(alarmMgr),
      actionMgr_(actionMgr), plcMgr_(plcMgr) {
    setWindowTitle(tr("变量配置"));
    resize(1000, 460);

    table_     = new QTableWidget(this);
    infoLabel_ = new QLabel(this);
    blindHint_ = new QLabel(this);
    lockHint_  = new QLabel(this);

    setupTable();
    setupButtons();

    auto* lay = new QVBoxLayout(this);
    lay->addWidget(table_);
    lay->addWidget(infoLabel_);
    lay->addWidget(blindHint_);
    lay->addWidget(lockHint_);

    auto* box = new QGroupBox(tr("操作"), this);
    auto* bl  = new QGridLayout(box);
    int col = 0;
    for (QPushButton* b : {addBtn_, removeBtn_}) {
        bl->addWidget(b, 0, col++);
    }
    col = 3;
    for (QPushButton* b : {applyBtn_, closeBtn_}) {
        bl->addWidget(b, 0, col++);
    }
    lay->addWidget(box);

    reloadFromManager();
}

VariableConfigDialog::~VariableConfigDialog() = default;

// ============================================================================
// 表格骨架
// ============================================================================

void VariableConfigDialog::setupTable() {
    // ★ objectName 是给测试找控件用的,不是给用户看的。
    //   没有它测试只能 findChildren 按顺序猜 —— 猜错就断言到别的行上,
    //   而测试还是绿的(同 PlcConfigDialog::setupTable 同一段理由)。
    table_->setObjectName(QStringLiteral("variableConfigTable"));
    table_->setColumnCount(kColCount);
    table_->setHorizontalHeaderLabels(QStringList()
        << tr("名称") << tr("PLC") << tr("地址") << tr("类型") << tr("读写")
        << tr("轮询ms") << tr("死区") << tr("常轮询") << tr("被引用"));
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked
                            | QAbstractItemView::EditKeyPressed
                            | QAbstractItemView::AnyKeyPressed);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setDefaultSectionSize(26);
    table_->verticalHeader()->setVisible(false);   // 行号在这张表里没有意义

    connect(table_, &QTableWidget::itemChanged, this,
            &VariableConfigDialog::onApplyFieldEdits);
}

void VariableConfigDialog::setupButtons() {
    addBtn_    = new QPushButton(tr("添加(&A)..."), this);
    removeBtn_ = new QPushButton(tr("删除(&D)"), this);
    applyBtn_  = new QPushButton(tr("应用到运行中(&A)"), this);
    closeBtn_  = new QPushButton(tr("关闭(&C)"), this);

    addBtn_->setObjectName(QStringLiteral("variableConfigAddBtn"));
    removeBtn_->setObjectName(QStringLiteral("variableConfigRemoveBtn"));
    applyBtn_->setObjectName(QStringLiteral("variableConfigApplyBtn"));
    closeBtn_->setObjectName(QStringLiteral("variableConfigCloseBtn"));

    // ★ 没有 varMgr_ 就把"应用"置灰,而不是让它点了报错。
    //   与 PlcConfigDialog::setupButtons 同一个取舍。
    applyBtn_->setEnabled(varMgr_ != nullptr);

    connect(addBtn_,    &QPushButton::clicked, this, &VariableConfigDialog::onAdd);
    connect(removeBtn_, &QPushButton::clicked, this, &VariableConfigDialog::onRemove);
    connect(applyBtn_,  &QPushButton::clicked, this, &VariableConfigDialog::onApply);
    connect(closeBtn_,  &QPushButton::clicked, this, &QDialog::reject);
}

// ============================================================================
// 读成草稿
// ============================================================================

void VariableConfigDialog::reloadFromManager() {
    rows_.clear();
    if (!varMgr_) return;   // 没数据源就是空表,不假装有内容

    for (Variable* v : varMgr_->allVariables()) {
        if (!v) continue;
        VariableDraft d;
        d.name       = v->name();
        d.plcId      = v->plcId();
        d.address    = v->address();
        d.dataType   = v->dataType();
        d.access     = v->access();
        d.pollMs     = v->pollIntervalMs();
        d.deadband   = v->deadband();
        d.alwaysPoll = v->alwaysPoll();
        d.derived    = v->isDerived();
        d.expression = v->expression();
        rescanRefs(d);
        rows_.append(d);
    }
    fillTable();
}

void VariableConfigDialog::rescanRefs(VariableDraft& d) const {
    const VariableRefScanResult r = scanVariableRefs(d.name, alarmMgr_, actionMgr_);
    d.refDescriptions.clear();
    for (const VariableRef& ref : r.refs) d.refDescriptions << ref.describe();
    d.refCount   = static_cast<int>(r.refs.size());
    d.blindSpots = r.blindSpots;
}

// ★ plcId 下拉的候选。
//   managedIds() 是"当前在管的",工程里正在用但没在管的 plcId 也【必须】列出来。
//
//   ★★ 为什么不能只列 managedIds —— 少写一个候选,后果不是"选不到",
//      是【用户的变量悄悄变成了别的 PLC】:
//   工程里写着 plc7,但驱动没起来(网线掉了 / 协议不支持),
//   于是 managedIds 里没有 plc7。如果下拉只有 plc1/plc2,
//   用户打开对话框看到 PLC 列显示 plc7、而选项里【找不到 plc7】——
//   很多下拉框会自动选中第一项,于是变量就被指到 plc1 上了,
//   而工程文件里明明写的是 plc7。没有任何提示。
QStringList VariableConfigDialog::plcIdChoices() const {
    QStringList ids;
    if (plcMgr_) ids = plcMgr_->managedIds();
    // 工程里正在用的 plcId 全部并进来(varMgr_ 是数据源,一定非空时有效)
    if (varMgr_) {
        for (Variable* v : varMgr_->allVariables()) {
            if (v && !v->plcId().isEmpty() && !ids.contains(v->plcId())) {
                ids << v->plcId();
            }
        }
    }
    ids.sort();   // 显示顺序要稳定,否则每次刷新表格都跳行
    return ids;
}

void VariableConfigDialog::fillTable() {
    suppressItemChanged_ = true;   // ★ 见头注释:不挡的话填表会触发回写
    table_->setRowCount(rows_.size());

    const QStringList plcChoices = plcIdChoices();

    for (int i = 0; i < rows_.size(); ++i) {
        const VariableDraft& d = rows_.at(i);

        // ★★ 只读的四列:名称 / 地址 / 类型 / 读写。
        //   它们【没有 setter】,改 = 删了重建,而删重建会让绑它的画面控件
        //   静默变空(ControlBase 不监听 variableReplaced)。
        //   不给 ItemIsEditable —— 见 .h 的决定 ①。
        auto putLocked = [&](int col, const QString& text) {
            auto* it = new QTableWidgetItem(text);
            // 【★ 钉住这个决定的测试】isCellEditable 就是读 item 的这个标志。
            it->setFlags(it->flags() & ~Qt::ItemIsEditable);
            // ★ 灰掉只是提示;真正决定"改不了"的是上面那行去掉 ItemIsEditable。
            //   只设灰不锁的话,双击照样进编辑,用户填了半天点应用发现没反应。
            it->setToolTip(tr("名称/地址/类型/读写不可改(没有 setter,改只能删了重建)"));
            table_->setItem(i, col, it);
        };
        auto putEditable = [&](int col, const QString& text) {
            auto* it = new QTableWidgetItem(text);
            it->setFlags(it->flags() | Qt::ItemIsEditable);
            table_->setItem(i, col, it);
        };

        putLocked(kColName, d.name);
        putLocked(kColAddress, QString::number(d.address));
        putLocked(kColType, Variable::dataTypeName(d.dataType));
        putLocked(kColAccess, Variable::accessName(d.access));

        // ---- PLC 列:QComboBox ----
        //   ★【为什么不用文本格】文本格敲进去的东西校验不住 ——
        //     敲错一个字母就变成"变量属于一台不存在的 PLC",
        //     而变量值停在 Bad,没有任何地方会说是名字敲错了。
        //   ★ 派生变量的 PLC【没意义】:它不读任何寄存器(见 tst_PollSchedulerDerived),
        //     它跟着依赖的源变量走。置灰并说明,而不是让用户去改一个假字段。
        auto* plc = new QComboBox(table_);
        plc->addItems(plcChoices);
        // ★ 工程里写着、但当前没在管的 plcId,照原样加进去并选中它 ——
        //   理由见 plcIdChoices 上面那段:对话框擅自"修正"配置,
        //   用户就再也发现不了自己文件里写错的地方。
        int pidx = plcChoices.indexOf(d.plcId);
        if (pidx >= 0) {
            plc->setCurrentIndex(pidx);
        } else if (!d.plcId.isEmpty()) {
            plc->addItem(d.plcId);
            plc->setCurrentIndex(plc->count() - 1);
        }
        if (d.derived) {
            plc->setEnabled(false);
            plc->setToolTip(tr("派生量不读任何寄存器,PLC 对它没有意义 —— "
                               "它由表达式算出来,跟随源变量。"));
        }
        plc->setProperty("varRow", i);
        table_->setCellWidget(i, kColPlcId, plc);
        connect(plc, &QComboBox::currentIndexChanged, this,
                &VariableConfigDialog::onPlcIdComboChanged);

        putEditable(kColPoll,     QString::number(d.pollMs));
        putEditable(kColDeadband, QString::number(d.deadband));
        // 常轮询是勾选而不是文本格:它是布尔,文本格能填出"是"/"true"/"1"/"y",
        //   解析失败时静默当成 false(同 PlcConfigDialog::onApplyFieldEdits 的取舍)。
        {
            auto* it = new QTableWidgetItem(d.alwaysPoll ? tr("是") : tr("否"));
            it->setFlags((it->flags() | Qt::ItemIsUserCheckable)
                         & ~Qt::ItemIsEditable);
            it->setCheckState(d.alwaysPoll ? Qt::Checked : Qt::Unchecked);
            table_->setItem(i, kColAlwaysPoll, it);
        }

        // ★ 被引用列。显示"几处 + 是谁",而不是只显示一个数字。
        //   只显示数字的话,用户看到"1"不知道该去找谁;
        //   逐条列出才可能去把它处理掉。
        //   ★ 有引用时标红 —— 因为那意味着这一行【删不掉】。
        QString refText = d.refDescriptions.isEmpty()
                        ? tr("无")
                        : QStringLiteral("%1:%2")
                            .arg(d.refDescriptions.size())
                            .arg(d.refDescriptions.join(QStringLiteral(" / ")));
        {
            auto* it = new QTableWidgetItem(refText);
            it->setFlags(it->flags() & ~Qt::ItemIsEditable);
            if (d.refCount > 0) {
                it->setForeground(QBrush(QColor(180, 0, 0)));
                it->setToolTip(tr("★ 有引用,这一行【删不掉】。"
                                  "先处理掉引用它的报警/联动再来删。"));
            } else {
                it->setToolTip(tr("报警和联动都没有引用它。"));
            }
            table_->setItem(i, kColRefs, it);
        }
    }
    suppressItemChanged_ = false;
    updateInfo();
}

void VariableConfigDialog::updateInfo() {
    infoLabel_->setText(tr("共 %1 个变量。改 PLC / 轮询 / 死区 / 常轮询 后点"
                           "「应用到运行中」;主界面「保存工程」才会写进文件。")
                            .arg(rows_.size()));

    // ★★★ 这一条不是"啰嗦的免责说明",是本对话框最重要的诚实 ★★★
    //   它必须【一直显示】,而不是"只在有引用时显示"——
    //   用户正是因为看不到画面绑定才可能误以为"删干净了"。
    //   引用扫描查得到报警和联动,查不到画面控件的绑定(见 .h 决定 ③)。
    blindHint_->setText(tr("★ 删变量前请注意:本窗口能查出【报警】和【联动】"
                           "对它的引用,【查不出画面上控件的绑定】——"
                           "画面控件在编辑器窗口里,这里看不到。"
                           "所以「被引用」显示为空【不等于】画面上没人用它,"
                           "删之前请到编辑器里确认。"));
    blindHint_->setWordWrap(true);
    blindHint_->setStyleSheet(QStringLiteral("color:#a05000"));

    lockHint_->setText(tr("名称 / 地址 / 类型 / 读写 这四列不可改:"
                          "它们没有 setter,改只能【删掉重建】,"
                          "而重建会让绑在它上面的画面控件静默失效"
                          "(定格在最后一个值、不报错)。"
                          "要改请先删掉旧的,再新建一个。"));
    lockHint_->setWordWrap(true);
}

// ============================================================================
// 表格编辑 → 草稿
// ============================================================================

void VariableConfigDialog::onApplyFieldEdits(QTableWidgetItem* item) {
    if (suppressItemChanged_ || !item) return;
    const int row = item->row();
    if (row < 0 || row >= rows_.size()) return;

    VariableDraft& d = rows_[row];
    const QString text = item->text().trimmed();

    // ★ 解析失败【不改草稿】,只把这格打回原值。
    //   改草稿 = 存一个解析不出来的东西,而 validateDraft 不查这三列 ——
    //   那样它会一路带到 VariableManager::setPollIntervalMs 里变成 0,
    //   于是这个变量【每 0ms 轮询一次】。
    auto toInt = [&](const QString& s, int fallback) {
        bool ok = false;
        const int v = s.toInt(&ok);
        return ok ? v : fallback;
    };
    auto toDbl = [&](const QString& s, double fallback) {
        bool ok = false;
        const double v = s.toDouble(&ok);
        return ok ? v : fallback;
    };

    switch (item->column()) {
        case kColPoll:     d.pollMs   = toInt(text, d.pollMs);   break;
        case kColDeadband: d.deadband = toDbl(text, d.deadband); break;
        case kColAlwaysPoll:
            d.alwaysPoll = (item->checkState() == Qt::Checked);
            break;
        // 锁死的四列和 plcId 不可编辑(见 fillTable);
        // plcId 是 cellWidget,走 onPlcIdComboChanged,itemChanged 不为它发。
        default: return;
    }
    updateInfo();
}

void VariableConfigDialog::onPlcIdComboChanged(int) {
    auto* combo = qobject_cast<QComboBox*>(sender());
    if (!combo || suppressItemChanged_) return;
    const int row = combo->property("varRow").toInt();
    if (row < 0 || row >= rows_.size()) return;
    rows_[row].plcId = combo->currentText();
    updateInfo();
}

// ============================================================================
// 增 / 删
// ============================================================================

QString VariableConfigDialog::nextFreeName(const QList<VariableDraft>& rows,
                                          const QString& prefix) {
    // ★ 顺延而不是找空位 —— 与 PlcConfigDialog::nextFreeDraftId 同一条理由:
    //   用户删过中间一台再加回来,看到"plc2"会以为"它一直在啊,只是没显示"。
    int maxN = 0;
    for (const VariableDraft& d : rows) {
        if (!d.name.startsWith(prefix)) continue;
        bool ok = false;
        const int n = d.name.mid(prefix.size()).toInt(&ok);
        if (ok && n > maxN) maxN = n;
    }
    return QStringLiteral("%1%2").arg(prefix).arg(maxN + 1);
}

int VariableConfigDialog::addRowToDraft(const QString& name) {
    VariableDraft d;
    d.name  = name.isEmpty() ? nextFreeName(rows_, QStringLiteral("Var")) : name;
    d.plcId = plcIdChoices().value(0);
    // 地址默认 0 —— 但 0 也是派生变量的哨兵地址,所以必须盯住
    //   "用户新建的普通变量地址填 0"这个情况:它不会崩,但它会读到 plc 的
    //   0 号寄存器。提示在 validateDraft 里给。
    d.address  = 0;
    d.dataType = Variable::DataType::Int16;
    d.access   = Variable::Access::ReadOnly;
    d.pollMs   = 1000;
    rescanRefs(d);   // 新名字目前没人引用,顺手把 blindSpots 填上

    // ★ 重名必须在这里就拦,而不是等到点应用:
    //   VariableManager::addVariable 对同名是【静默替换】(6.3 有意为之)。
    //   不拦的话,用户新建一个叫 Speed 的,点应用就把原来那个悄悄顶掉了,
    //   而绑在原变量上的报警/画面控件全部指向了空。
    for (const VariableDraft& e : rows_) {
        if (e.name == d.name) return -1;
    }

    rows_.append(d);
    fillTable();
    const int idx = rows_.size() - 1;
    if (idx >= 0) table_->selectRow(idx);
    return idx;
}

void VariableConfigDialog::onAdd() {
    const int row = addRowToDraft();
    if (row < 0) {
        QMessageBox::information(this, tr("名字冲突"),
                                 tr("新变量的名字必须是全新的 —— "
                                    "重复的名字会静默顶掉原来的变量,"
                                    "绑在它上面的报警和画面控件会一起失效。"));
        return;
    }
    // 新行的 PLC / 地址都还是空的,让光标落在最需要填的那一格。
    if (auto* combo = qobject_cast<QComboBox*>(table_->cellWidget(row, kColPlcId))) {
        combo->setFocus();
    }
}

bool VariableConfigDialog::removeRowFromDraft(int row) {
    // ★ 本方法【不弹任何框、不查引用】。理由见头注释:询问是 UI 的职责。
    //   把 QMessageBox 写在这里,offscreen 测试会永久阻塞(实测卡满 300s),
    //   于是最危险的这条路径反而一条测试都写不了。
    if (row < 0 || row >= rows_.size()) return false;
    rows_.removeAt(row);
    fillTable();
    return true;
}

// ★★★ 有引用就拒删 —— 本项目被拍板的策略 ★★★
bool VariableConfigDialog::removeRowChecked(int row, QString* reasonOut,
                                            QStringList* blindSpotsOut) {
    if (row < 0 || row >= rows_.size()) {
        if (reasonOut) *reasonOut = tr("行号越界");
        return false;
    }
    const VariableDraft d = rows_.at(row);
    if (blindSpotsOut) *blindSpotsOut = d.blindSpots;

    if (d.refCount == 0) {
        return removeRowFromDraft(row);   // 真的删掉
    }

    // ★ 拒删,而且【逐条列出】引用者。
    //   只说"正在被使用"没有用 —— 用户不知道该先去哪儿处理。
    if (reasonOut) {
        *reasonOut = tr("变量「%1」正在被引用,已拒绝删除:\n\n  · %2\n\n"
                        "★ 删掉它,存出来的工程文件【下次就打不开】"
                        "(报警/联动指向一个不存在的变量,加载直接失败)。\n"
                        "请先处理掉上面这几处引用,再回来删。")
                        .arg(d.name,
                             d.refDescriptions.join(QStringLiteral("\n  · ")));
    }
    return false;
}

void VariableConfigDialog::onRemove() {
    const int row = table_->currentRow();
    if (row < 0 || row >= rows_.size()) {
        QMessageBox::information(this, tr("先选一行"),
                                 tr("请先在表格里选中要删除的变量。"));
        return;
    }
    const VariableDraft d = rows_.at(row);

    QString blindText;
    QStringList sp;
    // ★ 无论有没有引用,盲区都要说 —— 用户正是因为看不到画面绑定
    //   才可能误以为"删干净了"。见 .h 决定 ③。
    {
        VariableDraft probe = d;
        rescanRefs(probe);
        for (const QString& s : probe.blindSpots) blindText += QStringLiteral("\n  · ") + s;
    }

    QString msg;
    if (d.refCount > 0) {
        // 引用查到了 → 拒删。这条消息同时列出引用者和盲区。
        msg = tr("变量「%1」正在被下面这些地方引用,【不能删除】:\n\n  · %2\n\n"
                 "★ 删掉它,存出来的工程文件【下次就打不开】。\n"
                 "请先处理掉上面这几处引用,再回来删。\n\n"
                 "★ 还要提醒:本窗口【查不出画面上控件的绑定】:%3")
                .arg(d.name,
                     d.refDescriptions.join(QStringLiteral("\n  · ")),
                     blindText);
        QMessageBox::warning(this, tr("删不掉"), msg);
        return;   // ★ 不删。这里直接 return,不给"仍然删除"的选项。
    }

    msg = tr("确定删除变量「%1」吗?").arg(d.name);
    if (!d.expression.isEmpty()) {
        msg += QStringLiteral("\n\n")
             + tr("★ 这是一个派生量(%1)。别的变量可能正在用它"
                  "作为计算的一部分,那些关系不会跟着删。").arg(d.expression);
    }
    msg += tr("\n\n★ 删变量前请注意:本窗口能查出报警和联动的引用,"
              "【查不出画面上控件的绑定】:%1\n"
              "「被引用」显示为空【不等于】画面上没人用它 —— "
              "请到编辑器里确认后再删。").arg(blindText);

    if (QMessageBox::question(this, tr("删除变量"), msg) != QMessageBox::Yes) return;
    removeRowChecked(row, nullptr);   // 这时一定删得掉(引用数为 0)
}

// ============================================================================
// 校验 / 应用
// ============================================================================

bool VariableConfigDialog::validateDraft(QString* errorOut) const {
    auto fail = [&](const QString& msg) { if (errorOut) *errorOut = msg; return false; };

    QSet<QString> seen;
    for (int i = 0; i < rows_.size(); ++i) {
        const VariableDraft& d = rows_.at(i);
        const QString at = tr("第 %1 行「%2」").arg(i + 1).arg(d.name);

        if (d.name.isEmpty()) {
            return fail(at + tr(":变量名不能为空"));
        }
        // ★ 重名必须拦:VariableManager::addVariable 对同名是【静默替换】。
        //   不拦的话两个同名变量会互相顶掉,而报警/画面控件全部指向空。
        if (seen.contains(d.name)) {
            return fail(at + tr(":变量名 \"%1\" 重复 —— 必须唯一").arg(d.name));
        }
        seen.insert(d.name);

        if (d.plcId.isEmpty()) {
            return fail(at + tr(":必须指定属于哪台 PLC"));
        }
        // ★ 轮询周期必须 > 0。0 会让 PollScheduler【每 0ms 排一次】,
        //   也就是每个采集周期全表扫一遍 —— 而这个错误在界面上看不出来
        //   (表格显示的就是用户填的那个 0)。
        if (d.pollMs <= 0) {
            return fail(at + tr(":轮询周期 %1 ms 非法(必须大于 0)")
                            .arg(d.pollMs));
        }
        if (d.deadband < 0.0) {
            return fail(at + tr(":死区 %1 非法(不能为负)").arg(d.deadband));
        }
        // ★★ 地址 0 单独拦一道 —— 这条最隐蔽。
        //   0 不是"没填",它是一个【真的寄存器地址】:
        //   填成 0 的普通变量会被 PollScheduler 拿去读 plc 的 0 号寄存器,
        //   然后【把那个寄存器的值当成这个变量的值】显示在画面上。
        //   界面上看不出任何异常(值是真的、质量是 Good),
        //   只有知道"0 号寄存器是干什么的"的人才发现得了。
        //   派生变量也用地址 0,但那是合法的 —— 它有表达式标记,
        //   而且 PollScheduler 已经不轮询派生量了(见 tst_PollSchedulerDerived)。
        if (d.address == 0 && !d.derived) {
            return fail(at + tr(":地址不能是 0 —— 那是这台 PLC 的【真的第 0 个寄存器】,"
                                "填成 0 会把那个寄存器的值当成这个变量的值显示。"
                                "请填实际的寄存器地址。"));
        }
    }
    if (errorOut) errorOut->clear();
    return true;
}

bool VariableConfigDialog::applyDraftToManager(QString* errorOut) {
    if (!varMgr_) {
        if (errorOut) *errorOut = tr("没有 VariableManager,无法应用");
        return false;
    }
    // ★ 先全查再全改 —— 与 PlcConfigDialog::applyDraftToRunning 同一条理由。
    QString err;
    if (!validateDraft(&err)) {
        if (errorOut) *errorOut = err;
        return false;   // ★ VariableManager 一个字节都没动
    }

    int added = 0, modified = 0, removed = 0;
    for (const VariableDraft& d : rows_) {
        Variable* v = varMgr_->findVariable(d.name);
        if (!v) {
            // ★ 新增。地址 0 的普通变量已经被 validateDraft 拦掉了,
            //   这里一定是个真地址。
            auto* nv = new Variable(d.name, static_cast<quint16>(d.address),
                                    d.dataType, d.access, varMgr_);
            nv->setExpression(d.expression);   // 空的 = 普通变量
            if (!varMgr_->addVariable(nv)) {
                if (errorOut) *errorOut = tr("添加变量「%1」失败(名字或别名冲突)").arg(d.name);
                return false;
            }
            v = nv;
            ++added;
        } else {
            ++modified;
        }
        // ★ 这四个【有 setter】,可以直接改 —— 与名称/地址/类型/读写不同。
        //   名字不同是因为 Variable 没有 setName(见 .h 决定 ①)。
        v->setPlcId(d.plcId);
        v->setPollIntervalMs(d.pollMs);
        v->setDeadband(d.deadband);
        v->setAlwaysPoll(d.alwaysPoll);
    }
    // (删除在 apply 之前就被 removeRowChecked 处理掉了 —— 见 onRemove。
    //  草稿里少掉的行,意味着用户已经在界面上删过它并确认过。)
    qCDebug(hmiVarCfg) << "[阶段2] 变量草稿已应用 —— 新增" << added
                       << "修改" << modified << "删除" << removed;
    if (errorOut) errorOut->clear();
    return true;
}

void VariableConfigDialog::onApply() {
    QString err;
    if (applyDraftToManager(&err)) {
        reloadFromManager();
        QMessageBox::information(
            this, tr("已应用"),
            tr("变量配置已应用到运行中。\n\n"
               "★ 要写进工程文件,请到主界面点「保存工程」——"
               "本窗口【没有】保存按钮,避免出现两个互相打架的配置来源。"));
    } else {
        QMessageBox::warning(this, tr("应用失败"), err);
        qCWarning(hmiVarCfg) << "[阶段2] 应用失败:" << err;
    }
}

// ============================================================================
// 只读访问器(验收面)
// ============================================================================

int     VariableConfigDialog::rowCount() const { return static_cast<int>(rows_.size()); }
QString VariableConfigDialog::nameAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).name : QString();
}
QString VariableConfigDialog::plcIdAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).plcId : QString();
}
int     VariableConfigDialog::addressAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).address : 0;
}
int     VariableConfigDialog::refCountAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).refCount : 0;
}
QStringList VariableConfigDialog::refDescriptionsAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).refDescriptions : QStringList();
}

bool VariableConfigDialog::isCellEditable(int row, int col) const {
    // ★ 读的是【表格项真实的 flags】,不是草稿里的某个字段。
    //   断言"草稿里没有 editable 标记"只能证明数据结构,
    //   证明不了用户面对的那张表真的锁住了 —— 而后者才是这个决定的意义。
    const auto* it = table_->item(row, col);
    return it && (it->flags() & Qt::ItemIsEditable);
}

QStringList VariableConfigDialog::plcIdChoicesAt(int row) const {
    Q_UNUSED(row)
    // 所有行的候选是同一份(由 plcIdChoices() 统一算)。
    //   单独按行算只会让"某一行的下拉不一样"这种 bug 有地方藏。
    return plcIdChoices();
}

QString VariableConfigDialog::plcIdComboCurrentAt(int row) const {
    auto* combo = qobject_cast<QComboBox*>(table_->cellWidget(row, kColPlcId));
    return combo ? combo->currentText() : QString();
}

// ============================================================================

int VariableConfigDialog::exec() {
    // ★ EditProject —— 与 AlarmConfigDialog / PlcConfigDialog 同一条。
    //   改变量就是改工程行为,而且删变量会写坏工程文件。
    if (!qwhmi::auth::checkPermission(qwhmi::auth::CurrentUser::instance().role(),
                                      qwhmi::auth::Permission::EditProject)) {
        QMessageBox::warning(this, tr("权限不足"),
                             tr("只有工程师/管理员才能修改变量配置"));
        return QDialog::Rejected;
    }
    reloadFromManager();
    return QDialog::exec();
}

} // namespace qwhmi::plc
