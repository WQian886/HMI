# CHANGELOG

> 最后核对：**2026-09-28**（基于代码核对：E.4d-follow-7 = `3473fa6` 是当前稳定基线，19 个 commit 全在 E.4d；B/C/D 阶段代码已全部落地，A 阶段 8/8 验证早已通过）

## [0.2.0-rc1] - 2026-09-28

### 阶段 A · 工程化基线 · **验证通过** 🎉（补档）
- A 阶段 8/8 验收已于 2026-09-11 通过，2026-09-16 补做 A2-5（崩溃 dump）和 A3（日志文件验证）。详细记录保留在 [0.1.0] 节。

### 阶段 B · 用户/角色/权限 + 操作审计 · **代码落地 ✅**
- 数据模型：`src/auth/{Role,User,PasswordHasher}.{h,cpp}`
- 持久化：`src/auth/{UserStore,UserManager}.{h,cpp}`（JSON + load/save + 4 signals）
- 对话框全套：`LoginDialog` / `AdminSetupDialog` / `ChangePasswordDialog` / `NewUserDialog` / `UserManagementDialog` / `InactivityWatcher`（5 分钟超时 + 自动登出重登）
- 权限：`Permission`（enum + checkPermission）/ `UIPermission`（bindButtonEnable + bindActionEnable）
- 审计：`AuditLog`（单例 + 按天滚动 + 7 类事件入口）
- 测试：`tests/tst_{PasswordHasher,UserManager,UserStore,CurrentUser,Permission,UIPermission,AuditLog,AuditE2E}.cpp`

### 阶段 C · 多协议 + 多 PLC · **代码落地 ✅（ModbusDriver）/ ⚠️ S7/EIP stub**
- 抽象层：`src/drivers/{IPollDriver,DriverFactory}.{h,cpp}`
- Modbus：`src/drivers/ModbusDriver.{h,cpp}`（包装 ModbusMaster + 工厂选择）
- S7：`src/drivers/S7Driver.{h,cpp}` ⚠️ **当前是 stub**：`connectTo/sendRead/sendWrite` 都是 `qWarning + emit errorOccurred`，`loadSnap7Library()` 直接 return false。Snap7 DLL 没真动态加载（C.2.2 TODO）
- EtherNet/IP：`src/drivers/EipDriver.{h,cpp}` ⚠️ **当前是 stub**：libplctag LGPL 库未真接
- 协议 UX 锁：`src/drivers/ProtocolGuard.{h,cpp}` + 单测 `tst_ProtocolSwitch`

### 阶段 D · 历史库 + 曲线 + 报表 + Tag 增强 · **代码落地 ✅**
- 历史：`src/history/{HistoryRecorder,AlarmRecorder}.{h,cpp}`（SQLite + 心跳 60s + 30 天保留）
- 曲线：`src/widgets/TrendWidget.{h,cpp}` + `TrendControl`（QCustomPlot）
- 导出：`src/export/CsvExporter.{h,cpp}`（QTextStream 零三方依赖）
- 表达式：`src/tag/ExpressionParser.{h,cpp}`（Pratt 手写解析 + 依赖收集）
- 标签导入：`src/tag/TagImporter.{h,cpp}`（CSV-based）
- 测试：`tst_{HistoryRecorder,AlarmRecorder,CsvExporter,ExpressionParser,TagImporter,Trend}.cpp`

### 阶段 E · 画面框架(控件库 + WYSIWYG 编辑器)· **🟡 收尾中（E.4d）**

#### 控件库（E.1）✅ 18 控件
- 基类：`src/widgets/base/ControlBase.{h,cpp}`（E.4d-follow-6 已修 setPos 路径不触发 positionChanged）
- 静态显示：`LabelControl` / `NumberDisplay` / `TextDisplay` / `BarControl` / `GaugeControl` / `LedControl` / `ImageControl` / `RectangleControl` / `LineControl` / `GroupControl`
- 控制：`ButtonControl` / `ToggleControl` / `SliderControl` / `InputEditControl`（含 placeholder 实功能）
- 数据：`TrendControl` / `TableControl` / `AlarmListControl`（E.4b 已修 proxy widget 缩放/选不中 bug）

#### 编辑器（E.2–E.4d）✅ 核心就位，🟡 视觉/对齐细节收尾
- 主窗口：`src/editor/EditorMainWindow.{h,cpp}`（6 对齐 + 3 等尺寸 + JSON 存读 + undo/redo）
- 属性面板：`src/editor/PropertySheet.{h,cpp}`（反射驱动 + E.4d-follow-2 多选共同属性）
- 撤销栈：`src/editor/PropertyChangeCommand.{h,cpp}`（QUndoCommand + 200ms 合并）
- 复合编辑器：`src/editor/{SizeEditor,PositionEditor}.{h,cpp}`（E.4c QSizeF/QPointF）
- 调色板：`src/editor/PaletteWidget.{h,cpp}`（E.4a 拖拽 + mime type 注入）
- 缩放手柄：`src/editor/{ResizeHandleItem,GroupHandleItem}.{h,cpp}`（E.4b 8 handle + Shift 等比 + E.4d-follow-3 多选组 handle）
- 选中框：`src/editor/SelectionFrameItem.{h,cpp}` 🟡 **视觉延后**：paint() 缺蓝色虚线，对应 `bug图/图1.png`（SelectionFrame 框在控件内部）

#### E.4d follow-up 链路（最近 19 个 commit 全在这条线上）
| commit | 类型 | 摘要 |
|---|---|---|
| `3473fa6` | feat | E.4d-follow-7 multi-select same width/height/size actions |
| `4735de0` | fix | E.4d-follow-6 ControlBase.itemChange emits positionChanged on setPos path（修 `bug图/12.png / 12 (2).png`） |
| `8a05de4` | fix | E.4d-follow-5 selection frame follows multi-move / resize |
| `4804bec` | fix | E.4d-follow-4 remove redundant multiMove + regression test |
| `3163532` | fix | E.4d-follow-3 group handle position bug（修 `bug图/图6/9/10/11.png`） |
| `e76115b` | feat | E.4d-follow-3 multi-select group resize handles（修 `bug图/图7.png`） |
| `32e6604` | feat | E.4d-follow-2 PropertySheet multi-select common properties |
| `f77592a` | feat | E.4d follow-up SelectionFrameItem skeleton（视觉 deferred，`bug图/图1.png` 待修） |
| `feb5fef` | test | E.4d real-click multi-select regression + cleanup |
| `8f8d61b` | fix | add E.4c SizeEditor/PositionEditor to hmi.vcxproj |
| `14e19aa` | feat | E.4d multi-select + 6 alignment actions |
| `181969c` | feat | E.4c PropertySheet QSizeF/QPointF composite editors |
| `870f846` | fix | proxy widget resize on shrink (AlarmList/Trend/Table visible-shrink)（修 `bug图/图2/3.png`） |
| `cb5529e` | fix | ControlBase.setSize prepareGeometryChange + width/height Q_PROPERTY override |
| `3372f67` | fix | handle visual edge alignment + add 5 shrink-direction regression tests |
| `04b3ab6` | fix | proxy widget 吞事件 → AlarmList/Trend/Table 选不中 |
| `ca7b50a` | fix | TrendControl/AlarmListControl 缩放跟 widget bug |
| `bf1388a` | feat | E.4b 8 handle 缩放 + Shift 等比 |
| `0c5396f` | feat | E.4a 控件调色板 + 拖拽 + 接入 hmi.exe |

### F–I 阶段 · ⬜ 未启动
- F 报警（商业级）、G OPC UA + REST、H 配方 + 报表、I 安装包 + i18n

### 改善清单 30 条进度：8 ✅ / 1 🟡 / 21 ⬜
- ✅ #8 appendRecv 限大 / #19 CMakeLists 补 Binding/WriteScheduler / #20 build.cmd 移到 scripts / #21 main try-catch / #22 setApplicationName / #23 qDebug QLoggingCategory / #26 inventory/HMI.md 作废 / #29 Mapping 改存变量名
- 🟡 #24 QSettings（仅自启注册表，主流程未用）
- ⬜ 其余 21 条见 `docs/改善清单.md`

---

## [0.1.0] - 2026-09-11

### 阶段 A · 工程化基线 · **验证通过** 🎉

**A 阶段验收状态:8/8 项通过** 🎉(详见下文)

#### 新增(代码落地 2026-09-08)
- 安装包脚本(Inno Setup):`installer/hmi.iss`
- 单实例检测:`src/instance/SingleInstance.{h,cpp}`
- 系统托盘:`src/tray/SystemTray.{h,cpp}`
- 日志系统(QLoggingCategory + 每日滚动 + 30 天保留):`src/logging/Logging.{h,cpp}`
- 崩溃 dump(MiniDumpWriteDump):`src/crash/CrashHandler.{h,cpp}`
- 单元测试(Variable / VariableManager / Serializer):`tests/tst_*.cpp`
- CI(GitHub Actions windows-2022 + Qt 6.8.3):`.github/workflows/build.yml`
- 静态分析配置:`.clang-tidy`, `.clang-format`
- 阶段 A 文档:`docs/阶段A_工程化基线.md`
- **vcxproj 同步脚本**:`scripts/sync_vcxproj.py`(标准库 only,自动扫描源码 vs vcxproj 差异)

#### 改动
- `main.cpp`:重写,集成 logging + crash + single instance + tray + 自启注册表
- `CMakeLists.txt`:GLOB → GLOB_RECURSE(子目录自动捡)
- **vcxproj**:A 阶段 8 个新文件 + DbgHelp.lib 链接器
- **vcxproj**:删除重复的 `<ClInclude>` 标签(`<QtMoc>` 已隐含 `<ClInclude>` 语义,重复注册会触发 VS 加载失败)
- **子系统**:Debug + Release 都改 Windows 子系统(启动无控制台,qDebug 输出到日志)
- **README.md**:学习路线小节加 v0.2.0 锚点,目录树反映 `hmi/hmi/` 单层结构(2026-09-08 仓库根上移重构)

#### 验证记录(2026-09-11)

| # | 验收项 | 结果 |
|---|---|---|
| A1 | F5 编译通过 | ✅ |
| A2-1 | 启动 + 托盘图标 | ✅ |
| A2-2 | 关主窗口不退出 | ✅ |
| A2-3 | 单击托盘显隐切换 | ✅ |
| A2-4 | 单实例检测(退出码 0)| ✅ |
| **A4** | **ctest 3 测试全绿** | **✅ 100% pass** |
| A2-5 | 主动崩溃 → dump 文件 | ✅ 已验证(2026-09-16,见 dump/crash_20260916_093410_510.dmp,exception_code=0xC0000005) |
| A3 | 日志文件验证 | ✅ 已验证(2026-09-16,见 logs/hmi_20260916_094322.log,5 行 [INFO] 格式正常;30 天清理机制由 24h QTimer 触发,首次清理要等 24h) |

#### 已知问题 (Known Issues)
- **A2-2 气泡提示缺失**:`hmi.cpp::closeEvent` 未 override,点 X 关主窗口不会触发 `hideRequested` 信号 → 不会弹"程序已隐藏到托盘"气泡。**临时接受**:用户已确认按 A 阶段原始标准(关窗口不退出)通过。**正式修法**:A 阶段回顾时统一补,5 行 closeEvent override。
- **Inno Setup installer 未实际编译产出**:脚本就绪,但 windeployqt + ISCC 实际打包放到 I 阶段。
- **vcxproj 手动同步**:虽有 `scripts/sync_vcxproj.py` 自动报告差异,但 vcxproj 实际写入仍需手动(或用脚本的 `--apply`)。文档同步约定见 `docs/现状.md` 第六节。

#### 跳过项说明
- A2-5 / A3 已于 2026-09-16 补做,详见上方表格。
- A3 中"30 天清理"机制由 24h QTimer 首次触发后才能真实验证,需 HMI 长跑 24h 后回头核对 `logs/` 目录。

---

## [0.0.x] - 2026-08 及以前

阶段 0~6 完成(详见 `docs/现状.md`)
- 阶段 0: Qt 热身
- 阶段 1: TCP 客户端(超时 + 重连 + 心跳)
- 阶段 2: Modbus RTU/TCP(0x01~0x10 + 异常)
- 阶段 3: 变量模型(订阅发布 + 轮询 + 死区)
- 阶段 4: 控件绑定(变量 ↔ UI 双向)
- 阶段 5: 业务层(状态机 + 报警 + 联动)
- 阶段 6: 工程文件(JSON 存读) — 6.5 端到端验收待做

---

## 下一步 · E.4d 收尾 + 阶段 F 启动决策

E.4d 收尾（约 2-4 小时）：
- 补 `SelectionFrameItem.cpp` 的 `paint()` 视觉（对应 `bug图/图1.png`）
- vcxproj.filters 给 E.4d 新增 4 个 .cpp 补 `<Filter>Source Files</Filter>`
- CMakeLists.txt 显式 SOURCES/HEADERS 加 `SelectionFrameItem`、`GroupHandleItem`

阶段 F 启动（商业级报警，2-3 周 P1）：
- Wqian 一句话"阶段 F 启动" → 展开 F 工单
- 详见 `docs/路线图.md`
