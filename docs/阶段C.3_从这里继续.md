# 阶段 C.3 · 从这里继续

> 适用:**C.3 EtherNet/IP 协议适配器**骨架就绪,准备开干 C.3.4 真连接 Logix Designer。
> 维护人:Wqian + AI 助手  ｜  创建:2026-09-18
> 配套:`docs/阶段C_详细规划.md` C.3 节 / `docs/阶段C.3_预研.md` / `docs/阶段C.2_从这里继续.md`

---

## 0. 一句话

**C.3 骨架 = EipDriver 类 + DriverFactory 支持 "eip" + vcxproj/filters/CMake 全同步,编译过 + 13/13 回归全过**。真 libplctag DLL 加载 + Logix Designer 连接留给 C.3.4/C.6a。

---

## 1. 改了什么(2026-09-18)

| 文件 | 改动 |
|---|---|
| `hmi/src/drivers/EipDriver.h` | **新文件**,声明 EipDriver : IPollDriver;8 个 libplctag 函数指针 typedef;`int tag_` 直接存 plc_tag_t |
| `hmi/src/drivers/EipDriver.cpp` | **新文件**,所有 IPollDriver 接口最小实现;`loadLibplctagLibrary()` 占位;`buildTagName()` 简化映射(N7:x / Bx:y);`waitForTagCompletion()` QEventLoop 模式就位 |
| `hmi/src/drivers/DriverFactory.cpp` | 加 `"eip"` / `"ethernet_ip"` 分支 → `new EipDriver(parent)`;`isSupported` 加 eip + ethernet_ip |
| `hmi/3rdparty/libplctag/README.md` | **新文件**,libplctag LGPL-2.1 集成占位目录 + 待放入文件清单 + LGPL 合规要点 + 8 个函数指针 typedef |
| `hmi/hmi.vcxproj` + `filters` | 同步 3 个新条目(EipDriver.h QtMoc + ClInclude + .cpp ClCompile) |
| `hmi/tests/CMakeLists.txt` | 加 `EipDriver.cpp` 到 `HMI_CORE_SOURCES`(教训 2.2) |
| `hmi/tests/tst_IPollDriver.cpp` | 更新 `driverFactory_isSupported_modbus` 用例:`isSupported("eip"/"ethernet_ip")` 由 false 改 true |
| `docs/阶段C.3_预研.md` | **新文件**,libplctag 关键事实 + 异步 → 同步方案 + 4 area 支持度 + CIP 寻址 + 7 天试用限制应对 |

---

## 2. EipDriver 接口签名

```cpp
namespace qwhmi::drivers {

class EipDriver : public IPollDriver {
    Q_OBJECT
public:
    explicit EipDriver(QObject* parent = nullptr);
    ~EipDriver() override;

    void connectTo(const QString& host, quint16 port) override;
    void disconnect() override;
    bool isConnected() const override;
    int pendingCount() const override;
    void sendReadRequest(const ReadRequest& req) override;
    bool sendWriteRequest(RegisterArea area, int slaveId, int addr,
                          const QVector<quint16>& values, quint32 requestId) override;
    QString protocolName() const override { return QStringLiteral("eip"); }

    int libplctagTag() const { return tag_; }    // 测试钩子(plc_tag_t 是 int)
    int timeoutMs() const { return timeoutMs_; }

private:
    // libplctag 函数指针
    using PlcTagCreateFunc       = int  (*)(const char*, int, const char*);
    using PlcTagDestroyFunc      = void (*)(int);
    using PlcTagReadFunc         = int  (*)(int, int);
    using PlcTagWriteFunc        = int  (*)(int, int);
    using PlcTagStatusFunc       = int  (*)(int);
    using PlcTagGetIntAttrFunc   = int  (*)(int, const char*, int*);
    using PlcTagSetIntAttrFunc   = int  (*)(int, const char*, int);
    using PlcTagAbortFunc        = void (*)(int);

    PlcTagCreateFunc       pPlcTagCreate       = nullptr;
    PlcTagDestroyFunc      pPlcTagDestroy      = nullptr;
    PlcTagReadFunc         pPlcTagRead         = nullptr;
    PlcTagWriteFunc        pPlcTagWrite        = nullptr;
    PlcTagStatusFunc       pPlcTagStatus       = nullptr;
    PlcTagGetIntAttrFunc   pPlcTagGetIntAttr   = nullptr;
    PlcTagSetIntAttrFunc   pPlcTagSetIntAttr   = nullptr;
    PlcTagAbortFunc        pPlcTagAbort        = nullptr;

    QLibrary libplctagLib_;
    int      tag_ = 0;          // plc_tag_t(0 表示未创建)
    QString  host_;
    quint16  port_ = 44818;      // EIP 默认端口
    int      timeoutMs_ = 5000;

    bool loadLibplctagLibrary();
    void unloadLibplctagLibrary();
    static QString buildTagName(RegisterArea area, int slaveId, int addr);
    bool waitForTagCompletion(int timeoutMs);
};

}
```

---

## 3. 关键设计决策

| 决策 | 理由 |
|---|---|
| `plc_tag_t` 用 `int` 直接存 | libplctag 是 C++ 库,plc_tag_t 就是 int32_t(Snap7 的 S7Client* 是指针) |
| 端口默认 44818 | EtherNet/IP TCP 标准端口 |
| `buildTagName` 简化 `N7:x` / `Bx:y` | MicroLogix/SLC500 风格,C.3.4 真接 Logix 时细化(CompactLogix 用 tag_name 字符串) |
| `waitForTagCompletion` 用 QEventLoop + 10ms poll | libplctag 异步 → IPollDriver 同步包装的标准模式(规划 C.3 第 5.3 节) |
| 4 area 主要支持 HoldingReg | EIP 其他 3 area 支持有限(规划 C.3 第 302 行) |
| 字节序小端(EIP) | 与 S7/Modbus 大端不同 — 暂不实现,S7Driver 的 byteSwap16 可参考 |

---

## 4. 100% 兼容验证

| 项 | 结果 |
|---|---|
| hmi.exe 主程序 build | ✅ 1.54 MB(Ninja,vcvarsall x64) |
| 13 个 tests build | ✅ 全链接成功 |
| ctest 全套跑 | ✅ **13/13 passed(0 failed)**, 1.35s |
| tst_IPollDriver 13 用例(含 eip 断言) | ✅ 全过 |

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
| **tst_IPollDriver**(含 s7/eip 断言) | **✓** |
| **总计** | **13/13** ✓ |

---

## 5. 下一步:**C.3.4 真 libplctag 集成**

按 `docs/阶段C_详细规划.md` 第 C.3.4-C.3.6 节 + 教训 2.2:

| # | 任务 | 文件 | 估时 |
|---|---|---|---|
| 3.4 | `loadLibplctagLibrary()` 实现 `QLibrary::resolve` 8 个函数指针 | `EipDriver.cpp` | 0.5 天 |
| 3.4 | `connectTo` 真 `plc_tag_create`(属性字符串 protocol=ab-eip&gateway=host&...) | `EipDriver.cpp` | 1 天 |
| 3.5 | `sendReadRequest` 真 `plc_tag_read` + `waitForTagCompletion` | `EipDriver.cpp` | 2 天 |
| 3.5 | `sendWriteRequest` 真 `plc_tag_write`(先写内部 buffer 再 write) | `EipDriver.cpp` | 1 天 |
| 3.6 | libplctag 状态码 → IPollDriver::errorOccurred(PENDING/OK/TIMEOUT/ERROR) | `EipDriver.cpp` | 1 天 |

**前置依赖(从 Wqian):**

1. libplctag LGPL-2.1 动态库放入 `hmi/3rdparty/libplctag/{include,lib/x64,bin/x64}`:
   - `libplctag.h`
   - `libplctag.lib`(虽然我们动态加载,但 msvc 链接器可能需要)
   - `libplctag.dll`(运行时,部署时放在 hmi.exe 同目录)
2. Logix Designer 仿真器(7 天试用限制)— **C.6a 真机验证前 1 周申请**

---

## 6. C.3.1 踩过的 1 个坑(今日教训追加)

1. **`plc_tag_t` 是 int 不是指针**:S7Driver 的 `void* snap7Client()` 测试钩子不能直接套用到 EipDriver — EipDriver 必须返回 `int`,否则报 C2440。教训:**不同三方库的 opaque handle 类型不同,先看头文件再决定 void* 还是 int**。

---

## 7. 关联文档

- `docs/阶段C_详细规划.md` C.3 节 + 第 5.3 节(异步 → 同步)
- `docs/阶段C.3_预研.md` — libplctag 关键事实 + 异步方案 + 7 天试用应对
- `docs/阶段C.2_从这里继续.md` — C.2.1 骨架模式(直接复用)
- `docs/今日教训_2026-09-17.md` + `docs/今日教训_2026-09-18.md` — 22 条教训
