// ============================================================================
// 阶段 E · E.1 · ControlBase 实现
// 对应 ControlBase.h
// ============================================================================

#include "widgets/base/ControlBase.h"
#include "../../Variable.h"
#include "../../VariableManager.h"

#include <QHash>
#include <QJsonObject>

namespace qwhmi::widgets {

// ★ 静态工厂注册表 — E.4 调色板拖入时按类型字符串 new
namespace {
QHash<QString, ControlBase::FactoryFn>& registry() {
    static QHash<QString, ControlBase::FactoryFn> r;
    return r;
}
} // namespace

void ControlBase::registerControlType(const QString& typeName, FactoryFn factory) {
    registry()[typeName] = factory;
}

ControlBase* ControlBase::create(const QString& typeName) {
    auto it = registry().find(typeName);
    if (it == registry().end()) return nullptr;
    return it.value()();
}

ControlBase::ControlBase(QGraphicsItem* parent)
    : QGraphicsObject(parent)
{
    // ★ 默认 flags — 可选中 + 可移动 + 位置变化发信号
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
}

ControlBase::~ControlBase() {
    unbind();
}

void ControlBase::setPosition(const QPointF& p) {
    if (p == pos()) return;
    setPos(p);
    emit positionChanged();
}

void ControlBase::setSize(const QSizeF& s) {
    if (s == size_) return;
    size_ = s;
    update();          // 触发重绘
    emit sizeChanged();
}

void ControlBase::setVariableName(const QString& name) {
    if (name == variableName_) return;
    variableName_ = name;
    emit variableChanged();
    // ★ 自动尝试绑定(从 hmi 顶层 VariableManager 拿)
    //   这里不直接 hold VM 指针,通过 unbind 旧 + 提示调用方重新 bind
    //   实际绑定由 EditorMainWindow 或 hmi.cpp 显式调 bind(Variable*)
}

void ControlBase::setValue(const QVariant& v) {
    if (v == value_) return;
    value_ = v;
    update();          // 触发重绘
    emit valueChanged();
}

QString ControlBase::displayText() const {
    return value_.toString();
}

// ===== E.3 JSON 存读基类默认实现 =====
QJsonObject ControlBase::toJson() const {
    QJsonObject o;
    o["type"] = metaObject()->className();
    o["x"] = position().x();
    o["y"] = position().y();
    o["w"] = size_.width();
    o["h"] = size_.height();
    o["variable"] = variableName_;
    return o;
}

void ControlBase::fromJson(const QJsonObject& obj) {
    setPosition(QPointF(obj.value("x").toDouble(), obj.value("y").toDouble()));
    setSize(QSizeF(obj.value("w").toDouble(120), obj.value("h").toDouble(40)));
    setVariableName(obj.value("variable").toString());
}

// ===== Variable 绑定 =====
void ControlBase::bind(Variable* v) {
    if (bound_) unbind();
    bound_ = v;
    if (!v) return;
    setVariableName(v->plcId() + "." + v->name());
    // ★ 监听 valueChanged → 更新本控件 value
    connect(v, &Variable::valueChanged, this, &ControlBase::onVariableValueChanged);
    // ★ 立即同步当前值
    setValue(v->value());
}

void ControlBase::unbind() {
    if (!bound_) return;
    disconnect(bound_, &Variable::valueChanged, this, &ControlBase::onVariableValueChanged);
    bound_ = nullptr;
}

void ControlBase::onVariableValueChanged() {
    if (!bound_) return;
    setValue(bound_->value());
}

} // namespace qwhmi::widgets