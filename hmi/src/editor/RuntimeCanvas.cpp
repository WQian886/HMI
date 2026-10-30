// ============================================================================
// RuntimeCanvas 实现
// ============================================================================

#include "editor/RuntimeCanvas.h"
#include "editor/EditorMainWindow.h"   // ★ 只为 ensureControlTypesRegistered()
#include "widgets/base/ControlBase.h"
#include "Project.h"
#include "../../AlarmManager.h"       // ★ F3:setAlarmManager
#include "../../VariableManager.h"

#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QVBoxLayout>
#include <QPainter>
#include <QLabel>
#include <QTimer>
#include <QPalette>

#include "logging/Logging.h"

namespace qwhmi::editor {

RuntimeCanvas::RuntimeCanvas(VariableManager* varMgr, AlarmManager* alarmMgr, QWidget* parent)
    : QWidget(parent),
      varMgr_(varMgr),
      alarmMgr_(alarmMgr) {
    setWindowTitle(tr("HMI 运行时画面"));

    scene_ = new QGraphicsScene(this);
    scene_->setSceneRect(0, 0, 1000, 700);   // 与编辑器同坐标系(E.0 决策 #8)
    view_ = new QGraphicsView(scene_, this);
    view_->setRenderHint(QPainter::Antialiasing);
    // ★ 底色必须跟 EditorMainWindow::setupCentralWidget 一致(245,245,245 浅灰)。
    //   【别自作主张改颜色】我第一版写了个深蓝 0x1e2430 试图"运行时看起来更像工业屏",
    //   结果是:用户在编辑器里摆好控件、切到运行时,满屏变色,会以为数据/坐标出了问题。
    //   WYSIWYG 的前提就是"两处长得一样",配色属于 WYSIWYG 的一部分,不是美化。
    //   要改配色就两处一起改,并且是有意为之,不是手滑。
    view_->setBackgroundBrush(QColor(245, 245, 245));

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(view_);

    // ★ G-22:拒绝提示条。默认隐藏 —— 平时不该占画面。
    //   为什么是"画布上方一条"而不是弹窗:
    //     弹窗会被操作员随手关掉,关掉之后那件事就没了 —— 而"我点了阀门,
    //     PLC 没动"这件事【必须留痕在屏幕上】,直到下一次成功的操作把它盖掉。
    //
    //   ★ 为什么它【不占布局】(不是 lay->addWidget(banner_)):
    //     一旦放进布局,显示的瞬间画布整体下移 28 像素 —— 运行时看到的画面
    //     跟编辑器里摆的不一样(控件位置全偏了)。这个类上面那段注释专门
    //     强调过 WYSIWYG("两处长得一样"),那不只是颜色的事,是位置的事。
    //     所以提示条做成【浮在画布上】的覆盖层,靠 resizeEvent 摆位。
    banner_ = new QLabel(this);
    banner_->setObjectName(QStringLiteral("writeRejectedBanner"));
    banner_->setWordWrap(true);
    banner_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    banner_->setMinimumHeight(28);
    // 红底白字:现场画面常用深色,红底在深色底上照样跳得出来。
    banner_->setStyleSheet(QStringLiteral(
        "QLabel { background-color:#c62828; color:#ffffff; padding:4px 8px; "
        "font-weight:bold; border-radius:3px; }"));
    banner_->hide();

    // ★ 注册表预热:create(typeName) 靠它,漏了就是"画面空白且零报错"
    EditorMainWindow::ensureControlTypesRegistered();
}

RuntimeCanvas::~RuntimeCanvas() {
    // ★ scene->clear() 不会 delete item(只从场景摘下来),所以必须自己删。
    //   漏了就是 22 个控件逐帧泄漏 —— 不崩,所以更难发现。
    clearCanvas();
    if (scene_) scene_->clear();
}

void RuntimeCanvas::clearCanvas() {
    while (!controls_.isEmpty()) {
        auto* c = controls_.takeFirst();
        if (c && scene_->items().contains(c)) {
            scene_->removeItem(c);
        }
        delete c;   // ★ 本对象拥有 controls_ 的所有权(跟 EditorMainWindow 一致)
    }
    boundCount_ = 0;
    expectedBindCount_ = 0;
    unboundNames_.clear();
    // ★ G-22:换画面时把上一次的"写被拒"提示一并清掉 ——
    //   上一张画面上的拒绝还挂在红条上,会让人以为【这张】画面出问题了。
    lastRejection_.clear();
    rejectionBannerShown_ = false;
    if (banner_) banner_->hide();
}

int RuntimeCanvas::load(const QString& path) {
    lastError_.clear();
    clearCanvas();

    // ★ E.8:先记住工程文件所在目录,loadControls 里给每个控件设相对路径基准。
    //   放在 load 开头而不是 loadControls 里,是因为 loadControls 也被
    //   "从内存灌控件"的测试路径直接调 —— 那条路径的 basePath_ 是上次 load
    //   留下的,会串味(在测试里尤其明显:上一个用例的工程目录泄漏到下一个)。
    basePath_ = QFileInfo(path).absolutePath();

    // ★★ 【为什么不直接用主画面的 project_】—— 这是本类最关键的一个决定
    //   Project::loadFromFile → loadRootFromFile 第 4 步是:
    //       varMgr_->clear();  alarmMgr_->clearAll();  actionMgr_->clear();
    //   也就是说,拿主画面的 project_ 加载一次工程文件,会把【正在跑的】
    //   变量表、报警、动作全清空重建。变量表一清,所有已绑定的 Variable* 全失效,
    //   PLC 轮询队列(WriteScheduler 里按名字缓存的)也跟着错位。
    //   对"运行时"来说这是灾难 —— 用户只是想换个画面,不是想重载整个工程。
    //   所以这里自建一个 4 个 manager 全 nullptr 的 Project:
    //     ① loadRootFromFile 对 null manager 是容错的(只解 schema + screens)
    //     ② 变量/报警/动作一个都不碰
    //     ③ 真正的绑定用 varMgr_(主画面的活着那张表)在 bindAllControls 里做
    //   代价:变量/报警/动作的解析不生效 —— 但运行时画布本来也不该改这些,
    //   那是"打开工程"的活,hmi.cpp 里已经有一份。
    Project loaderProject(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    // 用局部变量接信号,不用成员 —— loadFromFile 是【同步】emit 的,
    // 函数返回时连接随 loaderProject 一起销毁,不留悬挂 lambda。
    // (早期版本用 make_unique 自捕获做"一次性断开",纯属想复杂了:
    //  对象是栈上的局部变量,析构时 Qt 自动断连,没那个必要。)
    QList<qwhmi::widgets::ControlBase*> loadedControls;
    QObject::connect(&loaderProject, &Project::controlsLoaded,
                     this, [&loadedControls](const QList<qwhmi::widgets::ControlBase*>& loaded) {
        loadedControls = loaded;
    });
    // loadError 也接一下:失败时 lastError_ 要有内容,不然 UI 上什么都不显示,
    // 用户只看到一片空画布,不知道是"文件没选对"还是"文件里没控件"。
    QObject::connect(&loaderProject, &Project::loadError,
                     this, [this](const QString& reason) { lastError_ = reason; });

    if (!loaderProject.loadFromFile(path)) {
        if (lastError_.isEmpty()) {
            lastError_ = tr("加载 %1 失败(未给出原因)").arg(path);
        }
        return 0;
    }
    // controlsLoaded 已经把控件交到我们手上了(所有权也一并转移)
    loadControls(loadedControls);
    return controls_.size();
}

void RuntimeCanvas::loadControls(const QList<qwhmi::widgets::ControlBase*>& controls) {
    clearCanvas();

    for (auto* c : controls) {
        if (!c) continue;   // loadControlsFromJson 理论上不返 null,防一手
        // ★ E.8:底图存的是【相对工程文件目录】的路径(见 ControlBase::setBasePath),
        //   不给基准的话相对路径解析不出来 —— 主画面上每个带图控件都变成手绘,
        //   而且只在日志里留一行 qWarning,画面上完全看不出"图丢了"。
        if (!basePath_.isEmpty()) c->setBasePath(basePath_);
        // ★ F3(2026-09-30):运行时画布上的控件必须是【运行时模式】。
        //   这是 F3 能落地的前提,不是顺手加的:
        //   内嵌 widget 的控件(AlarmListControl)在编辑模式下,
        //   代理被设成 Qt::NoButton 让父收事件(为了选中/拖动/8 个 handle)——
        //   代价是内嵌表格一个鼠标事件都收不到,双击确认根本不可能触发。
        //   不设这一句,画面上的报警列表看着正常,双击却毫无反应。
        c->setRuntimeMode(true);
        // ★ F3:接报警数据源。少了这一句,AlarmListControl 的 alarmMgr_ 恒为 null,
        //   表格永远空白且零报错(见 RuntimeCanvas.h 里的说明)。
        if (alarmMgr_) c->setAlarmManager(alarmMgr_);
        // ★ G-22:接"写被拒"。这是全仓库【第一处】真正接这个信号的地方 ——
        //   信号以前只在三个子类上各声明一份,谁都没连,于是"点了没生效"
        //   对操作员是零反馈(现象跟"没点到"一模一样)。
        //   ControlBase::writeRejected 现在在基类上,Valve/Pump/Motor/Toggle
        //   全都发得到同一个信号 —— 一行 connect 覆盖全部控件。
        QObject::connect(c, &qwhmi::widgets::ControlBase::writeRejected,
                         this, &RuntimeCanvas::showWriteRejection);
        controls_.append(c);
        scene_->addItem(c);
    }
    bindAllControls();
}

void RuntimeCanvas::bindAllControls() {
    // ★ 这一步是 E.5 的核心:JSON 里的 variableName 是【字符串】,
    //   而 bind() 收的是 Variable*。以前没有任何代码做这步转换,
    //   所以编辑器存的画面在主画面里一个都绑不上 —— 画面能显示,
    //   但数字全是默认值/空白,用户以为 PLC 没数据。
    //   典型症状:"为什么我存的画面上,温度显示 0.0 而不是实时值?"
    boundCount_ = 0;
    expectedBindCount_ = 0;
    unboundNames_.clear();

    if (!varMgr_) {
        // 没给变量管理器 = 一概绑不上,如实计数,别假装成功
        for (auto* c : controls_) {
            if (!c->variableName().isEmpty()) {
                ++expectedBindCount_;
                unboundNames_.append(c->variableName());
            }
        }
        return;
    }

    for (auto* c : controls_) {
        const QString name = c->variableName();
        if (name.isEmpty()) continue;   // 本来就没打算绑的控件,不算失败
        ++expectedBindCount_;
        if (c->bindByName(varMgr_)) {
            ++boundCount_;
        } else {
            // ★ 记下来而不是 qWarning 后丢掉 —— 变量名打错是用户操作,
            //   他需要看到"你说绑 Speed,我的变量表里没有叫 Speed 的",
            //   而不是一句看不懂的终端输出。
            unboundNames_.append(name);
        }
    }

    if (!unboundNames_.isEmpty()) {
        qWarning() << "[RuntimeCanvas] 有" << unboundNames_.size()
                   << "个控件绑不上变量:" << unboundNames_;
    }
}

// ===== G-22:写被拒 → 操作员看得见 =====

// ★ G-22:把"写被拒"变成三件事 —— 红条、日志、审计。
//   (审计那一半在 ControlBase::notifyWriteRejected 里做,理由是
//    "谁点的、点了什么、为什么没生效"要在控件自己的上下文里才说得清;
//    这里只负责"让操作员当场看见"。)
//
// 【为什么红条【不自动消失】】
//   如果 3 秒后自动隐藏,操作员低头看了一眼 PLC 回来一看红条没了 ——
//   "刚才到底成没成"就又变成了只能猜。
//   它一直挂着,直到下一次写被拒把它换成新的原因。
//   (它不占布局,所以不会挡住画布 —— 见构造函数里那段说明。)
void RuntimeCanvas::showWriteRejection(const QString& detail) {
    lastRejection_ = detail;
    rejectionBannerShown_ = true;

    if (banner_) {
        banner_->setText(tr("写入被拒:%1").arg(detail));
        banner_->show();
        positionBanner();
        banner_->raise();
    }

    // 日志:【必须留一条】。画面是给人看的,日志是给事后复盘看的 ——
    //   事故调查时能查到的第一条就是"操作员点了什么、系统怎么回的"。
    qCWarning(hmiUi) << "[RuntimeCanvas] 控件写入被拒:" << detail;
}

// ★ 提示条是浮层:跟着画布的宽度走,贴在画布顶部。
//   放在布局里会让画布整体下移 —— 运行时看到的元件位置就跟编辑器摆的不一样,
//   操作员点同一个元件会点偏,这是比"少个提示"更严重的事。
void RuntimeCanvas::positionBanner() {
    if (!banner_ || !view_) return;
    const int h = banner_->sizeHint().height();
    banner_->setGeometry(view_->x(), view_->y(), view_->width(), h);
    banner_->raise();
}

void RuntimeCanvas::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    positionBanner();
}

} // namespace qwhmi::editor
