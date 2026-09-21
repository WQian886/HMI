# 阶段 E · E.2 · 绑定 + 属性面板(Q_PROPERTY 反射)调研决策(从这里继续)

> 适用:**E.2 阶段计划 + 6 个决策点 + 业余节奏周拆分**,开 E.2 编码前先看完
> 维护人:Wqian + AI 助手  ｜  创建:2026-09-21
> 配套:`docs/阶段E.0_从这里继续.md` §2(9 决策)/ §3.3(WYSIWYG 8 大功能)/ §6(22 周计划)/ §9(已知坑)/
>      `docs/路线图.md` 第 25-50 行 / `docs/UI规划调研与方案.md` v1.3

---

## 0. 一句话

**E.2 = 基于 QMetaObject/QMetaProperty 反射的属性面板 + 复用 ControlBase::bind() 的 Variable 绑定机制 + QUndoCommand 包装的属性修改,业余 2 周 ≈ 周 10-11**。

E.2 是 E.4 WYSIWYG 编辑器的"前置依赖":E.4a 拖拽 → E.4b 选中 → E.4c 属性修改(JSON/Undo),其中 E.4c 的属性修改就靠 E.2 的反射机制。

---

## 1. 改了什么(2026-09-21 — 本轮 E.2 决策)

| 项 | 内容 |
|---|---|
| **本文档新建** | `docs/阶段E.2_从这里继续.md`(路线图锁 E.2 P0) |
| **6 个决策点锁定** | 见节 2 |
| **属性面板架构 + widget 映射** | 见节 3 |
| **绑定机制 + 编辑/运行时隔离** | 见节 4 |
| **Undo 集成 + JSON 序列化钩子** | 见节 5 |
| **业余节奏周拆分** | 见节 6 |
| **下一步:E.2 第 1 步实现** | 见节 7 |

---

## 2. 决策锁定表(6 个)

| # | 决策点 | 选择 | 锁理由 |
|---|---|---|---|
| 1 | **属性面板生成方式** | **A 纯 QMetaObject 反射** | E.0 决策 #6 已锁方向;22 控件不写表单代码;新增控件零成本接入 |
| 2 | **属性编辑器 widget 映射** | **B 类型 → 内置 QWidget + Qt 反射**(见节 3.2 映射表) | 零三方依赖,跟项目 LGPL 边界一致;Q_ENUM 用 QMetaEnum 反射拿字符串列表 |
| 3 | **绑定机制** | **A 复用 ControlBase::bind()**(已有接口) | E.1 已实现且测试覆盖;不再造轮子;新增 PropertySheet::bindVariable 走 ControlBase |
| 4 | **属性修改触发** | **C 编辑控件 + QUndoCommand 包装**(实时 + Undo) | E.0 决策 #4 锁 QUndoStack;操作员模式直接 edit,工程师模式所有改动进 Undo |
| 5 | **运行时 vs 编辑模式隔离** | **B PropertySheet enabled=false 即可**(运行时禁编辑) | 简单一致;操作员看不到属性面板(在 EditorMainWindow 里),不需要细粒度权限 |
| 6 | **E.3 JSON 集成方式** | **A toJson/fromJson 接口已在 ControlBase,属性面板只调 setter 不碰 JSON** | E.2 跟 E.3 解耦;E.3 阶段只调 toJson/fromJson,不用动属性面板 |

---

## 3. 设计决策

### 3.1 属性面板架构(PropertySheet)

```
┌─ EditorMainWindow(工程师模式主窗口)─────────────┐
│  ┌─ 工具栏 ───────────────────────────────┐      │
│  │ [保存] [撤销] [重做] [预览] [发布]      │      │
│  └────────────────────────────────────────┘      │
│  ┌─ 左:控件调色板(E.4a)┐ ┌─ 中:画布(E.4a) ┐ ┌─ 右:属性面板(E.2)┐│
│  │ [Label]              │ │                │ │ ★ E.2 本阶段       ││
│  │ [NumberDisplay]      │ │  ┌──────────┐  │ │ ┌─────────────────┐││
│  │ [BarControl]         │ │  │ Label    │  │ │ │ position         │││
│  │ [GaugeControl]       │ │  │ "Hello"  │  │ │ │  x: [120] y: [40]│││
│  │ [LED]                │ │  └──────────┘  │ │ │ size             │││
│  │ [Button]             │ │                │ │ │  w: [120] h: [40]│││
│  │ ... (22 控件)        │ │                │ │ │ variableName     │││
│  └─────────────────────┘ │                │ │ │  [plc1.Speed   ] │││
│                          │                │ │ │ value            │││
│                          │                │ │ │  [1234        ]  │││
│                          │                │ │ │ format (派生)    │││
│                          │                │ │ │  [%.2f         ]  │││
│                          │                │ │ │ unit (派生)      │││
│                          │                │ │ │  [ rpm         ]  │││
│                          │                │ │ └─────────────────┘││
│                          │                │ │ [绑定变量...] (按钮)││
│                          └────────────────┘ └─────────────────────┘│
└──────────────────────────────────────────────────┘
```

### 3.2 Q_PROPERTY → 编辑器 widget 映射(核心表)

| Q_PROPERTY 类型 | 编辑器 widget | 说明 |
|---|---|---|
| `QPointF` | 2× QDoubleSpinBox (x, y 标签) | 派生 Q_PROPERTY,无 setter(派生用 setPos) |
| `QSizeF` | 2× QDoubleSpinBox (w, h 标签) | setSize |
| `QString` | QLineEdit | variableName / format / unit 等 |
| `QString`(enum) | QComboBox | Q_ENUM 类型 — 用 QMetaEnum 反射拿 keyToValue/valueToKey |
| `QString`(color hex #RRGGBB) | QPushButton + QColorDialog | 点按钮弹色盘,选完填 hex |
| `QString`(file path) | QLineEdit + "..." QFileDialog | imageControl 的 imagePath 用 |
| `QVariant` (int) | QSpinBox | setRange(min, max) |
| `QVariant` (double) | QDoubleSpinBox | setDecimals(2) + setRange |
| `QVariant` (bool) | QCheckBox | |
| `QVariant` (QString) | QLineEdit | 派生属性 value 由 setValue() 处理 |

**实现思路**:
```cpp
class PropertySheet : public QWidget {
    Q_OBJECT
public:
    explicit PropertySheet(QUndoStack* undoStack, QWidget* parent = nullptr);

    // ★ 选中某个 ControlBase 时调,反射生成表单
    void setTarget(ControlBase* target);

private:
    void rebuildForm();   // QMetaObject::propertyCount() → 遍历 metaProperty(i)
    QWidget* createEditorForProperty(const QMetaProperty& prop);
    void connectEditorToProperty(QWidget* editor, const QMetaProperty& prop);

    QFormLayout* form_;
    ControlBase* target_ = nullptr;
    QUndoStack*  undoStack_;
};
```

### 3.3 反射遍历示例(伪代码)

```cpp
void PropertySheet::rebuildForm() {
    // 清空旧表单
    while (form_->count() > 0) { /* removeRow */ }

    if (!target_) return;
    const QMetaObject* meta = target_->metaObject();

    for (int i = 0; i < meta->propertyCount(); ++i) {
        const QMetaProperty prop = meta->property(i);
        if (!prop.isWritable()) continue;   // 跳过 displayText 这类只读

        // ★ 标签:prop.name() → "position" / "size" / "variableName"
        QWidget* editor = createEditorForProperty(prop);
        if (!editor) continue;             // 不支持的类型跳过

        // ★ 初值:target_->property(prop.name())
        setEditorValueFromProperty(editor, prop, target_->property(prop.name()));

        // ★ 修改触发:setXxx → PropertyChangeCommand(QUndoCommand)
        connectEditorToProperty(editor, prop);

        form_->addRow(prop.name(), editor);
    }

    // ★ 派生类的额外 Q_PROPERTY 也会自动出现(因为 metaObject() 走整条 MRO 链)
}
```

### 3.4 enum 类型反射示例

```cpp
class BarControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(Direction direction READ direction WRITE setDirection NOTIFY directionChanged)
public:
    enum class Direction { Horizontal, Vertical };
    Q_ENUM(Direction)
    ...
};

// PropertySheet 检测 QMetaProperty.isEnumType() == true
// 调 QMetaEnum::valueToKey()/keyToValue() 拿字符串列表
QComboBox* createEnumEditor(const QMetaProperty& prop, const QMetaEnum& menum) {
    auto* combo = new QComboBox;
    for (int i = 0; i < menum.keyCount(); ++i) {
        combo->addItem(menum.key(i));   // "Horizontal" / "Vertical"
    }
    return combo;
}
```

---

## 4. 绑定机制 + 编辑/运行时隔离

### 4.1 绑定走 ControlBase::bind()(已有,不动)

```cpp
// hmi/src/widgets/base/ControlBase.h:75-76 — 已有
void bind(Variable* v);   // 连接 valueChanged → updateDisplay
void unbind();
```

E.2 只需要在属性面板加一个"绑定变量..."按钮:

```cpp
// PropertySheet 上的按钮点击:
void PropertySheet::onBindVariableClicked() {
    if (!target_) return;

    // ★ 弹变量选择对话框(用 VariableManager + 树状显示 plc1 → 所有变量)
    auto* dlg = new VariablePickerDialog(VariableManager::instance(), this);
    if (dlg->exec() == QDialog::Accepted) {
        QString varName = dlg->selectedVariableName();   // "plc1.Speed"
        // ★ 走 QUndoCommand(可撤销)
        undoStack_->push(new BindVariableCommand(target_, varName));
    }
}
```

### 4.2 编辑模式 vs 运行时隔离

| 模式 | 入口 | 属性面板 | 控件交互 |
|---|---|---|---|
| **运行时(操作员)** | login → "operator"/"viewer" | 不显示(E.2 设计上就不在 Runtime Mode 主窗口) | 控件正常交互(Button/Toggle/Slider 写值) |
| **编辑模式(工程师)** | login → "engineer"/"admin" | 显示,可编辑 | 控件选中框 + 8 handle(E.4b),不响应写值 |

**实现要点**:
- EditorMainWindow 内部 `setEditorMode(bool)`:
  - true → 显示属性面板(QDockWidget RightDock)
  - false → 隐藏 RightDock,只显示画布预览
- **不修改 ControlBase**,运行时 / 编辑时都是同一对象,通过 UI 层切换可见性

### 4.3 PropertyChangeCommand(QUndoCommand 子类)

```cpp
class PropertyChangeCommand : public QUndoCommand {
public:
    PropertyChangeCommand(ControlBase* target,
                          const QString& propName,
                          const QVariant& oldValue,
                          const QVariant& newValue)
        : target_(target), propName_(propName),
          oldValue_(oldValue), newValue_(newValue) {}

    void undo() override {
        target_->setProperty(propName_.toUtf8().constData(), oldValue_);
    }
    void redo() override {
        target_->setProperty(propName_.toUtf8().constData(), newValue_);
    }

private:
    ControlBase* target_;
    QString      propName_;
    QVariant     oldValue_;
    QVariant     newValue_;
};
```

**触发流程**:
```
属性面板 QDoubleSpinBox valueChanged(newVal)
  ↓
compare(oldVal, newVal) 不等
  ↓
new PropertyChangeCommand(target, "x", oldVal, newVal)
  ↓
undoStack_->push(cmd)
  ↓
cmd.redo()  →  target_->setProperty("x", newVal)
              → 触发 positionChanged 信号
              → 画布上控件立即移动
  ↓
Ctrl+Z → cmd.undo()  →  setProperty("x", oldVal)  → 控件回到原位
```

---

## 5. Undo 集成 + JSON 序列化钩子

### 5.1 QUndoStack 跨命令的归属

```
QUndoStack 归属:EditorMainWindow(单例)
  - PropertyChangeCommand(属性面板触发)
  - AddControlCommand(调色板拖入触发,E.4a)
  - DeleteControlCommand(选中删除触发,E.4c)
  - MoveControlCommand(8 handle 拖动触发,E.4b)
  - AddMultipleControls(多选复制粘贴触发,E.4d)
```

**关键约束**:
- 撤销后 redo 要 work → QUndoCommand 必须正确存 old/new value
- 合并连续同类命令(`mergeWith`):拖 8 handle 1px 一次太碎,合并为 1 个 5px 命令
- 内存控制:限制 undoStack 最大深度(默认 100)

### 5.2 与 E.3 JSON 序列化钩子(只读侧)

E.2 不实现 JSON 存读,但要预留接口避免 E.3 重构:

```cpp
// E.2 在 ControlBase 加 1 个 method(只读侧)
class ControlBase : ... {
public:
    // ★ E.3 会用:遍历所有 Q_PROPERTY → 输出 QJsonObject
    QJsonObject propertiesToJson() const;   // 基类反射所有 Q_PROPERTY

    // E.1 已实现的 toJson()/fromJson() 内部调 propertiesToJson()
    virtual QJsonObject toJson() const override {
        QJsonObject obj = propertiesToJson();
        // ★ 派生类可追加自定义字段(向后兼容)
        return obj;
    }
};
```

**约定**:
- Q_PROPERTY name ↔ JSON key(完全相同,如 "position" → "position")
- 类型转换:QPointF → {"x": 1.0, "y": 2.0}(Qt 内置支持,QJsonValue::fromVariant)
- E.3 实现 `fromJson()` 调 `setProperty(name, jsonValue.toVariant())`,完全走属性面板同一条路径

---

## 6. 业余节奏 2 周拆分(每周 6-8 h,周 10-11)

| 周 | 工作 | 状态 |
|---|---|---|
| **周 10**(本阶段前半) | ① `src/editor/PropertySheet.h/.cpp`(反射 + widget 映射)<br>② `PropertyChangeCommand` + `BindVariableCommand`<br>③ `src/editor/VariablePickerDialog`(选变量用,简单 QDialog + QTreeWidget)<br>④ `tests/tst_PropertySheet`(反射覆盖 + undo 触发 + 各类型 editor)<br>⑤ build + commit | ⏳ |
| **周 11**(本阶段后半) | ① `EditorMainWindow` 骨架(QMainWindow + QDockWidget + QGraphicsView)<br>② 集成 PropertySheet + QUndoStack<br>③ 操作员 / 工程师模式切换(`setEditorMode(bool)`)<br>④ `tests/tst_EditorMainWindow`(模式切换 + 选中切换 + 撤销/重做)<br>⑤ 文档 + commit | ⏳ |

**业余总时长:2 周 ≈ 0.5 个月**(符合 E.0 第 235 行规划)

---

## 7. 下一步:E.2 第 1 周(下次开工)

### 5 步路线图

```
1. hmi/src/editor/PropertySheet.h/.cpp
   - 反射遍历 QMetaObject::property(i)
   - 实现节 3.2 的 widget 映射表(QPointF/QSizeF/QString/QVariant/Q_ENUM/QColor)
   - 不接 Undo(本周只过编译)

2. hmi/src/editor/PropertyChangeCommand.h/.cpp
   - QUndoCommand 子类(4 步 undo/redo)
   - 接 QUndoStack

3. hmi/src/widgets/base/ControlBase.h/.cpp
   - 加 propertiesToJson() 反射接口(只读侧)
   - toJson() 调 propertiesToJson() + 派生类追加(已有 toJson() 实现保留)

4. 三件套同步(CMake / vcxproj / filters / tests)
   - CMakeLists.txt: add src/editor/PropertySheet.cpp + PropertyChangeCommand.cpp
   - vcxproj + filters: QtMoc + ClInclude + ClCompile
   - tests/CMakeLists.txt: add_hmi_test(tst_PropertySheet)

5. tst_PropertySheet 8 用例
   - reflect_findsAllWritableProperties
   - createEditorForProperty_intDoubleString
   - createEditorForProperty_enum (Q_ENUM)
   - createEditorForProperty_color
   - editProperty_triggersUndoCommand
   - undoRestoresOldValue
   - redoAppliesNewValue
   - propertiesToJson_roundTripsThroughFromJson

6. build hmi.exe + tst_PropertySheet + commit
   - MSBuild + QtMsBuild 三件套合规
   - ctest 37 → 45 通过
   - commit message:"feat(editor): E.2 属性面板 PropertySheet + Q_PROPERTY 反射"
```

### 验收(E.2 完成时)

- [ ] PropertySheet 能正确生成 LabelControl 的 5 个属性编辑器
- [ ] 编辑 position 数字 → 画布控件立即移动
- [ ] Ctrl+Z → 控件回到原位
- [ ] 编辑 enum 类型(Direction / DataType / Access)→ QComboBox 出现
- [ ] 编辑 color 类型 → 弹 QColorDialog,选完回填 hex
- [ ] propertiesToJson 输出的 JSON 能被 fromJson() 完全还原
- [ ] 8 个用例全过,ctest 45/45 全过
- [ ] hmi.exe build 0 错误 0 警告

---

## 8. 关联文档

- `docs/阶段E.0_从这里继续.md` §2 决策 #4 / #6(Undo 栈 / 控件实现方案)
- `docs/阶段E.0_从这里继续.md` §3.2(ControlBase.h 接口已部分实现,E.2 在它上面加 propertiesToJson)
- `docs/阶段E.0_从这里继续.md` §3.3(WYSIWYG 8 大功能 — E.2 是 E.4c 的前置)
- `docs/阶段E.0_从这里继续.md` §9 已知坑(已在 E.1 阶段总结踩过)
- `docs/路线图.md` 第 25-50 行 — E 战略
- `docs/UI规划调研与方案.md` v1.3 — 编辑器框架
- `docs/进度存档_2026-09-20.md` — E.0 决策背景

---

## 9. 已知坑(下次开 E.2 注意)

1. **Q_PROPERTY enum 类型反射** — Qt 6.5 之前 QMetaEnum 接口略繁琐,要用 `QMetaProperty::enumerator()`,不是 `property(i).valueToKey()`.Qt 6.8 已修复,本项目用 6.8.3 OK
2. **QPointF/QSizeF 通过 setProperty 写入** — Qt 6.5+ 支持但需要 `QVariant::fromValue(p)` 显式构造。代码里要 `target_->setProperty("position", QVariant::fromValue(QPointF(x, y)))`,不能直接传 QPointF
3. **PropertyChangeCommand 的合并(mergeWith)** — 拖控件 1px 一次会被 QUndoStack 记录 100 次,必须实现 mergeWith + id() 返回相同类名。E.4b 8 handle 拖动时重点关注
4. **反射拿不到 default value** — QMetaProperty 没"default value"接口。只能从控件初始状态读 1 次(`new LabelControl; props["variableName"]`)。这影响"恢复默认值"按钮 — E.4c 留 todo
5. **绑定 Variable 时不要触发 valueChanged 写值** — bind() 只连 `Variable::valueChanged → ControlBase::updateDisplay`,不要反向触发 `ControlBase → Variable::setValue`,否则编辑器拖动控件会乱写 PLC。已有 bind() 实现已经走单向,E.2 不要碰
6. **Q_PROPERTY 顺序** — metaObject() 返回顺序是声明顺序,派生类加的 property 会排在基类后面。属性面板按这个顺序显示,后续调整时注意排版
7. **QFormLayout 移除 row** — Qt 6 改了 API,QFormLayout::takeAt() 后必须 delete 或 reparent,否则内存泄漏 + 僵尸 widget。代码里小心

---

**E.2 收工,下次开工 → 周 10 第 1 步 PropertySheet 实现(5 步路线图见节 7)**
