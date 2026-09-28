// ============================================================================
// 阶段 E · E.1c · ValveControl(操作类 — 阀门)
//
// 文件职责:标准阀门符号(两个对顶三角) + 开关状态色。点击切换开/关,
//   通过 ControlBase::writeToPlc 真正写回 PLC。
//
// 【符号为什么是两个对顶三角】GB/T 标准的阀门符号就是「>◁」,
//   阀体两片挡板。画成开关/按钮的样子会让电工认不出这是阀门,
//   而操作员分不清阀门和按钮是要出事的 —— 阀门背后是高压管路。
//
// 【开/关判定用哪个信号】isOpen() 只看 value,【不】看 value 是否 dirty。
//   画面颜色必须跟 PLC 的真值一致:写下去还没收到 PLC 回读期间,
//   阀门已经变了(操作员点了就是点了),但如果 PLC 最终拒绝,
//   onVariableValueChanged 会把值刷回来,颜色也跟着回退 ——
//   这是「写回后被拒」的唯一视觉出口,不能省。
//
// 关联:ControlBase.h, ValveControl.cpp
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"
#include <QColor>

namespace qwhmi::widgets {

class ValveControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(QColor openColor READ openColor WRITE setOpenColor NOTIFY openColorChanged)
    Q_PROPERTY(QColor closedColor READ closedColor WRITE setClosedColor NOTIFY closedColorChanged)

public:
    explicit ValveControl(QGraphicsItem* parent = nullptr);
    ~ValveControl() override;

    QColor openColor() const { return openColor_; }
    void   setOpenColor(const QColor& c);
    QColor closedColor() const { return closedColor_; }
    void   setClosedColor(const QColor& c);

    // ★ 开 = value >= 0.5。绑 Bool 变量时 writeToPlc 会把 double 适配成
    //   bool(TankControl 那次加的 coerceToDataType),这里读到的是 QVariant(bool)
    //   —— QVariant::toDouble() 对 bool 返回 1/0,所以两种都能判
    bool isOpen() const;

    QRectF boundingRect() const override;
    void   paint(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

    // ★ 写被拒时(只读变量 / 无权限 / 值超范围)emit 一次,给上层弹提示用。
    //   没有这个信号,操作员点了没反应也收不到任何反馈,只能猜
    void   mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

signals:
    void openColorChanged();
    void closedColorChanged();
    // ★ value 变化时发,不是"点击"时发 —— PLC 远程改了值也会发,
    //   让上层能把"这条阀门被远程关了"记进画面日志
    void openedChanged();
    // ★ 写回被拒。detail 是给用户看的中文原因
    void writeRejected(const QString& detail);

private:
    QColor openColor_   = QColor(40, 170, 90);    // 开:绿(通行)
    QColor closedColor_ = QColor(190, 60, 60);    // 关:红(阻断)
    bool   lastOpen_    = false;                 // 上次的开合态,用来判断要不要 emit
};

} // namespace qwhmi::widgets
