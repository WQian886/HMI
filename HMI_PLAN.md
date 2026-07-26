# HMI 工程 - 整体计划

> Wqian 的 QT HMI 学习工程
> **生成时间**: 2026-07-25 13:03 GMT+8
> **三源核对**: 代码 + git log + 存档目录(`E:\qianwie\HMI存档\`)
> **作者**: 子衿(Wqian 个人助手)

---

## 📌 孟德补充(2026-07-27 07:20)— 工程环境同步 + 进度核对

> 这份 HMI_PLAN.md 是子衿 2026-07-25 写的。**主体一行没动**,这一节只补孟德(Mengde / 🐉)做的事 + 工程当前真实状态。阶段进度(0 → 7.x)以子衿原表为准。

### A. 工程迁移(2026-07-16)

- 路径:`E:\qt\hmi\hmi\` → **`E:\01 qianwei\HMI\`**
- 来源:`git clone https://github.com/WQian886/HMI.git` `main`,HEAD **`175f91e`**(子衿在此之上又有 5 个本地 commit)
- 老的 `_build_*.bat` 脚本**未迁移**(迁过来后直接作废)

### B. 编译环境踩坑 → 解决

- **环境**:VS 2026 v18.6.2 + Qt 6.8.3 `msvc2022_64`(路径 `D:\Qt\6.8.3\msvc2022_64\`) + MSVC `v145`(cl 19.51.36246)+ Win SDK `10.0.26100.0`
- **坑**:VS 2026 v18 的 `vcvars64.bat` 只把 `um/shared/winrt/cppwinrt` 加进 INCLUDE,**漏了 `ucrt`** → `error C1083: 无法打开包括文件: "stddef.h"`
- **两条无效解**:
  - `set INCLUDE=...;%INCLUDE%`(shell 设环境变量 → MSBuild 会重置 env,**不生效**)
  - `/p:IncludePath="ucrt;$(IncludePath)"`(编译过,但 link 时找不到 `kernel32.lib`,因为 `$(LibraryPath)` 此时还没被 props 赋值,被整体替换成只剩 UCRT 一个)
- **真稳修法**:改 `hmi.vcxproj.user`(VS 标准用户级覆盖,不污染 vcxproj / git),加:

  ```xml
  <PropertyGroup>
    <IncludePath>$(IncludePath);C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt</IncludePath>
    <LibraryPath>$(LibraryPath);C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64</LibraryPath>
  </PropertyGroup>
  ```
  这俩属性 props sheet 设置**之后**追加,VS IDE 和 MSBuild 命令行都生效。

**命令行构建**(工程根):
```cmd
"D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
"D:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" hmi.slnx /t:Build /p:Configuration=Debug /p:Platform=x64 /m
```

### C. 编译验证通过(2026-07-25)

- ✅ Debug|x64 → `x64\Debug\hmi.exe` 664KB
- Qt6 DLLs(windeployqt 自动拷)+ plugins 全到位,双击可起,无 stderr,正常退出

### D. 进度真实状态(2026-07-27 早核对)

**已 commit(子衿本地 `main`)**:
| commit | 内容 |
|---|---|
| `768d52f` | init: 项目骨架 |
| `175f91e` | init: stages 0-2 done, stage 3 in progress |
| `c3d8d9e` | stage 3.1 close |
| `4e0f6b8` | 阶段 4.1 收官 |
| `5f43da3` | 4.1 后微调 |
| `d86cae6` | docs: HMI_PLAN.md + 16 目标 + 阶段 6/7 细化 |

→ 入库:**阶段 0 / 1 / 2 / 3.1 / 3.2+3.3 / 4.1 + 计划文档**

**未 commit(working tree,本节核对时)**:
- `M Binding.cpp` ← 含 **4.2 简化版**(`bindEdit()`,回车=确认 / Esc=取消 / 橙色边框,L83-84 注释明写 "⚠️ 简化版")
- `M ModbusMaster.cpp` / `ModbusMaster.h`
- `M Variable.h`
- `M hmi.cpp` / `hmi.h`
- `M hmi.vcxproj`
- `?? WriteScheduler.cpp` / `WriteScheduler.h`(可能是 4.2 配套)
- `A  hmi.vcxproj.user.bak-20260725-pre-ucrt`(我改 vcxproj.user 前的备份)

> ⚠️ **这些 working tree 改动不归孟德管**——是子衿 / 钱卫本地工作没 commit 的,孟德不会去 `git add` / `commit` / `push`。4.2 简化版已经在 working tree 里,钱卫 commit 时自己处理。

### E. 原文件几处过期点(本次未改)

| 项 | 原文件写 | 实际 |
|---|---|---|
| 工程目录 | `E:\qianwie\hmi\hmi\` | `E:\01 qianwei\HMI\`(2026-07-16 已迁) |
| 关键参考用户名 | `HP` | `Zz` |
| 关键参考路径 | `C:\Users\HP\.openclaw\...` | `C:\Users\Zz\.openclaw\...` |
| 存档目录 | `E:\qianwie\HMI存档\` | 未核对(可能也不存在) |

---

## 🎯 项目目标

**主目标**: 理解 HMI 底层原理, 最终形成自己的 HMI 框架

**能力档位**: 能干活(单一项目能跑)+ 2.5-3.5 个月

**学习方向**: 高级语言 → 上位机 → 数据库(从 PLC 老电工转上位机)

---

## 🎯 16 条细化目标

### 框架能力(技术验收)

| # | 目标 | 怎么算"做到了" |
|---|---|---|
| **F1** | 多协议支持 | Modbus 已做 + S7(7 后) + 加新协议 < 1 周 |
| **F2** | 通信层稳定 | 断线自动重连 + 心跳 + 超时 + 异常重试,**24h 不掉** |
| **F3** | 变量层抽象到位 | 加新变量 = 改 JSON 配置一行, 不改框架代码 |
| **F4** | UI 绑定框架化 | 加新画面 = 拖控件 + `bind()` 一行, 不改 `hmi.cpp` |
| **F5** | 业务状态机手写 | `enum class` + `transition()` 校验, 三键互斥 + Fault(**不上 `QStateMachine`**) |
| **F6** | 报警体系 | 三等级(Info/Warning/Critical) + 列表 + 闪烁 + 触发/恢复历史 |
| **F7** | 工程文件 | JSON 存读 + 重开恢复(通讯/变量/画面/报警全恢复) |

### 工程能力(产出验收)

| # | 目标 | 怎么算"做到了" |
|---|---|---|
| **E1** | VS + Qt 工程完善 | F5 一遍过, 新加 `.h/.cpp` 自动注册 vcxproj |
| **E2** | git 流程清晰 | 完成 → commit + push, 按阶段标记(**已做到**) |
| **E3** | 存档习惯 | "我会了" → 自动存档到 `HMI存档/`(**已做到**) |
| **E4** | 真实 PLC 验证 | 用真 PLC 或 ModbusSlave 仿真, 跑完 7 阶段全套 |
| **E5** | README + demo | 阶段 7 收官时, 给一份能对外讲的文档 / 截图 / 视频 |

### 学习能力(成长验收)

| # | 目标 | 怎么算"做到了" |
|---|---|---|
| **L1** | Qt 知识成体系 | 核心类有知识卡索引(QTimer/QPushButton/Q_OBJECT/QTcpSocket/QModbusDevice/QTableWidget/QJsonDocument/...) |
| **L2** | 工业协议懂原理 | Modbus RTU/TCP 帧结构 + 异常响应 + S7 ISO-on-TCP — 知道为啥这样设计 |
| **L3** | 设计模式懂 | 状态机 / 适配器 / 观察者 / 单例 — 至少 3 个能在自己代码里指出来 |
| **L4** | 抽象能力 | 框架分层合理 — 拆得出"这层做啥 / 不做啥" |
| **L5** | 调试排错 | qDebug + Wireshark + 协议分析工具 — 不靠猜 |

---

## 📍 当前进度(截至 2026-07-25)

### ✅ 已完成

| 阶段 | 子练 | 主要文件 | 存档时间 |
|---|---|---|---|
| 0 | QT 热身 (0.1 + 0.2) | hmi 骨架 | 6-13 前 |
| 1 | TCP (1.1 + 1.2) | `TcpClient.cpp/h` | 6-22 |
| 2 | Modbus (2.1 - 2.7) | `ModbusMaster.cpp/h` | 7-13 |
| 3.1 | Variable + Manager | `Variable.cpp/h` + `VariableManager.cpp/h` | 7-17 |
| 3.2 | 订阅 UI + 死区 | `hmi.cpp` + Binding 雏形 | 7-20 / 7-22 |
| 3.3 | PollScheduler | `PollScheduler.cpp/h` | 7-21 |
| 4.1 | Binding 框架(正向) | `Binding.cpp/h` | 7-22 15:33 |
| 4.2 | Binding 反向写(**简化版**) | `Binding.cpp/h::bindEdit` | 7-22 16:07 |

**已用时长**: ~ 6 周(2026-06-11 ~ 2026-07-25)

### ⚠️ 4.2 的"未完成部分"(代码注释亲口留的尾巴)

`Binding.cpp` 注释原文:

```cpp
// ⚠️ 简化版:变量变就覆盖 UI(包括脏状态时 — 用户改了半截、PLC 也改了,以后者为准)
//          进阶(脏时不同步、提示"冲突")留后续练再说
// ⚠️ 类型硬编码 quint16(本练只演示 speed UInt16,Float/Bool 后续练再参数化)
```

也就是 4.2 已做: **回车=确认 / Esc=取消 / 橙色边框**  
还没做:
- ❌ 脏时不同步(用户改半截时 PLC 不覆盖)
- ❌ 冲突提示(PLC 改了脏值的弹窗)
- ❌ 类型参数化(其他类型支持)

→ **这就是 4.3 要做的**

### ⚪ 下一步(优先级排序)

| 序 | 阶段 | 主要内容 | 估时 |
|---|---|---|---|
| 1 | **4.3** | bindEdit 进阶(脏时不同步 + 冲突提示 + 类型参数化) | 1-2 天 |
| 2 | **5.1** | 三键状态机(`enum` + `transition()` + Fault) | 1-2 天 |
| 3 | **5.2** | 报警类 + 三等级(Info/Warning/Critical) | 1-2 天 |
| 4 | **5.3** | 报警列表 UI(`QTableWidget` + 闪烁) | 1-2 天 |
| 5 | **5.4** | 报警触发(Variable 超限) | 1-2 天 |
| 6 | **6** | 工程文件(JSON 存读 + 重开恢复) | 1-2 周 |
| 7 | **7** | 整合 demo(真 PLC + README + 截图视频) | 1-2 周 |

### 🎯 阶段 7 收官时应该全勾

- ✅ F1-F7(框架能力)
- ✅ E1-E5(工程能力)
- ✅ L1-L5(学习能力)

---

## 🗺️ 阶段 ↔ 目标 映射

| 阶段 | 主要任务 | 兑现目标 | 验收 | 估时 | 状态 |
|---|---|---|---|---|---|
| **0** QT 热身 | QTimer/Q_OBJECT/QPushButton | L1 | 三按钮窗体 F5 + 知识卡 | 1 周 | ✅ |
| **1** TCP | 异步收发 + 超时/重连/心跳 | F2 部分 | 1.2 收官 + 13 bug + F5 | 1-2 周 | ✅ |
| **2** Modbus | 0x01-0x10 + 异常响应 + CRC | **F1 (Modbus)** + L2 | ModbusSlave 全 FC 跑通 | 2-3 周 | ✅ |
| **3** 变量层 | Variable + 订阅 + 死区 + 调度 | F3 部分 | VariableManager + PollScheduler + F5 | 1-2 周 | ✅ |
| **4.1** Binding 正向 | `bind()` + 三方调用 | F4 部分 | hmi.cpp 替换 if/else + F5 | 1 天 | ✅ |
| **4.2** Binding 反向 | `bindEdit()` + 回车/Esc/橙边 | F4 部分(简化版) | UI 验证 | 1 天 | ✅ |
| **4.3** 4.2 进阶 | 脏时不同步 + 冲突 + 类型参数化 | **F4 完整** | 弹窗 + 类型化 + F5 | 1-2 天 | ⚪ **下一步** |
| **5.1** 三键状态机 | `enum` + `transition()` + Fault | **F5** | F5 跑通互斥逻辑 | 1-2 天 | ⚪ |
| **5.2** 报警类 | `Alarm{trigger/recover/level}` | F6 部分(数据) | console 验证 | 1-2 天 | ⚪ |
| **5.3** 报警列表 UI | `QTableWidget` + 闪烁 | F6 部分(UI) | F5 | 1-2 天 | ⚪ |
| **5.4** 报警触发 | Variable threshold 越界 | F6 部分(触发) | ModbusSlave 超限触发 | 1-2 天 | ⚪ |
| **6** 工程文件 | JSON schema + Project + 重开恢复 | **F7 + E1 部分** | F5 重开恢复 | 1-2 周 | ⚪ |
| **7** 整合 demo | 真 PLC + README + 截图视频 | **E4 + E5** | 截图/视频 + README ≥ 100 行 | 1-2 周 | ⚪ |

---

## 🔄 横切事项(贯穿所有阶段)

| # | 横切 | 节奏 | 怎么算"做到了" |
|---|---|---|---|
| **E1** | VS + Qt 工程完善 | 持续 | 新加 .h/.cpp 自动注册 vcxproj; F5 不掉 |
| **E2** | git 流程 | 每子练 | 完成 → commit + push(**已做到**) |
| **E3** | 存档习惯 | 每子练 | "我会了" → 自动存档 `HMI存档/`(**已做到**) |
| **L1** | Qt 知识卡 | 每新类引入 | 知识卡进 `knowledge-cards/` 或 inventory |
| **L2** | 协议原理 | 阶段 2/3/7 | 知道帧结构 + 为啥这样设计 |
| **L3** | 设计模式沉淀 | 每阶段回顾 | 状态机 / 适配器 / 观察者 — 代码里能指出来 |
| **L4** | 抽象能力 | 阶段 3-7 | 每层边界清楚(这层做啥 / 不做啥) |
| **L5** | 调试排错 | 持续 | qDebug + Wireshark + 协议分析 — 不靠猜 |

---

## 📋 阶段 6 细化(工程文件)

**总目标兑现**: F7 + E1 部分  
**总估时**: ~ 1 周  
**前置**: 4.3 + 5.x 都已收官

| 子练 | 任务 | 关键技术 | 验收 | 估时 |
|---|---|---|---|---|
| **6.1** | JSON schema 设计(顶层 document + 各 section) | `QJsonObject` / `QJsonArray` | validator 跑通 | 半天 |
| **6.2** | Project 类骨架 + `save(path)` / `load(path)` 静态接口 | `QFile` + JSON 读写 | 文件存 | 半天 |
| **6.3** | 通讯参数存读(IP/端口/slaveId/reg type) | 同上 | 改 UI → 保存 → 重开恢复 | 半天 |
| **6.4** | 变量定义存读(`{name,addr,type,access,deadband,pollMs}`) | `QJsonValue` 数组 | JSON 加变量 → 重开 → Manager 看到 | 半天 |
| **6.5** | 控件绑定信息存读(`BindingDef`) + 启动自动 bind | `findChildren` + `bind()` | JSON 加绑定 → 重开 → 生效 | 1 天 |
| **6.6** | 报警规则存读(`AlarmRule`) | 同上 | 加规则 → 重开 → 触发 | 半天 |
| **6.7** | UI 打开/保存入口(QFileDialog + menu) | `QMenu` / `QAction` | 点保存 → 写 JSON | 半天 |
| **6.8** | 集成测试(配项目 → 关 → 重开 → 全恢复 F5) | - | 全流程跑通 | 半天 |

**避坑**:
- ⚠️ 不做自动保存(学习阶段先手动)
- ⚠️ `findChildren` 必须按 `objectName` 找
- ⚠️ 路径用 `QStandardPaths`
- ⚠️ 版本号字段预留(schema 会迭代)

---

## 📋 阶段 7 细化(整合 demo)

**总目标兑现**: E4 + E5  
**总估时**: ~ 1 周  
**前置**: 6.x 都已收官

| 子练 | 任务 | 关键技术 | 验收 | 估时 |
|---|---|---|---|---|
| **7.1** | ModbusSlave 场景设计(水泵房,6-7 寄存器) | ModbusSlave | 截图 + 寄存器表(README 用) | 半天 |
| **7.2** | 工程文件配置(基于 6.x, 配 demo.json) | - | demo.json 实际存在 | 半天 |
| **7.3** | 三键控制接 PLC(写 0x0010 bit0) | `VariableManager` 写路径 | F5 按启动 → ModbusSlave 看到写 1 | 1 天 |
| **7.4** | 报警触发实跑(改 ModbusSlave 值触发 Critical) | `QTableWidget` 闪烁 | 截图证据 | 1 天 |
| **7.5** | README 撰写(简介/功能/架构/编译/知识卡索引/限制) | GitHub Markdown | README ≥ 100 行 | 半天 |
| **7.6** | 截图 + 视频(5 张静态 + 30s 录像) | Snipping Tool / OBS | README 显示 + 视频链接 | 半天 |
| **7.7** | 24h 长跑测试(F2 验收) | qDebug 计数 + 报告 | 24h 无崩溃 + 报告 | 半天(被动等) |

**避坑**:
- ⚠️ 7.7 长跑别打断(数据不准)
- ⚠️ 7.5 README 写到能给别人看, 不是给自己看
- ⚠️ 7.4 报警触发要在真 PLC 上验证, ModbusSlave 不算
- ⚠️ 7.3 写寄存器路径提前商定(0x05 还是 0x0F?)

---

## 🗓️ 时长档位

- **总时长**: 2.5-3.5 个月
- **已用**: ~ 6 周(2026-06-11 ~ 2026-07-25)
- **剩余**: ~ 4-8 周(按总时长上限)
- **阶段 6 + 7**: ~ 2 周
- **节奏建议**: 每周 ≥ 2 次有效编码 + 1 次同步进度

---

## ⚠️ 关键风险(inventory 拍板的)

| 风险 | 原因 | 解法 |
|---|---|---|
| **E5 留到阶段 7 才做** | README/视频最后才想起来 | 阶段 5 起每子练同步写"阶段笔记" |
| **F3 "加新变量 = JSON 一行" 没真验证** | 只做了改代码一行 | 阶段 6 写测试: 纯改 JSON 不动 C++, 加 5 个新变量验证 |
| **F2 "24h 不掉" 没真验证** | 阶段 1 收官靠短跑 | 阶段 7 留 24h 长跑 + 出统计报告 |
| **阶段 5 状态机 不上 `QStateMachine`** | 拍板 | 手写 `enum class` + `transition()` |
| **阶段 7 之前 不做 S7** | 拍板 | 会打断节奏 |

---

## 📚 关键参考

- **存档目录**: `E:\qianwie\HMI存档\`(**权威进度源**,"我会了"的真信号)
- **inventory**: `C:\Users\HP\.openclaw\workspace\inventory\HMI.md`
- **MEMORY 总表**: `C:\Users\HP\.openclaw\workspace\MEMORY.md`
- **远程仓库**: `https://github.com/WQian886/HMI.git`(branch `main`)
- **工程目录**: `E:\qianwie\hmi\hmi\`

---

## 📌 做事原则(防再忘)

1. **问"现在进度"**: 必须三源核对 — 存档目录 + git log + 关键代码
2. **inventory / MEMORY** 只作备忘,**不当真理**(本次修订 2026-07-25 因这俩文件都过时了才出此份)
3. **改动 HMI 源码**: 必须 Wqian 同意(子衿不擅自写代码)
4. **commit 流程**: 完成子练 → "我会了" → 自动存档 → Wqian 自己 commit(主工程 + 存档目录)
5. **改工作区文件**: 当条消息显式授权才能动

---

**主进度表见顶部 — 当前走到 4.2 完成(简化版) / 4.3 待启动**
