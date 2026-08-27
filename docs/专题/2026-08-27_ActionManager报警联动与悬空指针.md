ActionManager = 一张「如果…就…」清单 + 借来的门牌号。

报警联动动作模块。报警一响,自动把某个变量改成某个值(温度高 → 速度 = 0 模拟停机),不等操作员动手。全文件 58 行(.h)+ 69 行(.cpp),干的就这一件事。

【它在 6 层里的位置】

UI(hmi)              ┐
业务协调(AlarmMgr)   │  ← ActionManager 也在这层
业务执行(Checker)    ├─ Qt connect / 直接调用 ─ 信号
调度(Poll/Write)     │
协议(TCP/Modbus)     │
数据(Variable)       │
配置(Project)        ┘

上游:AlarmManager::alarmTriggered(5.2 就有的信号)
下游:不直接碰 Modbus,而是走 Variable::setValue + markDirty → WriteScheduler

关键: 它是"业务协调"层的旁挂模块,拔掉它整个工程照样跑 —— 这个性质很重要,见文末【定位】。

【核心数据结构:一张清单】

QHash<QString, Mapping> mappings_;   // 报警名 → 动作

struct Mapping {
    QString   alarmName;   // 冗余存一份(6.2 为 saveToFile 加的)
    Variable* variable;    // ★ 裸指针,不接管生命周期
    QVariant  value;       // 按值深拷贝,自包含
};

清单实际内容(hmi.cpp:837 initActionManager):
    "温度高" → speed = 0
    "温度低" → speed = 0

关键: 用 QHash 意味着一个报警名只能绑一个动作(同名 addMapping 直接覆盖)。
      想"温度高 → 同时停机 + 关阀 + 亮灯"目前做不到,得换 QMultiHash。

【addMapping 的三个细节】

void ActionManager::addMapping(alarmName, variable, value) {
    if (!variable || alarmName.isEmpty()) return;   // ① 静默失败
    Mapping m;                                       // ② 显式逐字段赋值
    m.alarmName = alarmName;
    m.variable  = variable;
    m.value     = value;
    mappings_[alarmName] = m;                        // ③ 覆盖语义
}

① 静默失败:参数不合法直接 return,不报错不返回 false。
   硬编码年代没问题;6.3 之后 Project::loadFromFile:229 也调它,参数来自 JSON。
   好在 Project.cpp:223-227 自己先 findVariable + emit loadError 把错堵在上游了。
   → 责任分配是隐式的:上游负责报错,这里只兜底。

② 为什么不用聚合初始化 {alarmName, variable, value}:
   5.6 时 Mapping 只有 2 个字段,6.2 加了 alarmName。
   聚合初始化依赖字段顺序,以后调整顺序会静默错位赋值且编译通过。
   逐字段赋值多写 4 行,换来"加字段/换顺序都不会错"。

③ mappings_[k] = m 是覆盖:key 不存在时 QHash 先默认构造一个 Mapping
   (此刻 variable 是未初始化野指针!)再整体赋值覆盖 —— 因为紧接着就赋值,安全。

关键: 两个字段的存储方式截然不同。value 是深拷贝、自包含、随 QHash 一起析构;
      variable 只存 8 字节地址,不拷贝对象、不 delete —— 它只是一张借条。

【为什么不直接写 PLC】

onAlarmTriggered 里不调 modbus_,而是:

    it->variable->setValue(it->value);   // ① 只改本地值
    it->variable->markDirty();           // ② 标脏

然后 WriteScheduler::onTick 每 100ms 扫 dirty 变量统一下发(WriteScheduler.cpp:52)。

白捡的好处(阶段 4.3.0 已经写好的):
    - 写失败自动重试(3 次 → kMaxWriteRetries)
    - 写操作审计日志(auditAppended → auditTable)
    - 失败上限熔断 → emit writeFailed → WriteFailChecker → 报警
    - 写成功后 clearDirty
    - 不阻塞报警检测(Modbus 写是慢 IO)

代价: 联动不是即时的,最坏延迟一个 tick(~100ms)。

关键: 这是阶段 4 基础设施复用的典型案例 —— 加一层"标脏"间接,换一整套可靠性机制。

【裸指针:清单上记的是门牌号,不是名字】

所有权链(单一所有者):

hmi(窗口)
 └─ VariableManager          ← 唯一所有者,addVariable 里 setParent(this)
     ├─ Variable "Pressure"     Qt 父子机制保证析构时自动 delete
     ├─ Variable "Temperature"  (VariableManager.cpp:11-16 特意注释:
     └─ Variable "Speed"         千万别再 qDeleteAll,那是双重 delete)

hmi
 └─ ActionManager            ← 只抄了 Speed 的地址,是借用者
     └─ mappings_["温度高"].variable ──借用──▶ Speed

ActionManager 手里的 Variable* :
    不是 QPointer  → 对象被 delete 时不会自动变 nullptr
    不是 shared_ptr → 不延长对象寿命
    连"对象还活着吗"都不知道

关键: 这个设计的隐含前提是「Variable 生命周期 == 程序生命周期」。
      5.6 阶段完全成立(变量开机建、关机销,中途从不消失)。
      阶段 6.3 引入"打开工程文件",前提当场崩塌。

【6.3 翻车现场】

Project::loadFromFile 的策略是 Q4-A「全删干净」(Project.cpp:174-192):

    varMgr_->clear();        // ← 所有 Variable 真的被 delete
    alarmMgr_->clearAll();
    actionMgr_->clear();
    ...然后重新 new 一批同名但地址不同的新对象

时序:

    用户点"打开工程"
     └─ varMgr_->clear()
         └─ delete 旧 Speed(假设在 0x1A2B)
            ↑ 此刻 mappings_["温度高"].variable 仍 == 0x1A2B(已释放)
     └─ addVariable(新 Speed,在 0x9F70)
    ...
    温度升到 55,报警触发
     └─ ActionManager::onAlarmTriggered
         └─ it->variable->setValue(0)   ← 往 0x1A2B 写,那是废墟

两种结果,都很糟:
    1. 当场崩溃 —— 算好的,起码知道出事了
    2. 更常见:废墟暂时没人用,代码"成功"写了一下,日志还显示已停机
       但机器根本没停。过一会儿别的对象搬进这块地址,程序在
       完全不相干的地方莫名崩掉,查都没法查

关键: 第 2 种叫"静默失效"。对工控软件是最忌讳的一类故障 —— 系统看起来一切正常。
      而且受害者不止 ActionManager:WriteScheduler::pending_、
      ThresholdAlarmChecker::variable_、Binding 的 UI 绑定,凡是持裸 Variable* 的都中招。

【补丁怎么打的:拆迁前先广播】

改动 1 —— VariableManager 加信号(VariableManager.h:44)

    void variableReplaced(Variable* oldVar, Variable* newVar);
    // 场景 1 同名替换:old=旧, new=新
    // 场景 2 clear 删除:old=旧, new=nullptr

改动 2 —— 两条删除路径都 emit,时机有讲究

  同名替换(VariableManager.cpp:22-32):
      Variable* old = vars_.value(name);
      vars_.remove(name); v->setParent(this); vars_.insert(name, v);
      emit variableReplaced(old, v);   // ← emit 在 delete 之前
      delete old;
  为什么 emit 在 delete 前:注释说"让监听者能读 old 的字段做最后处理"。
  此刻 old 还是合法对象,可以安全调 old->name();新对象也已进 hash,newVar 可用。

  clear(VariableManager.cpp:87-95):
      QList<Variable*> oldVars = vars_.values();   // ① 先快照指针
      vars_.clear();                                // ② 再清容器
      for (Variable* old : oldVars) {
          emit variableReplaced(old, nullptr);      // ③ 逐个通知
          delete old;
      }
  为什么先 clear 再 emit:emit 是同步调用,监听者槽里可能反过来调
  varMgr_->findVariable() / count()。边遍历边 emit 边 delete 会让监听者
  看到一个半清空、正在被修改的容器(迭代器失效 + 状态不一致)。
  先快照、清空、再慢慢通知 → 容器全程一致。

改动 3 —— ActionManager 加槽(ActionManager.cpp:40-46)

    void ActionManager::onVariableReplaced(Variable* oldVar, Variable* newVar) {
        for (auto& m : mappings_) {        // Qt 容器 range-for 迭代 value
            if (m.variable == oldVar) {
                m.variable = newVar;        // 可能是 nullptr
            }
        }
    }

  三个易错点:
    - auto& 必须是引用。写成 auto m 就是改副本,补丁静默失效且无编译警告
    - 按地址比较,不按名字。地址才是唯一可靠的身份标识
    - 必须全表扫描,不能 find 了就收工 —— "温度高"和"温度低"都指向 speed,
      两条都要改。这里偷懒用 find 会漏改一条

  连线位置不统一(值得留意):
    WriteScheduler 在自己构造函数里连,还带 if (varMgr_) 判空(WriteScheduler.cpp:20-24)
    ActionManager 在 hmi.cpp:329-331 外部连 ← 这种更容易漏

改动 4 —— 执行时再兜一次底(ActionManager.cpp:61-62)

    if (!it->variable) return;   // 房子没了,别去了

  因为 clear 路径会把指针置 nullptr,而那条规则本身没被删掉。

关键: 这套机制的本质是"所有者广播 + 各持有者自行修补"。
      改动最小、不动现有所有权模型;代价是新模块必须记得连信号,靠自觉。

【跟改善计划 #5 #7 的关系(★ 重要)】

docs/改善清单.md 里早就记了同一个病根:

    #5  vars_ raw pointer 隐式所有权     VariableManager.cpp   double-free  12xp
    #7  bindEdit lambda 捕获 v 段错风险   Binding.cpp           UI 删 Variable 后崩  10xp
    批次 2 计划:VariableManager 改 QSharedPointer<Variable>(1 小时)

也就是说:6.3 的 variableReplaced 广播,其实是给 #5 / #7 打的一个临时补丁,
而原计划是用 QSharedPointer 从根上解决。现在有三条竞争路线:

  路线 A(现状) 信号广播 + 各自修指针   已实现   靠自觉,新模块易漏
  路线 B(原计划)改 QSharedPointer      未做     跟 Qt parent-child 自动 delete 打架,要小心双删
  路线 C(推荐) Mapping 改存变量名      未做     最轻,做完 ActionManager 里整套广播可删

关键: 路线 C 对 ActionManager 单点最优,但解决不了 Binding / Checker;
      路线 B 是全局解法但改动面大。三条不互斥,可以 C 先做掉 ActionManager,
      B 留到阶段 7 重构时统一处理。

【4 个遗留问题(已登记到改善计划 E 类 #27-#30)】

  #27 联动静默失效:变量被删后 onAlarmTriggered 默默 return,
      不吭声不记日志。报警响了、安全动作没执行、没人知道
  #28 撒谎上报:Variable::setValue 返回 Accepted/NoChange/Rejected 三态,
      现在代码完全不看返回值。即使被 Rejected(类型不符/超 setRange 范围),
      照样 markDirty + emit actionExecuted → 审计日志记假账
  #29 Mapping 改存变量名(路线 C),做完可删掉 onVariableReplaced 整套
  #30 无 PLC 看门狗心跳(见 docs/通用知识/工业界引用失效与安全分层速查.md)

【定位:这个模块不是安全功能】

按工业功能安全标准(IEC 61508 / IEC 61511),真正的安全联锁必须独立于监视系统。
HMI 是跑在 Windows 上的应用:会崩、会卡、会被杀毒软件占满 CPU、网线会被踢掉。
「温度高 → 停机」这条逻辑住在 HMI 里,意味着 HMI 一崩安全保护就没了,而机器还在转。

正规分工:
    物理层  机械温控开关,超温直接断电        ← 最可靠,零代码
    PLC     温度高 → 停机 + HMI 心跳看门狗    ← 安全逻辑住这儿
    HMI     显示 / 操作员下命令 / 记历史      ← 只做这些

所以 ActionManager 顶多算"操作便利功能"(正常时帮操作员省几秒),
绝不能是唯一那道防线。它挂了、指针野了、消息丢了,都不该导致事故。

关键: 想通这点之后,悬空指针的严重程度降了一档 —— 仍是必修 bug,
      但不再是"会出人命的 bug"。

【消化口诀】
- ActionManager = 一张清单 + 三个动作(查表 / 改值 / 标脏)
- 它不写 PLC,只标脏 → 白捡 WriteScheduler 的重试+审计+熔断
- 清单记的是门牌号(地址),不是名字 → 房子拆了就是野指针
- 6.3 补丁 = 拆迁前先广播,各人自己改本子
- emit 必须在 delete 之前;clear 必须先快照再通知
- auto& 不是 auto;全表扫不是 find
- 存名字 > 存句柄 > 存地址(工业界主流是存名字)
- 静默失效比崩溃更可怕:失效要变成看得见的坏状态
- 安全联锁属于 PLC,HMI 只是便利功能

---
_子衿整理 · 2026-08-27 · 起因:Wqian 问 ActionManager.h 是干啥的_
_相关:docs/通用知识/工业界引用失效与安全分层速查.md(通用认知)/ docs/改善清单.md E 类(待办)_
