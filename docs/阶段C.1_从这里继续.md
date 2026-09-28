# 阶段 C.1 · 从这里继续

> 适用:**C.1 协议抽象层**完成,准备开干 C.2 S7 适配器。
> 维护人:Wqian + AI 助手  ｜  创建:2026-09-17
> 配套:`docs/阶段C_详细规划.md` / `docs/阶段C.0_从这里继续.md`
> ⚠ **状态(2026-09-28 基于代码核对)**:✅ **C.1 已落地**(`src/drivers/IPollDriver.{h,cpp}` 抽象接口 + `ModbusDriver.{h,cpp}` 包装 ModbusMaster);PollScheduler 完全协议无关化已生效。完整阶段状态见 `docs/路线图.md`。

---

## 0. 一句话

**C.1 = 协议抽象层就绪**(PollScheduler 完全协议无关,加 S7/EIP 不用改 PollScheduler)。

---

## 1. 改了什么

| 文件 | 改动 |
|---|---|
| `hmi/src/drivers/IPollDriver.h/.cpp` | **新文件**,抽象接口 + 4 种 RegisterArea + 通用信号 |
| `hmi/src/drivers/ModbusDriver.h/.cpp` | **新文件**,实现 IPollDriver,包装 ModbusMaster(100% 行为兼容) |
| `hmi/src/drivers/DriverFactory.h/.cpp` | **新文件**,工厂模式:字符串→driver |
| `hmi/PollScheduler.h/.cpp` | 从 ModbusMaster* 改成 IPollDriver*;信号改成 QVector<quint16> |
| `hmi/ModbusMaster.h/.cpp` | 加 `disconnect()` 方法(内部 tcp_->close()) |
| `hmi/hmi.h/.cpp` | modbus_ 成员改为 IPollDriver*;信号 connect 全部用 IPollDriver 接口 |
| `hmi/tests/tst_IPollDriver.cpp` | **新文件**,12 个用例(Mock driver 验证协议无关) |
| `hmi/hmi.vcxproj` + `filters` | 同步 7 个新条目 |

---

## 2. IPollDriver 接口签名

```cpp
namespace qwhmi::drivers {

enum class RegisterArea {
    HoldingReg = 0, InputReg = 1, Coils = 2, DiscreteInput = 3
};

class IPollDriver : public QObject {
    // 连接
    virtual void connectTo(const QString& host, quint16 port) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual int pendingCount() const = 0;

    // 异步请求
    struct ReadRequest {
        RegisterArea area;
        int slaveId, addr, count;
        quint32 requestId;
    };
    virtual void sendReadRequest(const ReadRequest& req) = 0;
    virtual bool sendWriteRequest(RegisterArea, int slaveId, int addr,
                                  const QVector<quint16>&, quint32 requestId) = 0;
    virtual QString protocolName() const = 0;

signals:
    void readFinished(quint32 requestId, QVector<quint16> registers, bool success);
    void writeFinished(quint32 requestId, bool success);
    void connected();
    void disconnected();
    void errorOccurred(const QString& msg);
};

}
```

---

## 3. 关键设计决策

| 决策 | 理由 |
|---|---|
| `readFinished` 用 `QVector<quint16>` 而非 `QModbusDataUnit` | 协议无关化,PollScheduler 不再 include QModbusDataUnit |
| 接口名 `connectTo` | 跟 Qt TCP/Modbus 命名一致(不用 `connect`) |
| ModbusDriver 包装 ModbusMaster | 100% 行为兼容 + C.2/C.3 Driver 同样模式 |
| `1 driver = 1 PLC` | 不做连接池(复杂度爆炸);多 PLC 在 C.5 用多 driver |
| `explicit IPollDriver(QObject*)` | Q_OBJECT 抽象类必须有这个,否则 DriverFactory::create 编译不过 |
| `IPollDriver.cpp` 空文件 | 触发 moc,让 msvc 找得到 staticMetaObject |

---

## 4. 100% 行为兼容验证

**关键约束**:`ModbusDriver` 必须 100% 兼容 ModbusMaster,所有现有测试无改动通过。

| 回归测试 | 通过 |
|---|---|
| tst_Permission | 9/9 ✓ |
| tst_CurrentUser | 9/9 ✓ |
| tst_UserManager | 19/19 ✓ |
| tst_UIPermission | 14/14 ✓ |
| tst_AuditLog | 7/7 ✓ |
| tst_AuditE2E | 10/10 ✓ |
| tst_ProjectSchema | 12/12 ✓ |
| **tst_IPollDriver**(新) | **12/12** ✓ |
| **总计** | **92/92** ✓ |

主程序 build ✓ hmi.exe 1.46 MB

---

## 5. 下一步:**C.2 S7 适配器(2-3 周)**

按 `docs/阶段C_详细规划.md` 第 C.2 节:
- **Snap7 LGPL 库集成**(DLL + .lib + 头文件,放 `hmi/3rdparty/snap7/`)
- **S7Driver 实现 IPollDriver**(包装 Snap7 C 库)
  - `connectTo` → `Cli_Connect`
  - `disconnect` → `Cli_Disconnect`
  - `read` → `Cli_ReadArea`(4 种 area → S7 数据块)
  - `write` → `Cli_WriteArea`
- **S7-PLCSIM 仿真器**(西门子官网免费下载)集成测试
- **tst_S7Driver 5+ 用例**(Mock Snap7)

**关键依赖**:需 Snap7 库 + S7-PLCSIM,Wqian 可能有,也可能需要先下载。

**DriverFactory 启用**:`s7` 字符串创建 S7Driver。

---

## 6. C.1 踩过的 5 个坑

1. **ModbusDriver.cpp include 路径**:在 src/drivers/,用相对路径 `../../ModbusMaster.h`
2. **IPollDriver 抽象类构造**:Q_OBJECT + virtual 析构 → 必须显式 default + QObject* parent 构造
3. **ModbusMaster.disconnect()**:原本没有,加一个(内部 tcp_->close())
4. **IPollDriver.cpp 空文件**:必须存在才能触发 moc + 链接 staticMetaObject
5. **tests CMakeLists 加 ModbusDriver.cpp + IPollDriver.cpp + DriverFactory.cpp + Qt6::Widgets/Network/SerialBus**

---

## 7. 关联文档

- `docs/阶段C_详细规划.md` — C.1 + C.2 + C.3 详细分解
- `docs/阶段C.0_从这里继续.md` — C.0 收尾产物
- `docs/UI规划调研与方案.md` — D/E 阶段 UI 规划
