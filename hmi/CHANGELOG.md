# CHANGELOG

## [0.1.0] - 2026-09-11

### 阶段 A · 工程化基线 · **验证通过** 🎉

**A 阶段验收状态:7/8 项通过**(详见下文)

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
| A3 | 日志文件验证 | ⏭ 跳过(同 A2-5,logging 实现已就位) |

#### 已知问题 (Known Issues)
- **A2-2 气泡提示缺失**:`hmi.cpp::closeEvent` 未 override,点 X 关主窗口不会触发 `hideRequested` 信号 → 不会弹"程序已隐藏到托盘"气泡。**临时接受**:用户已确认按 A 阶段原始标准(关窗口不退出)通过。**正式修法**:A 阶段回顾时统一补,5 行 closeEvent override。
- **Inno Setup installer 未实际编译产出**:脚本就绪,但 windeployqt + ISCC 实际打包放到 I 阶段。
- **vcxproj 手动同步**:虽有 `scripts/sync_vcxproj.py` 自动报告差异,但 vcxproj 实际写入仍需手动(或用脚本的 `--apply`)。文档同步约定见 `docs/现状.md` 第六节。

#### 跳过项说明
- A3 跳过:**Wqian 自评"也不知道验证了个啥"**。代码本身已就位(QStandardPaths 日志),可在 A 阶段回顾时补做,不阻塞 A→B 推进。
- A2-5 已于 2026-09-16 补做,详见上方表格。

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

## 下一步 · 阶段 B(2026-09 中-10)

阶段 B · 用户/角色/权限 + 操作审计(3-4 周,P0)
- 详见 `docs/路线图.md` 和 `docs/优化与改善计划.html`
- 商业化验收第一项
