// ============================================================================
// 阶段 E · E.5 · RuntimeCanvas 运行时画布
//
// 文件职责:把编辑器存的工程文件在【运行时】显示出来。
//
// 【为什么需要这个类 —— E 阶段标完工时漏掉的一环】
//   编辑器(EditorMainWindow)从 E.4a 就能画控件、存 .hmi.json 了,数据格式也
//   是完整的(tmp 探针实测:position/size/variableName/rotation 全都在 JSON 里,
//   loadControlsFromJson 也能 2/2 还原)。但——
//   ★ 主画面从来没有【消费】过这份数据。
//   hmi.cpp 全文的所有 QGraphicsScene/addItem 都在 onDemoCanvasClicked() 里,
//   那是 E.1a 的演示画布,控件是 C++ 硬编码 new 的,跟编辑器无关。
//   于是形成了一个荒谬的局面:编辑器画半天,存盘,主画面什么都不显示。
//
//   本类就是那个缺失的消费者。
//
// 【关键设计】
//   1. 不复用演示画布 —— 演示画布是 9 个写死的控件,加第 10 个要改 hmi.cpp。
//      运行时画布必须是【数据驱动】的:JSON 进 → 控件出。
//   2. 绑定由本类负责 —— ControlBase::bindByName() 补上了
//      "JSON 里的 variableName 字符串 → Variable* 指针" 这一环。
//      以前 bind() 只收指针,中间这步没人做,所以存的画面一个都绑不上。
//   3. 编辑器画布不共享控件对象 —— 编辑器改一个控件,运行时这边跟着变?不。
//      两边各自从 JSON 重建,各自持有自己的 ControlBase 实例。
//      共享对象会导致删一个控件两边都没了、Undo 直接崩编辑器。
//
// 关联:ControlBase.cpp(bindByName), Project.cpp(loadFromFile / controlsLoaded),
//      hmi.cpp(onRuntimeCanvasClicked)
// ============================================================================

#pragma once

#include <QList>
#include <QWidget>

class QGraphicsScene;
class QGraphicsView;
class Project;
class VariableManager;

namespace qwhmi::widgets { class ControlBase; }

namespace qwhmi::editor {

class RuntimeCanvas : public QWidget {
    Q_OBJECT
public:
    // varMgr:绑定变量用(传 hmi 的 varMgr_,别自己 new —— 两份变量表不是一回事)
    // project:共享主画面的 Project 实例,不接它的 signals,只被动调 load()
    explicit RuntimeCanvas(VariableManager* varMgr, QWidget* parent = nullptr);
    ~RuntimeCanvas() override;

    // ★ 从工程文件加载画面。返回成功创建的控件数。
    //   0 不一定是失败 —— 工程里一个控件都没有也是 0。要区分看 lastError()。
    int load(const QString& path);

    // ★ 直接从内存里的一组控件重建(测试用,绕开文件 IO)
    //   controls 的所有权转移给本对象。
    void loadControls(const QList<qwhmi::widgets::ControlBase*>& controls);

    // 清空画面(不 delete 控件 —— scene 会接管)
    void clearCanvas();

    // 当前画面上的控件(测试访问器)
    QList<qwhmi::widgets::ControlBase*> controls() const { return controls_; }
    // ★ 绑上变量的控件数 / 应绑的控件数 —— 这两个不等就是有名字打错了。
    //   静默失败的防线:加载完先看这个数,别等运行时报"变量没反应"。
    int boundCount() const { return boundCount_; }
    int expectedBindCount() const { return expectedBindCount_; }
    QStringList unboundNames() const { return unboundNames_; }
    QString lastError() const { return lastError_; }

    QGraphicsView* view() const { return view_; }
    QGraphicsScene* scene() const { return scene_; }

private:
    QGraphicsView*  view_      = nullptr;
    QGraphicsScene* scene_     = nullptr;
    VariableManager* varMgr_   = nullptr;
    QList<qwhmi::widgets::ControlBase*> controls_;

    // ★ 绑定统计 —— 见 boundCount() 注释
    int         boundCount_       = 0;
    int         expectedBindCount_ = 0;
    QStringList unboundNames_;      // 绑不上的控件 variableName(给用户看的报错依据)
    // ★ 加载失败的原因(空 = 这次没失败)。分开存而不是塞进 unboundNames_,
    //   是因为"文件打不开"和"变量名对不上"是两类完全不同的毛病,
    //   UI 上要给两种完全不同的提示。
    QString lastError_;
    // ★ E.8:工程文件所在目录 —— 底图相对路径的解析基准
    QString basePath_;

    void bindAllControls();
};

} // namespace qwhmi::editor
