// 阶段 F · F4(2026-10-08):报警历史查询视图 —— 实现

#include "history/AlarmHistoryView.h"

#include "../../Alarm.h"
#include "AlarmRecorder.h"
#include "../logging/Logging.h"

#include <QColor>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>   // std::reverse

namespace qwhmi::history {

namespace {

constexpr int kColTime     = 0;
constexpr int kColName     = 1;
constexpr int kColLevel    = 2;
constexpr int kColAction   = 3;
constexpr int kColVariable = 4;
constexpr int kColValue    = 5;
constexpr int kColThreshold = 6;

const char* levelDisplay(int level) {
    switch (level) {
    case 0: return "信息";
    case 1: return "警告";
    case 2: return "严重";
    case 3: return "危急";
    }
    return "?";
}

// action 是 AlarmRecorder 里存的那个 0/1/2/3。
// 【为什么要中文而不是数字】数字 2 用户看不懂它是"恢复"还是"清除",
// 而这两个的区别恰恰是事故追溯时最要紧的(条件消失 vs 被人工清掉)。
const char* actionDisplay(int action) {
    switch (action) {
    case 0: return "触发";
    case 1: return "确认";
    case 2: return "恢复";
    case 3: return "清除";
    }
    return "?";
}

// 表格的行高。
// ★ 为什么不用默认行高:默认行高是跟着字体走的,换一款中文字体就变。
//   而报警历史是要【一屏一屏地对比着看】的 ——
//   行高忽高忽低会让眼睛在两行之间找不到对应关系,
//   这正是"查事故"时最不能有的那种视觉噪声。
constexpr int kRowHeight = 26;

// 等级 → 前景色。返回无效 QColor = 不上色。
//
// ★ 【色值必须和 AlarmListControl 一样,不能各定各的】
//   F3 已经在报警列表里定了四档颜色。如果历史这里另挑一套,
//   同一个产品里"危急=这个颜色"就有了两个答案 ——
//   用户在列表里认出来的红,到历史里换了张脸,得重新记一遍映射。
//   抄的是 AlarmListControl.cpp:40-46 的 fg 那四个值。
//
// ★ 【为什么用 fg 而不是 bg】
//   列表里是整行铺底色(红底白字),历史表里只给"等级"两个字上色 ——
//   如果照抄 bg,严重(255,205,195)这种浅色在白底表格上等于没上色,
//   而危急的深红(205,40,40)又太扎眼。所以取"这一档的文字色"。
//
// ★ 【为什么信息级不上色】
//   信息级在历史里量最大(模式切换、泵启停全记),
//   给它也上色等于让整张表五颜六色,最该被看见的严重/危急就淹了。
//   返回无效 QColor,调用方据此跳过。
QColor levelColor(int level) {
    switch (level) {
    case 1:  return QColor(120,  80,   0);   // 警告 —— 暗黄
    case 2:  return QColor(150,  20,  10);   // 严重 —— 橙红
    case 3:  return QColor(205,  40,  40);   // 危急 —— 红
    }
    return QColor();   // 信息(0)不上色;越界值同样不上色
}

} // namespace

AlarmHistoryView::AlarmHistoryView(AlarmRecorder* recorder, QWidget* parent)
    : QWidget(parent), recorder_(recorder) {
    setupUi();
    if (!recorder_) {
        // ★ 不给一个"能打开但永远查不出东西"的空壳。
        //   数据库没开就明确说没开 —— 用户看到这句话会去查目录权限,
        //   而看到一张空表只会以为自己记错了。
        table_->setEnabled(false);
        statusLabel_->setText(tr("⚠ 报警历史库未启用(数据库打不开),无法查询。"));
        statusLabel_->setStyleSheet(QStringLiteral("color:#c00;"));
        if (queryBtn_) queryBtn_->setEnabled(false);
    }
    refresh();
}

void AlarmHistoryView::setupUi() {
    auto* root = new QVBoxLayout(this);
    // ★ 统一留白。原来全走 Qt 默认值,筛选栏贴着表格边,
    //   表格贴着状态栏,整个视图没有边界感。
    root->setContentsMargins(12, 12, 12, 8);
    root->setSpacing(8);
    setupFilterRow(this, root);

    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("alarmHistoryTable"));
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels(QStringList()
        << tr("时间") << tr("报警名") << tr("等级") << tr("动作")
        << tr("关联变量") << tr("触发值") << tr("阈值"));
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    // ★ 拉伸【报警名】那一列,而不是最后一列。
    //   原来用的是 setStretchLastSection(true) —— 那是"把多余的宽度全给最后一列"。
    //   而最后一列是"阈值",内容是 "> 80.0" 这么几个字:
    //   表格一宽,那几个字就摊在一列 700px 宽的空地上,其余六列被挤成一条条窄缝,
    //   于是【时间/报警名挤在一起、阈值那一大片空着】。
    //   ★ 亲眼看过渲染图才发现的 —— 几何断言查不出来,
    //     ResizeToContents 和 stretchLastSection 都是"合法"的,只是难看。
    //   报警名才是宽度最不确定的一列(用户自己起名),让它吃余量,
    //   其余列一律按内容收紧,谁也不用替谁扛空白。
    table_->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    table_->horizontalHeader()->setStretchLastSection(false);
    // ★ 隔行底色。
    //   报警历史动辄几十上百行,纯白底上横平竖直的网格线
    //   会让眼睛在【一行的哪一格】上串行 —— 而查事故时
    //   恰恰要横向对比同一行的"等级/动作/触发值/阈值"。
    //   隔行底色就是给眼睛一条横向的锚。
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setDefaultSectionSize(kRowHeight);
    table_->verticalHeader()->setVisible(false);   // 行号对查历史没有意义,纯占宽
    // ★ 历史是"给人查"的,不是"给人改"的。
    //   不设 NoEditTriggers 的话,用户双击一格就能改掉一个值,
    //   看着像编辑成功了,实际上只是内存里的显示变了 ——
    //   关掉窗口就没了,而用户会以为历史被改了。
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setWordWrap(false);
    root->addWidget(table_, 1);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("alarmHistoryStatus"));
    statusLabel_->setStyleSheet(QStringLiteral("color:#555;"));
    root->addWidget(statusLabel_);
}

void AlarmHistoryView::setupFilterRow(QWidget* parent, QVBoxLayout* layout) {
    auto* outer = new QVBoxLayout;
    outer->setSpacing(8);

    auto* row = new QHBoxLayout;
    row->setObjectName(QStringLiteral("alarmHistoryFilterRow"));
    row->setSpacing(8);

    const QDateTime now = QDateTime::currentDateTime();
    // ★ 默认"最近 24 小时",不是"全部"。
    //   全部的话,跑了一个月的机器一打开就是几万行,表格卡住,
    //   而操作员九成只想看"今天出了什么事"。
    fromEdit_ = new QDateTimeEdit(now.addDays(-1), parent);
    fromEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    fromEdit_->setCalendarPopup(true);
    fromEdit_->setObjectName(QStringLiteral("alarmHistoryFromEdit"));
    toEdit_ = new QDateTimeEdit(now, parent);
    toEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    toEdit_->setCalendarPopup(true);
    toEdit_->setObjectName(QStringLiteral("alarmHistoryToEdit"));

    row->addWidget(new QLabel(tr("从:"), parent));
    row->addWidget(fromEdit_);
    row->addWidget(new QLabel(tr("到:"), parent));
    row->addWidget(toEdit_);

    queryBtn_ = new QPushButton(tr("查询"), parent);
    queryBtn_->setObjectName(QStringLiteral("alarmHistoryQueryBtn"));
    queryBtn_->setDefault(true);   // 回车 = 查询,不用每次去够鼠标
    connect(queryBtn_, &QPushButton::clicked, this, &AlarmHistoryView::refresh);
    row->addWidget(queryBtn_);

    row->addStretch();
    outer->addLayout(row);

    // ---- 第二行:快捷范围 + 等级 ----
    // ★ 为什么拆成两行而不是七个控件挤一行:
    //   从/到/四个快捷/等级/查询 全排一行,窄一点的窗口(1024×768 的工控机很常见)
    //   就开始换行或者把控件压扁 —— 而"查事故"往往发生在分辨率最低的那台机器上。
    //   拆两行之后,第一行只有"时间 + 查询",第二行是筛选项,
    //   层次清楚,任何宽度下都不会挤。
    auto* row2 = new QHBoxLayout;
    row2->setObjectName(QStringLiteral("alarmHistoryFilterRow2"));
    row2->setSpacing(8);
    row2->addWidget(new QLabel(tr("快捷范围:"), parent));

    // 手点两次日历选"三天前 14:23 到今天 14:23"是件很烦的事,
    // 而这几个快捷键覆盖了绝大多数查事故的场景。
    //
    // ★ 每一项都单独写死成 tr("字面量"),而不是 tr(变量):
    //   lupdate 只能扫出字面量。用变量的话,界面照常显示中文,
    //   但翻译工具永远扫不到它 —— 到要做 i18n(阶段 I)那天
    //   这几个按钮就会莫名其妙地不跟着翻译。
    struct Shortcut { int hours; };
    static const Shortcut kShortcuts[] = { {1}, {24}, {72}, {168} };
    const char* kShortcutLabels[] = {
        QT_TRANSLATE_NOOP("AlarmHistoryView", "最近1小时"),
        QT_TRANSLATE_NOOP("AlarmHistoryView", "最近24小时"),
        QT_TRANSLATE_NOOP("AlarmHistoryView", "最近3天"),
        QT_TRANSLATE_NOOP("AlarmHistoryView", "最近7天"),
    };
    for (int i = 0; i < 4; ++i) {
        auto* b = new QPushButton(tr(kShortcutLabels[i]), parent);
        b->setObjectName(QStringLiteral("alarmHistoryQuick%1").arg(i));
        connect(b, &QPushButton::clicked, this, [this, h = kShortcuts[i].hours]() {
            applyShortRange(h);
        });
        row2->addWidget(b);
    }

    row2->addSpacing(12);
    row2->addWidget(new QLabel(tr("最低等级:"), parent));
    levelCombo_ = new QComboBox(parent);
    levelCombo_->setObjectName(QStringLiteral("alarmHistoryLevelCombo"));
    // ★ 选项是"下限",不是集合 —— 文案必须说清楚,否则用户以为
    //   选了"严重"就看不到危急的记录,而去查为什么危急报警没记录。
    levelCombo_->addItem(tr("全部"),          0);
    levelCombo_->addItem(tr("警告及以上"),    1);
    levelCombo_->addItem(tr("严重及以上"),    2);
    levelCombo_->addItem(tr("仅危急"),        3);
    levelCombo_->setCurrentIndex(1);   // 默认"警告及以上" —— 信息级不响,
                                       // 它的历史记录量大且少有人查
    levelCombo_->setToolTip(tr("按最低等级过滤:选「严重及以上」会同时显示危急。"));
    row2->addWidget(levelCombo_);

    row2->addStretch();
    outer->addLayout(row2);

    layout->addLayout(outer);
}

void AlarmHistoryView::applyShortRange(int hours) {
    const QDateTime now = QDateTime::currentDateTime();
    toEdit_->setDateTime(now);
    fromEdit_->setDateTime(now.addSecs(-hours * 3600));
    // ★ 快捷按钮点了就立刻查,不逼用户再按一次"查询"。
    //   少一次点击少一次"我是不是没点到"的怀疑。
    refresh();
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

void AlarmHistoryView::refresh() {
    table_->setRowCount(0);   // ★ 必须先清空。
    //   不清的话,第二次查出来的条数比第一次少,旧行会留在表格里 ——
    //   界面上看着"这次查到了 5 条",其实有 3 条是上次的残留。

    if (!recorder_ || !recorder_->isOpen()) {
        statusLabel_->setText(tr("⚠ 报警历史库未启用,无法查询。"));
        return;
    }

    QVector<AlarmRecorder::Record> rows = recorder_->query(
        fromEdit_->dateTime().toMSecsSinceEpoch(),
        toEdit_->dateTime().toMSecsSinceEpoch(),
        minLevel());

    // ★ 倒过来:最新的排在最上面。
    //   AlarmRecorder::query 是 ORDER BY action_at ASC(最旧在前)。
    //   那是【导出 CSV】想要的顺序 —— 事故分析从头顺着读下来最顺。
    //   但界面不一样:操作员点开"报警历史"是想看"刚才发生了什么",
    //   让他自己翻到最后一行去找,等于没有这个界面。
    //
    //   【为什么在视图里倒,不去改 query 的 ORDER BY】
    //   因为两种顺序各有各的用处,改全局会连 CSV 导出一起改掉 ——
    //   而导出那份的顺序已经被现有测试和用户习惯了。
    //   这是"表现层的事在表现层解决"的典型:同一份数据,
    //   给人看的倒着排,给机器读的顺着排。
    std::reverse(rows.begin(), rows.end());

    table_->setRowCount(static_cast<int>(rows.size()));
    for (int i = 0; i < rows.size(); ++i) {
        const AlarmRecorder::Record& r = rows.at(i);
        table_->setItem(i, kColTime,      new QTableWidgetItem(r.at.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
        table_->setItem(i, kColName,      new QTableWidgetItem(r.name));
        // ★ fromUtf8,不是 fromLatin1。
        //   levelDisplay/actionDisplay 返回的是源码里的 UTF-8 字面量,
        //   而"严"在 UTF-8 里是 3 个字节(E4 B8 A5)。fromLatin1 会把这 3 个字节
        //   当成 3 个 Latin-1 字符,表格里显示成 "ä¸¥" —— 等级和动作两列全是乱码。
        //   这条用例就是被这个坑绊红的:报错里 Actual 打印成 ä¸¥,
        //   一眼看过去像"实际值就是严重",其实比的是乱码。
        auto* lvl = new QTableWidgetItem(QString::fromUtf8(levelDisplay(r.level)));
        // ★ 等级上色 + 加粗。
        //   历史里四档全是黑字,而用户扫一眼历史最先找的就是"哪几条最重"。
        //   【和 F3 给 AlarmListControl 上色是同一个理由】
        //   报警列表里每行都是报警,没有"这行没报警"作参照,不分色就分不出轻重。
        //   色值和 AlarmListControl 一致 —— 同一个产品里"危急=这个颜色"
        //   必须处处相同,否则用户要记两套映射。
        //   同时加粗:只靠颜色区分,对色觉障碍用户和黑白打印出来的报表无效。
        const QColor c = levelColor(r.level);
        if (c.isValid()) {
            lvl->setForeground(c);
            QFont f = lvl->font();
            f.setBold(true);
            lvl->setFont(f);
        }
        table_->setItem(i, kColLevel, lvl);
        table_->setItem(i, kColAction,    new QTableWidgetItem(QString::fromUtf8(actionDisplay(r.action))));
        table_->setItem(i, kColVariable,  new QTableWidgetItem(r.variableName));
        table_->setItem(i, kColValue,     new QTableWidgetItem(r.triggerValue));
        table_->setItem(i, kColThreshold, new QTableWidgetItem(r.threshold));
    }

    // ★ 0 条必须说清楚是"这段时间没有记录",而不是留一张空表。
    //   空表有两种解释(没有报警 / 查错了),而用户分辨不了。
    if (rows.isEmpty()) {
        statusLabel_->setText(tr("该条件下没有记录。"
                                 "如果是刚发生过报警,请检查时间范围是否选对、等级是否选高了。"));
        statusLabel_->setStyleSheet(QStringLiteral("color:#a60;"));
    } else {
        statusLabel_->setStyleSheet(QString());
        statusLabel_->setText(tr("共 %1 条   (%2 → %3,最低等级:%4)")
            .arg(rows.size())
            .arg(fromEdit_->dateTime().toString(QStringLiteral("MM-dd HH:mm")))
            .arg(toEdit_->dateTime().toString(QStringLiteral("MM-dd HH:mm")))
            .arg(levelCombo_->currentText()));
    }
}

// ---------------------------------------------------------------------------
// 测试出口
// ---------------------------------------------------------------------------

int    AlarmHistoryView::rowCount() const { return table_ ? table_->rowCount() : 0; }

QString AlarmHistoryView::timeAt(int row) const {
    auto* it = table_->item(row, kColTime);
    return it ? it->text() : QString();
}
QString AlarmHistoryView::nameAt(int row) const {
    auto* it = table_->item(row, kColName);
    return it ? it->text() : QString();
}
QString AlarmHistoryView::levelTextAt(int row) const {
    auto* it = table_->item(row, kColLevel);
    return it ? it->text() : QString();
}
QString AlarmHistoryView::actionTextAt(int row) const {
    auto* it = table_->item(row, kColAction);
    return it ? it->text() : QString();
}
QString AlarmHistoryView::variableAt(int row) const {
    auto* it = table_->item(row, kColVariable);
    return it ? it->text() : QString();
}
QString AlarmHistoryView::statusText() const { return statusLabel_ ? statusLabel_->text() : QString(); }

QDateTime AlarmHistoryView::from() const { return fromEdit_->dateTime(); }
QDateTime AlarmHistoryView::to()   const { return toEdit_->dateTime(); }
int       AlarmHistoryView::minLevel() const { return levelCombo_->currentData().toInt(); }

} // namespace qwhmi::history