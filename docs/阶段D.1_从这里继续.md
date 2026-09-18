# 阶段 D.1 · 从这里继续

> 适用:**D.1 SQLite 历史库**完成,准备开干 D.2 Trend 控件(或并行 E.1 画布工具)。
> 维护人:Wqian + AI 助手  ｜  创建:2026-09-18
> 配套:`docs/UI规划调研与方案.md` 第 2 节 / `docs/阶段C_详细规划.md` / `docs/阶段C.5_从这里继续.md`

---

## 0. 一句话

**D.1 = SQLite 历史库骨架(HistoryRecorder 类)+ 6 个单元测试全过 + hmi.exe 接入并自动启动**。所有 Variable 的 valueChanged 自动写入 `%APPDATA%\QianWei\HMI\history.db`,支持采样间隔 + 变化阈值 + 环形缓冲。

---

## 1. 改了什么(2026-09-18)

| 文件 | 改动 |
|---|---|
| `hmi/src/history/HistoryRecorder.h` | **新文件**,HistoryRecorder 类(open/close/setSource/sampleInterval/changeThreshold/cleanupExpired/query) |
| `hmi/src/history/HistoryRecorder.cpp` | **新文件**,SQLite 操作 + 采样策略 + 环形缓冲 + query 接口 |
| `hmi/hmi.h` | 加 `qwhmi::history::HistoryRecorder* historyRecorder_` 成员 |
| `hmi/hmi.cpp` | 构造里 new HistoryRecorder + open(%APPDATA%) + setSource(varMgr) |
| `hmi/CMakeLists.txt` | 加 `Qt6::Sql` 到 find_package + target_link_libraries |
| `hmi/tests/CMakeLists.txt` | 加 `Qt6::Sql` + `HistoryRecorder.cpp` + `add_hmi_test(tst_HistoryRecorder)` |
| `hmi/hmi.vcxproj` + `filters` | 同步 2 个新条目(ClInclude + ClCompile) |
| `hmi/tests/tst_HistoryRecorder.cpp` | **新文件**,6 个用例(openClose/valueChanged/sampleInterval/changeThreshold/query/cleanupExpired) |

## 2. HistoryRecorder 接口签名

```cpp
namespace qwhmi::history {

class HistoryRecorder : public QObject {
    Q_OBJECT
public:
    explicit HistoryRecorder(QObject* parent = nullptr);
    ~HistoryRecorder() override;

    bool open(const QString& dbPath);   // 打开/创建 SQLite
    void close();
    bool isOpen() const;

    // 监听 VariableManager 数据源(自动 connect 所有 Variable 的 valueChanged)
    void setSource(VariableManager* varMgr);

    // 采样策略
    void setSampleInterval(int ms);       // 默认 1000ms
    void setChangeThreshold(double t);   // 0 = 不启用
    void setRetentionDays(int days);     // 默认 30 天

    void cleanupExpired();   // 手动清理 > N 天记录

    // D.2 Trend 控件用
    QVector<QPair<QDateTime, double>> query(
        const QString& variableName,
        const QDateTime& from,
        const QDateTime& to);

    static QString defaultDbPath();   // %APPDATA%\QianWei\HMI\history.db
};

}
```

## 3. 关键设计决策

| 决策 | 理由 |
|---|---|
| 用 `QSqlDatabase` named connection "hmi_history" | 避免多线程/多 driver 冲突,close 时 `removeDatabase` 清理 |
| 表 schema:history(id, var_name, timestamp_ms, value, quality) | 5 字段够用,加索引 `(var_name, timestamp)` 让 query 快 |
| 采样策略:距离上次写 < N ms 跳过 + 变化 < 阈值跳过 | 防高频写入打满 IO;Float 精度抖动不写 |
| 环形缓冲:`cleanupExpired()` 按 timestamp 删除 > N 天 | 防止数据库无限增长 |
| 默认路径 `%APPDATA%\QianWei\HMI\history.db` | 跟 hmi.log 同目录,运维简单 |
| listen `variableReplaced` 自动换绑 | 老 Variable 析构 → 自动 connect 新 Variable(类似其他 Listener 模式) |

## 4. 100% 兼容验证

| 项 | 结果 |
|---|---|
| hmi.exe 主程序 build | ✅ |
| 15 个 tests build | ✅ |
| ctest 全套跑 | ✅ **15/15 passed(0 failed)**, 2.72s |
| tst_HistoryRecorder 6 用例 | ✅ 全过 |
| 14 老测试回归 | ✅ 全过 |

| 回归测试 | 通过 |
|---|---|
| tst_Variable / tst_VariableManager / tst_Serializer | ✓ |
| tst_PasswordHasher / tst_UserManager / tst_UserStore | ✓ |
| tst_CurrentUser / tst_Permission / tst_UIPermission | ✓ |
| tst_AuditLog / tst_AuditE2E | ✓ |
| tst_ProjectSchema | ✓ |
| tst_IPollDriver | ✓ |
| tst_ProtocolSwitch | ✓ |
| **tst_HistoryRecorder**(新 6 用例) | **✓** |
| **总计** | **15/15** ✓ |

## 5. 数据流

```
Variable.setValue(QVariant, checkRange=false)  [PollScheduler 调用]
   ↓ valueChanged 信号
HistoryRecorder::onVariableChanged(const Variable&)
   ↓ shouldWrite(varName, value)  [采样策略检查]
   ↓ QSqlQuery INSERT INTO history
%APPDATA%\QianWei\HMI\history.db
   ↓ D.2 Trend 控件用
HistoryRecorder::query(varName, from, to)
   ↓ QSqlQuery SELECT
   ↓ QVector<QPair<QDateTime, double>>
Trend 控件绘制
```

## 6. 下一步:D.2 Trend 控件(或并行 E.1 画布)

按 v1.3 D+E 并行路线图,可任选:

### D.2 Trend 控件(2 周)
- 选 QCustomPlot 或 QtCharts(锁定用 QCustomPlot,LGPL,无需 QtCharts 商业许可)
- 控件继承 QGraphicsItem(E.1 画布也能复用)
- 监听 VariableManager::valueChanged → 实时绘制数据点
- 历史曲线:从 HistoryRecorder.query 取历史数据
- 时间范围选择(1h / 1d / 1w)
- 写 `docs/阶段D.2_从这里继续.md`

### E.1 画布 + 工具箱(2 周,可与 D.2 并行)
- hmi-editor.exe 独立 exe(CMake add_subdirectory)
- QGraphicsView + QGraphicsScene 画布
- 5 个控件图标(Gauge / Bar / Button / Label / Indicator)
- 拖控件到画布
- 写 `docs/阶段E.1_从这里继续.md`

## 7. D.1 踩过的 4 个坑(今日教训追加)

1. **QTEST_APPLESS_MAIN 不创建 QCoreApplication** → QSqlDatabase crash → 改 `QTEST_GUILESS_MAIN`
2. **Variable::setValue 类型严格匹配** → `setValue(123.45)` 被 Rejected → 用 `QVariant::fromValue<float>(123.45f)`
3. **VariableManager 默认空** → setSource 时 `allVariables()` 返空 → 必须 `addVariable` 在 setSource 之前
4. **Float 精度丢失**(SQLite REAL 存 float) → `qFuzzyCompare` 不工作 → 改 `qAbs(a-b) < 0.001`

## 8. 关联文档

- `docs/UI规划调研与方案.md` 第 2 节 D 阶段规划
- `docs/阶段C.5_从这里继续.md` — C 阶段收尾(已含 plcs[] 字段)
- `docs/今日教训_2026-09-18.md` — 5 条教训
