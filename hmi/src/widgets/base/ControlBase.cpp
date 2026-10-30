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
// ★ 2026-10-25:类型适配搬去共用模块了。
//   原来这里有一份私有实现(文件下方的匿名 namespace),现在改成调
//   util/ValueCoercion —— 因为 ActionManager 那条写路径需要同一份,
//   而"各写一份"正是本项目反复出事的地方。
//   ★ 那份私有实现里还藏着一个 typeId 碰撞的 bug(见 ValueCoercion.cpp 头部),
//     搬过来的时候一并修掉了。
#include "util/ValueCoercion.h"

#include <QDir>
#include <QHash>
#include <QJsonObject>
#include <QMetaType>
#include <QPainter>
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
// ★ 2026-10-25:整段搬走了 —— 现在在 util/ValueCoercion.h/.cpp。
//   【为什么搬】ActionManager 的联动写入需要同一份适配,而这份原来在
//   本文件的匿名 namespace 里,别的模块够不着 → 只能再抄一份 → 必然漂移。
//   (本项目同一个教训已经犯过两次:MenuBuilder、AlarmTablePresenter。)
//
// 【原来那份里的 bug,搬的时候修掉了】
//   它写的是 `if (v.typeId() == static_cast<int>(t)) return v;` ——
//   拿 QMetaType 的 id 跟 DataType 的【枚举序号】比,两套编号会撞车:
//     DataType::UInt16(2) == QMetaType::Int(2)
//   ⇒ 传 QVariant(Int) 给 UInt16 变量时,"已经是对的类型"会误判成立,
//     原样返回 Int → Variable::setValue 还是拒收 → 适配静默失效。
//   控件那边一直没事,是因为它传的是 double(Double 的 id 是 6,不撞)。
//   详见 util/ValueCoercion.cpp 的文件头。
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

// ===== E.8 底图 =====

QString ControlBase::resolvedImagePath() const {
    if (imagePath_.isEmpty()) return {};
    // 绝对路径直接用(QDir::isAbsolutePath 认 Windows 盘符和 UNC)
    if (QDir::isAbsolutePath(imagePath_)) return imagePath_;
    if (basePath_.isEmpty()) return imagePath_;   // 无基准 = 相对进程当前目录
    return QDir(basePath_).filePath(imagePath_);
}

void ControlBase::setImagePath(const QString& path) {
    if (path == imagePath_) return;
    imagePath_ = path;
    // ★ 路径一变缓存立刻作废。下次 paint 才重新读盘 —— 不在这里读,
    //   因为 setImagePath 可能被 JSON 批量回填调用(22 个控件一次 load),
    //   在这里读等于每个控件都同步读一次盘,编辑器打开会明显卡。
    cachedPixmap_ = QPixmap();
    cachedForPath_.clear();
    imageLoadFailed_ = false;
    update();
    emit imagePathChanged();
}

void ControlBase::setBasePathInternal(const QString& dir) {
    if (dir == basePath_) return;
    basePath_ = dir;
    // ★ 基准目录变了,同一个相对路径现在指向【另一个文件】。
    //   不清缓存的话,打开 B 工程时画面上还挂着 A 工程的图 —— 而且
    //   cachedForPath_ 存的是上次解析出的绝对路径,重新解析后不同,
    //   本来会自然失效……但 basePath_ 设成空再设回同一个值时不会。
    //   显式清一次,把这个时序坑彻底堵死。
    cachedPixmap_ = QPixmap();
    cachedForPath_.clear();
    imageLoadFailed_ = false;
}

void ControlBase::setScaleToFit(bool s) {
    if (s == scaleToFit_) return;
    scaleToFit_ = s;
    update();
    emit scaleToFitChanged();
}

// ★ 读盘 + 缓存。paint() 和 imageLoaded() 都走这一条,保证
//   "面板说加载成功了"和"画面上真的画出来了"是同一个判据。
//   返回 true = 现在有可用图像(或者压根没设路径)。
bool ControlBase::ensureImageLoaded() {
    if (imagePath_.isEmpty()) return true;      // 没配图 = 用手绘,不算失败
    const QString abs = resolvedImagePath();
    // ★ 缓存判据是【绝对路径】而不是 imagePath_:
    //   basePath_ 变了(打开另一个工程)但 imagePath_ 没变时,
    //   按 imagePath_ 判会命中旧缓存 —— 画面上还是上一个工程的图。
    if (cachedForPath_ == abs) return !imageLoadFailed_;

    cachedPixmap_.load(abs);
    cachedForPath_ = abs;
    imageLoadFailed_ = cachedPixmap_.isNull();
    if (imageLoadFailed_) {
        // ★ 加载失败只 qWarning 不 qCritical:路径失效是用户会遇到的常态
        //   (工程拷走了、图片被删了、相对路径基准变了),不是程序错误。
        //   矢量层照画 —— 画面上是个手绘控件 + 一行日志,
        //   好过一片空白或者崩溃。
        qWarning("ControlBase: 底图加载失败,回退手绘矢量 —— %s", qPrintable(abs));
    }
    return !imageLoadFailed_;
}

bool ControlBase::imageLoaded() {
    return ensureImageLoaded();
}

void ControlBase::paint(QPainter* painter,
                        const QStyleOptionGraphicsItem* option,
                        QWidget* widget) {
    // ① 底图(下层)—— 用户换的图
    if (ensureImageLoaded() && !cachedPixmap_.isNull()) {
        const QRectF br = boundingRect();
        if (scaleToFit_) {
            painter->drawPixmap(br.toRect(), cachedPixmap_);
        } else {
            painter->drawPixmap(QPointF(0, 0), cachedPixmap_);
        }
    }

    // ② 矢量层(上层)—— 液位 / 旋转动画 / 状态色,一律照旧
    paintContent(painter, option, widget);
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

// ===== ★ F3(2026-09-30):运行时 / 编辑模式 =====

// 【为什么这里只做两件事,不"聪明"地猜】
//   ① 存状态 ② 通知派生类。
//   不在这里去遍历子 item、不去猜宿主是谁 —— 那些都是派生类自己的事
//   (AlarmListControl 才知道自己有个 QGraphicsProxyWidget 要放行鼠标)。
//   基类要是"顺手"替所有控件改交互,就会重演 E.4b 之前的那个问题:
//   一个基类默认行为,悄悄改掉了 22 个控件的选中/拖动语义。
void ControlBase::setRuntimeMode(bool on) {
    if (runtimeMode_ == on) return;   // 幂等:重复设不重复通知,免得派生类反复重排
    runtimeMode_ = on;
    onRuntimeModeChanged(runtimeMode_);
}

// 【与 setRuntimeMode 完全对称】存完就通知派生类,不管这次是不是"换了个不同的"
// —— 派生类要能靠 onAlarmManagerChanged(mgr == nullptr) 感知"我被断供了",
//   AlarmListControl 正是这么把表格清空的。幂等判断故意只比指针不比别的:
//   同一个 mgr 重复设也要通知,否则"第一次设了没接上,再设一次想补救"会无效。
void ControlBase::setAlarmManager(AlarmManager* mgr) {
    alarmMgr_ = mgr;
    onAlarmManagerChanged(alarmMgr_);
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
//
// ★ G-22(2026-10-30):每一道闸拒绝时都把【理由】记进 lastWriteRejection_。
//   原来"为什么被拒"是 Valve/Pump/Motor 各自在别处现编的,
//   而 ToggleControl 压根没有这个概念 —— 于是"写被拒"在不同控件上
//   表现完全不一样,有的有原因有的【一声不吭】。
//   现在理由统一在这里产生,派生类只负责把它发出去。
bool ControlBase::writeToPlc(const QVariant& v) {
    lastWriteRejection_.clear();

    // 闸 1:没绑定变量 —— 纯编辑器里的摆件,没连 PLC 就没有可写的对象
    if (!bound_) {
        lastWriteRejection_ = tr("控件未绑定变量 —— 画面上这个元件没有接到 PLC,写不出去");
        return false;
    }

    // 闸 2:只读变量(传感器数据)。跟 Binding.cpp 的权限闸并列,不合并 ——
    //   一个是「这台 PLC 不许写」,一个是「这个人没资格写」,拒绝理由不同,排查时要能分开。
    if (bound_->access() != Variable::Access::ReadWrite) {
        lastWriteRejection_ = tr("变量「%1」是只读的(传感器数据),不能写").arg(bound_->name());
        return false;
    }

    // 闸 3:权限。Operator / Engineer / Admin 都有 WriteVariable(见 Permission.cpp:29-46),
    //   Viewer 没有。跟 Binding.cpp:147-155 同源。
    if (!qwhmi::auth::checkPermission(qwhmi::auth::CurrentUser::instance().role(),
                                      qwhmi::auth::Permission::WriteVariable)) {
        qCWarning(qwhmi::auth::hmiPerm) << "[Control] 本用户" << qwhmi::auth::CurrentUser::instance().name()
                           << "无 WriteVariable 权限,控件写入被拒:" << bound_->name();
        lastWriteRejection_ = tr("当前用户「%1」没有写权限,写入被拒")
                                  .arg(qwhmi::auth::CurrentUser::instance().name());
        return false;
    }

    // 闸 4:先按变量的 DataType 把控件的 double 适配成精确类型,再交 Variable 校验。
    //   Variable::setValue 的 matchesDataType 是严格 typeId 相等(Variable.cpp:35-45),
    //   不适配就等于闸 4 永远在拦 —— 写回通道看着通着,实际什么都写不进去。
    //   适配本身转不了(字符串、溢出 UInt16 等)也算这一闸拒。
    const QVariant typed = qwhmi::util::coerceToDataType(v, bound_->dataType());
    if (!typed.isValid()) {
        qCWarning(qwhmi::auth::hmiPerm) << "[Control] 值" << v.toString()
                           << "无法适配成变量" << bound_->name() << "的数据类型";
        lastWriteRejection_ = tr("值 %1 无法适配成变量「%2」的数据类型")
                                  .arg(v.toString(), bound_->name());
        return false;
    }

    // Variable 自己的范围校验。Rejected 时不碰本地 value_,
    //   保持跟 Variable 里的真值一致 —— 免得画面显示的值和 PLC 里的值对不上。
    const Variable::SetValueResult r = bound_->setValue(typed);
    if (r == Variable::SetValueResult::Rejected) {
        qCWarning(qwhmi::auth::hmiPerm) << "[Control] 变量" << bound_->name()
                           << "拒绝值" << typed.toString() << "(超范围)";
        lastWriteRejection_ = tr("变量「%1」拒绝值 %2(超出设定范围)")
                                  .arg(bound_->name(), typed.toString());
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

// ===== G-21 / G-22:写被拒的出口 + "能不能本地预览" =====

// ★ G-21:能不能本地翻一下当预览?
//   只有【编辑模式】才可以 —— 见头文件里那段说明(为什么运行时绝对不行)。
bool ControlBase::mayFlipLocallyAsPreview() const {
    return !runtimeMode_;
}

// ★ G-22:把"写被拒"这件事说出去。
//
//   【为什么审计放在这里,而不是散在四道闸里】
//     原来闸 4/闸 5 各写一条审计,而闸 1/闸 2/闸 3 一条都不写 ——
//     于是"没绑定""只读""没权限"这三种最常见的被拒【查不到】。
//     统一到这里之后:每一次"点了但没写成"都留下一条,
//     而编辑器里的"本地预览"根本不会走到这里(它不是被拒)。
void ControlBase::notifyWriteRejected() {
    if (lastWriteRejection_.isEmpty()) {
        // 兜底:理论上 writeToPlc 每道闸都会写理由,这里防的是"将来加了新闸忘了写"。
        lastWriteRejection_ = tr("写入被拒绝");
    }
    // 审计:一次被拒的写,复盘时要能查到"点的是谁、为什么没生效"。
    qwhmi::auth::AuditLog::instance().appendCurrent(
        QStringLiteral("control.write"),
        bound_ ? bound_->name() : QStringLiteral("<未绑定控件>"),
        lastWriteRejection_, QStringLiteral("rejected"));
    emit writeRejected(lastWriteRejection_);
}

} // namespace qwhmi::widgets