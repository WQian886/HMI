# 阶段 C.2 · 从这里继续

> 适用:**C.2 S7 协议适配器**骨架就绪,准备开干 C.2.2 真 Snap7 集成。
> 维护人:Wqian + AI 助手  ｜  创建:2026-09-18
> 配套:`docs/阶段C_详细规划.md` C.2 节 / `docs/阶段C.1_从这里继续.md`
> ⚠ **状态(2026-09-28 基于代码核对)**:🟡 **C.2 骨架已就位,但 `S7Driver` 仍是 stub**(`src/drivers/S7Driver.cpp:34-126` 所有 `connectTo/sendRead/sendWrite` 都是 `qWarning + emit errorOccurred`,`loadSnap7Library()` 直接 return false)。Snap7 LGPL DLL 未真动态加载,**用户切协议到 S7 会 emit errorOccurred + UI 卡死**。需 Wqian 拍板 A 本周装库接真驱动 vs B v0.2.0 只发 Modbus、S7/EIP 推 v0.3.0。完整阶段状态见 `docs/路线图.md`。

---

## 0. 一句话

**C.2 骨架 = S7Driver 类 + DriverFactory 支持 "s7" + vcxproj/filters/CMake 全同步,编译过 + 92/92 回归全过**。真 Snap7 DLL 加载 + PLCSIM 连接留给 C.2.2/C.2.4/C.2.7。

---

## 1. 改了什么(2026-09-18)

| 文件 | 改动 |
|---|---|
| `hmi/src/drivers/S7Driver.h` | **新文件**,声明 S7Driver : IPollDriver;7 个 Snap7 函数指针 typedef;`void* client_` 包 S7Client(避免头污染) |
| `hmi/src/drivers/S7Driver.cpp` | **新文件**,所有 IPollDriver 接口最小实现(返回 false / 0 + emit errorOccurred),`loadSnap7Library()` 占位;`areaToSnap7Area()` 映射 4 area → Snap7 area codes;字节序辅助 |
| `hmi/src/drivers/DriverFactory.cpp` | 加 `"s7"` 分支 → `new S7Driver(parent)`;`isSupported` 加 `"s7"` |
| `hmi/3rdparty/snap7/README.md` | **新文件**,Snap7 LGPL 集成占位目录 + 待放入文件清单 + LGPL 合规要点 |
| `hmi/hmi.vcxproj` + `filters` | 同步 3 个新条目(S7Driver.h 加 QtMoc + ClInclude + S7Driver.cpp ClCompile) |
| `hmi/CMakeLists.txt` | 加注释占位(`target_include_directories` 暂不开,等 C.2.2 真加载) |
| `hmi/tests/CMakeLists.txt` | 加 `S7Driver.cpp` 到 `HMI_CORE_SOURCES`(教训 2.2:DriverFactory 引用 S7Driver 必须链) |
| `hmi/tests/tst_IPollDriver.cpp` | 更新 `driverFactory_isSupported_modbus` 用例:`isSupported("s7")` 现在应返 true |

---

## 2. S7Driver 接口签名

```cpp
namespace qwhmi::drivers {

class S7Driver : public IPollDriver {
    Q_OBJECT
public:
    explicit S7Driver(QObject* parent = nullptr);
    ~S7Driver() override;

    void connectTo(const QString& host, quint16 port) override;
    void disconnect() override;
    bool isConnected() const override;
    int pendingCount() const override;
    void sendReadRequest(const ReadRequest& req) override;
    bool sendWriteRequest(RegisterArea area, int slaveId, int addr,
                          const QVector<quint16>& values, quint32 requestId) override;
    QString protocolName() const override { return QStringLiteral("s7"); }

    void* snap7Client() const { return client_; }   // 测试钩子

private:
    // Snap7 函数指针(QLibrary::resolve 拿)
    using Cli_CreateFunc       = void* (*)();
    using Cli_DestroyFunc      = void  (*)(void*);
    using Cli_ConnectFunc      = int   (*)(void*, const char*, int, int);
    using Cli_DisconnectFunc   = int   (*)(void*);
    using Cli_ReadAreaFunc     = int   (*)(void*, int, int, int, int, int, void*);
    using Cli_WriteAreaFunc    = int   (*)(void*, int, int, int, int, int, void*);
    using Cli_GetConnectedFunc = int   (*)(void*);

    Cli_CreateFunc       pCli_Create       = nullptr;
    Cli_DestroyFunc      pCli_Destroy      = nullptr;
    Cli_ConnectFunc      pCli_Connect      = nullptr;
    Cli_DisconnectFunc   pCli_Disconnect   = nullptr;
    Cli_ReadAreaFunc     pCli_ReadArea     = nullptr;
    Cli_WriteAreaFunc    pCli_WriteArea    = nullptr;
    Cli_GetConnectedFunc pCli_GetConnected = nullptr;

    QLibrary snap7Lib_;
    void*    client_ = nullptr;   // S7Client*
    QString  host_;
    quint16  port_ = 102;          // S7 ISO-TSAP 默认

    bool loadSnap7Library();
    void unloadSnap7Library();
    static int areaToSnap7Area(RegisterArea area);
    static quint16 byteSwap16(quint16 v);
    static void byteSwapBuffer(quint16* buf, int count);
};

}
```

---

## 3. 关键设计决策

| 决策 | 理由 |
|---|---|
| `S7Client*` 用 `void*` 包 | Snap7 是 C 库,头文件含 C 语法,放进 C++ 头会污染(规划 C.2 第 244 行) |
| `QLibrary` 动态加载 + 函数指针 | LGPL 合规硬要求:用户可替换 `snap7.dll`(规划 C.2 第 449-450 行 + C.6b) |
| 1 driver = 1 PLC | Snap7 单 client 只能连 1 台(教训 1.5 + 规划 C.2 第 209 行) |
| 端口默认 102 | S7 ISO-TSAP 标准端口 |
| `areaToSnap7Area` 映射 HoldingReg→DB / InputReg→PE / Coils→PA / DiscreteInput→PE | S7 用 DB(数据块)+ PE/PA(过程输入/输出)表达 Modbus 的 4 种 area |
| `byteSwap16` 辅助就位 | S7 内部存储是大端,但 Modbus register 是大端,PLC 端按字节序需 swap |
| 骨架阶段所有 IPollDriver 方法返回 `false` + emit errorOccurred | 占位但不崩;真实现在 C.2.2/C.2.4/C.2.5 填充 |

---

## 4. 100% 兼容验证矩阵

| 项 | 结果 |
|---|---|
| hmi.exe 主程序 build | ✅ 1.54 MB(Ninja,vcvarsall x64) |
| 13 个 tests build | ✅ 全链接成功 |
| ctest 全套跑 | ✅ **13/13 passed(0 failed)**, 1.44s |
| tst_IPollDriver 12 用例 | ✅ 全过(包含 isSupported("s7") 新断言) |
| 92 → 93 用例增 1 | ✅ 用例更新:isSupported("s7") 由 false 改 true |

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
| **tst_IPollDriver**(更新 1 用例) | **✓** |
| **总计** | **13/13** ✓ |

---

## 5. 下一步:**C.2.2 真 Snap7 集成**

按 `docs/阶段C_详细规划.md` 第 C.2.2 节 + 教训 2.2:

| # | 任务 | 文件 | 估时 |
|---|---|---|---|
| 2.2 | `loadSnap7Library()` 实现 `QLibrary::resolve` 7 个函数指针 | `S7Driver.cpp` | 0.5 天 |
| 2.3 | 字节序转换真接 Snap7 buffer(规划 C.2 第 264 行) | `S7Driver.cpp` | 1 天 |
| 2.4 | `connectTo` 真 `Cli_Connect` + 错误码映射 | `S7Driver.cpp` | 1 天 |
| 2.5 | `sendReadRequest` / `sendWriteRequest` 真 `Cli_ReadArea` / `Cli_WriteArea` | `S7Driver.cpp` | 2 天 |
| 2.6 | Snap7 错误码 → IPollDriver::errorOccurred | `S7Driver.cpp` | 1 天 |

**前置依赖(从 Wqian):**

1. Snap7 LGPL 动态库放入 `hmi/3rdparty/snap7/{include,lib/x64,bin/x64}`:
   - `snap7.h`
   - `snap7.lib`(MSVC 导入库,虽然我们动态加载,但 msvc 链接器可能需要)
   - `snap7.dll`(运行时,部署时放在 hmi.exe 同目录)
2. S7-PLCSIM 仿真器(西门子官网下载,TIA Portal 内置,或单独 PLCSIM):Wqian 应该有

---

## 6. C.2.1 踩过的 4 个坑(今日教训追加)

1. **vcxproj 必须同步**:S7Driver.h 加 Q_OBJECT → 必须 QtMoc;vcxproj + filters 同步 3 个条目(教训 2.1)
2. **tests CMakeLists 加 S7Driver.cpp**:DriverFactory.cpp 引用了 `S7Driver::S7Driver(QObject*)` → 所有测试链接都缺符号,教训 2.2 复现
3. **Ninja generator 缓存陷阱**:改了 CMakeLists.txt 后,`ninja` 有时不重生成 → 必须 `cmake -S . -B build` 强制重新 configure
4. **ctest 不通过 ≠ 代码 bug**:`driverFactory_isSupported_modbus` 用例断言 "!isSupported(s7)"(C.1 时合理),C.2 启用 s7 后用例要同步更新

---

## 7. 关联文档

- `docs/阶段C_详细规划.md` — C.2 详细分解(2.1-2.9)
- `docs/阶段C.1_从这里继续.md` — C.1 抽象层基线
- `docs/今日教训_2026-09-17.md` — 18 条教训(教训 1.5/2.1/2.2 已在 C.2.1 复用)
- `hmi/3rdparty/snap7/README.md` — Snap7 LGPL 合规要点 + 待放入文件清单
