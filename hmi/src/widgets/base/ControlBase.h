// ============================================================================
// 阶段 E · E.1 · 控件基类(QGraphicsObject 派生)
//
// 文件职责:22 个 E.1 控件的共同基类。提供:
//   - 通用 4 个属性:QPointF position / QSizeF size / QString variableName / QVariant value
//   - 通用接口:bind(Variable)/unbind()/toJson()/fromJson()
//   - QGraphicsObject 提供 paint/boundingRect/mousePress 等图形事件
//
// 关键设计:
//   - QGraphicsObject 而不是 QGraphicsItem — 支持 Q_PROPERTY 信号
//   - variableName_ + VariableManager 监听 valueChanged → emit valueChanged
//   - 序列化走 QJsonObject — E.3 JSON 存读复用
//
// 关联:ControlBase.cpp, 22 个派生控件,VariableManager, Project(E.3 JSON)
// ============================================================================

#pragma once

#include <QGraphicsObject>
#include <QJsonObject>
#include <QPointF>
#include <QPointer>
#include <QSizeF>
#include <QString>
#include <QVariant>

// ★ E.1c:bound_ 从裸 Variable* 改成 QPointer<Variable>,QPointer 的 static_cast
//   需要【完整类型】(要调 QObject::destroyed 钩子),所以这里必须 include 而非前向声明。
//   Variable.h 很轻(只依赖 QObject/QString/QVariant/QDateTime),无循环包含。
#include "../../Variable.h"

class VariableManager;
class QPainter;
class QStyleOptionGraphicsItem;
class QWidget;

namespace qwhmi::widgets {

class ControlBase : public QGraphicsObject {
    Q_OBJECT

    // ★ 通用 5 个属性(E.2 属性面板反射自动生成 UI)
    Q_PROPERTY(QPointF position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(QSizeF  size     READ size     WRITE setSize     NOTIFY sizeChanged)
    Q_PROPERTY(QString  variableName READ variableName WRITE setVariableName NOTIFY variableChanged)
    Q_PROPERTY(QVariant value READ value WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(QString  displayText READ displayText NOTIFY valueChanged)  // ★ derived
    // ★ 重定义 QGraphicsObject 内建的 width/height 让它们跟 size_ 同步
    Q_PROPERTY(qreal width  READ width  WRITE setWidth  NOTIFY sizeChanged)
    Q_PROPERTY(qreal height READ height WRITE setHeight NOTIFY sizeChanged)

public:
    explicit ControlBase(QGraphicsItem* parent = nullptr);
    ~ControlBase() override;

    // ===== 属性 getter/setter =====
    QPointF position() const { return pos(); }
    void    setPosition(const QPointF& p);

    QSizeF  size() const { return size_; }
    void    setSize(const QSizeF& s);

    // ★ 覆盖 QGraphicsObject 的 width/height Q_PROPERTY(默认是独立属性,不绑 boundingRect)
    //   PropertySheet 反射时显示 width/height 而不是 ControlBase 的 size(QSizeF 不支持),
    //   必须让 width/height 跟着 size_ 走,否则属性面板永远显示 0
    qreal   width() const { return size_.width(); }
    qreal   height() const { return size_.height(); }
    void    setWidth(qreal w);
    void    setHeight(qreal h);

    QString variableName() const { return variableName_; }
    void    setVariableName(const QString& name);

    QVariant value() const { return value_; }
    void    setValue(const QVariant& v);

    // ★ E.2 属性面板:显示文字(默认是 value 转字符串)
    virtual QString displayText() const;

    // ===== 通用接口 — 派生类必须实现 =====
    QRectF boundingRect() const override = 0;
    void   paint(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override = 0;

    // ★ E.4d 续-6:override itemChange → 在 Qt 内部 setPos 触发的
    //   ItemPositionHasChanged 上 emit positionChanged,这样 ItemIsMovable 拖动
    //   也会触发我们的 positionChanged 信号(走 setPos 路径不经过 setPosition
    //   wrapper,所以光在 setPosition 里 emit 不够)。
    QVariant itemChange(QGraphicsItem::GraphicsItemChange change,
                        const QVariant& value) override;

    // ===== E.3 JSON 存读(基类实现通用部分,派生类可重写)=====
    virtual QJsonObject toJson() const;
    virtual void        fromJson(const QJsonObject& obj);

    // ★ E.2 反射接口(基类实现)— 遍历所有可读 Q_PROPERTY → 输出 JSON
    //   E.3 序列化复用这条路径;派生类 toJson() 应先调这个再追加自定义字段
    QJsonObject propertiesToJson() const;
    // ★ 反向:从 JSON 写回所有同名 Q_PROPERTY(setProperty 走 Q_PROPERTY 反射)
    void        propertiesFromJson(const QJsonObject& obj);

    // ===== Variable 绑定(B.4 Binding 思路的简化版)=====
    void bind(Variable* v);          // 连接 valueChanged → updateDisplay
    void unbind();                    // 解绑

    // ★ E.1c(2026-09-28):UI → PLC 反向写通道
    //   背景:基类原本是单向的。onVariableValueChanged 只做「PLC 推过来 → 更新显示」,
    //         反方向推不出去。ToggleControl 点一下只翻本地 value_ + emit toggled,
    //         而全仓库没有任何人接那个信号 → 17 个控件全是只读显示器,一个都写不回 PLC。
    //         对 E.1c 的阀门/泵/电机是致命的:不能点开的阀门就只是张图片。
    //   改法:统一走 Variable::setValue + markDirty(WriteScheduler 扫 dirty 发出去),
    //         完整对齐 Binding.cpp:146-176 / ActionManager.cpp:81-88 两条既有写路径。
    //   语义:本地 setValue 立即生效(不等 PLC 回读),PLC 回读后 onVariableValueChanged
    //         会再刷一次 —— 这是乐观 UI,工业现场操作员要的就是"点了立刻有反应"。
    //   返回 false = 拒绝写入(未绑定 / 只读变量 / 无权限 / 值被 Variable 拒收),
    //         此时本地 value_ 保持不变(不撒谎),调用方应据此提示用户。
    bool writeToPlc(const QVariant& v);

    // ★ 派生类用的绑定句柄(阀门/泵/电机点击时可能需要看 access)
    Variable* boundVariable() const { return bound_; }
    // ★ 写权限判定(不含绑定检查)—— 供派生类在 mousePress 里做灰化/提示
    bool canWrite() const;

signals:
    void positionChanged();
    void sizeChanged();
    void variableChanged();
    void valueChanged();

private slots:
    // ★ Variable::valueChanged → 控件更新显示
    void onVariableValueChanged();

private:
    QSizeF       size_ = QSizeF(120, 40);   // 默认 120x40
    QString      variableName_;             // "plc1.Speed"
    QVariant     value_;                    // 当前值
    // ★ E.1c:裸 Variable* → QPointer。
    //   之前是裸指针,VariableManager 删掉变量后 bound_ 悬空。基类原来只读
    //   (悬空最多崩在 onVariableValueChanged),现在 writeToPlc 要【解引用它去写 PLC】
    //   —— 悬空就从"崩"升级成"往已释放内存写值",性质更恶劣。
    //   QPointer 在 QObject 析构时自动置 null,所有 if (!bound_) / bound_->x() 路径
    //   不用改就变安全。现状.md 第五节约定 5 说的正是这件事(要求 connect
    //   variableReplaced),QPointer 是更省心的等价手段。
    QPointer<Variable> bound_;            // 当前绑定的 Variable(自动防悬空)
    VariableManager* varMgr_ = nullptr;     // 全局 vm(查变量用)

public:
    // ★ 静态注册表(E.4 编辑器拖拽用)— 每个派生类构造时 register 一次
    //   类型字符串 ↔ 工厂函数;后续 E.4.1 调色板拖入用
    using FactoryFn = ControlBase* (*)();
    static void registerControlType(const QString& typeName, FactoryFn factory);
    static ControlBase* create(const QString& typeName);

    // ★ E.4a:列出所有已注册的类型名(调色板用)— 给 PaletteWidget 拖入候选
    static QStringList registeredTypeNames();

    // ★ E.3:用 className 反查注册表里的短名(如 "BarControl" → "Bar")
    //   给 controlsToJson 存 JSON 的 type 字段用 — 跟 create() 配对
    static QString registryKeyFor(const QMetaObject* meta);
};

} // namespace qwhmi::widgets