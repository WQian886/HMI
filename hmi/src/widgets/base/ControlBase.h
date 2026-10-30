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
#include <QPixmap>
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
class AlarmManager;      // ★ F3:只存指针 + 传给派生类钩子,不需要完整类型
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
    // ★ E.8(2026-09-30)底图:22 个控件手绘的矢量外观太"程序味",用户要能换自己的图。
    //   下沉到基类而不是每控件各写一份 —— 22 个控件各写一遍就是 22 份几乎相同的
    //   加载/缓存/降级逻辑,漏一个就是一个"别的控件不认图"的怪现象。
    //   scaleToFit=true 拉伸铺满(工业元件背景图一般按控件尺寸配);
    //   false 则原图尺寸画在左上角。
    Q_PROPERTY(QString imagePath READ imagePath WRITE setImagePath NOTIFY imagePathChanged)
    Q_PROPERTY(bool    scaleToFit READ scaleToFit WRITE setScaleToFit NOTIFY scaleToFitChanged)

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

    // ===== 底图(E.8)=====
    QString imagePath() const { return imagePath_; }
    void    setImagePath(const QString& path);
    bool    scaleToFit() const { return scaleToFit_; }
    void    setScaleToFit(bool s);

    // ★ 相对路径基准目录(通常是工程文件所在目录)。
    //   为什么必须存:工程文件会被人拷走、发给别人、图片也可能单独放到 assets/ 下,
    //   存绝对路径 = 换个机器全丢(跟 tst_ProjectSchema 那个硬编码路径同一类错误,
    //   只不过这次是我们自己埋的)。空 = 相对进程当前目录(编辑器未保存工程时)。
    void    setBasePath(const QString& dir) { setBasePathInternal(dir); }
    QString basePath() const { return basePath_; }
    //   实际读盘用的绝对路径(imagePath 为相对时拼 basePath_)
    QString resolvedImagePath() const;

    // ★ 加载是否成功。false = 设了路径但没画出来(文件不存在/不是图片/解码失败)。
    //   属性面板据此给红色提示 —— 静默画灰底会让人以为"设了没生效"。
    //   会触发一次读盘(非 const),因为"打开工程时"和"首次绘制前"是两个时刻:
    //   属性面板可能在控件被画过一次之前就来问,那时缓存还是空的,
    //   不主动读就只能返回"不知道",红边提示就永远不出现。
    bool    imageLoaded();

    // ===== 通用接口 — 派生类必须实现 =====
    QRectF boundingRect() const override = 0;

    // ★ E.8 改造:原来是纯虚 paint(),现改为基类模板方法 ——
    //   基类 paint() 先画底图,再调子类的 paintContent() 画矢量层。
    //   【为什么不留在子类里画底图】那样每个控件的 paint() 都要开头写一句
    //   drawBackgroundImage(),漏一个就是一个"这个控件不认图"的怪现象。
    //   【为什么图在下层】用户嫌丑的是【外观】,不是【状态】——
    //   泵转不转、罐里多少液位、阀门开没开是功能,被一张图盖掉就是废品。
    void   paint(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override final;

    // 派生类改实现这个,不用再管底图
    virtual void paintContent(QPainter* painter,
                              const QStyleOptionGraphicsItem* option,
                              QWidget* widget = nullptr) = 0;

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

    // ★ E.5(2026-09-29):按变量名绑定 —— 运行时画布从 JSON 恢复画面的入口
    //   背景:JSON 里存的是 variableName 字符串(编辑器期手填),而 bind() 要的是
    //         Variable*。中间这步"字符串 → 指针"以前没人做,所以编辑器存的画面
    //         在主画面里永远绑不上 —— E 阶段标完工时漏掉的就是这一环。
    //   查名支持两种:① 直接的名字(plc1.Speed)② 短名(Speed)。
    //     因为 VariableManager 里 Variable::name() 存的是短名,编辑器属性面板
    //     显示的却是全名,两种都收才不会让用户猜。
    //   返回 true = 找到并绑定上了;false = 没找到,控件保持未绑定(不静默假装成功)。
    bool bindByName(VariableManager* vm);
    // ★ 运行时画布批量用:先给一批控件设好 varMgr_,再逐个 bindByName(nullptr)
    //   —— varMgr_ 为空时直接返回 false,避免每个调用点都判空。
    void setVariableManager(VariableManager* vm) { varMgr_ = vm; }
    VariableManager* variableManager() const { return varMgr_; }

    // ===== ★ F3(2026-09-30):运行时 / 编辑模式 =====
    //
    // 【为什么需要这个开关 —— 这是 F3 撞上 E.4b 的硬冲突】
    //   AlarmListControl 用 QGraphicsProxyWidget 包一个真 QTableWidget。
    //   E.4b 为了让控件在编辑器里能被选中/拖动/缩放,给它设了
    //   setAcceptedMouseButtons(Qt::NoButton)(见 AlarmListControl.cpp:34)。
    //   那个设置的作用是【代理不吃鼠标事件】,事件穿透到父 item ——
    //   父才有 press 才能被选中、能拖。
    //   但代理不吃事件的直接后果是:内嵌的 QTableWidget 也一个鼠标事件都收不到,
    //   连选中行都不行,更别说双击确认。
    //   两边都要,就必须能切:编辑模式穿透(选得中、拖得动),运行时模式吃事件(点得动)。
    //
    // 【为什么不靠"在不在编辑器里"判断】
    //   EditorMainWindow 有 editorMode_ 开关,但它只用来灰掉属性面板,
    //   从没往控件上传过;而且 RuntimeCanvas 是另一个顶层窗口,跟它无关。
    //   让控件自己去猜宿主是谁,就会又出现"运行时画布上的按钮点不动"这类问题。
    //   谁创建控件谁显式声明 —— 跟 setVariableManager 同一个思路。
    //
    // 【默认 false】默认编辑模式。理由:编辑器/调色板/绝大多数测试都是编辑场景,
    //   默认成编辑模式意味着"不主动声明就是安全的",漏声明的表现是"运行时点不动"
    //   (立刻能看出来),而不是"编辑器里莫名拖不动"(隐蔽得多,还容易被当成 bug 报上来)。
    void setRuntimeMode(bool on);
    bool runtimeMode() const { return runtimeMode_; }

    // 派生类钩子:运行时模式切换时按需调整自己的交互。
    // 默认空实现 —— 纯矢量控件(按钮/阀门/泵)两种模式行为一致,不用管。
    // 需要管的是内嵌 widget 的那几个:AlarmListControl / TrendControl / TableControl。
    virtual void onRuntimeModeChanged(bool /*runtime*/) {}

    // ★ F3:给需要报警数据的控件一个统一的注入口。
    //   跟 setVariableManager 对称:RuntimeCanvas 批量设一次,控件自己决定要不要。
    //   目前只有 AlarmListControl 用得上;别的控件忽略即可。
    //
    // 【为什么必须有 —— 这是 F3 挖出来的第 5 个静默失败】
    //   RuntimeCanvas::loadControls 以前只做 setBasePath + scene_->addItem,
    //   【从没调过 bindAlarmManager】。所以运行时画布上的 AlarmListControl
    //   拿到的 alarmMgr_ 恒为 nullptr —— 表格永远是空的,一行都没有,
    //   而且不报错、不弹窗、终端里也一行日志都没有。
    //   画面上"摆了个报警列表但永远是空的",用户能做的只有猜。
    //   这跟 E.5 修的"编辑器存的画面不绑变量"是同一类病:
    //   链路的一环没人做,两端各自都正常,拼起来才缺。
    //
    //   【不要写成 inline 就直接存指针】—— 那样 onAlarmManagerChanged 永远不会被调用,
    //   变成一个"声明了但没人调"的死钩子(本项目的 E.5 dead member、5.2 死枚举
    //   都是这个形状)。实现在 ControlBase.cpp,里面存完就通知。
    void setAlarmManager(AlarmManager* mgr);
    AlarmManager* alarmManager() const { return alarmMgr_; }
    // 派生类钩子:拿到 / 失去报警管理器。默认空实现。
    virtual void onAlarmManagerChanged(AlarmManager* /*mgr*/) {}

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

    // ★★ G-22(2026-10-30):上一次写被拒的理由 ★★
    //   writeToPlc() 的每一道闸拒绝时都会把理由记在这里,
    //   派生类再用 notifyWriteRejected() 把它发出去。
    //   【为什么不让派生类自己编理由】
    //     原来 Valve/Pump/Motor 各自在 mouseReleaseEvent 里现编一句,
    //     而 ToggleControl【根本没有】这个信号 ——
    //     于是"写被拒"这件事在不同控件上表现完全不同,
    //     有的有原因、有的【一声不吭】。
    QString lastWriteRejection() const { return lastWriteRejection_; }

protected:
    // ★ G-22:派生类在 writeToPlc() 返回 false 之后调它 ——
    //   补齐理由(必要时给兜底)+ 留一条审计 + 把信号发出去。
    //   ★ 只有【真的没写成】才调它 ★
    //     编辑器里未绑定控件的"本地预览"不算被拒,不许调。
    void notifyWriteRejected();

    // ★ G-21(2026-10-30):能不能"本地翻一下当预览"?
    //
    //   【只有编辑模式下未绑定的控件才可以】
    //     那是作者在编辑器里摆件、点一下看效果 —— 不是在操作设备。
    //
    //   【★ 运行时模式下绝对不行 ★】
    //     运行时画面上摆了个没接线的元件,操作员点它一下,
    //     画面上那个阀门"开了"而 PLC 纹丝不动 ——
    //     操作员据此判断管路状态,阀门背后是高压管路。
    //     这跟 ActionManager.cpp:75-86 修的"联动撒谎上报"是同一类事故,
    //     而且更严重:那个至少在审计里留了假记录,这个是【画面直接骗人】。
    //
    //   【判据放在基类的理由】
    //     "运行时还是编辑"是【宿主】的性质,不是每个控件各自决定的事。
    //     让 4 个控件各写一遍 if (runtimeMode()) 必然漏一个 ——
    //     漏掉的那个就是"画面撒谎"的那一个。
    bool mayFlipLocallyAsPreview() const;

signals:
    void positionChanged();
    void sizeChanged();
    void variableChanged();
    void valueChanged();
    void imagePathChanged();
    void scaleToFitChanged();

    // ★★ G-22(2026-10-30):写回被拒 ★★
    //
    //   【原来这个信号只在 Valve/Pump/Motor 三个类上各声明了一份】,后果有三:
    //     ① ToggleControl(开关)没有 —— 它写被拒时【一声不吭】;
    //     ② 上层想统一处理"所有控件的写被拒"就接不到 ——
    //        信号不在基类上只能逐个类型 connect,漏一个就是一个静默的口子;
    //     ③ 全仓库【没有任何人接】它 —— 写被拒了操作员不知道、画面不变、
    //        日志里也未必有,现象跟"没点"一模一样。
    //   改法:信号下沉到基类,三个派生类删掉自己那份。
    //     (已有测试写的是 &ValveControl::writeRejected —— 信号继承下来之后
    //      那个写法照样编译通过、照样测的是同一个信号。)
    //
    //   【谁接】RuntimeCanvas(运行时画布)统一接:红条提示 + 日志 + 审计。
    //   detail 是给用户看的中文原因。
    void writeRejected(const QString& detail);

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
    // ★ G-22:上一次写被拒的理由(见 lastWriteRejection())
    QString      lastWriteRejection_;
    // ★ E.5(2026-09-29):这个成员以前声明了但从来没赋值过(全文件无赋值点),
    //   是条死成员。bindByName() 让它第一次有了用处。
    //   【为什么用成员而不是全局单例】VariableManager 没有 instance(),
    //   它是 hmi 构造时 new 出来的实例,生命周期跟主窗口绑定。用全局单例会
    //   引入一个现在不存在的概念,且测试里每个用例都得清全局状态。
    VariableManager* varMgr_ = nullptr;     // 查变量用(★ E.5 接真实实例)

    // ★ F3(2026-09-30)
    bool runtimeMode_ = false;             // 默认编辑模式(见 setRuntimeMode 注释)
    AlarmManager* alarmMgr_ = nullptr;     // 报警数据源(只有 AlarmListControl 用)

    // ★ E.8 底图成员
    QString imagePath_;
    QString basePath_;                       // 相对路径基准(工程文件所在目录)
    QPixmap cachedPixmap_;
    QString cachedForPath_;                  // ★ 防重复读盘:paint 每帧都调,
                                             //   不缓存就是每帧读一次文件。
                                             //   存"上次读的是哪条路径"来判失效。
    bool    scaleToFit_ = true;
    bool    imageLoadFailed_ = false;        // 设了路径但没加载出来(给属性面板提示)
    //   读盘 + 缓存的唯一入口。paint() 和 imageLoaded() 共用它,
    //   免得"面板说成功"和"画面上真画出来了"是两套判据。
    bool    ensureImageLoaded();
    //   ★ 换工程目录必须让缓存作废:basePath_ 变了之后同一个相对路径
    //   指向的是另一个文件,不失效就是"打开 B 工程,画面上还挂着 A 工程的图"。
    void    setBasePathInternal(const QString& dir);

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