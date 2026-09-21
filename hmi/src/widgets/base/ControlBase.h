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
#include <QSizeF>
#include <QString>
#include <QVariant>

class Variable;
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

public:
    explicit ControlBase(QGraphicsItem* parent = nullptr);
    ~ControlBase() override;

    // ===== 属性 getter/setter =====
    QPointF position() const { return pos(); }
    void    setPosition(const QPointF& p);

    QSizeF  size() const { return size_; }
    void    setSize(const QSizeF& s);

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
    Variable*    bound_ = nullptr;          // 当前绑定的 Variable
    VariableManager* varMgr_ = nullptr;     // 全局 vm(查变量用)

public:
    // ★ 静态注册表(E.4 编辑器拖拽用)— 每个派生类构造时 register 一次
    //   类型字符串 ↔ 工厂函数;后续 E.4.1 调色板拖入用
    using FactoryFn = ControlBase* (*)();
    static void registerControlType(const QString& typeName, FactoryFn factory);
    static ControlBase* create(const QString& typeName);
};

} // namespace qwhmi::widgets