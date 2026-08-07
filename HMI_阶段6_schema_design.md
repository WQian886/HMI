# HMI 阶段 6 - JSON Schema 设计(2026-08-07)

## 目标

工程配置(通讯 + 变量 + 报警 + 联动)→ JSON 文件 → 关了重开能完整恢复。

---

## 顶层结构

```json
{
  "schema_version": "1.0",
  "project_name": "...",
  "created_at": "ISO8601",
  "updated_at": "ISO8601",
  "communications": { ... },
  "variables": [ ... ],
  "alarms": [ ... ],
  "actions": [ ... ]
}
```

---

## 1. `communications`(通讯)

| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `protocol` | enum | `"modbus_tcp"` | 当前只支持 modbus_tcp,留扩展 |
| `host` | string | `"127.0.0.1"` | TCP server IP |
| `port` | int | `502` | Modbus TCP 标准端口 |
| `modbus.timeout_ms` | int | `1000` | Modbus 请求超时 |
| `modbus.max_retries` | int | `3` | 重试次数 |
| `modbus.poll_interval_ms` | int | `100` | PollScheduler 轮询周期 |

## 2. `variables`(变量数组)

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `name` | string | ✓ | 唯一标识(`"Temperature"`) |
| `address` | int | ✓ | Modbus 寄存器地址(0x0001 = 1) |
| `data_type` | enum | ✓ | `Bool` / `Int16` / `UInt16` / `Int32` / `Float` |
| `access` | enum | ✓ | `ReadOnly` / `ReadWrite` |
| `deadband` | float | | 默认 `0.0` |
| `poll_interval_ms` | int | | 默认 `1000` |
| `range` | {min, max} | | 可选,工程值范围(Q2 拍板:**对象** `{"min": ..., "max": ...}`) |

**不存**(runtime 状态):`value` / `quality` / `timestamp` / `dirty` / `write_fail_count`

## 3. `alarms`(报警数组 = CheckerConfig 列表)

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `name` | string | ✓ | 报警名(`"温度高"`) |
| `variable` | string | ✓ | **引用 variables[].name** |
| `level` | enum | ✓ | `Info` / `Warning` / `Critical` / `Emergency` |
| `direction` | enum | ✓ | `Above` / `Below` |
| `threshold` | float | ✓ | 阈值 |
| `min_duration_ms` | int | | 默认 `500` |

## 4. `actions`(联动数组 = Alarm → Variable 映射)

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `alarm_name` | string | ✓ | 引用 alarms[].name |
| `variable` | string | ✓ | 引用 variables[].name |
| `write_value` | any | ✓ | 写入值(JSON 自然类型:number/bool/string) |

---

## 关键策略

### Enum 序列化

用 **Q_ENUM 反射的名字**(字符串),不是数字:
- `"Int16"` 而不是 `1`
- `"Critical"` 而不是 `2`
- `"Above"` 而不是 `0`

实现:`QMetaEnum.valueToKey()` / `keyToValue()`,双向转换。

### 引用 by name

`alarms[].variable` 和 `actions[].variable` 用**变量名**引用,不用地址:
- 人类可读(`"Speed"` 比 `"0x0002"` 直观)
- 工业 HMI 习惯
- 副作用:变量名必须唯一(代码已要求)

### Load 顺序(关键)

```
1. communications → 建 TcpClient 连接
2. variables → new Variable + addVariable()
3. alarms → new ThresholdAlarmChecker + addChecker()(依赖 variables)
4. actions → addMapping()(依赖 variables + alarms)
```

任何一步失败 → 中止加载,弹错误对话框。

### schema_version

- 顶层必有
- 当前 `"1.0"`
- 加载检查:不存在/不认识/不兼容 → 拒绝加载
- `"1.x"` minor 系列 → 向后兼容,接受

---

## 完整示例(`default.json`)

```json
{
  "schema_version": "1.0",
  "project_name": "HMI Demo Project",
  "created_at": "2026-08-07T13:40:00",
  "updated_at": "2026-08-07T13:40:00",
  
  "communications": {
    "protocol": "modbus_tcp",
    "host": "127.0.0.1",
    "port": 502,
    "modbus": {
      "timeout_ms": 1000,
      "max_retries": 3,
      "poll_interval_ms": 100
    }
  },
  
  "variables": [
    { "name": "Pressure",    "address": 0, "data_type": "Int16",  "access": "ReadOnly",  "deadband": 0.0, "poll_interval_ms": 1000 },
    { "name": "Temperature", "address": 1, "data_type": "Int16",  "access": "ReadOnly",  "deadband": 0.0, "poll_interval_ms": 1000, "range": {"min": -100.0, "max": 200.0} },
    { "name": "Speed",       "address": 2, "data_type": "UInt16", "access": "ReadWrite", "deadband": 0.0, "poll_interval_ms": 1000, "range": {"min": 0, "max": 65535} }
  ],
  
  "alarms": [
    { "name": "温度高", "variable": "Temperature", "level": "Critical", "direction": "Above", "threshold": 50.0, "min_duration_ms": 500 },
    { "name": "温度低", "variable": "Temperature", "level": "Critical", "direction": "Below", "threshold": 10.0, "min_duration_ms": 500 }
  ],
  
  "actions": [
    { "alarm_name": "温度高", "variable": "Speed", "write_value": 0 },
    { "alarm_name": "温度低", "variable": "Speed", "write_value": 0 }
  ]
}
```

---

## ✅ 已拍板(2026-08-07)

| # | 问题 | 选择 | 理由 |
|---|---|---|---|
| **Q1** | Modbus 子字段存哪些 | **A. 全存** | 用户能调 timeout / retries / poll_interval,工业现场真要 |
| **Q2** | `range` 表示方式 | **B. 对象 `{"min": ..., "max": ...}`** | 自解释,谁 min 谁 max 一目了然(改我原倾向) |
| **Q3** | `min_duration_ms` 怎么存 | **A. 单字段** | 显式优于隐式,每个报警各设各的 |
| **Q4** | JSON 没列的旧对象怎么办 | **A. 全删** | 内存 = JSON,JSON 是唯一真理 |
| **Q5** | first-run 策略 | **A. 内置 `default.json`** | 开箱即用,适合学习项目定位 |

---

## Q2 影响说明(B 替换 A 之后)

- JSON 例子:
  - **A(原)**:`"range": [-100.0, 200.0]`
  - **B(现)**:`"range": {"min": -100.0, "max": 200.0}`
- 代码侧:`JsonSerializer` 解析时多一步(`obj["min"]` / `obj["max"]` 而不是直接 `arr[0]` / `arr[1]`)
- `Variable::setRange(QVariant, QVariant)` 接口**不变**,只改 JSON ↔ C++ 的转换

---

## 6.1 收官 ✅

**schema 设计定稿** → 6.2 Project 类(saveToFile) 可以开干。