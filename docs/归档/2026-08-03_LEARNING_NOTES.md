> ⚠️ **已归档 · 只读 · 不再维护**（归档于 2026-08-27）
> Codex(另一个 AI 助手)写的学习地图。进度停在「阶段 5.3 / 6 还没动工」。分层架构与两条数据流已并入 `docs/现状.md` 第三、四节。
>
> 当前状态看 [`docs/现状.md`](../现状.md) ｜ 接下来做什么看 [`docs/路线图.md`](../路线图.md)

---

# HMI — 学习笔记(Codex 助手维护)

> 本文件由 Codex (AI 编码助手) 在 2026-08-03 初读项目时一次性生成,后续随你看代码的过程增量补全。
> 目标:你 (Wqian) 能拿来当学习的"地图",哪里读过了、哪里是入口、哪里容易踩坑。

---

## 🎯 一、项目定位(一句话)

**这是 Wqian 的 Qt + C++ 工业 HMI 学习工程**,从零手写"上位机 ↔ PLC"协议层、变量层、UI 绑定层、业务层。
**协作伴侣**:子衿 (本地持久化 AI,负责代码节奏);**Codex** (本助手,负责辅助你学习)。

- **最终目标**:能形成自己的 HMI 框架 (而非简单调库),能跑通 Modbus/S7/OPC UA 多协议、24h 不掉、可改配置当模板复用。
- **工期档位**:2.5 ~ 3.5 个月 (从 2026-06-11 算起,已用约 6 周,剩余约 4-8 周)。
- **风格**:每阶段一个子练 → 编译过 → 写到 HMI日记/2026-MM-DD.md → "我会了" 才进下一阶段 (工业谨慎纪律)。

---

## 🧱 二、技术栈

| 维度 | 选型 |
|---|---|
| 语言 | C++17 |
| 框架 | Qt 6.8.3 (msvc2022_64, 路径 D:\Qt\6.8.3\msvc2022_64\) |
| 模块 | Core Gui Network Widgets SerialBus |
| 构建 | **双构建系统**: CMake 3.21 + VS hmi.vcxproj / hmi.slnx (Windows, MSVC v145) |
| IDE | VS 2022 / 2026, Qt VS Tools |
| 运行平台 | Windows (尚未跨平台, 但用 Qt 写的代码能跨) |
| 协议 | **手写** Modbus TCP (阶段 2 第 4 练起), 借鉴 Qt QModbusDataUnit / QModbusReply 接口 |
| 不要 | 不上 QStateMachine (业务状态机手写), 不引第三方 HMI 库 |

---

## 📅 三、阶段进度

主线  → 7,当前**收尾 4, 起步 5**。

| 阶段 | 状态 | 内容 | 关键文件 |
|---|---|---|---|
| 0 | ✅ | Qt 热身 (窗口 + 信号槽) | main.cpp / hmi.h |
| 1.1/1.2 | ✅ | TCP 客户端 (重连/心跳/超时) | TcpClient.{h,cpp} |
| 2 | ✅ | Modbus 0x03/04/05/06/0F (异常响应 + 重设计) | ModbusMaster.{h,cpp} |
| 3.1 | ✅ | Variable 数据模型 (类型/Quality/deadband/setRange) | Variable.{h,cpp} |
| 3.2 | ✅ | VariableManager + 订阅发布 UI 验证 | VariableManager.{h,cpp} + hmi.cpp |
| 3.3 | ✅ | PollScheduler (100ms tick + 连续地址合并) | PollScheduler.{h,cpp} |
| 4.1 | ✅ | Binding 框架 (3 重载 + 立即同步初值) | Binding.{h,cpp} |
| 4.2 | ✅ | bindEdit 反向写 (回车确认 / Esc 取消 / 脏边框) | Binding.cpp |
| 4.3.0 | ✅ | WriteScheduler (与 PollScheduler 对称) | WriteScheduler.{h,cpp} |
| 4.3.1 | ✅ | bindEdit 类型参数化 (Bool/Int/Float 全支持) | Binding.cpp (switch) |
| **5.1** | ✅ | **业务状态机 + Fault + 迁移表**(2026-08-03 拍板) | hmi.h/hmi.cpp |
| 5.3 | ⬜ | 报警系统 | 待新建 |
| 6 | ⬜ | 工程文件 JSON 存读 | 待新建 |
| 7 | ⬜ | 整合 demo (3 寄存器 + 三键 + 报警全跑) | 待新建 |

---

## 📁 四、目录速览


`	ext
E:\qianwie\hmi\
├── README.md                  (仓库总览:目标/路线/编译)
├── agents.md                  (你的 Codex 操作纪律,执行优先)
├── LEARNING_NOTES.md          (本文件,Codex 帮你整理)
├── LICENSE                    (MIT)
├── hmi\                       (Qt 工程根,VS 打开这一层)
│   ├── main.cpp               (程序入口,15 行,设 org/app 名)
│   ├── hmi.{h,cpp,ui}         (主窗口 = UI + 业务状态机)
│   ├── TcpClient.{h,cpp}      (阶段 1)
│   ├── ModbusDemo.{h,cpp}     (阶段 2 早期:用 QModbusTcpClient 演示,后续被 ModbusMaster 替代)
│   ├── ModbusMaster.{h,cpp}   (阶段 2 重设计,核心协议实现)
│   ├── Variable.{h,cpp}       (阶段 3.1)
│   ├── VariableManager.{h,cpp}(阶段 3.1)
│   ├── PollScheduler.{h,cpp}  (阶段 3.3,读)
│   ├── WriteScheduler.{h,cpp} (阶段 4.3.0,写)
│   ├── Binding.{h,cpp}        (阶段 4.1-4.3.1,变量↔UI)
│   ├── hmi.qrc                (Qt 资源,当前空)
│   ├── hmi.vcxproj(.filters)  (VS 工程)
│   ├── hmi.slnx               (VS 解决方案)
│   ├── CMakeLists.txt         (CMake 工程)
│   ├── 2026-07-06_ModbusMaster响应处理_知识点总结.md
│   ├── 2026-07-28_阶段3-4收尾+优化项_进度总结.md
│   ├── HMI_PLAN.md            (总计划,子衿 2026-07-25 写)
│   └── HMI_阶段5+计划_2026-07-28.md  (可选玩法清单)
├── HMI日记\                   (子练/阶段日记, 按日期)
├── HMI笔记\                   (个人备忘卡)
│   ├── HMI工程档案.md         (本工程总纲 66 KB + 知识卡片 + 文档索引)
│   └── 通用知识\              (★ 跨项目可复用:QHash/QByteArray 速查 + 工业界引用失效与安全分层)
└── x64\Debug\                 (构建产物,.gitignore 忽略)
`

---

## 🧩 五、模块分层

`mermaid
flowchart TB
    UI[hmi.ui + hmi.cpp<br/>业务状态机 + UI 同步]
    Binding[Binding<br/>变量↔UI 双向]
    Sched[Scheduler 层<br/>Poll + Write]
    Mgr[VariableManager<br/>name↔Variable 哈希]
    Var[Variable<br/>值/Quality/Timestamp/deadband]
    MB[ModbusMaster<br/>手写 Modbus TCP]
    TC[TcpClient<br/>QTcpSocket 封装<br/>重连/心跳/超时]
    PLC[PLC / ModbusSlave<br/>Modbus TCP 仿真]

    UI -->|setValue<br/>valueChanged| Binding
    Binding -->|valueChanged<br/>markDirty| Var
    Sched -->|Read/Write req| MB
    Sched -->|setValue<br/>clearDirty| Mgr
    Mgr -->|持有| Var
    MB -->|TCP 字节流| TC
    TC -->|TCP| PLC
`

---

## 🔄 六、核心数据流

### 6.1 读通路 (PLC → UI)

`mermaid
sequenceDiagram
    participant P as PLC / ModbusSlave
    participant T as TcpClient
    participant M as ModbusMaster
    participant PS as PollScheduler
    participant V as Variable
    participant L as QLabel (Binding)

    Note over PS: 100ms tick
    PS->>PS: 到期 + 非 in-flight 的 Variable 收集
    PS->>PS: 按地址连续区间合并成 1 次读请求
    PS->>M: sendReadRequest(unit, slaveId, requestId)
    M->>T: buildFrame(TID, FC, body) + send
    T->>P: MBAP 帧 (TCP)
    P-->>T: 响应帧
    T->>M: readyRead(bytes)
    M->>M: recvBuf_ 累积 + 切帧 + handleReadResponse
    M->>PS: emit readFinished(requestId, unit, success)
    PS->>V: setValue(读到的值, checkRange=false)
    V-->>L: valueChanged(Variable)
    L->>L: setProperty("text", formatted)
`

### 6.2 写通路 (UI → PLC,带审计)

`mermaid
sequenceDiagram
    participant U as QLineEdit
    participant B as Binding::bindEdit
    participant V as Variable
    participant WS as WriteScheduler
    participant M as ModbusMaster
    participant P as PLC

    Note over U: 用户输入 + 回车
    U->>B: textEdited → returnPressed
    B->>V: setValue(typedVal) [类型校验 + 范围]
    B->>V: markDirty()
    Note over WS: 100ms tick
    WS->>WS: 找 dirty + ReadWrite + 非 in-flight
    WS->>M: sendWriteRequest(unit, slaveId, reqId)
    M->>P: MBAP 帧 (FC=05/06/10)
    P-->>M: 应答 (或异常)
    M->>WS: emit writeFinished(reqId, success)
    alt 成功
        WS->>V: clearDirty + clearWriteFail
    else 失败 (累计)
        WS->>WS: 失败计数 +1
        Note over WS: 累计 ≥3 次: clearDirty + setQuality(Bad) + emit writeFailed
    end
    WS->>WS: 追加 WriteAuditEntry + emit auditAppended
    Note over U: UI 接 auditAppended 加一行 (auditTable)
`

### 6.3 重连通路 (TcpClient 状态机)

`
用户按"连接" → connectTo(host,port)
  ├─ socket 立刻 connectionAttempted → hmi connState=Connecting
  ├─ 3s 内 onSocketConnected → connState=Connected, startHeartbeat
  ├─ 3s 内未连上 → abort + errorOccurred + fallback 重连
  ├─ 已连后心跳 timer (5s): 15s 无活动 → abort 触发 onSocketError
  └─ onSocketError: scheduleReconnect() (指数退避 1/2/4/8/16/30s 上限,重试 5 次)
       └─ 达到上限 → reconnectAbandoned → hmi connState=Failed
`

---

## 🎚️ 七、业务状态机(阶段 5.1,2026-08-03 拍板已实装)

`cpp
// hmi.h
enum class State { Stopped, Running, Resetting, Fault };
enum class Trigger { StartButton, StopButton, ResetButton,
                     ResetTimerFinished, AlarmCritical, AlarmCleared };
static const QHash<QPair<State,Trigger>, State> kTransitionTable;
bool tryTransition(Trigger t);   // 单一入口,查表迁移
`

迁移规则 (核心条目,完整见 hmi.cpp 末尾 kTransitionTable 数组):

| 从态 | 触发器 | 到态 | 业务含义 |
|---|---|---|---|
| Stopped | StartButton | Running | 启动 |
| Stopped | ResetButton | Resetting | (3 秒 reset 计时开始) |
| Running | StopButton | Stopped | 停止 |
| Running | ResetButton | Resetting | (运行中按复位) |
| Resetting | ResetButton | Resetting | 重置中按无变化 |
| Resetting | ResetTimerFinished | Stopped | 3 秒到点 |
| **任何** | AlarmCritical | **Fault** | 严重报警抢占 |
| Fault | ResetButton | Resetting | 只能靠复位清 Fault |
| Fault | ResetTimerFinished | Stopped | 严重故障不等定时器 (Wqian 拍板) |

**WHY 拍这个设计**:之前三个槽函数直接 state = State::xxx,任何态都能转任何态。改后"所有合法迁移只能通过 	ryTransition 查表走",杜绝重复状态设置 + 误迁移。

**未实现** (待 5.3):AlarmCritical / AlarmCleared 两个 trigger 入口已留,但目前没人 emit,等报警系统接。

---

## 📐 八、关键设计原则沉淀

1. **手写不调库**:Modbus 协议层完全自己实现, 只借 QModbusDataUnit 接口对齐 Qt 官方便于以后查文档。
2. **协议与 UI 解耦**:ModbusMaster 不操作 UI, 通过 errorOccurred / idle / eadFinished / writeFinished 信号。
3. **状态机 = enum class + 查表**:业务层不用 QStateMachine, 手写 enum + Trigger 表 + tryTransition, 单一迁移入口。
4. **死区过滤不是多余的**:Float 必须配 deadband 防精度抖动;Bool/计数/状态码默认 0 不启用。
5. **PLC 读路径不查范围**:setValue(v, checkRange=false) 给 PollScheduler 用, 原始寄存器值可能超出工程值范围。
6. **UI 写路径查范围**:用户输入配范围 (-1000~1000 等) 避免误输, 失败回滚 + qDebug。
7. **失败重试 + 上限报警**:writeFailCount 累计达 3 次就 clearDirty + setQuality(Bad) + emit writeFailed (工业铁律: 不无限重发刷屏)。
8. **连接层防"UI 闪红"**:重连过程中 UI 显示橙 (Reconnecting) 而非红 (Failed), 只有真正 reconnectAbandoned 才标 Failed。
9. **注释模板** (Wqian 长期偏好): [背景] [Bug 现象] [修法] [参照] [取舍] 五段式。
10. **magic number 必提常量**:static constexpr int kXxxMs = ..., class 内 private 静态, Qt 习惯。

---

## 🐞 九、已知 Bug 修复历史(共 8 个,选重点)

| # | 现象 | 根因 | 修法 | 文件 |
|---|---|---|---|---|
| 1 | timer->start(1000) 启动时机不对 | 写死在外面 | 挪到 updateUi 的 Running 分支 | hmi.cpp |
| 5 | buffer_ append 不清导致死循环 | 字段保留未启用 | TcpClient 阶段先 readAll 一次一段发 | TcpClient.cpp |
| 9 | oid disconnect() 遮蔽 QObject 静态 | 同名 | 改 close() | TcpClient.h |
| 10 | enum stateReset 跟 Qt 内部信号名混淆 | 命名 | 改 Resetting | hmi.h |
| 12 | close 调 disconnectFromHost 在 Connecting 下不可靠 | Qt 状态分歧 | 按 socket state 三路处理 | TcpClient.cpp |
| 13 | 3s 超时 abort 后 disconnected 不发, UI 卡 "连接中" | Qt 信号竞态 | 50ms fallback 检查 + 手动 scheduleReconnect | TcpClient.cpp |
| **A1** | Modbus 跑几小时一直弹"Modbus 错误 TID=65543" | quint32 截断到 quint16 (buildFrame 形参) | TID 全改回 quint16, 跟 MBAP 协议对齐 | ModbusMaster.{h,cpp} |
| - | 重连后某些 Variable 永久不读 | 
extPollAtMs[v]=-1 卡 in-flight 残留 | 加 esetAll(), 重连瞬间清 | PollScheduler.{h,cpp} + hmi.cpp |
| - | 写线圈 0x05 用 0/1 而非 0x0000/0xFF00 | 协议不规范 | 按协议规范 | WriteScheduler.cpp |
| - | "Modbus 错误 / 上一帧未完成" 偶尔弹窗 | PollScheduler 没做 in-flight 防御 | 100ms tick 顶部加 pendingCount 检查 | PollScheduler.cpp |

---

## 🚀 十、编译与运行

**双构建系统**(需同步维护 — 已知 CMakeLists.txt 缺 WriteScheduler):

`ash
# CMake 优先(推荐)
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug

# VS 直接 F5 — 双击 hmi\hmi.slnx, 选 x64/Debug
`

**VC 环境注意 (孟德 2026-07-27 总结进 HMI_PLAN.md)**:
- VS 2026 v18 的 cvars64.bat 默认漏 ucrt INCLUDE, 改 hmi.vcxproj.user 追加:
  `xml
  <IncludePath>;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt</IncludePath>
  <LibraryPath>;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64</LibraryPath>
  `
- 命令行构建:
  `cmd
  "D:\...\VC\Auxiliary\Build\vcvars64.bat"
  "D:\...\MSBuild\Current\Bin\MSBuild.exe" hmi.slnx /t:Build /p:Configuration=Debug /p:Platform=x64 /m
  `

**搭配 ModbusSlave 仿真**: ModbusSlave 默认 127.0.0.1:5000, UI 上 IP/端口可改。

---

## 📚 十一、推荐学习路径

按你目标"理解 HMI 底层 + 形成自己的框架"分 **3 条路**。

### 路 A:从最底层开始(协议工程师视角)
1. TcpClient.{h,cpp} — 先吃透 TCP 状态机 (重连 + 心跳 + 超时)
2. 2026-07-06_ModbusMaster响应处理_知识点总结.md — MBAP 头 + 大端字节序 + quint8 强转命门
3. ModbusMaster.{h,cpp} 前 200 行 — connectTo / typeToReadFc / typeToWriteFc / buildFrame
4. ModbusMaster.cpp 后半 (~320 行未读) — handleReadResponse / handleWriteResponse / onTcpReadyRead 拆帧逻辑

### 路 B:从应用层往下挖(上层开发者视角)
1. hmi.cpp 构造函数 — 看 Variable 创建 + Binding::bind + PollScheduler/WriteScheduler 启动
2. Binding.{h,cpp} — 看 3 重载 ind + indEdit 的双向同步
3. Variable.{h,cpp} — 看 setValue 三态返值 (Accepted/NoChange/Rejected) + deadband
4. PollScheduler.cpp onTick — 看 100ms tick + 连续地址合并 + in-flight 防御
5. WriteScheduler.cpp onTick — 看 FC 按类型选 (0x05/06/10) + 失败 retry + 审计日志

### 路 C:从业务层框架理解(架构师视角)
1. hmi.h — 看完整的状态机 + 迁移表 + magic 常量集中
2. hmi.cpp 末尾 kTransitionTable 数组 — 单一迁移入口设计
3. HMI_PLAN.md 第 7 节 — 16 条框架能力目标 + 38 条支线优化清单
4. 2026-07-28_阶段3-4收尾+优化项_进度总结.md — 9 小时长会话的回看节奏 (子衿心得)

---

## ❓ 十二、待补读清单(标 [未读], 想聊可以让我展开)

> 这些是我没读完的 (总轮数 ≤ 3 / 单文件 ≤ 200 行 的纪律),之后你让我看时再展开。

- **半读**:
  - ModbusMaster.cpp 后 320 行 (响应拆帧 + 异常响应 + 超时清理的核心实现)
  - hmi.cpp 后 200 行 (业务三键槽 + updateUi 5 个 helper + parseValueList 实现)
- **未碰**:
  - 13 个 HMI日记 (HMI日记\2026-MM-DD.md):每个 1-8k 字, 子衿每次会话写的踩坑与决策
  - HMI笔记\HMI工程档案.md (66 KB, 个人化备忘库 + 文档总索引)
  - HMI笔记\通用知识\QHash速查.md / QByteArray速查.md / 工业界引用失效与安全分层速查.md (跨项目可复用)
- **核查要点** (你说的时候我读):
  - CMakeLists.txt 与 cxproj.filters 同步性 — 我看到 CMakeLists.txt 漏 Binding.cpp / WriteScheduler.cpp, 需要补
  - 阶段 5.3 报警类 / 阶段 6 工程文件 JSON, 还没动工
  - 8 条 IMPROVE-A* (modbus/Variable) 是否在最新代码里全体现 (有些可能在 .h 注释里写但没改实现)

---

## 📌 十三、协作约定(我接下来对你的默认行为)

1. **不重读**:你的工程已熟, 我不会再周期性重扫仓库。 你点哪个文件我才读。
2. **先看注释**:每个文件头都有"阶段 X 第 Y 练"标注, 我会优先用它判断"这是哪一阶段、意图是什么"。
3. **注释风格**:建议沿用 [背景] [Bug 现象] [修法] [参照] [取舍] 五段式 — 你和子衿已习惯这风格。
4. **commit 前提醒**:你计划 commit 时,告诉我改了什么 (建议用 phase5.1: add Fault state + transition table 这种格式), 我帮你写 commit message。
5. **调试模式**:贴 qDebug 输出或 modbus 抓包截图给我即可, 我从信号链 / 迁移表 / setValue 返值三处找。
6. **新模块上马前**:先告诉我"打算做什么 / 复用现有谁", 我先吃上下文再写, 不上来就动代码。

---

_整理人: Codex / 时间: 2026-08-03 (项目当前进度: 阶段 5.1 已拍板实装, 阶段 5.3 报警启动前)_
