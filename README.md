# HMI — Qt 工业人机界面(从零手写)

> 个人学习项目:用 Qt 6 / C++17 从零实现工业 HMI(Human-Machine Interface)。
> 不调现成控件库,**手写通信层、协议层、变量层、绑定层、业务层**,目标是理解工业 HMI 的底层原理。

> **2026-09-28 状态(基于代码核对)**:
> - A 工程化基线 ✅ 通过验证(2026-09-11,后补 A2-5/A3 验证)
> - B 用户/权限/审计 ✅ 代码落地
> - C 多协议 ✅ ModbusDriver 落地,S7/EIP 仍 stub(Snap7/libplctag 未真接)
> - D 历史库/曲线/报表/Tag ✅ 代码落地
> - E 画面框架 **🟢 全部完工(2026-09-29)** — 22 控件 + WYSIWYG 编辑器 + Undo 全覆盖 + 旋转,`ctest` 55/55 全绿
> - F/G/H/I ⬜ 未启动
> - 详见 `docs/现状.md` / `docs/路线图.md` / `hmi/CHANGELOG.md`

已实现:Modbus TCP 通信 · PLC 寄存器轮询 · 变量订阅发布 · 控件双向绑定 ·
阈值报警(4 档 + 确认/恢复)· 报警联动动作 · 工程配置 JSON 存读 ·
**用户权限 + 操作审计)** · **历史曲线 + CSV 导出)** · **WYSIWYG 编辑器(多选/对齐/缩放)**

---

## 📍 我想…… 就看……

> **这张表是全部文档的唯一入口。找不到东西时回到这里。**

| 我想…… | 看这个 |
|---|---|
| 知道**项目现在做到哪了** | **[`docs/现状.md`](docs/现状.md)** ← 唯一权威(2026-09-29 已对齐到 E 完工 + 收尾复核) |
| 知道**接下来要做什么** | **[`docs/路线图.md`](docs/路线图.md)** ← 唯一权威(v0.2.0 A-I,E 已完工 → 复现 B 组遗留 → F 启动) |
| 知道**有哪些代码待修** | [`docs/改善清单.md`](docs/改善清单.md)(30 条 / 255xp,**8 ✅ / 1 🟡 / 21 ⬜**) |
| 知道**最近 19 个 commit 干了啥** | [`hmi/CHANGELOG.md`](hmi/CHANGELOG.md)(含 v0.2.0-rc1) |
| **编译不过 / 环境配置** | [`docs/现状.md` 第六节](docs/现状.md)(含 VS2026 的 ucrt 坑) |
| 搞懂**某个模块怎么运作的** | [`docs/专题/`](docs/专题/) |
| 查**某个 Qt 类怎么用** | [`docs/HMI工程档案.md`](docs/HMI工程档案.md) 的知识卡片索引 |
| 查**换项目也能用的通用知识** | [`docs/通用知识/`](docs/通用知识/) |
| 回顾**当时是怎么想的 / 踩了什么坑** | [`docs/日记/`](docs/日记/) |
| 找**已经过期的旧文档** | [`docs/归档/`](docs/归档/)(只读) |
| 看**最近的 bug 现场** | [`docs/bug图/`](docs/bug图/)(10 张 E.4d bug 截图 + README 索引:现象↔根因↔commit↔回归测试) |

**三份权威文档的分工**:`现状.md` = 现在什么样 ｜ `路线图.md` = 接下来做什么 ｜ `改善清单.md` = 哪里要修

---

## 🎯 项目目标

- 掌握 Qt 核心机制(信号槽、元对象系统、事件循环、控件与布局)
- 手写工业协议栈,理解帧结构而非调库
- 打通「上位机 ↔ PLC」完整链路
- 沉淀一套可复用的自研 HMI 框架

**档位**:能干活(单一项目能跑)｜ **周期**:2.5-3.5 个月,2026-06-11 起

---

## 📚 学习路线(历史快照)

> ⚠ **本表是 2026-08-27 统一编号后的学习路线快照,仅作历史**。
> 2026-09-08 锁定 v0.2.0 商业化路线后,**阶段编号已改为 A–I**,权威路线见
> [`docs/路线图.md`](docs/路线图.md)("接下来做什么"的唯一答案)。
>
> 怎么对得上:
> - 阶段 0–5 = 完成态(本表 ✅)
> - 阶段 6 = 已并入 v0.2.0 的"阶段 A 工程化基线",见 [`hmi/docs/阶段A_工程化基线.md`](hmi/docs/阶段A_工程化基线.md)
> - 阶段 7 / 8+ = 已全部拆解进 v0.2.0 的 A–I 9 阶段(用户权限/多协议/历史库/画面/报警/OPC UA/配方/安装包)

| 阶段 | 内容 | 状态(历史) |
|---|---|---|
| 0 | Qt 热身(窗口、信号槽、元对象系统) | ✅ |
| 1 | 传输层 TCP(超时 + 指数退避重连 + 心跳) | ✅ |
| 2 | 协议层 Modbus RTU/TCP(0x01~0x10 + 异常响应) | ✅ |
| 3 | 变量模型(订阅发布 + 轮询 + 死区) | ✅ |
| 4 | 控件绑定(变量 ↔ UI 双向) | ✅ |
| 5 | 业务层(状态机 + 报警 + 报警联动) | ✅ |
| 6 | 工程文件(JSON 存读) | 🟡 6.5 端到端验收 ⏳ → 转入 v0.2.0 阶段 A |
| 7 | 整合 demo(真 PLC + 24h 长跑 + 截图视频) | ↪️ 见 v0.2.0 阶段 A |
| 8+ | 可选扩展(S7 / 曲线 / 串口 / 多 PLC / 权限) | ↪️ 见 v0.2.0 阶段 B–I |

**下一步该做什么**(2026-09-29 晚现状):
- ✅ **E 阶段已全部完工** —— 22 控件 + WYSIWYG 编辑器 + 写回 PLC + Undo 全覆盖 + 旋转,`ctest` **55/55** 全绿。E.5 主题 / E.6 Lua 已拍板推 v0.3.0。
- **Wqian 一句话"阶段 F 启动"**:展开 F 阶段工单(商业级报警,2-3 周 P1)。
- 详见 [`docs/现状.md`](docs/现状.md)(唯一权威)/ [`docs/路线图.md`](docs/路线图.md) 和 [`hmi/CHANGELOG.md`](hmi/CHANGELOG.md)。

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
HMI/                              ← 仓库根(2026-09-08 从更上层下移至此)
├── README.md                       本文件(唯一入口)
├── LICENSE                         MIT
├── scripts/                        自维护脚本(sync_vcxproj.py + build.cmd)
├── docs/                         ← 所有文档
│   ├── 现状.md                     进度 + 架构 + 约定 + 环境(唯一权威,2026-09-28 对齐)
│   ├── 路线图.md                   接下来做什么(唯一权威,v0.2.0 A–I)
│   ├── 改善清单.md                 代码待修 30 条(8 ✅ / 1 🟡 / 21 ⬜)
│   ├── 阶段X_从这里继续.md         每个阶段的开干说明
│   ├── 优化与改善计划.html         v0.2.0 完整方案
│   ├── HMI工程档案.md              Qt 知识卡片库
│   ├── 专题/                       模块深挖(调度机制/报警联动/Modbus/schema)
│   ├── 通用知识/                   跨项目可复用
│   ├── bug图/                      10 张 E.4d bug 现场截图 + README 索引
│   ├── 日记/                       按天流水
│   └── 归档/                       过期文档(只读)
└── hmi/                         ← Qt VS 工程根(单层)
    ├── hmi.slnx / hmi.vcxproj / hmi.vcxproj.filters / hmi.vcxproj.user
    ├── main.cpp / hmi.cpp / hmi.h / hmi.ui
    ├── TcpClient · ModbusMaster                传输 + 协议(7 层架构:传输 / 协议)
    ├── Variable · VariableManager              数据(7 层架构:数据)
    ├── PollScheduler · WriteScheduler · Binding 调度 + 绑定
    ├── Alarm · AlarmManager · ThresholdAlarmChecker · WriteFailChecker
    ├── ActionManager                           报警联动
    ├── Project · JsonSerializer                工程文件(7 层架构:配置)
    ├── src/                                  ← 阶段 A–E 代码落地
    │   ├── logging/  crash/  instance/  tray/   A 阶段工程化基线
    │   ├── auth/                                 B 阶段:用户/权限/审计(8 模块 + 8 测试)
    │   ├── drivers/                              C 阶段:ModbusDriver + S7/EIP stub
    │   ├── history/  export/  tag/               D 阶段:历史库 + CSV + 表达式 + Tag
    │   ├── widgets/                              E 阶段控件库(22 控件)
    │   │   ├── base/ControlBase                  E.4d 基类
    │   │   └── 22 控件(Label/Number/Bar/Gauge/Led/Button/Pipe/Valve/Pump/Motor/Tank/...)
    │   └── editor/                               E 阶段编辑器(✅ 完工)
    │       ├── PropertySheet · PropertyChangeCommand  E.2 反射驱动 + undo
    │       ├── SizeEditor · PositionEditor             E.4c 复合编辑器
    │       ├── PaletteWidget                          E.4a 调色板
    │       ├── ResizeHandleItem · GroupHandleItem     E.4b 多选 handle
    │       ├── SelectionFrameItem                    E.4d 选中框(视觉 deferred)
    │       └── EditorMainWindow                      E.2/E.4d 主窗口
    ├── installer/hmi.iss                       A/I 阶段:Inno Setup 安装包
    ├── tests/                                  A–E 阶段:单元测试(~25 个 tst_* + 自带 CMakeLists)
    ├── docs/阶段A_工程化基线.md                A 阶段:验证步骤
    ├── .clang-tidy / .clang-format             A 阶段:静态分析
    ├── .github/workflows/build.yml             A 阶段:CI
    └── CHANGELOG.md                            v0.1.0 + v0.2.0-rc1 变更日志
```

**VS 主打开路径**:仓库根 → `hmi/hmi.slnx`(单层,不是嵌套)
**cmake 构建路径**:仓库根 → `hmi/` 下 `cmake -B build`(CMakeLists.txt 2026-09-08 已升级为 GLOB_RECURSE)

---

## 📄 License

MIT — 随便用、改、卖,保留原作者声明即可。
