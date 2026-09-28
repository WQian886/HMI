# 阶段 E · E.0 · 调研 + 决策锁定(从这里继续)

> 适用:**E 阶段计划 + 9 个决策锁定 + 业余节奏 22-27 周拆分**,开 E.1 之前先看完
> 维护人:Wqian + AI 助手  ｜  创建:2026-09-20
> 配套:`docs/路线图.md` 第 25-50 行(9 阶段总览+E 战略) / `docs/UI规划调研与方案.md` v1.3 / `docs/进度存档_2026-09-20.md` / `docs/阶段D.2_从这里继续.md` 第 6 节(D.4 节) / `docs/阶段D.1_从这里继续.md`
> ⚠ **状态(2026-09-28 基于代码核对)**:🟡 **E 阶段收尾中** — E.0(本文件)决策锁定 ✅ + E.1 控件库 18 个已落地(`src/widgets/*`) + E.2 PropertySheet 反射 + E.4a 调色板/拖拽 + E.4b 8 handle 缩放 + E.4c QSizeF/QPointF 复合编辑器 + E.4d 多选/对齐/6 等尺寸 + E.4d-follow-1~7 收尾链。**🔴 SelectionFrameItem paint() 视觉延后未做**(对应 `bug图/图1.png`)。完整阶段状态见 `docs/路线图.md` 和 `hmi/CHANGELOG.md` v0.2.0-rc1。

---

## 0. 一句话

**E 阶段 = WYSIWYG 全功能拖拽编辑器(QGraphicsItem 派生 + 1 个 hmi.exe 内嵌工程师模式)+ Lua(sol2) + 主题(亮/暗)+ 22 个控件 + JSON 存读,业余节奏 22-27 周 ≈ 5.5-7 个月**。

---

## 1. 改了什么(2026-09-20 — 本轮 E.0 决策)

| 项 | 内容 |
|---|---|
| **本文档新建** | `docs/阶段E.0_从这里继续.md`(路线图.md 第 33 行锁 E 阶段 P0) |
| **9 个决策点锁定** | 见节 2 |
| **E.1 控件库详细方案** | 见节 3 |
| **E.4 WYSIWYG 详细方案** | 见节 4 |
| **E.6 Lua 详细方案** | 见节 5 |
| **业余节奏 22 周计划** | 见节 6 |
| **下一步:E.1 第 1 个控件 LabelControl** | 见节 7 |

---

## 2. 决策锁定表(9 个)

| # | 决策点 | 选择 | 锁理由 |
|---|---|---|---|
| 1 | **E 阶段时间投入档位** | **A 全速业余**(每周 6-8 h) | 路线图锁 P0,商业化杀手锏,11 月底-12 月完成 |
| 2 | **WYSIWYG 范围** | **A 全 8 大功能**(画布/选中/属性/Undo/多选/对齐/JSON/预览) | 路线图.md 第 44-48 行锁,工业级完整编辑器 |
| 3 | **hmi-editor.exe 关系** | **B 1 个 hmi.exe(内嵌工程师模式)** | Wqian 自用 + 商用化客户场景多单机部署,免运维装 2 个程序 |
| 4 | **Lua 脚本引擎** | **B 保留 Lua(sol2 LGPL v2.1)** | 化工/电力复杂现场调优,纯预定义动作不够 |
| 5 | **E.5 主题** | **B 做**(亮/暗 2 套 theme.json) | 客户喜欢换肤;1 周工作量,可接受 |
| 6 | **控件实现方案** | **A QGraphicsItem 派生** | Qt 官方推荐,与 WYSIWYG 编辑器天然契合;B/C 是 fallback |
| 7 | **测试策略** | **A 每控件独立测试 target** | 22 个新 target,build 慢但定位快,符合 Qt 项目惯例 |
| 8 | **画布坐标系** | **A 逻辑坐标(0-1000 × 0-1000)** | 跨分辨率兼容,商用化必备 |
| 9 | **hmi.exe 加载 JSON** | **A 要**(E.3 顺便做 loadFromFile) | OEM 批量部署必备;纯复用 E.3 schema 零成本 |
| 10 | (无 Wqian 选项,自动锁定) | **每控件独立测试目标** | 来自 #7 |
| 11 | (无 Wqian 选项,自动锁定) | **C.0 schema_version 锁 3** | E.3 JSON 存读触发 — bump 2→3 + migrateV2ToV3 |

---

## 3. 设计决策

### 3.1 整体架构(hmi.exe 内嵌工程师模式)

```
hmi.exe 进程:
│
├─ 启动 →
│  └─ LoginDialog(已有,B.2 阶段)
│     ├─ 操作员身份(username/password) → 操作员模式
│     │  └─ hmi.cpp 主窗口(Runtime Mode)— 现有功能 + D 阶段尾巴控件
│     └─ 工程师密码(login() password 是"eng"前缀或 AuditLog "eng"角色)→ 工程师模式
│        └─ EditorMainWindow(继承 hmiClass 主窗口)
│           ├─ 顶部 toolbar:画布切换 / 撤销重做 / 模拟运行 / 保存
│           ├─ 左侧:控件调色板(22 控件图标,QListWidget 拖源)
│           ├─ 中间:QGraphicsView(画布,逻辑坐标 0-1000 × 0-1000)
│           ├─ 右侧:属性面板(QMetaObject 反射 E.2 阶段)
│           └─ 底部:状态栏(坐标 / 选中控件名 / 文件名)
└─ 模式进入记录 AuditLog("editor.mode.enter" / "editor.mode.exit")
```

### 3.2 QGraphicsItem 派生基类 ControlBase.h(E.1 基础)

```cpp
class ControlBase : public QGraphicsObject {
    Q_OBJECT
    Q_PROPERTY(QPointF position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(QSizeF size READ size WRITE setSize NOTIFY sizeChanged)
    Q_PROPERTY(QString variableName READ variableName WRITE setVariableName NOTIFY variableChanged)
    Q_PROPERTY(QVariant value READ value WRITE setValue NOTIFY valueChanged)
public:
    explicit ControlBase(QGraphicsItem* parent = nullptr);

    // ★ 通用接口 — 22 控件都实现
    virtual void bind(Variable* v);            // 绑定 Variable(自动监听 valueChanged)
    virtual void unbind();                      // 解绑

    // ★ 序列化 — E.3 JSON 存读用
    virtual QJsonObject toJson() const;
    virtual void fromJson(const QJsonObject& obj);

    // ★ 重写 QGraphicsItem
    QRectF boundingRect() const override = 0;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override = 0;

signals:
    void positionChanged();
    void sizeChanged();
    void variableChanged();
    void valueChanged();
};
```

**22 个派生控件**(分批):
- **E.1a 显示 6** + 元件 4 = 10 个(业余 2-3 周)
  - LabelControl, NumberDisplay, TextDisplay, BarControl, GaugeControl, LedControl
  - ImageControl, RectangleControl, LineControl, GroupControl
- **E.1b 控制 4** + 数据 3 = 7 个(业余 2 周)
  - ButtonControl, ToggleControl, SliderControl, InputEditControl
  - TrendControl(reuse D.2 TrendWidget)/ TableControl / AlarmListControl(复用 AlarmManager)
- **E.1c 图形工业 5**(业余 2-3 周)
  - PipeControl, ValveControl, PumpControl, MotorControl, TankControl

### 3.3 WYSIWYG 8 大功能(E.4 拆分)

| # | 功能 | 估时 | 关键 API |
|---|---|---|---|
| ① | 拖拽画布 + 控件调色板 | 2 周 | QGraphicsView + QListWidget drag + QMimeData |
| ② | 选中/移动/缩放 | 3 周 | QGraphicsItem itemChange + 8 handle rect + Qt::Shift modifier |
| ③ | 属性面板(Q_PROPERTY 反射) | 2 周 | QMetaObject + QMetaProperty + QFormLayout |
| ④ | Undo/Redo(QUndoStack) | 1 周 | QUndoCommand 子类 + QUndoStack push |
| ⑤ | 多选 / 复制粘贴 | 1 周 | QGraphicsScene selectedItems + QClipboard |
| ⑥ | 对齐线 / 参考线 | 1 周 | sceneEventFilter + 临时辅助线绘制 |
| ⑦ | 保存/加载 JSON(E.3 复用) | 0.5 周 | Project::saveToFile / loadFromFile |
| ⑧ | 实时预览(reuse hmi renderer) | 1.5 周 | EditorMainWindow 切换到 PreviewMode + 同一份 paint |

### 3.4 Lua + 8 类预定义动作(E.6)

**8 类预定义动作**(覆盖 80% 场景,E.4 阶段完成时一起交付):
- 启动 / 停止 / 复位 / 写值 / 切画面 / 弹报警确认 / 弹修改密码 / 触发事件

**Lua 引擎**(E.6 单独阶段):
- sol2 LGPL v2.1(< 5 MB)
- ButtonControl 集成 `setLuaScript(QString)` + 简单 QPlainTextEdit 编辑器
- 沙箱:Lua 解释器禁止 `os.execute` / `io.*` / `require`,只暴露 `read_var/write_var/os.time` 4 个函数
- **商用化前**:律师审 LGPL 边界 + 沙箱安全性报告

### 3.5 主题(E.5)

```
hmi/
└─ themes/
   ├─ light.json    浅色工业风(默认)
   └─ dark.json     深色 OLED 省电
   {
     "name": "light",
     "colors": {
       "background": "#F0F0F0",
       "foreground": "#202020",
       "border": "#808080",
       "alarm": "#FF4040"
     },
     "fonts": { "default": "Microsoft YaHei", "size": 10 }
   }
```

`Theme` 类加载 + ControlBase.paint() 调 `Theme::color("foreground")` 拿颜色。

---

## 4. 验证(每子阶段测试覆盖)

| 阶段 | 新增测试 | 累计目标 | 关键用例 |
|---|---|---|---|
| E.0(本轮) | 0 | 20 | — |
| E.1a(显示 + 元件 10) | +25 | 45 | 每控件 2-3 用例(构造/paint/property) |
| E.1b(控制 + 数据 7) | +18 | 63 | ButtonControl clicked / SliderControl value 同步 / TrendControl reuse |
| E.1c(图形工业 5) | +13 | 76 | 5 控件 paint 不崩 + 边界 |
| E.2(绑定+属性) | +8 | 84 | 枚举反射/数值读写/绑定自动更新 |
| E.3(JSON 存读) | +5 | 89 | save/load/roundTrip/migrate v2→v3 |
| E.4a(画布+调色板+拖拽) | +6 | 95 | 拖入/拖出/位置正确 |
| E.4b(选中/移动/缩放) | +8 | 103 | 8 handle 响应 + Shift 等比 + 边界限制 |
| E.4c(属性+Undo+JSON) | +7 | 110 | QUndoStack 撤销/重做 + 属性绑定 + JSON 保存后属性还在 |
| E.4d(多选+对齐+参考线+预览) | +8 | 118 | 多选/对齐/参考线吸附/预览切回实时数据 |
| E.5(主题) | +4 | 122 | 加载/应用/切换主题后控件重新渲染 |
| E.6(Lua+8 类) | +8 | 130 | 8 类各 1 用例 + Lua 引擎加载/执行/沙箱 |

**E 阶段全完成预计 130 个测试**(原 20 + E 阶段 +110)

---

## 5. 数据流(关键路径)

### 5.1 启动 + 模式切换
```
hmi.exe 启动
  ↓
main.cpp → QApplication + plugins path(D.1)
  ↓
LoginDialog(B.2)
  ├─ 操作员身份 → hmiClass 主窗口(Runtime Mode)
  └─ 工程师密码 → EditorMainWindow 包裹 hmiClass(Editor Mode)
      ├─ lazy init 控件调色板 + 属性面板
      ├─ QGraphicsView 显示当前工程(ControlBase 实例集合)
      ├─ 画布拖入控件 → 调 ControlBase::bind() → 监听 VariableManager
      └─ saveToFile(.hmi.json)→ E.3 schema_version=3
```

### 5.2 hmi.exe 加载 JSON(E.3 + Q10 = A)
```
hmi.exe 启动
  ↓
loadFromFile(.hmi.json)
  ├─ 解析 schema_version(2 → migrateV2ToV3;3 → 直读)
  ├─ rebuild Screen + Controls(ControlBase 派生)
  ├─ each ControlBase::bind(VariableManager::findVariable)
  └─ Runtime Mode 直接画 16 控件 + 显示实时 PLC 数据
```

### 5.3 WYSIWYG 编辑保存(E.4 + E.3)
```
Editor Mode 拖控件到画布
  ↓
画布上每个 ControlBase 实例 → toJson() → JSON array
  ↓
EditorMainWindow::save() → Project::saveToFile
  ↓
.hmi.json → 工程文件保存
  ↓
下次启动 loadFromFile → rebuild 控件 + enter Edit Mode 直接显示刚才编辑的画布
```

---

## 6. 业余节奏 22 周拆分(每周 6-8 h)

| 周 | 工作 | 状态 |
|---|---|---|
| **本周 1** | E.0 调研 + 文档(本轮) | ⏳ 进行中 |
| **周 2** | E.1a-1:ControlBase.h 基类 + LabelControl + NumberDisplay + TextDisplay | ⏳ |
| **周 3** | E.1a-2:BarControl + GaugeControl + LedControl(显示 6 完) | ⏳ |
| **周 4** | E.1a-3:ImageControl + RectangleControl + LineControl + GroupControl(元件 4 完) | ⏳ |
| **周 5** | E.1b-1:ButtonControl + ToggleControl | ⏳ |
| **周 6** | E.1b-2:SliderControl + InputEditControl | ⏳ |
| **周 7** | E.1b-3:TrendControl(复用 D.2)+ TableControl + AlarmListControl(数据 3 完) | ⏳ |
| **周 8** | E.1c:PipeControl + ValveControl | ⏳ |
| **周 9** | E.1c:PumpControl + MotorControl + TankControl(图形工业 5 完) | ⏳ |
| **周 10** | E.2:绑定 + 属性面板(Q_PROPERTY 反射) | ⏳ |
| **周 11** | E.3:JSON 存读(schema_version 3 + load/save + migrate) | ⏳ |
| **周 12-13** | E.4a:画布 + 调色板 + 拖拽 | ⏳ |
| **周 14-15** | E.4b:选中/移动/缩放(8 handle + Shift 等比) | ⏳ |
| **周 16-17** | E.4c:属性面板 + Undo/Redo + JSON | ⏳ |
| **周 18-19** | E.4d:多选 + 对齐 + 参考线 + 实时预览 | ⏳ |
| **周 20** | E.5:主题(亮/暗 2 套 + ControlBase 集成) | ⏳ |
| **周 21-22** | E.6:Lua 脚本引擎 + 8 类预定义动作 + ButtonControl 集成 | ⏳ |
| **周 23-24** | 缓冲 + 文档整理 + 经验沉淀 | ⏳ |

**业余总时长:22-27 周 ≈ 5.5-7 个月**(2027 年 3-5 月完成 E 阶段)

---

## 7. 下一步:E.1 第 1 个控件 LabelControl(下次开工)

### 5 步路线图

```
1. hmi/src/widgets/base/ControlBase.h/.cpp(基类)
   - QGraphicsObject 派生 + Q_PROPERTY 5 个 + 接口 4 个
   - 教训 5.1:QtMoc + ClInclude + ClCompile 三件套

2. hmi/src/widgets/LabelControl.h/.cpp
   - paint() 画文字(QPainter.drawText)
   - variable 绑定 → 文字更新
   - 选 0 个控件 + 4 个用例(construct / paint / bind / fromJson)

3. 三件套同步(CMake / vcxproj / filters / tests)
   - CMakeLists.txt: add src/widgets/base/ControlBase.cpp + LabelControl.cpp
   - vcxproj + filters: QtMoc + ClInclude + ClCompile
   - tests/CMakeLists.txt: add_hmi_test(tst_LabelControl)

4. tst_LabelControl 5 用例
   - construct_createsItem
   - paint_doesNotCrash
   - bind_setsValueAndEmits
   - fromJson_loadsRestoresValues
   - toJson_serializesAllFields

5. build hmi.exe + tst_LabelControl + commit
   - MSBuild + QtMsBuild 三件套合规
   - ctest 20 → 25 通过
   - commit message:"feat(canvas): E.1 ControlBase.h + LabelControl 骨架"
```

### 验收(LabelControl 完成时)

- [ ] ControlBase.h 接口稳定(22 控件后续都用)
- [ ] LabelControl 能在 QGraphicsView 看到文字("Speed: 123.50")
- [ ] bind(plc1.Speed) → 文字每 100ms 更新(来自 PollScheduler 写值)
- [ ] fromJson/toJson roundTrip 一致
- [ ] 4 个用例全过,ctest 25/25 全过
- [ ] hmi.exe build 0 错误 0 警告

---

## 8. 关联文档

- `docs/路线图.md` 第 25-50 行 — 9 阶段总览 + E 战略
- `docs/UI规划调研与方案.md` v1.3 — D+E 并行 + E 8 大功能估时修正
- `docs/进度存档_2026-09-20.md` — D 阶段尾巴 100% 闭环
- `docs/阶段D.1_从这里继续.md` — D 阶段模板参考(8 节结构)
- `docs/阶段D.2_从这里继续.md` — D 阶段交接样板
- `docs/今日教训_2026-09-18.md` 第 5 节 — QtMoc 三件套教训(直接复用)
- `docs/今日教训_2026-09-20.md` — Qt::CoarseTimer / struct vs class 教训(E.0 接入时复用)
- 决策依据:本轮 E 阶段详细计划回答(2026-09-20)+ Wqian 9 个决策确认

---

## 9. 已知坑(下次开 E.1 注意)

1. **QGraphicsItem::itemChange 重入** — 选中/移动时 setPos → 触发 itemChange → 死循环。**修法**:加 flag 标记 + 守卫
2. **Qt6 Q_PROPERTY + 重置类型 — 严重**(D.3 踩过):forward decl 必须 struct/class 一致
3. **sol2 LGPL 合规** — 商用化前必须找律师过边界(Wqian 已知,商用化阶段补)
4. **hmi.exe 启动变慢** — 编辑器 lazy init,操作员模式不创建 QGraphicsView 子对象
5. **WYSIWYG 撤销/重做与多选 bug 多** — QUndoCommand 子类的 merge/restore 太复杂;E.4c 留 2 周反复调试

---

**E.0 收工,下次开工 → E.1 第 1 个控件 LabelControl(5 步路线图见节 7)**