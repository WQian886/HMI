// ============================================================================
// tst_ProtocolComboShot — 把协议下拉框的弹出框【真的画出来】并存成 PNG
//
// 为什么单元测试答不了这个问题
// ----------------------------
// tst_ProtocolComboGrey 断言的是 itemData(Qt::ForegroundRole) 的数值,
// 那是"数据对了"。但用户看到的是"像素对了"。
// 只有渲染才知道:
//   1. 弹出框的 delegate 认不认 ForegroundRole(认了才会真的灰)
//   2. ★ "(未实现)" 里的中文会不会画成豆腐块 —— 字体没有 CJK 字形时,
//      字符串断言照样全绿,屏幕上却是 "S7(□□□)"
//
// 【不注册进 ctest】—— 里面有 tooltip 那条断言,依赖 QTipLabel 的显示延迟,
// 轮询 1.5 秒还不行就判红。本项目已经有 tst_Trend 偶发红的前车之鉴,不再加。
// 字体字形那条倒是确定性的,但为它单开一个 ctest 目标目前不值得。
//
// 用法:build_ninja\tst_ProtocolComboShot.exe
//      PNG 落在 build_ninja\shots\
//
// ★ 探针本身的两处错,别再犯(2026-10-10 实踩):
//   1. 【别用 QTest::mouseMove 触发 tooltip】Qt 的 ToolTip 事件是事件派发器
//      在鼠标停住不动之后合成的,mouseMove 只投递移动事件,压根不产生它。
//   2. 【tooltip 窗口的类名是 QTipLabel,不是 QToolTip】Qt 6 里 QToolTip
//      只是 QObject 单例,不是窗口。按 QToolTip 找永远找不到。
//   这两条叠在一起,让我一度判"tooltip 是死数据",并为一个不存在的问题
//   加了事件过滤器 —— 是负向对照(把过滤器摘掉再测)才把它救回来的。
//   ★ 只验"加了修复之后是好的",验不出"这个修复根本没必要"。
// ============================================================================

#include <QtTest>
#include <QComboBox>
#include <QAbstractItemView>
#include <QUiLoader>
#include <QFontMetrics>
#include <QWidget>
#include <QPixmap>
#include <QToolTip>
#include <QDir>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QDebug>

#include "drivers/ProtocolAvailabilityUi.h"
#include "drivers/DriverFactory.h"

using namespace qwhmi::drivers;

class tst_ProtocolComboShot : public QObject
{
    Q_OBJECT

private slots:
    void renderPopup();
    void measureStretchLastSection();
};

// 量两遍:【改之前】和【改之后】。
//
// ★ 只量改之前那遍没有意义 —— 那只能证明"原来是坏的",
//   证明不了"改完是好的"。两边都量,中间的差才是结论。
//   两条 QVERIFY 一起把两头都钉住:改完必须放得下,改回去必须放不下。
//
// 复刻的是 hmi.cpp:417-419 / AlarmListControl.cpp:83-85 的那份配置
// (5 列:时间/报警名/等级/值/状态)。不是复制生产逻辑 ——
// 生产逻辑只有那几行,抄过来量的是 Qt 自己的默认行为。
void tst_ProtocolComboShot::measureStretchLastSection()
{
    auto run = [](bool fixed, const char* label) {
        QWidget host;
        host.resize(900, 400);

        auto* table = new QTableWidget(&host);
        table->setColumnCount(5);
        table->setHorizontalHeaderLabels(
            QStringList() << QStringLiteral("时间") << QStringLiteral("报警名")
                          << QStringLiteral("等级") << QStringLiteral("值")
                          << QStringLiteral("状态"));
        if (fixed) {
            // ★ 2026-10-10 改完的配置,与 hmi.cpp / AlarmListControl.cpp 一致
            table->horizontalHeader()->setSectionResizeMode(
                QHeaderView::ResizeToContents);
            table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
            table->horizontalHeader()->setStretchLastSection(false);
        } else {
            table->horizontalHeader()->setStretchLastSection(true);
        }

        auto* lay = new QVBoxLayout(&host);
        lay->addWidget(table);
        host.show();
        QTest::qWaitForWindowExposed(&host);

        table->setRowCount(1);
        table->setItem(0, 0, new QTableWidgetItem(QStringLiteral("14:23:05")));
        table->setItem(0, 1, new QTableWidgetItem(
            QStringLiteral("3#锅炉给水温度高高报警")));
        table->setItem(0, 2, new QTableWidgetItem(QStringLiteral("高高")));
        table->setItem(0, 3, new QTableWidgetItem(QStringLiteral("87.30")));
        table->setItem(0, 4, new QTableWidgetItem(QStringLiteral("未确认")));

        // Qt6 的 QHeaderView 没有 text(),得从 model 的 headerData 取
        auto colName = [&](int c) {
            return table->model()
                ->headerData(c, Qt::Horizontal, Qt::DisplayRole)
                .toString();
        };

        qInfo().noquote() << "=== " << label << " ===";
        qInfo().noquote() << "表格视口宽度:" << table->viewport()->width();
        int total = 0;
        for (int c = 0; c < table->columnCount(); ++c) {
            const int w = table->columnWidth(c);
            total += w;
            qInfo().noquote()
                << QStringLiteral("  列[%1] %2 = %3 px")
                       .arg(c)
                       .arg(colName(c))
                       .arg(w);
        }
        qInfo().noquote() << "  合计:" << total;

        const int nameW = table->columnWidth(1);
        const int nameNeed = table->fontMetrics().horizontalAdvance(
            QStringLiteral("3#锅炉给水温度高高报警"));
        const int lastW = table->columnWidth(4);
        const int lastNeed = table->fontMetrics().horizontalAdvance(
            QStringLiteral("未确认"));
        qInfo().noquote()
            << QStringLiteral("  报警名列 %1 px / 需要 %2 px -> %3")
                   .arg(nameW)
                   .arg(nameNeed)
                   .arg(nameW >= nameNeed ? QStringLiteral("够")
                                          : QStringLiteral("★放不下,截断★"));
        qInfo().noquote()
            << QStringLiteral("  状态列 %1 px / 内容只要 %2 px -> 空掉 %3 px")
                   .arg(lastW)
                   .arg(lastNeed)
                   .arg(lastW - lastNeed);
        return nameW >= nameNeed;
    };

    const bool oldOk =
        run(false, "改之前:stretchLastSection(true),无 ResizeToContents");
    const bool newOk = run(
        true, "改之后:ResizeToContents + 拉伸报警名 + stretchLastSection(false)");

    qInfo().noquote()
        << QStringLiteral("★ 结论:改之前放得下=%1,改之后放得下=%2")
               .arg(oldOk ? QStringLiteral("是") : QStringLiteral("否"))
               .arg(newOk ? QStringLiteral("是") : QStringLiteral("否"));

    // 这才是真断言:改完必须放得下。
    QVERIFY2(newOk, "★ 改完之后报警名仍然被截断");
    // 反向也钉一下:免得哪天有人把配置改回去,这条测试还照样绿。
    QVERIFY2(!oldOk,
             "★ 旧配置居然也放得下 —— 说明当初的问题判断错了,先查清楚再改");
}

void tst_ProtocolComboShot::renderPopup()
{
    // ---- 1. 载入真的 hmi.ui(下拉项就是设计时那几个) --------------------
    const QString uiPath = QStringLiteral(HMI_SRC_DIR_STRING "/hmi.ui");
    QVERIFY2(QFile::exists(uiPath), qPrintable(QStringLiteral("找不到 %1").arg(uiPath)));

    QUiLoader loader;
    QFile f(uiPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QWidget* root = loader.load(&f);
    f.close();
    QVERIFY(root);

    auto* combo = root->findChild<QComboBox*>(QStringLiteral("protocolComboBox"));
    QVERIFY2(combo, "hmi.ui 里没有 protocolComboBox");

    qInfo() << "下拉项(置灰前):" << combo->count();
    for (int i = 0; i < combo->count(); ++i) {
        qInfo().noquote() << QStringLiteral("  [%1] %2").arg(i).arg(combo->itemText(i));
    }

    // ---- 2. 调生产函数 ------------------------------------------------
    applyProtocolAvailabilityStyling(combo);

    qInfo() << "下拉项(置灰后):";
    for (int i = 0; i < combo->count(); ++i) {
        qInfo().noquote()
            << QStringLiteral("  [%1] %2  fg=%3")
                   .arg(i)
                   .arg(combo->itemText(i))
                   .arg(combo->itemData(i, Qt::ForegroundRole)
                            .value<QBrush>().color().name());
    }

    // ---- 3. ★ 字体到底有没有这些字形 ------------------------------------
    // 这一条单元测试永远测不出来:字符串对了不代表画得出来。
    const QFont f0 = combo->font();
    const QFontMetrics fm(f0);
    qInfo().noquote() << "combo 字体:" << f0.family() << f0.pointSize() << "pt";
    const QString probe = QStringLiteral("未实现协议置灰");
    for (const QChar& ch : probe) {
        const bool has = fm.inFont(ch);
        qInfo().noquote()
            << QStringLiteral("  字形 %1(U+%2) %3")
                   .arg(ch)
                   .arg(static_cast<uint>(ch.unicode()), 4, 16, QChar('0').toUpper())
                   .arg(has ? QStringLiteral("有") : QStringLiteral("★缺★"));
        if (!has) {
            qWarning("字体缺字形,屏幕上会是豆腐块");
        }
    }

    // ---- 4. 真的弹出来、真的抓像素 ------------------------------------
    root->resize(900, 700);
    root->show();
    QVERIFY(QTest::qWaitForWindowExposed(root));

    combo->showPopup();
    QTest::qWait(350);        // 等弹出动画走完,别抓到半成品

    QWidget* popup = combo->view() ? combo->view()->window() : nullptr;
    QVERIFY2(popup, "拿不到弹出框窗口");

    const QString outDir = QStringLiteral(HMI_SHOT_DIR);
    QDir().mkpath(outDir);

    const QString closedPath = outDir + QStringLiteral("/protocol_combo_closed.png");
    const QString popupPath  = outDir + QStringLiteral("/protocol_combo_popup.png");

    QVERIFY2(combo->grab().save(closedPath), "存闭合态 PNG 失败");
    QVERIFY2(popup->grab().save(popupPath), "存弹出态 PNG 失败");

    qInfo().noquote() << "已存:" << closedPath;
    qInfo().noquote() << "已存:" << popupPath;

    // 弹出框尺寸:如果高度塌了或者只有一行,说明下拉其实没真弹出来,
    // 抓到的 PNG 会是空白 —— 那等于白验。
    qInfo().noquote() << "弹出框尺寸:" << popup->size();

    // ---- 5. ★ tooltip 到底弹不弹得出来 ---------------------------------
    //   上面断言 itemData(Qt::ToolTipRole) 有内容,那只证明【数据在】。
    //   QComboBox 的弹出框是个 QListView,Qt 默认不给它装 tooltip 的事件过滤,
    //   item 上的 tooltip 很可能【永远不会被显示】——
    //   那就是"数据对、用户看不到",和豆腐块同一类问题。
    //
    //   ★ 这里【不能】用 QTest::mouseMove 来触发:Qt 的 ToolTip 事件是由
    //     事件派发器在"鼠标停住不动"之后合成的,mouseMove 只投递移动事件,
    //     根本不会产生 ToolTip。第一版就是这么写的,结果无论有没有过滤器
    //     都是"否",差点把结论下反。
    //     直接 sendEvent 发 QHelpEvent 才是确定性的做法。
    {
        auto* view = combo->view();
        const QModelIndex idx1 = view->model()->index(1, 0);
        QVERIFY(idx1.isValid());
        const QRect cell = view->visualRect(idx1);
        const QPoint local = cell.center();
        const QPoint globalPos = view->viewport()->mapToGlobal(local);

        QHelpEvent help(QEvent::ToolTip, local, globalPos);
        QApplication::sendEvent(view->viewport(), &help);
        QTest::qWait(150);

        QWidget* tip = nullptr;
        // ★ Qt 6 里 tooltip 真正那个窗口类叫 QTipLabel,
        //   QToolTip 只是个 QObject 单例,【不是窗口】。
        //   第一版探针按 "QToolTip" 找,自然永远找不到 —— 结论整个是错的。
        auto findTip = []() -> QWidget* {
            QWidget* found = nullptr;
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (w->metaObject()->className() == QLatin1String("QTipLabel")) {
                    found = w;
                }
            }
            return found;
        };
        tip = findTip();
        // Qt 的 tooltip 有显示延迟,showText 不是同步出窗口的
        for (int extra = 0; extra < 10 && !(tip && tip->isVisible()); ++extra) {
            QTest::qWait(150);
            tip = findTip();
        }
        const bool shown = tip && tip->isVisible();
        qInfo().noquote()
            << "★ 发 ToolTip 事件,tooltip 是否显示:"
            << (shown ? QStringLiteral("是") : QStringLiteral("★否★"));
        if (shown && tip) {
            qInfo().noquote() << "  tooltip 内容:" << tip->property("text");
        }
        QVERIFY2(shown, "★ tooltip 弹不出来 —— item 说明对用户不可见");

        //   ★ 这条断言现在【是通过】的 —— 原代码本来就能弹。
    //     第一版探针判它"坏",是因为探针本身两处都写错了:
    //       1. 用 QTest::mouseMove 触发 —— Qt 的 ToolTip 事件是事件派发器
    //          在"鼠标停住"之后合成的,mouseMove 根本不产生该事件。
    //       2. 找窗口类名 "QToolTip" —— Qt 6 里 tooltip 那个窗口叫 QTipLabel,
    //          QToolTip 只是 QObject 单例,压根不是窗口。
    //     两处都错,结论整个反了,还差点为它加一个不需要的事件过滤器。
    //     是"摘掉过滤器照样弹"这个负向对照把它救回来的 ——
    //     只验"有修复时是好的",验不出"修复根本没必要"。
    }

    combo->hidePopup();
    delete root;
}

QTEST_MAIN(tst_ProtocolComboShot)
#include "tst_ProtocolComboShot.moc"