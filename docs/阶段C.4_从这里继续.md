# 阶段 C.4 · 从这里继续

> 适用:**C.4 统一协议选择 UI**完成,准备开干 C.5 多 PLC 架构。
> 维护人:Wqian + AI 助手  ｜  创建:2026-09-18
> 配套:`docs/阶段C_详细规划.md` C.4 节 / `docs/阶段C.3_从这里继续.md`
> ⚠ **状态(2026-09-28 基于代码核对)**:✅ **C.4 已落地**(`hmi.hmi::onProtocolComboChanged` + `hmi/src/drivers/ProtocolGuard.{h,cpp}` + `tst_ProtocolSwitch`,14/14 测试全过)。完整阶段状态见 `docs/路线图.md`。

---

## 0. 一句话

**C.4 = 主窗口加 protocolComboBox + 跨协议切工程弹错误 + AuditLog 记录 + 14/14 测试全过**。用户能选 3 协议但不能跨协议切工程(教训 1.5 强制 UX 锁)。

---

## 1. 改了什么(2026-09-18)

| 文件 | 改动 |
|---|---|
| `hmi/hmi.ui` | tcpConnLayout 加 `QComboBox protocolComboBox`(Modbus/S7/EtherNet/IP 三选项) |
| `hmi/hmi.h` | 加 `onProtocolComboChanged(QString)` 槽函数 + `QString currentProtocol_ = "modbus"` 成员 |
| `hmi/hmi.cpp` | 加 `protocolComboBox.currentTextChanged` connect + onProtocolComboChanged 实现(用 ProtocolGuard 纯逻辑)+ QMessageBox 警告 + AuditLog 记录 rejected + 撤销下拉框 |
| `hmi/src/drivers/ProtocolGuard.h` | **新文件**,`isCrossProtocolChange(current, new)` 纯逻辑(可单测,大小写不敏感) |
| `hmi/tests/tst_ProtocolSwitch.cpp` | **新文件**,6 个用例覆盖跨协议/同协议/大小写/空格/3 协议互转 |
| `hmi/tests/CMakeLists.txt` | 加 `add_hmi_test(tst_ProtocolSwitch)` |

---

## 2. UX 流(用户视角)

```
启动 hmi.exe → protocolComboBox 默认 "Modbus"
   ↓
┌─ 用户操作 ───────────────────────────────────────────┐
│ A. 切换协议:                                              │
│    ├─ 同协议切 IP/Port → ✓ 允许                       │
│    └─ 跨协议切(modbus→s7)→ ✗ 弹错误:                   │
│         ┌─────────────────────────────────┐            │
│         │ ✗ 协议不兼容                      │            │
│         │                                   │            │
│         │ 当前工程使用 MODBUS,不能切换到 S7。│            │
│         │ MODBUS 地址(如 0x0002)在 S7 中    │            │
│         │ 没有对应语义。                     │            │
│         │                                   │            │
│         │ 请创建新工程。                      │            │
│         │                                   │            │
│         │        [确定]                      │            │
│         └─────────────────────────────────┘            │
│         + AuditLog: action=protocol.change            │
│           target=modbus detail=modbus→s7              │
│           result=rejected                             │
│         + 下拉框撤销回 modbus                          │
└──────────────────────────────────────────────────────┘
```

## 3. 关键设计决策

| 决策 | 理由 |
|---|---|
| 抽 `ProtocolGuard.h` 纯逻辑函数 | GUI 难单测,纯函数可独立单测(tst_ProtocolSwitch 6 用例)|
| 用 `QString::toLower()` 比较 | "Modbus"/"MODBUS"/"modbus" 等价(用户大小写习惯) |
| 撤销下拉框(`blockSignals` + setCurrentIndex) | 让 UI 状态自洽,不留半截状态 |
| AuditLog `result=rejected` | 区分"协议切换成功"vs"协议切换被阻止",出问题时可查 |
| AuditLog user/role = "system" | 不是用户主动行为,是 UI 守门拦截 |
| 不在 ProtocolGuard 里直接调 AuditLog | 单一职责 — ProtocolGuard 只判断,AuditLog 在 hmi.cpp 调 |

## 4. 100% 兼容验证

| 项 | 结果 |
|---|---|
| hmi.exe 主程序 build | ✅ |
| 14 个 tests build | ✅ 全链接成功 |
| ctest 全套跑 | ✅ **14/14 passed(0 failed)**, 1.91s |
| tst_ProtocolSwitch 6 用例 | ✅ 全过(同协议/跨协议/大小写/空格/3 协议互转) |
| 13 原有测试回归 | ✅ 全过(C.3.1 骨架完全兼容) |

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
| **tst_ProtocolSwitch**(新) | **✓** |
| **总计** | **14/14** ✓ |

## 5. 下一步:**C.5 多 PLC 架构**

按 `docs/阶段C_详细规划.md` 第 C.5 节:

| # | 任务 | 文件 | 估时 |
|---|---|---|---|
| 5.1 | Variable 加 `plcId` 字段 + Project.migrateV2ToV3 | `Variable.h/.cpp` + `Project.cpp` | 0.5 天 |
| 5.2 | PollScheduler 支持多 driver(QHash<QString, IPollDriver*>)| `PollScheduler.h/.cpp` | 3 天 |
| 5.3 | 报警跨 PLC 联动 + 失败降级(重试 3 次 + 新报警 + audit) | `ActionManager.cpp` | 1 天 |
| 5.4 | 写冲突处理(同 Variable 多 driver 写时,后写者覆盖 + audit) | `VariableManager.cpp` | 1 天 |
| 5.5 | tst_MultiPlc 8+ 用例 + 文档 + commit | `tst_MultiPlc.cpp` + `docs/阶段C.5_从这里继续.md` | 2 天 |

**关键约束**(从教训):
- 教训 1.5:1 PLC 挂不影响其他 PLC(故障隔离)— 多 driver 独立 tick
- 规划 C.5 第 360 行:plcId 默认 "plc1"(老工程兼容)

## 6. C.4 踩过的 0 个坑

C.4 复用 C.2.1/C.3.1 的教训,**零新坑**。值得记录的:
- ProtocolGuard 抽离让单测可行(教训 1.6:抽象层接口必须可独立测试)

## 7. 关联文档

- `docs/阶段C_详细规划.md` C.4 节 + 第 5.5 节(配置驱动)
- `docs/阶段C.3_从这里继续.md` — C.3.1 骨架(直接复用)
- `docs/今日教训_2026-09-17.md` — 教训 1.5/1.6
