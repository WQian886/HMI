# 阶段 C · 详细规划

> 适用:**阶段 B(用户/角色/权限 + 操作审计)已收官**,准备开干 C(多协议 + 多 PLC)。
> 维护人:Wqian + AI 助手  ｜  创建:2026-09-17(B5 收尾 + B 阶段通过后)
> 配套:`docs/现状.md` / `docs/路线图.md` / `docs/优化与改善计划.html`(v0.2.0)/ `docs/阶段B_从这里继续.md`

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
配置(Project/JsonSerializer) ← 工程文件存协议类型 + IP + port
```

C 阶段会让 Wqian 真正理解 **hmi 是怎么把"PLC 数据 → 内存变量 → UI 显示"这条管道做出来的** — 这是 hmi 的灵魂。

---

## 2. 范围 / 非范围

### 包含

- **C.1** 协议抽象层(IPollDriver 接口)
- **C.2** S7 协议适配器(Snap7 LGPL 动态链接)
- **C.3** EtherNet/IP 协议适配器(libplctag LGPL 动态链接)
- **C.4** 统一协议选择 UI(主窗口下拉框)
- **C.5** 多 PLC 架构(同一工程连多台 PLC)
- **C.6** 真机验证(S7-PLCSIM + Logix Designer 仿真)

### 不包含

- 协议栈自己实现(用现成 LGPL 库:Snap7/libplctag)
- OPC UA / REST API(阶段 G)
- 历史曲线 / 报表(阶段 D)
- 画面编辑器(阶段 E)
- 加密通讯(S7Comm-Plus / CIP Security)— 太复杂,后续可选

---

## 3. 6 个里程碑详细分解

### C.1 协议抽象层(1 周)⭐ 地基

**目标**:抽出统一接口,让 3 个协议都能塞,业务层不用知道协议细节。

**任务清单**:

| # | 任务 | 文件 | 估时 |
|---|---|---|---|
| 1.1 | 设计 IPollDriver 抽象接口 | `hmi/src/drivers/IPollDriver.h`(新) | 0.5 天 |
| 1.2 | 把 ModbusMaster 包装成 ModbusDriver : IPollDriver | `hmi/src/drivers/ModbusDriver.h/.cpp`(新) | 1 天 |
| 1.3 | PollScheduler 改用 IPollDriver* 而非 ModbusMaster* | `hmi/PollScheduler.h/.cpp`(改) | 1 天 |
| 1.4 | DriverFactory:根据字符串("modbus"/"s7"/"eip")创建对应 driver | `hmi/src/drivers/DriverFactory.h/.cpp`(新) | 0.5 天 |
| 1.5 | tst_IPollDriver:接口契约单测(Mock driver 验 PollScheduler) | `hmi/tests/tst_IPollDriver.cpp`(新) | 1 天 |
| 1.6 | 回归测试 + cmake + vcxproj 同步 |  | 0.5 天 |

**关键 API**:
```cpp
// hmi/src/drivers/IPollDriver.h
class IPollDriver : public QObject {
    Q_OBJECT
public:
    virtual ~IPollDriver() = default;
    virtual bool connect(const QString& host, quint16 port) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    
    // 协议无关的统一读(返回寄存器值数组 + 成功标志)
    struct ReadResult {
        bool success;
        QVector<quint16> registers;
        QString errorMsg;
    };
    virtual ReadResult readHoldingRegs(int slaveId, int addr, int count) = 0;
    
    // 协议无关的统一写
    virtual bool writeHoldingRegs(int slaveId, int addr, const QVector<quint16>& values) = 0;
    
signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString& msg);
};

// hmi/src/drivers/ModbusDriver.h
class ModbusDriver : public IPollDriver {
    // 内部包一个 ModbusMaster(不破坏现有 Modbus 实现)
    ModbusMaster* inner_;
    // ...
};
```

**验收**:
- [ ] PollScheduler 用 `IPollDriver*` 接口指针能正常工作
- [ ] ModbusDriver 跑通,所有现有功能不变
- [ ] tst_IPollDriver 用 mock driver 验证 PollScheduler 不依赖具体协议
- [ ] cmake + vcxproj 双 build 通过
- [ ] 现有 68 个测试全过

**吃透要点**:
- **接口隔离原则(IPI)**:业务层只依赖接口,不依赖实现 — 这是软件架构的"开闭原则"
- **为什么需要抽象层**:直接用 `ModbusMaster*` 的话,加 S7 就要改 PollScheduler;用 `IPollDriver*` 改 1 行就行
- **适配器模式(Adapter)**:ModbusDriver 是 ModbusMaster 的适配器,把它"老"接口包成"新"接口

---

### C.2 S7 协议适配器(2-3 周)⭐ 国内 60% PLC

**目标**:让 hmi.exe 能跟西门子 S7-200/300/400/1200/1500 全系列通讯。

**任务清单**:

| # | 任务 | 文件 | 估时 |
|---|---|---|---|
| 2.1 | Snap7 库集成(DLL + .lib + 头文件) | `hmi/3rdparty/snap7/`(新目录) | 0.5 天 |
| 2.2 | 写 S7Driver 包装层(QLibrary 动态加载 + 类型转换) | `hmi/src/drivers/S7Driver.h/.cpp`(新) | 3 天 |
| 2.3 | S7 数据类型映射(S7 字节序 vs Modbus 大端) | `S7Driver.cpp` | 1 天 |
| 2.4 | S7 连接管理(Cli_Connect / Cli_Disconnect) | `S7Driver.cpp` | 1 天 |
| 2.5 | S7 读写寄存器(Cli_ReadArea / Cli_WriteArea) | `S7Driver.cpp` | 2 天 |
| 2.6 | 错误处理 + 超时(Snap7 错误码 → IPollDriver::errorOccurred) | `S7Driver.cpp` | 1 天 |
| 2.7 | S7-PLCSIM 仿真器集成测试(下载安装 + 跑通) |  | 2 天 |
| 2.8 | tst_S7Driver 单元测试(用 mock Snap7) | `hmi/tests/tst_S7Driver.cpp`(新) | 2 天 |
| 2.9 | 文档 + 真实 PLC 验证(若有) | `docs/阶段C_S7验证.md` | 1 天 |

**关键 API**:
```cpp
// hmi/src/drivers/S7Driver.h
class S7Driver : public IPollDriver {
    Q_OBJECT
public:
    explicit S7Driver(QObject* parent = nullptr);
    ~S7Driver() override;
    
    bool connect(const QString& host, quint16 port) override;
    void disconnect() override;
    bool isConnected() const override;
    ReadResult readHoldingRegs(int slaveId, int addr, int count) override;
    bool writeHoldingRegs(int slaveId, int addr, const QVector<quint16>& values) override;
    
private:
    // Snap7 是 C 库,用 void* 避免头文件污染
    void* client_ = nullptr;  // S7Client*
    bool loadSnap7Library();   // QLibrary 动态加载
    void unloadSnap7Library();
};
```

**验收**:
- [ ] S7-PLCSIM 仿真器连上,读 MW0(模拟值寄存器)→ 变量刷新
- [ ] 写真值到 MW100 → PLC 端能看到
- [ ] 断网模拟 → driver 发 disconnected 信号,PollScheduler 标记 in-flight 失败重试
- [ ] tst_S7Driver 至少 5 个用例(连接/读/写/断线/错误码)
- [ ] LGPL 合规:头文件 + .lib + DLL 分发,Snap7 源码不混入

**吃透要点**:
- **LGPL 动态链接合规**:链接的 .dll 不算"衍生作品",只要不修改 Snap7 源码 + 动态链接 + 用户能替换 dll,就合规。商业化前必须找律师审一遍
- **C 库集成模式**:Snap7 是 C 库(Qt 是 C++),用 `QLibrary::resolve()` 拿函数指针 + `void*` 包客户端句柄 + try/catch 兜底(防止 dll 缺失崩)
- **字节序差异**:Modbus 大端 / S7 大端 / 西门子 PLC 内部存储方式 — 跨协议数据转换是常见坑

**依赖**:Snap7 LGPL 库 + S7-PLCSIM 仿真器(西门子官网免费下载)

---

### C.3 EtherNet/IP 协议适配器(2-3 周)⭐ 北美主流

**目标**:让 hmi.exe 能跟罗克韦尔/Allen-Bradley ControlLogix / CompactLogix 通讯。

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
- [ ] tst_EipDriver 至少 5 个用例

**吃透要点**:
- **CIP 协议**:EtherNet/IP 底层是 CIP(Common Industrial Protocol),标签寻址完全不同于 Modbus 寄存器,需要重新建模
- **异步到同步转换**:libplctag 是异步 API(plc_tag_read 不阻塞),但 IPollDriver 是同步接口 — 需要用 QEventLoop 等 reply 或 thread 包装
- **多 vendor 兼容**:AB / 罗克韦尔 / Schneider 等都用 EtherNet/IP,但细节有差异

**依赖**:libplctag LGPL 库 + Logix Designer 仿真器(罗克韦尔,有 7 天试用限制)

---

### C.4 统一协议选择 UI(1 周)

**目标**:主窗口加"协议"下拉框,让用户选 Modbus / S7 / EtherNet/IP。

**任务清单**:

| # | 任务 | 文件 | 估时 |
|---|---|---|---|
| 4.1 | hmi.ui 加 protocolComboBox + plcHostEdit + plcPortEdit(已存在,改 labels) | `hmi/hmi.ui`(改) | 0.5 天 |
| 4.2 | hmi.cpp 加 protocolComboBox 的 connect + DriverFactory 创建 driver | `hmi/hmi.cpp`(改) | 1 天 |
| 4.3 | PollScheduler 改成接受 driver 指针(协议切换时重建) | `hmi/hmi.cpp`(改) | 1 天 |
| 4.4 | 现有 ModbusMaster 替换为 ModbusDriver(接口一致) | `hmi/hmi.cpp`(改) | 1 天 |
| 4.5 | 工程文件 JSON 加 "protocol": "modbus"/"s7"/"eip" 字段 | `hmi/Project.cpp`(改) | 0.5 天 |
| 4.6 | 回归测试 + 真实跑通三种协议 |  | 1 天 |

**验收**:
- [ ] 启动后选 Modbus → 之前的 ModbusSlave 仿真器还能连
- [ ] 切到 S7 → S7-PLCSIM 能连
- [ ] 切到 EtherNet/IP → Logix Designer 能连
- [ ] 工程文件保存协议选择,重启后自动用回

**吃透要点**:
- **配置驱动**:UI 的下拉选择驱动 DriverFactory 创建不同 driver — 这是"工厂模式 + 策略模式"
- **协议无关业务层**:PollScheduler 不知道也不关心是哪个协议,它只调 IPollDriver 接口

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
| 5.5 | 报警联动跨 PLC 设计(如果 PLC1 报警 → PLC2 写停止值) | `hmi/ActionManager.cpp`(改) | 1 天 |
| 5.6 | 写冲突处理(同一 Variable 被多 driver 写) | `hmi/VariableManager.cpp`(改) | 1 天 |
| 5.7 | tst_MultiPlc 集成测试 | `hmi/tests/tst_MultiPlc.cpp`(新) | 2 天 |

**验收**:
- [ ] 工程配 2 台 PLC(1 台 Modbus + 1 台 S7)同时连,变量轮询正常
- [ ] 拔 1 台网线 → 那台 PLC 状态变 disconnected,其他照常
- [ ] 报警联动跨 PLC 工作(PLC1 温度高 → PLC2 速度写 0)
- [ ] tst_MultiPlc 至少 8 个用例

**吃透要点**:
- **数据归属**:每条 Variable 必须明确属于某台 PLC,不能"全局变量"
- **故障隔离**:1 台 PLC 挂了不影响其他 PLC(单独的 driver + 单独的 PollScheduler tick)
- **跨 PLC 联动**:报警联动从"同 PLC 内"扩展到"跨 PLC",ActionManager 需要加 plcId 参数

---

### C.6 真机验证(1 周)

**目标**:在 3 种仿真器 + (可选)真 PLC 上跑通,确认商业可用。

**任务清单**:

| # | 任务 | 文件 | 估时 |
|---|---|---|---|
| 6.1 | S7-PLCSIM 安装 + ModbusSlave + Logix Designer 仿真器三件套 |  | 0.5 天 |
| 6.2 | 3 协议 × 4 角色(Admin/Engineer/Operator/Viewer)端到端测试 |  | 2 天 |
| 6.3 | 写 `docs/阶段C_真机验证报告.md`(含截图 + 协议对比表) | `docs/阶段C_真机验证报告.md`(新) | 1 天 |
| 6.4 | LGPL 合规检查清单 + 律师咨询(商用前必须) | `docs/阶段C_LGPL合规.md`(新) | 1 天 |
| 6.5 | 录 `docs/阶段C_Demo.mp4`(T3 业余模式约定) |  | 0.5 天 |

**验收**:
- [ ] 3 种仿真器都能连、读、写、断线重连
- [ ] 写一份完整验证报告,包含协议支持矩阵
- [ ] LGPL 合规清单(律师签字 — 商用前)
- [ ] 录 5 分钟 Demo 视频

**吃透要点**:
- **协议支持矩阵**:哪些 PLC 型号支持 / 不支持 / 部分支持(必须明确告知客户)
- **LGPL 边界**:商用时必须动态链接 + 提供 dll + 不能改 Snap7/libplctag 源码

---

## 4. 关键架构吃透要点

C 阶段会触及 hmi **所有 7 层**。Wqian 应该重点理解:

### 4.1 抽象层 / 接口隔离

```cpp
// 抽象层之前:PollScheduler 直接依赖 ModbusMaster
PollScheduler(VariableManager*, ModbusMaster*);  // 紧耦合

// 抽象层之后:PollScheduler 依赖接口
PollScheduler(VariableManager*, IPollDriver*);  // 松耦合
```

**学到什么**:加新协议不需要改 PollScheduler — 开闭原则实战。

### 4.2 三方库集成模式

```cpp
// LGPL 动态链接 + 函数指针解析
QLibrary snap7Lib("snap7");
using Cli_ConnectFunc = int (*)(void*, const char*, int, int);
Cli_ConnectFunc Cli_Connect = (Cli_ConnectFunc)snap7Lib.resolve("Cli_Connect");
```

**学到什么**:C 库怎么包装成 C++ 类 + 怎么保证 LGPL 合规。

### 4.3 异步 → 同步转换

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

### 4.4 多 PLC 数据归属

```cpp
// Variable 必须明确属于哪台 PLC
Variable{"Speed", addr=0x0002, plcId="plc1"}  // ← plc1 这台 PLC 的
Variable{"Temp",  addr=0x0001, plcId="plc2"}  // ← plc2 这台 PLC 的
```

**学到什么**:数据归属 + 故障隔离。

---

## 5. 风险 + 缓解

| 风险 | 严重度 | 缓解 |
|---|---|---|
| **LGPL 商用合规** | 🟠 中 | 自用完全免费,商用前找律师审一次(路线图标的强制项) |
| **Snap7 / libplctag 接口变更** | 🟡 低 | 锁版本,固定用一份,升级时单独工单 |
| **S7-PLCSIM 仿真器与真 PLC 行为差异** | 🟡 低 | 文档说明仿真器限制;能借真 PLC 测更好 |
| **Logix Designer 7 天试用限制** | 🟡 低 | 用真 PLC(若 Wqian 有);或用开源的 CIP 仿真器 |
| **多 PLC 状态机复杂度爆炸** | 🟠 中 | C.5 拆小步,先支持 2 台,再加 3+ 台 |
| **协议栈 bug 调试耗时** | 🟠 中 | 单测 + Mock + 真实 PLC 三层验证;卡住时切下一个里程碑 |
| **LGPL 库更新不及时** | 🟢 低 | 锁版本号,Snap7 1.4.x 稳定 |

---

## 6. 验收清单(总)

### C.1 协议抽象层
- [ ] IPollDriver 接口清晰,3 个方法 + 3 个信号
- [ ] ModbusDriver 包装现有 ModbusMaster,所有 68 测试还过
- [ ] tst_IPollDriver 用 Mock driver 验证 PollScheduler 不依赖具体协议

### C.2 S7
- [ ] S7Driver 能连 S7-PLCSIM,读写 MW 寄存器
- [ ] 断线重连正常
- [ ] tst_S7Driver 5+ 用例

### C.3 EtherNet/IP
- [ ] EipDriver 能连 Logix Designer,读写 N7 文件
- [ ] tst_EipDriver 5+ 用例

### C.4 统一 UI
- [ ] 主窗口下拉框选 3 协议都能切
- [ ] 工程文件保存协议

### C.5 多 PLC
- [ ] 2 台不同协议 PLC 同时连
- [ ] 报警跨 PLC 联动
- [ ] tst_MultiPlc 8+ 用例

### C.6 真机验证
- [ ] 3 协议真机跑通(仿真器 + Wqian 的真 PLC)
- [ ] 验证报告 + LGPL 合规清单 + 5 分钟 Demo

---

## 7. 推荐开干顺序

```
C.1 (1 周,纯代码)  ⭐ 立刻能开干
   ↓
C.2 (2-3 周,需 Snap7 + S7-PLCSIM)  ⭐ 国内 60% PLC
   ↓
C.3 (2-3 周,需 libplctag + Logix)
   ↓
C.4 (1 周,接 C.1)
   ↓
C.5 (2 周,多 PLC)
   ↓
C.6 (1 周,真机验证 + Demo)
```

**总 9-12 周**(路线图说 6-8 周,实际偏长 — 因为 LGPL 库调试 + 多 PLC 复杂度会被低估)。

**第一步现在就开干**:`git checkout -b feature/c-protocol-abstraction`,开始 C.1。

---

## 8. 关联文档引用

- **战略决策**:`docs/路线图.md` 第 1 节(C 锁了 3 协议 + LGPL)
- **现有架构**:`docs/现状.md` 第 3 节(7 层架构)
- **B 阶段基础**:`docs/阶段B_从这里继续.md`(B5 audit + 4 角色权限会贯穿 C)
- **详细方案**:`docs/优化与改善计划.html` v0.2.0 阶段 C 节(锁的方案)
- **改善清单**:`docs/改善清单.md` C 阶段任务(批次 2 #5 等)

---

> **总结**:C 阶段不是"加 3 个协议",是 **重塑 hmi 的通讯架构**。做完之后,hmi 真正能商业化 — 60% 工业现场都覆盖。
>
> **第一步**:开干 C.1 协议抽象层(1 周纯代码,不需要任何外部库)。
