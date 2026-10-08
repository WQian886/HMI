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
const char* kLevelNames[4] = {
    QT_TRANSLATE_NOOP("AlarmSoundDialog", "信息(Info)—— 例:泵启动、模式切换"),
    QT_TRANSLATE_NOOP("AlarmSoundDialog", "警告(Warning)—— 例:接近上限"),
    QT_TRANSLATE_NOOP("AlarmSoundDialog", "严重(Critical)—— 例:超限,设备受影响"),
    QT_TRANSLATE_NOOP("AlarmSoundDialog", "危急(Emergency)—— 例:停机、危险")
};

QString levelName(int i) {
    return QCoreApplication::translate("AlarmSoundDialog", kLevelNames[i]);
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

    masterChk_ = new QCheckBox(tr("启用报警声音(总开关)"), this);
    masterChk_->setObjectName(QStringLiteral("alarmSoundMasterChk"));
    root->addWidget(masterChk_);

    // ---- 每档等级 ----
    auto* lvlBox = new QGroupBox(tr("各等级是否发声"), this);
    auto* lvlLay = new QVBoxLayout(lvlBox);
    for (int i = 0; i < 4; ++i) {
        levelChk_[i] = new QCheckBox(levelName(i), lvlBox);
        levelChk_[i]->setObjectName(QStringLiteral("alarmSoundLevelChk%1").arg(i));
        lvlLay->addWidget(levelChk_[i]);
    }
    lvlLay->addWidget(new QLabel(
        tr("说明:信息级默认不响。现场最吵的往往是信息级报警,全都响的话\n"
           "操作员很快就会麻木,而麻木等于真报警也听不见。"),
        lvlBox));
    root->addWidget(lvlBox);

    // ---- 重复提醒 ----
    auto* repBox = new QGroupBox(tr("未确认时重复提醒"), this);
    auto* repLay = new QVBoxLayout(repBox);
    repeatChk_ = new QCheckBox(tr("报警未确认前每隔一段时间再提醒一次"), repBox);
    repeatChk_->setObjectName(QStringLiteral("alarmSoundRepeatChk"));
    repLay->addWidget(repeatChk_);

    auto* repRow = new QHBoxLayout();
    repRow->addWidget(new QLabel(tr("提醒间隔(毫秒):"), repBox));
    repeatSpin_ = new QSpinBox(repBox);
    repeatSpin_->setRange(500, 600000);
    repeatSpin_->setSingleStep(500);
    repeatSpin_->setSuffix(tr(" ms"));
    repeatSpin_->setObjectName(QStringLiteral("alarmSoundRepeatSpin"));
    repRow->addWidget(repeatSpin_);
    repRow->addStretch();
    repLay->addLayout(repRow);
    repLay->addWidget(new QLabel(
        tr("说明:一次提醒只响一声,并按当前最高等级选声音 ——\n"
           "同时挂五条未确认报警时响五下,只会变成一片噪音。"), repBox));
    root->addWidget(repBox);

    // ---- 静默时段 ----
    auto* quietBox = new QGroupBox(tr("静默时段(免打扰)"), this);
    auto* quietLay = new QVBoxLayout(quietBox);
    quietChk_ = new QCheckBox(tr("在指定时段内不响"), quietBox);
    quietChk_->setObjectName(QStringLiteral("alarmSoundQuietChk"));
    quietLay->addWidget(quietChk_);

    auto* quietRow = new QHBoxLayout();
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

    quietLay->addWidget(new QLabel(
        tr("说明:静默时段内【不响,但报警照常显示、照常记录】——\n"
           "关掉的只是声音,不是报警本身。跨零点(如 22:00 到 07:00)可以直接填。"), quietBox));
    root->addWidget(quietBox);

    // ---- 试听:不接就是"配了不知道对不对"----
    //   这一条按钮把 F5 最尴尬的地方补上了:系统音好不好听、够不够大,
    //   光看界面是判断不了的。
    auto* previewBtn = new QPushButton(tr("试听当前设置"), this);
    previewBtn->setObjectName(QStringLiteral("alarmSoundPreviewBtn"));
    connect(previewBtn, &QPushButton::clicked, this, [this]() {
        // 试听【不受静默时段和已确认影响】—— 它试的是"声音本身",
        // 不是"此刻这条报警该不该响"。混在一起会导致用户在静默时段里
        // 点试听什么也听不到,以为坏了。
        if (player_) player_->previewBeep(highestEnabledLevel());
    });
    root->addWidget(previewBtn);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { saveToPlayer(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &AlarmSoundDialog::reject);
    root->addWidget(buttons);

    loadFromPlayer();
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
    repeatSpin_->setValue(s.repeatIntervalMs);
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
    s.repeatIntervalMs = repeatSpin_->value();
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
int  AlarmSoundDialog::repeatIntervalMs() const { return repeatSpin_ ? repeatSpin_->value() : 0; }
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