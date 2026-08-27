# HMI — Qt 工业人机界面(从零手写)

> 个人学习项目:用 Qt 6 / C++17 从零实现工业 HMI(Human-Machine Interface)。
> 不调现成控件库,**手写通信层、协议层、变量层、绑定层、业务层**,目标是理解工业 HMI 的底层原理。

已实现:Modbus TCP 通信 · PLC 寄存器轮询 · 变量订阅发布 · 控件双向绑定 ·
阈值报警(4 档 + 确认/恢复)· 报警联动动作 · 工程配置 JSON 存读

---

## 📍 我想…… 就看……

> **这张表是全部文档的唯一入口。找不到东西时回到这里。**

| 我想…… | 看这个 |
|---|---|
| 知道**项目现在做到哪了** | **[`docs/现状.md`](docs/现状.md)** ← 唯一权威 |
| 知道**接下来要做什么** | **[`docs/路线图.md`](docs/路线图.md)** ← 唯一权威 |
| 知道**有哪些代码待修** | [`docs/改善清单.md`](docs/改善清单.md)(30 条 / 255xp) |
| **编译不过 / 环境配置** | [`docs/现状.md` 第六节](docs/现状.md)(含 VS2026 的 ucrt 坑) |
| 搞懂**某个模块怎么运作的** | [`docs/专题/`](docs/专题/) |
| 查**某个 Qt 类怎么用** | [`docs/HMI工程档案.md`](docs/HMI工程档案.md) 的知识卡片索引 |
| 查**换项目也能用的通用知识** | [`docs/通用知识/`](docs/通用知识/) |
| 回顾**当时是怎么想的 / 踩了什么坑** | [`docs/日记/`](docs/日记/) |
| 找**已经过期的旧文档** | [`docs/归档/`](docs/归档/)(只读) |

**三份权威文档的分工**:`现状.md` = 现在什么样 ｜ `路线图.md` = 接下来做什么 ｜ `改善清单.md` = 哪里要修

---

## 🎯 项目目标

- 掌握 Qt 核心机制(信号槽、元对象系统、事件循环、控件与布局)
- 手写工业协议栈,理解帧结构而非调库
- 打通「上位机 ↔ PLC」完整链路
- 沉淀一套可复用的自研 HMI 框架

**档位**:能干活(单一项目能跑)｜ **周期**:2.5-3.5 个月,2026-06-11 起

---

## 📚 学习路线

> ⚠ 阶段编号只有这一套(2026-08-27 统一)。旧版 README 用过另一套编号,已作废归档。

| 阶段 | 内容 | 状态 |
|---|---|---|
| 0 | Qt 热身(窗口、信号槽、元对象系统) | ✅ |
| 1 | 传输层 TCP(超时 + 指数退避重连 + 心跳) | ✅ |
| 2 | 协议层 Modbus RTU/TCP(0x01~0x10 + 异常响应) | ✅ |
| 3 | 变量模型(订阅发布 + 轮询 + 死区) | ✅ |
| 4 | 控件绑定(变量 ↔ UI 双向) | ✅ |
| 5 | 业务层(状态机 + 报警 + 报警联动) | ✅ |
| 6 | 工程文件(JSON 存读) | 🟡 端到端验收中 |
| 7 | 整合 demo(真 PLC + 24h 长跑 + 截图视频) | ⬜ |
| 8+ | 可选扩展(S7 / 曲线 / 串口 / 多 PLC / 权限) | ⬜ |

细化任务见 [`docs/路线图.md`](docs/路线图.md)。

---

## 🧱 架构(7 层,全部主线程)

```
UI(hmi)                          3 个状态机,唯一出口 updateUi()
业务协调(AlarmManager/ActionManager)
业务执行(ThresholdAlarmChecker/WriteFailChecker)
调度(PollScheduler/WriteScheduler)   100ms tick,唯一的"主动推"
协议(ModbusMaster)                手写,接口对齐 Qt 官方 QModbusClient
传输(TcpClient)                   连接/重连/心跳
数据(Variable/VariableManager)     tag/value/quality/deadband/dirty
配置(Project/JsonSerializer)       JSON 存读
```

Qt signal/slot 是层间唯一的调度机制。详见 [`docs/专题/2026-08-09_调度机制.md`](docs/专题/2026-08-09_调度机制.md)。

---

## 🛠️ 编译与运行

**环境**:VS 2026 v18.6.2 ｜ Qt 6.8.3 `msvc2022_64` ｜ MSVC v145 ｜ Win SDK 10.0.26100.0

**主路径**:VS 打开 `hmi/hmi.slnx` → 选 `x64 / Debug` → F5

> ⚠ VS 2026 的 `vcvars64.bat` 漏了 `ucrt`,会报 `C1083: 无法打开包括文件 "stddef.h"`。
> 修法(改 `hmi.vcxproj.user`)和两条**无效解**见 [`docs/现状.md` 第六节](docs/现状.md)。

> ⚠ `CMakeLists.txt` 已过期(缺 `Binding.cpp` / `WriteScheduler.cpp`),cmake 构建会 LNK2019。**以 vcxproj 为准。**

**测试**:用 Modbus Slave 模拟器(Windows 桌面软件)验证。

---

## 📁 目录结构

```
HMI/
├── README.md              ← 本文件(唯一入口)
├── LICENSE                  MIT
├── docs/                  ← 所有文档
│   ├── 现状.md              进度 + 架构 + 约定 + 环境(唯一权威)
│   ├── 路线图.md            接下来做什么(唯一权威)
│   ├── 改善清单.md          代码待修 30 条
│   ├── HMI工程档案.md       Qt 知识卡片库
│   ├── 专题/                模块深挖(调度机制/报警联动/Modbus/schema)
│   ├── 通用知识/            跨项目可复用
│   ├── 日记/                按天流水
│   └── 归档/                过期文档(只读)
└── hmi/                   ← Qt VS 工程(VS 打开这一层)
    ├── hmi.slnx / hmi.vcxproj
    ├── main.cpp / hmi.cpp / hmi.h / hmi.ui
    ├── TcpClient · ModbusMaster                 传输 + 协议
    ├── Variable · VariableManager               数据
    ├── PollScheduler · WriteScheduler · Binding 调度 + 绑定
    ├── Alarm · AlarmManager · ThresholdAlarmChecker · WriteFailChecker
    ├── ActionManager                            报警联动
    └── Project · JsonSerializer                 工程文件
```

---

## 📄 License

MIT — 随便用、改、卖,保留原作者声明即可。
