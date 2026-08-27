# 2026-07-06 ModbusMaster 响应处理 — 知识点总结

> 子衿整理 · 2026-07-06
> 触发: Wqian 17:48 问"总结一下这个解读运用到的所有知识"
> 范围: 阶段 2 第 2 练(手写 ModbusMaster)· 0x03 读保持响应处理流程

---

## 一、MBAP 头结构(7 字节)

| 字段 | 字节 | 干啥 | 代码里咋用 |
|---|---|---|---|
| **TID** | 0~1 | 请求/响应配对 | `qFromBigEndian<quint16>(frame.mid(0,2).constData())` |
| **PROTO** | 2~3 | 协议标识(必须 0) | (前置校验 `if (proto != 0) ...`) |
| **LEN** | 4~5 | 帧边界(剩多少字节) | 算 `frameSize = 7 + LEN` |
| **UID** | 6 | 从站地址 | `quint8 uid = static_cast<quint8>(frame[6])` |

---

## 二、PDU 结构(0x03 读保持响应)

```
字节 7:    FC (0x03 正常 / 0x83 异常)
字节 8:    byteCount (数据字节数)
字节 9~:   N × 2 字节的寄存器值(大端 BE)
```

**每寄存器 = 2 字节**(协议规定),所以 N 个寄存器 = N×2 字节。

---

## 三、字节序(大端 BE)

Modbus 默认大端: 高字节在前, 低字节在后。

```cpp
quint16 v = (hi << 8) | lo;
// frame[9]=0x12, frame[10]=0x34 → 0x1200|0x34 = 0x1234
```

**对照**:`qFromBigEndian<quint16>(ba.mid(pos, 2).constData())` 是 Qt 封装,等价但更简洁。

---

## 四、`quint8` 强转(避免有符号解读)— 协议解析命门

`QByteArray::operator[]` 返回 `char` (有符号, Windows 上 -128~127)。  
**直接用 `char` → 0x83 变 -125, 0x80+ 字节变负数。**  
`static_cast<quint8>` 强制按无符号解读 → 0x83 还是 131。

代码里用 3 次:
- `fc = static_cast<quint8>(frame[7])` — 保留 FC 最高位的"异常"标志
- `byteCount = static_cast<quint8>(frame[8])`
- `hi/lo = static_cast<quint8>(frame[offset])` — 大端拼字节必须用

**为啥命门:** `fc & 0x80` 异常判断依赖最高位; `(hi << 8) | lo` 拼字节必须把 0x80+ 当无符号。

---

## 五、位运算:`fc & 0x80` 判断最高位

Modbus 协议规定: **FC 最高位 = 1 = 异常响应**。

- `0x03` → 正常读保持响应
- `0x83` → 异常读保持响应 (0x03 | 0x80)

**协议设计技巧:** 同一字节既当"正常/异常"标志(最高位), 又保留原功能码(低 7 位 = 0x03/0x06/0x10...)。

---

## 六、`QByteArray` 切帧: `left` + `size` + `remove`

**TCP 缓冲三件套:**

| 方法 | 干啥 | 何时用 |
|---|---|---|
| `append(data)` | 攒字节 | readyRead 时 |
| `left(frameSize)` | 瞄一眼当前帧(不动缓冲) | 处理每帧前 |
| **`remove(0, frameSize)`** | **剔掉已处理字节** | 处理完每帧 |

**`left` 不删数据**, `frame` 是新对象(隐式共享底层字节)。**必须手动 `remove`**, 否则下次 `left` 还是同一段 → 死循环。

---

## 七、`QHash` 哈希表

`QHash<quint16, Pending> pending_` = key(TID) → value(请求上下文)

| 三件套 | 干啥 |
|---|---|
| 存 | `pending_[tid] = Pending{...}` (发请求时) |
| 查 | `auto it = pending_.find(tid); if (it == end()) ...` |
| 删 | `pending_.erase(it)` (处理完) |

**`find` + `end()` 是 Qt 标准做法:**
- `value(k)` → 静默返回默认值(分不清"没找到"vs"找到但值空")
- `operator[](k)` → 找不到会插入空值(污染数据)

---

## 八、`Pending` 结构(自定义上下文)

不是 Qt 给的, 是工程自定义 struct, 装"发请求时存的上下文":

- `fc` (功能码)
- `count` (寄存器数)
- `addr` (起始地址)
- `slaveId` (从站号)
- `target` (响应写回给谁)
- `sentAt` / `timeoutMs` (超时用)

**`QObject* target` 风险: 悬空指针 → 推荐用 `QPointer<QObject>`**。

---

## 九、流程控制: `continue` vs `return`

| 触发场景 | 用谁 | 原因 |
|---|---|---|
| TID 找不到 / UID 不匹配 / FC 异常 / byteCount 不匹配 | `continue` | 缓冲里可能还有别的完整帧, 看下一帧 |
| **帧不完整(`size < 9` / `size < 9 + byteCount`)** | **`return`** | **帧结构本身有问题**, 继续 while 会让残留字节拼成错位的"完整帧" |

**`return` 用得少**, 只在"继续会出大错"时。

---

## 十、错误处理"三件套"

任何"协议错位"都走:

1. `emit errorOccurred(...)` — 通知 UI
2. `pending_.erase(it)` — 从 pending 表删
3. `recvBuf_.remove(0, frameSize)` — 从 TCP 缓冲剔

---

## 十一、信号槽机制 (`emit`)

- `emit holdingReadResult(req.slaveId, req.addr, values)` — 解完数据给 UI
- `emit errorOccurred(...)` — 出错给 UI

**ModbusMaster 不直接操作 UI**, 通过信号槽解耦: 业务层 / 协议层 / UI 层各管各的。

---

## 十二、`QString::arg(...)` 格式化

```cpp
QString("错误: FC=0x%1").arg(fc, 2, 16, QChar('0'))
// fc=0x83 → "错误: FC=0x83"
// fc=0x06 → "错误: FC=0x06" (前面补 0, 2 位十六进制)
```

`2, 16, QChar('0')` = 字段宽度 2, 十六进制, 补 0。

---

## 十三、Modbus 异常码翻译表

| 码 | 含义 |
|---|---|
| 0x01 | 不支持的功能码 |
| 0x02 | **地址越界** ← 最常见 |
| 0x03 | 值越界 |
| 0x04 | 从机故障 |
| 0x05/0x06 | 从机忙/确认 |
| 0x08 | 存储奇偶错 |
| 0x0A/0x0B | 网关路径/目标失败 |

**协议错误码 → 人话** 的标准做法。

---

## 十四、`QVector::reserve` + `append`

```cpp
QVector<quint16> values;
values.reserve(req.count);    // 预分配, 避免反复 realloc
for (int i = 0; i < req.count; ++i) {
    values.append(...);
}
```

**`reserve` 是性能习惯** — 数据量大时省内存拷贝。

---

## 十五、`qDebug()` 日志

```cpp
qDebug() << "[ModbusMaster] 读保持成功 TID=" << tid
         << "slave=" << req.slaveId << "addr=" << req.addr
         << "values=" << values;
```

Qt 输出流, `<<` 串联, **变量直接打**(不用 `%d %s`)。

---

## 全流程(响应处理 12 步)

```
1. 切帧            left(frameSize)                  ← QByteArray 三件套
2. 取 FC           frame[7] + static_cast            ← quint8 强转
3. 异常位          (fc & 0x80) != 0?                 ← 位运算 + 协议设计
   ├─ 是 → 翻译异常码 + emit error + 清理 + continue
   └─ 否 ↓
4. 取 TID          qFromBigEndian<quint16>           ← 大端读 2 字节
5. TID 配对        find(tid) == end()?               ← QHash 三件套
   ├─ 是 → 丢帧 + continue
   └─ 否 → Pending req = it.value()
6. UID 校验        uid == req.slaveId?               ← 协议字段
   ├─ 否 → 丢帧 + continue
   └─ 是 ↓
7. FC 类型         fc == 0x03?                       ← 协议字段
   ├─ 否 → 报错 + continue
   └─ 是 ↓
8. 最小长度        size < 9?                         ← 协议字段
   ├─ 是 → return
   └─ 否 ↓
9. 数据长度        size < 9 + byteCount?             ← 协议字段
   ├─ 是 → return
   └─ 否 ↓
9.5 byteCount 校验 byteCount == count*2?             ← 业务字段
   ├─ 否 → 报错 + continue
   └─ 是 ↓
10. 解数据         for i: (hi<<8)|lo                 ← 大端 + 数组
11. qDebug + emit holdingReadResult                  ← 调试 + 信号槽
12. pending_.erase + recvBuf_.remove                 ← 清理三件套
```

---

## 还差两段(阶段 2 第 2 练收尾)

- **段 8. 超时清理** — 定时器扫 `pending_`, 把超时未响应的 TID 清理掉
- **段 9. 收尾存档** — `HMI阶段2第2练_手写ModbusMaster`
