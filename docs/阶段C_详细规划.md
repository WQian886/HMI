# 阶段 C · 详细规划

> 适用:**阶段 B(用户/角色/权限 + 操作审计)已收官**,准备开干 C(多协议 + 多 PLC)。
> 维护人:Wqian + AI 助手  ｜  创建:2026-09-17(B5 收尾 + B 阶段通过后) ｜ v1.1:2026-09-17 优化
> 配套:`docs/现状.md` / `docs/路线图.md` / `docs/优化与改善计划.html`(v0.2.0)/ `docs/阶段B_从这里继续.md` / `docs/UI规划调研与方案.md`
> ⚠ **状态(2026-09-28 基于代码核对)**:🟡 **C 阶段部分完成** — `ModbusDriver` ✅ 已落地(`src/drivers/ModbusDriver.*`),`S7Driver` ⚠️ 当前是 stub(Snap7 LGPL 未真接,`loadSnap7Library()` 直接 return false),`EipDriver` ⚠️ 同上 stub(libplctag LGPL 未真接)。C.1/C.4/C.5 子阶段代码已落地,C.2/C.3 待 Wqian 拍板真接 vs 推 v0.3.0。完整阶段状态见 `docs/路线图.md`。

---

## 0. 一句话

**C 阶段 = 让 hmi.exe 跟西门子(S7)+ 罗克韦尔(EtherNet/IP)PLC 说话 + 多 PLC 架构。**

目前只会 Modbus — 工业现场 60% PLC 是西门子,不支持就卖不出去。

---

## 1. 为什么 C 是"吃透 hmi 核心"

Wqian 的话:"c 阶段应该是算上懂啊 hmi 的核心了"(打字错位,意思:**C 阶段是吃透 hmi 核心架构的关键期**)

理由 — C 阶段会**触碰 hmi 所有 7 层架构**:

```
UI(hmi)              ← C.4 统一协议选择 UI
业务协调(AlarmManager/ActionManager) ← 多 PLC 时报警跨 PLC 联动
业务执行(Checker)   ← 多 PLC 时变量地址唯一性
调度(PollScheduler)  ← ★★★ IPollDriver 接口抽象层 ★★★
协议(S7Driver/EipDriver) ← 新增 2 个,ModbusMaster 改造成 ModbusDriver
传输(TcpClient)      ← S7/EIP 也用 TCP,但 Snap7/libplctag 自带 socket
数据(Variable/Manager) ← 多 PLC 时 Variable 需要带 plcId
配置(Project/JsonSerializer) ← 工程文件存协议类型 + IP + port(+ schema 演进)
```

C 阶段会让 Wqian 真正理解 **hmi 是怎么把"PLC 数据 → 内存变量 → UI 显示"这条管道做出来的** — 这是 hmi 的灵魂。

---

## 2. 范围 / 非范围

### 包含

- **C.0** 工程文件 schema 演进(schema_version 自动迁移)⭐ 新增
- **C.1** 协议抽象层(IPollDriver 接口 — 通用 `read(area, addr, count)`)
- **C.2** S7 协议适配器(Snap7 LGPL 动态链接,1 driver = 1 PLC)
- **C.3** EtherNet/IP 协议适配器(libplctag LGPL 动态链接,1 driver = 1 PLC)
- **C.4** 统一协议选择 UI(主窗口下拉框,不允许跨协议切换工程)
- **C.5** 多 PLC 架构(Variable 带 plcId,跨 PLC 联动 + 失败处理)
- **C.6a** 真机技术验证(3 仿真器跑通)
- **C.6b** LGPL 合规 + Demo 视频

### 不包含

- 协议栈自己实现(用现成 LGPL 库:Snap7/libplctag)
- OPC UA / REST API(阶段 G)
- 历史曲线 / 报表(阶段 D,详见 `docs/UI规划调研与方案.md`)
- 画面编辑器(阶段 E,详见 `docs/UI规划调研与方案.md`)
- 加密通讯(S7Comm-Plus / CIP Security)— 太复杂,后续可选

---

## 3. 为什么 C 阶段估时 9-12 周(比路线图 6-8 周长)

路线图锁的 C 阶段是 6-8 周,实际估算会偏长,原因:

| 因素 | 路线图估时 | 实际估时 | 差额原因 |
|---|---|---|---|
| C.1 协议抽象 | 包含在 C | 1 周(纯代码) | OK,无差额 |
| C.2 S7 | 2 周 | 2-3 周 | +1 周:Snap7 协议细节(优化块 / 时钟同步 / 字节序) |
| C.3 EIP | 2 周 | 2-3 周 | +1 周:libplctag 异步 API + Logix Designer 试用限制 |
| C.5 多 PLC | 包含在 C | 2 周 | 路线图未单独估时,实际要做跨 PLC 联动 + 失败处理 |
| C.6 真机验证 | 1 周 | 3 周 | +2 周:LGPL 律师咨询 + 仿真器反复重装 + Demo 视频 |
| **合计** | **6-8 周** | **9-12 周** | **+3-4 周** |

**路线图把库调试 + 多 PLC + 合规 这 3 块低估了**。T3 业余节奏宁可估长不可估短 — 估短了会频繁砍功能,代码质量下降。

---

## 4. 6 个里程碑详细分解(+ C.0)

### C.0 工程文件 schema 演进(3-5 天)⭐ 新增

**为什么需要这个**:C 阶段给 Project 类加 `protocol: "modbus"/"s7"/"eip"` 字段 + Variable 加 `plcId` 字段 = **schema 变**。已有 default.json / project.json 文件如果不兼容,旧工程打开会崩。

**任务清单**:

| # | 任务 | 文件 | 估时 |
|---|---|---|---|
| 0.1 | 加 schema_version 自动迁移函数(`migrateV1ToV2()`) | `hmi/Project.cpp`(改) | 1 天 |
| 0.2 | loadFromFile 检测 schema_version,不匹配自动迁移 | `hmi/Project.cpp`(改) | 1 天 |
| 0.3 | 旧 default.json 加新字段默认值(protocol=modbus, plcId=plc1) | `hmi/default.json`(备份 + 改) | 0.5 天 |
| 0.4 | tst_ProjectSchema:旧 → 新 迁移单测 | `hmi/tests/tst_ProjectSchema.cpp`(新) | 1 天 |
| 0.5 | 文档:迁移策略 + 旧工程如何升级 | `docs/阶段C_schema迁移.md`(新) | 0.5 天 |

**关键设计**:
```cpp
// hmi/Project.h
class Project {
public:
    // 自动检测 schema_version,迁移到当前版本
    bool loadFromFile(const QString& path);
    
private:
    bool migrateV1ToV2(QJsonObject& root);  // 阶段 C 新增
    int schemaVersion() const;
};

// 迁移策略:
//   - 检测 root["schema_version"],int
//   - 如果 < 当前版本,依次调 migrateVxToVxPlus1
//   - 迁移失败 → 报错但不丢数据(给用户机会手动修)
```

**验收**:
- [ ] 旧 default.json(没有 protocol 字段)能打开,自动加 protocol=modbus
- [ ] tst_ProjectSchema 至少 3 个用例(v1→v2 / v2→v3 / 迁移失败)
- [ ] 迁移失败时弹错误对话框,用户可手动编辑 JSON

---

### C.1 协议抽象层(1 周)⭐ 地基

**目标**:抽出统一接口,让 3 个协议都能塞,业务层不用知道协议细节。

**关键约束**:**ModbusDriver 必须 100% 行为兼容现有 ModbusMaster,所有现有 68 测试无改动通过**。

**任务清单**:

| # | 任务 | 文件 | 估时 |
|---|---|---|---|
| 1.1 | 设计 IPollDriver 抽象接口(通用 `read(area, addr, count)`) | `hmi/src/drivers/IPollDriver.h`(新) | 0.5 天 |
| 1.2 | 把 ModbusMaster 包装成 ModbusDriver : IPollDriver(**行为兼容**) | `hmi/src/drivers/ModbusDriver.h/.cpp`(新) | 1 天 |
| 1.3 | PollScheduler 改用 IPollDriver* 而非 ModbusMaster* | `hmi/PollScheduler.h/.cpp`(改) | 1 天 |
| 1.4 | DriverFactory:根据字符串("modbus"/"s7"/"eip")创建对应 driver | `hmi/src/drivers/DriverFactory.h/.cpp`(新) | 0.5 天 |
| 1.5 | tst_IPollDriver:接口契约单测(Mock driver 验 PollScheduler) | `hmi/tests/tst_IPollDriver.cpp`(新) | 1 天 |
| 1.6 | 回归测试 + cmake + vcxproj 同步 |  | 0.5 天 |

**关键 API(通用版)**:
```cpp
// hmi/src/drivers/IPollDriver.h
namespace qwhmi::drivers {

// 通用数据区域(替代原 ReadResult 只支持 HoldingReg)
enum class RegisterArea {
    HoldingReg    = 0,  // Modbus 保持寄存器 / S7 DBW
    InputReg      = 1,  // Modbus 输入寄存器 / S7 IW
    Coils         = 2,  // Modbus 线圈 / S7 位输出
    DiscreteInput = 3,  // Modbus 离散输入 / S7 位输入
};

class IPollDriver : public QObject {
    Q_OBJECT
public:
    virtual ~IPollDriver() = default;
    virtual bool connect(const QString& host, quint16 port) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    
    // 通用读接口:支持 4 种数据区域,不只是保持寄存器
    struct ReadResult {
        bool success;
        QVector<quint16> registers;  // 线圈 / 离散输入映射成 0/1
        QString errorMsg;
    };
    virtual ReadResult read(RegisterArea area, int slaveId, int addr, int count) = 0;
    
    // 通用写接口
    virtual bool write(RegisterArea area, int slaveId, int addr, 
                       const QVector<quint16>& values) = 0;
    
signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString& msg);
};

// hmi/src/drivers/ModbusDriver.h
class ModbusDriver : public IPollDriver {
    // 内部包一个 ModbusMaster(不破坏现有 Modbus 实现)
    ModbusMaster* inner_;
public:
    ReadResult read(RegisterArea area, int slaveId, int addr, int count) override {
        // 内部根据 area 映射到 QModbusDataUnit::RegisterType
        // HoldingReg ↔ HoldingRegisters / InputReg ↔ InputRegisters / 等
    }
};

} // namespace qwhmi::drivers
```

**验收**:
- [ ] PollScheduler 用 `IPollDriver*` 接口指针能正常工作
- [ ] ModbusDriver 跑通,**68 个现有测试无任何改动全过**(行为兼容)
- [ ] tst_IPollDriver 用 mock driver 验证 PollScheduler 不依赖具体协议
- [ ] cmake + vcxproj 双 build 通过
- [ ] 现有功能 100% 保留(UI/写值/报警/工程文件读写都不变)

**吃透要点**:
- **接口隔离原则(IPI)**:业务层只依赖接口,不依赖实现 — 软件架构的"开闭原则"
- **为什么需要抽象层**:直接用 `ModbusMaster*` 的话,加 S7 就要改 PollScheduler;用 `IPollDriver*` 改 1 行就行
- **适配器模式(Adapter)**:ModbusDriver 是 ModbusMaster 的适配器,把它"老"接口包成"新"接口
- **为什么 100% 行为兼容**:C 是扩展不是重构,不能借机改 Modbus 行为(风险大)

---

### C.2 S7 协议适配器(2-3 周)⭐ 国内 60% PLC

**目标**:让 hmi.exe 能跟西门子 S7-200/300/400/1200/1500 全系列通讯。

**关键约束**:**1 S7Driver = 1 PLC**。Snap7 单 client 只能连 1 台 PLC,不要在 driver 内做连接池(增加复杂度),多 PLC 场景在 C.5 用多 driver 解决。

**任务清单**:

| # | 任务 | 文件 | 估时 |
|---|---|---|---|
| 2.1 | Snap7 库集成(DLL + .lib + 头文件) | `hmi/3rdparty/snap7/`(新目录) | 0.5 天 |
| 2.2 | 写 S7Driver 包装层(QLibrary 动态加载 + 类型转换) | `hmi/src/drivers/S7Driver.h/.cpp`(新) | 3 天 |
| 2.3 | S7 数据类型映射 + 字节序(大端 vs Modbus 大端对齐) | `S7Driver.cpp` | 1 天 |
| 2.4 | S7 连接管理(Cli_Connect / Cli_Disconnect) | `S7Driver.cpp` | 1 天 |
| 2.5 | S7 读写(Cli_ReadArea / Cli_WriteArea,4 种 area) | `S7Driver.cpp` | 2 天 |
| 2.6 | 错误处理 + 超时(Snap7 错误码 → IPollDriver::errorOccurred) | `S7Driver.cpp` | 1 天 |
| 2.7 | S7-PLCSIM 仿真器集成测试(下载安装 + 跑通) |  | 2 天 |
| 2.8 | tst_S7Driver 单元测试(用 mock Snap7) | `hmi/tests/tst_S7Driver.cpp`(新) | 2 天 |
| 2.9 | 文档 + 真实 PLC 验证(若有) | `docs/阶段C_S7验证.md` | 1 天 |

**关键 API**:
```cpp
// hmi/src/drivers/S7Driver.h
namespace qwhmi::drivers {

class S7Driver : public IPollDriver {
    Q_OBJECT
public:
    explicit S7Driver(QObject* parent = nullptr);
    ~S7Driver() override;
    
    bool connect(const QString& host, quint16 port) override;
    void disconnect() override;
    bool isConnected() const override;
    ReadResult read(RegisterArea area, int slaveId, int addr, int count) override;
    bool write(RegisterArea area, int slaveId, int addr,
               const QVector<quint16>& values) override;
    
private:
    // Snap7 是 C 库,用 void* 避免头文件污染
    void* client_ = nullptr;  // S7Client*
    bool loadSnap7Library();   // QLibrary 动态加载
    void unloadSnap7Library();
};

} // namespace qwhmi::drivers
```

**验收**:
- [ ] S7-PLCSIM 仿真器连上,读 MW0(模拟值寄存器)→ 变量刷新
- [ ] 写真值到 MW100 → PLC 端能看到
- [ ] 4 种 area 都支持(HoldingReg/InputReg/Coils/DiscreteInput)
- [ ] 断网模拟 → driver 发 disconnected 信号,PollScheduler 标记 in-flight 失败重试
- [ ] tst_S7Driver 至少 5 个用例(连接/读/写/断线/错误码)
- [ ] LGPL 合规:头文件 + .lib + DLL 分发,Snap7 源码不混入

**吃透要点**:
- **LGPL 动态链接合规**:链接的 .dll 不算"衍生作品",只要不修改 Snap7 源码 + 动态链接 + 用户能替换 dll,就合规。商业化前必须找律师审一遍
- **C 库集成模式**:Snap7 是 C 库(Qt 是 C++),用 `QLibrary::resolve()` 拿函数指针 + `void*` 包客户端句柄 + try/catch 兜底(防止 dll 缺失崩)
- **字节序差异**:Modbus 大端 / S7 大端 / 西门子 PLC 内部存储方式 — 跨协议数据转换是常见坑
- **1 driver = 1 PLC 原则**:为什么不做连接池?连接池要处理 "client 复用" + "故障隔离" + "超时回收",复杂度爆炸。多 driver 在 C.5 解决,故障天然隔离

**依赖**:Snap7 LGPL 库 + S7-PLCSIM 仿真器(西门子官网免费下载)

---

### C.3 EtherNet/IP 协议适配器(2-3 周)⭐ 北美主流

**目标**:让 hmi.exe 能跟罗克韦尔/Allen-Bradley ControlLogix / CompactLogix 通讯。

**关键约束**:**1 EipDriver = 1 PLC**(同 S7Driver 原则)。

**任务清单**:

| # | 任务 | 文件 | 估时 |
|---|---|---|---|
| 3.1 | libplctag 库集成(DLL + 头文件) | `hmi/3rdparty/libplctag/`(新目录) | 0.5 天 |
| 3.2 | 写 EipDriver 包装层 | `hmi/src/drivers/EipDriver.h/.cpp`(新) | 3 天 |
| 3.3 | EtherNet/IP 数据模型(CIP 标签 vs Modbus 寄存器) | `EipDriver.cpp` | 2 天 |
| 3.4 | Eip 连接管理(plc_tag_create / plc_tag_destroy) | `EipDriver.cpp` | 1 天 |
| 3.5 | Eip 读写(plc_tag_read / plc_tag_write) | `EipDriver.cpp` | 2 天 |
| 3.6 | Eip 异步模型适配到 IPollDriver 同步接口 | `EipDriver.cpp` | 1 天 |
| 3.7 | tst_EipDriver 单元测试 | `hmi/tests/tst_EipDriver.cpp`(新) | 2 天 |
| 3.8 | Logix Designer 仿真器集成测试 |  | 2 天 |
| 3.9 | 文档 | `docs/阶段C_EIP验证.md` | 1 天 |

**关键 API**(类似 S7Driver,但 libplctag 是 C++ 异步):
```cpp
class EipDriver : public IPollDriver {
    // libplctag 用 tag handle(plc_tag_t = int)+ 异步轮询
    // 需要把异步回调转同步等待(用 QEventLoop 等 reply)
};
```

**验收**:
- [ ] Logix Designer 仿真器连上,读 N7:0(整数文件)→ 变量刷新
- [ ] 写 N7:1 → PLC 端能看到
- [ ] 4 种 area 都支持(主要用 HoldingReg,其他 3 种 EIP 支持有限)
- [ ] tst_EipDriver 至少 5 个用例

**吃透要点**:
- **CIP 协议**:EtherNet/IP 底层是 CIP(Common Industrial Protocol),标签寻址完全不同于 Modbus 寄存器,需要重新建模
- **异步到同步转换**:libplctag 是异步 API(plc_tag_read 不阻塞),但 IPollDriver 是同步接口 — 需要用 QEventLoop 等 reply 或 thread 包装
- **多 vendor 兼容**:AB / 罗克韦尔 / Schneider 等都用 EtherNet/IP,但细节有差异

**依赖**:libplctag LGPL 库 + Logix Designer 仿真器(罗克韦尔,有 7 天试用限制)

---

### C.4 统一协议选择 UI(1 周)

**目标**:主窗口加"协议"下拉框,让用户选 Modbus / S7 / EtherNet/IP。

**关键 UX 规则(锁定)**:

1. **协议选择只在新工程或工程加载时生效** — 切换协议 = 新建 PollScheduler + 新 driver
2. **不允许跨协议切换工程** — Modbus 工程切到 S7 时,Variable 地址(0x0002)在 S7 无意义 → 弹错误"协议不兼容,请创建新工程"
3. **同协议切换** = 切 IP/Port/重建 PollScheduler,工程文件继续用
4. **AuditLog 记录**:`action=protocol.change detail=modbus→s7`(C.2/C.3 完成后)

**任务清单**:

| # | 任务 | 文件 | 估时 |
|---|---|---|---|
| 4.1 | hmi.ui 加 protocolComboBox + plcHostEdit + plcPortEdit(已存在,改 labels) | `hmi/hmi.ui`(改) | 0.5 天 |
| 4.2 | hmi.cpp 加 protocolComboBox 的 connect + DriverFactory 创建 driver | `hmi/hmi.cpp`(改) | 1 天 |
| 4.3 | PollScheduler 改成接受 driver 指针(协议切换时重建) | `hmi/hmi.cpp`(改) | 1 天 |
| 4.4 | 现有 ModbusMaster 替换为 ModbusDriver(接口一致) | `hmi/hmi.cpp`(改) | 1 天 |
| 4.5 | 协议切换 UX 校验(不允许跨协议 + AuditLog) | `hmi/hmi.cpp`(改) | 0.5 天 |
| 4.6 | 回归测试 + 真实跑通三种协议 |  | 1 天 |

**验收**:
- [ ] 启动后选 Modbus → 之前的 ModbusSlave 仿真器还能连
- [ ] 切到 S7 → S7-PLCSIM 能连(前提 C.2 完成)
- [ ] 切到 EtherNet/IP → Logix Designer 能连(前提 C.3 完成)
- [ ] **跨协议切工程时弹错误**(关键 UX 规则)
- [ ] 工程文件保存协议选择,重启后自动用回
- [ ] audit-*.log 有 `action=protocol.change`

**吃透要点**:
- **配置驱动**:UI 的下拉选择驱动 DriverFactory 创建不同 driver — 这是"工厂模式 + 策略模式"
- **协议无关业务层**:PollScheduler 不知道也不关心是哪个协议,它只调 IPollDriver 接口
- **协议 = 工程级配置,不是会话级** — 切换协议 = 重新打开工程,不能热切

---

### C.5 多 PLC 架构(2 周)

**目标**:同一工程连多台 PLC(主/从、数据聚合),PollScheduler 支持多 driver。

**任务清单**:

| # | 任务 | 文件 | 估时 |
|---|---|---|---|
| 5.1 | Variable 加 plcId 字段(属于哪台 PLC) | `hmi/Variable.h/.cpp`(改) | 0.5 天 |
| 5.2 | 工程文件 JSON 加 "plcs": [{"id": "plc1", "protocol": "s7", "host": "...", "port": ...}] | `hmi/Project.cpp`(改) | 1 天 |
| 5.3 | PollScheduler 支持多 driver(每个 driver 独立 tick) | `hmi/PollScheduler.h/.cpp`(改) | 3 天 |
| 5.4 | UI 加"PLC 列表"标签页(显示每台 PLC 状态:connected/disconnected/error) | `hmi/hmi.ui + hmi.cpp`(改) | 1 天 |
| 5.5 | 报警联动跨 PLC(PLC1 报警 → PLC2 写停止值) | `hmi/ActionManager.cpp`(改) | 1 天 |
| 5.6 | **联动失败处理**(PLC2 挂了 → 重试 + 报警 + 记录) | `hmi/ActionManager.cpp`(改) | 0.5 天 |
| 5.7 | 写冲突处理(同一 Variable 被多 driver 写) | `hmi/VariableManager.cpp`(改) | 1 天 |
| 5.8 | tst_MultiPlc 集成测试 | `hmi/tests/tst_MultiPlc.cpp`(新) | 2 天 |

**验收**:
- [ ] 工程配 2 台 PLC(1 台 Modbus + 1 台 S7)同时连,变量轮询正常
- [ ] 拔 1 台网线 → 那台 PLC 状态变 disconnected,其他照常
- [ ] 报警联动跨 PLC 工作(PLC1 温度高 → PLC2 速度写 0)
- [ ] **联动失败时**:PLC2 挂了,ActionManager 自动重试 3 次,失败后记录新报警 + audit
- [ ] tst_MultiPlc 至少 8 个用例(含联动失败场景)

**吃透要点**:
- **数据归属**:每条 Variable 必须明确属于某台 PLC,不能"全局变量"
- **故障隔离**:1 台 PLC 挂了不影响其他 PLC(单独的 driver + 单独的 PollScheduler tick)
- **跨 PLC 联动**:报警联动从"同 PLC 内"扩展到"跨 PLC",ActionManager 需要加 plcId 参数
- **联动失败处理**:联动目标 PLC 不可达 = 局部故障,但不能影响报警链路 — 必须有降级机制

---

### C.6 真机验证 + LGPL 合规

**重新拆为 C.6a + C.6b**(原 1 周估时太乐观)。

### C.6a 真机技术验证(2 周)

**目标**:在 3 种仿真器 + (可选)真 PLC 上跑通,确认商业可用。

**任务清单**:

| # | 任务 | 文件 | 估时 |
|---|---|---|---|
| 6a.1 | S7-PLCSIM 安装 + ModbusSlave + Logix Designer 仿真器三件套 |  | 0.5 天 |
| 6a.2 | 3 协议 × 4 角色(Admin/Engineer/Operator/Viewer)端到端测试 |  | 4 天 |
| 6a.3 | 24h 长跑稳定性测试(每个协议 24h 看是否掉) |  | 3 天 |
| 6a.4 | 写 `docs/阶段C_真机验证报告.md`(含截图 + 协议对比表 + 已知问题) | `docs/阶段C_真机验证报告.md`(新) | 1 天 |
| 6a.5 | 修验证发现的所有 bug |  | 2 天 |

**验收**:
- [ ] 3 种仿真器都能连、读、写、断线重连
- [ ] 24h 长跑不掉线
- [ ] 写一份完整验证报告,包含协议支持矩阵 + 已知问题清单
- [ ] 报告里所有"已知问题"都被修复或标注后续阶段

### C.6b LGPL 合规 + Demo(1 周)

**目标**:商用前的法律和文档准备。

**任务清单**:

| # | 任务 | 文件 | 估时 |
|---|---|---|---|
| 6b.1 | LGPL 合规清单(动态链接 / 用户可替换 dll / 不改源码 / 版权声明) | `docs/阶段C_LGPL合规.md`(新) | 1 天 |
| 6b.2 | **找律师过一遍 LGPL 边界**(商用前必修) | 外部律师 | 3 天 |
| 6b.3 | 录 `docs/阶段C_Demo.mp4`(T3 业余模式约定,5 分钟) |  | 1 天 |
| 6b.4 | 写 `docs/阶段C_从这里继续.md`(B 阶段产物模板) | `docs/阶段C_从这里继续.md`(新) | 1 天 |
| 6b.5 | 写 `docs/阶段C_回顾.md`(估时偏差 + 决策复盘) | `docs/阶段C_回顾.md`(新) | 1 天 |

**验收**:
- [ ] LGPL 合规清单有律师签字 / 咨询记录
- [ ] 录 5 分钟 Demo 视频(3 协议 + 多 PLC + 报警联动 + 权限)
- [ ] 写 `阶段C_从这里继续.md`(给两周后的自己)

---

## 5. 关键架构吃透要点

C 阶段会触及 hmi **所有 7 层**。Wqian 应该重点理解:

### 5.1 抽象层 / 接口隔离

```cpp
// 抽象层之前:PollScheduler 直接依赖 ModbusMaster
PollScheduler(VariableManager*, ModbusMaster*);  // 紧耦合

// 抽象层之后:PollScheduler 依赖接口
PollScheduler(VariableManager*, IPollDriver*);  // 松耦合
```

**学到什么**:加新协议不需要改 PollScheduler — 开闭原则实战。

### 5.2 三方库集成模式

```cpp
// LGPL 动态链接 + 函数指针解析
QLibrary snap7Lib("snap7");
using Cli_ConnectFunc = int (*)(void*, const char*, int, int);
Cli_ConnectFunc Cli_Connect = (Cli_ConnectFunc)snap7Lib.resolve("Cli_Connect");
```

**学到什么**:C 库怎么包装成 C++ 类 + 怎么保证 LGPL 合规。

### 5.3 异步 → 同步转换

```cpp
// libplctag 是异步 API
plc_tag_read(tag, 0);  // 不阻塞,需要 poll status

// IPollDriver 要求同步,转:
QEventLoop loop;
QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
connect(this, &EipDriver::readReady, &loop, &QEventLoop::quit);
loop.exec();  // 阻塞到 readReady 或超时
```

**学到什么**:同步接口包装异步 API 的标准模式。

### 5.4 多 PLC 数据归属

```cpp
// Variable 必须明确属于哪台 PLC
Variable{"Speed", addr=0x0002, plcId="plc1"}  // ← plc1 这台 PLC 的
Variable{"Temp",  addr=0x0001, plcId="plc2"}  // ← plc2 这台 PLC 的
```

**学到什么**:数据归属 + 故障隔离 + 跨 PLC 联动。

### 5.5 Schema 演进

```cpp
// 旧 v1:{"variables": [...]}
// 新 v2:{"schema_version": 2, "plcs": [...], "variables": [...]}

// loadFromFile 检测 schema_version:
//   v1 → 调 migrateV1ToV2(补 plcs/protocol/plcId 字段)
//   v2 → 直接读
//   不支持的高版本 → 报错但保留原文件
```

**学到什么**:配置文件 schema 怎么向后兼容 — 商业化必备。

---

## 6. 风险 + 缓解

| 风险 | 严重度 | 缓解 |
|---|---|---|
| **LGPL 商用合规** | 🟠 中 | 自用完全免费,商用前必须找律师审一次(路线图标的强制项,C.6b 强制项) |
| **ModbusDriver 行为不兼容** | 🟠 中 | **100% 兼容作为 C.1 验收硬指标**,现有 68 测试无改动通过 |
| **Snap7 / libplctag 接口变更** | 🟡 低 | 锁版本,固定用一份,升级时单独工单 |
| **S7-PLCSIM 仿真器与真 PLC 行为差异** | 🟡 低 | 文档说明仿真器限制;能借真 PLC 测更好 |
| **Logix Designer 7 天试用限制** | 🟡 低 | 用真 PLC(若 Wqian 有);或用开源的 CIP 仿真器 |
| **多 PLC 状态机复杂度爆炸** | 🟠 中 | C.5 拆小步,先支持 2 台,再加 3+ 台 |
| **协议栈 bug 调试耗时** | 🟠 中 | 单测 + Mock + 真实 PLC 三层验证;卡住时切下一个里程碑 |
| **工程文件不兼容老工程** | 🟠 中 | **C.0 schema 演进**做自动迁移(新加任务) |
| **跨协议切换工程时 Variable 地址失效** | 🟡 低 | **C.4 锁定 UX**:不允许跨协议,弹错误 |
| **LGPL 库更新不及时** | 🟢 低 | 锁版本号,Snap7 1.4.x 稳定 |
| **C 阶段总估时偏长(9-12 周 vs 路线图 6-8 周)** | 🟡 低 | 路线图低估了库调试 + 多 PLC + 合规,T3 业余节奏宁可估长 |

---

## 7. 验收清单(总)

### C.0 schema 演进
- [ ] 旧 default.json 无 protocol 字段 → 自动加 protocol=modbus
- [ ] tst_ProjectSchema 至少 3 个用例(v1→v2 / v2→v3 / 迁移失败)

### C.1 协议抽象层
- [ ] IPollDriver 接口清晰,通用 read(area, addr, count) 支持 4 种 area
- [ ] **ModbusDriver 100% 兼容 ModbusMaster,68 测试无改动全过**
- [ ] tst_IPollDriver 用 Mock driver 验证 PollScheduler 不依赖具体协议

### C.2 S7
- [ ] S7Driver 能连 S7-PLCSIM,读写 4 种 area
- [ ] 断线重连正常
- [ ] tst_S7Driver 5+ 用例

### C.3 EtherNet/IP
- [ ] EipDriver 能连 Logix Designer,读写 HoldingReg
- [ ] tst_EipDriver 5+ 用例

### C.4 统一 UI
- [ ] 主窗口下拉框选 3 协议都能切
- [ ] **跨协议切换工程时弹错误**(关键 UX 规则)
- [ ] 工程文件保存协议
- [ ] audit-*.log 有 `action=protocol.change`

### C.5 多 PLC
- [ ] 2 台不同协议 PLC 同时连
- [ ] 报警跨 PLC 联动
- [ ] **联动失败时降级**(重试 + 报警 + audit)
- [ ] tst_MultiPlc 8+ 用例(含联动失败场景)

### C.6a 真机技术验证
- [ ] 3 种仿真器都能连、读、写、断线重连
- [ ] 24h 长跑不掉线
- [ ] 完整验证报告(协议支持矩阵 + 已知问题)

### C.6b LGPL + Demo
- [ ] LGPL 合规清单 + 律师签字
- [ ] 录 5 分钟 Demo 视频
- [ ] 写 `阶段C_从这里继续.md`

---

## 8. 推荐开干顺序

```
C.0 (3-5 天) ⭐ 新加
   ↓
C.1 (1 周)    ⭐ 立刻能开干,纯代码
   ↓
C.2 (2-3 周)  ⭐ 国内 60% PLC
   ↓
C.3 (2-3 周)
   ↓
C.4 (1 周)
   ↓
C.5 (2 周)
   ↓
C.6a (2 周)   真机技术验证
   ↓
C.6b (1 周)   LGPL + Demo
```

**总 10-13 周**(从 9-12 周调整为 10-13 周,因为加了 C.0)。

### 8.1 什么时候停下来重新 plan

- **C.0 完成后**:停下来确认 schema 迁移能跑通(关键基础)
- **C.1 完成后**:确认 ModbusDriver 100% 兼容后,**直接开干 C.2**(协议抽象层不会反复改)
- **C.3 完成后**:停下来重新 plan,因为 C.4 / C.5 会暴露协议不一致问题
- **C.5 完成后**:停下来评估"是否真的需要 E 阶段 WYSIWYG"(可能发现 D 阶段够用)

---

## 9. 关联文档引用

- **战略决策**:`docs/路线图.md` 第 1 节(C 锁了 3 协议 + LGPL)
- **现有架构**:`docs/现状.md` 第 3 节(7 层架构)
- **B 阶段基础**:`docs/阶段B_从这里继续.md`(B5 audit + 4 角色权限会贯穿 C)
- **详细方案**:`docs/优化与改善计划.html` v0.2.0 阶段 C 节(锁的方案)
- **D / E 阶段 UI 规划**:`docs/UI规划调研与方案.md`(中期 D + 长期 E)
- **改善清单**:`docs/改善清单.md` C 阶段任务(批次 2 #5 等)

---

> **总结**:C 阶段不是"加 3 个协议",是 **重塑 hmi 的通讯架构**。做完之后,hmi 真正能商业化 — 60% 工业现场都覆盖。
>
> **第一步现在就开干**:`git checkout -b feature/c-protocol-abstraction`,开始 C.0 → C.1。
