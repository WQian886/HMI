// ============================================================================
// 阶段 F · F4-a · 报警配置对话框(只读)
//
// 文件职责:把 AlarmManager 里所有 checker 的配置【读出来给人看】。
//
// ★ 为什么第一版是只读的(2026-10-08)
//   F4 剩下的活分两步,这一步只做第一步:
//     F4-a(本文件):把配置读出来 + 可编辑地维护一份草稿 + 导出 JSON
//     F4-b:        把草稿应用回运行中的 AlarmManager
//   拆开的原因是 F4-b 有个必须先看见界面才能拍板的设计问题 ——
//   "报警正在跑的时候改阈值,怎么生效?"(重建 checker / 加 setConfig / 只准停机改)
//   三个方案对 UI 的要求不一样。界面没做出来就拍板,拍的是想象。
//
//   所以这一步【刻意不改运行中的任何状态】:
//     - 不调 addChecker / removeChecker
//     - 不碰 ThresholdAlarmChecker::config_
//     - 不动 shelved_
//   好处是这一整步可以脱离 PLC 单独验收 —— 而这个项目反复吃亏的
//   恰恰是"测试绿了 ≠ 功能能用"(见 CHANGELOG 里 4 次同类教训)。
//
// ★ 为什么这个对话框必须存在(而不是"继续手改 JSON")
//   F2 滞回、F2-c 屏蔽都已经落地了,但【没有任何入口能配它们】:
//     hysteresis 只能手改 JSON 里的 "hysteresis" 键(JsonSerializer.cpp:55)
//     shelve     只能手改 JSON 里的 alarms[].shelved
//   报警配置是一线工程师每天要动的东西,让它只存在于 JSON 里
//   等于这个功能对真实用户不存在。
//
// ★ 权限
//   要 EditProject(Permission.h:35)。这条权限枚举定义了但从没人调用过 ——
//   本文件是它的第一个使用者。理由:改报警配置 = 改工程行为 = 工程编辑。
//
// 关联:../../AlarmManager.h, ../../ThresholdAlarmChecker.h,
//      ../auth/Permission.h, ../auth/CurrentUser.h
// ============================================================================

#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTextEdit;

class AlarmManager;
class VariableManager;

namespace qwhmi::alarm {

// ★ 一条报警配置的草稿。
//   【为什么不用 CheckerConfig 直接当表格数据源】
//   CheckerConfig 是"检测逻辑"的参数(level/direction/threshold/...),
//   不含 name 和 variable —— 那两个在 ThresholdAlarmChecker 上。
//   表格要显示的是"用户认知中的一条报警",得把三处凑齐,所以有这个中间结构。
//
//   【为什么 shelved 放这里而不是单独一个屏蔽管理界面】
//   屏蔽是【某条报警的属性】,不是另一类对象。工程上查看报警配置时
//   必然想知道"这条现在是不是被屏蔽了"。拆成两个界面 = 用户要来回对照。
struct AlarmConfigRow {
    QString name;          // 报警名(AlarmManager 里 checkers_ 的 key)
    QString variableName;  // 关联变量名
    QString level      = QStringLiteral("Warning");
    QString direction  = QStringLiteral("Above");
    double  threshold  = 0.0;
    int     minDurationMs = 500;
    double  hysteresis = 0.0;
    bool    shelved    = false;   // ★ F2-c:当前是否被屏蔽

    // 【非阈值报警】WriteFailChecker 这类不在表里,也【不该】出现在表里。
    //   它没有 variable、没有 threshold —— 硬塞进来会让用户以为能编辑它。
    //   对话框底部会明说"N 条非阈值报警未显示",不给静默截断。
    bool    editable   = true;
};

class AlarmConfigDialog : public QDialog {
    Q_OBJECT
public:
    // mgr / varMgr 都不接管生命周期(由主窗口持有,活得比对话框长)
    AlarmConfigDialog(AlarmManager* mgr,
                      VariableManager* varMgr,
                      QWidget* parent = nullptr);
    ~AlarmConfigDialog() override;

    // 要求:CurrentUser 有 EditProject 权限(否则 exec() 拒绝)
    int exec() override;

    // ===== ★ F4-a:给测试/宿主看的只读访问器 =====
    //   【为什么暴露】这个对话框的价值全部体现在"用户能看到什么、改了什么"。
    //   反射 private 成员去断言,测的就成了实现细节,改个成员名就红,
    //   却证明不了界面真的对 —— F3 在 AlarmListControl 上已经踩过这条(见其 .h:47)。
    int     rowCount() const;
    QString nameAt(int row) const;
    QString variableAt(int row) const;
    QString levelAt(int row) const;
    QString directionAt(int row) const;
    double  thresholdAt(int row) const;
    int     minDurationMsAt(int row) const;
    double  hysteresisAt(int row) const;
    bool    shelvedAt(int row) const;
    bool    editableAt(int row) const;

    // ★ F4-a 的产出物:当前草稿导成的 JSON。
    //   【为什么导出而不是直接写文件】
    //   F4-b 要"应用草稿",F4-c 要"存进工程文件",这两个都还没做。
    //   导出成 JSON 文本 = 草稿有一个稳定、可测、可粘进工程文件的形态,
    //   而且它复用的是 JsonSerializer —— 和 saveToFile 走的是同一套序列化,
    //   所以能顺手验证"编辑器里配的,存进工程文件会不会丢字段"。
    QString exportDraftToJson() const;

    // ★ F4-c(2026-10-08):把草稿写进已有工程文件的 alarms 段。
    //
    //   【为什么导出 JSON 不够】exportDraftToJson 只把 JSON 放进一个文本框 +
    //   剪贴板。用户得自己找工程文件、找到 alarms 段、粘贴 —— 而这一步
    //   几乎没人会做。F4-a 的"导出"实际等于"没有落地能力"。
    //   这个方法是【草稿唯一的落地出口】。
    //
    //   【边界:不碰运行中的报警】写完文件后,运行中的 AlarmManager 一概不变。
    //   要让新配置生效仍需重载工程 —— 这是 F4-b 的事(运行中改阈值怎么生效,
    //   是必须先看见界面才能拍板的设计问题)。
    //
    //   path 必须指向【已存在的合法工程文件】。失败原因写进 errorOut。
    bool saveDraftToProjectFile(const QString& path, QString* errorOut);

    // ★ F4-c:存盘前的校验,把 Project::validateAlarmsArray 查不到的那一条补上 ——
    //   报警引用的变量必须真实存在。理由见 Project.cpp:211:加载时引用不存在的
    //   variable 会让【整个工程文件打不开】,而不只是这一条报警有问题。
    //   【为什么要单独一个函数】onSave 里要把"变量不存在"和"名字重复/为空"
    //   两种原因分开报给用户 —— 前者要他去修变量,后者要他改名字。
    bool validateDraft(QString* errorOut) const;

private slots:
    void onAdd();
    void onRemove();
    void onApplyFieldEdits();   // 表单改动 → 回写草稿行
    void onExport();            // 导出草稿 JSON(显示 + 复制到剪贴板)
    void onSaveToProject();     // ★ F4-c:草稿 → 工程文件 alarms[]

private:
    void setupTable();
    void setupForm();
    void setupButtons();
    void reloadFromManager();
    void fillFormFromRow(int row);
    void writeFormToRow(int row);
    void setFormEnabled(bool on);
    void updateInfo();
    // ★ 新增报警时取一个未被占用的占位名("新报警" / "新报警 2" / …)。
    //   名字是 checkers_ 的 key,重名会导致加载工程文件时静默覆盖。
    QString nextFreeDraftName() const;

    AlarmManager*   alarmMgr_ = nullptr;
    VariableManager* varMgr_   = nullptr;

    QList<AlarmConfigRow> rows_;   // 草稿(★ 本对话框唯一的数据源)

    // 程序性改表格时置 true,让 itemChanged 处理器知道自己不是"用户改的"。
    // 【为什么需要它】fillFormFromRow 只 blockSignals 了【表单】控件,
    //   但 reloadFromManager() 里的 setItem 同样会发 itemChanged。
    //   不挡的话,填表会触发回写,而那时 rows_ 可能还没填完 —— 静默错行。
    bool suppressItemChanged_ = false;

    QTableWidget*  table_       = nullptr;
    QLabel*        infoLabel_   = nullptr;
    QLabel*        hiddenLabel_ = nullptr;  // ★ "N 条非阈值报警未显示" —— 不静默截断

    // ★ 表单。注意 hysteresis 的量纲提示:它和 threshold 同单位,
    //   但语义不同(threshold 是"越界点",hysteresis 是"死区宽度")。
    // ★ 工程里是否存在变量 —— 静态事实,只在 setupForm 算一次。
    //   【为什么必须单独存】setFormEnabled 里曾写成
    //   setEnabled(on && varCombo_->isEnabled()),那是自引用门闩:
    //   组合框一旦被禁就永远回不来(构造函数末尾会 setFormEnabled(false))。
    //   结果"新建报警后关联变量点不动"。把"有没有变量"(事实)和
    //   "现在能不能操作"(状态)分开,这个 bug 就不可能再发生。
    bool            hasVariables_   = false;

    QComboBox*      varCombo_       = nullptr;
    QComboBox*      levelCombo_     = nullptr;
    QComboBox*      dirCombo_       = nullptr;
    QDoubleSpinBox* thresholdSpin_  = nullptr;
    QSpinBox*       minDurSpin_     = nullptr;
    QDoubleSpinBox* hysteresisSpin_ = nullptr;

    QPushButton*    addBtn_     = nullptr;
    QPushButton*    removeBtn_  = nullptr;
    QPushButton*    exportBtn_  = nullptr;
    // ★ F4-c:真正落地的按钮。exportBtn_ 只是"看一眼 + 复制到剪贴板",
    //   几乎等于没有落地能力;这个才是把草稿写进工程文件的。
    QPushButton*    saveBtn_    = nullptr;
    QPushButton*    closeBtn_   = nullptr;
    QTextEdit*      jsonView_   = nullptr;
};

} // namespace qwhmi::alarm
