# 阶段 C.0 · 从这里继续

> 适用:**C.0 工程文件 schema 演进**完成,准备开干 C.1 协议抽象层。
> 维护人:Wqian + AI 助手  ｜  创建:2026-09-17
> 配套:`docs/阶段C_详细规划.md` / `docs/UI规划调研与方案.md`

---

## 0. 一句话

**C.0 = 工程文件 schema 演进框架就绪**(老 default.json 自动迁移到 v2,不丢数据)。

---

## 1. 改了什么

| 文件 | 改动 |
|---|---|
| `hmi/Project.h` | 加 `static constexpr int kCurrentSchemaVersion = 2` + 公开 `migrateTo` / `migrateV1ToV2` |
| `hmi/Project.cpp` | `saveToFile` 写 schema_version=2(数字) + `plcs[0]` 顶级数组;`loadFromFile` 检测旧版本自动调迁移;实现 `migrateV1ToV2` 和 `migrateTo` 调度器 |
| `hmi/tests/tst_ProjectSchema.cpp` | **新文件**,12 个用例 |
| `hmi/tests/CMakeLists.txt` | 加 Project + ActionManager + ThresholdAlarmChecker + TcpClient + Alarm + AlarmManager + PollScheduler + ModbusMaster + WriteScheduler(9 个 cpp);Qt6::Network + Qt6::SerialBus |

---

## 2. v1 → v2 字段映射

| v1(老) | v2(新) | 迁移行为 |
|---|---|---|
| `"schema_version": "1.0"`(字符串) | `schema_version: 2`(数字) | 类型 + 值都变 |
| `communications.protocol` | `plcs[0].protocol` | 提取到顶级数组 |
| `communications.host/port` | `plcs[0].host/port` | 同上 |
| `communications.modbus` | `plcs[0].modbus` | 整体搬迁 |
| `communications` 字段本身 | (删除) | 冗余 |
| `variables[].plcId` | (待 C.5) | C.0 没加,所有变量默认属于 plc1 |

## 3. 关键设计原则(3 条)

1. **内存迁移, 不立即写回**:用户主动 save 才落盘成 v2(防误改)
2. **失败返 false, 不丢数据**:`loadFromFile` 报 `loadError`,原文件不动
3. **缺字段用默认值**:`communications.modbus` 缺失时自动补 timeout=3000 / max_retries=5 / poll=100ms

---

## 4. 测试覆盖矩阵(12 用例)

| # | 用例 | 验什么 |
|---|---|---|
| 1 | migrateV1ToV2_addsPlcsField | v2 有 plcs[0] |
| 2 | migrateV1ToV2_convertsSchemaVersionToNumber | "1.0" → 2 |
| 3 | migrateV1ToV2_removesCommunicationsField | 删冗余字段 |
| 4 | migrateV1ToV2_preservesModbusSubObject | 保留 3 个 timeout 字段 |
| 5 | migrateV1ToV2_usesDefaultsWhenModbusMissing | 缺 modbus 补默认 |
| 6 | migrateV1ToV2_handlesMissingCommunicationsGracefully | 最老格式不崩 |
| 7 | migrateTo_chainsFromV1ToV2 | 链式调用 |
| 8 | migrateTo_returnsFalseOnUnknownVersion | 未知版本不丢数据 |
| 9 | saveToFile_writesSchemaVersion2 | 写出数字 2 |
| 10 | saveToFile_writesPlcsArrayNotCommunications | 写 plcs[0] 不写 communications |

**总测试数 80/80 全过**(12 新增 + 68 回归)。

---

## 5. 下一步:**C.1 协议抽象层**

按 `docs/阶段C_详细规划.md` 第 C.1 节:
- 1 周纯代码,**不需要 PLC 设备**
- 抽出 `IPollDriver` 通用接口(`read(area, addr, count)` 支持 4 种 area)
- 包装 `ModbusMaster` 为 `ModbusDriver`(100% 行为兼容)
- 加 `DriverFactory` 工厂模式
- 改 `PollScheduler` 用 `IPollDriver*` 指针
- 写 `tst_IPollDriver`(用 Mock driver 验证 PollScheduler 不依赖具体协议)

**关键约束**:**ModbusDriver 必须 100% 兼容 ModbusMaster,所有现有 68 测试无改动通过**。

---

## 6. C.0 踩过的 4 个坑

1. **namespace 误用**:Project 类在全局命名空间(不是 `qwhmi::`),`Project p(...)` 不能加 `qwhmi::`
2. **Qt6 模块漏链**:`Project.cpp` 间接 include QTcpSocket + QModbusDataUnit → tests 加 `Qt6::Network` + `Qt6::SerialBus`
3. **链接全套 cpp**:`Project` 依赖链长(VariableManager/AlarmManager/ActionManager/PollScheduler/AlarmChecker/TcpClient/ModbusMaster/WriteScheduler),tests 都要加
4. **文件冲突**:`QTemporaryFile` + `QSaveFile` 同路径冲突 → 改用固定路径 + `QFile::remove` 清理

---

## 7. 迁移策略(用户视角)

```
用户升级 hmi.exe (B 阶段 → C 阶段)
   ↓
打开上周的 default.json
   ↓
hmi.exe 检测 schema_version = "1.0"
   ↓
自动 migrateV1ToV2(内存里)
   ↓
通讯字段从 communications → plcs[0]
   ↓
工程照常打开,用户无感知
   ↓
用户主动 save → 文件升级为 v2 格式(老 v1 文件不立即动)
```

---

## 8. 关联文档

- `docs/阶段C_详细规划.md` — C.0 + 后续 6 里程碑详细分解
- `docs/UI规划调研与方案.md` — D/E 阶段 UI 规划
- `docs/现状.md` — 当前架构
- `docs/路线图.md` — 9 阶段总览
