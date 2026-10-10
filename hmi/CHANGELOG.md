# CHANGELOG

> 最后核对：**2026-10-10**（基于代码核对：`ctest` **67 个目标 67 通过**，
> `hmi.vcxproj` 经 **MSBuild** 编译通过、`hmi.exe` 产出）

## [Unreleased] - 2026-10-10 (六)

### TcpClient 重连:"放弃"之后客户端自己又活了(改善清单 #2)

**新开 `tst_TcpClient`** —— `TcpClient` 从阶段 1 起就是核心类，
此前**一个测试文件都没有**，重连/退避/放弃整套逻辑从没被验证过。

Qt 连接失败时的信号顺序是**确定的**：`errorOccurred` 先，`disconnected` 紧跟其后，
两个槽都调 `scheduleReconnect()`。平时靠 `reconnectTimer_->isActive()` 挡住第二条。
但"重试到上限放弃"那条分支**故意没有启动定时器** —— 防护失效了；
那条分支又顺手把 `retryCount_` 清成 0，于是紧接着的 `disconnected`
看到 `retryCount_=0 < 5`，**又排了第 1 次重连**。

后果不是"偶尔多试一次"：**每一轮放弃后都重启，客户端永远停在"重连中"，
界面上"连接失败"一闪而过** —— 恰好是操作员最该看见的那句话。
而且没有任何人按过重连按钮，是它自己活过来的。

改法：加 `reconnectAbandoned_` 标志，置位后 `scheduleReconnect()` 直接返回。
标志由 `connectTo()`（用户手动重连）和 `onSocketConnected()`（连上了）清掉 ——
**不能做成永久状态**，那比原 bug 更严重（机器再也连不上）。

负向对照：去掉守卫后，只有 `abandoned_thenDisconnected_doesNotRestartRetryCycle`
和 `abandoned_emitsAbandonedOnlyOnce` 两条转红（`abandoned.count()` 变成 2 和 4，
正是"放弃 → 重启 → 再放弃"的循环）；
`abandoned_clearedWhenUserConnectsAgain` **保持绿**，证明反向没被误伤。

### 改善清单 #10 复核结论：这条清单是错的，照做会让程序变慢

清单原话"`setSocketOption` 每次都设 → 移到构造"，听起来是纯粹的清理。实测不是：
在**未连接**的 `QTcpSocket` 上设 `LowDelayOption` 是 **no-op**，Qt 直接丢弃这个请求。
把 `onSocketConnected` 里那行挪进构造函数后，连上再读 `socketOption` 拿到的是 **0**
—— Nagle 根本没被关掉，每个小 Modbus 帧又慢 ~40ms，而**代码上看不出任何变化**。

维持原样，并在构造函数处写了长注释防止下一轮再被"优化"回去。
`lowDelayOption_mustBeSetAfterConnect` 用两个**与 TcpClient 无关的裸 socket 探针**
隔离验证（第一次想直接在 TcpClient 上断言，被自己先红了一次：
`onSocketConnected` 本来就在设 1，分不清是哪一次设的 —— 分不清来源的断言不算断言）。

验证：`ctest` **67/67 通过**、`hmi.exe` 编译通过（6.67 MB）。

## [Unreleased] - 2026-10-10 (五)

### F6 区域过滤 —— 卡了很久,因为它不是界面活,是改工程文件格式的决定

Wqian 拍板:**手填自由文本 + 已用过的值做补全**。三个选项里
（a 加 `area` 字段 / b 不做 / c 拿画面当区域复用）选了 a 的一个变体。

三条理由：

1. **不复用 `plcId`** —— 它是**通信归属**不是**组织归属**。
   一台 PLC 常同时带一号泵房和二号泵房，一个区域也可能横跨两台 PLC。
   **一个经常筛错的筛选框比没有更糟。**
2. **不做工程级区域清单** —— 自由文本是清单方案的**真子集**：
   现在只多一列 `TEXT`，将来想收窄成下拉清单只改 UI 不动 schema；
   反过来先做清单、日后发现多数项目只有两三个区域，就得迁移第二次。
3. 老库迁移不再是障碍 —— F2 已建好探测式加列。

链路：`Alarm.area` / `CheckerConfig.area` → JSON → `ThresholdAlarmChecker`
→ `AlarmRecorder` 落库（11 列）→ `AlarmHistoryView` 下拉筛选
→ `AlarmConfigDialog` 表格 + 可编辑下拉 → `CsvExporter` 导出。

**★ 落地时冒出来的额外要求（原始设计里没有）**
老库里**全部**记录 `area` 都是 NULL。用户第一次用新版本，选「一号泵房」→ 0 行
→ 他会得出「报警历史丢了」的结论，而真相是记录都在、只是没区域可分。
**一个筛选器如果能把用户引向错误的结论，那它比没有这个功能更糟。**
所以下拉里必须有「未分配」，而且必须真的能筛出来 ——
区分靠 QString 的 **null（不筛）/ 空串（筛未分配）**。

**★ 途中踩到一个真实的 Qt 坑：`QVariant` 把 null 和空串判成相等。**
最初把区域值放进 `QComboBox::itemData`，`findData("")` 命中了索引 0 那项
「全部」（data 是 `QString()`，null）—— **「筛未分配」静默退化成了「不筛」**。
界面看不出任何异常。改成旁路一份 `QStringList areaValues_`（不过 `QVariant`），
并在 `reloadAreaCombo` / `selectedArea` / `setAreaFilter` 里一律显式 `isNull()` 比较。
**是测试逼出来的**：`areaFilter_unassignedBucketIsReachable` 期望 1 行、实得 2 行。

**CSV 也补上了 area 列** —— 事故分析要的是「按区域透视」，
而那是把 CSV 拖进 Excel 干的事。区域只进数据库不进 CSV，功能等于不存在。
追加在**末尾**（插中间会让按列号取值的老脚本串列），
未分配导出成**空**而不是「未分配」三个字（否则 Excel 透视里会多出一个假区域）。

负向对照（拆掉实现重跑，红的是且仅是该红的）：
`COALESCE` 去掉 → 只红 1 条；`unassigned()` 返回 null → 只红同一条；
`insertAction` 不绑 area → 红 4 条；对话框导出丢 `cfg.area` → 红 2 条；
去掉 `.trimmed()` → 只红 1 条；CSV 少 `area` 列 → 红 3 条且原有 4 条 CSV 用例**仍绿**。

新增 18 条用例：`tst_AlarmRecorder` 5 + `tst_AlarmHistoryView` 4 +
`tst_AlarmConfigDialog` 6 + `tst_CsvExporter` 3。
测试**目标数**仍是 66（新增的是用例不是可执行文件）。
`ctest` **66/66 通过**、`hmi.exe` 编译通过。

## [Unreleased] - 2026-10-10 (四)

### 报警历史库补上老库升级路径 —— 纯基建,不含 schema 语义变更

`AlarmRecorder::ensureSchema()` 原本只有 `CREATE TABLE IF NOT EXISTS`，
而 `IF NOT EXISTS` 的语义是**表在就什么也不做**。老库一旦少了列就一直少着，
直到用户现场第一次写报警或查历史才抛 `no such column`。
新装环境永远建完整表，**这条路径压根没人走过** —— 测试全绿、现场才炸。

- `kColumns` 列清单成为 `alarm_history` schema 的**唯一真相源**：
  `CREATE TABLE` 由它拼出，老库升级也逐列比对，两处不再可能漂移。
- 新增 `addColumnIfMissing()`：`PRAGMA table_info` 探测 → 缺才
  `ALTER TABLE ADD COLUMN`，幂等。沿用 `HistoryRecorder`（D.2）既有做法。
- **没有引入版本号**：`AlarmRecorder` 与 `HistoryRecorder` 共用同一个
  `history.db`（`hmi.cpp:531`），而 `PRAGMA user_version` 是**文件级唯一整数**，
  两个类各自记版本会互相覆盖、且不报任何错。

验证方式不是"加了之后是好的"，而是**负向对照**：临时开关关掉迁移循环后，
红的是且仅是 3 条新用例，其余 15 条老用例照常通过。其中
`legacyDb_existingRowsSurviveUpgrade` 暴露的真实现场症状是 ——
缺列时 `query()` 返回 **0 行**，即"报警历史全空"。

`ctest` **66/66**、`hmi.vcxproj` 经 **MSBuild** 编译通过。

## [Unreleased] - 2026-10-10 (三)

### F5-a 漏掉的两张报警表 —— 「报警名」被截断,「状态」空摊 440px

F5-a 在 `AlarmConfigDialog` / `AlarmHistoryView` 上修掉过
"拉伸最后一列 → 短列吃走全部余量"这个缺陷，但**漏了报警表本身**。

`hmi.cpp` 与 `AlarmListControl.cpp` 两处配置完全一样
（时间/报警名/等级/值/**状态**），都是 `setStretchLastSection(true)`
且**没有** `ResizeToContents`。实测 876px 视口：

| 列 | 改之前 | 改之后 | 内容需要 |
|---|---|---|---|
| 时间 | 100 px | 55 px | — |
| **报警名** | **100 px（★截断★）** | **708 px（够）** | 135 px |
| 等级 | 100 px | 32 px | — |
| 值 | 100 px | 38 px | — |
| 状态 | **476 px（空 440）** | **43 px（空 7）** | 36 px |

真实报警名（如「3#锅炉给水温度高高报警」）需要 135px，而那一列只有 100px。

`AlarmListControl` 那一处优先级不低于主窗口 —— E.5 之后操作员实际盯的是
**运行时画布**上这个"会闪的"列表，不是主窗口那张。

做法照搬 F5-a 已确立的写法：`ResizeToContents` + 指定列 `Stretch` +
`stretchLastSection(false)`，余量给宽度最不确定的一列。
审计表（`hmi.cpp`）同构处理，拉伸 `Variable` 列。

**这一条是量出来的，不是看渲染图看出来的** —— `tst_ProtocolComboShot` 里
新增 `measureStretchLastSection`，把列宽直接打出来，并把「改之前放不下 /
改之后放得下」两头都钉成断言，免得有人把配置改回去还照样绿。

未动：`UserManagementDialog`（末列是时间戳，不是空列，吃不满余量）、
`TableControl`（列头由用户配置，**不能写死索引**，规则待 Wqian 拍板）。

验证：`ctest` 66/66、`hmi.exe` 经 **MSBuild** 编译通过。

> ⚠️ 期间全量套跑出现过 **1 次红**，连续 8 次复跑全绿，未取到失败断言原文，
> 未能确认是否仍是那条已知的 `tst_Trend` 偶发失败。**如实记，不编原因。**

## [Unreleased] - 2026-10-10 (二)

### 把协议下拉框真的渲染出来看 —— 结论:功能是好的,但我差点修错东西

上一条把 S7/EIP 置灰做完之后,只跑了单元测试就收工了。问题是那是个**纯视觉**改动 ——
"用户能看出这一项没实现" —— 而 `tst_ProtocolComboGrey` 断言的是
`itemData(Qt::ForegroundRole)` 的数值,那是**数据对了**,不是**像素对了**。

新增 `tst_ProtocolComboShot`:载入**真的 `hmi.ui`**、调**真的生产函数**、
真的弹框、真的抓像素存 PNG。

**渲染结果是对的**:

- 字体 Microsoft YaHei UI 9pt,`(未实现)` 四个字形全部存在 —— 不会画成豆腐块。
  这条字符串断言永远测不出来,是本次唯一有价值的"渲染才知道"的事。
- 深色主题下 Modbus 白字(对比度 ≈14.2:1)、S7/EIP 灰字 `#9E9E9E`(≈5.3:1),
  都过 WCAG AA(4.5:1),两者之间也分得开。

### ★ 一次靠"负向对照"才没闯过去的假修复

排查时发现 tooltip 的 `ToolTipRole` 数据在、但怀疑**弹不出来**,
于是加了个 `QEvent::ToolTip` 事件过滤器,并"验证"了它确实弹出 tooltip。

**然后做了负向对照:把新加的过滤器摘掉,再发一次事件 —— 照样弹。**

即 `QComboBox` 的弹出框本来就支持 item tooltip,原代码没毛病,
那个过滤器**修的是不存在的问题**,已整个撤掉。

错在探针本身,两处:

1. 用 `QTest::mouseMove` 触发。Qt 的 `ToolTip` 事件是**事件派发器在鼠标停住不动之后**
   合成的,`mouseMove` 只投递移动事件,压根不产生该事件 —— 有没有过滤器都是"不弹"。
2. 找窗口类名 `"QToolTip"`。Qt 6 里 tooltip 那个窗口叫 **`QTipLabel`**,
   `QToolTip` 只是个 `QObject` 单例,不是窗口。按错名字找,永远找不到。

★ 只验"加了修复之后是好的",**验不出这个修复根本没必要**。
这跟上一条里 `sync_vcxproj` 探针自己打印 PASS 是同一个毛病:
**探针必须有一条能证伪自己的对照**,否则它只是在替我已有的结论背书。

验证:`ctest` **66/66 通过**(生产代码一个字没改)、`tst_ProtocolComboShot` 全绿。

## [Unreleased] - 2026-10-10

### 协议下拉框置灰 —— v0.2.0 只发 Modbus,S7/EIP 之前是"假可用"

`isSupported()` 答的是"工厂认不认识这个名字",而 S7/EIP 有骨架、**认识**。
于是两个 stub 被当正常协议摆在下拉框里,用户选下去只看到一句
"协议不兼容,不能切换" —— **界面从头到尾没说过它们没实现**。

拆成两个函数:`isSupported()` 保持原义,新增 **`isUsable()`** 答"真能连吗"
(v0.2.0 只有 Modbus 返 true)。UI 抽成 `ProtocolAvailabilityUi.cpp`,负责置灰 +
加"(未实现)"后缀 + tooltip 写明档期,启动时逐条 `qWarning`,选中时弹说明并撤回下拉框。

> 抽成生产代码是为了让测试打的是**真代码**。第一版把这段逻辑抄进测试文件,
> 那样测的是副本 —— `hmi.cpp` 里那份被谁改错,测试照样全绿。

顺带修掉两个**此前从未执行过**的 bug,都是被新测试逼出来的:

- **下拉框撤回那段是死代码**:`findText(current, Qt::MatchFixedString)` 大小写敏感,
  拿协议 ID 去显示名里找永远返回 -1,撤回逻辑从未生效。
- **`"EtherNet/IP"` 匹配不上任何协议标识**:显示名小写是 `"ethernet/ip"`,
  工厂认的是 `"eip"` / `"ethernet_ip"`。为此加了 `normalizeProtocolName()`。

这两个一直没暴露,是因为 `DriverFactory::create` 硬编码 `"modbus"`,
S7/EIP 分支**从来没真跑起来过**。v0.3.0 真接上时第一个撞的就是它们。

三条行为变更全部做了变异验证(`isUsable` 放行 s7 / 后缀不拆 / 撤回退回 `indexOf`),
三次都如实红过再绿回来。

**验证做到哪一步**:`ctest` **66/66 通过**(64 + `tst_ProtocolAvailability` +
`tst_ProtocolComboGrey`)、`hmi.exe` 构建通过、**MSBuild 真编一次**。
注意 64→66 这两个新测试**不登记进 vcxproj** —— 测试走自己的 CMake 工程,
CMake 根本不读 `hmi.vcxproj`,所以它们证明不了工程文件是对的。

### `sync_vcxproj.py --apply` 会毁掉 vcxproj —— 已修,并加了脚本守着

登记新文件时顺手 `--apply` 了一次,**文件当场坏掉**,靠 `git checkout` 撤销:
BOM 从 `239,187,191` 变成 `60,63,120`、CRLF 406 行变 0 行、
git diff **395 增 403 删**、连带 filters 报出 **222 条"假的不一致"**。

修的时候又挖出两层。第一版修复(按字节写回 BOM + CRLF)只挡住了表层 ——
ElementTree 照样把 `<Project xmlns="...">` 写成 **`<ns0:Project xmlns:ns0="...">`**,
并且**丢掉全部 68 条 XML 注释**(23814 → 20103 字节)。

最终**根本不用 ElementTree 写**:改成文本级纯追加,在 `</Project>` 前插一个新
`<ItemGroup>`。判据一句话:**纯增行、零删行** —— 真的 diff 就该是"加了两条",
不是"重排了四百行"。

★ 第一版探针**打印了 PASS**:我只查了 BOM / 裸 LF / XML 能否解析,三条全过,
而文件已被改得面目全非。**自检脚本自己也会假绿**,判据必须挑真能区分好坏的项。

新增 `scripts/check_vcxproj_writeback.py` 守这条写回路径(TEMP 副本上跑,不动真文件)。
**这已是本项目第三次被"脚本说成功、文件其实坏了"绊到** —— 前两次分别死在
`fix_filters_entries.py` 的 `return 0` 排在写回之前、和这次的 `tree.write()`。

## [Unreleased] - 2026-10-09

### vcxproj.filters 漏配 27 条 —— 编译不受影响,所以测试全绿也照样漏

约定 3 要求新增 `.cpp/.h` 同时登记到 `hmi.vcxproj` **和** `hmi.vcxproj.filters`。
实测登记长期只做了前一半:QtMoc 缺 12、ClCompile 缺 11、ClInclude 缺 4,
最早可追到 5.2 阶段的 `ThresholdAlarmChecker`,一直到昨天的 `AlarmConfigDialog`。

**为什么没有任何自动检查抓到它**:`.filters` 只管 VS 资源管理器的**显示**,
Ninja / ctest 走 CMakeLists,根本不读这个文件。症状是"文件在 VS 里看不见",
而 64/64 全绿。已用新增的 `scripts/fix_filters_entries.py` 补齐(默认 dry-run)。

顺带清掉 2 条幽灵条目:`HighAlarmChecker.h/.cpp` 在 `369451f` 就删了、
改名成 `ThresholdAlarmChecker`,filters 里的旧名没人跟着清。
`--prune` 只删磁盘上也不存在的条目 —— `AlarmHistoryView.h` 那类
"只登记 `<QtMoc>` 不登记 `<ClInclude>`"的(29 个头都是这惯例)保留,那是惯例不是错。

### 待办里"EXCLUDE_DIRS 应跳过 `3rdparty/`"这条照做会编译失败

原 P1 写的做法会**直接编译失败**:`3rdparty` 下只有 `qcustomplot` 真的有源文件,
而 `qcustomplot.cpp` 是必须编进 hmi 的(`CMakeLists.txt:37` + `hmi.vcxproj` 都已收录)。
真正要排的是 `3rdparty/qcustomplot/examples/` —— 5 个示例程序、21 个源文件,
**其中 5 个定义了 `main()`**;`--apply` 塞进去就是链接期 `multiple definition of main`,
而这个错**只在链接期爆**,编译阶段一路绿灯。已改的是 `"examples"` 子目录名。

> 改完 dry-run 从「缺 12 个 .cpp + 7 个 .h」变成「缺 0 残留 0」,
> 顺带证明 `hmi.vcxproj` 本体一直是准的(本次没动它)。

**工程文件是 UTF-8 带 BOM + 全 CRLF,本项目已经因此踩坑两次**。
脚本按字节读写,已核对:BOM 仍是 `239,187,191`、CRLF 675→753、**裸 LF = 0**。

验证做到哪一步:filters 222 条逐条查磁盘 0 幽灵、`sync_vcxproj.py` 缺 0 残留 0、
**MSBuild 真编一次**(唯一会读 filters 的路径 —— CMake 那条读不到,别拿 configure 成功当验证)、
`ctest` 64/64。

## [Unreleased] - 2026-10-08

### CI 一直在假装运行 —— 阶段 A 标 ✅ 的那一项,实际一次都没跑过

`docs/现状.md` 一直把阶段 A 记作"✅ 8/8 验证通过"。复核时发现 CI 那项**从没执行过一次**:
**GitHub 只读仓库根的 `.github/workflows/`**,而文件一直在 `hmi/.github/workflows/build.yml`
(仓库根是 `E:/qianwie/hmi`,`hmi/` 只是子目录)。没被执行 → 没跑过 → 自然也没红过。

> 本地 64/64 全绿 + 文档写着"✅",两个证据互相印证着,谁也不会去查 CI 配置本身。

已移到仓库根,并修了另外三处,**每一处都实测过**:

- `cmake -S . -B build` → `CMakeLists.txt` 在 `hmi/` 下,原来那条必失败;测试同理
- 产物路径 `build/bin/Debug/hmi.exe` → 实跑 VS generator 读 `.vcxproj`,
  `<OutDir>` 是 `build\Debug\`,**没有 `bin` 这层**
- 上面那个错配配的是 `if-no-files-found: warn` → **产物从来没上传过,而 CI 不会因此变红**。改 `error`
- 删 `submodules: recursive`:`git ls-files -s` 里 gitlink 一个都没有,仓库没子模块
- `QT_QPA_PLATFORM: offscreen` 显式设上:64 个测试里 **27 个不自己设**,本地靠
  `build_tests.ps1` 设,CI 上没这个脚本。实测设与不设结果一致,零风险
- **超时 30 → 60 分钟**:本机 Ninja 从零编**仅 64 个测试目标**实测 **19.5 分钟**,
  而 CI 还要多编主程序再跑测试,runner 通常更慢。30 分钟会在编译中途被掐掉,
  报出来是"超时失败",没人想得到去查编到第几个。
  (这 19.5 分钟是本机值,不是 CI 值,真跑完还可能不够,到时再校)

**一条我原本猜错、实测才发现无害的**:`hmi/CMakeLists.txt` 无条件
`set(CMAKE_PREFIX_PATH "D:/Qt/6.8.3/msvc2022_64")`,我本来认定它会顶掉 CI 上
`install-qt-action` 的环境变量。**实测是错的** —— 把硬编码改成不存在的路径,
`find_package(Qt6)` 仍从环境变量回落找到 6.8.3。它不是 bug,但仍改成
"优先环境变量 → `$ENV{QTDIR}` → 本机兜底":理由是这个文件要进 CI,
写死本机 D 盘路径会让人以为"CI 依赖某人的 D 盘",谁当真去排查就是白查。
改完验了三种情形,`.\build.ps1 -Clean` 从零重编 94 个目标通过。

⚠️ **修完的 CI 一次都没在 GitHub 上真跑过** —— push 不通(代理),推不上去就无从触发。
所以只记"已修正并本地实测等价命令可跑通",**不记 ✅**。下次 push 通了第一件事看 Actions。

### F3-b 运行时画布的报警列表会闪了 —— 并且抓到一句我自己写的假注释

**补的什么**:运行时画布上的 `AlarmListControl` 原来**根本不闪**。
而 E.5(运行时画布)之后,操作员实际盯的就是运行时画布 ——
也就是"会闪的那套"根本不在他眼前。主画面 `ui.alarmTable` 会闪,它不会。

- **速度对齐 500ms**(同 `hmi.cpp:423`),**判据对齐** `hmi.cpp:1492`:未确认 + 严重/危急。
  信息/警告闪 = 最吵的永远在闪,真出事的那条被淹;已确认的还闪 = 骗操作员二次处置。
- **定时器会停**:有该闪的行才开表;停之前把相位复位 ——
  否则新报警第一帧是暗相、颜色还停在上一轮的亮相上,看着像"新报警颜色不对"。
- **关键设计:闪的是【亮度】,不是【换一套颜色】**。
  主画面 `hmi.cpp` 是直接换成 `QColor(255,80,80)` / `(255,200,100)`;
  那样危急/严重/警告会共用一个红,而"哪一档"正是颜色在表达的信息。
  改为**本档底色变亮/变暗、色相不变**:闪在说"还没处理",色深在说"有多重",两通道不打架。

**这一笔真正的收获是抓到自己写错的东西。**

`repaintRow()` 里我先写的是亮红 `#f05a5a` 配白字,注释还特意标了"保证对比度不降"。
拿 WCAG 相对亮度公式算了一遍,发现**那句话是假的**:

| | 底色 | 字色 | 对比度 |
|---|---|---|---|
| 危急 暗相(原) | `#cd2828` | 白 | 5.34:1 |
| 危急 亮相(**原写法**) | `#f05a5a` | 白 | **3.33:1** ← 掉了一截 |
| 危急 亮相(改后) | `#f05a5a` | `#3b0606` | 5.20:1 |
| 严重 亮相 | `#ff9682` | `#780a05` | 5.39:1 |

即**闪烁最亮的那半拍恰恰是字最难读的那半拍** —— 而那正是操作员最想看清
"哪条报警、多严重"的时候。根因是底色一提亮白字必然失效,两者不能同时要。

**所以这句话不能只留在注释里。** 注释是写给我自己看的,下一次调色的人未必会重算;
而"字看得清"和"底色够亮"在屏幕上长得完全一样,没人会拿两个色块数字去比。
→ 新增 `blink_textStaysReadableInBothPhases`,断言两个相位下对比度都 ≥ 4.5:1
(WCAG AA 正文最低要求),并用变异验证:字色改回白字,该用例立刻红并报出 3.33:1。

代价如实记:危急在亮相半拍不是白字了(「唯一用白字的档」只对静止态成立)。
取舍是"看得清"优先于"用白字标识最重的那一档"—— 后者只是约定,前者是能不能读。

**测试写法**:5 条新用例全部用 `advanceBlinkForTest()` 手动走相位,
没有一条 `QTest::qWait(500)`。干等 500ms 既慢又不稳(CI 上一忙,
定时器可能没被派发,红的原因和被测逻辑无关)。两条变异都实测过会红。

**没解决的**:闪烁实现**仍是两份代码**。主画面 `hmi.cpp` 原封不动,配色也不同
(那边换色相,这边变亮度)。真统一要先定"报警闪烁的配色权威在哪一处",
那是改产品行为,留待 Wqian 拍板 —— 见 `docs/现状.md` 第七节。

### F5-a 报警三界面排版整改 —— 用户的原话是"界面有点丑"

改 `AlarmSoundDialog` / `AlarmConfigDialog` / `AlarmHistoryView` 三处。
**业务逻辑和工程文件格式一个字没动**;`ctest` 64/64。

**先读代码列缺陷,再动手** —— 不凭感觉调间距。改掉的实际问题:

- 声音设置的三段说明写死了 `\n` 换行 → 改 `wordWrap`,换个 DPI 也不会断在奇怪的地方
- 四档等级"名字+举例"塞在一个字符串里,右边参差不齐 → 拆成两列
- 提醒间隔显示"5000 ms"(现场的人心算里没有毫秒)→ 界面换算成秒,存储仍是毫秒
- **总开关关着,下面三组照样能勾能改** —— 用户以为设了,其实一条都不生效。
  新增 `updateEnabledState()` 逐个置灰;**试听按钮故意不跟着灰**,
  因为总开关关着的时候用户恰恰最需要试听来确认"这台机器到底能不能出声"
- 配置对话框 7 组表单挤一行 → 拆两行进 `QGroupBox`,标签右对齐,
  三个输入框的左边缘才真正成一条线
- 配置对话框 6 个语义完全不同的按钮挤一行(其中"删除选中"会丢数据)→ 分左右两簇
- 打开对话框时表单全灰、界面上零解释 → 加 `formHint_`,
  分"没选中行"和"这条是非阈值报警"两种说法(同 F2-c「静音不能等于隐藏」)
- 历史视图四档等级全是黑字 → 上色+加粗,色值照抄 `AlarmListControl` 的 fg 四色,
  免得同一个产品在两处给两套"危急=什么颜色"的映射

**最要紧的一条,任何断言都查不出来,只有看渲染图才能发现:**

两张表原来都用 `setStretchLastSection(true)`,即**把多余宽度全给最后一列**。
而最后一列是「阈值」(`> 80.0` 几个字)和「已屏蔽」(绝大多数时候为空) ——
于是历史视图里「阈值」摊着约 700px 空白、其余六列挤成窄缝,
配置对话框里「已屏蔽」占掉约 400px。改成**拉伸「报警名」列**。

> `ResizeToContents` 和 `stretchLastSection` 全都是"合法"的,ctest 全绿也照样难看。
> 这和 `tst_SelectionFrameRender` 记的是同一类错误:**几何对 ≠ 画出来对**。
> 当时写了个一次性渲染工具把三个界面 grab 成 PNG,看完已删 ——
> **它是工具,不是 65 号测试**。工具本身的坑也记在 `docs/现状.md`:
> `QT_QPA_PLATFORM=offscreen` 下汉字全渲成方框(该平台不带字体),
> 必须用原生平台 + `WA_DontShowOnScreen`。

**另外**:`src/drivers/ProtocolGuard.h` 补进 `hmi.vcxproj` 与 `.filters`(积压项闭环);
⚠️ 整改期间 `tst_Trend` 偶发红过 1 次,单独跑 12 次 + `ctest` 10 次 + 全套 8 连跑均未复现,
**未取到失败断言原文**,与本次改动无代码交集,如实记在 `docs/现状.md`,不下结论。

### F4 报警历史查询 —— 存了三个月的历史,终于看得见

D.3 把报警落库做完了,主窗口的"报警历史"tab 却一直是个占位 Label。
**存了但看不见的历史等于没有历史** —— 出了事故要复盘,连"昨晚到底报了什么"都答不上来。

**加了什么**

- 时间范围过滤(可手选)+ 最近 1 小时 / 24 小时 / 3 天 / 7 天快捷按钮
- 等级过滤(准确说是"最低等级",见下)
- **最新的排在最上面**
- 0 条时明确说"该条件下没有记录",而不是留一张空表
- 数据库没打开时整个界面置灰 + 说清楚是"库未启用" —— 界面能开但什么都查不出来,
  用户只会以为"没报警",而真相是数据库没开

**三件写下来才想明白的事**

1. **"等级"那一栏必须写"最低等级: 严重及以上",不能写"等级 = 严重"。**
   `AlarmRecorder::query` 的第三个参数是 `minLevel`,它是**下限**不是集合,
   选"严重及以上"会把危急一起带出来。界面上写"等级 = 严重"的话,
   用户看到危急的记录不见了,会去查"危急报警是不是没记录" —— 而它好好地在库里。
   **这是界面在撒谎,比缺功能更糟。**

2. **最新的排在最上面,但改的是视图不是 query。**
   `AlarmRecorder::query` 是 `ORDER BY action_at ASC`,那是**导出 CSV** 要的顺序
   (事故分析从头顺着读下来最顺)。但操作员点开"报警历史"是想看"刚才发生了什么",
   让他自己翻到最后一行去找,等于没有这个界面。
   两种顺序各有各的用处,改全局会连 CSV 导出和现有测试一起改掉,
   所以 `std::reverse` 放在表现层 —— 同一份数据,给人看的倒着排,给机器读的顺着排。

3. **等级和动作两列全是乱码,一条用例绊出来的。**
   报错打印 Actual 是 `ä¸¥` —— 肉眼看过去**就是"严重"两个字的乱码形状**,
   很容易以为是自己期望值写错了。真病根在实现:显示用的字面量是源码里的 UTF-8,
   代码却用 `fromLatin1` 转,"严"的 3 个字节 `E4 B8 A5` 被当成 3 个 Latin-1 字符。
   名字那一列一直是对的(直接从库里取的 QString),
   **正是这种"别的列好好的、就这两列是乱码",最容易让人以为是数据问题。**

**区域过滤没做** —— 整个代码库里**没有"区域"这个概念**
(`Alarm` 结构里没这个字段,工程文件里也没有)。要支持就得先给报警加字段、
动 schema,那是会改工程文件格式的设计决定,不是界面层能顺手加的。
三个选项(a 加 area 字段 / b 暂时不做 / c 拿画面当区域)写在 `AlarmHistoryView.h` 里等 Wqian 拍板。
**在拍板之前界面上不出现"区域"字眼**,免得用户去找一个还不存在的功能。

`tst_AlarmHistoryView` 12 条 —— **用 QTEMP 下的库,绝不碰用户真实的 `history.db`**
(测试写用户的生产数据是不可原谅的)。`ctest` 64/64 全绿。

### F5 报警声音 / 蜂鸣 —— 让报警出得了声,而且出得对

**选 Windows `MessageBeep` 而不是 Qt6::Multimedia**:不用为一声蜂鸣再装一个 Qt 模块。
`setSoundSink()` 是留给将来的替换口。

**做到了**

- 4 档等级分别用不同系统提示音(危急最刺耳,信息默认不响)
- **确认即静音** —— 操作员处理过了就不该再吵
- **重复提醒直到确认**,默认 5 秒一圈,每圈只响一次、按当前最高等级响
- **静默时段**(可设起止,默认 22:00–7:00)
- 主窗口新增"报警声音"按钮 + 设置对话框(带试听,需 EditProject 权限)

**决策逻辑(`decide()`)是纯函数,完全可单测;声音有没有真的出来测不了**
(没有 CI 声卡,`MessageBeep` 也不返回任何东西)。`tst_AlarmSound` 27 条测的是决策。
**绿灯之后仍然必须人耳验一次。**

**写这一节时挖出两个真 bug:**

- **静默时段里触发的报警,过了静默时段就再也不响了。**
  重复提醒定时器在静默时段被停掉,而**没有任何东西会再把它启动起来** ——
  于是凌晨 2 点触发的一个危急报警,一直安静到它自己恢复为止。
  修法在 `onRepeatTick()`:**必须按"还有没有待处理的报警"来停,不能按"这一圈有没有响"。**
  静默时段里 `decide()` 全返回 `SilencedByQuietHours`,按后者判断定时器会 22:00 就停掉。
- **`pendingReminderCount()` 一开始错算了静默时段。**
  我最初让"待处理"= "这一圈会响",那么晚上界面上会显示"0 条报警",
  而一个没人处理的危急报警就躺在活动列表里。已纠正:这个计数**刻意不考虑静默时段**,
  和 F2-c 的"静音不能等于隐藏"是同一条原则。

### F4-b 报警配置回灌 —— 草稿终于能落到运行中的 AlarmManager 上

F4-a 做的是"看和改草稿",改完存哪儿都清楚。F4-b 补上最后一环:**把草稿应用回运行中的
`AlarmManager`**,包括改已有报警的阈值、加新报警、删掉不再要的。

**两条用例红了,红的原因都是我的测试写错了,不是生产代码:**

- `apply_updatesThresholdOfRunningAlarm` 点"新增"造新报警,但没传 `PollScheduler`,
  新增被正确拒绝了(报"新报警没有关联到有效变量")。
  **这条用例只验"改已有报警",就不该新增** —— 新增是另一条用例的活。
  一条用例只验一件事。
- `apply_rejectsDuplicateNamesAndChangesNothing` 只有一行时根本造不出重名,
  于是断言"重名被放过了"直接绿了。先点"新增"造出第二条草稿才测得到。
  → 这条正好说明了为什么"只断条数/只断返回码"的用例容易假绿。

## [Unreleased] - 2026-09-30

### F2-c 报警屏蔽(shelve)—— 让"这条报警现在没意义"能被按掉,而且必须看得见

**加了什么**

- `AlarmManager` 新增 `shelve / unshelve / isShelved / shelveCount / shelvedNames`,
  外加 `shelveChanged(name, bool)` 信号。屏蔽收口在 `onCheckerTriggered` ——
  所有 checker 报警的唯一入口,所以阈值报警、写失败报警等**所有类别**一致受管。
  (放进各个 Checker 里就只有阈值报警能被屏蔽。)
- `clearAll()` 顺带清空屏蔽名单,换工程不会带着上个工程的静默名单开局。
- 新增 `tst_AlarmShelve`(9 个用例)。

**为什么把"可见"当成硬约束**

如果"被屏蔽的 checker 不触发"是唯一表现,那就是:屏幕上没有行、没有计数、
没有日志 —— 操作员会以为"这次真没报警",实际是**被我们藏起来了**。
更糟的是检修结束忘了取消屏蔽 → 传感器真的坏了 → 依然没有报警。
设备坏了而界面一片安静,这是能出人命的。所以三条硬约束:
`shelveCount()` 随时可查、`shelveChanged` 让界面自己跳出来、屏蔽必须能撤销。

**没做的:自动到期**。工业上常见"屏蔽 8 小时自动恢复",本次不做 ——
它需要常驻定时器 + 到期语义 + 跨重启策略,而当前项目连报警编辑 UI 都还没有
(配置只能手改 JSON)。宁可少做一半,也不做出一个"以为会自己恢复、实际不会"的半成品。

**屏蔽的是"进不进系统",不是"检测器还测不测"**

拦截点在 Manager,`ThresholdAlarmChecker` 照常在算、传感器照常在越线。
这条不能含糊:调试时看到"检测器还在报但界面没反应"是正常的,
反过来若把检测也停了,故障就彻底不可见了。

**幂等与失败语义**(UI 要能分清"名字写错了"和"本来就没屏蔽")

| 情况 | `shelve` | `unshelve` |
|---|---|---|
| 名字没注册过 | `false` | `false` |
| 已注册、首次 | `true` + 发信号 | `true` + 发信号 |
| 已注册、状态未变 | `true`,不重复计数 | `true`,no-op 不发信号 |

**证据**(反向验证,不是"改完看着对了")

把 `onCheckerTriggered` 里的拦截条件改成 `false &&` 后:

| 用例 | 拦截关掉后 |
|---|---|
| `shelvedChecker_doesNotAppearInActiveAlarms` | **红** |
| `shelveIsReversible_unshelvedAlarmTriggersAgain` | **红** |
| `shelveOtherAlarms_stillWorkNormally` | **红** |
| 其余 6 条(计数/幂等/信号/clearAll/未知名) | 绿 |

3 红 6 绿正是该有的样子:只有"抑制真的生效"的三条依赖拦截,
另外六条守的是状态机和可见性,本来就不该因为拦截被关掉而红。

**本次写测试时踩的一个 C++ 坑,记在 `tst_AlarmShelve.cpp` 头部**

`new` 出来的裸指针调非静态成员函数必须写 `ptr->fire()`,写 `ptr.fire()` 报
`C2228` 且**只报第一处**。我一度以为是 moc 认不准嵌套类,还为此把类挪出了
`TestAlarmShelve` —— 那个判断是错的。定位方法是**把报错行删掉看错误会不会顺移**,
一顺移就说明是全篇都错、只报第一个。

### F2 报警滞回 —— 治 chatter,并且先纠正一个我自己的误判

**加了什么**

- `CheckerConfig` 新增 `hysteresis` 字段(默认 `0.0` = 不启用),`JsonSerializer`
  读写 `hysteresis` 键。老工程文件没这个键 → 回落 `0.0`,行为与 5.3 完全一致。
- `ThresholdAlarmChecker` 状态机从 `bool active_` 改成显式三态
  `Idle / Pending / Fired`。滞回只在 `Fired` 档生效:报警已报出后,必须跌到
  `threshold - hysteresis` 才算恢复,值在死区里来回摆不再反复"恢复→再触发"。
- 新增 `tst_ThresholdAlarmChecker`(8 个用例)—— 这是**该文件第一次有测试**。
  `minDurationMs` 防抖逻辑此前从来没被任何一条用例执行过。

**我自己的误判,记在这里别删**

我最初判断"`active_` 同时表示待触发和已触发 → 报警恢复后再超限会【重报同一条报警】,
这是 bug",还把它立成任务 #14。**拿修复前的代码跑测试,那条断言是绿的** ——
它描述的是原有行为,不是缺陷。

真实结论:**高 → 正常 → 再高报两次是正确的工业语义**。两次就是两次独立工况,
操作员需要知道"它又犯了一次"。该压掉的只有 chatter:模拟量在阈值附近来回摆,
每次越线都超过 `minDurationMs`,于是形成一串完整的 触发→恢复→触发。

三态重构仍然保留 —— 因为单 bool 确实区分不了"待触发"和"已触发",
滞回没法只挂在 `Fired` 这一档上。但它是**为滞回铺路的重构,不是 bug 修复**,
这两者的区别不该含糊。

**证据**(反向验证,不是"改完看着对了"就交)

| 用例 | 修复前 | 修复后 |
|---|---|---|
| `chatter_slowOscillation_isSuppressedByHysteresis` | **红**(4 次触发 / 期望 1) | 绿 |
| `hysteresis_valueInDeadBand_doesNotRecover` | **红**(死区内就报了恢复) | 绿 |
| 其余 6 条 | 绿 | 绿 |

chatter 用例里 A 组(无滞回)断言 `4 次触发 + 4 次恢复` —— 这条是**在修复前就成立**的,
它先把"抖动确实会刷屏"这个事实钉住,再由 B 组(加滞回 → 1 次)对照。

**顺带记录一个滞回的边界**:低点必须**留在死区内**才有效果。
`threshold=50, hyst=10` 时死区是 `(40, 60)`,低点若跌到 30(< 40)已越过恢复沿,
照样每次都报。我第一版用例把低点写成 30,测试红着才发现 —— 边界值得写进用例。

### F3 报警列表交互 —— 操作员终于能确认报警了

范围由 Wqian 拍板:**只改 `AlarmListControl`**(主画面那张 5 列表格 5.5 就有右键确认,不动),
确认手势用**双击行**。

**开工前挖出两件比"加个按钮"严重得多的事:**

1. **运行时画布上的报警列表从来没接过数据源。**`RuntimeCanvas::loadControls` 以前
   只做 `setBasePath` + `addItem`,**从没调过 `bindAlarmManager`** —— 画面上摆的
   `AlarmListControl` 拿到的 manager 恒为 `nullptr`,表格永远空,
   **不报错、不弹窗、日志一行都没有**。跟 E.5 修的「编辑器存的画面不绑变量」
   是同一类病:链路缺一环,两端各自看着都正常,拼起来才缺。
   → `RuntimeCanvas` 构造多收一个 `AlarmManager*`,`hmi.cpp` 传 `alarmMgr_`。

2. **双击在当时的代码里是不可能的。**
   `proxy_->setAcceptedMouseButtons(Qt::NoButton)` 是 E.4b 修「选不中/拖不动」加的
   —— 代理不吃事件,事件穿透到父 item。代价是**内嵌的 `QTableWidget`
   一个鼠标事件都收不到**,连选中行都不行。两边需求正面冲突,只能切:
   → 新增 `ControlBase::setRuntimeMode()`(默认**编辑**模式),
   运行时模式才让代理吃事件;同时关掉 `ItemIsMovable`,
   免得双击被当成拖拽、把报警列表从屏幕上拖走。

**顺带修的 3 个静默失败(本项目第 4/5/6 次):**

| # | 问题 | 修法 |
|---|---|---|
| 4 | `acknowledge()` 返回 `void`,权限不足只 `qCWarning` 一行 → 界面"点了没反应",分不清是没权限还是已经确认过 | 改成返回 `bool`;两个 UI 都按三种失败分别给提示 |
| 5 | `addAlarmRow` 无条件写"未确认" → 任何一次 `refreshNow()` 都把已确认的报警变回未确认,操作员于是再点一次,第二次又没反应 | 按 `Alarm.state` 填 |
| 6 | 4 档等级只有 Critical/Emergency 有色 —— 报警列表里每行都是报警,没有"这行没报警"作参照,Info/Warning 根本分不出来 | 4 档各一色,两两不同;已确认/已恢复淡化成灰底 |

未确认计数用 **`AlarmManager` 里 `state == Active` 的条数**算,**不是表格行数** ——
`acknowledge` 只改 state 不删记录(5.5 有意为之),按行数算数字永远降不下来,
而这个指示灯的全部价值就是"降下来才说明活儿办完了"。

**测试有效性已反向验证**(两轮,共 **8 条变红**):
断双击连线 + 计数退回数行数 → 6 条红;运行时模式退回永远穿透 + 配色退回两档有色 → 2 条红。
`doubleClick_actuallyAcknowledges` 断言的是 `Alarm::state == Acknowledged`
**而不是表格文字** —— 文字谁都能写,state 才是真被确认了的证据。

**顺带发现,未擅自改**:`hmi.cpp` 的 `acknowledgeAll()` 因为上面 `count() > 0`
就 return 的守卫,**永远确认不到任何报警**,从 2026-08-04 一直是空转。
改不改涉及复位语义,属 Wqian 的决定;本次只把返回值接住并留了 qWarning。

> ⚠ **本节编号说明**:`阶段E.0_从这里继续.md` 的计划表里 **E.5 = 主题(亮/暗)**,
> 而 `1946fc7` 的 commit message 把「运行时画布」也写成了 E.5 —— **编号撞车**。
> 本节一律写全称。**建议**:运行时画布改称 E.7a、复制粘贴改称 E.7b(E.7 计划表里空着)。
> **2026-09-30 判断:暂不改。** 改编号要 `rebase` 改两条已推送 commit 的 message 并 **force-push
> 公开仓库** —— 代价是别人若已 clone 过就得重新拉;而收益只是文档措辞。
> 文档里一律写全称已经能防误读。**等 F 阶段开工时一并处理**,或 Wqian 明确要改再改。

### 阶段 E · 收口后补的两件用户路径缺口

这两件**是在 E 阶段标"全部完工"之后才发现的**。原因值得记:
`ctest` 全绿 ≠ 用户路径通了 —— 老测试只断言"控件在不在 `controls_` 列表里",
**从没断言过"主画面能不能把它显示出来"**,也没断言过"能不能复制/删除"。

#### 1. 运行时画布 RuntimeCanvas（`1946fc7`）

编辑器摆好的画面,存进工程文件后**在主画面显示不出来**。新增 `RuntimeCanvas`,
让 `hmi.exe` 按工程文件里的控件清单把画面画出来,并按 `variableName` 绑变量。

#### 2. E.7 复制 / 粘贴 / 删除（`35fc787`）

编辑器此前**只能加控件** —— 选中一个元件按 Delete 没有任何反应,也不能复制粘贴。

**为什么命令存 JSON 快照而不是 `ControlBase*`**：
`GeometryChangeCommand.h` 早就写明"QUndoCommand 被 delete 时控件可能已经没了"。
删除类命令更狠 —— 撤销一次删除,`undo()` 造的是一个**新对象**,
所有旧指针当场变悬空指针。所以 `ControlListCommand` 只存 `QList<QJsonObject>`,
把命令和控件的生命周期彻底解开。

**快照必须用 `propertiesToJson()` + `registryKeyFor()`,不能用 `toJson()`**：
后者写的 `type` 是类名 `"RectangleControl"`,而 `create()` 查的是注册表短名 `"Rectangle"`。
对不上就**静默返回 nullptr**,画面凭空少一块且零报错 —— 这正是 E.5 踩过的坑,
`tst_ControlClipboard::copy_snapshotType_isRegistryShortName_notClassName` 专门守它。

**增删合一**:`forPaste` / `forDelete` 两个工厂,内部同一个 `Kind` 枚举。
两者 undo/redo 逻辑完全一样,只有"首次 redo 是不是空操作"不同,
拆成两个类等于把快照/销毁/选中逻辑写两遍。

**顺带修掉 3 个真 bug**：
1. **粘贴首贴不偏移** —— offset 乘的是 `pasteCount_`(首次为 0),第一份会严丝合缝压在原件上,
   用户看着像没生效。改成乘 `pasteCount_ + 1`。
2. **撤销删除后控件层级整个翻转** —— `targets_` 来自 `selectedItems()`,Qt 给的是**按叠放序倒序**。
   照抄这个顺序重建,画面上的前后关系跟删之前不一样。改成按 `controls_` 原始顺序重排。
3. **`destroyControl` 缺 `setSelected(false)`** —— `QGraphicsScene` 内部拿**裸指针**记着选中项,
   直接 `removeItem` + `delete` 会留一个指向已释放内存的指针在 scene 里。

**测试** `tst_ControlClipboard` 25 用例,重点是「删→撤→删→撤」连环 5 轮 ——
命令存快照,撤销造新对象,旧指针当场失效,残留一处在第二轮就出事。

#### 3. 修 `tst_ProjectSchema` 硬编码路径（`7ef3fce`）—— ctest 才是真 57/57

上面两件做完,`ctest` 是 **57 个目标 56 通过**。唯一失败的 `tst_ProjectSchema` 红了两个多月,
一直记成"E 阶段有个已知小尾巴",实际是**测试自己在骗人**:

两个用例把输出路径写死成 `E:/qianwie/hmi/hmi/tests/build/xxx.json`,
而 `build_tests.ps1` 建的是 `hmi/tests/build_ninja/` —— `build/` 目录根本不存在,
`saveToFile` 落不了盘返回 false。**硬编码绝对路径 = 换台机器、CI 上换个工作区,这两条照样红。**

改用 `QTemporaryDir`(是**目录**不是已存在的文件,所以不跟 `QSaveFile` 抢文件名 ——
代码里原先注释担心的 `QTemporaryFile` 冲突本来就不存在),析构自动清理。

**生产代码一行没改。** `ctest` → **57/57 全绿**。

> 教训:测试红 ≠ 功能坏,得先分清是谁的问题;而"56/57"这种残缺口径一旦写进四份文档,
> 读者眼里的分量就和"全绿"差不多了。**口径要么写准,要么别写。**

#### 4. B 组 4 张 bug 现场图 —— 复现 + 修复（多选时位置框永远置灰）

`docs/bug图/README.md` 里挂了两个月"疑似遗留未修"的 4 张图(图2/图3/图6/图7),
2026-09-30 复现成功。**一个根因,不是四个。**

`PropertySheet::rebuildForm` 判断"多选是否同值"时拿的是**整个属性值**:

```cpp
if (other->property(prop.name()) != firstVal) { common = false; }
```

`position` 是 `QPointF`、`size` 是 `QSizeF` —— 比的是整个点/整个矩形。
两个控件只要有**任一分量**不同,整个复合编辑器就被 `setEnabled(false)` 置灰。

**为什么在多选时是致命的**:多选时 `position` 几乎**必然**不同值(不然就不叫多选了),
于是属性面板的位置框**永远灰着、永远看不了**。用户选了 3 个控件想看它们各自在哪,
面板只给一个灰掉的 `0.00`。

而 `0.00` 恰恰是**症状的放大器**:异值时 editor 被置空,`QDoubleSpinBox` 读回来是默认值 0。
用户看到 `x = 0.00` 会以为控件真跑到原点了,于是怀疑框算错、handle 错位 ——
图上那些"红框和控件错位"的观感就是这么来的。**框没算错,是面板在装死,还装得像报了个坐标。**

**改动**:
1. `PositionEditor` / `SizeEditor` 加按分量的接口(enabled / valid 分离)
2. `PropertySheet` 改成**按分量分别判同值**:`x` 相同就照常显示可编辑,只有真不同的那格才灰
3. 顺带修掉一个连带的坑 —— 异值那格被置空成 0,改另一格时 `onXChanged` 把这个
   **0 当真值**发出去,`applyEditorChange` 写进所有控件 → **改 x 把所有 y 刷成 0**。
   现在用 NaN 标记"该分量别动",写入侧跳过
4. `syncFormFromTarget` 同步改成分量级刷新(否则属性变更后灰度不更新)

**测试** `tst_PropertySheet` 新增 3 条:
- `multiSelect_positionNotAllZero` —— 两分量都不同 → 都该灰(别把"该灰"改成"不灰")
- `multiSelect_positionPartialCommon` —— x 同 y 异 → x 可编辑显示 10;改 x 后 y 各归各不被刷成 0
- `multiSelect_sizePartialCommon` —— size 分支同样按分量(图2 的 width/height 0.00)

`ctest` **57/57 全绿**,`hmi.exe` 构建通过。

> **教训(同类第三次)**:`efb0f56` 记的根因是错的;`3f63ddf` 计划书列的 3 处有 2 处本来不用改;
> 这次 4 张图以"疑似遗留"躺了两个月没人确认。
> **静默失败 + 归因不明 = 永远排不上优先级。** 这类条目要么当天花 10 分钟跑一遍确认,
> 要么别留在待办里占位 —— 挂着只会让人以为还有 4 个未知问题悬着。

#### 5. E.8 控件自定义底图 —— 22 个控件手绘太丑

Wqian:"这些元件的图片都太丑了,有没有那种自定义背景图片的功能"。

**为什么是"底图叠加"而不是"换掉手绘"**：泵的转子旋转、罐的液位、阀的开合
**本身就是功能**,不是装饰。换成一张静态图,储罐的液位就永远不动了。
所以图走**下层**,22 个控件的矢量层(液位/动画/状态色)**一律保留在上层**。

**改动 1:能力下沉到基类,一个文件覆盖 22 个控件**

`ControlBase` 原本是 `virtual void paint() = 0`(每个控件自己画全部)。
改成**模板方法**：

```cpp
void ControlBase::paint(...) override final {      // 终态,派生类改不了
    if (ensureImageLoaded() && !cachedPixmap_.isNull()) { /* 画底图 */ }
    paintContent(painter, option, widget);         // 纯虚,各控件自己的矢量层
}
virtual void paintContent(...) = 0;                // 22 个控件只需改函数名
```

22 个控件的改动是**纯机械改名** `paint` → `paintContent`(22 个 .h + 22 个 .cpp),
新增的底图逻辑一行都不用碰它们。**这是选基类而不是逐个控件加属性的全部理由。**

**改动 2:`ImageControl` 的属性被删掉了,不是"重复定义"**

`ImageControl` 原本**自己就有** `imagePath` / `scaleToFit`。
基类加上同名属性后,一个类上会出现两个同名 `Q_PROPERTY` ——
哪个生效取决于 Qt 元对象的继承顺序,反射和序列化行为会变得不可预测。
所以把 `ImageControl` 自己的两个属性**删干净**,只保留"没配图时画个占位框"。
**两个真值来源 = 迟早有一次不匹配。**

**改动 3:相对路径,不存绝对路径**

存绝对路径 = 把刚修掉的 `tst_ProjectSchema` 硬编码 `E:/qianwie/...` 那个错
复制到产品代码里 —— 工程文件拷给同事、换个盘符,底图全丢。
所以存**相对工程文件目录**的路径,`basePath` 从 `EditorMainWindow` / `RuntimeCanvas`
一路传到 `ControlBase`。

顺带一个坑:**pixmap 缓存的判据是解析后的绝对路径,不是 `imagePath_` 属性值**。
按属性值判的话,`basePath_` 变了(打开另一个工程)而属性没变时会命中旧缓存 ——
画面上挂着的是上一个工程的图。`basePathChange_invalidatesCache` 专门守这条。

**改动 4:加载失败回退手绘,不是画灰底**

图没了(文件被删/路径错)如果画一片灰,用户看到的是"设了没反应",
和"图加载了但太暗"分不清。所以**直接不画底图、矢量层照旧**,
`qWarning` 打出来,属性面板那个输入框**描红边 + tooltip 提示**。

**属性面板**:输入框 + `[浏览…]` + `[清除]`。
`浏览…` 直接把绝对路径**转成相对工程目录**再存 —— 用户在文件对话框里
选的是绝对路径,存的时候不转,这个功能在别人机器上就是坏的。

**测试** `tst_ControlBackgroundImage` 8 条(渲到 QImage 数像素,不是只断言 getter):
- `validImage_actuallyPaints` —— **扫红色像素**。"设了路径但没画出来"是这个功能最容易的失败方式
- `imageIsUnderneathVectorLayer` —— 满罐时罐体中心必须是液体蓝,不是底图红。**专测分层顺序**
- `basePathChange_invalidatesCache` —— 两个临时目录、同名不同色的图,换 `basePath` 后画面必须变
- `brokenPath_fallsBackToVector` —— 坏路径不能崩,也不能画空白

`ctest` **59/59 全绿**(`build_tests.ps1 -Run` 实跑,含下面 6. F3 的 `tst_JsonLoadDiagnostics`),`hmi.exe` 构建通过。

> **编号说明**:这个功能原打成 "F.1",**提交前改成 E.8** —— 阶段 F 是报警(下一步),
> 底图是 E 阶段画面的延续,打成 F.1 就是本文开头批评的 E.5 撞车,
> **同一天犯第二次没有意义**。E.7 已被复制/粘贴占用,故取 E.8。

### 已知遗留
- ⬜ 等比缩放一个**已旋转**的控件时同步旋转（保持形状）—— E 阶段只保证"不出离谱值"
- ⬜ 旋转只按 90° 整数倍换光标；45° 一档要补表（算法不用改，加表即可）
- ⬜ 主题 / Lua —— 已拍板推 v0.3.0

#### 6. F3 实测 · "加新变量 = 改 JSON 一行"

**怎么测的**：手工在 `default.json` 里加 5 个变量，然后调**生产的** `Project::loadFromFile`
—— 不是重写一份解析器，那样测的是我自己的代码不是产品的代码。这是本项目 F3 之后一直
"未真验证"的验收缺口。

**结论：卖点成立。** 5 个变量全部加载，address / data_type / access / deadband /
poll_interval_ms / alwaysPoll / range 逐字段断言全对；v1→v2→v3 schema 迁移无人值守完成。

**但暴露 3 个问题 —— 全部是"静默失败"或"报错无用"，已修。**

##### 6.1 报错不含变量名、不含字段、两种错报同一句话

`data_type` 写成小写 `int16`、漏写 `access`、缺 `name` —— 三种完全不同的错，
原来报出来是同一句 `"variable 字段缺失或类型错误"`，且**不含变量名**。
200 个变量的工程里，第 137 个写错一个字母，用户完全不知道该改哪一行，只能挨个试。

改成错误信息带**变量名 + 字段名 + 合法值**：

```
第 6 个 variable 解析失败:
变量 "Boom": "data_type" = "int16" 不合法
  合法值:Bool, Int16, UInt16, Int32, Float（注意大小写,小写 int16 无效）
```

> 合法值是 `keyCount()/valueToKey(i)` 现场拼的 —— `QMetaEnum` 没有 `keys()` 成员。
> 写死一份枚举名列表的话，加新类型时它会静静过期，变成第二种静默失败。

##### 6.2 未知字段静默丢弃

手打 `dead_band`（少个下划线）→ 死区没生效，**但用户以为配了**。
配错和没配长得一模一样 —— 这就是静默失败。

改成 `qWarning` 点名变量和字段。**只警告不报错**：报错会破坏前向兼容
（新版本加的字段被老版本读到时，不该直接拒绝加载整个工程）。
"这个字段当前版本用不上"和"用户拼错了"是两回事，不该同等对待。

##### 6.3 解析中途失败，内存里留"半个工程"

`clear()` 只在 `loadRootFromFile` 开头调一次，而下面 5 个解析循环各有 `return false`
—— 走到那里时前 N 个变量/报警已经建出来了。调用方若不检查返回值直接继续用，
就拿到了残缺状态。

收成一个 `failWith()` lambda 统一清。**抽 lambda 不是为了省那 3 行**，是为了
**不再有第 6 处 `return false` 忘了清** —— 这正是"每处各写一遍"必然漏的形状。

##### 6.4 附带发现：重名是静默替换（未修，留待拍板）

`VariableManager::addVariable` 碰到同名变量是**替换**不是拒绝（6.3 有意为之 ——
替换前后要 emit `variableReplaced` 让报警/绑定更新 `Variable*` 指针）。
所以手改 JSON 写两个同名变量，第一个被悄悄丢掉，**零提示** —— 跟 6.2 同一类。

`VariableManager.h:20` 的旧注释「返回 false:name 重复」**是错的**，已更正 ——
不更正的话下一个人会照着它写测试、把这个有意行为当 bug"修"掉。
`tst_JsonLoadDiagnostics::duplicateName_silentlyKeepsLast` 把当前真实行为锁住了，
但**只陈述事实，不主张该不该改**。

> **这 4 条是同一个病**。项目里它已经咬过 4 次：`efb0f56` 归因错误、旋转方案 2/3 分支多余、
> B 组 4 张图 2 个月没确认、这次的未知字段。**"加载成功"和"加载对了"是两件事，
> 而产品目前只报告前者。**

##### 测试

`tst_JsonLoadDiagnostics` 13 个用例（`ctest` 58 → **59/59 全绿**）：
正向手工加 5 变量逐字段断言 · 报错点名变量/字段/合法值 · 指出第几个变量失败 ·
两种错报不同句 · 未知字段忽略而不拒载 · 标准字段不刷噪音 · 失败不留半成品（两条路径）。

**改动文件**：`JsonSerializer.h/.cpp`、`Project.cpp`、`VariableManager.h`（仅注释）。

#### 7. F1 · 报警历史存出孤儿行 —— 「超额完成」是单测层面的

开 F 之前通读报警代码。**F6 之前标的 ✅「超额完成:4 档 + 列表 + 闪烁 +
触发/确认/恢复三态」要降级为 🟡** —— 那是**单测层面**的 ✅,不是产品层面的。

##### 7.1 历史表里每一条"恢复"行都是孤儿

`AlarmManager::onCheckerRecovered` 的顺序是 **erase → emit**:

```cpp
it->recoveredAt = now();
activeAlarms_.erase(it);        // ← 记录没了
emit alarmRecovered(id);        // ← 只传 id
```

而 `AlarmRecorder::onAlarmRecovered` 靠**遍历 `activeAlarms()`** 取完整数据 ——
刚 erase 的那条**必然遍历不到**,直接走兜底 `insertAction(id, {}, 0, ...)`。

**结果:历史表里每一条"恢复"行的 `name`/`level`/`variable_name`/
`trigger_value`/`threshold` 全空。** 事故追溯查出来是一行只有 id 和时间的孤儿。

**为什么两个月没人发现:** 旧测试 `recordRecovered_writesActionTwo` 只断言
`action==2 && id 匹配` —— 恰好是这条路径上**唯一没坏的两样**。
断言挑在了不会红的地方。

##### 7.2 修法(不只是调顺序)

1. 恢复的记录进 `archived_`(有界 500 条)而非直接销毁
   —— 只调顺序的话 `acknowledgedAt`/`state` 照样当场蒸发,
   「没来得及确认值就回来了」这种现场最常见的情况无从回答
2. `alarmRecovered` 多带一个 `const Alarm&` —— 下游不必回查一个可能已空的容器
3. `state` 置 `Cleared` —— 这个枚举值定义在 `Alarm.h:24` 一直没人写,是死代码
4. `clearAll()` 连存档一起清 —— 换工程时 `findAlarm()` 会串到上个工程的数据
5. `findAlarm(id)` 正确的取法:先 active 后 archived_

##### 7.3 改信号签名的回归风险已钉住

全仓有两个消费者是**老签名**(只接 `qint64`):
`hmi::onAlarmRecovered`(`hmi.cpp:1350`)、
`AlarmListControl::onAlarmRecovered`(`AlarmListControl.h:49`)。

Qt 允许"多参信号 → 少参槽"所以不影响它们,但**没有编译期检查兜底**:
失配的症状是"UI 不更新了",跟代码看着没问题完全不搭边,极难查。
`legacyIdOnlySlot_stillReceivesRecovery` 专门钉这条。

##### 7.4 测试有效性做了反向验证

临时把行为改回旧的 erase 顺序(保留新 API,只让行为退化),
`tst_AlarmRecorder` **7 条立刻变红**,改回来又全绿。
**这证明这些测试测的是行为,不是代码的形状。**
新增 7 条用例,`ctest` 仍 **59/59 全绿**(并入既有 `tst_AlarmRecorder`)。

##### 7.5 附带发现:报警 UI 有两套,能力不对等

| | `hmi.cpp` 的 `ui.alarmTable` | `src/widgets/AlarmListControl` |
|---|---|---|
| 用在哪 | 主画面(运行时) | 编辑器画布 |
| 闪烁 | ✅ `alarmBlinkTimer_` 500ms(`hmi.cpp:387-389`) | ❌ 无 |
| 未确认计数 | ❌ | ❌ |
| 确认入口 | ❌ 只读 | ❌ 只读 |

两套都只按 `id` 更新行文字。**F 阶段要么统一,要么明确"主画面那套才是运行时权威"。**

> **教训(第四次同类)**:`efb0f56` 归因错误、B 组 4 张图 2 个月没确认、
> F3 未知字段静默丢弃、F1 断言挑在不会红的地方 ——
> **都是"测试绿了 ≠ 功能能用"**。这次靠**读生产代码 + 反向验证测试有效性**抓到,
> 不是靠加测试。

**改动文件**:`AlarmManager.h/.cpp`、`src/history/AlarmRecorder.h/.cpp`、
`tests/tst_AlarmRecorder.cpp`。

---

## [Unreleased] - 2026-09-28 ~ 09-29

### 阶段 E · E.1c 补齐 + 收尾（Path 2「降级 E」）

**写在最前面**：本节之前 CHANGELOG 停在 0.2.0-rc1（E.4d-follow-7 / 18 控件）。
这 3 天做完了 E.1c 全部 5 个工业控件 + 写回 PLC 通道 + Undo 全覆盖，控件库 **22 个全落地**，
`ctest` 从「52/53，10 个长期红」变成 **53/53 全绿**。

#### 1. `ControlBase::writeToPlc` —— 控件写回 PLC 通道（计划外前置，但绕不过去）

**发现**：`ControlBase` 当时是**单向**的。`onVariableValueChanged` 只做「PLC 推过来 → 更新显示」，
反方向推不出去。而 `ToggleControl.cpp:63-70` 鼠标点击翻转本地值并发 `toggled` 信号，
**grep 全仓库没有任何人接这个信号**。也就是说当时 22 个控件**全是只读显示器,一个都写不回 PLC**。

对阀门/泵/电机是致命的 —— 不能点开的阀门、不能启停的泵就只是 5 张图片。

- 新增 `ControlBase::writeToPlc(v)`：`setValue` + `Variable::setValue` + `markDirty`。
  `markDirty()` → `WriteScheduler::onTick` 扫 `isDirty()` 发出去，**不用动 WriteScheduler**。
- 拒绝条件：未绑定 / 变量只读 / 编辑器模式 → 返回 `false`。
- 顺带修好 `ToggleControl` 断线的 `toggled`。
- 测试：`tst_ControlBaseWrite`。**最要紧的一条是 `click_readOnly_doesNotFlip`** ——
  只读变量必须连本地值都不许翻，否则画面会显示一个根本没发生的状态。

#### 2. 5 个工业控件（E.1c 原计划 0 个，全部新写）

| 控件 | 外观 | 交互 |
|---|---|---|
| `TankControl` | 矩形 + 底部液位填充 + 刻度 | 只显示（`minimum`/`maximum`/`tankColor`/`liquidColor`） |
| `PipeControl` | 圆角矩形管身 | 只显示（流量 > 0 推导 `flowing`） |
| `ValveControl` | 双三角（标准阀门符号） | **点击切换开/关 → writeToPlc** |
| `PumpControl` | 圆 + 3 叶片转子 | **点击启停 → writeToPlc**，带旋转动画 |
| `MotorControl` | 圆 + M 字 | **点击启停 → writeToPlc**，带旋转动画 |

**`SpinAnimator`（泵/电机共用）修了一个真 bug**：原来的循环逻辑是死代码。
`delta = next - lastLoop_*360` 里 `next` 恒 ≥ 0，`delta < 0` 永不成立，`lastLoop_` 从不递增；
而且纯累加在**掉一帧**时会把整圈丢掉（角度从 355 跳到下一圈的 5，累加以为只走了 5°），
表现为泵的转速随卡顿忽快忽慢。改用 `QAbstractAnimation::currentLoop()`（**是 public 的**）
+ 新增 `offset_` 让停止后重启从停下处续转而不是跳回 0°。
测试 `tst_SpinAnimator` 的 `droppedFrame_doesNotLoseAWholeTurn` 锁住这一点。

#### 3. 修一个静默失败的生产 bug：控件工厂注册表

`tst_MotorControl::pumpAndMotor_areDistinctTypes` 报「Pump 不在注册表里」。
根因：`ControlBase.cpp` 的工厂 `QHash<QString, FactoryFn>` **由构造函数填充**，
而 `EditorMainWindow.cpp` 开头那串 `delete new XxxControl;` 就是为了逼所有构造函数跑一遍。
**新增的 5 个控件没被加进那串 `delete`** —— 后果:① 调色板上不出现这 5 个控件
② 工程文件里存着它们,`loadFromFile` 静默跳过。两个症状都是「看起来坏了但说不清哪坏」。
已修 + 加 2 个回归断言(`tst_EditorMainWindow`:调色板项数 == 22)。

#### 4. 10 个过时测试的 JSON 键名 —— 基线**首次**全绿（52/53 → 53/53）

> ⚠ **归因更正**：这 10 个失败此前被记成「生产代码 `toJson`/`fromJson` 键名不匹配（w/h vs width/height）」。
> **那是错的。** 生产代码是对称的（`propertiesToJson` 走反射输出嵌套的 `position`/`size`，
> `propertiesFromJson` 读回来），**一个字都不用改**。
> 错的是**测试在断言一个从未存在过的 JSON 格式** —— 写的是平铺 `x`/`y`/`w`/`h`，
> 而 `Q_PROPERTY` 名是 `position`/`size`。`obj.contains(name)` 一个都匹配不上，
> **整个对象被静默忽略**，控件保持默认值。失败信息只说「值不等」，完全看不出是键名没匹配上。

改完顺手给 `tst_LabelControl` 加了 `toJson_fromJson_roundTrips`：
原来的 toJson 测试只验输出内容，**两边一起错也照样绿**。

#### 5. E.4d-follow-8：9 个对齐/等尺寸动作补 Undo

EditorMainWindow 的 6 个对齐 + 3 个等尺寸原来直接调 `setPosition`/`setSize`，**零 Undo 包装**。
用户按了左对齐，Ctrl+Z 撤不回来 —— 排版是反复试错的操作。

- 新增 `GeometryChangeCommand`：记「一批控件的矩形」而不是 N 条单属性命令
  （多选下「一次 Ctrl+Z = 一步操作」是硬要求，**退一半的排版比不撤销更困惑**）。
- 快照/入栈抽成 `snapshotGeometry()` + `pushGeometryCommand()` 两个辅助，9 个动作强制同一套写法。
- **几何没变就不 push** —— 否则 Ctrl+Z 会「按了没反应」。
- merge 保留**最早**的 `oldRects`（写反的话退一次之后还有控件没对齐）。
- `tst_GeometryUndo` 21 用例全绿。**这 9 个动作的 Undo 覆盖此前是 0。**

#### 6. E.4d-follow-9：选中框渲染测试（`efb0f56`）—— 生产代码一行没改

计划书说 `SelectionFrameItem::paint()` 缺蓝色虚线描边、要补外圈描边。**读 `.cpp` 发现笔刷在构造时就配好了。**
与其照着"没读过的实现"去改一个正在工作的东西，不如先证明它到底画不画得出来 ——
新增 `tst_SelectionFrameRender`（7 用例），把 frame 渲进 `QImage` 数像素。

**7/7 通过，生产代码零改动。** E 阶段挂着的"看得见的问题"(`bug图/图1.png`) 本来就是好的。

> 写这个测试时踩了两个**假象**,差点照着去改生产代码:
> ① 2px cosmetic pen 是**画在路径中心线上**的,一半落在 rect 外 → 图像正好等于 frame 尺寸时数不到像素
>    → 以为没画,其实是被裁了;
> ② 一边减了 scene 偏移一边还在局部坐标里 paint → 偏移减了两遍,框"飞"到画布外
>    → 以为位置算错了。
> **这就是"先跑再看"比"读文档就动手"值钱的地方。**

#### 7. E.4d-follow-10：旋转收尾（`3f63ddf`）—— 计划书说改 3 处，实测 2 处不用改

计划书把这条尾巴标成"风险最高、单独排"，理由是**要动的 3 处正是 E.4d 刚逐个修好的代码**
（`3163532` group handle position、`8a05de4` frame follow multi-move、`3372f67` handle 视觉对齐）。

先写 `tst_Rotation`（10 用例）当诊断量了一遍，**结论是 3 处里有 2 处本来就不用改**：

| 计划书说 | 实测 |
|---|---|
| 给 `ControlBase` 加 `Q_PROPERTY(qreal rotation)` | **不用加** —— `QGraphicsItem` 自己就有(`qgraphicsitem.h:497`)，不是 FINAL，反射已经列得出来，JSON 已经存读。照做会变成"子类覆盖父类同名属性" |
| `layoutHandlesFor` 要让 8 个 handle 位置跟着转 | **不用改** —— handle 是 target 的**子 item**，父一转它们跟着转（Qt 的父子 transform）。`handles_doNotFollowRotation_currently` 一开始是红的，细看是**断言写错了**：旋转重新定义了"哪个角在左上" |
| `SelectionFrameItem` 可能不用改但要实测确认 | **实测确认要改** —— `sceneBoundingRect()` 会变，但 frame 没接 `rotationChanged` |
| `applyProportionalResize` 含旋转控件时语义不对 | 输出仍是有效值（非负、非 0、不跑飞）。严格讲语义确实不对，那是更大的问题，留 v0.3.0 |

真修的 2 处：
1. **`ResizeHandleItem::applyCursorForRotation()`** —— 原来在左边的点转 90° 后视觉上到了上边，光标还是 `SizeHor`。
   按 target 当前 rotation 查 3 张 8 项表换光标。**非 90 整数倍不动** —— 任意角度下
   "这个点算左边还是上边"没有唯一答案，猜错比不改更糟（会出现"箭头朝左但只能上下拖"）。
2. **`EditorMainWindow::onRotationChanged()`** —— 接 `rotationChanged` 后同刷选中框和 handle 光标。
   **光标刷新必须走信号**，塞在 `installSelectionFrame()` 里没用 —— 后者只在选中变化时跑。

> ⚠ 踩到的坑：`connect(t, &QGraphicsItem::rotationChanged, ...)` 编译不过（`error C2039`）。
> 那个信号实际声明在 **`QGraphicsObject`** 里(`qgraphicsitem.h:530`)—— 信号要有 moc 才存在，
> 得继承 QObject。**属性写在基类、信号住在派生类**，这个错位很容易踩。

**E 阶段最担心的风险没有发生**：`tst_HandleVisualAlignment` / `tst_ResizeHandle` / `tst_EditorMainWindow` 全绿，
旋转没有重新打开 E.4d 修好的任何 bug。`ctest` **55/55 全绿**。

### 已知遗留
> 本节是 **2026-09-28~29 的历史快照**,当时的 55/55 是真的。更晚的进展见上面 `[Unreleased] - 2026-09-30`。

- ✅ ~~`SelectionFrameItem::paint()` 视觉（`bug图/图1.png`）~~ —— 见上 #6，本来就是好的
- ✅ ~~`ControlBase::rotation` + handle 跟随~~ —— 见上 #7，E 阶段闭合
- ⬜ 等比缩放一个**已旋转**的控件时同步旋转（保持形状）—— 本阶段只保证"不出离谱值"
- ⬜ 旋转只按 90° 整数倍换光标；45° 一档要补表（算法不用改，加表即可）
- ⬜ 主题(亮/暗)/ Lua(sol2) —— 已拍板推 v0.3.0

---

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

> ⚠ 本节记录的是 **rc1 时点(2026-09-28)的快照**，当时控件库 18 个。后续 E.1c 补了 5 个工业控件
> + 写回 PLC 通道 + Undo，见本文档顶部 **[Unreleased]** 节。**看当前状态请看 [Unreleased]。**

#### 控件库（E.1）✅ 18 控件（rc1 时点）
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

## 下一步 · E 阶段尾巴 + 阶段 F 启动决策（2026-09-29 更新）

E 阶段尾巴（半天 ~ 1 天，做完 E 阶段彻底关闭）：
- 补 `SelectionFrameItem.cpp` 的 `paint()` 视觉（对应 `bug图/图1.png`）
- `ControlBase::rotation` + handle 跟随 —— **已明确单独排**，风险最高。
  要同时动 `ResizeHandleItem::layoutHandlesFor` / `SelectionFrameItem` / `GroupHandleItem::applyProportionalResize`
  三处，而这 3 处正是 E.4d 那 19 个 commit 刚逐个修好的代码。
  每改一处跑一次 `tst_HandleVisualAlignment` + `tst_ResizeHandle` + `tst_EditorMainWindow`。
  阻力大就只加属性不做 handle 跟随。详细风险表见 `docs/阶段E.0_从这里继续.md` 节 7。

> ~~原清单里的 vcxproj.filters 分组 / CMakeLists 显式列表 / `.bak` 清理 / `.gitignore` 已随 `1114bc0` 完成。~~

阶段 F 启动（商业级报警，2-3 周 P1）：
- Wqian 一句话"阶段 F 启动" → 展开 F 工单
- 详见 `docs/路线图.md`
