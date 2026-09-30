// ============================================================================
// RuntimeCanvas 实现
// ============================================================================

#include "editor/RuntimeCanvas.h"
#include "editor/EditorMainWindow.h"   // ★ 只为 ensureControlTypesRegistered()
#include "widgets/base/ControlBase.h"
#include "Project.h"
#include "../../VariableManager.h"

#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QVBoxLayout>
#include <QPainter>

namespace qwhmi::editor {

RuntimeCanvas::RuntimeCanvas(VariableManager* varMgr, QWidget* parent)
    : QWidget(parent),
      varMgr_(varMgr) {
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

} // namespace qwhmi::editor
