// ============================================================================
// tst_TableControl
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QApplication>
// ★ 这两个必须在这里 include,不能指望 TableControl.h。
//   它为了少拉头文件只做了 class QTableWidget; 前向声明。
//   前向声明够不够取决于【调用方要用多少】:
//     只把指针传给别的函数   → 够;
//     要调 table->columnWidth()、header->sectionResizeMode() → 不够,
//     编译器报 C2027「使用了未定义类型」。
//   我第一版就是漏了这两个,22 个编译错误全来自这一处。
#include <QTableWidget>
#include <QHeaderView>
#include "widgets/TableControl.h"

using namespace qwhmi::widgets;

class TestTableControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_TableControl] init"; }
    void cleanupTestCase() { qInfo() << "[tst_TableControl] cleanup"; }

    void construct_defaults();
    void headers_changeTriggersUpdate();
    void setCell_addsRowsAndCols();

    // ---- 列宽(2026-10-14 新增)----
    void longContent_notTruncated_afterFix();   // ★ 修复前是红的
    void lastColumn_takesTheSlack();
    void setHeaders_reappliesSizing();           // 改表头后不能退回旧策略
};

void TestTableControl::construct_defaults() {
    TableControl t;
    QCOMPARE(t.size(), QSizeF(300, 120));
    QStringList expected;
    expected << "时间" << "值";
    QCOMPARE(t.headers(), expected);
}

void TestTableControl::headers_changeTriggersUpdate() {
    TableControl t;
    QSignalSpy spy(&t, &TableControl::headersChanged);
    t.setHeaders({"时间", "变量", "值", "质量"});
    QCOMPARE(spy.count(), 1);
    QCOMPARE(t.headers().size(), 4);
}

void TestTableControl::setCell_addsRowsAndCols() {
    TableControl t;
    // 不崩就好 — 内部 QTableWidget 自动扩 row/col
    t.setCell(0, 0, "12:00:00");
    t.setCell(0, 1, "100.5");
    t.setCell(1, 0, "12:00:01");
    t.setCell(1, 1, "101.0");
    // 设置宽列(测试自动扩)
    t.setCell(2, 3, "Good");
    QVERIFY(true);
}

// ============================================================
// 列宽(2026-10-14)
// ============================================================
//
// 【为什么要有这些】生产代码原来只有一句
//     horizontalHeader()->setStretchLastSection(true);
// 看着像"余量给最后一列",实测【它什么都没做】:
//     300px 表格 + 3 列 → 列0=100px 列1=100px 列2=100px
//     合计 300,而 viewport 只有 282 → 列比可视区还宽 18px
// 原因是默认 resizeMode 是 Interactive,三列被平均分掉,余量就是 0。
// 于是「3#锅炉给水温度高高报警」(实测需 144px)被截断。
//
// ★ 负向对照实测(把 applyColumnSizing 换回那一句之后):
//     5 passed / 3 failed —— 本文件三条新测试全红,四个老测试全绿。
//     即红是【这三条测出来的】,不是环境/构建问题。
//
// ★ 这类 bug 正是"代码读起来完全合理,行为却不跟字面意思走"。
//   光读代码看不出来 —— 只能把真实控件量一遍。

namespace {
// 真实点场景:300px 宽的表格,3 列,第一列放长点名
constexpr int kTableWidth = 300;
const QString kLongName = QStringLiteral("3#锅炉给水温度高高报警");

void settle() {
    // 让 Qt 把 ResizeToContents / Stretch 算完
    QTest::qWait(120);
    qApp->processEvents();
}
}  // namespace

// ★ 修复前这条是红的:列0 只有 100px < 需要 144px
void TestTableControl::longContent_notTruncated_afterFix() {
    TableControl t;
    t.setHeaders({"点位名称", "值", "状态"});
    t.setCell(0, 0, kLongName);
    t.setCell(0, 1, "85.3");
    t.setCell(0, 2, "高高报警");
    settle();

    const auto* table = t.tableWidgetForTest();
    QVERIFY(table != nullptr);

    QFontMetrics fm(table->font());
    const int need = fm.horizontalAdvance(kLongName);
    const int got  = table->columnWidth(0);

    QVERIFY2(got >= need,
             qPrintable(QStringLiteral(
                 "列0 只有 %1px,而『%2』需要 %3px —— 内容被截断了。"
                 "(修复前恒为 100px < 144px,这就是负向对照)")
                 .arg(got).arg(kLongName).arg(need)));
}

// 余量确实进了最后一列,而不是凭空消失
void TestTableControl::lastColumn_takesTheSlack() {
    TableControl t;
    t.setHeaders({"点位名称", "值", "状态"});
    t.setCell(0, 0, "A");       // 故意放很短,让 ResizeToContents 收窄前两列
    t.setCell(0, 1, "1");
    t.setCell(0, 2, "1");
    settle();

    const auto* table = t.tableWidgetForTest();
    const int last = table->columnCount() - 1;

    // ★ 分母必须是 viewport()->width(),不是 table->width()。实测数字:
    //       table->width()             = 300
    //       table->viewport()->width() = 282      ← 差 18px
    //       table->frameWidth()        = 1
    //   差的那 18px 是 Qt 给垂直滚动条预留的位置。QHeaderView 的列宽
    //   按 viewport 分配,不是按整个 widget —— 拿 table->width() 当分母
    //   会凭空算出 18px 的"空余",然后误判成"余量漏掉了"。
    //   我第一版就是那么写的,实测直接打脸:合计 282 / viewport 282,余量恰好 0。
    int sum = 0;
    for (int c = 0; c < table->columnCount(); ++c) sum += table->columnWidth(c);
    const int viewport = table->viewport()->width();
    const int slack = viewport - sum;

    QVERIFY2(qAbs(slack) <= 2,
             qPrintable(QStringLiteral(
                 "列宽合计 %1 / viewport %2,空余 %3px 太大 —— "
                 "余量既没给最后一列,也没被别的吃掉。"
                 "(修复前实测:合计 300 / viewport 282 = -18px,"
                 " 三列均分撑得比可视区还宽)"
                 " —— 真正的判据是下面那句 columnWidth(last) > columnWidth(0)")
                 .arg(sum).arg(viewport).arg(slack)));
    // 最后一列不应是最窄的那列(内容只有 1 个字符)
    QVERIFY(table->columnWidth(last) > table->columnWidth(0));
}

// ★ 用户改表头后,新加出来的最后一列也必须拿到 Stretch。
//   setSectionResizeMode 是按【列索引】设的,setColumnCount 之后
//   新列不在策略里 —— 不重设就退回均分,余量又漏掉了。
void TestTableControl::setHeaders_reappliesSizing() {
    TableControl t;
    t.setHeaders({"A", "B"});          // 2 列
    t.setCell(0, 0, "1");
    settle();
    t.setHeaders({"A", "B", "C", "D"}); // 改成 4 列,多出来的两列是新的"最后一列"
    settle();

    const auto* table = t.tableWidgetForTest();
    QCOMPARE(table->columnCount(), 4);

    const QHeaderView::ResizeMode lastMode = table->horizontalHeader()->sectionResizeMode(3);
    QCOMPARE(lastMode, QHeaderView::Stretch);

    // 4 列时也不该回到均分:前两列放长内容,后两列放短内容
    t.setCell(1, 0, kLongName);
    t.setCell(1, 1, kLongName);
    t.setCell(1, 2, "1");
    t.setCell(1, 3, "1");
    settle();
    QVERIFY(table->columnWidth(3) > table->columnWidth(2));
}

QTEST_MAIN(TestTableControl)
#include "tst_TableControl.moc"