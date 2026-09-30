# 阶段 E · E.0 · 调研 + 决策锁定(从这里继续)

> 适用:**E 阶段计划 + 9 个决策锁定 + 业余节奏 22-27 周拆分**,开 E.1 之前先看完
> 维护人:Wqian + AI 助手  ｜  创建:2026-09-20
> 配套:`docs/路线图.md` 第 25-50 行(9 阶段总览+E 战略) / `docs/UI规划调研与方案.md` v1.3 / `docs/进度存档_2026-09-20.md` / `docs/阶段D.2_从这里继续.md` 第 6 节(D.4 节) / `docs/阶段D.1_从这里继续.md`
> ✅ **状态(2026-09-30 基于代码核对)**:🟢 **E 阶段全部完工** — E.0(本文件)决策锁定 ✅ + E.1 控件库 **22 个全落地**(`src/widgets/*`,E.1c 的储罐/管道/阀门/泵/电机 2026-09-28 补齐)+ E.1c 写回 PLC 通道 `ControlBase::writeToPlc` ✅ + E.2 PropertySheet 反射 + E.4a 调色板/拖拽 + E.4b 8 handle 缩放 + E.4c QSizeF/QPointF 复合编辑器 + E.4d 多选/对齐/等尺寸 + E.4d-follow-1~8 收尾链 + E.4d-follow-9 选中框渲染测试 + E.4d-follow-10 旋转(见节 10、11)+ **运行时画布 `1946fc7`** + **复制/粘贴/删除 `35fc787`**。
> **测试基线:`ctest` 57 个目标 56 通过**(1 个既有失败:`tst_ProjectSchema` 硬编码 `tests/build/` 路径,
> 而构建目录是 `build_ninja/`;已 stash 到干净 HEAD 复跑确认与新改动无关)。
> **推 v0.3.0 的两项**:主题(亮/暗)、Lua(sol2)—— Wqian 已拍板,不在 0.2.0 范围。
> ⚠ **编号提醒**:本文件计划表里 **E.5 = 主题**,但 `1946fc7` 的 commit message
> 把「运行时画布」也写成 E.5 —— **撞车了**。本文件以下一律用全称。
> **下一阶段:F(商业级报警)**。完整阶段状态见 `docs/现状.md`(唯一权威)/ `hmi/CHANGELOG.md`。

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

| 阶段 | 新增测试 target | 关键用例 | 实际 |
|---|---|---|---|
| E.0(本轮) | 0 | — | ✅ |
| E.1a(显示 + 元件 10) | 10 | 每控件 构造/paint/property/JSON 往返 | ✅ |
| E.1b(控制 + 数据 7) | 7 | ButtonControl clicked / SliderControl value 同步 / TrendControl reuse | ✅ |
| E.1c(图形工业 5) | 5 | 5 控件 paint 不崩 + 边界 + **点击写回 PLC** | ✅ 2026-09-28(每控件 24-25 用例,超出原计划 +13 的估算) |
| E.1c 补充 | 3 | `tst_ControlBaseWrite` / `tst_ControlBaseColorJson` / `tst_SpinAnimator` | ✅ |
| E.2(绑定+属性) | 1 | 枚举反射/数值读写/多选共同属性 | ✅ |
| E.3(JSON 存读) | (并入各控件测试的 roundTrip) | save/load/migrate | ✅ |
| E.4a(画布+调色板+拖拽) | 1 | 拖入/拖出/位置正确/调色板项数 | ✅ |
| E.4b(选中/移动/缩放) | 3 | 8 handle 响应 + Shift 等比 + 视觉对齐 | ✅ |
| E.4c(属性+Undo+JSON) | (并入 E.2) | QUndoStack 撤销/重做 | ✅ |
| E.4d(多选+对齐+等尺寸) | 1 | **9 个动作的 Undo 完整性/合并/空步骤** | ✅ 2026-09-29 |
| E.4d 收尾链 | 3 | 回归(HandleVisualAlignment / EditorMainWindow / GraphicsObjectProperty) | ✅ |
| E.5(主题) | — | 加载/应用/切换主题 | ⬜ **推 v0.3.0** |
| E.6(Lua+8 类) | — | 8 类各 1 用例 + 沙箱 | ⬜ **推 v0.3.0** |

> ★ **口径变了**:原表按"用例数"累加(目标 130)。实际项目是**每个控件一个 test target**,
> 一个 target 里装几十个用例 —— 现在是 **53 个 target / 800+ 用例**。按 target 记更贴近
> `ctest` 的实际输出(53 tests passed)。E.5/E.6 推 v0.3.0 后,E 阶段在 target 口径下不再增长。

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

> 状态列 2026-09-29 更新。实际推进比原计划快(E.1a-E.4d 全部在 2026-09-20~28 三周内做完),
> 但 E.1c 补做时发现两个计划外的前置:① `ControlBase` 当时是**单向**的(控件点不动 PLC),
> ② 泵/电机需要旋转动画。E.5/E.6 已拍板推 v0.3.0。

| 周 | 工作 | 状态 |
|---|---|---|
| **周 1** | E.0 调研 + 文档(本文件) | ✅ 2026-09-20 |
| **周 2** | E.1a-1:ControlBase.h 基类 + LabelControl + NumberDisplay + TextDisplay | ✅ 09-20 |
| **周 3** | E.1a-2:BarControl + GaugeControl + LedControl(显示 6 完) | ✅ 09-20 |
| **周 4** | E.1a-3:ImageControl + RectangleControl + LineControl + GroupControl(元件 4 完) | ✅ 09-20 |
| **周 5** | E.1b-1:ButtonControl + ToggleControl | ✅ 09-20 |
| **周 6** | E.1b-2:SliderControl + InputEditControl | ✅ 09-20 |
| **周 7** | E.1b-3:TrendControl(复用 D.2)+ TableControl + AlarmListControl(数据 3 完) | ✅ 09-20 |
| **周 8** | E.1c:**写回 PLC 通道** + TankControl | ✅ 09-28(计划外前置) |
| **周 9** | E.1c:PipeControl + ValveControl + PumpControl + MotorControl(图形工业 5 完) | ✅ 09-28 |
| **周 10** | E.2:绑定 + 属性面板(Q_PROPERTY 反射) | ✅ 09-21 |
| **周 11** | E.3:JSON 存读(schema_version 3 + load/save + migrate) | ✅ 09-21 |
| **周 12-13** | E.4a:画布 + 调色板 + 拖拽 | ✅ 09-22 |
| **周 14-15** | E.4b:选中/移动/缩放(8 handle + Shift 等比) | ✅ 09-22 |
| **周 16-17** | E.4c:属性面板 + Undo/Redo + JSON | ✅ 09-22 |
| **周 18-19** | E.4d:多选 + 对齐 + 等尺寸 + Undo 补齐(follow-1~8) | ✅ 09-22~09-29 |
| **周 20** | E.5:主题(亮/暗 2 套) | ⬜ **推 v0.3.0** |
| **周 21-22** | E.6:Lua 脚本引擎 + 8 类预定义动作 | ⬜ **推 v0.3.0** |
| — | E 阶段尾巴:SelectionFrameItem 视觉 + `ControlBase::rotation` | 🔴 见节 10 |

**E 阶段主体(除两条尾巴)已完工。**原估 22-27 周,实际 ~7 天集中推进。

---

## 7. ~~下一步:E 阶段剩下的 2 条尾巴~~ → 已全部完工

> 原节 7 是"E.1 第 1 个控件 LabelControl 的 5 步路线图" —— 2026-09-20 就做完了,
> 留在这里只会误导下次接手的 AI(它会照着再做一个 LabelControl)。**现改为完工记录**,
> 重点不是"做了什么",而是"计划书说的和实测出来的差多少" —— 见节 11。

### 尾巴 A:`SelectionFrameItem::paint()` 视觉(对应 `bug图/图1.png`)—— ✅ `efb0f56`

**计划书说**:`paint()` 缺蓝色虚线,要补外圈描边(2px、`DashLine`、Z 值 200)。
**实测**:`paint()` 的笔刷在构造时就配好了,**能正常画,也扛得住整场景渲染**。
新增 `tst_SelectionFrameRender`(7 个用例,渲到 `QImage` 数像素)证明这一点。
**结果:生产代码一行没改。**

### 尾巴 B:`ControlBase::rotation` + handle 跟随 —— ✅ `3f63ddf`

**计划书说**"必须同时改 3 处",还标了风险等级,还要求"阻力大就拆"。

**实测:3 处里有 2 处根本不用改。**逐条列在节 11,这里只留结论 ——
真坏的是 **handle 光标** 和 **选中框** 两处,已修;其余三处(Qt 已有的 rotation 属性、
Qt 自动做的父子 transform、group 等比缩放)经测试确认本来就是对的。

---

## 8. 关联文档

- **`docs/现状.md`** — "项目现在什么样"的**唯一权威**,跟本文件冲突以它为准
- `docs/路线图.md` 第 25-50 行 — 9 阶段总览 + E 战略
- `docs/UI规划调研与方案.md` v1.3 — D+E 并行 + E 8 大功能估时修正
- `docs/进度存档_2026-09-20.md` — D 阶段尾巴 100% 闭环
- `docs/阶段D.1_从这里继续.md` — D 阶段模板参考(8 节结构)
- `docs/阶段D.2_从这里继续.md` — D 阶段交接样板
- `docs/今日教训_2026-09-18.md` 第 5 节 — QtMoc 三件套教训(直接复用)
- `docs/今日教训_2026-09-20.md` — Qt::CoarseTimer / struct vs class 教训
- 决策依据:本轮 E 阶段详细计划回答(2026-09-20)+ Wqian 9 个决策确认

---

## 9. 已知坑(全部实战验证过,非推测)

1. **QGraphicsItem::itemChange 重入** — 选中/移动时 setPos → 触发 itemChange → 死循环。**修法**:加 flag 标记 + 守卫。
   ★ 追加(E.4d-follow-6):`setPos()` 走 `itemChange` 但 `setPosition()` 走自己的 setter,**两条路径**。
   只在 `itemChange` 里补 `positionChanged` 会漏掉 `setPosition` 那条 —— 属性面板不刷新。
2. **Qt6 Q_PROPERTY + 前置声明类型 — 严重**(D.3 踩过):forward decl 必须 struct/class 一致
3. **sol2 LGPL 合规** — 商用化前必须找律师过边界(Wqian 已知)
4. **hmi.exe 启动变慢** — 编辑器 lazy init,操作员模式不创建 QGraphicsView 子对象
5. **★ 控件工厂注册靠构造函数,不靠静态初始化**(E.1c 踩过,静默失败):
   `ControlBase.cpp` 的 `QHash<QString, FactoryFn>` 由每个控件**构造函数**里 `registerControlType` 填充。
   `EditorMainWindow.cpp` 开头有一串 `delete new XxxControl;` 就是为了**逼所有控件的构造函数跑一遍**
   把注册表填满。加新控件漏了那行 → ① 调色板上不出现 ② 工程文件里存着它但 `loadFromFile` 静默跳过。
   **两个症状都是"看起来坏了但说不清哪坏"**。少一个控件的判断:调色板项数 < 22(`tst_EditorMainWindow` 有断言)。
6. **★ 测试用错 API 编译不过,但用错 JSON 键名会静默通过**(E.1c 踩过,坑了 10 个测试文件):
   `propertiesFromJson` 按 `Q_PROPERTY` 名反查,`obj.contains(name)` 不中就**跳过**。
   键名写错 → 整个对象被静默忽略 → 控件保持默认值 → 断言"值不等",错误信息完全看不出是键名没匹配上。
   **写完 roundTrip 测试一定要 `toJson` 完再 `fromJson` 回来比一遍**,
   只验 `toJson` 输出的测试,两边一起错也照样绿。
7. **★ `QAbstractAnimation::currentLoop()` 是 public,`currentLoopChange()` 是 signal**(SpinAnimator 踩过):
   自己累加 `delta` 的写法只在"每帧都来一次"时成立,卡一帧就丢掉整整一圈,表现为泵转速随卡顿忽快忽慢。

---

## 10. 2026-09-28 ~ 09-29 补记(Path 2 降级 E)

E.4d-follow-8 之后,按 Wqian 拍板的 **Path 2(降级 E)** 又做了 3 件事:

| # | 做什么 | 结果 |
|---|---|---|
| A | 修 10 个过时测试的 JSON 键名(坑 6) | **ctest 首次 53/53 全绿** |
| B | 对齐/等尺寸 9 个动作补 Undo(`GeometryChangeCommand`) | 21 个新用例,Undo 覆盖 0 → 全覆盖 |
| C | `ControlBase::rotation` + handle 跟随 | ✅ `3f63ddf`,见节 7 尾巴 B / 节 11 |

**A 的归因更正**:那 10 个失败此前被记成"生产代码 `toJson`/`fromJson` 的键名不匹配(w/h vs width/height)"。
**这是错的。**生产代码 `toJson`/`fromJson` 是**对称**的(反射输出 + 反射读回),
问题在**测试写的是平铺的 `x`/`y`/`w`/`h`,而 `Q_PROPERTY` 名是 `position`/`size` 嵌套对象** ——
测试在断言一个**从未存在过的 JSON 格式**。所以只改了测试,生产代码一个字没动。

**B 的两个刻意的设计**(改这两个地方前先读 `src/editor/GeometryChangeCommand.h` 的头注释):
- 多选场景下"一次 Ctrl+Z = 一步操作"是硬要求。**退一半的排版比不撤销更让人困惑**。
  所以记"一批控件的矩形"而不是 N 条单属性命令。
- 几何没变就**不 push**。否则用户按 Ctrl+Z 会发现画面一动不动但栈里还有内容 ——
  那种"按了没反应"比不能撤销更招人烦。

---

## 11. 两条尾巴收口(2026-09-29 晚)—— 计划书 vs 实测

这两条尾巴在开工前都写了详细的改动计划。**做完回头对账,发现计划书里 5 条改动建议
有 3 条是多余的**。过程本身比结果更值得记,因为这个偏差在本项目不是第一次。

| 计划书说 | 实测 | 差别来源 |
|---|---|---|
| 尾巴 A:`paint()` 缺蓝色虚线,要补描边 | 笔刷构造时就配好了,**能画,7/7 渲染测试通过** | 只读了 `SelectionFrameItem.h` 的声明,没读 `.cpp` 实现 |
| B-1:给 `ControlBase` 加 `Q_PROPERTY(qreal rotation)` | **不用加** —— `QGraphicsItem` 自己就有(`qgraphicsitem.h:497`),不是 FINAL,反射已经能列出来,JSON 已经存读 | 没查基类头文件。照做会变成"子类覆盖父类同名属性",moc 允许,但凭空多一层间接,底下那层还在跑 |
| B-2:`layoutHandlesFor` 要让 8 个 handle 位置跟着转 | **不用改** —— handle 是 target 的**子 item**,父一转它们跟着转(Qt 的父子 transform) | 没意识到 handle 的父子关系。加一套旋转换算会跟 Qt 已做好的事重复,还多一个能算错的地方 |
| B-3:`SelectionFrameItem` 可能不用改但要实测确认 | **实测确认要改** —— `sceneBoundingRect()` 会变,但 frame 没接 `rotationChanged`,不刷就是旧尺寸 | ✅ 计划书这条判断对了 |
| B-4:`applyProportionalResize` 含旋转控件时语义不对 | 实测输出仍是有效值(非负、非 0、不跑飞)。**严格讲语义确实不对**(缩放一个已旋转的控件该同步旋转来保持形状),那是更大的问题,留 v0.3.0 | 判断偏保守,不算错 |

**真正修的只有 2 处**:
1. `ResizeHandleItem::applyCursorForRotation()` —— 原来在左边的点转 90° 后视觉上到了上边,
   光标还是 `SizeHor`。按 target 当前 rotation 查 3 张 8 项表换光标;
   **非 90 整数倍不动** —— 任意角度下"这个点算左边还是上边"没有唯一答案,猜错比不改更糟
   (会出现"箭头朝左但只能上下拖")。
2. `EditorMainWindow::onRotationChanged()` —— 接 `rotationChanged` 后同刷选中框和 handle 光标。
   **光标刷新必须走信号**,不能塞在 `installSelectionFrame()` 里 —— 后者只在选中变化时跑,
   属性面板改 rotation 根本不会触发它。

### 踩到的坑(第 8 条已知坑)

8. **★ Q_PROPERTY 的 NOTIFY 信号可以不住在声明它的类上**(`3f63ddf` 踩过,`error C2039`):
   `QGraphicsItem` 的 `Q_PROPERTY(qreal rotation ... NOTIFY rotationChanged)` 看着像能用
   `&QGraphicsItem::rotationChanged` 接,编译直接报"不是 QGraphicsItem 的成员" ——
   那个 `void rotationChanged();` 实际声明在 **`QGraphicsObject`** 里(`qgraphicsitem.h:530`)。
   **信号要有 moc 才存在,得有 QObject 基类**。属性写在基类、信号住在派生类,这个错位很常见。
   正确写法:`connect(t, &ControlBase::rotationChanged, ...)`(经派生类取, moc 认)。

9. **★ 别从 `scene()->items()` 里扫自己刚建的 item**(`tst_Rotation` 0xC0000005):
   扫出来的第一个匹配项可能是别的测试留下的野指针,崩在 `QAtomicOps` 里,栈上看不出是谁。
   **改用窗口自己的 accessor**(`EditorMainWindow::groupHandle()`)。项目里已有这个惯例。

### 一条元教训

"先读基类头文件 + 先写诊断测试,再改生产代码"这两件事,这次各自省下了一整块返工。
B-1(rotation 属性)和 B-2(handle 跟随)都是**读一眼头文件就能否掉的**,却各自被写成了
"必须做的改动",还附了风险等级。写计划书时容易把"我以为要改的"当成"要改的",
**风险估得越细,越容易让人不去验证** —— 表格填满了,人就倾向于相信表格。

---

## 12. E 阶段完工 ✅(2026-09-29)

**产出**:22 控件 + 写回 PLC 通道 + WYSIWYG 编辑器核心(调色板/拖拽/8 handle/多选/对齐/等尺寸)
+ Undo **全覆盖** + 旋转 + 选中框。**`ctest` 55/55 全绿**,`hmi.exe` 构建通过。

**E.4d 修过的东西没有被旋转重新打开** —— 这是当初把旋转单独立项时最担心的风险,
实测没有发生。`tst_HandleVisualAlignment` / `tst_ResizeHandle` / `tst_EditorMainWindow` 全绿。

**推 v0.3.0 的两项**(Wqian 已拍板):
- **E.5 主题(亮/暗)** —— 见节 3.5
- **E.6 Lua(sol2)** —— 见节 3.4。**注意坑 3:sol2 的 LGPL 合规,商用前要找律师过边界**

**v0.3.0 里 E 阶段的欠账**:
- 等比缩放一个已旋转的控件时同步旋转(保持形状)—— 语义问题,本阶段只保证"不出离谱值"
- 旋转目前只按 90° 的整数倍换光标;45° 一档要补表(算法不用改,`applyCursorForRotation` 里加表即可)
- `SelectionFrameItem` 仍不区分单选/多选的样式(共用一个)

**下一阶段 → F(商业级报警)。**开工前先看 `docs/现状.md`。