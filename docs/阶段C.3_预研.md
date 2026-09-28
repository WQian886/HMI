# 阶段 C.3 · 预研(2026-09-18)

> 适用:**C.3 EtherNet/IP 协议适配器**预研完成,准备开干 C.3.1 EipDriver 骨架。
> 维护人:Wqian + AI 助手  ｜  创建:2026-09-18
> 配套:`docs/阶段C_详细规划.md` C.3 节 / `docs/阶段C.2_从这里继续.md`
> ⚠ **状态(2026-09-28 基于代码核对)**:✅ **C.3 预研完成**(libplctag LGPL-2.1,DLL/lib 选型记录在本文件)。⚠ **但 `EipDriver` 仍是 stub**(`loadLibplctagLibrary()` TODO),Logix Designer 真连接未做。需 Wqian 拍板(见 C.2 状态条)。

---

## 0. 一句话

**C.3 = 让 hmi.exe 跟罗克韦尔/Allen-Bradley ControlLogix/CompactLogix 通过 EtherNet/IP 通讯**(北美主流 PLC,LGPL libplctag 包装)。

---

## 1. libplctag 关键事实

| 项 | 事实 | 来源 |
|---|---|---|
| 官网 | https://github.com/libplctag/libplctag | LGPL-2.1 |
| Windows DLL | `libplctag.dll`(运行时)+ `libplctag.lib`(MSVC 导入) | 官方 release |
| C++ API | `plc_tag_create / plc_tag_destroy / plc_tag_read / plc_tag_write / plc_tag_status` | libplctag.h |
| 异步模型 | **plc_tag_read 不阻塞**,需要 poll `plc_tag_status(tag)` 直到 `PLCTAG_STATUS_OK` | 规划 C.3 第 307 行 |
| PLC 类型 | AB MicroLogix/SLC500/PLC5/CompactLogix/ControlLogix | 官方支持矩阵 |
| 协议限制 | **Logix Designer 仿真器有 7 天试用限制**(规划 C.3 第 310 行) | 罗克韦尔商业策略 |

## 2. 异步 → 同步转换(规划 C.3 第 307 行 + 5.3 节)

libplctag 是**异步 API**,但 IPollDriver 期望**同步行为**(read 阻塞到完成)。两种方案:

### 方案 A:QEventLoop 阻塞等待(推荐)
```cpp
// 同步包装:
bool EipDriver::sendReadRequest(const ReadRequest& req) {
    int rc = plc_tag_read(tag_, 0);  // 启动读
    if (rc != PLCTAG_STATUS_OK && rc != PLCTAG_STATUS_PENDING) {
        emit readFinished(req.requestId, {}, false);
        return;
    }
    // 等到完成(用 QEventLoop 阻塞,不超过 timeout)
    QEventLoop loop;
    QTimer::singleShot(timeoutMs_, &loop, &QEventLoop::quit);
    connect(this, &EipDriver::pollTick, &loop, [&]() {
        if (plc_tag_status(tag_) == PLCTAG_STATUS_OK) loop.quit();
    });
    loop.exec();
    // 读结果到 QVector<quint16>
    emit readFinished(req.requestId, registers_, true);
}
```

**优**:实现简单,业务层不变
**劣**:阻塞 UI 线程 — 需要在 PollScheduler 的 worker 线程调用

### 方案 B:异步状态机 + Qt 信号
- IPollDriver 接口改为全异步(改抽象层 → 影响 C.1 已固化的契约)
- **不推荐**:违反"100% 行为兼容"原则(教训 1.2)

**C.3.1 骨架先按方案 A 占位**,真正连接 PLCTAG_STATUS_PENDING 等状态转换留给 C.3.4。

## 3. 4 种 RegisterArea 在 EIP 的支持(规划 C.3 第 302 行)

| RegisterArea | EIP 等价 | 支持度 |
|---|---|---|
| HoldingReg | `DINT[]` / `INT[]` / 标签数组 | ✅ 主要场景 |
| InputReg | `InputInteger` 标签 | 🟡 部分 PLC 支持 |
| Coils | `BOOL[]` / 单个 `BOOL` 标签 | 🟡 部分支持 |
| DiscreteInput | `InputBool` 标签 | 🟡 有限 |

**结论**:EIP 主要场景是 HoldingReg(类似 Modbus 保持寄存器)。其他 3 种 area 在 EIP 有标签名映射限制,**测试矩阵需要分别覆盖**。

## 4. CIP 标签寻址 vs Modbus 寄存器(规划 C.3 第 306 行 + 5.3 节)

- **Modbus**:`slave_id + addr`(数字)
- **EIP**:`tag_name`(字符串路径,如 `N7:0` / `MyTag[3]` / `Program:MainProgram.var1`)

**架构映射**:
- Modbus `slaveId` → EIP 不用(每个 driver 一个 connection,标签名含路径)
- Modbus `addr` → EIP `tag_name`
- Modbus `count` → EIP 数组长度(从 tag_name 解析或显式指定)

**C.3.1 骨架**:把 `slaveId` 当 tag_index,`addr` 当 tag_name 起始位置(暂用 magic 字符串 `"N7:0"`),后续 C.3.3 真接 Logix Designer 时细化。

## 5. Logix Designer 试用限制(规划 C.3 第 310 行)

| 限制 | 应对 |
|---|---|
| 罗克韦尔商业策略:7 天试用,过期强制购买 | C.3.1 骨架阶段不需要;C.6a 真机验证时申请新试用 |
| MicroLogix/SLC500 仿真器是免费版(Emulate 500) | **优先用 Emulate 500 做 C.6a 真机验证** |
| CompactLogix/ControlLogix 需要 Studio 5000 Logix Designer | 7 天试用,排期到 C.6a 前 1 周申请 |

**C.3.1 决策**:骨架阶段不依赖任何 EIP 仿真器,LibPlcTag 函数指针占位 + Mock Driver(类似 S7Driver 的 Mock 模式)。

## 6. 与 C.2 S7Driver 的对比

| 项 | S7Driver(C.2.1) | EipDriver(C.3.1) |
|---|---|---|
| 三方库 | Snap7 LGPL(C 库) | libplctag LGPL-2.1(C++ 库) |
| 加载方式 | QLibrary 动态加载 + 函数指针 | QLibrary 动态加载 + 函数指针 |
| API 风格 | 同步(Cli_ReadArea 阻塞) | **异步(plc_tag_read 不阻塞)** |
| 连接模型 | 1 client = 1 PLC | 1 tag handle = 1 标签;CIP connection per driver |
| 字节序 | S7 大端 | EIP 小端 |
| 4 area 支持 | 全 4 种 | 主要 HoldingReg |
| 真机验证 | S7-PLCSIM(免费) | Logix Designer(7 天试用) |

## 7. 前置清单(开干 C.3.1 之前)

| # | 项 | 阻塞? | 说明 |
|---|---|---|---|
| 1 | libplctag DLL/.h | 否(骨架不需要) | C.6a 真机前到位 |
| 2 | Logix Designer 仿真器 | 否 | C.6a 真机前申请 |
| 3 | libplctag LGPL 版本号 | 建议锁 | 用 libplctag 2.x 稳定版 |
| 4 | IPollDriver 抽象层 | **已就位** | C.1 完成 |

## 8. 下一步:C.3.1 EipDriver 骨架

按 C.2.1 模板:
1. `hmi/src/drivers/EipDriver.h/.cpp` 类骨架(Q_OBJECT + 函数指针 + plc_tag_t void*)
2. `hmi/3rdparty/libplctag/README.md` LGPL 合规要点
3. `DriverFactory.cpp` 加 `"eip"` / `"ethernet_ip"` 分支
4. `hmi/hmi.vcxproj` + `filters` 同步(教训 2.1)
5. `hmi/tests/CMakeLists.txt` 加 `EipDriver.cpp`(教训 2.2)
6. `hmi/tests/tst_IPollDriver.cpp` 更新 `isSupported("eip")` 断言
7. hmi.exe build + 13 ctest 全过 + 单 commit

## 9. 关联文档

- `docs/阶段C_详细规划.md` 第 C.3 节 + 第 5.3 节(异步 → 同步)
- `docs/阶段C.2_从这里继续.md` — C.2.1 骨架模式(直接复用)
- `docs/今日教训_2026-09-17.md` — 教训 1.2/1.3/1.4/1.5/2.1/2.2
- `docs/今日教训_2026-09-18.md` — 教训 1.1/1.2/2.1/3.1
