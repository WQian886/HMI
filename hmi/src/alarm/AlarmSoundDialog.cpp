// 阶段 F · F5(2026-10-08):报警声音设置对话框 —— 实现

#include "alarm/AlarmSoundDialog.h"

#include "../../Alarm.h"
#include "../../AlarmSoundPlayer.h"
#include "../auth/CurrentUser.h"
#include "../auth/Permission.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTime>
#include <QTimeEdit>
#include <QVBoxLayout>

namespace qwhmi::alarm {

namespace {

// 4 档的显示名。顺序必须和 AlarmSoundPlayer::levelIndex 一致 ——
// 这里写死一份而不是反向查表,是为了让"用户看到的顺序"是固定的,
// 不会因为将来有人调枚举而悄悄变。
//
// ★ 【为什么要拆成"名字"和"举例"两列】
//   原来把两样塞进一个字符串:"信息(Info)—— 例:泵启动、模式切换"。
//   后果是四个复选框的宽度差出一倍多,左边框参差不齐,
//   而【用户要做的判断只有一件事:这一档响不响】——
//   举例是给人对照用的,不是选项本身,不该占掉和选项一样的视觉权重。
//   拆成两列之后,左边四个复选框宽度一致、可以竖着扫一眼,
//   右边的举例单独一列,长短不一也不会把版面撑歪。
struct LevelRow {
    const char* name;
    const char* example;
};
const LevelRow kLevels[4] = {
    { QT_TRANSLATE_NOOP("AlarmSoundDialog", "信息"),
      QT_TRANSLATE_NOOP("AlarmSoundDialog", "例:泵启动、模式切换") },
    { QT_TRANSLATE_NOOP("AlarmSoundDialog", "警告"),
      QT_TRANSLATE_NOOP("AlarmSoundDialog", "例:接近上限,还能撑一会儿") },
    { QT_TRANSLATE_NOOP("AlarmSoundDialog", "严重"),
      QT_TRANSLATE_NOOP("AlarmSoundDialog", "例:已超限,设备受影响") },
    { QT_TRANSLATE_NOOP("AlarmSoundDialog", "危急"),
      QT_TRANSLATE_NOOP("AlarmSoundDialog", "例:停机、有危险") },
};

QString levelName(int i) {
    return QCoreApplication::translate("AlarmSoundDialog", kLevels[i].name);
}

QString levelExample(int i) {
    return QCoreApplication::translate("AlarmSoundDialog", kLevels[i].example);
}

// 说明文字专用的小标签。
// ★ 【为什么单独一个函数】三处说明文字如果各写各的,过两个月就会变成三种灰。
//   而"说明"和"选项"在视觉上必须分得开 —— 说明用灰字、正常字重,
//   否则用户分不清"这一行是能点的"还是"这一行是给我看的"。
QLabel* makeHint(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    // ★ wordWrap,不是硬编码 \n。
    //   原来三段说明都写死了换行位置,125% 缩放或换一款字体,
    //   行就断在奇怪的地方 —— 而且 QLabel 默认 wordWrap 是关的,
    //   不开的话长句会直接把对话框撑得比屏幕还宽。
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color:#666;"));
    // 说明是给人读的,不该抢选项的注意力:去掉焦点框和多余内边距。
    l->setMargin(0);
    return l;
}

} // namespace

AlarmSoundDialog::AlarmSoundDialog(AlarmSoundPlayer* player, QWidget* parent)
    : QDialog(parent), player_(player) {
    setWindowTitle(tr("报警声音设置"));
    // 【只有 player_ 为空才走"没有界面"这条路】
    //   权限检查放在 exec() 里,和 AlarmConfigDialog 一致 ——
    //   理由是同一个:按钮该灰还是弹窗,只能有一处说了算,
    //   两处各判一次就会出现"按钮亮着但点了说没权限"。
    if (!player_) {
        // 不给一个"能打开但什么都改不了"的空壳 —— 那会让用户以为功能坏了。
        auto* v = new QVBoxLayout(this);
        auto* lbl = new QLabel(tr("报警声音模块未启用,无法修改声音设置。"), this);
        v->addWidget(lbl);
        auto* box = new QDialogButtonBox(QDialogButtonBox::Ok, this);
        connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
        v->addWidget(box);
        return;
    }

    auto* root = new QVBoxLayout(this);
    // ★ 统一留白。原来全走 Qt 默认值,三个 GroupBox 挤在一起、
    //   说明文字贴着下一个框,整个对话框像一张没排版的表单。
    //   工业软件看着"贵",靠的就是这种克制的留白。
    root->setContentsMargins(16, 16, 16, 12);
    root->setSpacing(12);

    masterChk_ = new QCheckBox(tr("启用报警声音"), this);
    masterChk_->setObjectName(QStringLiteral("alarmSoundMasterChk"));
    root->addWidget(masterChk_);

    // ---- 每档等级 ----
    auto* lvlBox = new QGroupBox(tr("各等级是否发声"), this);
    auto* lvlLay = new QVBoxLayout(lvlBox);
    lvlLay->setSpacing(6);
    // ★ 用 QGridLayout,而不是四个竖排的复选框。
    //   竖排时每行宽度各自不同(名字 + 举例长度不一),
    //   右边参差不齐,竖着扫对不齐行 —— 而这个对话框的主要动作
    //   就是"对着这四行做勾选",对齐在这里最要紧。
    //   拆成两列之后,左边四个复选框宽度一致、可竖扫,
    //   右边举例长短不一也不会把版面撑歪。
    auto* lvlGrid = new QGridLayout;
    lvlGrid->setHorizontalSpacing(12);
    lvlGrid->setVerticalSpacing(4);
    lvlGrid->setColumnStretch(1, 1);   // 举例那一列吃掉多余宽度
    for (int i = 0; i < 4; ++i) {
        levelChk_[i] = new QCheckBox(levelName(i), lvlBox);
        levelChk_[i]->setObjectName(QStringLiteral("alarmSoundLevelChk%1").arg(i));
        lvlGrid->addWidget(levelChk_[i], i, 0);
        auto* eg = new QLabel(levelExample(i), lvlBox);
        eg->setStyleSheet(QStringLiteral("color:#777;"));
        lvlGrid->addWidget(eg, i, 1);
    }
    lvlLay->addLayout(lvlGrid);
    lvlLay->addSpacing(4);
    lvlLay->addWidget(makeHint(
        tr("信息级默认不响。现场最吵的往往是信息级报警,全都响的话"
           "操作员很快就会麻木,而麻木等于真报警也听不见。"),
        lvlBox));

    // ---- 试听:放在"各等级"这一组里,而不是孤零零占一整行 ----
    //   【为什么】它试的就是"这几档里勾中的最高那一档"。
    //   原来它夹在最后一个 GroupBox 和"确定/取消"中间,
    //   视觉上不属于任何一节,用户改完勾还要用眼睛去找它。
    //   挨着复选框放:改完勾,伸手就点。
    auto* previewBtn = new QPushButton(tr("试听"), lvlBox);
    previewBtn->setObjectName(QStringLiteral("alarmSoundPreviewBtn"));
    previewBtn->setToolTip(tr("按当前勾选里最高的一档试听一声。"
                              "不受静默时段和总开关影响 —— 它试的是声音本身。"));
    connect(previewBtn, &QPushButton::clicked, this, [this]() {
        // 试听【不受静默时段和已确认影响】—— 它试的是"声音本身",
        // 不是"此刻这条报警该不该响"。混在一起会导致用户在静默时段里
        // 点试听什么也听不到,以为坏了。
        if (player_) player_->previewBeep(highestEnabledLevel());
    });
    lvlLay->addSpacing(6);
    lvlLay->addWidget(previewBtn, 0, Qt::AlignLeft);

    root->addWidget(lvlBox);

    // ---- 重复提醒 ----
    auto* repBox = new QGroupBox(tr("未确认时重复提醒"), this);
    auto* repLay = new QVBoxLayout(repBox);
    repLay->setSpacing(6);
    repeatChk_ = new QCheckBox(tr("报警未确认前每隔一段时间再提醒一次"), repBox);
    repeatChk_->setObjectName(QStringLiteral("alarmSoundRepeatChk"));
    repLay->addWidget(repeatChk_);

    auto* repRow = new QHBoxLayout;
    repRow->setSpacing(8);
    repRow->addWidget(new QLabel(tr("提醒间隔:"), repBox));
    repeatSpin_ = new QSpinBox(repBox);
    // ★ 单位从毫秒改成秒。
    //   "提醒间隔: 5000 ms" 对现场的人是纯噪音 —— 谁的心算里有毫秒?
    //   而光一个"5000"更容易被误读成 5000 秒。
    //   存进 player 的仍然是毫秒(setSettings 的契约一个字没变),
    //   只是界面这层换算 —— 工程文件和已有设置都不受影响。
    //   为什么不用带小数的 spin:间隔本就是"人类节奏"(5 秒、30 秒),
    //   0.5 秒这种值对"提醒"没有意义,徒增一个看不懂的小数点。
    repeatSpin_->setRange(1, 600);
    repeatSpin_->setSingleStep(1);
    repeatSpin_->setSuffix(tr(" 秒"));
    repeatSpin_->setToolTip(tr("两次提醒之间隔多久。5 秒是常用值。"));
    repeatSpin_->setObjectName(QStringLiteral("alarmSoundRepeatSpin"));
    repRow->addWidget(repeatSpin_);
    repRow->addStretch();
    repLay->addLayout(repRow);
    repLay->addWidget(makeHint(
        tr("一次提醒只响一声,并按当前最高等级选声音 —— "
           "同时挂五条未确认报警时响五下,只会变成一片噪音。"),
        repBox));
    root->addWidget(repBox);

    // ---- 静默时段 ----
    auto* quietBox = new QGroupBox(tr("静默时段(免打扰)"), this);
    auto* quietLay = new QVBoxLayout(quietBox);
    quietLay->setSpacing(6);
    quietChk_ = new QCheckBox(tr("在指定时段内不响"), quietBox);
    quietChk_->setObjectName(QStringLiteral("alarmSoundQuietChk"));
    quietLay->addWidget(quietChk_);

    auto* quietRow = new QHBoxLayout;
    quietRow->setSpacing(8);
    quietRow->addWidget(new QLabel(tr("从:"), quietBox));
    quietStart_ = new QTimeEdit(quietBox);
    quietStart_->setDisplayFormat(QStringLiteral("HH:mm"));
    quietStart_->setObjectName(QStringLiteral("alarmSoundQuietStart"));
    quietRow->addWidget(quietStart_);
    quietRow->addWidget(new QLabel(tr("到:"), quietBox));
    quietEnd_ = new QTimeEdit(quietBox);
    quietEnd_->setDisplayFormat(QStringLiteral("HH:mm"));
    quietEnd_->setObjectName(QStringLiteral("alarmSoundQuietEnd"));
    quietRow->addWidget(quietEnd_);
    quietRow->addStretch();
    quietLay->addLayout(quietRow);

    quietLay->addWidget(makeHint(
        tr("静默时段内【不响,但报警照常显示、照常记录】—— "
           "关掉的只是声音,不是报警本身。跨零点(如 22:00 到 07:00)可以直接填。"),
        quietBox));
    root->addWidget(quietBox);

    // ---- 试听按钮已挪进"各等级"组(见上面)----

    root->addStretch(1);   // 三组内容顶到上面,按钮沉到底 —— 对话框的常规视觉重心

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { saveToPlayer(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &AlarmSoundDialog::reject);
    // ★ 中文界面里 QDialogButtonBox 没有 translator 时就显示原文 OK / Cancel,
    //   夹在一堆中文按钮里很扎眼。这里显式改掉。
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    root->addWidget(buttons);

    // ★ 总开关关掉时,下面三组整体置灰。
    //   【这不是装饰,是原来缺的行为】总开关关着,可下面的复选框
    //   照样能勾能改 —— 用户会以为"设了",而这些设置一条都不会生效。
    //   置灰是唯一能让"当前不生效"这件事在界面上看得见的办法。
    //   （和 F2-c「静音不能等于隐藏」是同一条原则:不生效的东西要看得见。）
    connect(masterChk_, &QCheckBox::toggled, this, &AlarmSoundDialog::updateEnabledState);
    // "要不要重复提醒"关掉时,间隔选哪个数都没意义 —— 跟着一起灰掉。
    // 少了这条,用户会为了一个根本不生效的数字去纠结该填几秒。
    connect(repeatChk_, &QCheckBox::toggled, this, &AlarmSoundDialog::updateEnabledState);
    connect(quietChk_,  &QCheckBox::toggled, this, &AlarmSoundDialog::updateEnabledState);

    loadFromPlayer();
    updateEnabledState();
}

// 总开关 → 下面三组的可用状态。
//
// ★ 为什么拆成独立函数而不是写在 lambda 里:
//   exec() 每次打开都要重读一遍设置,那时 masterChk_ 的值可能被程序改了
//   (loadFromPlayer 里的 setChecked),而信号只在【用户】点的时候才发 ——
//   所以打开对话框的第一帧,下面三组的可用状态会是上一次残留的。
//   构造函数末尾显式调一次,不能指望信号。
void AlarmSoundDialog::updateEnabledState() {
    const bool on = masterChk_ && masterChk_->isChecked();
    // ★ 逐个设,而不是靠一个包裹它们的父 QWidget。
    //   试听按钮【不受总开关影响】—— 它试的是"声音本身",
    //   总开关关着的时候用户恰恰最需要试听来确认
    //   "这台机器到底能不能出声",这时候灰掉它最不合理。
    for (int i = 0; i < 4; ++i)
        if (levelChk_[i]) levelChk_[i]->setEnabled(on);
    if (repeatChk_)  repeatChk_->setEnabled(on);
    if (repeatSpin_) repeatSpin_->setEnabled(on && repeatChk_ && repeatChk_->isChecked());
    if (quietChk_)   quietChk_->setEnabled(on);
    const bool quietOn = on && quietChk_ && quietChk_->isChecked();
    if (quietStart_) quietStart_->setEnabled(quietOn);
    if (quietEnd_)   quietEnd_->setEnabled(quietOn);
}

// ---------------------------------------------------------------------------
// exec
// ---------------------------------------------------------------------------

// ★ 为什么声音设置也要 EditProject
//   声音开着的时候,一条阈值还没配到的报警也能把人吵醒;
//   而谁能开、谁能关,是工程行为的控制权,和报警配置同一条权限。
//   Operator 能看报警列表,但不该有能力让整条产线"半夜突然开始叫"。
int AlarmSoundDialog::exec() {
    if (!qwhmi::auth::checkPermission(qwhmi::auth::CurrentUser::instance().role(),
                                      qwhmi::auth::Permission::EditProject)) {
        QMessageBox::warning(this, tr("权限不足"), tr("只有工程师/管理员才能修改报警声音设置"));
        return QDialog::Rejected;
    }
    // 每次打开都重读 —— 和 AlarmConfigDialog 一样,免得显示的是上次的旧值。
    loadFromPlayer();
    return QDialog::exec();
}

// ---------------------------------------------------------------------------
// 读 / 写
// ---------------------------------------------------------------------------

void AlarmSoundDialog::loadFromPlayer() {
    const AlarmSoundPlayer::Settings s = player_->settings();
    masterChk_->setChecked(s.masterEnabled);
    for (int i = 0; i < 4; ++i)
        levelChk_[i]->setChecked(s.levelEnabled[static_cast<size_t>(i)]);
    repeatChk_->setChecked(s.repeatUntilAck);
    // ★ 毫秒 → 秒。向上取整而不是四舍五入:
    //   这是个"至少隔多久"的设置,存 500ms 显示成 0 秒会让用户以为填不了,
    //   而显示成 1 秒再存回去变成 1000ms —— 宁可多响一点,不可少响。
    repeatSpin_->setValue(qMax(1, (s.repeatIntervalMs + 999) / 1000));
    quietChk_->setChecked(s.quietEnabled);
    quietStart_->setTime(QTime(s.quietStartMin / 60, s.quietStartMin % 60));
    quietEnd_->setTime(QTime(s.quietEndMin / 60, s.quietEndMin % 60));
}

void AlarmSoundDialog::saveToPlayer() {
    AlarmSoundPlayer::Settings s = player_->settings();
    s.masterEnabled = masterChk_->isChecked();
    for (int i = 0; i < 4; ++i)
        s.levelEnabled[static_cast<size_t>(i)] = levelChk_[i]->isChecked();
    s.repeatUntilAck = repeatChk_->isChecked();
    s.repeatIntervalMs = repeatSpin_->value() * 1000;
    s.quietEnabled = quietChk_->isChecked();
    s.quietStartMin = quietStart_->time().hour() * 60 + quietStart_->time().minute();
    s.quietEndMin   = quietEnd_->time().hour() * 60 + quietEnd_->time().minute();
    player_->setSettings(s);
}

// ---------------------------------------------------------------------------
// 测试出口
// ---------------------------------------------------------------------------

bool AlarmSoundDialog::masterEnabled() const { return masterChk_ && masterChk_->isChecked(); }

bool AlarmSoundDialog::levelEnabled(int index) const {
    if (index < 0 || index > 3 || !levelChk_[index]) return false;
    return levelChk_[index]->isChecked();
}

bool AlarmSoundDialog::repeatEnabled() const { return repeatChk_ && repeatChk_->isChecked(); }
// ★ 对外仍然报【毫秒】。测试和外部调用方的语义没变,
//   只有界面上那个 spin 是秒 —— 换算只发生在对话框内部。
int  AlarmSoundDialog::repeatIntervalMs() const { return repeatSpin_ ? repeatSpin_->value() * 1000 : 0; }
bool AlarmSoundDialog::quietEnabled() const { return quietChk_ && quietChk_->isChecked(); }

QString AlarmSoundDialog::quietStartText() const {
    return quietStart_ ? quietStart_->time().toString(QStringLiteral("HH:mm")) : QString();
}
QString AlarmSoundDialog::quietEndText() const {
    return quietEnd_ ? quietEnd_->time().toString(QStringLiteral("HH:mm")) : QString();
}

AlarmLevel AlarmSoundDialog::highestEnabledLevel() const {
    // 从重到轻找第一个开着的 —— 试听时优先让用户听到"最难受"的那档,
    // 因为那才是真正需要判断"够不够大"的。
    static const AlarmLevel kByIndex[4] = { AlarmLevel::Info, AlarmLevel::Warning,
                                             AlarmLevel::Critical, AlarmLevel::Emergency };
    for (int i = 3; i >= 0; --i)
        if (levelChk_[i] && levelChk_[i]->isChecked()) return kByIndex[i];
    return AlarmLevel::Warning;
}

} // namespace qwhmi::alarm