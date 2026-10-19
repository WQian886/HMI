// 阶段 6.2(2026-08-07):Project 类
//   主存读入口:saveToFile 把当前工程状态 → JSON 文件
//   loadFromFile 是 6.3 阶段,本阶段只写空壳(返回 false)
//
//   ★ E.3(2026-09-21):JSON 存读扩展
//   - schema_version 2 → 3 + migrateV2ToV3
//   - saveToFile/loadFromFile 加 controls 参数 + controlsLoaded 信号
//   - 画布控件(ControlBase)用 propertiesToJson 反射序列化
//
//   【设计】Project 持有 4 个管理器指针 + 1 个 TcpClient 指针(通讯)
//          saveToFile 不接管生命周期,只读
//   【取舍】TcpClient 用于拿 host/port(阶段 6.2 getter 已经加好)
//          Modbus 详细参数暂时硬编码在 saveToFile 里(以后可加 ModbusMaster getter)
//   【参照】docs/专题/阶段6_schema设计.md

#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QJsonArray>
#include <QJsonObject>

#include <functional>

class VariableManager;
class AlarmManager;
class ActionManager;
class PollScheduler;   // ★ 6.3 新增:ThresholdAlarmChecker 构造需要 PollScheduler*
class TcpClient;

namespace qwhmi::widgets { class ControlBase; }

class Project : public QObject {
    Q_OBJECT
public:
    // ★ C.0(2026-09-17):schema_version 演进框架
    //   - 老文件 schema_version="1.0"(字符串)
    //   - v2 改成整数(2)+ 顶级 plcs[] 数组(替代原 communications 单 PLC)
    //   - ★ E.3(2026-09-21):v3 加 screens[] 数组(画布控件)
    //   - 每次加字段时 kCurrentSchemaVersion++ + 写新的 migrateVxToVxPlus1 函数
    static constexpr int kCurrentSchemaVersion = 3;

    explicit Project(VariableManager* varMgr,
                     AlarmManager* alarmMgr,
                     ActionManager* actionMgr,
                     PollScheduler* pollScheduler,   // ★ 6.3 新增
                     TcpClient* tcp,
                     QObject* parent = nullptr);
    ~Project() override;

    // 主入口:把当前工程状态写到 JSON 文件
    // 成功返回 true;失败(写文件失败 / JSON 构造失败)返回 false + emit loadError
    bool saveToFile(const QString& path);

    // ★ E.3:带 controls 的 saveToFile — 把画布控件也序列化到 screens[0].controls
    //   不接管 controls 生命周期(由 EditorMainWindow 拥有)
    bool saveToFile(const QString& path, const QList<qwhmi::widgets::ControlBase*>& controls);

    // 6.3 阶段用,本阶段只写空壳(返回 false)
    bool loadFromFile(const QString& path);

    // ★ F4-c(2026-10-08):只替换工程文件里的 alarms 段,其余原样保留。
    //
    //   【为什么需要它,为什么不能用 saveToFile】
    //   saveToFile 是"把当前【运行中】的管理器状态整个重新序列化"。
    //   而 F4-c 的草稿【故意不】回写 AlarmManager(见 AlarmConfigDialog.h 的不变量),
    //   所以走 saveToFile 会把草稿原封不动丢掉、覆盖成运行中的旧值 ——
    //   存了等于没存。这个方法是唯一能把草稿真正落地的出口。
    //
    //   【为什么必须"只改一段"而不是重写整个文件】
    //   工程文件里还有 variables / actions / plcs / screens[].controls 等段。
    //   若用一份残缺的 root 覆盖,这些段会被静默清空 —— 而对话框里根本没碰过它们,
    //   用户不会知道自己把画面和变量弄丢了。所以这里读原文件、只替换 alarms、
    //   写回。其它键一律原样保留。
    //
    //   【前置条件】path 必须已存在且是合法 JSON object —— 不负责创建新工程。
    //   理由:凭空造一个只有 alarms 的文件,加载时必然缺 variables 而失败
    //   (见 loadRootFromFile 第 5 步)。宁可报错也不要写出一个打不开的文件。
    //
    //   成功返回 true;失败时 errorOut 填人类可读的原因(调用方直接弹窗)。
    static bool replaceAlarmsInFile(const QString& path,
                                    const QJsonArray& alarms,
                                    QString* errorOut);

    // ★ F4-c:校验草稿能不能安全落进工程文件。
    //   【为什么必须先校验】loadRootFromFile 里有两条硬失败(Project.cpp:211-221):
    //     - 报警引用了不存在的 variable → 整个文件加载失败
    //     - 报警重名 → addChecker 返回 false → 整个文件加载失败
    //   也就是说,一条没填完的草稿存进去,产出的工程文件是【打不开】的。
    //   与其存完让用户下次开工程时才发现,不如存之前就说清楚。
    //   【为什么不在这里查 variable 存不存在】因为要访问 VariableManager,
    //   而这是个 static 方法。查变量存在性的是 validateDraftAgainstManager。
    //
    //   校验内容:报警名非空、报警名唯一。返回 false 时 errorOut 说明原因。
    static bool validateAlarmsArray(const QJsonArray& alarms, QString* errorOut);

    // ★ C.0(2026-09-17):schema 迁移公开 API(单测和外部工具可直接用)
    //   - migrateTo:从 fromVersion 链式迁移到 toVersion(内部按需调 migrateVxToVxPlus1)
    //   - migrateV1ToV2:v1 → v2 单步迁移(单独暴露给单测)
    //   - migrateV2ToV3:v2 → v3 单步迁移(E.3 加 screens)
    //   返回 false 表示迁移失败(原 JSON 不变,调用方报 loadError)
    bool migrateTo(QJsonObject& root, int fromVersion, int toVersion);
    bool migrateV1ToV2(QJsonObject& root);
    bool migrateV2ToV3(QJsonObject& root);

    // ★ E.3:控件 JSON 序列化静态工具(供测试和 EditorMainWindow 直接调用)
    //   - controlsToJson:遍历控件列表 → JSON array(走 ControlBase::propertiesToJson 反射)
    //   - loadControlsFromJson:从 JSON array 重建控件列表(用 factory 按 type 字符串 new)
    //     factory 参数让调用方决定所有权(本函数不 delete 返回的指针)
    static QJsonArray controlsToJson(const QList<qwhmi::widgets::ControlBase*>& controls);
    // factory: 输入 typeName(如 "BarControl"),返回 new 出来的 ControlBase*(失败返回 nullptr)
    // outControls: 成功创建的控件列表(调用方负责 delete)
    // 返回成功创建的控件数
    static int loadControlsFromJson(const QJsonArray& arr,
                                    qwhmi::widgets::ControlBase* (*factory)(const QString& typeName),
                                    QList<qwhmi::widgets::ControlBase*>& outControls);

    // ★ 2026-10-16:PLC 配置存取(多 PLC 第一步 · 2a)
    //
    //   【为什么需要它】
    //     2a 之前,plcs[] 这个数组是"花瓶":加载只读 plcsArr[0](Project.cpp 第 8 步),
    //     保存写死一台(id 恒为 "plc1")。文件里就算手写了 5 台,程序也只认第一台,
    //     保存时还会把另外 4 台【静默丢掉】—— 而这不报错、不警告。
    //     典型症状:用户配了 3 台 PLC,存一次,打开发现只剩 1 台,且没有任何提示。
    //
    //   【2a / 2b 的分工 —— 边界很重要】
    //     本组 API 只负责【数据存得住】:load 能把全部 PLC 读进来,
    //     save 能把全部 PLC 原样写回去。
    //     它【永远不负责真的连多台】—— 2b 起,建连接由 setPlcApplier 递出去的
    //     PlcConnectionManager 做(见下面那个方法)。
    //     拆成两层的理由见 setPlcApplier 的注释:Project 不该握着通讯层对象。
    //
    //   【为什么用 JSON 数组原样持有,而不是解析成结构体】
    //     解析成结构体会要求现在就定义"PLC 配置到底有哪些字段",
    //     而 2b 要改的正是这个形状(每台 PLC 要带自己的一堆通讯参数)。
    //     原样持有 = 2b 改形状时这层不用跟着动。代价是不做校验,
    //     但校验在 loadPlcsFromJson 里做【最浅一层】—— 只查会导致运行期
    //     静默失效的东西(id 为空 / host 为空 / 端口越界),字段本身不解释。

    // 从工程 JSON 的 plcs[] 读出全部 PLC 配置。
    //   - 缺 plcs 键 / 空数组 → 返回空数组(不是错误,老工程可能有 communications 已被迁移)
    //   - 每项校验 id / host / port;不合格的**跳过并 qWarning 点名**,
    //     不因一台配错就让整个工程打不开(与 F3 诊断策略一致:
    //     能继续就继续,但一定要说清楚哪一项被丢了)
    //   - 合格项按原样存入内部数组(字段不解释,交给 PlcConnectionManager 去解释)
    //   返回成功接受的台数
    int loadPlcsFromJson(const QJsonObject& root);

    // 取当前持有的 PLC 配置(原样 QJsonObject 数组)
    const QJsonArray& plcs() const { return plcs_; }

    // 设置 PLC 配置数组。
    //   【什么时候需要】程序运行中通过界面增删 PLC 后,调用它让下次 save 写出去。
    //   【校验】同样过滤掉 id/host 为空、端口越界的项 —— 宁可少存一台,
    //   也不要存出一台"打开工程后连不上、且看不出为什么"的配置。
    //   返回实际存下的台数
    int setPlcs(const QJsonArray& plcs);

    // 默认单台 PLC 配置(从 tcp_ 的当前 host/port 生成)。
    //   【什么时候用】程序【没有】加载过任何工程、直接新建保存时。
    //   【为什么不能一律用这个】那会把多 PLC 配置覆盖成 1 台 ——
    //   这正是 2a 要修的那个 bug 换个方向重演一次。
    static QJsonObject defaultPlcFromTcp(TcpClient* tcp);

    // ============================================================
    // ★ 2b(2026-10-19):把"建立连接"这个副作用【交出去】
    // ============================================================
    //
    // 【为什么要交出去】Project 一直是"配置存取"这一层,唯独第 8 步
    //   顺手调了 tcp_->connectTo —— 也就是它【自己】握着通讯层的对象,
    //   于是"加载工程"这个纯数据动作里藏了一个连网络的副作用。
    //   后果:想支持 N 台 PLC 就得改 Project(Project 得知道 PlcConnectionManager),
    //   而 Project 是全项目跟工程文件耦合最深的一个类。
    //
    //   改成:Project 只负责把 plcs_ 递出去,由 hmi 拼装层决定递给谁。
    //   测试里则可以递一个纯计数的 lambda,不必起任何连接。
    //
    // 【为什么不直接调 PlcConnectionManager】
    //   hmi_core 不含 hmi.cpp,但【含】Project.cpp。让 Project 依赖
    //   PlcConnectionManager 会把"连不连"重新焊死在 Project 上 ——
    //   那正是这里要拆的东西。std::function 是这里唯一合适的抽象层级:
    //   Project 不需要知道被调的是真连接还是测试探针。
    //
    // 【不设时的行为】什么都不做(纯数据层)。
    //   ★ 不是"退回只连第一台" —— 2b 之前的 loadFromFile 会调 tcp_->connectTo,
    //     而那段代码在本提交里被【删掉】了。所以不设 applier 的调用方
    //     (主要是测试)拿到的是"配置读进来了,没人连",这是准确描述。
    using PlcApplier = std::function<void(const QJsonArray&)>;
    void setPlcApplier(PlcApplier applier) { plcApplier_ = std::move(applier); }

signals:
    void saved(const QString& path);
    void loaded(const QString& path);   // ★ 6.3:loadFromFile 成功(给 hmi 重绑 UI 用)
    // ★ E.3:loadFromFile 后发出,带刚加载的控件列表(EditorMainWindow 监听后建到画布)
    void controlsLoaded(const QList<qwhmi::widgets::ControlBase*>& controls);
    void loadError(const QString& reason);

private:
    // 共享 saveToFile 内部:构造 root JSON(不含 controls),返回 true/false 表示 IO 成功
    bool saveRootToFile(const QString& path, const QJsonObject& root);

    // 共享 loadFromFile 内部:读文件 + 解析 + schema 校验 + migrate + 应用 vars/alarms/actions/plcs
    // 返回 root QJsonObject(供 E.3 进一步解析 screens[0].controls);失败时 root 设为空
    bool loadRootFromFile(const QString& path, QJsonObject& outRoot);

    VariableManager* varMgr_;
    AlarmManager*    alarmMgr_;
    ActionManager*   actionMgr_;
    PollScheduler*   pollScheduler_;
    TcpClient*       tcp_;

    // ★ 2026-10-16:PLC 配置(多 PLC 2a)。加载时填满,保存时原样写回。
    //   【和 tcp_ 的关系】tcp_ 是【运行期连接对象】,只有一台;
    //   plcs_ 是【工程配置】,可以有 N 台。两者刻意分开 ——
    //   2b 起,运行期的 N 台由 plcApplier_ 交给 PlcConnectionManager 管,
    //   tcp_ 只剩"手工调试通道 + defaultPlcFromTcp 取 host/port"这两个用途。
    QJsonArray plcs_;

    // ★ 2b(2026-10-19):加载工程后谁来建连接。没设 = 没人建。详见 setPlcApplier。
    PlcApplier plcApplier_;
};