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

// ★ E.3:用 className 反查注册表短名 — 通过 typeid 比对
//   实例化临时控件只为比对类型,代价值得(只在 save 时调一次)
QString ControlBase::registryKeyFor(const QMetaObject* meta) {
    if (!meta) return {};
    const QString target = QString::fromUtf8(meta->className());
    for (auto it = registry().begin(); it != registry().end(); ++it) {
        ControlBase* probe = it.value()();
        if (QString::fromUtf8(probe->metaObject()->className()) == target) {
            delete probe;
            return it.key();
        }
        delete probe;
    }
    return target;   // 找不到时回退到 className
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
    // ★ E.2 用反射统一所有 Q_PROPERTY,派生类追加自定义字段
    QJsonObject o = propertiesToJson();
    o["type"] = metaObject()->className();   // 单独保留,便于 E.4 调色板按 type 字符串 new
    return o;
}

void ControlBase::fromJson(const QJsonObject& obj) {
    // ★ 反射写回:setProperty(name, value) 走 Q_PROPERTY 反射
    propertiesFromJson(obj);
}

QJsonObject ControlBase::propertiesToJson() const {
    QJsonObject o;
    const QMetaObject* meta = metaObject();
    for (int i = 0; i < meta->propertyCount(); ++i) {
        QMetaProperty prop = meta->property(i);
        if (!prop.isReadable()) continue;
        const QString name = QString::fromUtf8(prop.name());
        // ★ E.3 已知坑修复:Qt 6.5 的 QJsonValue::fromVariant 对 QPointF/QSizeF
        //   支持不完整(可能返回 null)— 手动构造 JSON 对象保证一致性
        //   (跟 propertiesFromJson 互逆)
        const int type = prop.userType();
        QJsonValue value;
        if (type == QMetaType::QPointF) {
            QPointF p = prop.read(this).toPointF();
            value = QJsonObject{{"x", p.x()}, {"y", p.y()}};
        } else if (type == QMetaType::QSizeF) {
            QSizeF s = prop.read(this).toSizeF();
            value = QJsonObject{{"width", s.width()}, {"height", s.height()}};
        } else {
            value = QJsonValue::fromVariant(prop.read(this));
        }
        o.insert(name, value);
    }
    return o;
}

void ControlBase::propertiesFromJson(const QJsonObject& obj) {
    const QMetaObject* meta = metaObject();
    for (int i = 0; i < meta->propertyCount(); ++i) {
        QMetaProperty prop = meta->property(i);
        if (!prop.isWritable()) continue;
        const QString name = QString::fromUtf8(prop.name());
        if (!obj.contains(name)) continue;
        // ★ E.3 已知坑修复:Qt 6.5+ QPointF/QSizeF 通过 setProperty 写入需要
        //   QVariant::fromValue 显式构造 — QJsonValue::toVariant 把 JSON object
        //   转成 QVariantMap,不是 QPointF。手动检测类型并重新包装。
        QVariant value = obj.value(name).toVariant();
        const int type = prop.userType();
        if (type == QMetaType::QPointF) {
            QVariantMap m = value.toMap();
            value = QVariant::fromValue(QPointF(m.value("x").toDouble(),
                                              m.value("y").toDouble()));
        } else if (type == QMetaType::QSizeF) {
            QVariantMap m = value.toMap();
            value = QVariant::fromValue(QSizeF(m.value("width").toDouble(),
                                              m.value("height").toDouble()));
        }
        prop.write(this, value);
    }
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