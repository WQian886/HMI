// ============================================================================
// 阶段 E · E.1 · ControlBase 实现
// 对应 ControlBase.h
// ============================================================================

#include "widgets/base/ControlBase.h"
#include "../../Variable.h"
#include "../../VariableManager.h"
// ★ E.1c:写回 PLC 需要跟既有写路径(Binding.cpp / ActionManager.cpp)对齐的三件套:
//   权限检查 + 审计留痕。include 根是 src/(见 CMakeLists target_include_directories)
#include "auth/AuditLog.h"
#include "auth/CurrentUser.h"
#include "auth/Permission.h"

#include <QHash>
#include <QJsonObject>
#include <QMetaType>
#include <algorithm>
#include <limits>

namespace qwhmi::widgets {

// ★ 静态工厂注册表 — E.4 调色板拖入时按类型字符串 new
namespace {
QHash<QString, ControlBase::FactoryFn>& registry() {
    static QHash<QString, ControlBase::FactoryFn> r;
    return r;
}

// ★ E.1c(2026-09-28):QColor 存读
//
// 【先纠正两个误判,都是同一个毛病:不实测就照着文档概述写】//
//   误判①:我以为 QJsonValue::fromVariant 不认 QColor,颜色属性存盘全丢。
//     实测:【错的】。Qt 6 的 fromVariant 认 QColor,不透明色往返完全正常
//     (#aabbcc 进 #aabbcc 出),tst_ProjectSchema.cpp:337 的老注释早写明了。
//     错因:只看了 QJsonValue 文档"只支持 map/list/string/double/bool/int"
//     的概述就下结论。
//
//   误判②:改成显式分支后,我在注释里写「c.name() 对 alpha != 255 会输出
//     8 位」。实测:【也是错的】。QColor::name() 的默认格式是 HexRgb,
//     【无论 alpha 是不是 255 一律输出 6 位】,alpha 照丢 —— 测试当场变红
//     才暴露出来。必须显式按 alpha 分支选 name() 还是 name(HexArgb)。
//
// 【真正的问题只有一条:alpha 被丢】QJsonValue::fromVariant 走 QColor::toString(),
//   而 QColor::name() 的默认格式是 HexRgb —— 【无论 alpha 是不是 255,一律输出
//   6 位 #RRGGBB】,alpha 直接消失。实测:
//     QColor(1,2,3,78) → 存 "#010203" → 读回 QColor(1,2,3,255)
//   控件里半透明很常见(液体高光、禁用态遮罩、玻璃罐体),存一次盘再打开
//   就变成不透明 —— 画面上表现为"某些控件莫名其妙变实了",不报错不崩溃。
//
// 【顺带修的第二个问题:非法值会画没】走通用反射路径时,JSON 里
//   fillColor:"not-a-color" → QVariant 是 QString,prop.write 进去后
//   QColor 解析失败变成 QColor() = 全透明黑,控件直接从画面上消失。
//   工程文件是手写的,这个字段打错字完全可能。colorFromJson 用【当前值】
//   当 fallback,保留原色。
QJsonValue colorToJson(const QColor& c) {
    // ★ 按 alpha 分支,不是无脑 name() 或无脑 name(HexArgb):
    //   · 不透明 → name() 6 位 #RRGGBB,【与改动前逐字节相同】,
    //     存量工程文件和 tst_ProjectSchema.cpp:338 的断言都不受影响
    //   · 半透明 → name(HexArgb) 8 位 #AARRGGBB,alpha 才活得下来
    //   无条件 HexArgb 会把【所有】颜色变 8 位,属于无谓地改存量格式。
    return QJsonValue(c.alpha() == 255
                      ? c.name()
                      : c.name(QColor::HexArgb));
}

QColor colorFromJson(const QJsonValue& v, const QColor& fallback) {
    const QString s = v.toString();
    if (s.isEmpty()) return fallback;
    const QColor c(s);
    if (!c.isValid()) return fallback;   // 打错字 → 保留原色,不画没
    return c;
}

// ★ E.1c:把控件传来的通用 QVariant 适配成 Variable 声明的精确类型
//
// 【为什么必须有】Variable::setValue 里的 matchesDataType(Variable.cpp:35-45)
//   是【严格 typeId 相等】:Bool 只认 QMetaType::Bool,Float 只认 QMetaType::Float,
//   double 一个都不认。但控件内部一律用 double 算 —— ToggleControl.cpp:68 的
//   `value().toDouble() < 0.5`、SliderControl 的百分比、Gauge 的比例,全是 double。
//   不适配的结果:每个开关、每个滑块点下去都被 Variable 静默拒绝 ——
//   画面不动、PLC 不动,日志里只有一行 type mismatch。写回通道等于没接通。
//
// 【跟既有写路径一致】Binding.cpp:120-137 的 inline edit 同样做这个适配
//   (按 dataType 分支 fromValue<qint16>/<quint16>/<qint32>/<float>),只是它
//   在调用方做。这里放在 writeToPlc 是因为控件有 22 个派生类,散着做必漏。
//
// 【转不了就返回空 QVariant】调用方按"值非法"处理,跟 Rejected 走同一条路。
QVariant coerceToDataType(const QVariant& v, Variable::DataType t) {
    // 已经是对的类型就别动 —— 避免 float→float 之类无意义的往返丢精度
    if (v.typeId() == static_cast<int>(t)) return v;

    switch (t) {
    case Variable::DataType::Bool: {
        // 非 0 即真。QVariant::toBool 对字符串会按 "true"/非空 判断,
        // 但变量值不会来自字符串(PLC 那边是寄存器),这里只处理数值/布尔
        if (v.typeId() == QMetaType::QString) return {};
        return QVariant::fromValue<bool>(v.toDouble() != 0.0);
    }
    case Variable::DataType::Int16: {
        bool ok = false;
        const qint64 n = v.toLongLong(&ok);
        if (!ok || n < std::numeric_limits<qint16>::min()
                 || n > std::numeric_limits<qint16>::max()) return {};
        return QVariant::fromValue<qint16>(static_cast<qint16>(n));
    }
    case Variable::DataType::UInt16: {
        bool ok = false;
        const qint64 n = v.toLongLong(&ok);
        // ★ 无符号:负数直接判非法,不静默回绕成 65535 —— 那会把 -1 写成 65535,
        //   现场表现是"写个 -1 结果寄存器变最大正数",极难排查
        if (!ok || n < 0 || n > std::numeric_limits<quint16>::max()) return {};
        return QVariant::fromValue<quint16>(static_cast<quint16>(n));
    }
    case Variable::DataType::Int32: {
        bool ok = false;
        const qint64 n = v.toLongLong(&ok);
        if (!ok || n < std::numeric_limits<qint32>::min()
                 || n > std::numeric_limits<qint32>::max()) return {};
        return QVariant::fromValue<qint32>(static_cast<qint32>(n));
    }
    case Variable::DataType::Float:
        // toFloat 失败返回 0 且 ok=false,必须查 ok —— 否则用户输 "abc" 会静默写 0
        if (v.typeId() == QMetaType::QString) {
            bool ok = false;
            const float f = v.toString().toFloat(&ok);
            return ok ? QVariant::fromValue<float>(f) : QVariant();
        }
        return QVariant::fromValue<float>(v.toFloat());
    }
    return {};
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

// ★ E.4a:返回所有注册键(调色板用)— 按 key 字母序
QStringList ControlBase::registeredTypeNames() {
    QStringList keys;
    keys.reserve(registry().size());
    for (auto it = registry().begin(); it != registry().end(); ++it) {
        keys.append(it.key());
    }
    std::sort(keys.begin(), keys.end());
    return keys;
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
    // ★ Qt QGraphicsItem 强制要求:boundingRect 变化前必须调 prepareGeometryChange,
    //   否则 BspTreeIndex/sceneBoundingRect cache 不会更新,缩小时旧区域内容残留(残影)。
    prepareGeometryChange();
    size_ = s;
    update();          // 触发重绘
    emit sizeChanged();
}

void ControlBase::setWidth(qreal w) {
    setSize(QSizeF(w, size_.height()));
}
void ControlBase::setHeight(qreal h) {
    setSize(QSizeF(size_.width(), h));
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

// ★ E.4d 续-6:itemChange → emit positionChanged on ItemPositionHasChanged
//   关键:Qt 的 ItemIsMovable 拖动内部直接调 setPos,绕开我们的 setPosition() wrapper。
//   setPos 不 emit 我们的 positionChanged,所以光在 setPosition 里 emit 不够。
//   这里 hook ItemPositionHasChanged — Qt 在每次 setPos 完成后都会经过这个事件,
//   包括 ItemIsMovable 拖动、animation、parent-driven 子节点移动。
//   派生类如果重写 itemChange 必须调 ControlBase::itemChange 以保证这条信号链不断。
QVariant ControlBase::itemChange(QGraphicsItem::GraphicsItemChange change,
                                 const QVariant& value) {
    if (change == QGraphicsItem::ItemPositionHasChanged) {
        emit positionChanged();
    } else if (change == QGraphicsItem::ItemTransformHasChanged ||
               change == QGraphicsItem::ItemScaleChange) {
        // ★ transform 变化也会影响 sceneBoundingRect,需要刷新依赖它的人
        //   (SelectionFrameItem / EditorGroupHandle 的 layout)
        //   注意:ItemTransformHasChanged 是通知事件,value 是新 transform
    }
    return QGraphicsObject::itemChange(change, value);
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
        } else if (type == QMetaType::QColor) {
            value = colorToJson(prop.read(this).value<QColor>());
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
        } else if (type == QMetaType::QColor) {
            // ★ 读当前值当 fallback:字段缺失/格式不对时保留现有颜色,
            //   不用 QColor() 默认(全透明黑)把画面画没
            value = QVariant::fromValue(
                colorFromJson(obj.value(name), prop.read(this).value<QColor>()));
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

// ★ E.5(2026-09-29):按名绑定 —— 补上"JSON 字符串 → Variable 指针"这一环
//
//   【为什么需要】编辑器把 variableName 存进 JSON,主画面读回来时手里只有
//   字符串。以前 bind() 只收 Variable*,中间这步没人做 —— 于是编辑器存的画面
//   在主画面里一个都绑不上。这是 E 阶段标完工时漏掉的一环。
//
//   【两种名字都要收】
//   JSON 里存的是 bind() 自动写进去的全名 "plc1.Speed"(ControlBase.cpp:323),
//   而 VariableManager::findVariable 只认 Variable::name() 即短名 "Speed"。
//   属性面板里用户又可能直接手填短名。两种都试一遍,少一种就是"用户填对了
//   却没绑上"这种没法自查的坑。
bool ControlBase::bindByName(VariableManager* vm) {
    if (!vm) return false;
    varMgr_ = vm;   // 顺手存住,后续 writeToPlc 等路径可用
    const QString name = variableName();
    if (name.isEmpty()) return false;

    // ★ 三个查找分支统一走 bindAndRestore,不要各写各的。
    //   【为什么】第一版三条分支里只有两条记得"改回去",结果用户填短名 "Temp"
    //   却被悄悄改写成 "plc1.Temp" —— 打开工程这个【只读动作】不该改工程文件的内容。
    //   tst_RuntimeCanvas::bindByName_acceptsShortName 就是为这条断言存在的。
    const auto bindAndRestore = [this, &name](Variable* v) {
        if (!v) return false;
        bind(v);
        // bind() 内部会 setVariableName(规范全名),这里改回用户填的原样。
        // 理由:variableName_ 是工程文件的一部分,"打开"这个动作不该动它。
        setVariableName(name);
        return true;
    };

    // 1) 直接按名查(findVariable 认的是短名)
    if (Variable* v = vm->findVariable(name)) {
        return bindAndRestore(v);
    }
    // 2) 全名 "plc1.Speed" → 剥掉 plcId 前缀再试
    const int dot = name.indexOf(QLatin1Char('.'));
    if (dot > 0) {
        if (Variable* v = vm->findVariable(name.mid(dot + 1))) {
            return bindAndRestore(v);
        }
    }
    // 3) 找别名(D.5 机制:操作员看到的"转速" → plc1.Speed)
    if (Variable* v = vm->findByAlias(name)) {
        return bindAndRestore(v);
    }
    // 找不到就明确返回 false,不做任何"假装成功"的处理
    return false;
}

void ControlBase::onVariableValueChanged() {
    if (!bound_) return;
    setValue(bound_->value());
}

// ===== E.1c UI → PLC 反向写通道 =====

// ★ 权限判定单独抽出来:writeToPlc 要用,派生类(阀门/泵/电机)点击前也要用
//   —— 没权限 / 只读变量时应该画成灰的让操作员一眼看出点不动,而不是点了没反应。
//   角色 → 权限映射见 auth/Permission.cpp:60 checkPermission。
bool ControlBase::canWrite() const {
    if (!bound_) return false;
    if (bound_->access() != Variable::Access::ReadWrite) return false;
    return qwhmi::auth::checkPermission(qwhmi::auth::CurrentUser::instance().role(),
                                        qwhmi::auth::Permission::WriteVariable);
}

// ★ IMPROVE #28 同款教训(见 ActionManager.cpp:75-86):setValue 返 Rejected 时
//   【绝不能】照样 markDirty —— 那样审计会记「已执行」但 PLC 实际啥也没收到,
//   事故复盘时唯一证据是假的。所以四道闸任一不过,都 return false 且不标 dirty。
bool ControlBase::writeToPlc(const QVariant& v) {
    // 闸 1:没绑定变量 —— 纯编辑器里的摆件,没连 PLC 就没有可写的对象
    if (!bound_) return false;

    // 闸 2:只读变量(传感器数据)。跟 Binding.cpp 的权限闸并列,不合并 ——
    //   一个是「这台 PLC 不许写」,一个是「这个人没资格写」,拒绝理由不同,排查时要能分开。
    if (bound_->access() != Variable::Access::ReadWrite) return false;

    // 闸 3:权限。Operator / Engineer / Admin 都有 WriteVariable(见 Permission.cpp:29-46),
    //   Viewer 没有。跟 Binding.cpp:147-155 同源。
    if (!qwhmi::auth::checkPermission(qwhmi::auth::CurrentUser::instance().role(),
                                      qwhmi::auth::Permission::WriteVariable)) {
        qCWarning(qwhmi::auth::hmiPerm) << "[Control] 本用户" << qwhmi::auth::CurrentUser::instance().name()
                           << "无 WriteVariable 权限,控件写入被拒:" << bound_->name();
        return false;
    }

    // 闸 4:先按变量的 DataType 把控件的 double 适配成精确类型,再交 Variable 校验。
    //   Variable::setValue 的 matchesDataType 是严格 typeId 相等(Variable.cpp:35-45),
    //   不适配就等于闸 4 永远在拦 —— 写回通道看着通着,实际什么都写不进去。
    //   适配本身转不了(字符串、溢出 UInt16 等)也算这一闸拒。
    const QVariant typed = coerceToDataType(v, bound_->dataType());
    if (!typed.isValid()) {
        qCWarning(qwhmi::auth::hmiPerm) << "[Control] 值" << v.toString()
                           << "无法适配成变量" << bound_->name() << "的数据类型";
        qwhmi::auth::AuditLog::instance().appendCurrent(
            QStringLiteral("control.write"), bound_->name(),
            QStringLiteral("value=%1").arg(v.toString()), QStringLiteral("rejected"));
        return false;
    }

    // Variable 自己的范围校验。Rejected 时不碰本地 value_,
    //   保持跟 Variable 里的真值一致 —— 免得画面显示的值和 PLC 里的值对不上。
    const Variable::SetValueResult r = bound_->setValue(typed);
    if (r == Variable::SetValueResult::Rejected) {
        qCWarning(qwhmi::auth::hmiPerm) << "[Control] 变量" << bound_->name()
                           << "拒绝值" << typed.toString() << "(超范围)";
        // 审计记 fail:PLC 侧没收到,复盘时这里就是证据
        qwhmi::auth::AuditLog::instance().appendCurrent(
            QStringLiteral("control.write"), bound_->name(),
            QStringLiteral("value=%1").arg(typed.toString()), QStringLiteral("rejected"));
        return false;
    }

    // 四闸全过:本地立刻更新(乐观 UI —— 操作员点了要马上看到反应,不等 PLC 回读)
    setValue(v);
    // ★ 关键一行:markDirty 才是真正让 WriteScheduler 动起来的开关
    //   (WriteScheduler.cpp:51-52 每帧扫 isDirty)。光 setValue 不写 PLC。
    //   Accepted 和 NoChange 都标 —— 跟 Binding.cpp:168-170 同一决策:
    //   值没变也可能 PLC 端被外部改过,写回去对齐。
    bound_->markDirty();

    // 留痕,跟 Binding.cpp:173-176 的 "variable.write" 同格式
    qwhmi::auth::AuditLog::instance().appendCurrent(
        QStringLiteral("control.write"), bound_->name(),
        QStringLiteral("value=%1").arg(v.toString()), QStringLiteral("ok"));
    return true;
}

} // namespace qwhmi::widgets