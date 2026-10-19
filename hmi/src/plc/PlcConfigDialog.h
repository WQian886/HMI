// ============================================================================
// ★ 多 PLC 2b · 阶段 6(2026-10-19):PLC 配置对话框
//
// 文件职责:把工程里的 plcs[] 【读出来给人看、给人改】,并落到该落的地方。
//
// ★ 为什么这个对话框必须存在(而不是"继续手改 JSON")
//   2a/2b 把 N 台 PLC 的配置存读和运行期连接都做完了,但【没有任何入口能配它们】。
//   plcs[] 只能手改 JSON:host 写错了要停机改文件,slave_id 填成 0
//   (Modbus 广播地址)没有任何拦截,填错协议的 PLC 会静默 rejected 然后
//   从界面上消失 —— 用户配了 3 台,界面上只连上 2 台,且看不出少了哪台。
//   PLC 连接是一线工程师每天要动的东西,让它只存在于 JSON 里
//   等于这个功能对真实用户不存在。
//
// ★ 三个动作各管什么(照 F4-a/b/c 的分法,别合并)
//   保存到工程文件:草稿 → plcs[]。   【不碰运行期】
//   应用到运行中:  草稿 → PlcConnectionManager::applyConfig。【立即重连】
//   关闭:          丢草稿。
//   合并前两个是本项目最容易犯的错:对话框的表看着是"工程的配置",
//   用户合理地以为点一下保存就生效了 —— 而它只写进了文件。
//   AlarmConfigDialog 靠 F4-b/F4-c 两个分开的按钮 + 各自的提示文案解决,
//   这里照抄,包括那个"运行中的连接不受影响,要在主界面重载工程"的说明。
//
// ★ 【删除一台仍被变量引用的 PLC:不阻止,也不自动改变量的 plcId】
//   这条踩过坑,值得写死:
//   最自然的实现是"删 plc2 就把它那些变量改成 plc1" ——
//   那样原来写向 plc2 的压力值会【突然开始写进一号机】,
//   而界面上没有任何提示。这比"变量变成无连接状态"危险得多:
//   后者看得见(值停在 Bad、变量列表里那个 plcId 不存在任何连接),
//   前者是静默地往错误的机器下发数据。
//   正确做法:删之前 QMessageBox 明说"plc2 正被 3 个变量引用,
//   删除后它们会变成无连接状态",让用户自己决定要不要去改那些变量。
//
// ★ removeRowFromDraft 刻意【不含】任何 QMessageBox
//   onRemove = 弹确认框 + 调它。理由见 AlarmConfigDialog.h 同名方法的说明:
//   AlarmConfigDialog 曾因为把 QMessageBox 写进数据层方法,
//   在 offscreen 测试下永久阻塞(卡满 300s 超时),
//   于是"删除"这条路一条测试都写不了。
//   而删除在这里恰恰是最危险的路径 —— 最需要测试的恰恰是最测不了的,这个坑不能再踩。
//
// ★ 权限
//   要 EditProject —— 与 AlarmConfigDialog 同一条。
//   改 PLC 配置就是改工程行为,而且改完会【立刻重连现场设备】,
//   权限要求只会比报警配置更高,不会更低。
//
// 关联:../plc/PlcConnectionManager.h(应用到运行中),
//      ../../Project.h(草稿落盘), ../auth/Permission.h,
//      ../../../drivers/DriverFactory.h(协议下拉 + 置灰), ../alarm/AlarmConfigDialog.*(形状模板)
//
// ★★ 与计划的两处偏离(2026-10-19,都已落地且有测试钉住,记在这里备查)
//   计划写的是"slaveId SpinBox 范围 1..247,不给 0"—— ✅ 做了,
//   但它是【表格单元格里嵌的 SpinBox】(setCellWidget),不是另起一行的一个 SpinBox。
//   为什么:一行一台 PLC,从站号是每行各一个,放行外就得做"选中哪台"的联动,
//   多一个状态还多一处能不同步。
//
//   计划写的是"protocol 下拉复用 ProtocolAvailabilityUi 的置灰"—— ✅ 做了,
//   同样是单元格里的 QComboBox,并且额外加了一条:
//   工程文件里写了个本程序【没有】的协议(比如别人用将来版本存的 "profinet")时,
//   照原样加进下拉框并选中它,而不是悄悄换成 modbus。
//   用户打开对话框是为了看配置,看见的必须是文件里真正写着的那个协议名 ——
//   对话框擅自"修正"配置,用户就再也发现不了自己文件里写错的那一处了。
// ============================================================================

#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;   // onApplyFieldEdits 的参数类型,前向声明即可

class VariableManager;

class PlcConnectionManager;

namespace qwhmi::plc {

// ★ 一台 PLC 的草稿行。
//
//   【为什么单独有这个结构,不用裸 QJsonObject】
//   与 PlcConnectionManager::Entry 同理:表格要显示的是"用户认知中的一台 PLC",
//   而 QJsonObject 里还可能带着 timeout_ms / poll_interval_ms 这些
//   "代码不认识但用户可能有"的字段。草稿结构刻意【原样带着这些字段】
//   (extra 成员),存回去时原样写回 —— 重建 == 宣告"代码没列的字段一律丢弃",
//   理由与 PlcConnectionManager::lastConfig_ 完全同源。
struct PlcConfigRow {
    // ★ 键名在 plcs[] 里是 "id",【不是】"plc_id" —— 后者是【变量】
    //   那个字段(JsonSerializer.cpp:30)。两者长得像但层级不同:
    //   plcs[].id 是"这台 PLC 叫什么",variables[].plc_id 是"这个变量归哪台"。
    //   写错了不报编译错,只让 parseEntry 说"缺少 id"然后整台 rejected,
    //   界面上只显示"不在线"—— 用户看不出是键名错了。
    QString id;         // 【改了等于换了一台】——变量靠它找过来
    QString host;
    int     port       = 502;
    QString protocol   = QStringLiteral("modbus");
    // ★ 这三个在工程文件里是【嵌套】在 plc.modbus 对象里的,不是平铺
    //   (见 Project::defaultPlcFromTcp:133-137)。平铺着读会永远读到缺省值,
    //   而界面显示的是真值 —— 用户改了也存不进去,且看不出没生效。
    int     timeoutMs  = 1000;
    int     maxRetries = 5;
    int     pollMs     = 1000;
    // ★ slave_id:Modbus unit id。范围 1..247,【不给 0】——
    //   0 是 Modbus 的广播地址(写进去全网响应、读没有响应),
    //   而 TCP 帧里的 Unit ID 字段宽度就是 1 字节,能装下 0。
    //   下拉框不给、SpinBox 也不给,从入口就堵死。
    int     slaveId    = 1;

    // ★ 引用它的变量数。见上面的"删除被引用的 PLC"那段。
    int     refCount   = 0;

    // ★ 用户没碰过的字段原样带着。存回去时并进 QJsonObject。
    //   没有它们 = 用户文件里的未知字段会因为打开一次对话框而消失。
    QJsonObject extra;
    // 同理:plc.modbus 对象里我们不认识的键(今天没有,将来可能有)。
    QJsonObject extraModbus;

    // ★★ "用户文件里原来有没有 slave_id 这个键"。
    //   【它和 slaveId != 1 是两回事】值填了 1 但键存在 = 用户手填过 1;
    //   键不存在 = 从来没配过。存回去时前者保留、后者不凭空生成 ——
    //   否则用户只是打开对话框看了一眼,git 里就多出一行"他改过的"diff。
    bool hadSlaveIdKey = false;
};

class PlcConfigDialog : public QDialog {
    Q_OBJECT
public:
    // plcMgr / varMgr / project 都不接管生命周期(由主窗口持有,活得比对话框长)
    //   plcMgr 传 nullptr 时"应用到运行中"会拒绝并说明原因,
    //   而不是"假装应用成功了"—— 与 AlarmConfigDialog 传 nullptr
    //   拒绝新建报警是同一条取舍。
    explicit PlcConfigDialog(PlcConnectionManager* plcMgr,
                              VariableManager* varMgr = nullptr,
                              QWidget* parent = nullptr);
    ~PlcConfigDialog() override;

    // 要求:CurrentUser 有 EditProject 权限(否则 exec() 拒绝)
    int exec() override;

    // ===== 给测试/宿主看的只读访问器 =====
    //   【为什么暴露】这个对话框的价值全部体现在"用户能看到什么、改了什么"。
    //   反射 private 成员去断言,测的就成了实现细节,改个成员名就红,
    //   却证明不了界面真的对(理由见 AlarmConfigDialog.h 同一处注释)。
    int     rowCount() const;
    QString idAt(int row) const;
    QString hostAt(int row) const;
    int     portAt(int row) const;
    QString protocolAt(int row) const;
    int     slaveIdAt(int row) const;
    int     refCountAt(int row) const;

    // ★ 从草稿导出 plcs[] 数组。字段顺序与工程文件里一致。
    //   【这是草稿唯一的落地形态】既用于存盘,也用于"应用到运行中"。
    QJsonArray exportDraftToJson() const;

    // 存盘前的校验(与 PlcConnectionManager::parseEntry 同口径,不重复实现一套)
    //   检查:plc_id 非空且唯一、host 非空、port 合法、slave_id 在 1..247。
    //   返回 false 时 errorOut 说明【哪一行、为什么】。
    bool validateDraft(QString* errorOut) const;

    // ★ 保存草稿到工程文件的 plcs[] 段。其余键原样保留。
    //   path 必须指向【已存在的合法工程文件】(理由见 Project::replaceAlarmsInFile)。
    bool saveDraftToProjectFile(const QString& path, QString* errorOut);

    // ★ 应用草稿到【运行中】的 PlcConnectionManager。
    //   真正的重连判据全在 PlcConnectionManager::applyConfig 里
    //   (只比 host/port/protocol/slave_id 四元组)—— 对话框不重复这个逻辑,
    //   两处各判一次必然会判得不一样。
    //   返回 true 表示草稿已递下去;失败时 errorOut 说明原因。
    bool applyDraftToRunning(QString* errorOut);

    // ★ 从草稿里删掉一行【不做任何 UI 询问】。onRemove = 弹确认框 + 调它。
    //   理由见头注释 ★ removeRowFromDraft 那一段。
    //   返回 false = 行号越界。
    bool removeRowFromDraft(int row);

    // ★ 新增一行,返回新行号。失败(见不到能用的名字)返回 -1。
    //   【为什么要返回行号】界面上要把它选中,测试要拿到它去改字段。
    int  addRowToDraft();

private slots:
    void onAdd();
    void onRemove();
    void onSaveToProject();
    void onApplyToRunning();
    void onApplyFieldEdits(QTableWidgetItem* item);   // 表格改动 → 回写草稿行
    // ★ 协议 / 从站号是 cellWidget(下拉 / SpinBox),不走 itemChanged,
    //   所以各有各的槽。它们靠 sender()->property("plcRow") 认回自己在第几行。
    void onProtocolComboChanged(int index);
    void onSlaveIdSpinChanged(int value);

private:
    void setupTable();
    void setupButtons();
    // 从 plcMgr_->lastConfig() 读成草稿。没有 lastConfig() 时是空表 ——
    //   【不】从 Project 读:Project 那份是"文件里的", PlcConnectionManager
    //   那份是"用户刚应用过的",对话框要显示的是后者。
    void reloadFromManager();
    void fillTable();
    void updateInfo();
    // ★ 未被任何变量引用的行才给下一个占位 id。
    //   重名会导致加载工程时后者覆盖前者,且【不报错】—— 与
    //   AlarmConfigDialog::nextFreeDraftName 同一个坑。
    QString nextFreeDraftId() const;
    // 有多少变量正在引用这个 plcId。
    static int countVariablesUsing(const VariableManager* varMgr, const QString& plcId);

    PlcConnectionManager* plcMgr_ = nullptr;
    // 不接管生命周期(hmi 持有它)。用于算"引用变量数"这一列。
    VariableManager*     varMgr_  = nullptr;

    QList<PlcConfigRow> rows_;   // 草稿(★ 本对话框唯一的数据源)

    // 程序性改表格时置 true,让 itemChanged 处理器知道自己不是"用户改的"。
    //   【为什么需要】fillTable 里的 setItem 同样会发 itemChanged,
    //   不挡的话填表会触发回写,而那时 rows_ 可能还没填完 —— 静默错行。
    bool suppressItemChanged_ = false;

    QTableWidget* table_      = nullptr;
    QLabel*       infoLabel_  = nullptr;
    // ★ "这一行正被几个变量引用"。默认关掉这一列的用户看不懂是干嘛的。
    QLabel*       refHint_    = nullptr;

    QPushButton*  addBtn_    = nullptr;
    QPushButton*  removeBtn_ = nullptr;
    // ★ 两个不同的出口,不是替代关系:saveBtn_ 定版存档(重载工程才生效),
    //   applyBtn_ 立刻生效(不落盘)。理由见头注释。
    QPushButton*  saveBtn_   = nullptr;
    QPushButton*  applyBtn_  = nullptr;
    QPushButton*  closeBtn_  = nullptr;
};

} // namespace qwhmi::plc
