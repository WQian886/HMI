// ============================================================================
// AlarmListControl 实现
// ============================================================================

#include "widgets/AlarmListControl.h"
#include "../../Alarm.h"
#include "../../AlarmManager.h"
#include "alarm/AlarmRowPalette.h"
#include "alarm/AlarmTablePresenter.h"
#include "auth/CurrentUser.h"
#include "auth/Permission.h"
#include "logging/Logging.h"   // hmiAlarm(qCWarning 用)

#include <QColor>
#include <QDebug>
#include <QTimer>
#include <QGraphicsProxyWidget>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace qwhmi::widgets {

namespace {

// ★ 2026-10-10:报警名那一列的下标(时间/报警名/等级/值/状态)。
//   写常量而不是裸 1:这一列的宽度取决于用户自己起什么名,是唯一有理由吃余量的列。
constexpr int kColName = 1;
constexpr int kColLevel = 2;
constexpr int kColStatus = 4;

// ★ 等级 → 显示文字
//
//   【文字留在本地,只有【颜色】上移到 AlarmRowPalette】
//   理由:两套 UI 现在一个显示英文、一个显示中文(主画面 hmi.cpp 的 alarmLevelName),
//   那是产品的一处口径不一致 —— 但它【不是颜色】。文字是各界面自己的呈现选择,
//   而"哪一档该是什么颜色、哪一档该闪"是全产品一条规则,必须只有一处说了算。
//   把两者混在一个函数里,改语言就会顺手改到配色。
//   ★ 统一文字语言是另一件事,已记在 docs/现状.md,不在本次范围。
QString levelText(AlarmLevel level) {
    switch (level) {
        case AlarmLevel::Info:      return QStringLiteral("Info");
        case AlarmLevel::Warning:   return QStringLiteral("Warning");
        case AlarmLevel::Critical:  return QStringLiteral("Critical");
        case AlarmLevel::Emergency: return QStringLiteral("Emergency");
    }
    return QStringLiteral("?");   // 不该到这
}

// ★ 业务状态 → 状态列文字
//
//   ★ 文字从【AlarmState】派生,颜色从【它映射出的视觉状态】派生 ——
//     两者出自同一个 st,不可能各说各话。
//     原来 addAlarmRow 写 "Acknowledged ? 已确认 : 未确认",
//     updateAlarmRow 又从调用点收一个字符串("已确认"/"已恢复")——
//     文字有两个来源,就可能不一致,而"文字说已确认、颜色说未确认"
//     会让那一行既灰着又在闪,操作员看不出该管哪条。
//
//   ★ 注意 Cleared(已恢复)不要被写成"未确认":F1 修完之后
//     恢复的记录会先进 archived_ 再从 activeAlarms_ 移除,
//     所以表里本来不该再有 Cleared —— 但万一有一行(clearAll 后的残留半截行),
//     显示"未确认"就是在一本正经地撒谎。
QString stateTextFor(AlarmState st) {
    // ★ 2026-10-24:改调 AlarmTablePresenter —— 状态文字两套 UI 共用一份。
    return qwhmi::alarm::stateText(st);
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

    // ★ 2026-10-14:计数行右侧放一个"全部确认"按钮。
    //   为什么是按钮而不是右键菜单:这个控件跑在触摸屏上,
    //   而右键菜单在触摸屏上要么够不着、要么要点两次。
    //   为什么需要它:现场一次异常往往同时来七八条,
    //   只有双击单条的话操作员得挨个点,而他此时最该做的是去现场看一眼。
    ackAllButton_ = new QPushButton(QStringLiteral("全部确认"));
    ackAllButton_->setAutoDefault(false);
    ackAllButton_->setFocusPolicy(Qt::NoFocus);
    // ★ 没有未确认时要灰掉 —— 否则按钮一直亮着,
    //   按下去只会得到"当前没有未确认的报警",像坏了。
    ackAllButton_->setEnabled(false);
    connect(ackAllButton_, &QPushButton::clicked, this, &AlarmListControl::acknowledgeAll);

    auto* countRow = new QHBoxLayout;
    countRow->setContentsMargins(0, 0, 0, 0);
    countRow->setSpacing(4);
    countRow->addWidget(countLabel_, 1);        // 计数占满剩余宽度
    countRow->addWidget(ackAllButton_, 0);      // 按钮按内容宽
    lay->addLayout(countRow);

    table_ = new QTableWidget;
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({"时间", "报警名", "等级", "值", "状态"});
    // ★ 拉伸【报警名】那一列,不是最后一列(2026-10-10)。
    //   原来 setStretchLastSection(true) —— 余量全给"状态"(就"未确认"三个字),
    //   实测 876px 视口下摊到 476px,空掉 440px;而报警名只有 100px,
    //   真实报警名需要 135px,被截断。
    //   ★ E.5 之后操作员实际盯的是【运行时画布】这张表,不是主窗口那张,
    //     所以这一处的优先级不低于 hmi.cpp。
    //   做法与 AlarmHistoryView / AlarmConfigDialog 一致(F5-a):
    //   按内容收紧,余量给宽度最不确定的一列。
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    table_->horizontalHeader()->setStretchLastSection(false);
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

    // ★ F3-b(2026-10-08):闪烁定时器
    //   500ms 与 hmi.cpp:423 的 alarmBlinkTimer_ 一致 —— 见头文件里为什么必须同速。
    //   ★ 【一开始不 start】updateBlinkTimer() 才是唯一的开关。
    //   构造完就无条件开表的话,这个控件一被放进画布(哪怕工程里一条报警都没有)
    //   就开始空转,而"没报警"恰恰是最常见的状态。
    blinkTimer_ = new QTimer(this);
    blinkTimer_->setInterval(500);
    connect(blinkTimer_, &QTimer::timeout, this, &AlarmListControl::onBlinkTick);
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
    } else {
        // ★ F3-b:换 manager 时表格被清空了(上面 setRowCount(0)),
        //   闪烁也必须跟着停 —— 否则"没接上数据源的控件"在后台一直闪。
        updateBlinkTimer();
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
    // ★ 2026-10-24:整行的创建改调 AlarmTablePresenter(与主画面 hmi.cpp 共用)。
    //   原来这一段在这里自己写 5 个 setItem —— 而主画面那份当时【少写了状态列】,
    //   两套各写一份就漂出了"主画面报警表整张废了"那个事故(见
    //   src/alarm/AlarmTablePresenter.h 头部)。现在只有一份。
    //   等级文字两套 UI 故意不同(主画面中文、画布英文),所以由这里传进去。
    //
    //   ★ 状态文字【按 Alarm 自带的 state 填,不是无脑写"未确认"】。
    //     原来这里写死"未确认"。refreshNow() 在重画时会走这里 ——
    //     而 activeAlarms_ 里【确实存在已经确认过但还没恢复的报警】
    //     (acknowledge 只改 state,不删记录,是 5.5 有意为之:"复位不该清历史")。
    //     写死"未确认"的后果:操作员确认完之后,任何一次 refreshNow()
    //     都会把那行变回"未确认",看着像没确认过,于是再点一次。
    //     而 acknowledge 第二次会返回 false(状态已不是 Active),
    //     界面"点了没反应" —— 又是静默失败。
    const int row = table_->rowCount();   // appendAlarmRow 会往这个下标插
    idToRow_[alarm.id_] = row;
    rowToId_[row] = alarm.id_;

    qwhmi::alarm::appendAlarmRow(table_, alarm, levelText(alarm.level));

    repaintRow(row);
    refreshCount();
    // ★ F3-b:新来一条可能"该闪",也可能来的是已确认的。
    //   每次都重算一遍 —— 比在这里判断 level/state 更不容易漏,
    //   少判断一次的后果是"该闪的不闪"或"已确认的还在闪",两个都是骗操作员。
    updateBlinkTimer();
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
    // ★ 2026-10-14:配色决策整段搬进 AlarmRowPalette,这里只剩"取到结果、刷上去"。
    //   原来这一整个函数(F3 + F3-b 两次加料)就是配色的第二个权威,
    //   而 hmi.cpp 那套是第三个 —— 三个权威必然漂,详见 AlarmRowPalette.h 开头。
    // ★ 2026-10-24:改调 AlarmTablePresenter —— 配色的【行】这一层也收到一处。
    //   原来主画面和这里各写一份"读等级列 + 读状态列 → 上色",
    //   而主画面那份读的状态列【从来没被创建过】(BUGFIX-C3)——
    //   两套各写一份就漂成了"主画面报警表一格颜色都没有"。
    qwhmi::alarm::repaintRow(table_, row, blinkOn_);
}

// ★ 从行里读回等级 —— UserRole 缺失时【向"更像报警"的方向兜底】
//
//   【为什么不用"最不显眼"兜底】
//   兜底值必须偏向【多报】而不是【漏报】 —— 本项目在 F2 里的原话是
//   "压错了报警,比多报一条危险得多"。同理,一行读不出等级时
//   显示成 Warning + 未确认,后果是"多吸引一点注意力";
//   显示成 Info + 已处理,后果是"一条真报警看着像已经处理过了",那危险得多。
//   ★ 但光兜底不够 —— 静默兜底正是本项目反复吃亏的那一类,所以配 qCWarning。
AlarmLevel AlarmListControl::levelAt(int row) const {
    // ★ 2026-10-24:改调 AlarmTablePresenter(两套 UI 共用同一份读法与兜底)。
    return qwhmi::alarm::rowLevel(table_, row);
}

qwhmi::alarm::RowVisualState AlarmListControl::visualStateAt(int row) const {
    // ★ 2026-10-24:改调 AlarmTablePresenter(两套 UI 共用同一份读法与兜底)。
    return qwhmi::alarm::rowVisualState(table_, row);
}

// ★ F3-b:这一行此刻该不该闪
//
//   ★ 2026-10-14:判据搬到 AlarmRowPalette::shouldBlink。
//     原来这里是"状态文字==未确认 && 等级文字∈{Critical,Emergency}",
//     而 hmi.cpp 那套是"状态文字==未确认 && 等级文字∈{严重,危急}" ——
//     两边一个认英文一个认中文,判据天生可能对不上,
//     而"两套 UI 的闪烁判据必须一致"恰恰是 F3-b 对齐 500ms 时强调的硬要求。
bool AlarmListControl::rowShouldBlink(int row) const {
    if (!table_) return false;
    if (row < 0 || row >= table_->rowCount()) return false;
    return qwhmi::alarm::rowShouldBlink(table_, row);
}

int AlarmListControl::blinkingRowCount() const {
    if (!table_) return 0;
    int n = 0;
    for (int row = 0; row < table_->rowCount(); ++row)
        if (rowShouldBlink(row)) ++n;
    return n;
}

bool AlarmListControl::blinkOn() const { return blinkOn_; }
bool AlarmListControl::blinkEnabled() const { return blinkTimer_ && blinkTimer_->isActive(); }
void AlarmListControl::advanceBlinkForTest() { onBlinkTick(); }

// ★ F3-b:该闪的行一个都没了就停表
//
// 【为什么必须有这个】这是工控机上的 7×24 程序,不是开完就关的桌面软件。
// 定时器一开就会一直唤醒事件循环 —— 没有报警时也照转,
// 等于让 CPU 永远醒着做一件没有产出的事。
// 而"报警全处理完了还闪"本身也是错的:那是在骗操作员。
void AlarmListControl::updateBlinkTimer() {
    if (!blinkTimer_) return;
    if (blinkingRowCount() > 0) {
        if (!blinkTimer_->isActive()) blinkTimer_->start();
    } else {
        blinkTimer_->stop();
        // ★ 停下来之前把相位复位。
        //   不复位的话,下一次启动的【第一帧】是"暗相",而那一行的
        //   当前颜色可能还停在亮相的深色上(上一次 repaint 留下的)。
        //   于是新报警刚出现时是深色,要等 500ms 才切回本档色 ——
        //   用户看到的是"新报警的颜色不对,过一会儿才对"。
        if (blinkOn_) {
            blinkOn_ = false;
            for (int row = 0; row < (table_ ? table_->rowCount() : 0); ++row)
                repaintRow(row);
        }
    }
}

void AlarmListControl::onBlinkTick() {
    blinkOn_ = !blinkOn_;
    // ★ 只重画"该闪的那些行",不是全表。
    //   全表重画在行数多时是纯粹的浪费,而闪烁本来就是个"局部信号"。
    for (int row = 0; row < (table_ ? table_->rowCount() : 0); ++row)
        if (rowShouldBlink(row)) repaintRow(row);
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
    // ★ 2026-10-14:没有未确认就把"全部确认"灰掉。
    //   不灰的话按钮一直亮着,按下去只得到一句"没有可确认的" —— 像坏了。
    //   ★ 必须在【所有】会改变确认状态的路径上生效,所以跟着 refreshCount 走,
    //     而不是各调用点自己记得 —— 后者迟早漏一处。
    if (ackAllButton_) ackAllButton_->setEnabled(n > 0);
}

// ★ 2026-10-14:一次确认全部未确认报警。
//
//   【为什么这是"接线"而不是"新功能"】
//   AlarmManager::acknowledgeAll() 从 5.5 就写好了,F3 又把它改成返回真实条数,
//   也有回归测试 —— 但全项目【没有任何入口调它】。
//   它唯一的调用点 hmi.cpp:867 在 count() > 0 的守卫后面,而 acknowledgeAll
//   遍历的正是 activeAlarms_ —— 守卫一旦通过,里面必然是空的,
//   于是【永远确认不到任何东西】。建好了没接线。
//
//   【为什么不弹窗】这个控件跑在触摸屏上,弹窗要么被别的窗口挡住、
//   要么要点两次才能关掉 —— 同文件 onCellDoubleClicked 里已经因为同样的
//   理由明确选择了"不弹窗,改计数标签",这里跟着一致。
//   代价:标签上那条提示会被下一次 refreshCount 冲掉。
//   ★ 那是有意的取舍,不是遗漏:计数必须永远是真的,
//     宁可提示短暂,也不要为了留住提示让计数撒谎。
void AlarmListControl::acknowledgeAll() {
    if (!alarmMgr_) {
        // 与双击那条路径同一个理由:没接上数据源要说出来,
        // 否则"点了没反应"和"没权限"在现场完全分不开。
        if (countLabel_) countLabel_->setText(QStringLiteral("未确认:0(未接入报警数据)"));
        return;
    }
    const int pending = unacknowledgedCount();
    if (pending == 0) return;   // 按钮本来就该是灰的;真走到这多半是竞态

    const int acked = alarmMgr_->acknowledgeAll();
    if (acked > 0) {
        // 每条成功的 acknowledge 都会 emit alarmAcknowledged → updateAlarmRow →
        // refreshCount,所以计数和按钮状态在这之前就已经更新过了。
        return;
    }

    // 一条都没确认成 —— 分清"没权限"和"本来就没什么可确认的"。
    if (!qwhmi::auth::checkPermission(qwhmi::auth::CurrentUser::instance().role(),
                                     qwhmi::auth::Permission::AcknowledgeAlarm)) {
        if (countLabel_)
            countLabel_->setText(QStringLiteral("未确认:%1(无确认权限)").arg(pending));
        return;
    }
    // 有权限却没确认成 → 刚被别处清掉了(比如同一时刻另一路先恢复了)。
    // 不是错误,不必打扰操作员。
    qCInfo(hmiAlarm) << "[AlarmListControl] 全部确认无事可做:待确认" << pending
                     << "条,但 acknowledgeAll 返回" << acked;
}

int AlarmListControl::rowCount() const { return table_ ? table_->rowCount() : 0; }

bool AlarmListControl::ackAllEnabled() const {
    return ackAllButton_ && ackAllButton_->isEnabled();
}

QString AlarmListControl::levelTextAt(int row) const {
    if (!table_) return QString();
    auto* it = table_->item(row, kColLevel);
    return it ? it->text() : QString();
}

QColor AlarmListControl::levelColorAt(int row) const {
    if (!table_) return QColor();
    auto* it = table_->item(row, kColLevel);
    return it ? it->background().color() : QColor();
}

QColor AlarmListControl::levelTextColorAt(int row) const {
    if (!table_) return QColor();
    auto* it = table_->item(row, kColLevel);
    return it ? it->foreground().color() : QColor();
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
    updateAlarmRow(id, AlarmState::Cleared);
}

void AlarmListControl::onAlarmAcknowledged(qint64 id) {
    updateAlarmRow(id, AlarmState::Acknowledged);
}

// ★ 2026-10-14:形参从 QString 改成 AlarmState。
//
//   原来这里收一个状态文字字符串("已确认"/"已恢复"),而文字和 UserRole
//   又是两份东西 —— 改文字时很容易忘了改 UserRole,或者反过来。
//   收 enum 之后,文字和配色都由它派生,不存在"各说各话"。
void AlarmListControl::updateAlarmRow(qint64 id, AlarmState st) {
    const int row = findRowByAlarmId(id);
    if (row < 0 || !table_) return;
    // ★ 2026-10-24:改调 AlarmTablePresenter —— 文字与 UserRole 必须一起改,
    //   这个不变量现在由一处保证(原来两边各写一份,主画面那份漏了整列)。
    qwhmi::alarm::setRowState(table_, row, st);
    // ★ F3:状态变了 → 配色跟着变(已确认/已恢复要淡化成灰底),
    //   计数也要重算(确认一条,未确认数就该减一)。
    //   以前只改文字不改颜色,所以确认过的行还留着红底,
    //   看着像"还没处理" —— 跟 hmi.cpp 5.5 那个闪烁残留是同一类毛病。
    repaintRow(row);
    refreshCount();
    // ★ F3-b:这一行刚从"该闪"变成"不该闪"(确认/恢复),
    //   如果它是最后一条,定时器必须停 —— 否则会一直空转。
    updateBlinkTimer();
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
    // ★ F3-b:清表同理 —— 表格都空了,闪烁必须停。
    //   少这一句的表现是"全清之后定时器还在转",而屏幕上什么都没有 ——
    //   这是本项目反复吃亏的那一类:后台还在为一个已经不存在的东西干活。
    updateBlinkTimer();
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
