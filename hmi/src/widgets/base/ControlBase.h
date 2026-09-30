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
    void imagePathChanged();
    void scaleToFitChanged();

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
    // ★ E.5(2026-09-29):这个成员以前声明了但从来没赋值过(全文件无赋值点),
    //   是条死成员。bindByName() 让它第一次有了用处。
    //   【为什么用成员而不是全局单例】VariableManager 没有 instance(),
    //   它是 hmi 构造时 new 出来的实例,生命周期跟主窗口绑定。用全局单例会
    //   引入一个现在不存在的概念,且测试里每个用例都得清全局状态。
    VariableManager* varMgr_ = nullptr;     // 查变量用(★ E.5 接真实实例)

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