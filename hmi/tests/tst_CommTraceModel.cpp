// ============================================================================
// tst_CommTraceModel —— 通讯帧监视的【数据层】
//
// 【这个测试在防什么】
//   通讯帧监视器(调试区第三页「通讯监视」)是 2026-10-20 新加的功能。
//   它的数据层是 CommTraceModel —— 一个 QAbstractListModel,
//   职责只有两件:① 存最近 N 帧 ② 算本次运行的统计。
//
//   看着简单,但它身上有三个【不写下来就一定会被改坏】的决定:
//
//   ① 【淘汰而不是拒绝】超过行数上限时,淘汰【最旧的】那一条,不是拒绝新的。
//      拒绝的话监视器会在跑满 500 帧之后【从此不再更新】——
//      而它不报错、不弹窗,表现是"监视器卡在第 500 帧不动了"。
//      现场看到的就是"监视器坏了",实际上是内存上限在起作用。
//
//   ② 【清空只清列表,不清统计】
//      错误帧数是【本次运行】的累计量,帧列表只是一个【滚动窗口】。
//      抹掉统计的话,一个持续报错的链路会看起来像"刚启动"——
//      这正是"只看眼前"最容易骗人的地方。
//
//   ③ 【暂停时统计也冻结】
//      如果暂停只挡列表不挡统计,界面会显示"已暂停采集"而数字还在涨,
//      用户会以为暂停没生效,或者以为统计坏了。
//
// 【为什么它能脱开界面单测】
//   CommTraceModel 纯 QtCore,一行界面代码都没有(数据层不许有 QMessageBox
//   是本仓库的硬规矩,见 AlarmConfigDialog / VariableConfigDialog)。
//   所以"行数超限之后最旧的那一行真的被淘汰了吗"这种断言写得出来 ——
//   用 QTableWidget 直接塞行的话,行号偏移是最容易写错的地方。
//
// 【诚实的边界】
//   ✓ 能抓:淘汰顺序 / 暂停语义 / 清空语义 / 统计算术 /
//           "-1 显示成短横而不是 0" / 时间戳补齐 / 上限被改小之后立刻收缩
//   ✗ 不能抓:"好不好看"、行着色(还没做)、面板高度(那是 tst_CommTracePanel 的事)
//
// 【负向对照 —— 六轮全部实跑,数字是跑出来的不是推出来的】
//   绿基线:22 passed / 0 failed
//   ① 把淘汰算式改成 dropOldest(size - maxRows)(少减 1)
//        → 红 2:oldestFrameIsEvictedWhenOverLimit(行数 4 ≠ 3)
//                maxRows_zero_isClampedToOne
//          绿 20
//   ② clear() 里把统计也清掉
//        → 红 1:clear_keepsTheCounters
//          绿 21
//   ③ 暂停时只挡列表不挡统计(把 collecting_ 的判断挪到统计之后)
//        → 红 2:paused_rejectsFramesAndFreezesTheCounters
//                statsChanged_isEmittedOnEveryAcceptedFrame
//          绿 20
//   ④ 把 elapsedMs < 0 显示成 0(而不是短横)
//        → 红 1:elapsedMinusOne_isShownAsDashNotZero
//          绿 21
//   ⑤ 让 elapsedMs = -1 的帧也参与平均
//        → 红 2:clear_keepsTheCounters(最小值被 -1 污染)
//                framesWithoutElapsed_doNotPolluteTheAverage
//          绿 20
//   ⑥ 把 columnCount() 返回 1(等于没覆写)—— 这一轮红得最多,
//      而且抓的是一个【真的会在现场发作的 bug】:
//        → 红 7:rowContent_isExactlyWhatWasPutIn
//                columnCount_isFive_notTheListDefaultOne
//                oldestFrameIsEvictedWhenOverLimit
//                shrinkingMaxRows_dropsTheOldestRightAway
//                maxRows_zero_isClampedToOne
//                elapsedMinusOne_isShownAsDashNotZero
//                elapsedZero_isAValidSample
//          绿 15
//
// 【一个必须记下来的坑:还原之后 ninja 说 "no work to do"】
//   第一遍跑完负向对照,还原代码后重建,输出是 "ninja: no work to do",
//   跑出来的还是红的 —— 那不是还原失败,是【跑的根本不是新代码】:
//   Copy-Item 会把 LastWriteTime 一起拷回去,于是 ninja 认为 .obj 比 .cpp 新,
//   根本不重编,exe 里还是改坏的那份。
//   还原之后必须强制改时间戳再重建,否则"还原后是绿的"这句话是假的。
//   上面 ①~⑤ 的"绿 N"是当轮跑出来的红/绿;还原后的最终绿基线 22/0
//   是强制重建之后重新确认过的。
//
// 关联:src/comm/CommTraceModel.{h,cpp} / src/comm/CommFrame.h
//      / ModbusMaster.h(frameTraced 信号)
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>

#include "comm/CommTraceModel.h"

using qwhmi::comm::CommTraceModel;
using qwhmi::comm::FrameDir;
using qwhmi::comm::FrameRecord;

// 造一帧的省事写法:方向 + 摘要 + 字节 + 耗时
static FrameRecord mk(FrameDir dir, const QString& summary,
                      const QByteArray& raw = QByteArray(), qint64 elapsed = -1) {
    FrameRecord r;
    r.dir       = dir;
    r.summary   = summary;
    r.raw       = raw;
    r.elapsedMs = elapsed;
    return r;
}

class tst_CommTraceModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ---- 基本形状 ----
    void appendFrame_addsOneRow();
    void rowContent_isExactlyWhatWasPutIn();
    void headerNames_areStable();

    // ★★ 列数。QAbstractListModel 的默认列数是 1 ——
    //   不覆写 columnCount() 的话,index(row, 1..4) 全是无效索引,
    //   data() 返回空 QVariant,而 QTableView 只会画出【一列】。
    //   这条是本文件第一次跑就抓出来的真 bug(6 条红),见文件头。
    void columnCount_isFive_notTheListDefaultOne();
    void everyColumn_hasAValidIndex();

    // ---- ① 淘汰 ----
    void oldestFrameIsEvictedWhenOverLimit();
    void shrinkingMaxRows_dropsTheOldestRightAway();
    void maxRows_zero_isClampedToOne();

    // ---- ② 清空 ----
    void clear_keepsTheCounters();

    // ---- ③ 暂停 ----
    void paused_rejectsFramesAndFreezesTheCounters();

    // ---- 统计算术 ----
    void counters_areCountedByDirection();
    void bytes_areCountedByDirection();
    void elapsedStats_useOnlyRealSamples();
    void framesWithoutElapsed_doNotPolluteTheAverage();
    void elapsedMinusOne_isShownAsDashNotZero();
    void elapsedZero_isAValidSample();
    void statsText_saysNoSampleWhenThereIsNone();
    void statsText_marksPaused();

    // ---- 时间戳 ----
    void invalidTimestamp_isStampedWithNow();

    // ---- 信号 ----
    void statsChanged_isEmittedOnEveryAcceptedFrame();
};

void tst_CommTraceModel::initTestCase() {
    // ★ 这个模型不需要界面,所以用 QTEST_GUILESS_MAIN(QCoreApplication) ——
    //   也就意味着这个测试在【没有显示设备】的机器上也能跑。
    //   (QTEST_MAIN 会建 QApplication,那就得靠 offscreen 平台插件了。)
    //
    // 地基:默认行数上限必须是 500。
    //   轮询 100ms 一帧 → 一小时 3.6 万帧,一夜 30 万帧。
    //   上限太小用户看不到刚才发生了什么,太大则吃光内存。
    //   这个数字写死在头文件里,这里钉住它。
    CommTraceModel m;
    QCOMPARE(m.maxRows(), 500);
    QVERIFY(m.isCollecting());
}

// -----------------------------------------------------------------------

void tst_CommTraceModel::appendFrame_addsOneRow() {
    CommTraceModel m;
    QCOMPARE(m.rowCount(), 0);

    QVERIFY(m.appendFrame(mk(FrameDir::Tx, QStringLiteral("FC03 读"))));
    QCOMPARE(m.rowCount(), 1);

    QVERIFY(m.appendFrame(mk(FrameDir::Rx, QStringLiteral("FC03 响应"))));
    QCOMPARE(m.rowCount(), 2);
}

void tst_CommTraceModel::rowContent_isExactlyWhatWasPutIn() {
    CommTraceModel m;
    const QByteArray raw = QByteArray::fromHex("010300000002");
    m.appendFrame(mk(FrameDir::Rx, QStringLiteral("FC03 读保持寄存器"), raw, 7));

    const QModelIndex idx = m.index(0, 0);
    QVERIFY(idx.isValid());

    QCOMPARE(m.data(m.index(0, CommTraceModel::ColDir)).toString(),
             QString::fromUtf8("接收"));
    QCOMPARE(m.data(m.index(0, CommTraceModel::ColSummary)).toString(),
             QStringLiteral("FC03 读保持寄存器"));
    QCOMPARE(m.data(m.index(0, CommTraceModel::ColElapsed)).toString(),
             QStringLiteral("7"));
    // ★ 原始字节的写法必须和 ModbusMaster 打日志时一样(大写、空格分隔)——
    //   否则用户拿着界面上看到的那串去日志里搜不到。
    QCOMPARE(m.data(m.index(0, CommTraceModel::ColRaw)).toString(),
             QStringLiteral("01 03 00 00 00 02"));

    // 时间列:HH:mm:ss.zzz
    const QString t = m.data(m.index(0, CommTraceModel::ColTime)).toString();
    QVERIFY2(QRegularExpression(QStringLiteral("^\\d{2}:\\d{2}:\\d{2}\\.\\d{3}$"))
                 .match(t).hasMatch(),
             qPrintable(QStringLiteral("时间列的格式不对:%1").arg(t)));

    // 附加角色:方向 / 耗时 / 原始字节
    // ★ 角色是【行、列之外的第三个维度】—— index(row, column) 里的 column
    //   只能是 0..ColumnCount-1。把角色当列传进去会得到一个【无效索引】,
    //   data() 对无效索引直接返回空 QVariant —— 那这条断言会恒红,
    //   看起来在测什么,其实什么都没测到。
    QCOMPARE(m.data(idx, CommTraceModel::DirRole).toInt(), int(FrameDir::Rx));
    QCOMPARE(m.data(idx, CommTraceModel::ElapsedRole).toLongLong(), 7LL);
    QCOMPARE(m.data(idx, CommTraceModel::RawRole).toByteArray(), raw);
}

void tst_CommTraceModel::headerNames_areStable() {    CommTraceModel m;
    QCOMPARE(m.headerData(CommTraceModel::ColTime, Qt::Horizontal).toString(),
             QStringLiteral("时间"));
    QCOMPARE(m.headerData(CommTraceModel::ColDir, Qt::Horizontal).toString(),
             QStringLiteral("方向"));
    QCOMPARE(m.headerData(CommTraceModel::ColSummary, Qt::Horizontal).toString(),
             QStringLiteral("摘要"));
    QCOMPARE(m.headerData(CommTraceModel::ColElapsed, Qt::Horizontal).toString(),
             QStringLiteral("耗时(ms)"));
    QCOMPARE(m.headerData(CommTraceModel::ColRaw, Qt::Horizontal).toString(),
             QStringLiteral("原始字节"));
}

// ---- 列数(2026-10-20 第一次跑就抓出来的真 bug)--------------------------

void tst_CommTraceModel::columnCount_isFive_notTheListDefaultOne() {
    CommTraceModel m;
    // QAbstractListModel 的默认列数是 1(它是给单列列表用的)。
    // 本表有 5 列,必须覆写 columnCount()。
    // ★ 不覆写的后果不是"少一列"这么轻:
    //   QAbstractListModel::index() 走 hasIndex(),后者查 column < columnCount(),
    //   于是 index(row, 1..4) 全部【无效】,data() 对无效索引返回空 QVariant。
    //   界面上看到的是"表格里只有时间,别的列什么都没有"。
    QCOMPARE(m.columnCount(), 5);
}

void tst_CommTraceModel::everyColumn_hasAValidIndex() {
    CommTraceModel m;
    m.appendFrame(mk(FrameDir::Rx, QStringLiteral("一帧"), QByteArray(4, '\x11'), 3));
    // 每一列都得能拿到有效索引。
    //   上一条钉的是"列数是 5",这一条钉的是"5 列真的都能取到" ——
    //   两者是不同的坏法:列数对了但 index() 走错了路径,一样是空数据。
    for (int c = 0; c < m.columnCount(); ++c) {
        const QModelIndex idx = m.index(0, c);
        QVERIFY2(idx.isValid(),
                 qPrintable(QStringLiteral("第 %1 列拿不到有效索引 —— "
                     "index() 走 hasIndex() 时会查 column < columnCount(),"
                     "列数没覆写的话这里全是无效索引").arg(c)));
        QVERIFY2(!m.data(idx).toString().isEmpty(),
                 qPrintable(QStringLiteral("第 %1 列的显示内容是空的").arg(c)));
    }
}

// ---- ① 淘汰 ------------------------------------------------------------

void tst_CommTraceModel::oldestFrameIsEvictedWhenOverLimit() {
    CommTraceModel m;
    m.setMaxRows(3);
    for (int i = 1; i <= 5; ++i) {
        QVERIFY(m.appendFrame(mk(FrameDir::Tx, QStringLiteral("帧%1").arg(i))));
    }

    // 行数必须【严格】等于上限 —— 淘汰算式少减 1 的话这里就是 4。
    QCOMPARE(m.rowCount(), 3);

    // 而且淘汰的是【最旧的】:留着的必须是最后三帧(帧3/帧4/帧5)。
    QCOMPARE(m.data(m.index(0, CommTraceModel::ColSummary)).toString(),
             QStringLiteral("帧3"));
    QCOMPARE(m.data(m.index(1, CommTraceModel::ColSummary)).toString(),
             QStringLiteral("帧4"));
    QCOMPARE(m.data(m.index(2, CommTraceModel::ColSummary)).toString(),
             QStringLiteral("帧5"));

    // ★ 统计【不受】淘汰影响:淘汰的是"列表窗口",不是"发生过的事"。
    //   5 帧发出去就是 5 帧,哪怕列表里只剩 3 行。
    QCOMPARE(m.txCount(), 5);

    // 负向对照 ①(实跑):把 dropOldest(size - maxRows + 1) 改成 (size - maxRows)
    //   → 红 2:oldestFrameIsEvictedWhenOverLimit(行数 4 ≠ 3)
    //           maxRows_zero_isClampedToOne
}

void tst_CommTraceModel::shrinkingMaxRows_dropsTheOldestRightAway() {
    CommTraceModel m;
    m.setMaxRows(10);
    for (int i = 1; i <= 8; ++i) {
        m.appendFrame(mk(FrameDir::Tx, QStringLiteral("帧%1").arg(i)));
    }
    QCOMPARE(m.rowCount(), 8);

    m.setMaxRows(3);
    QCOMPARE(m.rowCount(), 3);
    QCOMPARE(m.data(m.index(0, CommTraceModel::ColSummary)).toString(),
             QStringLiteral("帧6"));
    QCOMPARE(m.txCount(), 8);   // 统计照旧
}

void tst_CommTraceModel::maxRows_zero_isClampedToOne() {
    CommTraceModel m;
    m.setMaxRows(0);
    // 0 会让 appendFrame 每次进来先把全部行删掉再插一行 ——
    // 界面上看起来就是"永远只有一行而且在闪"。所以下限是 1。
    QVERIFY(m.maxRows() >= 1);
    m.appendFrame(mk(FrameDir::Tx, QStringLiteral("a")));
    m.appendFrame(mk(FrameDir::Tx, QStringLiteral("b")));
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(m.data(m.index(0, CommTraceModel::ColSummary)).toString(),
             QStringLiteral("b"));
}

// ---- ② 清空 ------------------------------------------------------------

void tst_CommTraceModel::clear_keepsTheCounters() {
    CommTraceModel m;
    m.appendFrame(mk(FrameDir::Tx, QStringLiteral("发"), QByteArray(4, '\x01')));
    m.appendFrame(mk(FrameDir::Rx, QStringLiteral("收"), QByteArray(4, '\x02'), 5));
    m.appendFrame(mk(FrameDir::Error, QStringLiteral("错")));

    m.clear();

    // 列表清了
    QCOMPARE(m.rowCount(), 0);
    // 统计【没】清 —— 这是这一条的全部意义。
    QCOMPARE(m.txCount(), 1);
    QCOMPARE(m.rxCount(), 1);
    QCOMPARE(m.errorCount(), 1);
    QCOMPARE(m.bytesOut(), 4LL);
    QCOMPARE(m.bytesIn(), 4LL);
    QCOMPARE(m.minElapsedMs(), 5LL);
    QCOMPARE(m.avgElapsedMs(), 5.0);

    // 负向对照 ②(实跑):clear() 里把统计也清掉
    //   → 红 1:clear_keepsTheCounters
}

// ---- ③ 暂停 ------------------------------------------------------------

void tst_CommTraceModel::paused_rejectsFramesAndFreezesTheCounters() {
    CommTraceModel m;
    QVERIFY(m.isCollecting());

    QVERIFY(m.appendFrame(mk(FrameDir::Tx, QStringLiteral("发1"))));
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(m.txCount(), 1);

    m.setCollecting(false);
    QVERIFY(!m.isCollecting());

    // 暂停期间:不收帧,而且【要告诉调用方没收】(返回值 false)。
    //   静默丢弃的话,界面会以为"程序没在通信"。
    QVERIFY(!m.appendFrame(mk(FrameDir::Tx, QStringLiteral("发2"))));
    QCOMPARE(m.rowCount(), 1);
    // ★ 统计也必须冻住 —— 否则界面显示"已暂停采集"而数字还在涨。
    QCOMPARE(m.txCount(), 1);
    QVERIFY2(m.statsText().contains(QStringLiteral("已暂停采集")),
             qPrintable(QStringLiteral("暂停时统计标签上必须写明'已暂停采集',"
                                       "否则用户以为暂停没生效:") + m.statsText()));

    // 继续之后恢复正常
    m.setCollecting(true);
    QVERIFY(m.appendFrame(mk(FrameDir::Tx, QStringLiteral("发3"))));
    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(m.txCount(), 2);

    // 负向对照 ③(实跑):暂停时只挡列表不挡统计
    //   → 红 2:paused_rejectsFramesAndFreezesTheCounters
    //           statsChanged_isEmittedOnEveryAcceptedFrame
}

// ---- 统计算术 ----------------------------------------------------------

void tst_CommTraceModel::counters_areCountedByDirection() {
    CommTraceModel m;
    m.appendFrame(mk(FrameDir::Tx,    QStringLiteral("a")));
    m.appendFrame(mk(FrameDir::Tx,    QStringLiteral("b")));
    m.appendFrame(mk(FrameDir::Rx,    QStringLiteral("c")));
    m.appendFrame(mk(FrameDir::Error, QStringLiteral("d")));
    m.appendFrame(mk(FrameDir::Error, QStringLiteral("e")));

    QCOMPARE(m.txCount(), 2);
    QCOMPARE(m.rxCount(), 1);
    QCOMPARE(m.errorCount(), 2);
}

void tst_CommTraceModel::bytes_areCountedByDirection() {
    CommTraceModel m;
    m.appendFrame(mk(FrameDir::Tx,    QStringLiteral("a"), QByteArray(12, '\x01')));
    m.appendFrame(mk(FrameDir::Rx,    QStringLiteral("b"), QByteArray(9,  '\x02')));
    // 异常响应:它【是】收到的字节,只是被判成了错误。
    m.appendFrame(mk(FrameDir::Error, QStringLiteral("c"), QByteArray(5,  '\x03')));
    // 超时:一个字节都没收到。
    m.appendFrame(mk(FrameDir::Error, QStringLiteral("d"), QByteArray()));

    QCOMPARE(m.bytesOut(), 12LL);
    QCOMPARE(m.bytesIn(), 14LL);   // 9(正常响应) + 5(异常响应) + 0(超时没字节)
}

void tst_CommTraceModel::elapsedStats_useOnlyRealSamples() {
    CommTraceModel m;
    m.appendFrame(mk(FrameDir::Rx, QStringLiteral("r1"), QByteArray(), 10));
    m.appendFrame(mk(FrameDir::Rx, QStringLiteral("r2"), QByteArray(), 30));
    m.appendFrame(mk(FrameDir::Rx, QStringLiteral("r3"), QByteArray(), 20));

    QCOMPARE(m.minElapsedMs(), 10LL);
    QCOMPARE(m.maxElapsedMs(), 30LL);
    QCOMPARE(m.avgElapsedMs(), 20.0);
    QCOMPARE(m.lastElapsedMs(), 20LL);
}

void tst_CommTraceModel::framesWithoutElapsed_doNotPolluteTheAverage() {
    CommTraceModel m;
    // 发出帧的耗时是 -1(发出时还不知道要等多久)。
    m.appendFrame(mk(FrameDir::Tx, QStringLiteral("t1")));
    m.appendFrame(mk(FrameDir::Tx, QStringLiteral("t2")));
    m.appendFrame(mk(FrameDir::Rx, QStringLiteral("r1"), QByteArray(), 10));

    // ★ 只有真的有值的样本参与统计。
    //   把 -1 当 0 参与平均,三次平均会变成 3.3 —— 一个看起来像真的、
    //   其实是假的数字。比"没有样本"危险得多。
    QCOMPARE(m.avgElapsedMs(), 10.0);
    QCOMPARE(m.minElapsedMs(), 10LL);
    QCOMPARE(m.maxElapsedMs(), 10LL);
    QCOMPARE(m.lastElapsedMs(), 10LL);
}

void tst_CommTraceModel::elapsedMinusOne_isShownAsDashNotZero() {
    CommTraceModel m;
    m.appendFrame(mk(FrameDir::Tx, QStringLiteral("发出")));
    // 0 是一个【合法】的耗时(本机回环常常 0~1ms),
    // 显示成 0 会让人以为"测出来是 0ms",而真实含义是"这一行没有往返耗时"。
    QCOMPARE(m.data(m.index(0, CommTraceModel::ColElapsed)).toString(),
             QStringLiteral("-"));

    // 负向对照 ④(实跑):把 "-" 改成 "0"
    //   → 红 1:elapsedMinusOne_isShownAsDashNotZero
    // 负向对照 ⑤(实跑):让 elapsedMs = -1 的帧也参与平均
    //   → 红 2:clear_keepsTheCounters(最小值被 -1 污染)
    //           framesWithoutElapsed_doNotPolluteTheAverage
}

void tst_CommTraceModel::elapsedZero_isAValidSample() {
    CommTraceModel m;
    m.appendFrame(mk(FrameDir::Rx, QStringLiteral("回环"), QByteArray(), 0));

    // 0 必须显示成 "0",不能显示成 "-" ——
    //   否则"本机回环 0ms"和"这一行没有耗时"分不清。
    QCOMPARE(m.data(m.index(0, CommTraceModel::ColElapsed)).toString(),
             QStringLiteral("0"));
    QCOMPARE(m.minElapsedMs(), 0LL);
    QCOMPARE(m.avgElapsedMs(), 0.0);
    QVERIFY2(m.maxElapsedMs() >= 0, "0 是一次合法样本,必须有 max");
}

void tst_CommTraceModel::statsText_saysNoSampleWhenThereIsNone() {
    CommTraceModel m;
    const QString s = m.statsText();
    QVERIFY2(s.contains(QStringLiteral("尚无样本")),
             qPrintable(QStringLiteral("没有样本时要写'尚无样本',"
                                       "写 0 会被当成'实测 0ms':") + s));
    QVERIFY2(s.contains(QStringLiteral("发 0")), qPrintable(s));
}

void tst_CommTraceModel::statsText_marksPaused() {
    CommTraceModel m;
    QVERIFY(!m.statsText().contains(QStringLiteral("已暂停采集")));
    m.setCollecting(false);
    QVERIFY2(m.statsText().contains(QStringLiteral("已暂停采集")),
             qPrintable(m.statsText()));
}

// ---- 时间戳 ------------------------------------------------------------

void tst_CommTraceModel::invalidTimestamp_isStampedWithNow() {
    CommTraceModel m;
    FrameRecord r = mk(FrameDir::Tx, QStringLiteral("无时间戳的帧"));
    QVERIFY(!r.timestamp.isValid());
    m.appendFrame(r);

    const QString shown = m.data(m.index(0, CommTraceModel::ColTime)).toString();
    const QDateTime now = QDateTime::currentDateTime();
    // 同一分钟之内(测试跑得很快,跨分钟的极小概率忽略不计)
    const QString nowStr = now.toString(QStringLiteral("HH:mm"));
    QVERIFY2(shown.startsWith(nowStr),
             qPrintable(QStringLiteral("无效时间戳应该被补成当前时间,"
                                       "显示的是 %1,当前是 %2")
                            .arg(shown, nowStr)));
}

// ---- 信号 --------------------------------------------------------------

void tst_CommTraceModel::statsChanged_isEmittedOnEveryAcceptedFrame() {
    CommTraceModel m;
    QSignalSpy spy(&m, &CommTraceModel::statsChanged);

    m.appendFrame(mk(FrameDir::Tx, QStringLiteral("a")));
    QCOMPARE(spy.count(), 1);

    m.appendFrame(mk(FrameDir::Rx, QStringLiteral("b"), QByteArray(), 3));
    QCOMPARE(spy.count(), 2);

    // 暂停时不收帧 —— 但统计变了(暂停状态本身变了),所以要发一次。
    m.setCollecting(false);
    QCOMPARE(spy.count(), 3);

    // 暂停期间来的帧:【不发】statsChanged。
    //   发了也没用(统计没变),但界面会白刷新一次 —— 每秒十几次,值得省。
    m.appendFrame(mk(FrameDir::Tx, QStringLiteral("c")));
    QCOMPARE(spy.count(), 3);
}

QTEST_GUILESS_MAIN(tst_CommTraceModel)
#include "tst_CommTraceModel.moc"
