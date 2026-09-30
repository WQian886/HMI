# bug 现场图 · E.4d 几何交互回归基准

> 建档:2026-09-29(E 阶段收尾) ｜ 全部对应 **E.4d 链路已修**的 bug,`ctest` 55/55 全绿
> 用途:这几类是**静默退化高发区** —— 几何对不对用户一眼能看出来,但没有测试会红。
> 留着原始现场,是为了将来 E.4d 那类代码被动过时,能直接翻出"当初长什么样"。
>
> ✅ **2026-09-30:B 组 4 张(图2/图3/图6/图7)已复现 + 已修,根因见下文第一节之二。**
> 之前挂了两个多月"疑似遗留未修",实际是一个根因:`PropertySheet` 拿整个
> `QPointF`/`QSizeF` 比同值 → 多选时位置框永远置灰。`ctest` 现 **57/57 全绿**。

## ⚠ 读图前必知

图里的**红框和红箭头是 Wqian 手绘标注**,不是程序渲染的。
程序实际画的是:蓝色虚线框(`SelectionFrameItem`)、蓝色小方点(`ResizeHandleItem`)、红框内的控件本身。
判断"哪里坏了"时别把手绘标注当成渲染输出。

---

## 一、索引表

### A 组:有 commit 明确点名的(权威)

| 图 | 现象 | 根因 | 修复 commit | 回归测试 |
|---|---|---|---|---|
| **图9** | 多选后 8 个蓝色 handle 全部跑到画布左上角 (0,0),离实际控件很远 | `GroupHandleItem::setTargets` 只 `setRect(unionBoundingRect)` 没动 `group->pos()`,默认仍是 (0,0)。8 个子 handle 放在本地坐标,scenePos = (0,0)+local ≈ 原点 | `3163532` (E.4d-follow-3) | `groupHandle_handlesAtCorrectScenePosition` |
| **图10 / 图11** | 多选整体拖动后,选中框(红框)不跟着走,留在原地 | follow-3 之前 `installSelectionFrame` / `updateSelectionFrame` / `removeSelectionFrame` **三个都是空 stub**(Qt 6.8 reentrancy 标注为 TODO)。frame 的 rect 创建时定死一次 | `8a05de4` (E.4d-follow-5) | `multiMove_selectionFrameFollows`、`multiResize_selectionFrameFollows` |
| **12.png / 12 (2).png** | 修完 follow-5 仍然不跟随 —— 真实拖动路径上信号压根没发 | Qt `ItemIsMovable` 拖动**内部直接调 `QGraphicsItem::setPos()`**,绕开我们的 `setPosition` wrapper;`setPos` 不 emit `positionChanged` | `4735de0` (E.4d-follow-6) | `multiMove_setPosBypassesWrapper_frameFollows` |
| **图1** | 选中框看起来"没有虚线" | ⚠ **归因错误,见下**。原记"paint() 缺蓝色虚线",实测 pen 在**构造函数**里就配好了(蓝色 #0078D7 / DashLine / cosmetic / z=200) | `efb0f56`(测试,生产代码零改动) | `tst_SelectionFrameRender` 7 用例 |

### B 组:commit 未点名,按图面现象归类(**2026-09-30 已复现并修复**)

这几张的共同特征:**属性面板显示"已选 2 个控件"**,
`pos` / `position` / `x` / `y` 全部显示 `0.00` 并带"(异值)"标注,`transformOriginPoint` 也是 0。
画布上 handle 点与控件本体**明显错位**(handle 落在控件外的空白处)。

| 图 | 现象 | 根因 | 状态 |
|---|---|---|---|
| **图2** | Group 选中时,红框圈住的是 Group 外接矩形,但内部子控件比红框小很多;属性面板 `width`/`height` 均为 `0.00` | **同上(size 分支)** | ✅ 已修 |
| **图3** | Trend 选中,红框明显大于 Trend 控件实际边界 | 同上(position 分支) | ✅ 已修 |
| **图6** | 单选一个 Button,红框圈住一大片区域,远大于按钮本身;`visible` 复选框是灰的 | 同上 | ✅ 已修 |
| **图7** | 多选 2 个 Button,**没有 handle 点**,红框圈住属性面板和画布两处 | 同上 | ✅ 已修 |

**根因(一个,不是四个)**:`PropertySheet::rebuildForm` 判断"多选是否同值"时,
拿的是**整个属性值**:

```cpp
if (other->property(prop.name()) != firstVal) { common = false; }
```

`position` 是 `QPointF`、`size` 是 `QSizeF` —— 比的是**整个点 / 整个矩形**。
两个控件只要有**任一分量**不同,整个复合编辑器就被 `setEnabled(false)` 置灰。

**为什么这在多选时是致命的**:多选时 `position` 几乎**必然**不同值
(不然就不叫多选了)。于是属性面板的位置框**永远灰着、永远看不了** ——
用户选了 3 个控件想看它们各自在哪,面板只给一个灰掉的 `0.00`。

而 `0.00` 这个数字恰恰是**症状的放大器**:异值时 editor 被置空,
`QDoubleSpinBox` 读回来就是默认值 0。用户看到 "x = 0.00" 会以为
**控件真的跑到原点去了**,于是怀疑框算错、handle 错位 —— 图上那些"红框和控件错位"的
观感,有一部分就是这么来的。**框没算错,是面板在装死,还装得像报了个坐标。**

**修复**:改成**按分量分别判同值**。`x` 相同就照常显示可编辑,只有真正不同的那一格才灰。
顺带修了一个连带的坑:异值那一格被置空成 0,如果用户改另一格,
`onXChanged` 会把这个 **0 当真值**发出去,把所有控件的 `y` 一起刷成 0 ——
**改 x 把 y 全弄没了**。现在用 NaN 标记"该分量别动",`applyEditorChange` 侧跳过。

回归测试:`tst_PropertySheet` 3 条
(`multiSelect_positionNotAllZero` / `multiSelect_positionPartialCommon` / `multiSelect_sizePartialCommon`)

> **教训(第三次了)**:`efb0f56` 记的根因是错的,`3f63ddf` 计划书列的 3 处有 2 处不用改,
> 这次 B 组 4 张被记成"疑似遗留"躺了两个多月没人确认。
> **静默失败 + 归因不明 = 永远排不上优先级。**
> 建议:B 组这种"没人确认是否还复现"的条目,**要么当天花 10 分钟跑一遍确认**,
> 要么就别留在待办里占位 —— 挂着只会让人以为还有 4 个未知问题悬着。

---

## 一之二、B 组之外的补充(`tst_PropertySheet` 修复时顺带查的)

| 现象 | 判断 | 状态 |
|---|---|---|
| 图7 记的"多选没有 handle 点" | **不是 bug** —— `EditorMainWindow.cpp:504` 明确写了
  多选时 `target_ = nullptr`,单个 handle 走 GroupHandleItem(8 个组 handle)那条路。
  图上没有点是因为当时 Group handle 还没接上,现已由 `tst_GroupHandle` 覆盖 | ✅ 非问题 |

---

## 二、为什么 E.4d 这批 bug 特别难防

三条同源的原因,写在这里给未来的自己:

1. **静默失败**。几何算错不会崩,不会报警,只会"看起来怪"。
   `efb0f56` 那次更极端:文档记的根因是错的,照着改等于重写一个正确的实现。
2. **计划书会骗人**。E.4d-follow-10 的计划书列了 3 处必改点,实测 2 处本来就不用改
   —— 详见 `docs/今日教训_2026-09-29.md` 第 6 条。
3. **断言写错了测试还是绿的**。`tst_Rotation::handles_doNotFollowRotation_currently`
   一开始是红的,细看是**断言本身写错了**(旋转重新定义了"哪个角在左上")。

> 这三条正好对上 `docs/今日教训_2026-09-29.md` 的第 1、3、6 条。
> **同一类问题在一天内出了三次**,不是巧合 —— 是这个代码区域的固有属性。

---

## 三、怎么用这份图

| 场景 | 做法 |
|---|---|
| 改 `GroupHandleItem` / `ResizeHandleItem` / `SelectionFrameItem` 前 | 先翻 A 组对应的图,确认现在的行为没变 |
| 写新测试找不到"什么算对" | 把图渲成 QImage 存基线,像素级比对(`tst_SelectionFrameRender` 是范例) |
| ~~复现 B 组遗留~~ | ✅ **2026-09-30 已完成**,见第一节之二。多选 2 个 x 相同 y 不同的控件 → x 应可编辑、y 应置灰 |

---

## 四、相关文档

- `docs/今日教训_2026-09-29.md` —— 静默失败 / 归因错误 / 照着计划书改代码
- `docs/阶段E.0_从这里继续.md` 节 11 —— 旋转收尾对账表
- `docs/路线图.md` —— E 阶段状态
- `docs/改善清单.md` —— 剩余待修项
