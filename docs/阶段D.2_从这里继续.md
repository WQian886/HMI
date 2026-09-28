# 阶段 D.2 · 从这里继续(Trend 控件骨架起步)

> 适用:**D.1 SQLite 历史库 + D.2 心跳策略 + hmi.ui QTabWidget 容器**完成,准备开干 Trend 控件(QCustomPlot)主体。
> 维护人:Wqian + AI 助手  ｜  创建:2026-09-20
> 配套:`docs/阶段D.1_从这里继续.md` / `docs/UI规划调研与方案.md` 第 2 节 / `docs/进度存档_2026-09-20.md`(今日存档)
> ⚠ **状态(2026-09-28 基于代码核对)**:✅ **D 阶段全部落地**(同 D.1 状态条);D.2 TrendControl 已在 `src/widgets/TrendWidget.{h,cpp}` + `TrendControl` 就位,E.4b 已修 proxy widget 缩放/选不中 bug。完整阶段状态见 `docs/路线图.md`。

---

## 0. 一句话

**D.2 起步 = 3 个零依赖前置全部落地**(hmi.ui 加 QTabWidget / HistoryRecorder 补 60s 心跳 + `is_heartbeat` 字段 / 2 个心跳测试用例全过),**hmi.exe + 17/17 测试全过**,**真实 history.db 已有 16 KB + ALTER TABLE 升级路径已验证**。D.2 主体(TrendWidget + QCustomPlot + 历史/实时曲线 + 时间范围下拉)留待下一轮开干。

---

## 1. 改了什么(2026-09-20)

| 文件 | 改动 |
|---|---|
| `hmi/hmi.ui` | 把原 `centralLayout` 末 3 个 widget(`recvText` / `auditTable` / `alarmTable`)包进 `QTabWidget`(objectName=`bottomTabs`)的"实时" tab;新增"曲线"tab(placeholder QLabel,objectName=`trendChartPlaceholder`,D.2 完成后用 QCustomPlot 替换);新增"报警历史"tab(placeholder QLabel,objectName=`alarmHistoryPlaceholder`,D.3 接)。整体 minimumSize 420 px,等于原 3 widget 之和,避免 UI 缩崩。**未改任何子控件 objectName,`hmi.cpp` 不需要改动**。 |
| `hmi/src/history/HistoryRecorder.h` | 加 `heartbeatTimer_` 成员(QTimer*)+ `setHeartbeatInterval(int)` / `start()` / `stop()` / `isRunning()` 公开接口;`onHeartbeatTick()` 私有槽;`heartbeatIntervalMs_ = 60000` 默认字段;声明 `migrateAddHeartbeatColumn()` 私有方法。 |
| `hmi/src/history/HistoryRecorder.cpp` | `ensureSchema()` 的 CREATE TABLE 加 `is_heartbeat INTEGER DEFAULT 0` 列;新增 `migrateAddHeartbeatColumn()`(PRAGMA table_info 探测 + ALTER TABLE 幂等升级);`onVariableChanged` 写 `is_heartbeat=0`(变化检测行);新增 `onHeartbeatTick()` 遍历所有 Variable 各写一行 `is_heartbeat=1`;`start()/stop()/isRunning()` 控制 timer;`setSource` 首次绑定自动 start(若 interval > 0)。 |
| `hmi/tests/tst_HistoryRecorder.cpp` | 新增 2 个测试:`heartbeat_writesAllVariablesEveryInterval`(200ms 间隔 + QTest::qWait(300) + 验 N 行 is_heartbeat=1)+ `heartbeatField_migratesFromOldDbWithoutColumn`(手工建无 is_heartbeat 列的旧 DB → 打开应自动 ALTER 升级)。 |

---

## 2. HistoryRecorder 新接口签名(D.2 增量)

```cpp
namespace qwhmi::history {

class HistoryRecorder : public QObject {
    // ... D.1 接口不变 ...

    // ★ D.2:心跳策略
    void setHeartbeatInterval(int ms);   // 默认 60000ms,0 = 禁用
    void start();                         // 启动心跳 timer(跟 setSource 解耦,允许 open 后先不监听)
    void stop();                          // 停止心跳 timer
    bool isRunning() const;

private slots:
    // ... onVariableChanged 写 is_heartbeat=0 ...
    void onHeartbeatTick();               // 每 N ms 遍历所有 Variable 各写一行 is_heartbeat=1

private:
    bool migrateAddHeartbeatColumn();     // PRAGMA + ALTER TABLE 幂等升级

    class QTimer* heartbeatTimer_ = nullptr;
    int heartbeatIntervalMs_ = 60000;
};

}
```

---

## 3. 关键设计决策

| 决策 | 理由 |
|---|---|
| hmi.ui 用 QTabWidget 包原 3 widget,不改 objectName | `hmi.cpp` 里 `ui.recvText/auditTable/alarmTable` 引用全部保留,零侵入。教训:UI 容器重构不改子节点名 = 0 处代码改动。 |
| 心跳 timer 用 `Qt::CoarseTimer`(精度 5%,50ms 最小) | `Qt::VeryCoarseTimer` 会被四舍五入到 1000ms,测试 200ms 永远触发不了;CoarseTimer 兼顾生产 60s(±3s)+ 测试 200ms(命中)。 |
| 心跳 60s 一次,生产可调 | 计划文档第 2.3 节锁 60s;测试用 200ms 加速。 |
| ALTER TABLE 加列兼容旧 DB | D.1 时期生成的 history.db 没有 is_heartbeat 列,加默认 0;SQLite ALTER TABLE ADD COLUMN 支持 DEFAULT,无需重写表。幂等:已存在列则 PRAGMA 检测后跳过。 |
| `is_heartbeat=0` 变化检测 / `=1` 心跳 | D.2 Trend 画曲线时两者都画(用户看的是"连续数据");但"健康度检查"功能可只查心跳行 — 为后续 H.2 健康度报表预留。 |
| 心跳启动放在 `setSource` 里 | 历史数据"被记录"的同一时刻,心跳 timer 启动 = 数据流开始计时,语义最干净。 |

---

## 4. 100% 兼容验证

| 项 | 结果 |
|---|---|
| hmi.exe build(MSBuild + QtMsBuild) | ✅ 16.26s,0 警告 0 错误 |
| hmi.exe build(Ninja 测试套) | ✅ 17/17 测试 target 全编 |
| **ctest 全套** | ✅ **17/17 passed(0 failed)**, 2.76s |
| **tst_HistoryRecorder**(10 用例 = 原 8 + 新 2) | ✅ 全过 |
| 14 老测试回归 | ✅ 全过 |
| 旧 DB 升级 | ✅ 手工建无 `is_heartbeat` 列 DB → 打开自动 ALTER 升级 |

| 回归测试 | 通过 |
|---|---|
| tst_Variable / tst_VariableManager / tst_Serializer | ✓ |
| tst_PasswordHasher / tst_UserManager / tst_UserStore | ✓ |
| tst_CurrentUser / tst_Permission / tst_UIPermission | ✓ |
| tst_AuditLog / tst_AuditE2E | ✓ |
| tst_ProjectSchema / tst_IPollDriver / tst_ProtocolSwitch | ✓ |
| **tst_HistoryRecorder**(D.2 新增心跳 2 用例) | **✓** |
| **总计** | **17/17** ✓ |

---

## 5. 数据流(D.2 视角)

```
Variable.setValue(QVariant, checkRange=false)  [PollScheduler 调用]
   ↓ valueChanged 信号
HistoryRecorder::onVariableChanged(const Variable&)
   ↓ shouldWrite(varName, value)  [采样策略检查]
   ↓ QSqlQuery INSERT INTO history (... is_heartbeat=0)
   ↓ 更新 lastWrite_ / lastValue_

[★ D.2 新增]QTimer::timeout (heartbeatIntervalMs_)
   ↓
HistoryRecorder::onHeartbeatTick()
   ↓ 遍历 varMgr_->allVariables()
   ↓ 各 INSERT INTO history (... is_heartbeat=1)  -- "数据流还活着"信号
   ↓
%LOCALAPPDATA%\QianWei\HMI\history.db
   ↓ D.2 Trend 控件用
HistoryRecorder::query(varName, from, to)  [★ 不区分 is_heartbeat,两者都取]
   ↓ QSqlQuery SELECT
   ↓ QVector<QPair<QDateTime, double>>
Trend 控件绘制(QCustomPlot,下一轮接入)
```

---

## 6. 下一步:D.2 Trend 控件主体(QCustomPlot)

按 v1.3 D+E 并行路线图,D.2 主体是 TrendWidget(QCustomPlot + 1h/1d/1w 时间范围 + 历史/实时混合)。

### 6.1 下载 QCustomPlot(LGPL v2.1)
```bash
cd E:\qianwie\hmi\hmi\3rdparty
git submodule add https://github.com/Deruago/qcustomplot.git qcustomplot
# 或:wget https://www.qcustomplot.com/release/2.1.1/QCustomPlot.tar.gz
```

### 6.2 TrendWidget 骨架
- 新建 `hmi/src/widgets/TrendWidget.h/.cpp`(本轮已在 hmi.ui 留 placeholder)
- 头文件:公开 `setVariable(QString)` / `setRange(enum { Hour, Day, Week })` / `setHistoryRecorder(HistoryRecorder*)`
- 持有 1 个 QCustomPlot 实例,默认显示 1 个变量(后续支持多变量叠加)
- 时间范围下拉框:1h / 1d / 1w
- 历史 query → QCustomPlot 的 QCPGraph 加点
- 实时:valueChanged → appendPoint 到当前 QCPGraph 末尾

### 6.3 工程三件套同步 ⚠️ 教训 5.1/1.1
- `hmi/CMakeLists.txt` + `hmi/hmi.vcxproj` + `hmi/hmi.vcxproj.filters` 三件套都加 TrendWidget.cpp/.h
- **vcxproj 必须同时配 `<QtMoc>` + `<ClInclude>` + `<ClCompile>`**(教训 5.1)
- `hmi/tests/CMakeLists.txt` HMI_CORE_SOURCES 加 TrendWidget.cpp(教训 1.1)
- **改 CMakeLists.txt 后必须 `cmake -S . -B build -G "Ninja"` 重 configure**(教训 1.2)
- ⚠️ **PowerShell 编译必须先 vcvarsall**(教训 2.1)

### 6.4 接进主窗口
- `hmi.h` 加 `qwhmi::widgets::TrendWidget* trendWidget_` 成员
- `hmi.cpp` 构造里 new + 把 trendChartPlaceholder QLabel 替换掉(bottomTabs->widget(1)->layout()->replaceWidget)
- ⚠️ **历史数据首次加载可能慢**(几秒到几十秒,看时间窗内行数),建议加进度提示

### 6.5 测试
- `tst_Trend.cpp` 5+ 用例:
  - `queryHistory_returnsNonEmptyWhenDataExists`
  - `setRange_changesWindowCorrectly`
  - `setVariable_replacesGraphCorrectly`
  - `addRealtimePoint_extendsGraph`
  - `clearHistory_clearsGraph`

---

## 7. D.2 起步踩过的 3 个坑(今日教训追加)

1. **`Qt::VeryCoarseTimer` 精度 1000ms**:测试 200ms 永远触发不了 — 必须用 `Qt::CoarseTimer`(50ms 最小)。生产 60s 也够精确(±3s)。
2. **`QSqlDatabase::removeDatabase("legacy_setup")` 报警**:测试里手动 close 后 removeDatabase 时,如果 QSqlQuery 还在用,会告警但不致命。修法:close 后立刻 removeDatabase。
3. **Ninja "build log version is too old"**:改 CMakeLists.txt 或换 vcvarsall 路径后第一次 build 会重置,正常。

---

## 8. 关联文档

- `docs\阶段D.1_从这里继续.md` — D.1 收尾(HistoryRecorder 主体)
- `docs\UI规划调研与方案.md` 第 2 节 — D 阶段总设计(心跳 / Trend UI)
- `docs\进度存档_2026-09-20.md` — 今日 17/17 测试 + 2 commit 全过
- `docs\今日教训_2026-09-18.md` — 5 条教训(QtMoc / Qt6 plugin 等)