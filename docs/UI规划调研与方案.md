# UI 规划调研与方案 — C/D/E 三个层级

> 适用:Wqian 在 C 阶段规划(commit 7ecba00)后问:"UI 怎么规划 + 用户怎么操作 + 是否能做拖拽式编程"
> 维护人:Wqian + AI 助手  ｜  创建:2026-09-17
> 配套:`docs/阶段C_详细规划.md` / `docs/路线图.md` / `docs/优化与改善计划.html` v0.2.0 / `docs/现状.md`

---

## 0. Wqian 问了 3 个层级的问题

| 层级 | 问题 | 对应路线图阶段 | 估时 |
|---|---|---|---|
| **短期** | C.4 统一协议选择 UI 怎么搞? | 阶段 C | 1 周 |
| **中期** | hmi 整体用户怎么操作?运维体验如何? | 阶段 D(历史曲线/报表) | 3-4 周 |
| **长期** | 可不可以做 UI 拖拽式编程? | **阶段 E(WYSIWYG 全功能编辑器)** | **10-12 周** |

**重要结论**:**拖拽式 WYSIWYG 编辑器**是路线图锁的 P0 阶段(代号 E),**完全可以做**,而且是商业化必备 — 但工作量很大(10-12 周),排在 C 之后。

---

## 1. 工业 HMI WYSIWYG 行业调研

> 来源:WebSearch 2026 主流工业 HMI 编辑器对比

### 1.1 三大流派

| 流派 | 代表 | 特点 | 适用 |
|---|---|---|---|
| **轻量原型派** | iFIX、威纶通 EasyBuilder Pro | 拖拽 + 即绑 PLC 标签,门槛低 | 中小项目、独立设备 |
| **工程化平台派** | WinCC Unified(TIA Portal)、Studio 5000 | 拖拽 + 严格模板/符号库/版本控制 | 大型项目、多人协同 |
| **自研定制派** ⭐ | Qt + QML、LVGL + SquareLine | 完全可控、可深度定制 | 自研产品、嵌入式 |

### 1.2 Wqian 项目对应路线图

路线图 `docs/优化与改善计划.html` 第 6 行锁了:
> **H6 画面编辑 → WYSIWYG 全功能(拖拽 + 选中 + 属性 + Undo/Redo + 主题)**

技术选型路线图也锁了:
> **Qt + QML**(声明式 UI 天然 WYSIWYG 预览)+ **Lua 脚本**(用户级逻辑,可选)

**这正好对应"自研定制派"** — 路线图决策跟市场主流对齐。

### 1.3 关键洞察

1. **QML 是关键**:Qt Quick 的声明式语法 + Scene Graph 硬件加速 → 天然适合 WYSIWYG(改完代码立刻预览)
2. **Lua 脚本是分水岭**:WinCC/iFIX 都有脚本层(自定义逻辑),威纶通靠宏指令。Lua 比 C# / Python 更适合嵌入式 + 沙箱安全
3. **拖拽式核心 8 个功能**(路线图 E4 锁了):拖拽画布 + 选中/缩放 + 属性面板 + Undo/Redo + 多选/复制 + 对齐线 + 保存 JSON + 实时预览
4. **控件库要 20+**(路线图 E1 锁了):显示(Label/Bar/Gauge)+ 图形(Pipe/Valve/Pump)+ 控制(Button/Slider)+ 数据(Trend/Table)+ 元件(Image/Rect)

---

## 2. 短期:C.4 统一协议选择 UI(1 周)

### 2.1 用户操作流程

```
启动 hmi.exe
   ↓
[首启] 弹"创建管理员"对话框
   ↓
[非首启] 弹"登录"对话框 → 输入用户名/密码
   ↓
登录成功,主窗口显示
   ↓
主窗口顶部工具栏:
  [协议 ▼] [PLC IP] [PLC Port] [连接] [断开]
   ↓
用户选协议(下拉框):
  - Modbus TCP  → 现有功能
  - S7 西门子    → C.2 完成后可用
  - EtherNet/IP → C.3 完成后可用
   ↓
填 IP / Port,点"连接"
   ↓
PollScheduler 用对应 driver 通讯
```

### 2.2 关键 UI 元素(hmi.ui 改)

```
┌─ 顶部工具栏 ────────────────────────────────────┐
│ 协议: [Modbus TCP ▼]  IP: [127.0.0.1]  Port: [502] │
│ [连接] [断开]    状态: 已连接 ●                  │
└─────────────────────────────────────────────────┘
```

| 控件 | objectName | 行为 |
|---|---|---|
| QComboBox | `protocolComboBox` | 3 选 1:Modbus/S7/EIP |
| QLineEdit | `ipEdit` | IP 地址(已有) |
| QLineEdit | `portEdit` | 端口(已有,默认 502 / 102 / 44818) |
| QPushButton | `connectBtn` | 已有 |
| QPushButton | `disconnectBtn` | 已有 |
| QLabel | `connLabel` | 已有,显示连接状态 |

### 2.3 切换协议的处理

- **协议改了 → 重建 PollScheduler**(旧的断开,新的用新 driver)
- **B3/B4 权限不受影响**(协议选择是工程层,不是用户层)
- **AuditLog 记录**:`action=protocol.change detail=modbus→s7`(C.2/C.3 完成后)

---

## 3. 中期:阶段 D(历史曲线 + 报表)— 运维体验(3-4 周)

### 3.1 当前 hmi 用户操作流程(无 D 阶段)

```
操作员老王上班
   ↓
打开 hmi.exe → 看主窗口实时数据(转速/温度/压力)
   ↓
想看昨天夜班机器跑得怎么样 → ???(没历史功能)
   ↓
想导出本月报警记录给老板 → ???(只能截图)
   ↓
机器出故障 → 看主窗口只能看"现在",查不了"过去"
```

### 3.2 D 阶段后用户操作流程(目标)

```
操作员老王上班
   ↓
打开 hmi.exe → 看主窗口 + 实时曲线(最近 10 分钟)
   ↓
点"历史曲线"标签 → 选时间范围(昨天/最近 7 天/本月)→ 看完整曲线
   ↓
点"导出报表" → 选日期范围 + 类型(报警/写值/...) → 导出 Excel
   ↓
机器出故障 → 点"报警历史" → 看完整时间线 + 当时谁在操作(audit)
```

### 3.3 D 阶段 UI 元素(hmi.ui 加)

| 控件 | objectName | 用途 |
|---|---|---|
| QTabWidget | `bottomTabs` | 主窗口底部 3 个标签:实时 / 历史 / 报警 |
| QChartView | `trendChart` | 历史曲线(Qt Charts 模块) |
| QDateEdit | `fromDate` / `toDate` | 时间范围选择 |
| QTableView | `historyTable` | 历史数据表 |
| QPushButton | `exportExcelBtn` | 导出 Excel(.xlsx) |

### 3.4 SQLite 存储(D 阶段后端)

- 路径:`%LOCALAPPDATA%\QianWei\HMI\history.sqlite`
- 表结构:
  - `variable_history(variable_name, value, quality, timestamp)` — 变量快照
  - `alarm_history(alarm_name, level, triggered_at, recovered_at, acked_at)` — 报警历史
- 写入策略:PollScheduler 每 5-10 秒写一次变化值(不写每次轮询,避免爆盘)
- 读取策略:UI 按时间范围查表 + 渲染曲线

---

## 4. 长期:阶段 E(WYSIWYG 编辑器)— 商业化杀手锏(10-12 周)

### 4.1 路线图锁的 E 阶段分解

来源:`docs/优化与改善计划.html` v0.2.0 阶段 E

| 子任务 | 内容 | 估时 |
|---|---|---|
| **E1** | 控件库(20+ 控件,基类 + 派生) | 3 周 |
| **E2** | 绑定系统(Q_PROPERTY 反射) | 1 周 |
| **E3** | JSON 存读(扩展现有 JsonSerializer) | 0.5 周 |
| **E4** | **WYSIWYG 编辑器** | **5-7 周** |
| E5 | 主题系统(theme.json,2 套起) | 1 周 |
| E6 | Lua 脚本引擎(sol2 + 沙箱) | 1 周 |

### 4.2 E4 编辑器 8 大核心功能(路线图锁的)

| # | 功能 | 估时 | 难度 |
|---|---|---|---|
| ① 拖拽画布 + 控件调色板 | 1 周 | 🟡 中 |
| ② 选中/移动/缩放(8 handle + shift 等比) | 1.5 周 | 🟠 难(几何) |
| ③ 属性面板(自动生成,Q_PROPERTY 反射) | 1 周 | 🟡 中 |
| ④ Undo/Redo(QUndoStack) | 0.5 周 | 🟢 易 |
| ⑤ 多选 / 复制粘贴 | 0.5 周 | 🟡 中 |
| ⑥ 对齐线 / 参考线 | 0.5 周 | 🟡 中 |
| ⑦ 保存/加载 JSON(扩展现有 JsonSerializer) | 0.5 周 | 🟢 易 |
| ⑧ 实时预览(reuse HMI renderer) | 0.5 周 | 🟢 易 |

### 4.3 用户操作流程(WYSIWYG 编辑器)

```
工程师小李接到新项目(给 XX 化工厂做监控屏)
   ↓
打开 HMI Editor(独立的 exe,跟 hmi.exe 配套)
   ↓
[新建工程] → 选协议(Modbus/S7/EIP) + 选 PLC + 选 PLC IP
   ↓
主窗口 = 画布 + 左侧"控件调色板" + 右侧"属性面板"
   ↓
拖拽控件到画布:
  - 拖 1 个 Gauge → 改 size → 改绑定变量(plc1.Speed)
  - 拖 1 个 Pipe(管道) → 旋转 90° → 改颜色
  - 拖 1 个 Button → 改 text("启动") → 绑动作(Lua 脚本)
   ↓
画布上 5-10 个控件,组成 1 个画面
   ↓
保存 → 生成 .json 工程文件
   ↓
切到 hmi.exe → 打开这个工程文件 → 实时显示 + 操作
```

### 4.4 控件库(E1)详细清单

**路线图锁的 20+ 控件**:

| 分类 | 控件 | 数量 |
|---|---|---|
| 显示 | Label / NumberDisplay / TextDisplay / Bar / Gauge / LED | 6 |
| 图形(工业) | Pipe / Valve / Pump / Motor / Tank | 5 |
| 控制 | Button / Toggle / Slider / InputEdit | 4 |
| 数据 | Trend / Table / AlarmList | 3 |
| 元件 | Image(PNG/SVG) / Rectangle / Line / Group | 4 |
| **合计** | | **22** |

每个控件需要:
- C++ 类(QWidget 派生,带 Q_PROPERTY 反射)
- 渲染(QPainter / QML)
- 属性面板(自动生成自 Q_PROPERTY)
- 序列化(写 JSON)

### 4.5 技术选型(自研 vs 用第三方)

| 选项 | 推荐度 | 理由 |
|---|---|---|
| **自研 Qt + QML WYSIWYG** ⭐ | ⭐⭐⭐ | 路线图锁了;跟现有架构一致;无授权费;完全可控 |
| LVGL + SquareLine Studio | ⭐ | 适合 MCU,不适合桌面 HMI(本项目是 Windows) |
| Qt + Qwt | ⭐ | Qwt 是老牌图表库,但不是拖拽编辑器 |
| 用 Qt Designer 自带的 .ui | — | Qt Designer 是 Qt 官方编辑器,但不可嵌入到自家产品(它是个独立 app),不能商用 |

**结论**:**自研 Qt + QML WYSIWYG**,路线图已经决策。

---

## 5. 推荐路径

### 5.1 时间窗口(按路线图)

| 阶段 | 估时 | 累计 | 何时开始 |
|---|---|---|---|
| C.1-C.6(协议 + 多 PLC) | 9-12 周 | 9-12 周 | 现在 |
| D(历史曲线 + 报表) | 3-4 周 | 12-16 周 | C 完成后 |
| **E(WYSIWYG)** | **10-12 周** | **22-28 周** | D 完成后 |
| F(报警商业级) | 2-3 周 | 24-31 周 | E 完成后 |
| G(OPC UA + REST) | 3-4 周 | 27-35 周 | F 完成后 |
| H(配方 + 报表) | 2 周 | 29-37 周 | |
| I(安装包 + i18n) | 2 周 | 31-39 周 | |

### 5.2 推荐先做什么

按"立竿见影"排序:

| 优先级 | 做什么 | 估时 | 价值 |
|---|---|---|---|
| ⭐⭐⭐ | **C.1 协议抽象层** | 1 周 | 地基,后面全靠它 |
| ⭐⭐⭐ | **C.2 S7 适配器** | 2-3 周 | 国内 60% 工厂用,商业化必做 |
| ⭐⭐ | D.1 SQLite 历史库 | 1 周 | "能查过去"是运维刚需 |
| ⭐⭐ | E.1 控件库(分批做,先 5 个) | 1 周 | 商业化杀手锏 |
| ⭐ | E.4 编辑器主体 | 5-7 周 | 真要做 WYSIWYG 才有编辑器 |

### 5.3 不推荐先做什么

- **C.3 EtherNet/IP 先做**(国内用得少,放 C.2 后面)
- **C.5 多 PLC**(商业化早期单 PLC 够用,放后期)
- **E4 编辑器主体**(没控件库先不上,否则编辑啥?)
- **G OPC UA / REST**(等 D + E 完成后才有数据可暴露)

---

## 6. 关键决策(需要 Wqian 拍板)

### 6.1 E 阶段技术选型:Qt + QML vs QWidget

| 维度 | Qt + QML ⭐ | QWidget |
|---|---|---|
| WYSIWYG 适合度 | 天然适合(声明式 + 实时预览) | 一般(代码 UI,改完要重新编译看效果) |
| 动画 / 过渡 | 强(QML Animation) | 弱 |
| 学习曲线 | 中(新语法) | 低(现有 hmi 都用 QWidget) |
| 跟现有架构兼容 | 需重构(现有 hmi.cpp 是 QWidget) | 直接加新控件即可 |

**建议**:QML 新画面,老画面(hmi.cpp)保留 QWidget。两种共存,新画面走 QML。

### 6.2 Lua 脚本是否必做

路线图锁了 E6 要做 Lua。但如果客户场景简单(只看不操作),可以砍掉,直接拖拽 + 绑变量。

**建议**:E4 做编辑器主体,**E6 砍掉或延后**。Lua 是"用户级脚本",真需要时再加。

### 6.3 商用 license 边界

- **自用**:完全免费(Wqian 自己用)
- **客户用**:LGPL 边界注意 — Snap7 / libplctag 动态链接 + 不改源码 = 合规
- **卖产品 + 不公开源码**:咨询律师

**建议**:**C 阶段开干前**先找律师过一遍 LGPL 边界,不要等商用出问题才补。

---

## 7. 关联文档

| 文档 | 关系 |
|---|---|
| `docs/阶段C_详细规划.md` | 短期(C 阶段)的具体里程碑 |
| `docs/路线图.md` | 9 阶段总览 |
| `docs/优化与改善计划.html` v0.2.0 | E 阶段详细方案(路线图锁的) |
| `docs/现状.md` | 7 层架构现状 |
| `docs/改善清单.md` E 部分 | E 阶段任务清单 |

---

## 8. 一句话总结

> **拖拽式 WYSIWYG 编辑器可以做**(路线图 E 阶段,10-12 周),技术选型 Qt + QML(自研),**但建议做完 C + D 再开干**(否则没数据也没控件可编辑)。
>
> **当前应该先开干 C.1 协议抽象层**(1 周纯代码,不需要 PLC 设备),同时**联系律师过一遍 LGPL 合规**(商用前必修)。

Sources:
- [eathealthy365.com: Weintek vs Siemens](https://eathealthy365.com/comparing-weintek-hmi-software-and-siemus/)
- [anexee.com: Ignition vs iFIX vs WinCC Unified vs FactoryTalk](https://www.anexee.com/blog/ignition-vs-ifix-vs-wincc-comparison-2026)
- [zediot.com: HMI Development 2025](http://www.zediot.com/blog/hmi-development-2025/)
- [boardor.com: HMI Development Stack 2025](https://boardor.com/blog/what-technologies-will-be-used-for-hmi-development-in-2025-a-comprehensive-overview-of-the-human-machine-interface-development-stack)
- [CSDN: WinCC vs iFIX](https://wenku.csdn.net/doc/3yzy167n95)
