# HMI 阶段 6 计划(优化版 2026-08-07)

## 目标

工程配置(变量 + 报警 + 联动)→ **JSON 文件** → 关了重开能完整恢复。

---

## 当前进度(2026-08-07)

| 节 | 内容 | 状态 | 备注 |
|---|---|---|---|
| **6.1** | JSON schema 设计 | ✅ 完成 | 4 类结构定稿,Q1-Q5 拍板 |
| **6.2** | Project 类 saveToFile | ✅ 完成 | 代码 + UI 按钮 + 验证 + 3 处优化 |
| 6.3 | Project 类 loadFromFile | ⏳ 待办 | 明天开始 |
| 6.4 | UI 打开按钮 + default.json | ⏳ 待办 | |
| 6.5 | 端到端验收 | ⏳ 待办 | |

**已完成 2 / 5**,**总估时 4-5 天**。

---

## 保存 4 类(从 6 类精简)

1. 通讯参数(TCP host/port + Modbus 详细)
2. 变量定义(name/address/dataType/deadband/poll_interval/range)
3. 报警配置(CheckerConfig list)
4. 报警联动(ActionManager mappings)

## 不做(进 backlog)

- ❌ 变量绑定序列化(走代码默认,Binding 太复杂)
- ❌ Window geometry
- ❌ Schema 自动迁移
- ❌ Reverse recovery(报警恢复 → 自动写回正常值)

---

## 关键决策(2026-08-07 拍板)

| 决策 | 选择 | 理由 |
|---|---|---|
| 默认配置 | 内置 `default.json` 跟 exe 走 | 首次运行 = 默认 demo 3 变量,开箱即用 |
| Schema 版本 | 顶层 `schema_version: "1.0"` | 5 分钟成本,以后改 schema 不慌 |
| Bindings | 代码默认,不存 | 省 2-3 天大头 |
| Load 失败 | 弹错误对话框,中止 | 简单可靠 |
| `range` 表示 | **对象 `{"min": ..., "max": ...}`**(Q2-B) | 自解释 |
| `min_duration_ms` | 单字段(每报警各设) | 显式优于隐式 |
| JSON 没列旧对象 | 全删 | 内存 = JSON,JSON 是唯一真理 |

---

## 子任务 + 估时(共 4-5 天)

| 节 | 内容 | 估时 | 状态 |
|---|---|---|---|
| **6.1** | JSON schema 设计 | 1 天 | ✅ |
| **6.2** | Project 类 saveToFile | 1.5 天 | ✅ |
| **6.3** | Project 类 loadFromFile | 1.5 天 | ⏳ |
| 6.4 | UI 按钮 + default.json | 1 天 | ⏳ |
| 6.5 | 端到端验收 | 0.5 天 | ⏳ |

---

## 文件改动

**新建**:
- `Project.h` / `Project.cpp`(主存读类)✅ 已建
- `JsonSerializer.h` / `JsonSerializer.cpp`(per-type helper)✅ 已建
- `default.json`(内置默认)— 6.4

**改**:
- `hmi.cpp` 构造(改 `loadFromJson`)— 6.3
- `hmi.ui`(加"打开"按钮)— 6.4
- `hmi.vcxproj`(注册新文件)✅ 已改(6.2 加了 JsonSerializer/Project)

---

## 实施顺序

```
6.1 ✅ → 6.2 ✅ → 6.3 ⏳ → 6.4(按钮先 stub) → 6.5
```

---

## 关键技术点

### Enum 序列化

- **Variable::DataType / Access**:`Q_ENUM` 反射 → `"Int16"` / `"ReadOnly"`
- **AlarmLevel / Direction**:无 Q_ENUM → 手写 switch

### Load 顺序(关键)

```
1. communications → 建 TcpClient 连接
2. variables → new Variable + addVariable()
3. alarms → new ThresholdAlarmChecker + addChecker()(依赖 variables)
4. actions → addMapping()(依赖 variables + alarms)
```

任何一步失败 → 中止加载,弹错误对话框。

---

## 风险点

1. **Load 顺序敏感**:variables → checkers → actions 按依赖建,不能错
2. **Enum 序列化手写**:Direction/AlarmLevel → helper,容易拼错
3. **Variable 引用 by name**:alarm 配置里的 `"variable": "Temperature"` 必须存在于 variables 数组
4. **QSaveFile 原子写**:saveToFile 用 QSaveFile 防止写到一半断电

---

## 6.2 已完成的接口

```cpp
class Project : public QObject {
public:
    explicit Project(VariableManager*, AlarmManager*, ActionManager*, TcpClient*, QObject* parent = nullptr);
    bool saveToFile(const QString& path);    // 主入口 ✅
    bool loadFromFile(const QString& path);  // 6.3 实现 ⏳
signals:
    void saved(const QString& path);
    void loadError(const QString& reason);
private:
    VariableManager* varMgr_;
    AlarmManager*    alarmMgr_;
    ActionManager*   actionMgr_;
    TcpClient*       tcp_;
};
```

---

## 6.3 待做的(明天开)

- `bool Project::loadFromFile(const QString& path)` 实现
- `fromJson()` 反向 helper(JsonSerializer 加)
- `Variable::fromJsonVariable()` / `CheckerConfig::fromJson()` 等
- hmi.cpp 构造函数里调 `loadFromFile(default.json)`(启动默认读)
- 验证:启动 → 内存里的 3 变量 = JSON 里的 3 变量

---

## 一句话

> **4-5 天,4 类配置 + 内置 default + schema_version,bindings 留 backlog。**
> **已完成 6.1 + 6.2,明天 6.3 接 loadFromFile + UI 打开按钮。**

---

来源:Wqian 2026-08-07 阶段 5 收官后讨论,优化版。同内容也存于 Biji 笔记(工控 KB,note_id 1917781068216987568)。