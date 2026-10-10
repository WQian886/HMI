// ============================================================================
// 阶段 F · F4-a/F4-b/F4-c · 报警配置对话框实现
//
// 关联:AlarmConfigDialog.h
//
// ★ 本文件的三个阶段,以及它们各自允许碰什么
//
//   F4-a 只读  —— 把 AlarmManager 读成草稿,能看能改能导出。【不动运行中任何状态】
//   F4-c 落地  —— 草稿 → 工程文件 alarms[]。【仍然不动运行中任何状态】
//   F4-b 应用  —— 草稿 → 运行中的 AlarmManager。★ 这一版才开始改运行中的状态
//
// ★ 为什么"不动运行中"曾是硬约束,现在又放开了
//   F4-a/F4-c 阶段不放开的理由是:F4-b 有个必须先看见界面才能拍板的设计问题 ——
//   "报警正在跑的时候改阈值,怎么生效?"。那时拍板等于拍想象。
//   2026-10-08 用户拍板选了就地改配置(setConfig),那条路才被铺出来。
//   本文件的约束因此从"绝不碰"收窄成两条:
//     ① 改已有报警一律【就地改配置】,绝不删了重建(见 ThresholdAlarmChecker.h)
//     ② 应用前先整体校验,失败时运行中的状态保持原样(见 applyDraftToManager)
//   这两条比"绝不碰"弱,但仍足以保证 F4-a 的只读断言在不改代码的前提下继续成立。
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
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
// ★ F6:refreshAreaHistory 要读写【可编辑】下拉框里的行编辑框。
//   QComboBox 只前向声明了 QLineEdit,拿到指针也不能调它的方法 ——
//   报错是 C2027「使用了未定义类型」,离"你少 include 了"很远。
#include <QLineEdit>
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
constexpr int kColArea        = 8;   // ★ F6:区域
//   ★ 加在【最后】而不是"关联变量"旁边(那才是逻辑上更顺的位置)。
//     插在中间要把后面每一列的下标全挪一位,而 setItem / column() /
//     on_itemChanged 都靠这些 constexpr 对齐 —— 漏改一处不会报错,
//     只会让每列的值错一格,界面看着还挺像样。
//     "看着像样"正是这类 bug 最难被发现的原因,不值得为排版顺序冒这个险。

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

// "最短持续"这一格显示成什么。
//
// ★ 【为什么单独一个函数】这个值要显示在【三个地方】
//   (reloadFromManager 填表 / onAdd 新增行 / writeFormToRow 回写),
//   三处各写一遍 QString::number(...) 的后果是:改了格式就有两处忘了改,
//   于是同一列里出现 "500" 和 "500 ms" 混排 —— 而这一列的意义恰恰是
//   逐行对比"谁防抖多久",混排的那一刻这个用途就没了。
//
// ★ 【为什么不自动带单位】Qt 没有"显示单位但值是纯数字"的表格单元。
//   要么数字要么字符串,这里选字符串:这一列只给人看,
//   没有任何逻辑会去 setText(QString::number(...)) 之后再拿回来解析 ——
//   草稿里的真值是 row.minDurationMs(int),不经过这个字符串。
QString minDurText(int ms) {
    return QCoreApplication::translate("qwhmi::alarm", "%1 ms").arg(ms);
}

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
                                     PollScheduler* pollScheduler,
                                     QWidget* parent)
    : QDialog(parent), alarmMgr_(mgr), varMgr_(varMgr), pollScheduler_(pollScheduler) {
    setWindowTitle(tr("报警配置"));
    setModal(true);
    // ★ 520 → 560。原来 520 是"一行表单"的尺寸;表单拆成两行 + 一行提示之后,
    //   520 会让表格被压到只剩 200 出头,一条报警的完整行都要滚动才看得全 ——
    //   而"表格 + 表单"必须同屏可见,否则改参数时看不到自己改的是哪一行。
    resize(920, 560);

    table_       = new QTableWidget(this);
    infoLabel_   = new QLabel(this);
    hiddenLabel_ = new QLabel(this);
    jsonView_    = new QTextEdit(this);
    jsonView_->setReadOnly(true);
    jsonView_->setVisible(false);
    // ★ JSON 用等宽字体。
    //   比例字体下,缩进、逗号、花括号对不齐,
    //   而"看一眼导出的草稿对不对"这件事本身就是要用眼睛对齐着看结构 ——
    //   等宽字体是让这件事成立的前提,不是装饰。
    jsonView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    jsonView_->setLineWrapMode(QTextEdit::NoWrap);

    setupTable();
    setupForm();
    setupButtons();

    // ---- 布局 ----
    //
    // ★ 【原来这里是两条各挤了 7 个 / 6 个控件的横排】
    //   ① 表单行:关联变量 / 等级 / 方向 / 阈值 / 最短持续 / 滞回 —— 7 组
    //      标签+控件,全塞进一条 920px 的横排里。
    //      后果不是"难看"这么简单:七个标签宽度各不相同("最短持续(ms):"
    //      比"滞回:"宽一倍),Qt 默认左对齐,于是六个输入框的【左边缘参差不齐】,
    //      眼睛在"等级"和"阈值"之间找不到一条竖线可以对。
    //      而这个对话框的主要动作就是"选中一行 → 改几个数",
    //      对齐恰恰是这里最要紧的东西。
    //      现在拆成两行放进 GroupBox,每行三组,组内用竖直基准线对齐。
    //   ② 按钮行:6 个按钮 + 1 个状态标签挤一条。
    //      六个按钮长短不一("新增报警" 4 个字,"保存到工程文件" 7 个字),
    //      而且【危险度和语义完全不同】的动作混在一起:
    //      "删除选中"会丢数据,"应用到运行中"会立刻改变现场,
    //      "导出草稿 JSON"什么都不改只是看一眼 —— 三个按钮长得一模一样。
    //      现在按语义分成两簇:左边是"改草稿",右边是"落地 / 关闭"。
    auto* paramBox = new QGroupBox(tr("报警参数"), this);
    auto* formGrid = new QGridLayout(paramBox);
    formGrid->setHorizontalSpacing(10);
    formGrid->setVerticalSpacing(6);
    // 三列等宽:两行之间"等级"对"阈值"、"方向"对"滞回",
    // 竖着扫也能对上。
    formGrid->setColumnStretch(1, 1);
    formGrid->setColumnStretch(3, 1);
    formGrid->setColumnStretch(5, 1);
    struct FormField { const char* label; QWidget* w; };
    const FormField kFields[7] = {
        { "关联变量:",   varCombo_ },
        { "等级:",       levelCombo_ },
        { "方向:",       dirCombo_ },
        { "阈值:",       thresholdSpin_ },
        { "最短持续:",   minDurSpin_ },
        { "滞回:",       hysteresisSpin_ },
        // ★ F6:区域放第三行第一格。补这一格后网格从 2 行变 3 行,
        //   formHint_ 的行号必须跟着往下挪 —— 见下面 addWidget(formHint_, ...)。
        { "区域:",       areaCombo_ },
    };
    for (int i = 0; i < 7; ++i) {
        // 【每项都单独写死成 tr("字面量"),而不是 tr(变量)】
        //   lupdate 只能扫出字面量。用 kFields[i].label 这种变量,
        //   界面照常显示中文,但翻译工具永远扫不到它 ——
        //   到要做 i18n(阶段 I)那天这几个标签会莫名其妙地不跟着翻译。
        //   和 AlarmHistoryView 里的快捷按钮是同一条纪律。
        auto* lbl = new QLabel(tr(kFields[i].label), paramBox);
        // ★ 标签右对齐。
        //   Qt 默认左对齐时,"关联变量:"(5 字)和"等级:"(2 字)是两个不同宽度的框,
        //   框宽不同 → 右边缘不同 → 紧挨着的输入框左边缘也就对不齐。
        //   右对齐 + 竖直基准线,三个输入框的左边缘才真正成一条线。
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        formGrid->addWidget(lbl, i / 3, (i % 3) * 2);
        formGrid->addWidget(kFields[i].w, i / 3, (i % 3) * 2 + 1);
    }
    // 单位跟着控件走,而不是塞在标签里 ——
    // "最短持续:" + 一个带 " ms" 后缀的输入框,比"最短持续(ms):" + 裸数字
    // 少一层来回翻译的负担:后缀写在控件上,值变了单位不会忘。
    minDurSpin_->setSuffix(tr(" ms"));

    // 表单为什么是灰的 —— 必须说出来。
    // 原来一打开就是六个灰控件,界面上一个字的解释都没有,
    // 用户会以为功能坏了,而实际上只是"还没选中行"。这和 F2-c
    // 「静音不能等于隐藏」是同一条:不生效的东西要看得见为什么。
    formHint_ = new QLabel(paramBox);
    formHint_->setStyleSheet(QStringLiteral("color:#888;"));
    formHint_->setWordWrap(true);
    formGrid->addWidget(formHint_, 3, 0, 1, 6);

    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);
    // ---- 左簇:只改草稿,不动运行中 ----
    btnRow->addWidget(addBtn_);
    btnRow->addWidget(removeBtn_);
    btnRow->addWidget(exportBtn_);
    btnRow->addStretch();
    // ---- 右簇:落地 / 关闭 ----
    // 保存和"应用"之间留一道缝:这两个不是一类动作
    // (一个写文件、一个改现场),挤在一起容易被当成同一个按钮按错。
    btnRow->addWidget(saveBtn_);
    btnRow->addWidget(applyBtn_);
    btnRow->addSpacing(12);
    btnRow->addWidget(closeBtn_);

    auto* root = new QVBoxLayout(this);
    // ★ 统一留白 + 统一间距。原来全走 Qt 默认值,表格贴着对话框边、
    //   表单贴着表格,整块内容没有边界。
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);
    root->addWidget(table_, 1);
    root->addWidget(hiddenLabel_);
    root->addWidget(paramBox);
    // 状态标签独占一行。夹在按钮中间时,它既不像说明也不像控件,
    // 而且按钮一多就被挤到"看不到"的位置。
    infoLabel_->setStyleSheet(QStringLiteral("color:#555;"));
    root->addWidget(infoLabel_);
    root->addLayout(btnRow);
    root->addWidget(jsonView_);

    // ---- 信号 ----
    connect(closeBtn_,  &QPushButton::clicked, this, &QDialog::reject);
    connect(exportBtn_, &QPushButton::clicked, this, &AlarmConfigDialog::onExport);
    connect(saveBtn_,   &QPushButton::clicked, this, &AlarmConfigDialog::onSaveToProject);
    connect(applyBtn_,  &QPushButton::clicked, this, &AlarmConfigDialog::onApplyToRunning);
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
    // ★ F6:区域。currentTextChanged 而不是 currentIndexChanged ——
    //   它是【可编辑】下拉,用户直接在里面打字,根本不产生"索引变化"。
    //   连错信号的后果是:打字没反应,而从下拉里选才有反应 ——
    //   一个只在某一半场景下生效的输入框,比没有还难排查。
    connect(areaCombo_,      &QComboBox::currentTextChanged,   this, &AlarmConfigDialog::onApplyFieldEdits);

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
    table_->setColumnCount(9);
    // ★ 表头去掉单位,单位跟值走。
    //   原来写的是"最短持续(ms)",而单元格里是一个裸数字 500 ——
    //   单位只在表头,一滚动到看不见表头的地方就没了。
    //   改成表头只说是什么,值后面带单位(见 writeFormToRow),
    //   这样"这一格是什么"和"这一格是什么单位"分开了,列名也能短一截。
    table_->setHorizontalHeaderLabels(QStringList()
        << tr("报警名") << tr("关联变量") << tr("等级") << tr("方向")
        << tr("阈值") << tr("最短持续") << tr("滞回") << tr("已屏蔽")
        << tr("区域"));
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    // ★ 拉伸【报警名】,不是最后一列。
    //   原来用 setStretchLastSection(true),而最后一列是"已屏蔽" ——
    //   这一列绝大多数时候是【空的】(没屏蔽时填空串)。
    //   于是表格一宽,几百像素的空白全落在这一列上,"已屏蔽"三个字孤零零
    //   悬在右边,中间六列被挤成窄缝。
    //   真正宽度不确定的是报警名(用户自己起名),余量给它才说得通。
    //   —— 同样是看渲染图才发现的,ResizeToContents + stretchLastSection
    //   全都是"合法"的,只是难看。
    table_->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    table_->horizontalHeader()->setStretchLastSection(false);
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
    // ★ 行高固定、隔行底色 —— 和 AlarmHistoryView 同一条理由:
    //   报警是【一屏一屏横向对比】着看的(名字/等级/阈值/滞回挨着看),
    //   行高随字体变动会让眼睛在两行之间串行,隔行底色则给一条横向锚点。
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setDefaultSectionSize(26);
    table_->verticalHeader()->setVisible(false);   // 行号在这张表里没有意义
}

void AlarmConfigDialog::setupForm() {
    varCombo_ = new QComboBox(this);
    levelCombo_ = new QComboBox(this);
    dirCombo_  = new QComboBox(this);
    areaCombo_ = new QComboBox(this);
    varCombo_->setObjectName(QStringLiteral("alarmConfigVarCombo"));
    levelCombo_->setObjectName(QStringLiteral("alarmConfigLevelCombo"));
    dirCombo_->setObjectName(QStringLiteral("alarmConfigDirCombo"));
    areaCombo_->setObjectName(QStringLiteral("alarmConfigAreaCombo"));

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

    // ★ F6:区域 —— 可编辑下拉框。
    //   【为什么可编辑】只读下拉就要求先有一份"区域清单"(另一套管理 UI +
    //   工程顶层结构),纯输入框则每次手打、极易写出两个"一号泵房"。
    //   可编辑下拉两头都要:能打新的,也能从已用过的里点。
    //   【怎么补全已有区域】历史值来自 rows_ 里已经有的区域名,
    //   而不是数据库 —— 这个对话框管的是工程里的报警配置,
    //   而"已用过的区域"此刻就摆在 rows_ 里,不必去查 history.db。
    //   将来若要收窄成工程级清单,只需 setEditable(false),schema 一个字不用动。
    areaCombo_->setEditable(true);
    areaCombo_->setInsertPolicy(QComboBox::NoInsert);   // 打错的名字不自动入库
    areaCombo_->setToolTip(
        tr("这条报警属于现场的哪个区域(如「一号泵房」「反应釜区」)。\n"
           "留空 = 未分配。\n\n"
           "区域只用于【报警历史的筛选和分组】,不影响报警是否触发、"
           "也不改变触发时的处理逻辑。\n"
           "从下拉里选可以避免手打时出现两个同名的区域。"));

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
    applyBtn_  = new QPushButton(tr("应用到运行中"), this);
    closeBtn_  = new QPushButton(tr("关闭"), this);
    addBtn_->setObjectName(QStringLiteral("alarmConfigAddBtn"));
    removeBtn_->setObjectName(QStringLiteral("alarmConfigRemoveBtn"));
    exportBtn_->setObjectName(QStringLiteral("alarmConfigExportBtn"));
    closeBtn_->setObjectName(QStringLiteral("alarmConfigCloseBtn"));

    addBtn_->setToolTip(tr("在草稿里加一条新报警。\n"
                            "改完要按「应用到运行中」才会真正生效。"));
    removeBtn_->setToolTip(tr("从草稿里删除选中这条。\n"
                              "改完要按「应用到运行中」才会真正生效。"));
    exportBtn_->setToolTip(tr("把当前草稿导出成 JSON。\n"
                              "字段与 Project::saveToFile 写入工程文件时完全一致。"));
    // ★ 这是真正把草稿落地的按钮 —— 导出只是"看一眼 + 复制到剪贴板"。
    saveBtn_->setObjectName(QStringLiteral("alarmConfigSaveBtn"));
    saveBtn_->setToolTip(tr("把当前草稿写进已有工程文件的 alarms 段。\n"
                            "只改报警,变量/画面/联动一律不动。\n"
                            "运行中的报警不受影响 —— 要生效需重新载入工程。"));

    // ★ F4-b:另一条路。和 saveBtn_ 不是替代关系,是两个场景:
    //   saveBtn_  = 定版存档(存进文件,重载工程才生效)
    //   applyBtn_ = 现场调试(立刻改运行中的报警,不落盘)
    applyBtn_->setObjectName(QStringLiteral("alarmConfigApplyBtn"));
    applyBtn_->setToolTip(
        tr("把当前草稿【立刻】应用到运行中的报警,不用重载工程、不用停机。\n\n"
           "注意:这个按钮【不写文件】。想存进工程请另按「保存到工程文件」。\n"
           "正在响的报警改阈值后,会在下一个采集周期按新阈值重新判定。"));
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
            row.area          = cfg.area;   // ★ F6:老配置读回来是空 = 未分配
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
        put(kColMinDur,     r.editable ? minDurText(r.minDurationMs) : QString());
        put(kColHysteresis, r.editable ? QString::number(r.hysteresis, 'g', 6) : QString());
        put(kColShelved,    r.shelved ? tr("是") : QString());
        // ★ F6:非阈值报警没有 CheckerConfig,也就没有区域 —— 显示"未分配"
        //   而不是空白,理由同 AlarmHistoryView 的 areaDisplay。
        put(kColArea,       r.area.isEmpty() ? tr("未分配") : r.area);
    }

    suppressItemChanged_ = false;

    // ★ F6:补全列表要在 rows_ 填完之后才建 —— 它的数据源就是 rows_。
    refreshAreaHistory();

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
    // ★ F6:区域。刻意【不】跟 hasVariables_ 挂钩 ——
    //   区域跟工程里有没有变量无关,空工程照样能填"一号泵房"。
    areaCombo_->setEnabled(on);

    // ★ 灰色必须带解释。
    //   这个函数是全文件【唯一】决定表单能不能用的地方,所以提示也只能写在这里 ——
    //   写在构造函数里的话,后面每次改选中行都得记得同步一句,漏一次就成了
    //   "控件灰着,理由还是上次那条",比没有更误导。
    //
    //   三种情况给三种说法,而不是一句"请先选中一行":
    //     · 没选中行        → 告诉用户下一步做什么
    //     · 选中的是非阈值报警 → 告诉用户【为什么这条改不了】(否则会一直试)
    //     · 能改            → 收起提示,把版面还给控件
    if (!formHint_) return;
    if (on) {
        formHint_->setVisible(false);
        return;
    }
    // ★ 判据是"当前行号还落在 rows_ 范围内",不是单纯的 currentRow() >= 0。
    //   clearSelection() 不会把 currentRow() 复位,所以删掉最后一行之后
    //   currentRow() 还指着那个已经不存在的行 —— 只判 >= 0 的话,
    //   用户会看到"这一条是非阈值报警"这一句完全对不上的解释。
    const int cur = table_->currentRow();
    if (cur < 0 || cur >= rows_.size()) {
        formHint_->setText(tr("↑ 先在上表选中一行,这里才能改参数。"));
    } else {
        formHint_->setText(
            tr("↑ 这一条是非阈值报警(没有阈值可配),只能查看、不能改参数。"));
    }
    formHint_->setVisible(true);
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
                                     thresholdSpin_, minDurSpin_, hysteresisSpin_,
                                     // ★ F6:区域也要一起 block。
                                     //   漏了它的话,填区域时会触发 onApplyFieldEdits,
                                     //   而当前选中行还没切过来 —— 又是那个错行 bug。
                                     areaCombo_ };
    for (QObject* w : widgets) w->blockSignals(true);

    // ★ 变量下拉:这一格【必须处理"找不到"的情况】,不能像别的控件那样
    //   "找到才设,找不到就跳过"。
    //
    //   【这里曾经漏了,是 F4-b 写测试时才暴露出来的】
    //   原来的写法是:
    //       const int varIdx = varCombo_->findText(r.variableName);
    //       if (varIdx >= 0) varCombo_->setCurrentIndex(varIdx);   // ← 找不到就【什么都不做】
    //   "什么都不做"意味着组合框【保留上一次选中的那个变量】。
    //   于是这一行的真实变量(比如一个已被从工程里删掉的 plc1.Orphan)
    //   在界面上显示成【别的变量】,而紧接着 writeFormToRow() 会把那个
    //   显示值【写回草稿】—— 原来的关联被静默改掉了。
    //
    //   【为什么这在生产里也会出事,而不只是测试的问题】
    //   工程里删掉一个变量后,还指着它的报警不会消失,而是会在
    //   "工程师点进这个对话框、选中那一行、改个别的参数"的那一刻,
    //   被悄悄改指向另一个变量。界面上看不出任何异常,历史记录也会
    //   从那一刻起对不上。这正是本项目反复吃亏的"静默改数据"。
    //
    //   【正确的做法】找不到时明确把下拉置为"没有对应项"的状态,
    //   并且【不要】让 writeFormToRow 用这个假值覆盖真实值。
    //   置 -1(QComboBox 表示"无选中项"),界面上一目了然。
    const int varIdx = varCombo_->findText(r.variableName);
    varCombo_->setCurrentIndex(varIdx);   // varIdx == -1 时 Qt 会显示"空",正好如实反映"没配"

    const int lvlIdx = levelCombo_->findData(r.level);
    if (lvlIdx >= 0) levelCombo_->setCurrentIndex(lvlIdx);

    const int dirIdx = dirCombo_->findData(r.direction);
    if (dirIdx >= 0) dirCombo_->setCurrentIndex(dirIdx);

    thresholdSpin_->setValue(r.threshold);
    minDurSpin_->setValue(r.minDurationMs);
    hysteresisSpin_->setValue(r.hysteresis);
    // ★ F6:setCurrentText 对【可编辑】下拉写的是行编辑框的内容,
    //   所以"这一行的区域"即使不在补全列表里也照样显示得出来。
    //   (用 setCurrentIndex 就会丢:不在列表里的值没有对应索引。)
    //   这也是为什么这个控件必须是 editable —— 只读下拉装不下新区域。
    areaCombo_->setCurrentText(r.area);

    for (QObject* w : widgets) w->blockSignals(false);

    // 非阈值报警:能看到,但不许改
    setFormEnabled(r.editable);
    removeBtn_->setEnabled(r.editable);
}

// ★ F6:重建区域补全列表。
//   数据源是 rows_ 里已经有的区域名(去重 + 排序),不是数据库 ——
//   这个对话框管的是工程里的报警配置,"已用过的区域"此刻就在 rows_ 里。
//   ★ 重建时不能动当前文本:用户在编辑某个区域名的过程中,
//     下拉列表刷新把他正在打的字冲掉,是很难受的。
void AlarmConfigDialog::refreshAreaHistory() {
    if (!areaCombo_) return;

    QStringList seen;
    for (const AlarmConfigRow& r : rows_) {
        const QString a = r.area.trimmed();
        if (!a.isEmpty() && !seen.contains(a)) seen << a;
    }
    seen.sort();

    // 只清列表项,不动 lineEdit 里的当前文本
    const bool blocked = areaCombo_->blockSignals(true);
    QString current;
    {
        QLineEdit* le = areaCombo_->lineEdit();
        if (le) current = le->text();
    }
    areaCombo_->clear();
    areaCombo_->addItems(seen);
    if (!current.isEmpty()) {
        areaCombo_->setCurrentText(current);
    }
    areaCombo_->blockSignals(blocked);
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
    //
    // ★ currentIndex() == -1 必须【保留原值】,不能写成空串。
    //   currentIndex 为 -1 表示"这一行配的变量在下拉里找不到"
    //   (典型场景:变量被从工程里删了,而报警还指着它)。
    //   此时 currentText() 是空的 —— 若照写,这一行的变量关联就被
    //   【静默清空】了,而界面上看不出任何异常。
    //   保留原值才能让 validateDraft 报出"变量不存在",
    //   那是用户真正需要知道的事(存下去会打不开整个工程)。
    if (hasVariables_ && varCombo_->currentIndex() >= 0) {
        const QString v = varCombo_->currentText();
        // startsWith('(') 排除占位项"(工程里还没有变量)"
        if (!v.isEmpty() && !v.startsWith('(')) r.variableName = v;
    }
    r.level         = levelCombo_->currentData().toString();
    r.direction     = dirCombo_->currentData().toString();
    r.threshold     = thresholdSpin_->value();
    r.minDurationMs = minDurSpin_->value();
    r.hysteresis    = hysteresisSpin_->value();
    // ★ F6:区域。currentText() 取的是行编辑框里的字,包括用户刚打的。
    //   顺手 trim:同一个区域打成 "一号泵房 " 和 "一号泵房",
    //   在历史筛选里就是两个桶 —— 而用户绝不会知道自己多了个空格。
    //   这一行 trim 抵得上后面一整套"区域清单"UI 的防错能力。
    r.area          = areaCombo_->currentText().trimmed();

    // ★ 写回表格。
    //   不回写的话,表格显示的还是改之前的值 —— 用户改了阈值但看着没变,
    //   导出 JSON 却是新值。这种"界面和实际状态不一致"是最难查的一类 bug。
    if (row < table_->rowCount()) {
        suppressItemChanged_ = true;   // 回写是程序性的,不能反过来触发自己
        if (auto* it = table_->item(row, kColVariable))   it->setText(r.variableName);
        if (auto* it = table_->item(row, kColLevel))      it->setText(levelDisplayName(r.level));
        if (auto* it = table_->item(row, kColDir))        it->setText(dirDisplayName(r.direction));
        if (auto* it = table_->item(row, kColThreshold))  it->setText(QString::number(r.threshold, 'g', 6));
        if (auto* it = table_->item(row, kColMinDur))     it->setText(minDurText(r.minDurationMs));
        if (auto* it = table_->item(row, kColHysteresis)) it->setText(QString::number(r.hysteresis, 'g', 6));
        // ★ F6:区域写回表格。空就显示"未分配" ——
        //   空白单元格和一整列空白在表格里分不清"没配"还是"这列坏了"。
        if (auto* it = table_->item(row, kColArea)) {
            it->setText(r.area.isEmpty() ? tr("未分配") : r.area);
        }
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
    put(kColMinDur,     minDurText(row.minDurationMs));
    put(kColHysteresis, QString::number(row.hysteresis, 'g', 6));
    put(kColShelved,    QString());
    // ★ F6:新报警默认未分配。刻意【不】预填第一个区域 ——
    //   预填会让"新增"看起来像"已经归到某个区了",而它其实没有依据。
    put(kColArea,       tr("未分配"));
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
                                  "删完要按「应用到运行中」才会真正删掉。").arg(r.name))
        != QMessageBox::Yes) {
        return;
    }
    removeRowFromDraft(row);
}

// ★ 把"确认"和"删除"拆成两半(2026-10-08 F4-b 时补的)
//   【为什么要拆】onRemove 里原本直接 QMessageBox::question + 原地删除。
//   那个模态框在 offscreen 测试下会【永久阻塞】—— QMessageBox 在没有事件循环
//   的测试环境里等不到应答,测试直接卡到超时(实测 300s 超时,timer 停不下来)。
//   结果是:整个"删除"路径【一条测试都写不了】,只能像当时的
//   removeButton_removesSelectedRow 那样"只检查按钮是亮的,但不点"。
//   而 F4-b 一上来,删除就成了最危险的路径(删掉正在响的报警会留孤儿),
//   恰恰最需要测试覆盖 —— 于是这个可测性缺口第一次变成了拦路虎。
//
//   拆法:onRemove 负责"问用户",removeRowFromDraft 负责"删",后者不做任何 UI 询问。
//   这不是为测试开后门 —— 询问本来就是 UI 的职责,而"怎么删"是数据层的职责。
bool AlarmConfigDialog::removeRowFromDraft(int row) {
    if (row < 0 || row >= rows_.size()) return false;
    rows_.removeAt(row);
    // removeRow 会让后面的行号整体前移,草稿和表格的索引必须【同时】动,
    // 所以先记住要删的行,再让表格删 —— 顺序反了会错位一行。
    table_->removeRow(row);

    table_->clearSelection();
    setFormEnabled(false);
    updateInfo();
    return true;
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

// ============================================================================
// ★ F4-b:应用草稿到运行中的 AlarmManager
//
// 【为什么分三步走:先全查、再全改】
//   如果一边查一边改,改到第 5 条才发现第 3 条有问题,前 4 条已经改了运行中的报警 ——
//   而用户看到的是"应用失败",于是他不知道到底哪些生效了。
//   结果就是运行中的状态和界面上的草稿【对不上,而且没人说得清哪里对不上】。
//   所以:第一遍只读地查,全过了才动手。
//
// 【为什么删除走 removeChecker 而不是直接 delete】
//   removeChecker 会先给这条报警正在响的活动记录补发恢复,再清屏蔽名单。
//   F4-b 之前它一个生产调用点都没有,那两个洞是这次一并补上的(见 AlarmManager.cpp)。
// ============================================================================

bool AlarmConfigDialog::applyDraftToManager(QString* errorOut) {
    if (!alarmMgr_) {
        if (errorOut) *errorOut = tr("没有连接到报警管理器,无法应用");
        return false;
    }

    // ================= 第 1 遍:只读地查,一个字都还不改 =================

    // ---- ① 草稿本身合不合法(名字空/重名、变量不存在)----
    //   和 F4-c 存盘用的是同一套校验 —— 存下去会打不开工程文件的东西,
    //   直接应用到运行里同样是垃圾进垃圾出。
    QString verr;
    if (!validateDraft(&verr)) {
        if (errorOut) *errorOut = verr;
        return false;
    }

    // ---- ② 有没有"草稿里新增"的行(运行中还不存在的)----
    //   这类行需要 new ThresholdAlarmChecker,必须有 PollScheduler*,
    //   否则建出来的报警收不到任何实时值、永不触发。
    QStringList newNames;
    for (const AlarmConfigRow& r : rows_) {
        if (!r.editable) continue;
        if (!hasCheckerNamed(r.name)) newNames << r.name;
    }
    if (!newNames.isEmpty() && !pollScheduler_) {
        if (errorOut) {
            *errorOut = tr("草稿里有 %1 条新增报警,但拿不到轮询调度器,建不出能真正生效的报警。\n\n"
                           "没有它,新建的报警永远收不到实时值 —— 那是一条【看着配好了、"
                           "实际上永远不会响】的报警,比不配更危险,所以这里直接拒绝。")
                            .arg(newNames.size());
        }
        return false;
    }

    // ---- ③ 正在响的报警被改关联变量 → 拒绝(理由见 setVariable)----
    //   这一条必须在动手前查:等到改到那一条才发现,前面的改动已经生效了。
    for (const AlarmConfigRow& r : rows_) {
        if (!r.editable) continue;
        auto* tc = thresholdCheckerNamed(r.name);
        if (!tc) continue;                       // 新增行,③ 不适用
        if (tc->variable() && tc->variable()->name() == r.variableName) continue;
        if (tc->isActive()) {
            if (errorOut) {
                *errorOut = tr("报警 \"%1\" 正在响,此时不能改它的关联变量。\n\n"
                               "它当前监控的是 \"%2\"。恢复之后才能换 —— "
                               "否则历史记录里会出现「%2 触发、%3 恢复」这种自相矛盾的一行。")
                                .arg(r.name,
                                     tc->variable() ? tc->variable()->name() : tr("(无)"),
                                     r.variableName);
            }
            return false;
        }
    }

    // ================= 第 2 遍:确认无误,开始改 =================

    int added = 0, updated = 0, removed = 0, shelveChanged = 0;

    // ---- ④ 处理删除:运行里有、草稿里没有的可配置报警 ----
    //   草稿 reloadFromManager 时是从运行中读的,所以"运行里有、草稿里没有"
    //   一定意味着用户在本会话里删过它(非阈值报警不在删除范围内)。
    //   标记而非直接删,是下面的⑤要"就地改"而不是"重建"。
    QStringList existingNames;
    for (AlarmChecker* c : alarmMgr_->checkers()) {
        if (!c) continue;
        if (dynamic_cast<ThresholdAlarmChecker*>(c)) existingNames << c->name();
    }
    for (const QString& n : std::as_const(existingNames)) {
        if (hasDraftRowNamed(n)) continue;
        if (alarmMgr_->removeChecker(n)) ++removed;
    }

    // ---- ⑤ 逐条落地 ----
    for (AlarmConfigRow& r : rows_) {
        if (!r.editable) continue;

        Variable* var = varMgr_ ? varMgr_->findVariable(r.variableName) : nullptr;
        if (!var) continue;   // ①已经查过变量存在性,到不了这里

        CheckerConfig cfg;
        cfg.level         = JsonSerializer::alarmLevelFromString(r.level);
        cfg.direction     = JsonSerializer::directionFromString(r.direction);
        cfg.threshold     = r.threshold;
        cfg.minDurationMs = r.minDurationMs;
        cfg.hysteresis    = r.hysteresis;
        cfg.area          = r.area;

        if (auto* tc = thresholdCheckerNamed(r.name)) {
            // ---- 已存在:就地改,不重建(重建会丢 currentId_,见头文件)----
            tc->setConfig(cfg);
            // 变量没变就不碰 —— setVariable 会清 lastValue_,
            // 白清一次会让下一次触发快照变成 0。
            if (!tc->variable() || tc->variable()->name() != r.variableName) {
                tc->setVariable(var);
            }
            ++updated;
        } else {
            // ---- 新增:new 一个真的能跑起来的 checker ----
            auto* checker = new ThresholdAlarmChecker(r.name, var, pollScheduler_, cfg);
            if (!alarmMgr_->addChecker(checker)) {
                // addChecker 会因为重名/空名拒绝。走到这里说明草稿有问题,
                // 而①已经查过重名 —— 所以这属于"不该发生"的路径,
                // 但绝不能 silently ignore:必须说出来。
                delete checker;
                if (errorOut) {
                    *errorOut = tr("新增报警 \"%1\" 没能注册到报警管理器(名字重复或非法)。")
                                    .arg(r.name);
                }
                return false;
            }
            ++added;
        }

        // ---- ⑥ 屏蔽状态跟着草稿走 ----
        //   屏蔽是运行中状态(存在 shelved_ 里),草稿里那一列本来就显示它。
        //   不同步的话:用户在这个对话框里看到的"已屏蔽"和实际屏蔽状态会各说各话。
        if (alarmMgr_->isShelved(r.name) != r.shelved) {
            if (r.shelved) alarmMgr_->shelve(r.name);
            else           alarmMgr_->unshelve(r.name);
            ++shelveChanged;
        }
    }

    if (errorOut) {
        *errorOut = tr("新增 %1 条,更新 %2 条,删除 %3 条,屏蔽状态变更 %4 条。")
                        .arg(added).arg(updated).arg(removed).arg(shelveChanged);
    }

    // ★ 应用完必须重读 —— 草稿里的"新增行"现在是真正的 checker 了,
    //   而 reloadFromManager 会把它们按名字排序后重新填表。
    //   不重读的话,用户会看到一个和运行中状态不一致的表格,
    //   下次打开对话框又变了 —— 这正是"界面和实际状态对不上"最难查的一类。
    reloadFromManager();

    qCInfo(hmiAlarm) << "[F4-b] 草稿已应用到运行中的报警:"
                     << "新增" << added << "更新" << updated
                     << "删除" << removed << "屏蔽变更" << shelveChanged;
    return true;
}

void AlarmConfigDialog::onApplyToRunning() {
    // ★ 应用会立刻改变运行中的报警,不能像 F4-a 那样"按了没反应"。
    //   所以按之前先说清楚会发生什么 —— 尤其是【正在响的报警】,
    //   那是操作员正盯着屏幕上的东西,不能在他不知情时改掉。
    QStringList firing;
    if (alarmMgr_) {
        for (const Alarm& a : alarmMgr_->activeAlarms()) firing << a.name;
    }

    QString msg = tr("把当前草稿应用到运行中的报警?\n\n"
                     "新增 %1 条,删除 %2 条。")
                      .arg(rows_.size()).arg(rows_.size());
    if (!firing.isEmpty()) {
        msg += tr("\n\n注意:以下报警正在响,改它们的阈值会在下一个采集周期重新判定:\n  %1")
                   .arg(firing.join(", "));
    }
    if (QMessageBox::question(this, tr("确认应用"), msg) != QMessageBox::Yes) {
        return;
    }

    QString err;
    if (applyDraftToManager(&err)) {
        QMessageBox::information(this, tr("已应用"), err);
    } else {
        // 失败时运行中的状态保持原样(第 1 遍就全查过了),这点要告诉用户,
        // 否则他会以为改了一半。
        QMessageBox::warning(this, tr("应用失败"), err);
        qCWarning(hmiAlarm) << "[F4-b] 应用失败:" << err;
    }
}

// ---- 小工具:按名字找 checker(草稿 ⇄ 运行中的对照)----

ThresholdAlarmChecker* AlarmConfigDialog::thresholdCheckerNamed(const QString& name) const {
    if (!alarmMgr_) return nullptr;
    for (AlarmChecker* c : alarmMgr_->checkers()) {
        if (!c || c->name() != name) continue;
        // 非阈值报警(WriteFailChecker 等)名字对得上但没有阈值可改 —— 返回 nullptr,
        // 调用方据此认为"运行中没有这条可配置的报警"。
        if (auto* tc = dynamic_cast<ThresholdAlarmChecker*>(c)) return tc;
    }
    return nullptr;
}

bool AlarmConfigDialog::hasCheckerNamed(const QString& name) const {
    return thresholdCheckerNamed(name) != nullptr;
}

bool AlarmConfigDialog::hasDraftRowNamed(const QString& name) const {
    for (const AlarmConfigRow& r : rows_) {
        if (r.name == name) return true;
    }
    return false;
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
        cfg.area          = r.area;

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

// ★ F6:草稿里的区域原值。刻意返回【空串】而不是表格里显示的"未分配" ——
//   "未分配"是给人看的话,不是数据。测试要断的是"用户填的那个名字回来了"。
QString AlarmConfigDialog::areaAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).area : QString();
}

bool AlarmConfigDialog::editableAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).editable : false;
}

// ============================================================================
// 小工具
// ============================================================================

} // namespace qwhmi::alarm
