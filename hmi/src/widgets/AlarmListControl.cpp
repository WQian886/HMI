// ============================================================================
// AlarmListControl 实现
// ============================================================================

#include "widgets/AlarmListControl.h"
#include "../../Alarm.h"
#include "../../AlarmManager.h"
#include "auth/CurrentUser.h"
#include "auth/Permission.h"

#include <QColor>
#include <QGraphicsProxyWidget>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace qwhmi::widgets {

namespace {

// ★ F3:4 档等级 → (文字, 底色, 字色)
//
// 【为什么以前只有 Critical/Emergency 有颜色】原来只给这两档上了红底,
//   Info / Warning 跟"这一行没有报警"长得一模一样 —— 白底黑字。
//   而报警列表里【每一行都是报警】,没有"没报警"这个参照物。
//   结果就是:操作员扫一眼列表,真正跳出来的只有红色的两档,
//   Warning(该去现场看一眼)跟 Info(记一下就行)在视觉上完全等价。
//   这是本项目第 N 次"代码里写了但用户看不到差别"。
//
// 【为什么用低饱和底色而不是纯饱和色】工业屏常年亮着,大面积高饱和红/黄
//   看久了刺眼还容易和"这行被选中"混淆。底色压到浅色调,靠字色定对比,
//   四档之间的色相差异仍能一眼分出。
struct LevelStyle { QString text; QColor bg; QColor fg; };

LevelStyle styleFor(AlarmLevel level) {
    switch (level) {
        // 信息 —— 冷灰蓝,最不显眼,符合"知会一声"的定位
        case AlarmLevel::Info:     return {"Info",     QColor(222, 232, 242), QColor( 40,  60,  90)};
        // 警告 —— 黄,对应现场该去看一眼
        case AlarmLevel::Warning:  return {"Warning",  QColor(255, 240, 190), QColor(120,  80,   0)};
        // 严重 —— 橙红
        case AlarmLevel::Critical: return {"Critical", QColor(255, 205, 195), QColor(150,  20,  10)};
        // 危急 —— 深红 + 白字,唯一用白字的档(最深 = 最重)
        case AlarmLevel::Emergency:return {"Emergency",QColor(205,  40,  40), QColor(255, 255, 255)};
    }
    return {"?", QColor(), QColor(Qt::black)};   // 不该到这
}

} // namespace

AlarmListControl::AlarmListControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    setSize(QSizeF(320, 100));
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);

    // ★ F3:表格外面套一层 container,里面竖排放 计数标签 + 表格。
    //   【为什么以前是"一个表格独占整个控件"】原来 proxy 直接包 table_,
    //   整个控件就是一张表,没有地方放"还有几条没确认"。
    //   而未确认计数是工业 HMI 的硬要求:操作员需要一眼知道
    //   "我手上欠了几条没处理",而不是自己数行。
    //   —— 不想数行,或者干脆没意识到还有活儿,是漏处置报警的头号原因。
    container_ = new QWidget;
    auto* lay = new QVBoxLayout(container_);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    countLabel_ = new QLabel;
    countLabel_->setContentsMargins(4, 2, 4, 2);
    // ★ 无报警时也要显示"未确认:0",不能空白。
    //   空白 = "这个功能没装/没数据",0 = "确实没有未处理的"。
    //   这两件事必须区分开,否则又是一个"看不出是没数据还是没事"的静默失败。
    countLabel_->setText(QStringLiteral("未确认:0"));
    lay->addWidget(countLabel_);

    table_ = new QTableWidget;
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({"时间", "报警名", "等级", "值", "状态"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);   // 只读
    // ★ F3:单选 + 双击确认。单选是为了"双击哪行就确认哪行"不会歧义。
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(table_, &QTableWidget::cellDoubleClicked,
            this, &AlarmListControl::onCellDoubleClicked);
    lay->addWidget(table_);

    proxy_ = new QGraphicsProxyWidget(this);
    proxy_->setWidget(container_);
    // ★ E.4b 的设置(编辑模式下)让父收事件,父才能选中/拖动。
    //   F3 让它变成"按模式切换" —— 见 onRuntimeModeChanged。
    proxy_->setAcceptedMouseButtons(Qt::NoButton);

    // ★ 修复:E.4b 缩放后 table widget 不跟变 — 监听 sizeChanged 更新 container + 表格
    //   setMinimumSize 只能限制最小值,不强制 resize;必须同时调 resize 才能让 widget
    //   视觉跟缩,否则用户看到的是 "data 缩了但 widget 视觉没缩" 的撕裂状态
    //   ★ F3:尺寸给 container(它才是 proxy 里的顶层 widget),表格靠布局跟。
    //     以前直接给 table_ 设 minSize,现在表格在布局里,再单独 resize 它
    //     会跟布局打架(布局下一帧又给改回去,表现为"拖一下闪一下")。
    connect(this, &ControlBase::sizeChanged, this, [this]() {
        if (container_) {
            const int w = static_cast<int>(size().width());
            const int h = static_cast<int>(size().height());
            container_->setMinimumSize(w, h);
            container_->resize(w, h);
        }
    });
    {
        const int w = static_cast<int>(size().width());
        const int h = static_cast<int>(size().height());
        container_->setMinimumSize(w, h);
        container_->resize(w, h);
    }

    registerControlType("AlarmList", []() -> ControlBase* { return new AlarmListControl(); });
}

AlarmListControl::~AlarmListControl() = default;

// ★ F3(2026-09-30):运行时模式 → 代理吃不吃鼠标
//
// 【这是 F3 和 E.4b 的正面冲突,必须说清楚】
//   E.4b(2026-09-2x)把 proxy_->setAcceptedMouseButtons 设成 Qt::NoButton,
//   修的是"控件在编辑器里选不中、拖不动" —— 代理吃了鼠标事件,父收不到 press。
//   修法是让代理【不吃】事件,事件穿透到父 item。
//
//   但 NoButton 的直接后果是:内嵌的 QTableWidget 也一个鼠标事件都收不到。
//   连选中行都做不到,双击确认更是无从谈起 —— 而 F3 要加的正是双击确认。
//
//   两边都是对的,需求是冲突的,所以必须能切:
//     · 编辑模式(默认):NoButton —— 父收事件,选得中/拖得动/8 个 handle 挂得上
//     · 运行时模式  :LeftButton —— 表格收事件,点得动、能双击确认
//   运行时画布上控件不需要被选中或拖动(那是编辑器的活),所以这个切换没有代价。
void AlarmListControl::onRuntimeModeChanged(bool runtime) {
    if (!proxy_) return;
    proxy_->setAcceptedMouseButtons(runtime ? Qt::LeftButton : Qt::NoButton);
    // ★ 运行时模式下父不该还能被拖动 —— 用户是在操作一块屏,不是在挪窗口。
    //   不关的话,运行时双击确认可能被当成"拖控件",第一次点击就把报警列表拖走了。
    //   现场被拖走的报警列表 = 报警从此看不见,这比不能确认更糟。
    setFlag(QGraphicsItem::ItemIsMovable, !runtime);
    setFlag(QGraphicsItem::ItemIsSelectable, !runtime);
}

// ★ F3:拿到 / 失去 AlarmManager
//   【为什么要有这条】RuntimeCanvas 以前【从来没调过 bindAlarmManager】,
//   运行时画布上的报警列表 alarmMgr_ 恒为 nullptr,永远空白且零报错。
//   基类 setAlarmManager 现在统一负责注入,这里只管【响应】:
//     · 非空 → 接信号 + 刷一次
//     · nullptr → 断信号 + 清空表格(别把上一个 manager 的报警留在这儿冒充当前状态)
void AlarmListControl::onAlarmManagerChanged(AlarmManager* mgr) {
    if (mgr == alarmMgr_) return;
    if (alarmMgr_) alarmMgr_->disconnect(this);
    alarmMgr_ = mgr;
    if (table_) {
        table_->setRowCount(0);
        idToRow_.clear();
        rowToId_.clear();
    }
    if (alarmMgr_) {
        connect(alarmMgr_, &AlarmManager::alarmTriggered,
                this, &AlarmListControl::onAlarmTriggered);
        connect(alarmMgr_, &AlarmManager::alarmRecovered,
                this, &AlarmListControl::onAlarmRecovered);
        connect(alarmMgr_, &AlarmManager::alarmAcknowledged,
                this, &AlarmListControl::onAlarmAcknowledged);
        connect(alarmMgr_, &AlarmManager::allCleared,
                this, &AlarmListControl::onAllCleared);
        refreshNow();
    }
    refreshCount();
}

void AlarmListControl::bindAlarmManager(AlarmManager* mgr) {
    if (alarmMgr_) return;
    // 走基类的路,好处是"第二次 bind 换 manager"也能正确断掉旧的。
    // 保留 if(alarmMgr_) return 是为了不改变既有调用方的语义
    // (hmi.cpp:937 和 tst_AlarmListControl 都依赖"重复 bind 无副作用")。
    setAlarmManager(mgr);
}

void AlarmListControl::refreshNow() {
    if (!alarmMgr_ || !table_) return;
    table_->setRowCount(0);
    idToRow_.clear();
    rowToId_.clear();
    // Alarm 在全局命名空间(Alarm.h 没有 namespace 包裹)
    // 头文件用裸 Alarm(配前向声明),这里也用裸名 — 编译器穿透 qwhmi::widgets 找到 ::Alarm
    for (const Alarm& a : alarmMgr_->activeAlarms()) {
        addAlarmRow(a);
    }
    refreshCount();
}

void AlarmListControl::addAlarmRow(const Alarm& alarm) {
    if (!table_) return;
    const int row = table_->rowCount();
    table_->insertRow(row);
    idToRow_[alarm.id_] = row;
    rowToId_[row] = alarm.id_;

    table_->setItem(row, 0, new QTableWidgetItem(alarm.triggeredAt.toString("HH:mm:ss")));
    table_->setItem(row, 1, new QTableWidgetItem(alarm.name));
    table_->setItem(row, 2, new QTableWidgetItem(styleFor(alarm.level).text));
    table_->setItem(row, 3, new QTableWidgetItem(alarm.triggerValue));
    // ★ F3:状态文字【按 Alarm 自带的 state 填,不是无脑写"未确认"】。
    //   原来这里写死"未确认"。refreshNow() 在重画时会走这里 ——
    //   而 activeAlarms_ 里【确实存在已经确认过但还没恢复的报警】
    //   (acknowledge 只改 state,不删记录,是 5.5 有意为之:"复位不该清历史")。
    //   写死"未确认"的后果:操作员确认完之后,任何一次 refreshNow()
    //   都会把那行变回"未确认",看着像没确认过,于是又点一次。
    //   而 acknowledge 第二次会返回 false(状态已不是 Active),
    //   界面"点了没反应" —— 又是静默失败。
    //   等级列的颜色同理,由 repaintRow 统一按当前状态重刷。
    table_->setItem(row, 4, new QTableWidgetItem(
        alarm.state == AlarmState::Acknowledged ? QStringLiteral("已确认")
                                                : QStringLiteral("未确认")));
    repaintRow(row);
    refreshCount();
}

// ★ F3:按等级 + 状态给整行上色 —— 配色的唯一入口
//   【为什么整行都刷,而不是只刷等级那一列】
//   只刷等级列的话,四档的差别被压在很窄的一格里,余光扫列表时看不出。
//   工业 HMI 的通行做法是整行着色:远处扫一眼就知道"红的多还是黄的多"。
//   【为什么"已确认"要淡化】已确认 = 操作员已经知道了,继续闪/继续抢注意力
//   就是在骗人。淡化成灰底,注意力自然转到还没确认的行上。
void AlarmListControl::repaintRow(int row) {
    if (!table_) return;
    if (row < 0 || row >= table_->rowCount()) return;
    auto* levelItem = table_->item(row, 2);
    auto* statusItem = table_->item(row, 4);
    if (!levelItem || !statusItem) return;

    const QString level = levelItem->text();
    const QString status = statusItem->text();

    // 由"等级文字"反查档位。存 enum 会更严谨,但要多存一个 UserRole,
    // 而等级列的文字本来就是 styleFor 的产物,两者必然一致。
    QColor bg, fg;
    if      (level == "Emergency") { bg = QColor(205,  40,  40); fg = QColor(255,255,255); }
    else if (level == "Critical")  { bg = QColor(255, 205, 195); fg = QColor(150, 20, 10); }
    else if (level == "Warning")   { bg = QColor(255, 240, 190); fg = QColor(120, 80,   0); }
    else                            { bg = QColor(222, 232, 242); fg = QColor( 40, 60,  90); }  // Info

    // 已确认 / 已恢复 → 灰底。让"还没处理的"自己跳出来。
    if (status != QStringLiteral("未确认")) {
        bg = QColor(240, 240, 240);
        fg = QColor(120, 120, 120);
    }
    for (int col = 0; col < table_->columnCount(); ++col) {
        if (auto* it = table_->item(row, col)) {
            it->setBackground(bg);
            it->setForeground(fg);
        }
    }
}

// ★ F3:重算未确认计数
//   【判据用 AlarmManager,不用表格行数】
//   表格里既有 Active 的,也有 Acknowledged 还没恢复的(记录不删是 5.5 有意为之),
//   行数 ≠ 未确认数。用行数算的话,操作员确认过的报警仍然被计入,
//   计数永远降不下来 —— 数字不反映真实待办,这个指示灯就废了。
int AlarmListControl::unacknowledgedCount() const {
    if (!alarmMgr_) return 0;
    int n = 0;
    const auto alarms = alarmMgr_->activeAlarms();
    for (const Alarm& a : alarms) {
        if (a.state == AlarmState::Active) ++n;
    }
    return n;
}

QString AlarmListControl::countText() const {
    return countLabel_ ? countLabel_->text() : QString();
}

void AlarmListControl::refreshCount() {
    if (!countLabel_) return;
    const int n = unacknowledgedCount();
    countLabel_->setText(QStringLiteral("未确认:%1").arg(n));
    // 有未确认时整个标签变醒目,没有时回到普通色。
    // 计数不只是"数字",还得是"信号" —— 数字要人去读,颜色不用。
    if (n > 0) {
        countLabel_->setStyleSheet(QStringLiteral("color:#c00000;font-weight:bold;"));
    } else {
        countLabel_->setStyleSheet(QStringLiteral("color:#606060;"));
    }
}

int AlarmListControl::rowCount() const { return table_ ? table_->rowCount() : 0; }

QString AlarmListControl::levelTextAt(int row) const {
    if (!table_) return QString();
    auto* it = table_->item(row, 2);
    return it ? it->text() : QString();
}

QColor AlarmListControl::levelColorAt(int row) const {
    if (!table_) return QColor();
    auto* it = table_->item(row, 2);
    return it ? it->background().color() : QColor();
}

qint64 AlarmListControl::alarmIdAtRow(int row) const { return rowToId_.value(row, 0); }
void AlarmListControl::setAlarmIdAtRow(int row, qint64 id) { rowToId_[row] = id; }

int AlarmListControl::findRowByAlarmId(qint64 id) const {
    return idToRow_.value(id, -1);
}

void AlarmListControl::onAlarmTriggered(const Alarm& alarm) {
    addAlarmRow(alarm);
}

void AlarmListControl::onAlarmRecovered(qint64 id) {
    updateAlarmRow(id, "已恢复");
}

void AlarmListControl::onAlarmAcknowledged(qint64 id) {
    updateAlarmRow(id, "已确认");
}

void AlarmListControl::updateAlarmRow(qint64 id, const QString& stateText) {
    const int row = findRowByAlarmId(id);
    if (row < 0 || !table_) return;
    auto* item = table_->item(row, 4);
    if (item) item->setText(stateText);
    // ★ F3:状态变了 → 配色跟着变(已确认/已恢复要淡化成灰底),
    //   计数也要重算(确认一条,未确认数就该减一)。
    //   以前只改文字不改颜色,所以确认过的行还留着红底,
    //   看着像"还没处理" —— 跟 hmi.cpp 5.5 那个闪烁残留是同一类毛病。
    repaintRow(row);
    refreshCount();
}

// ★ F3(2026-09-30):双击行 → 确认这条报警
//
// 【为什么是双击不是单击】单击在触摸屏上太容易误触 ——
//   操作员想看某条详情,结果把报警确认掉了。确认是【有后果的动作】
//   (写审计日志 alarm.ack、操作员的责任被记录在案),不该随手就能做。
//   双击是工业触摸屏的通用习惯,且单击保留给"选中查看"。
//
// 【权限检查为什么不在这儿做】AlarmManager::acknowledge 内部已经查了
//   AcknowledgeAlarm 权限(B3)。这里【不重复查】—— 两处各查一次的话,
//   迟早有一处改了另一处没改,表现是"界面禁用了但其实有权限"或反之。
//   唯一权威在 AlarmManager,UI 只负责把它的 false 讲清楚。
void AlarmListControl::onCellDoubleClicked(int row, int col) {
    Q_UNUSED(col);
    if (!table_ || row < 0 || row >= table_->rowCount()) return;
    if (!alarmMgr_) {
        // 没有 manager 却收到了点击 —— 说明这个控件没接上数据源。
        // 什么都不做的话,操作员看到的是"双击了但什么也没发生",
        // 和"没权限"完全一样。明确说没接上,是两种完全不同的排查方向。
        countLabel_->setText(QStringLiteral("未确认:0(未接入报警数据)"));
        return;
    }
    const qint64 id = alarmIdAtRow(row);
    if (id == 0) return;   // 这一行没有对应 id(半截行),不动

    if (alarmMgr_->acknowledge(id)) return;   // 成功 → alarmAcknowledged 信号会回来刷新

    // 失败 → 分三种情况给不同提示,不能笼统一句"失败"
    if (!qwhmi::auth::checkPermission(qwhmi::auth::CurrentUser::instance().role(),
                                     qwhmi::auth::Permission::AcknowledgeAlarm)) {
        countLabel_->setText(QStringLiteral("未确认:%1(无确认权限)")
                                 .arg(unacknowledgedCount()));
        return;
    }
    // 有权限却没确认成 → 已经不是 Active(已确认过 / 已恢复)。
    // 这不是错误,只是没必要重复做。刷新一次让界面显示真状态即可,
    // 不用弹窗打断操作员(在触摸屏上弹窗很烦,而且这不是他需要知道的事)。
    refreshNow();
}

void AlarmListControl::onAllCleared() {
    if (!table_) return;
    table_->setRowCount(0);
    idToRow_.clear();
    rowToId_.clear();
    // ★ F3:清表必须同时清计数,否则标签停在"未确认:5"而表格已经空了。
    refreshCount();
}

QRectF AlarmListControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void AlarmListControl::paintContent(QPainter* p,
                             const QStyleOptionGraphicsItem* option,
                             QWidget* widget) {
    Q_UNUSED(p); Q_UNUSED(option); Q_UNUSED(widget);
    // ★ proxy 自己渲染 — paint 不画东西
}

} // namespace qwhmi::widgets
