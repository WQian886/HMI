调度 = signal/slot + 100ms tick。

Qt signal/slot 是 6 层之间唯一的调度机制(没有线程,没有 RPC,直接函数调用)。主要靠 PollScheduler / WriteScheduler 100ms tick 触发,其他层被动响应。

【6 层调度全景】
UI(hmi)              ┐
业务协调(AlarmMgr)   │ 
业务执行(Checker)    ├─ Qt connect / 直接调用 ─ 信号
调度(Poll/Write)     │ 
协议(TCP/Modbus)     │
数据(Variable)       │
配置(Project)        ┘

关键事实: 所有 6 层都在主线程(没 QThread / QtConcurrent),emit/slot 同步触发。PollScheduler / WriteScheduler 100ms tick 是唯一的"主动推",其他都是"被动响应"。

【5 个核心流程】

流程 A:启动
main.cpp
 └─ new hmi() (180 行构造函数)
 ├─ new TcpClient() ← 协议层
 ├─ new ModbusMaster() ← 协议层
 ├─ new VariableManager() ← 数据层
 ├─ new Variable("Pressure") x3
 ├─ new PollScheduler() ← 调度层
 ├─ new WriteScheduler() ← 调度层
 ├─ new AlarmManager() ← 业务协调
 ├─ new ThresholdAlarmChecker x2 ← 业务执行
 ├─ new WriteFailChecker() ← 业务执行
 ├─ new ActionManager() ← 业务协调
 ├─ new Project() ← 配置层
 ├─ connect(15+ 对) ← 把所有信号接好
 ├─ project_->loadFromFile(default.json)
 │ └─ tcp_->connectTo(127.0.0.1, 502)
 └─ window.show()

关键: connect 必须在 loadFromFile 之前(6.3 修过 Bug #2)。

流程 B:读(PLC → UI)
PollScheduler::onTick(100ms)
 ├─ 扫描 varMgr_->allVariables()
 ├─ 找"该读"的 var
 ├─ 合并连续地址
 └─ modbus_->sendReadRequest(unit, reqId)
 ↓ 组帧 + 发送
[异步] TcpClient 收到字节
 └─ ModbusMaster 拆帧
 └─ emit readFinished(reqId, data)
 ├─→ PollScheduler::onModbusReply
 │ ├─ Variable.setValue(value)
 │ │ └─ emit valueChanged
 │ │ └─→ Binding::bind lambda
 │ │ └─→ label->setText("压力(bar): 100")
 │ │ ↑ UI 刷新
 │ └─ emit rawRead(v, rawRegister)
 │ └─→ ThresholdAlarmChecker::onRawRead
 │ └─ 触发条件检查
 └─→ emit idle → UI 防抖

关键: valueChanged 触发 UI 刷新(订阅发布),rawRead 触发报警(并行通道,因为绕开 Variable 死区)。

流程 C:写(UI → PLC)
用户在 speedWriteEdit 输入 "1500" + 回车
 └─ Binding::bindEdit lambda
 ├─ v->setValue(1500) ← 数据层
 ├─ v->markDirty() ← 写"标记"
 └─ 清边框
WriteScheduler::onTick(100ms)
 └─ 找 dirty var
 └─ modbus_->sendWriteRequest(unit, reqId)
 ↓ 组帧 + 发送
[异步] 收到响应
 └─ emit writeFinished(reqId, success)
 ├─→ WriteScheduler::onWriteReply
 │ ├─ success → v->clearDirty()
 │ └─ fail → dirty 保留等下帧
 └─→ emit auditAppended(WriteAuditEntry)
 └─→ hmi.cpp 槽 → auditTable 插入行

关键: 写失败重试 3 次 → kMaxWriteRetries → emit writeFailed → WriteFailChecker → 报警队列。

流程 D:报警(值越界 → 报警 + 联动)
[读流程末尾] PollScheduler::emit rawRead(v, raw)
 └─→ ThresholdAlarmChecker::onRawRead(v, raw)
 ├─ 比较 raw > threshold (Above)
 ├─ 启动 durationTimer(500ms)
 └─ 持续 500ms 仍越界:
 └─ emit triggered(Alarm)
 ├─→ AlarmManager::onCheckerTriggered
 │ ├─ activeAlarms_[id] = alarm
 │ └─ emit alarmTriggered(Alarm)
 │ ├─→ hmi.cpp 槽 → alarmTable 插入 + 闪烁 + 状态机 Fault
 │ └─→ ActionManager::onAlarmTriggered
 │ └─ 找 mapping("温度高" → speed=0)
 │ └─ v->setValue(0) + markDirty
 │ └─→ [走写流程]

关键: 报警触发后自动联动写 PLC(模拟"紧急停机"),联动由 ActionManager 持 mappings 字典完成。

流程 E:存读(内存 ↔ JSON)
保存(saveButton):
 QFileDialog → path
 └─ Project::saveToFile(path)
 ├─ JsonSerializer::toJson(Variable) x3
 ├─ JsonSerializer::toJson(CheckerConfig) x2
 ├─ JsonSerializer::toActionMappingJson x2
 ├─ QSaveFile 写(原子写)
 └─ emit saved(path)

加载(openButton):
 QFileDialog → path
 └─ Project::loadFromFile(path)
 ├─ 读 + 解析 JSON
 ├─ 校验 schema_version
 ├─ varMgr_->clear() ← 数据层
 ├─ alarmMgr_->clearAll() ← 业务协调
 ├─ actionMgr_->clear() ← 业务协调
 ├─ 重新 addVariable + addChecker + addMapping
 ├─ tcp_->connectTo(host, port) ← 协议层
 └─ emit loaded(path)
 └─→ hmi.cpp 槽:
 ├─ Binding 重绑 4 widget (先 unbind,防泄漏)
 ├─ auditTable 清空
 └─ modbus_->connectTo 同步

关键: 加载流程会彻底重置内存(Q4-A "JSON 是唯一真理")。加载失败 = 内存已经清了 + 弹错误对话框。

【调度机制总结】

驱动方式: 用户操作 / 网络异步 / 100ms tick / Qt signal/slot
谁:       UI / TcpClient / PollScheduler, WriteScheduler / 任意类
频率:     手动 / 异步 / 100ms / 事件驱动

最关键的 1 个 connect 是 modbus_->readFinished → PollScheduler::onModbusReply — 它把"协议层"和"调度层"串成一条链。

【消化口诀】
- 主动(tick) = 调度层 + UI
- 被动(slot) = 业务 + 数据 + 协议
- 唯一跨线程 = 0(都主线程)
- signal/slot = 整个工程的"胶水"