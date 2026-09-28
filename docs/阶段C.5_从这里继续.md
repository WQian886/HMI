# 阶段 C.5 · 从这里继续

> 适用:**C.5 多 PLC 架构**骨架完成(Variable.plcId + PollScheduler 多 driver),准备开干 C.6a 真机验证。
> 维护人:Wqian + AI 助手  ｜  创建:2026-09-18
> 配套:`docs/阶段C_详细规划.md` C.5 节 / `docs/阶段C.4_从这里继续.md`
> ⚠ **状态(2026-09-28 基于代码核对)**:✅ **C.5 已落地**(`Variable::plcId_` 字段 + `PollScheduler::drivers_` QHash + 14/14 测试全过)。报警跨 PLC 联动 + 写冲突处理 + `tst_MultiPlc` 仍推迟到真机阶段(C.6a)。完整阶段状态见 `docs/路线图.md`。

---

## 0. 一句话

**C.5 骨架 = Variable 加 plcId 字段 + PollScheduler 多 driver(独立 tick + 故障隔离)+ 14/14 回归全过**。报警跨 PLC 联动 + 写冲突处理 + tst_MultiPlc 推迟到 C.5.x(真机阶段)。

---

## 1. 改了什么(2026-09-18)

| 文件 | 改动 |
|---|---|
| `hmi/Variable.h` | 加 `QString plcId_` 字段(默认 `"plc1"` 老工程兼容)+ getter `plcId()` + setter `setPlcId()` |
| `hmi/PollScheduler.h` | 单 `IPollDriver* driver_` → `QHash<QString, IPollDriver*> drivers_` + 多 driver 构造重载 + `addDriver()` / `removeDriver()` + `pendingPlcId_` 分派表 + `onDriverReadFinished` 签名加 plcId |
| `hmi/PollScheduler.cpp` | 构造支持单/多 driver;`onTick` 按 plcId 分组 + per-plc driver 检查 isConnected/pendingCount(故障隔离);readFinished 用 lambda 捕获 plcId |

## 2. 关键设计决策

| 决策 | 理由 |
|---|---|
| `Variable.plcId_` 默认 `"plc1"` | 老工程兼容(所有变量当 plc1)— C.0 schema migrateV2ToV3 自动补 |
| 单 driver 构造保留 | 向后兼容老调用方(单 PLC 行为不变)— 包成 `drivers_["plc1"] = driver` |
| `legacyDriver_` 指向 drivers_.values().first() | 兼容老字段访问(`driver_` 改名为 `legacyDriver_` 是 BC break) |
| `pendingPlcId_` 单独 hash | reqId → plcId 分派,让 readFinished 知道 reply 是哪个 plc 的 |
| onTick 按 plcId 分组 + per-plc driver 检查 | 1 个 plc 挂不影响其他 plc(教训 1.5:故障隔离) |
| driver 没注册 plc → 变量全部 Bad | 防御:变量配错 plcId 时不会静默不读 |
| driver 没连 → 只该 plc 变量 Bad | 故障隔离核心 |
| driver pendingCount > 0 → 100ms 后重试 | 串行协议(Modbus)单 in-flight 防御 |

## 3. 架构前后对比

### C.5 之前(单 driver)

```
hmi.exe → IPollDriver* (1 个)
       → PollScheduler (单 driver_)
       → 1 台 PLC
```

### C.5 之后(多 driver)

```
hmi.exe → QHash<QString, IPollDriver*> drivers_
       ├─ "plc1": ModbusDriver → 192.168.1.10:502
       ├─ "plc2": S7Driver     → 192.168.1.20:102
       └─ "plc3": EipDriver    → 192.168.1.30:44818
       → PollScheduler(每个 driver 独立 tick + 故障隔离)
       → N 台 PLC
```

### Variable.plcId 决定数据归属

```cpp
// 之前
Variable{"温度", addr=0x0002}  // 不知道是哪台 PLC

// 之后
Variable{"温度", plcId="plc1", addr=0x0002}  // 明确属于 plc1
Variable{"压力", plcId="plc2", addr=DBW0}    // 明确属于 plc2
```

## 4. 100% 兼容验证

| 项 | 结果 |
|---|---|
| hmi.exe 主程序 build | ✅ 1.55 MB(Ninja,vcvarsall x64) |
| 14 个 tests build | ✅ 全链接成功 |
| ctest 全套跑 | ✅ **14/14 passed(0 failed)**, 1.78s |
| tst_IPollDriver 13 用例 | ✅ 全过(单 driver 构造兼容路径) |
| tst_ProtocolSwitch 6 用例 | ✅ 全过 |

| 回归测试 | 通过 |
|---|---|
| tst_Variable | ✓ |
| tst_VariableManager | ✓ |
| tst_Serializer | ✓ |
| tst_PasswordHasher | ✓ |
| tst_UserManager | ✓ |
| tst_UserStore | ✓ |
| tst_CurrentUser | ✓ |
| tst_Permission | ✓ |
| tst_UIPermission | ✓ |
| tst_AuditLog | ✓ |
| tst_AuditE2E | ✓ |
| tst_ProjectSchema | ✓ |
| tst_IPollDriver | ✓ |
| tst_ProtocolSwitch | ✓ |
| **总计** | **14/14** ✓ |

## 5. 下一步:**C.5.x 推迟到真机 + C.6a 真机验证**

按 `docs/阶段C_详细规划.md` 第 C.5.3/C.5.4/C.5.5 节:

### C.5.x 待办(推迟到真机阶段)

| # | 任务 | 估时 | 阻塞 |
|---|---|---|---|
| 5.3 | 报警跨 PLC 联动 + 失败降级(重试 3 次 + 新报警 + audit) | 1 天 | 需要真 PLC 数据(规划 C.5 第 363-364 行) |
| 5.4 | 写冲突处理(同 Variable 多 driver 写) | 1 天 | 需要 2 个 driver 同时写(规划 C.5 第 366 行) |
| 5.5 | tst_MultiPlc 8+ 用例 + 文档 + commit | 2 天 | 需要多 driver 实际场景 |

**为什么推迟**:
- 没真 PLC 数据时,**报警联动测不出真效果**(联动了看不到后果)
- 写冲突需要 2 个 driver 同时写同一变量,现在只有 stub driver 测不出
- ActionManager / WriteScheduler 是 B 阶段代码,改大风险高

### C.6a 真机验证(下一步,推荐)

按 `docs/阶段C_详细规划.md` 第 C.6a 节:
- 3 仿真器连通(S7-PLCSIM + ModbusSlave + Logix Designer)
- 3 协议 × 4 角色端到端测试
- 24h 长跑稳定性
- 写 `docs/阶段C.6a_真机验证报告.md`

**前置依赖(Wqian 必须准备)**:
- Snap7 LGPL DLL → `hmi/3rdparty/snap7/{include,lib/x64,bin/x64}`
- libplctag LGPL DLL → `hmi/3rdparty/libplctag/{include,lib/x64,bin/x64}`
- S7-PLCSIM 仿真器(西门子官网)
- ModbusSlave 仿真器(可免费)
- Logix Designer 试用申请(7 天限制,提前 1 周)

## 6. C.5 踩过的 1 个坑(今日教训追加)

**坑**:`QHash::first()` 不存在 — Qt 6 改了接口,我写 `drivers_.first()` 编译报 C2039。
**教训**:Qt 6 的 QHash 没有 STL 风格的 `first()`,要用 `drivers_.values().first()` 或 `drivers_.begin().value()`。
**怎么避免**:看 Qt 6 文档,或者用 begin/end 迭代器。

## 7. 关联文档

- `docs/阶段C_详细规划.md` C.5 节
- `docs/阶段C.4_从这里继续.md` — C.4 协议 UI(直接复用)
- `docs/今日教训_2026-09-17.md` — 教训 1.5(1 driver = 1 PLC)
- `docs/今日教训_2026-09-18.md` — 教训 5.1(vcxproj QtMoc)
