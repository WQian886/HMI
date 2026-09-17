# UI 规划调研与方案 — D/E 两个层级

> 适用:Wqian 在 C 阶段规划后问:"UI 怎么规划 + 用户怎么操作 + 是否能做拖拽式编程"
> 维护人:Wqian + AI 助手  ｜  创建:2026-09-17 ｜ v1.1:2026-09-17 优化
> 配套:`docs/阶段C_详细规划.md`(C 阶段含 C.4 协议选择 UI)/ `docs/路线图.md` / `docs/优化与改善计划.html` v0.2.0 / `docs/现状.md`

> **本文档范围说明**:**只讲 D(历史曲线+报表)和 E(WYSIWYG 编辑器)两个层级**。C.4 协议选择 UI 在 `docs/阶段C_详细规划.md` 第 C.4 节单独讲,本文引用不重复。

---

## 0. Wqian 问了 3 个层级的问题

| 层级 | 问题 | 对应文档 |
|---|---|---|
| **短期** | C.4 协议选择 UI 怎么搞? | `docs/阶段C_详细规划.md` 第 C.4 节 |
| **中期** | hmi 整体用户怎么操作?运维体验如何? | **本文档 D 部分**(阶段 D 历史曲线 + 报表) |
| **长期** | 可不可以做 UI 拖拽式编程? | **本文档 E 部分**(阶段 E WYSIWYG 全功能编辑器) |

**重要结论**:**拖拽式 WYSIWYG 编辑器**是路线图锁的 P0 阶段(代号 E),**完全可以做**,而且是商业化必备 — 但工作量很大(10-15 周,见 E 部分估时修正),排在 C + D 之后。

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

路线图锁了 **Qt + QML WYSIWYG + Lua 脚本**(E 阶段)— 这正好对应"自研定制派"。

### 1.3 关键洞察

1. **QML 是关键**:Qt Quick 的声明式语法 + Scene Graph 硬件加速 → 天然适合 WYSIWYG
2. **Lua 脚本是分水岭**:WinCC/iFIX 都有脚本层,Lua 比 C# / Python 更适合嵌入式 + 沙箱安全
3. **拖拽式核心 8 个功能**(路线图 E4 锁了):拖拽画布 + 选中/缩放 + 属性面板 + Undo/Redo + 多选/复制 + 对齐线 + 保存 JSON + 实时预览
4. **控件库要 20+**(路线图 E1 锁了):显示(Label/Bar/Gauge)+ 图形(Pipe/Valve/Pump)+ 控制(Button/Slider)+ 数据(Trend/Table)+ 元件(Image/Rect)

---

## 2. 中期:阶段 D(历史曲线 + 报表)— 运维体验(3-4 周)

### 2.1 当前 hmi 用户操作流程(无 D 阶段)

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

### 2.2 D 阶段后用户操作流程(目标)

```
操作员老王上班
   ↓
打开 hmi.exe → 看主窗口 + 实时曲线(最近 10 分钟)
   ↓
点"历史曲线"标签 → 选时间范围(昨天/最近 7 天/本月)→ 看完整曲线
   ↓
点"导出报表" → 选日期范围 + 类型(报警/写值/...) → 导出 Excel
   ↓
机器出故障 → 点"报警历史" → 看完整时间线 + 当时谁在操作(audit 联动 B5)
```

### 2.3 D 阶段采样频率(关键设计决策)

**问题**:历史数据采多频繁?
- 太频繁:磁盘爆 + 写 IO 慢
- 太慢:曲线不精细,丢失细节

**决策**(锁定):
```
策略:PollScheduler 已有数据 → 每 5 秒一次"变化检测" → 只写变化的
       + 每 60 秒一次"心跳" → 即使没变化也写(确认数据流还活着)

SQLite 容量估算:
  - 单个 variable 每小时最多写 720 次(每秒变化)
  - 但用了变化检测后,实际平均 < 10 次/小时(工业现场多数稳定)
  - 100 个 variable × 10 次/小时 × 24 × 365 = 876 万行/年
  - 1 行 ≈ 100 字节 → 876 MB/年
  - 实际 ≤ 200 MB/年(工业现场很少持续变化)
```

**规则**:
- **变化检测阈值**:值变化超过 `Variable.deadband_` 才写
- **心跳**:60 秒一次,写"值 + 时间戳 + quality=Good",即使值不变
- **采样精度**:PollScheduler 的 100ms tick 已经够细,采样频率 = 100ms,但只写"满足变化检测 / 心跳条件"的

### 2.4 D 阶段 UI 元素(hmi.ui 加)

| 控件 | objectName | 用途 |
|---|---|---|
| QTabWidget | `bottomTabs` | 主窗口底部 3 个标签:实时 / 历史 / 报警 |
| QChartView | `trendChart` | 历史曲线(Qt Charts 模块) |
| QDateEdit | `fromDate` / `toDate` | 时间范围选择 |
| QTableView | `historyTable` | 历史数据表 |
| QPushButton | `exportExcelBtn` | 导出 Excel(.xlsx,QXlsx 库) |

### 2.5 SQLite 存储决策(锁定)

**技术选型**:**Qt SQL (QSqlDatabase)**,不用 sqlite3 C API 直接调。

**原因**:
- Qt 跨平台封装(Windows/Linux/macOS 都行)
- Qt 资源管理(RAII,出作用域自动关)
- 防止 SQL 注入(用 QSqlQuery 占位符 + bindValue)
- 跟现有 Qt 架构一致(不引新语言栈)

**存储位置**:`%LOCALAPPDATA%\QianWei\HMI\history.sqlite`

**表结构**:
```sql
-- 变量历史(变化检测 + 心跳)
CREATE TABLE variable_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    variable_name TEXT NOT NULL,    -- 例:"plc1.Speed"
    plc_id TEXT NOT NULL,           -- 多 PLC 支持(C.5 后)
    value REAL NOT NULL,
    quality INTEGER NOT NULL,       -- 0=Bad, 1=Good, 2=Uncertain
    timestamp INTEGER NOT NULL,     -- Unix ms
    is_heartbeat INTEGER DEFAULT 0  -- 1=心跳, 0=变化检测
);
CREATE INDEX idx_var_ts ON variable_history(variable_name, timestamp);

-- 报警历史
CREATE TABLE alarm_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    alarm_id INTEGER NOT NULL,
    alarm_name TEXT NOT NULL,
    plc_id TEXT,
    level INTEGER NOT NULL,
    triggered_at INTEGER NOT NULL,
    recovered_at INTEGER,
    acked_at INTEGER,
    acked_by TEXT
);
CREATE INDEX idx_alarm_ts ON alarm_history(triggered_at);
```

### 2.6 写入策略(锁定的伪代码)

```cpp
// hmi/src/history/HistoryWriter.cpp(新)
void HistoryWriter::onVariableChanged(Variable* v) {
    // 变化检测
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (qAbs(v->value().toDouble() - lastWrittenValue_) > v->deadband()) {
        insertVariable(v, now, /*heartbeat=*/false);
        lastWrittenValue_ = v->value().toDouble();
    }
}

void HistoryWriter::onHeartbeatTick() {
    // 每 60 秒:写所有 variable 一行(值 + 心跳标志)
    for (Variable* v : varMgr_->allVariables()) {
        insertVariable(v, QDateTime::currentMSecsSinceEpoch(), /*heartbeat=*/true);
    }
}
```

**验收**:
- [ ] SQLite 文件 `history.sqlite` 生成在 `%LOCALAPPDATA%\QianWei\HMI\`
- [ ] 变量值变化 → 数据库新增一行(变化检测)
- [ ] 60 秒无变化 → 心跳行(确认数据流活着)
- [ ] 主窗口底部 TabWidget 切到"历史" → 显示曲线 + 表格
- [ ] 导出 Excel → .xlsx 文件可打开
- [ ] tst_History 至少 5 个用例(写入 / 查询 / 导出 / 心跳)

---

## 3. 长期:阶段 E(WYSIWYG 编辑器)— 商业化杀手锏(10-15 周)

### 3.1 估时修正(从 5-7 周 → 10-15 周)

**原估时问题**:路线图锁的 E4 编辑器 5-7 周太乐观。

**修正依据**:
- 商业 WYSIWYG 编辑器(Qt Creator 自己)花了 Qt 公司 5+ 人年
- 几何算法(选中/移动/缩放/对齐/参考线)单独就要 2-3 周
- Q_PROPERTY 反射 + 自动生成属性面板要 1 周
- Undo/Redo 跟其他特性交互多,bug 不少
- 实时预览(reuse hmi renderer)调试坑多

**新估时**:

| 子任务 | 路线图估时 | 实际估时 | 差额 |
|---|---|---|---|
| E1 控件库(22 个) | 3 周 | 4 周 | +1 周(22 个控件比想象多) |
| E2 绑定系统 | 1 周 | 1.5 周 | +0.5 周(反射调试) |
| E3 JSON 存读 | 0.5 周 | 0.5 周 | 0 |
| **E4 WYSIWYG 编辑器** | **5-7 周** | **8-12 周** | **+3-5 周(主因)** |
| E5 主题系统 | 1 周 | 1 周 | 0 |
| E6 Lua 脚本(可选) | 1 周 | 1 周 | 0 |
| **E 阶段总计** | **10-12 周** | **15-19 周** | **+5-7 周** |

**T3 业余节奏推荐**:E 阶段实际用 **15-19 周**(4-5 个月),可以分 2-3 个季度做。

### 3.2 E 阶段:独立 exe(锁定决策)

**问题**:编辑器是独立 exe 还是 hmi.exe 内嵌模式?

**决策**:**独立 exe `hmi-editor.exe`**,跟 hmi.exe 分开。

**原因**:
- **安全**:操作员不能进编辑模式(避免误操作改界面)
- **责任分离**:工程师用 hmi-editor,操作员用 hmi,职责清晰
- **部署灵活**:操作员的 hmi.exe 不带编辑器(轻量),工程师电脑上两个都有
- **Qt Creator 自己也是这么做的**(Creator IDE 独立于运行时)

**架构**:
```
hmi.exe            操作员用,只能看/操作
hmi-editor.exe     工程师用,可以编辑工程文件 + 预览

两者共享:
  - src/drivers/*        3 协议驱动
  - src/widgets/*        22 个控件
  - src/core/*           Variable/Project/JsonSerializer
```

### 3.3 路线图锁的 E 阶段分解

来源:`docs/优化与改善计划.html` v0.2.0 阶段 E

| 子任务 | 内容 | 估时(修正) |
|---|---|---|
| **E1** | 控件库(22 个,基类 + 派生) | 4 周 |
| **E2** | 绑定系统(Q_PROPERTY 反射) | 1.5 周 |
| **E3** | JSON 存读(扩展现有 JsonSerializer) | 0.5 周 |
| **E4** | **WYSIWYG 编辑器**(8 大核心功能) | **8-12 周** |
| E5 | 主题系统(theme.json,2 套起) | 1 周 |
| E6 | Lua 脚本引擎(sol2 + 沙箱)— **可选,见 3.5** | 1 周 |

### 3.4 E4 编辑器 8 大核心功能(估时修正)

| # | 功能 | 估时(原) | 估时(修正) |
|---|---|---|---|
| ① 拖拽画布 + 控件调色板 | 1 周 | 2 周 |
| ② 选中/移动/缩放(8 handle + shift 等比) | 1.5 周 | 3 周 |
| ③ 属性面板(Q_PROPERTY 反射自动生成) | 1 周 | 2 周 |
| ④ Undo/Redo(QUndoStack) | 0.5 周 | 1 周 |
| ⑤ 多选 / 复制粘贴 | 0.5 周 | 1 周 |
| ⑥ 对齐线 / 参考线 | 0.5 周 | 1 周 |
| ⑦ 保存/加载 JSON(扩展现有 JsonSerializer) | 0.5 周 | 0.5 周 |
| ⑧ 实时预览(reuse hmi renderer) | 0.5 周 | 1.5 周 |

### 3.5 E6 Lua 脚本 — 建议砍掉或延后

**问题**:如果不做 Lua 脚本,Button 控件怎么触发动作?

**无 Lua 方案**(锁定):
- Button 控件触发**预定义动作**,而不是任意 Lua 脚本
- 预定义动作清单(路线图锁的 8 类):
  - 启动(写启动变量为 true)
  - 停止(写停止变量为 true)
  - 复位(写复位变量为 true)
  - 写值(写固定值到目标变量)
  - 切画面(跳到指定画面 ID)
  - 弹报警确认(模拟右键报警确认)
  - 弹修改密码(调用 ChangePasswordDialog)
  - 触发事件(emits 一个 signal 给业务层)
- 客户场景够用(工业 HMI 80% 的 Button 操作是这 8 类)

**Lua 留作可选**:如果未来客户需要自定义逻辑(比如"按钮按下后先读 A 变量,如果 > 50 才写 B"),再启用 Lua。

**建议**:**E6 砍掉或延后**(节省 1 周),简单场景不需要 Lua,工业 HMI 80% 操作走预定义动作。

### 3.6 用户操作流程(WYSIWYG 编辑器)

```
工程师小李接到新项目(给 XX 化工厂做监控屏)
   ↓
打开 hmi-editor.exe(独立 exe,跟 hmi.exe 分开)
   ↓
[新建工程] → 选协议(Modbus/S7/EIP) + 选 PLC IP/Port
   ↓
主窗口 = 画布 + 左侧"控件调色板" + 右侧"属性面板"
   ↓
拖拽控件到画布:
  - 拖 1 个 Gauge → 改 size → 改绑定变量(plc1.Speed)
  - 拖 1 个 Pipe(管道) → 旋转 90° → 改颜色
  - 拖 1 个 Button → 改 text("启动") → 选预定义动作("启动")
   ↓
画布上 5-10 个控件,组成 1 个画面
   ↓
保存 → 生成 .json 工程文件
   ↓
切到 hmi.exe → 打开这个工程文件 → 实时显示 + 操作
```

### 3.7 控件库(E1)详细清单

**路线图锁的 22 个控件**:

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

### 3.8 技术选型(锁定)

| 选项 | 推荐度 | 理由 |
|---|---|---|
| **自研 Qt + QML WYSIWYG** ⭐ | ⭐⭐⭐ | 路线图锁了;跟现有架构一致;无授权费;完全可控 |
| LVGL + SquareLine Studio | ⭐ | 适合 MCU,不适合桌面 HMI(本项目是 Windows) |
| Qt + Qwt | ⭐ | Qwt 是老牌图表库,但不是拖拽编辑器 |
| 用 Qt Designer 自带的 .ui | — | Qt Designer 是 Qt 官方编辑器,但不可嵌入到自家产品(它是个独立 app),不能商用 |

**结论**:**自研 Qt + QML WYSIWYG**,路线图已经决策。

### 3.9 E 阶段 QML vs QWidget 决策(锁定)

| 维度 | Qt + QML ⭐ | QWidget |
|---|---|---|
| WYSIWYG 适合度 | 天然适合(声明式 + 实时预览) | 一般(代码 UI,改完要重新编译看效果) |
| 动画 / 过渡 | 强(QML Animation) | 弱 |
| 学习曲线 | 中(新语法) | 低(现有 hmi 都用 QWidget) |
| 跟现有架构兼容 | 需重构(现有 hmi.cpp 是 QWidget) | 直接加新控件即可 |

**决策**:**新画面用 QML,老画面(hmi.cpp)保留 QWidget**。两种共存,新画面走 QML。

---

## 4. 推荐路径

### 4.1 时间窗口(按路线图 + 实际估算)

| 阶段 | 估时(路线图) | 估时(实际) | 何时开始 |
|---|---|---|---|
| **C**(协议 + 多 PLC) | 6-8 周 | **10-13 周** | 现在 |
| **D**(历史曲线 + 报表) | 3-4 周 | 3-4 周 | C 完成后 |
| **E**(WYSIWYG 编辑器) | 10-12 周 | **15-19 周** | D 完成后 |
| F(报警商业级) | 2-3 周 | 2-3 周 | E 完成后 |
| G(OPC UA + REST) | 3-4 周 | 3-4 周 | F 完成后 |
| H(配方 + 报表) | 2 周 | 2 周 | |
| I(安装包 + i18n) | 2 周 | 2 周 | |
| **合计** | 33-40 周 | **42-51 周** | |

**结论**:T3 业余节奏(每晚 + 周末)做完全部 9 阶段约 **10-13 个月**,实际日历 12-15 个月(留 buffer)。

### 4.2 推荐先做什么

按"立竿见影"排序:

| 优先级 | 做什么 | 估时 | 价值 |
|---|---|---|---|
| ⭐⭐⭐ | **C.0 schema 演进** | 3-5 天 | 地基,工程文件兼容 |
| ⭐⭐⭐ | **C.1 协议抽象层** | 1 周 | 地基,后面全靠它 |
| ⭐⭐⭐ | **C.2 S7 适配器** | 2-3 周 | 国内 60% 工厂用,商业化必做 |
| ⭐⭐ | D.1 SQLite 历史库 + 采样策略 | 1-2 周 | "能查过去"是运维刚需 |
| ⭐⭐ | E.1 控件库(分批做,先 5 个 Gauge/Bar/Button/Label/Indicator) | 1-2 周 | 商业化起步 |
| ⭐ | E.4 编辑器主体 | 8-12 周 | 真要做 WYSIWYG 才有编辑器 |

### 4.3 不推荐先做什么

- **C.3 EtherNet/IP 先做**(国内用得少,放 C.2 后面)
- **C.5 多 PLC**(商业化早期单 PLC 够用,放后期)
- **E6 Lua 脚本**(建议砍掉或延后,见 3.5)
- **E4 编辑器主体**(没控件库先不上,否则编辑啥?)
- **G OPC UA / REST**(等 D + E 完成后才有数据可暴露)

---

## 5. 关键决策(需要 Wqian 拍板)

### 5.1 E 阶段技术选型:Qt + QML vs QWidget ✅ 已锁

**决策**:Qt + QML(新画面)+ QWidget(老画面)共存。

### 5.2 Lua 脚本是否必做 ✅ 已砍

**决策**:E6 砍掉或延后,Button 走 8 类预定义动作。简单场景够用,Lua 留作可选。

### 5.3 商用 license 边界(必须做)

- **自用**:完全免费(Wqian 自己用)
- **客户用**:LGPL 边界注意 — Snap7 / libplctag 动态链接 + 不改源码 = 合规
- **卖产品 + 不公开源码**:咨询律师

**建议**:**C 阶段开干前**先找律师过一遍 LGPL 边界,不要等商用出问题才补。

---

## 6. 什么时候写 `阶段C_从这里继续.md`

按 T3 业余模式约定,每阶段结束必出 3 件产物:`阶段C_从这里继续.md` + `阶段C_Demo.mp4` + `阶段C_回顾.md`。

**本文档建议**:
- **C.1 完成后**:写 mini 版 `阶段C_1_从这里继续.md`(1 页,给两周后的自己看)
- **C.6 完成后**:写正式版 `阶段C_从这里继续.md`(详细,B5 模板参照)
- **E.4 编辑器完成后**:写 `阶段E_从这里继续.md`

---

## 7. 关联文档

| 文档 | 关系 |
|---|---|
| `docs/阶段C_详细规划.md` | 短期(C 阶段含 C.4 协议选择 UI)的具体里程碑 |
| `docs/路线图.md` | 9 阶段总览 |
| `docs/优化与改善计划.html` v0.2.0 | E 阶段详细方案(路线图锁的) |
| `docs/现状.md` | 7 层架构现状 |
| `docs/改善清单.md` E 部分 | E 阶段任务清单 |

---

## 8. 一句话总结

> **拖拽式 WYSIWYG 编辑器可以做**(路线图 E 阶段,实际 15-19 周而非路线图 10-12 周,自研 Qt + QML),**建议做完 C + D 再开干**(否则没数据也没控件可编辑)。
>
> **当前应该先开干 C.0 schema 演进 + C.1 协议抽象层**(3-5 天 + 1 周纯代码,不需要 PLC 设备),同时**联系律师过一遍 LGPL 合规**(商用前必修)。

---

## 9. 关键 sources(精选 3 个,其他放参考)

**Sources** (web search 2026 主流 HMI 编辑器对比):
- [eathealthy365: Weintek vs Siemens](https://eathealthy365.com/comparing-weintek-hmi-software-and-siemens/) — 威纶通拖拽式 vs WinCC 工程化对比
- [anexee: Ignition vs iFIX vs WinCC Unified vs FactoryTalk 2026](https://www.anexee.com/blog/ignition-vs-ifix-vs-wincc-comparison-2026) — 5 大 SCADA 平台 ISA-101 合规对比
- [zediot: HMI Development 2025](http://www.zediot.com/blog/hmi-development-2025/) — Qt / LVGL / Web HMI 技术栈全景

**其他参考(不直接引用)**:
- wifitalents.com Top 10 HMI Programming Software 2026
- boardor HMI Development Stack 2025
- CSDN WinCC vs iFIX
- gitnux Best HMI Development Software 2026
