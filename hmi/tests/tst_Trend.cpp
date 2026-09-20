// ============================================================================
// 阶段 D · D.2 · TrendWidget 单测
//
// 验证 5 个核心行为:
//   1. setVariable 绑变量名 + 触发 refresh
//   2. refresh 从 HistoryRecorder.query 加载历史点
//   3. setRange 切时间窗(1h/1d/1w)
//   4. addRealtimePoint (onVariableChanged) 追加实时点
//   5. clearHistory 清空 graph
//
// 关联:TrendWidget.h, HistoryRecorder.h, VariableManager.h, Variable.h
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "widgets/TrendWidget.h"
#include "../src/history/HistoryRecorder.h"  // HMI_SRC_DIR/include src/, so ../src/
#include "VariableManager.h"
#include "Variable.h"

using namespace qwhmi::widgets;
using qwhmi::history::HistoryRecorder;

class TestTrend : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();    // 强制 offscreen 平台
    void cleanupTestCase();

    void setVariable_setsCurrentNameAndTriggersRefresh();
    void refresh_loadsHistoryPoints();
    void setRange_changesWindow();
    void addRealtimePoint_extendsGraph();
    void clearHistory_resetsGraph();
};

void TestTrend::initTestCase() {
    // ★ TrendWidget 内部用 QCustomPlot(QWidget 派生的画图控件),
    //   测试需 GUI 主循环;用 offscreen 平台避免真起窗口
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qInfo() << "[tst_Trend] init,QT_QPA_PLATFORM=" << qgetenv("QT_QPA_PLATFORM");
}

void TestTrend::cleanupTestCase() {
    qInfo() << "[tst_Trend] cleanup";
}

// 公共 fixture:TrendWidget + HistoryRecorder + VariableManager(已 add "Speed")
struct Fixture {
    QTemporaryDir tmp;
    QString dbPath;
    HistoryRecorder* hr = nullptr;
    VariableManager* vm = nullptr;
    Variable* speed = nullptr;
    TrendWidget* tw = nullptr;

    explicit Fixture() {
        QVERIFY(tmp.isValid());
        dbPath = tmp.path() + "/test_trend.db";

        hr = new HistoryRecorder;
        QVERIFY(hr->open(dbPath));
        hr->setSampleInterval(0);     // 不限频
        hr->setChangeThreshold(0.0);  // 不限变化

        vm = new VariableManager;
        speed = new Variable("Speed", 0, Variable::DataType::Float,
                              Variable::Access::ReadOnly, vm);
        speed->setPlcId("plc1");
        vm->addVariable(speed);   // ★ 必须在 setSource 之前 add

        hr->setSource(vm);

        tw = new TrendWidget;
        tw->setHistoryRecorder(hr);
        tw->setVariableManager(vm);
    }
    ~Fixture() {
        delete tw;
        delete vm;     // 析构 Variable
        hr->close();
        delete hr;
    }
};

// ★ 用例 1:setVariable 绑变量名
void TestTrend::setVariable_setsCurrentNameAndTriggersRefresh() {
    Fixture f;
    f.tw->setVariable("plc1", "Speed");
    QCOMPARE(f.tw->currentVariable(), QString("plc1.Speed"));
    QCOMPARE(f.tw->currentRange(), TrendWidget::Range::Hour);
}

// ★ 用例 2:refresh 从 HistoryRecorder.query 加载历史点
void TestTrend::refresh_loadsHistoryPoints() {
    Fixture f;

    // 写 5 条历史点
    for (int i = 0; i < 5; ++i) {
        f.speed->setValue(QVariant::fromValue<float>(static_cast<float>(i * 10)));
        QTest::qWait(50);
    }

    f.tw->setVariable("plc1", "Speed");
    f.tw->refresh();
    // refresh 内 query 当前时间窗(默认 Hour = 最近 1 小时),5 条都在窗内
    QCOMPARE(f.tw->dataCount(), 5);
}

// ★ 用例 3:setRange 切时间窗
void TestTrend::setRange_changesWindow() {
    Fixture f;
    f.tw->setVariable("plc1", "Speed");

    f.tw->setRange(TrendWidget::Range::Day);
    QCOMPARE(f.tw->currentRange(), TrendWidget::Range::Day);

    f.tw->setRange(TrendWidget::Range::Week);
    QCOMPARE(f.tw->currentRange(), TrendWidget::Range::Week);

    f.tw->setRange(TrendWidget::Range::Hour);
    QCOMPARE(f.tw->currentRange(), TrendWidget::Range::Hour);
}

// ★ 用例 4:addRealtimePoint (onVariableChanged) 追加实时点
void TestTrend::addRealtimePoint_extendsGraph() {
    Fixture f;
    f.tw->setVariable("plc1", "Speed");
    f.tw->refresh();
    const int before = f.tw->dataCount();

    // 模拟 PollScheduler 写新值 → emit valueChanged → TrendWidget::onVariableChanged → addData
    f.speed->setValue(QVariant::fromValue<float>(99.5f));
    QTest::qWait(50);   // 让信号跨线程/事件循环传递(其实同线程,但保险)

    QCOMPARE(f.tw->dataCount(), before + 1);
}

// ★ 用例 5:clearHistory 清空 graph
void TestTrend::clearHistory_resetsGraph() {
    Fixture f;

    // 先写点数据 + refresh 加载
    for (int i = 0; i < 3; ++i) {
        f.speed->setValue(QVariant::fromValue<float>(static_cast<float>(i)));
        QTest::qWait(50);
    }
    f.tw->setVariable("plc1", "Speed");
    f.tw->refresh();
    QVERIFY(f.tw->dataCount() > 0);

    f.tw->clearHistory();
    QCOMPARE(f.tw->dataCount(), 0);
}

// ★ QTEST_MAIN 创建 QApplication(TrendWidget 用 QWidget 需要 GUI 主循环;
//   已在 initTestCase 里 QT_QPA_PLATFORM=offscreen,无窗口弹出)
QTEST_MAIN(TestTrend)
#include "tst_Trend.moc"