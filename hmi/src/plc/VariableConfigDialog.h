// ============================================================================
// ★ 2026-10-19:变量配置对话框
//
// 文件职责:把 VariableManager 里的变量【读出来给人看、给人改、给人删】。
//
// ★ 为什么这个对话框必须存在(而不是"继续手改 JSON")
//   多 PLC 2b 把 N 台 PLC 的配置存读做完了,但变量指向哪台 PLC 这件事
//   【至今没有任何入口】。plcId 只能手改 JSON 或者导 CSV。
//   这不是"还没画界面",是 2b 的多 PLC 架构只完成了一半:
//   配了 3 台 PLC,却没有任何正常途径把变量分配给它们。
//   TagImporter 那个"警告但不拒绝"就是为这个妥协的(变量 plcId 没有界面,
//   拒绝会堵死"先导 CSV 再配 PLC"这条正常顺序)。
//   这半截状态比没有多 PLC 更危险:配了 PLC,变量却只能手改 JSON 指过去。
//
// ★★ 三个被拍板的决定,以及它们各自的理由
//
//   ① 【被引用的变量:名字锁死,改名按钮变成"删了重建"】
//      名字是 VariableManager::vars_ 这个 QHash 的【键】,而 Variable
//      没有 setName。改名字在今天只有一个实现:删掉旧的、新建一个同名改过的。
//      而 Variable 绑在画面控件上(ControlBase 存的是 QPointer),
//      删掉变量的瞬间,那个控件的指针会【静默变空、定格在最后一个值、不报错】。
//      所以对无引用的变量,改名 = 删了重建,界面必须把这句话说出来,
//      让人知道自己正在做一件有后果的事。
//
//   ② 【被引用的变量:直接拒删,并逐条列出引用者】
//      这不是洁癖,是实测出来的:删掉被引用的变量,存出来的工程【整个打不开】
//      (见 VariableRefScan.h 顶部的两条独立链路)。
//      而"拒删"必须带【名字】:只说"这个变量正在被使用"没有用,
//      用户不知道该先去哪儿处理。
//
//   ③ ★★【诚实的边界:画面控件那层,本对话框查不到】
//      ControlBase 存 QPointer<Variable> 但【不监听 variableReplaced】,
//      而编辑器窗口/运行画布在 hmi.cpp:1105/1128 是【函数内 static】,
//      本对话框从 hmi 构造时枚举不到它们。
//      如果不把这件事说出来,"有引用就拒删"就是个【兑现不了的承诺】:
//      用户删干净了,画面上那个显示温度的表还在,已经定格在上一次读到的值,
//      而界面上什么异常都没有。
//      所以 deleteRowFromDraft 的返回值里带着 blindSpots,
//      调用方【必须】把它显示给用户。宁可承认看不见,也不能让人以为看全了。
//
// ★ 为什么"删除"这么克制,而"新增/修改"这么大方
//   新增一个变量:最坏情况是多一个没人用的变量,删掉就完事。
//   删除一个变量:可能毁掉一个正在运行的工程文件,而且【不可逆】——
//   工程文件被写坏之后,上一份能打开的副本在哪?用户不一定有。
//   两者不对称,所以策略不对称。
//
// ★ removeRowFromDraft 刻意【不含】任何 QMessageBox
//   理由与 PlcConfigDialog::removeRowFromDraft 完全相同:
//   AlarmConfigDialog 曾因为把 QMessageBox 写进数据层方法,
//   在 offscreen 测试下永久阻塞(卡满 300s 超时),
//   于是"删除"这条路一条测试都写不了 —— 而删除恰恰是最需要测试的那条。
//
// ★ 权限
//   EditProject —— 与 AlarmConfigDialog / PlcConfigDialog 同一条。
//   改变量就是改工程行为,而且删变量会写坏工程文件,
//   权限要求只会比报警配置更高,不会更低。
//
// ★ 不做"保存到工程文件"按钮 —— 这是刻意的,不是漏了
//   事实:Project.cpp:670 直接序列化【运行中】的 varMgr_。
//   所以这个对话框改完 varMgr_,用户点主界面的"保存工程"自然就存进去了。
//   单独做一个"保存变量"按钮等于凭空造出第二个真相来源:
//   用户在这里存了,主界面的变量状态却没变,两边对不上。
//   没有真相来源要协调,也就没有"两边不一致"这回事。
//
// 关联:../../VariableRefScan.h(删除前查引用),
//      ../../VariableManager.h(数据源), plc/PlcConfigDialog.*(形状模板),
//      ../auth/Permission.h, plc/PlcConnectionManager.h(plcId 下拉候选)
// ============================================================================

#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

// ★ 必须完整包含,不能只前向声明。
//   VariableDraft 里有两个成员用的是 Variable 的【嵌套枚举】
//   (Variable::DataType / Variable::Access),嵌套枚举要外层类完整定义。
#include "../../Variable.h"

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

class AlarmManager;
class ActionManager;
class PlcConnectionManager;
class VariableManager;

namespace qwhmi::plc {

// ★ 一行变量的草稿。
//
//   【为什么要草稿,而不是在表格上直接改 Variable】
//   Variable 没有 setName / setAddress / setDataType / setAccess ——
//   这四个只能"删了重建"。而 plcId / pollIntervalMs / deadband / alwaysPoll
//   是有 setter 的。两者混在一张表里,如果一边改一边落,
//   "改地址"这一格就会在用户还没点应用时就把旧变量删掉重建了。
//   草稿式的意义就在这里:【所有改动在点「应用」之前都不碰 VariableManager】。
//   对运行中的上位机来说,这不是 UI 洁癖 —— 立即生效的话,
//   用户在对话框里把地址填到一半,轮询已经开始读那个地址了。
struct VariableDraft {
    QString name;        // ★ 只读,改 = 删了重建(见头注释决定 ①)
    QString plcId;      // 可改 —— 这是本对话框存在的【主目的】
    int     address   = 0;   // ★ 只读(没有 setter)
    Variable::DataType dataType = Variable::DataType::Int16;  // ★ 只读
    Variable::Access   access   = Variable::Access::ReadOnly; // ★ 只读
    int     pollMs    = 1000;
    double  deadband  = 0.0;
    bool    alwaysPoll = false;
    bool    derived    = false;   // 派生量:地址恒为 0,不由本对话框改
    QString expression;           // 派生量的表达式(只显示,不在这里改)

    // ★ 被谁引用。0 = 没人引用。
    int     refCount = 0;
    // 人类可读的一行行引用说明,例如 "报警「温度高」(监控)"。
    QStringList refDescriptions;
    // ★ 本次扫描【覆盖不到】的类别。永远非空(至少含画面绑定那一条)——
    //   见头注释决定 ③。显示它不是可选项。
    QStringList blindSpots;
};

class VariableConfigDialog : public QDialog {
    Q_OBJECT
public:
    // varMgr 是唯一的数据源,必传(传 nullptr 就没什么可配的了)。
    // alarmMgr / actionMgr / plcMgr 可以为 nullptr ——
    //   那样的话对应的引用【查不到】,并记进 blindSpots,
    //   而不是假装"没有引用"(与 VariableRefScan 同一条取舍)。
    explicit VariableConfigDialog(VariableManager* varMgr,
                                  AlarmManager* alarmMgr = nullptr,
                                  ActionManager* actionMgr = nullptr,
                                  PlcConnectionManager* plcMgr = nullptr,
                                  QWidget* parent = nullptr);
    ~VariableConfigDialog() override;

    // 要求:CurrentUser 有 EditProject 权限(否则 exec() 拒绝)
    int exec() override;

    // ===== 给测试/宿主看的只读访问器 =====
    //   【为什么暴露】这个对话框的价值全部体现在"用户能看到什么、改了什么"。
    //   反射 private 成员去断言,测的就成了实现细节(理由见 PlcConfigDialog.h)。
    int     rowCount() const;
    QString nameAt(int row) const;
    QString plcIdAt(int row) const;
    int     addressAt(int row) const;
    int     refCountAt(int row) const;
    QStringList refDescriptionsAt(int row) const;
    // ★ 第 col 列在 row 行是不是只读。用来钉"名称/地址/类型/读写锁死"这个决定。
    bool    isCellEditable(int row, int col) const;
    // ★ plcId 下拉在某一行列出的候选(按显示顺序)。
    QStringList plcIdChoicesAt(int row) const;
    QString plcIdComboCurrentAt(int row) const;

    // ★ 校验草稿。返回 false 时 errorOut 说明【哪一行、为什么】。
    bool validateDraft(QString* errorOut) const;

    // ★ 从草稿里删掉一行【不做任何 UI 询问,也不查引用】。
    //   onRemove = 弹框说明后果 + 查引用 + 调它。
    //   ★★ 查引用刻意【不】放在这里:那会让"能删/不能删"变成数据层的决定,
    //     而同一个查询将来还要服务于"改名""导出前检查"。
    //     这里只负责"纯删除",能不能删由 removeRowChecked 判。
    bool removeRowFromDraft(int row);

    // ★★★ 删一行的【完整判定】—— 有引用就返回 false ★★★
    //   这是"用户决定"的实现层:它查引用、给出理由、不弹框。
    //   返回 true = 可以删(真的从草稿里删掉了)。
    //   返回 false = 【拒绝】,reasonOut 里逐条列出引用者,
    //   blockedByBlindSpot 说明"还有查不到的那一层"(通常恒为 true)。
    //
    //   【为什么把"查引用"和"删"合成一个方法,而不是让调用方先查后删】
    //   分成两步的话,调用方(UI)必须记得先查再删;
    //   忘了就是"删了被引用的变量",也就是我们要防的那件事本身。
    //   合在一起,漏不掉 —— 代价是想单独查引用时得再开一个入口,
    //   那个入口是只读的,不会引入新的删除路径。
    bool removeRowChecked(int row, QString* reasonOut,
                          QStringList* blindSpotsOut = nullptr);

    // ★ 新增一行(名字/地址待填),返回新行号。
    //   name 非空则用它,空则自动生成一个不冲突的名字(新N)。
    int  addRowToDraft(const QString& name = QString());

    // ★ 草稿 → VariableManager(点「应用」时走这条路)。
    //   先全查再全改:改到第 5 行才发现第 3 行有问题,前 4 行已经动了,
    //   而用户看到的是"应用失败",他不知道到底哪些生效了。
    //   返回 false 时 errorOut 说明原因,且【VariableManager 一个字节都没动】。
    bool applyDraftToManager(QString* errorOut);

    // ★ 应用成功后从 VariableManager 重新读一遍(与 AlarmConfigDialog 同做法):
    //   草稿里有 Variable 表达不了的东西(派生表达式、值、质量),
    //   不重读的话表格会显示出跟实际运行中不一致的样子。
    void reloadFromManager();

private slots:
    void onAdd();
    void onRemove();
    void onApply();
    void onApplyFieldEdits(QTableWidgetItem* item);
    void onPlcIdComboChanged(int index);

private:
    void setupTable();
    void setupButtons();
    void fillTable();
    void updateInfo();
    // 重扫一行的引用信息(草稿行的 name 定下来之后 / 每次重载时)。
    void rescanRefs(VariableDraft& d) const;
    // plcId 下拉的候选:managedIds + 【工程里正在用但没在管的 plcId】。
    QStringList plcIdChoices() const;
    static QString nextFreeName(const QList<VariableDraft>& rows, const QString& prefix);

    VariableManager*      varMgr_   = nullptr;
    AlarmManager*         alarmMgr_ = nullptr;
    ActionManager*        actionMgr_= nullptr;
    PlcConnectionManager* plcMgr_   = nullptr;

    QList<VariableDraft> rows_;   // 草稿(★ 本对话框唯一的数据源)

    // 程序性改表格时置 true,让 itemChanged 知道自己不是"用户改的"。
    //   【为什么需要】fillTable 里的 setItem 同样会发 itemChanged,
    //   不挡的话填表会触发回写,而那时 rows_ 可能还没填完 —— 静默错行。
    bool suppressItemChanged_ = false;

    QTableWidget* table_     = nullptr;
    QLabel*       infoLabel_ = nullptr;
    // ★ "扫描查不到什么"。这一条必须在界面上,理由见头注释决定 ③。
    QLabel*       blindHint_ = nullptr;
    // ★ "名称/地址/类型/读写为什么锁死"。不解释的话,用户看到灰格
    //   只会以为程序有 bug,或者以为功能没做完。
    QLabel*       lockHint_  = nullptr;

    QPushButton* addBtn_    = nullptr;
    QPushButton* removeBtn_ = nullptr;
    QPushButton* applyBtn_  = nullptr;
    QPushButton* closeBtn_  = nullptr;
};

} // namespace qwhmi::plc
