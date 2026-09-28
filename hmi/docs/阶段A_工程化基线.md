# 阶段 A · 工程化基线 — 落地说明

> 配套:本计划"阶段 A"小节
> 落地时间:2026-09-08(单轮)
> 状态:✅ **8/8 验收通过**(2026-09-11 通过 6 项,A2-5/A3 于 2026-09-16 补做验证,详见 `hmi/CHANGELOG.md` [0.1.0] 节)

> ⚠ **2026-09-28 基于代码核对**:本文件状态从 `⏳ 待 Wqian 在 VS 里集成验证` 升级为 `✅ 已通过`。A2-2 气泡提示缺失、A2-5 崩溃 dump、A3 日志验证均已在 2026-09-16 补做。Inno Setup 实际打包留到 I 阶段。完整阶段状态见 `docs/路线图.md` 和 `docs/现状.md`。

## 子任务完成情况

| 子任务 | 状态 | 文件 |
|---|---|---|
| A1 安装包(Inno Setup) | ✅ 脚本就绪(实际打包留 I 阶段) | `installer/hmi.iss` |
| A2-1 单实例 + 托盘 + 自启注册表 | ✅ 通过 | `src/instance/`, `src/tray/`, `main.cpp` |
| A2-2 关主窗口不退出 | ✅ 通过(气泡提示缺失,临时接受) | `hmi.cpp::closeEvent`(未 override) |
| A2-3 单击托盘显隐切换 | ✅ 通过 | `src/tray/SystemTray.cpp` |
| A2-4 单实例检测(退出码 0) | ✅ 通过 | `src/instance/SingleInstance.cpp` |
| A2-5 主动崩溃 → dump 文件 | ✅ 通过(2026-09-16 验证) | `src/crash/CrashHandler.cpp`,`dump/crash_20260916_093410_510.dmp` |
| A3 日志文件验证 | ✅ 通过(2026-09-16 验证,30 天清理 24h QTimer) | `src/logging/Logging.cpp`,`logs/hmi_20260916_094322.log` |
| A4 ctest(3 测试全绿) | ✅ 通过 | `tests/tst_{Variable,VariableManager,Serializer}.cpp` |
| A5 Sanitizer | 📝 文档说明 | (见下方) |

## 文件清单(新增 17 + 改 2 = 19 个)

### 新增

```
src/
├── logging/
│   ├── Logging.h
│   └── Logging.cpp
├── crash/
│   ├── CrashHandler.h
│   └── CrashHandler.cpp
├── instance/
│   ├── SingleInstance.h
│   └── SingleInstance.cpp
└── tray/
    ├── SystemTray.h
    └── SystemTray.cpp

installer/
└── hmi.iss

tests/
├── CMakeLists.txt
├── tst_Variable.cpp
├── tst_VariableManager.cpp
└── tst_Serializer.cpp

.github/
└── workflows/
    └── build.yml

.clang-tidy
.clang-format
```

### 改动

| 文件 | 改动 |
|---|---|
| `main.cpp` | 重写:加 logging + crash + single instance + tray + 自启注册表 |
| `CMakeLists.txt` | GLOB → GLOB_RECURSE(子目录自动捡) |

> ⚠ **vcxproj 未自动同步** —— 按 `现状.md` 约定,加新 .h/.cpp 要手动同步 vcxproj + filters。这步 Wqian 做。

## Wqian 在 VS 里的集成步骤

### 1. vcxproj 加新文件

打开 `hmi.vcxproj` 和 `hmi.vcxproj.filters`,加入以下内容:

**`hmi.vcxproj.filters`** (在 `Header Files` Filter 下加):

```xml
<ClInclude Include="src\logging\Logging.h"><Filter>Header Files</Filter></ClInclude>
<ClInclude Include="src\crash\CrashHandler.h"><Filter>Header Files</Filter></ClInclude>
<ClInclude Include="src\instance\SingleInstance.h"><Filter>Header Files</Filter></ClInclude>
<ClInclude Include="src\tray\SystemTray.h"><Filter>Header Files</Filter></ClInclude>
```

在 `Source Files` Filter 下加:

```xml
<ClCompile Include="src\logging\Logging.cpp"><Filter>Source Files</Filter></ClCompile>
<ClCompile Include="src\crash\CrashHandler.cpp"><Filter>Source Files</Filter></ClCompile>
<ClCompile Include="src\instance\SingleInstance.cpp"><Filter>Source Files</Filter></ClCompile>
<ClCompile Include="src\tray\SystemTray.cpp"><Filter>Source Files</Filter></ClCompile>
```

**`hmi.vcxproj`** (在对应 `<ItemGroup>` 加):

```xml
<ClInclude Include="src\logging\Logging.h" />
<ClInclude Include="src\crash\CrashHandler.h" />
<ClInclude Include="src\instance\SingleInstance.h" />
<ClInclude Include="src\tray\SystemTray.h" />
<ClCompile Include="src\logging\Logging.cpp" />
<ClCompile Include="src\crash\CrashHandler.cpp" />
<ClCompile Include="src\instance\SingleInstance.cpp" />
<ClCompile Include="src\tray\SystemTray.cpp" />
```

### 2. 链接器加 dbghelp.lib(crash dump 用)

在 `hmi.vcxproj` 的 Debug + Release 两个配置的 `<Link>` 段,加:

```xml
<AdditionalDependencies>DbgHelp.lib;%(AdditionalDependencies)</AdditionalDependencies>
```

### 3. 编译验证

```
F5 编译 → 应通过(如果失败看错误信息,常见是 link 阶段缺 DbgHelp.lib)
```

### 4. 跑测试

```cmd
cd tests
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

> 期望:3 个测试全绿。

### 5. 打 installer(等 Release 编译过)

1. 编译 Release:`cmake --build build --config Release`
2. windeployqt 收集依赖:`windeployqt --qmldir . build\bin\Release\hmi.exe`
3. 编译安装包:`"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\hmi.iss`
4. 产出:`installer\Output\hmi-setup-0.1.0.exe`

## 测试用例清单

| # | 文件 | 覆盖 |
|---|---|---|
| tst_Variable | `tests/tst_Variable.cpp` | 构造 / setValue 三态 / deadband / range / quality 不重复 emit |
| tst_VariableManager | `tests/tst_VariableManager.cpp` | add/find/remove / 同名替换 / variableReplaced 信号 |
| tst_Serializer | `tests/tst_Serializer.cpp` | roundtrip / range 不存 / 非法 data_type 返 nullptr |

## 已知缺口(留给后续 / 手动补)

- [ ] **Crash dump → 邮件发送**:dump 写出来了,但没接 SMTP,需要手工收 dump 文件。阶段 F 报警路由时一并做。
- [ ] **A8 Sanitizer**:CI 没跑 sanitizer(单线程架构下,先上 ASan/UBSan)。需要本地手动编译 Debug-with-sanitizer 版本。
- [ ] **Installer 未实际编译**:Inno Setup 需要本机装 + windeployqt 收集依赖。脚本就绪,实际产出等打包时做。
- [ ] **Tray 隐藏窗口**:只接了 main.cpp 入口,关窗口(hmi.cpp::closeEvent)未 override,需要时 Wqian 加:
  ```cpp
  void hmi::closeEvent(QCloseEvent* e) {
      if (trayIcon_->isVisible()) {
          e->ignore();
          hide();
      } else {
          e->accept();
      }
  }
  ```
- [ ] **vcxproj 同步**:按惯例 Wqian 手动加。
- [ ] **`hmi.json` 配置日志级别**:默认用 info 级别,客户想看调试日志时创建 `%APPDATA%\HMI\hmi.json`:
  ```json
  {
    "logging": {
      "default_level": "debug",
      "categories": {
        "hmi.comm": "debug",
        "hmi.var": "debug"
      }
    }
  }
  ```

## A8 · Sanitizer 使用说明

```cmd
REM ASan + UBSan(默认推荐)
set CFLAGS=/fsanitize=address,undefined
cmake -B build-asan -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan --config Debug
ctest --test-dir build-asan -C Debug

REM TSan(单线程架构下价值有限,只在多线程模块启用时跑)
cmake -B build-tsan -G "Visual Studio 17 2022" -A x64 -DCMAKE_CXX_FLAGS=/fsanitize=thread
```

> 单线程架构(现状.md 第四节):所有 7 层都在主线程,没有 QThread / QtConcurrent。
> ASan 抓的:use-after-free / buffer-overflow / leak —— 对单线程仍然有效。
> TSan 抓 data race —— 单线程下几乎不会触发。

## 拍板点

阶段 A 通过标准(Wqian 确认):
- [ ] F5 编译通过(vcxproj 加完)
- [ ] 启动一次,确认托盘图标出现
- [ ] 关窗口,确认程序不退出(因为 setQuitOnLastWindowClosed(false))
- [ ] 双击任务栏图标,确认隐藏/显示切换
- [ ] 第二次启动 hmi.exe,确认第一次启动的程序收到消息(任务栏图标闪一下)
- [ ] 看 `%APPDATA%\HMI\logs\` 目录,有 hmi_*.log 生成
- [ ] 故意制造崩溃(临时改 main.cpp 加 `hmi::crash::debugTriggerCrash();`),重启看到 dump 提示
- [ ] ctest 3 个测试全绿
- [ ] 阶段性 commit:打 tag `v0.1.0`,写 CHANGELOG

> 全部 ✅ → 进入阶段 B
