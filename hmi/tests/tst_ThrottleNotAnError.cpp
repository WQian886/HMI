// ============================================================================
// 回归:单 in-flight 限流【不得】被当成错误
//
// 关联:../ModbusMaster.cpp sendReadRequest / sendWriteRequest 的限流分支,
//      ../hmi.cpp 的 errorOccurred lambda
//
// ★ 2026-10-23 新增。这条用例钉的是一个【已经造成过 P0】的契约。
//
// ── 它防的是什么 ────────────────────────────────────────────────────────
//   Modbus 协议串行,一次只能有一个在途请求。PollScheduler::onTick 把变量
//   按地址排序后拆成多批,一个 tick 里连发多帧 —— 于是第 2 帧起必然撞上
//   ModbusMaster 的单 in-flight 限流,被拒收。
//
//   ★ 限流是【内部调度细节】,不是错误。把它 emit errorOccurred 出去,
//     在 hmi.cpp 那边被接到「改状态 Failed + 弹一个模态 QMessageBox」。
//     后果(2026-10-23 实测):任何【地址不连续】的工程,第一次轮询之后
//     永久停摆 —— 主线程卡在弹窗上,而那条消息走的是 qCDebug,
//     INFO 级别下一行都不落 ⇒ 全程零告警。
//     现场看着还特别正常:进程活着、CPU 低、内存平、统计行照常每 60 秒打印,
//     只有计数永远停在 发出=1 成功=1。
//
//   为什么现有 92 条用例没抓到:调度器层的回滚是用【假 driver】测的
//   (tst_IPollDriver.cpp / tst_PlcMultiServerE2E.cpp 都覆盖了"地址不连续"),
//   假 driver 上方没有 hmi.cpp。**没有任何一条用例穿过 errorOccurred 这根线。**
//   每一层单独测都对,接起来就死。
//
// ── ★ 2026-10-26(BUGFIX-E1):写侧的【契约变了】────────────────────────────
//   原来:写撞上在途槽 → 直接拒收(返回 nullptr)。
//   现在:写撞上在途槽 → 【排队】(返回非空,槽一空就发出去)。
//   理由见 writeThrottle_doesNotEmitErrorOccurred() 里的长注释
//   (实测:604 次写全被拒、0 帧到从站,而读全成功;
//    决定成败的是两个 100ms 节拍的启动相位差,一辈子不变)。
//
//   ★ 本文件【要钉的那件事一个字都没变】:
//     「限流是内部调度细节,不是错误」—— 撞车不许 emit errorOccurred。
//   读侧【完全没变】:读撞车还是拒收(丢一帧没有代价,下一秒会重发)。
//   写侧变了,但【不发 errorOccurred】这件事两边都必须成立。
//
//   ★ 写排队的完整语义在 tst_ModbusWriteQueue.cpp 里(那才是它的地盘):
//     真的发出去 / 值不串 / 断线作废 / 重连作废 / 校验在排队前面。
//
// ── 为什么这个文件有两半 ────────────────────────────────────────────────
//   ① 协议层:用【真的 ModbusMaster】,真连一个自己写的最小从站,
//      断言限流时【不发 errorOccurred】。这是行为断言,不是文本匹配。
//   ② UI 层:弹窗没法在进程内断言(QMessageBox 是模态阻塞的,弹出来
//      测试自己就卡住了 —— 跟现场同一个坑)。所以用【读源码文本】的静态守卫,
//      断言 hmi.cpp 的 errorOccurred lambda 里有 modalAlertsEnabled() 守卫。
//      这个套路在本仓库已有先例:tst_CommTracePanel.cpp:471 就是这么钉
//      "hmi.cpp 里有没有 connect ModbusMaster::frameTraced" 的。
//
// ── 【静态守卫的局限】,必须说在前面 ────────────────────────────────────
//   第二部分只能证明"源码里有这么一行字",不能证明"运行时行为正确"。
//   它挡的是"有人把守卫那一行删了"这种最常见的回归;
//   它挡不住"有人改了写法但没改行为"这类更刁钻的情况。
//   真正的运行期证据在 scripts/repro_noncontiguous_stall.ps1 ——
//   那个脚本会枚举进程的全部顶层窗口,亲眼看有没有「Modbus 错误」弹窗。
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QModbusDataUnit>
#include <QFile>
#include <QDir>

#include "ModbusMaster.h"

// ★ 用编译期宏拿仓库根,不用"从当前目录往上找" ——
//   上层目录会随测试运行方式变(IDE 里跑 / ctest 里跑 / 从别处跑),
//   宏是 CMake 算出来的,不会变。
//   同一个宏 tst_CommTracePanel / tst_ModbusPanel / tst_Theme 都在用。
#ifndef HMI_SRC_DIR_STRING
#error "HMI_SRC_DIR_STRING 没定义 —— 这个目标不能用 add_hmi_test,见 CMakeLists.txt 里的登记处"
#endif
static const QString kHmiSrcDir = QString::fromLatin1(HMI_SRC_DIR_STRING);

// ============================================================================
// 最小 Modbus TCP 从站 —— 只做一件事:收下请求,但【不回包】。
//
// 【为什么装死】限流分支的前提是"上一帧还没完成"。
// 如果从站正常回包,上一帧瞬间就完成了,限流分支根本走不到,
// 也就测不出它到底 emit 了什么。
// 装死 = 让 pending_ 一直非空 = 限流分支必然被走到。
// ============================================================================
class SilentSlave : public QTcpServer {
    Q_OBJECT
public:
    explicit SilentSlave(QObject* parent = nullptr) : QTcpServer(parent) {
        connect(this, &QTcpServer::newConnection, this, &SilentSlave::onNewConnection);
    }
    int requestCount = 0;

protected:
    void onNewConnection() {
        while (QTcpSocket* s = nextPendingConnection()) {
            connect(s, &QTcpSocket::readyRead, this, [this, s] {
                s->readAll();                 // 收下就扔,绝不回包
                ++requestCount;
            });
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        }
    }
};

class tst_ThrottleNotAnError : public QObject {
    Q_OBJECT
private slots:
    // ── ① 协议层:限流不得 emit errorOccurred ──
    void readThrottle_doesNotEmitErrorOccurred();
    void writeThrottle_doesNotEmitErrorOccurred();
    void readThrottle_stillReturnsNullptr_andDoesNotReply();   // 读侧行为没变,只是不报错了

    // ── ② UI 层:弹窗必须受守卫约束(静态) ──
    void ui_errorOccurredHandler_isGuardedByModalAlerts();
    void ui_machineTriggeredDialogs_areGuarded();

private:
    QString readSource(const QString& relPath) const;
};

// ============================================================================
// 源码解析辅助
//
// ── 为什么需要这些 ────────────────────────────────────────────────────────
//   这个文件要断言的是"某个弹窗被某个守卫包着"。
//   【最朴素的写法是"附近 N 字符里有没有 modalAlertsEnabled()"】——
//   我第一版就是这么写的,两处都栽了:
//
//     ① 窗口开太大(3000 字符):里面混进了 hmi.cpp 【别处】的守卫,
//        于是【把这个弹窗的守卫整个删掉,用例照样绿】。
//        负向对照第一次跑就抓到了 —— 绿的测试不等于有效的测试。
//     ② 注释里到处写着 modalAlertsEnabled() ——
//        守卫的说明文字本身就含这个词。于是"文本里有这个词"根本
//        不能证明"守卫在生效"。
//
//   所以这里做三件事:
//     · 把注释抹掉(但保留字符位置,偏移量不变)
//     · 另做一份"字符串内容也抹掉"的版本,专门用来配对花括号
//       (免得字符串里的 '{' 把块结构搞乱)
//     · 用【括号配对】找到"包含目标位置的最内层块",再从那一条语句的
//       开头取到目标位置 —— 这才是"这个弹窗所在的语句上下文"
//
//   【局限,必须说在前面】
//   这是文本层的结构分析,不是编译器。极端情况下
//   "if (modalAlertsEnabled()) { doSomething(); }" 紧跟着一个没守卫的弹窗,
//   本守卫会误判为"有守卫"。
//   它挡的是"把守卫整段删掉"这种最常见、最致命的回归 ——
//   而那正是 2026-10-23 这个 P0 的实际成因。
// ============================================================================
struct SrcPrep {
    QString clean;   // 注释已抹,字符串内容保留(给人看的)
    QString mask;    // 注释已抹,字符串内容也抹(给机器看的:配括号、找标识符)
};

static SrcPrep prepSource(const QString& code) {
    SrcPrep r;
    r.clean.resize(code.size());
    r.mask.resize(code.size());
    const int n = code.size();
    int i = 0;
    while (i < n) {
        const QChar c = code.at(i);
        if (c == QLatin1Char('/') && i + 1 < n && code.at(i + 1) == QLatin1Char('/')) {
            // 行注释:整行抹成空格(换行保留)
            while (i < n && code.at(i) != QLatin1Char('\n')) {
                r.clean[i] = QLatin1Char(' '); r.mask[i] = QLatin1Char(' '); ++i;
            }
        } else if (c == QLatin1Char('/') && i + 1 < n && code.at(i + 1) == QLatin1Char('*')) {
            // 块注释
            r.clean[i] = QLatin1Char(' '); r.mask[i] = QLatin1Char(' '); ++i;
            r.clean[i] = QLatin1Char(' '); r.mask[i] = QLatin1Char(' '); ++i;
            while (i < n) {
                if (code.at(i) == QLatin1Char('*') && i + 1 < n && code.at(i + 1) == QLatin1Char('/')) {
                    r.clean[i] = QLatin1Char(' '); r.mask[i] = QLatin1Char(' ');
                    r.clean[i + 1] = QLatin1Char(' '); r.mask[i + 1] = QLatin1Char(' ');
                    i += 2;
                    break;
                }
                r.clean[i] = (code.at(i) == QLatin1Char('\n')) ? QLatin1Char('\n') : QLatin1Char(' ');
                r.mask[i]  = r.clean[i];
                ++i;
            }
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const QChar quote = c;
            r.clean[i] = c; r.mask[i] = c; ++i;
            while (i < n) {
                const QChar d = code.at(i);
                if (d == QLatin1Char('\\') && i + 1 < n) {
                    r.clean[i] = d; r.mask[i] = QLatin1Char(' '); ++i;
                    r.clean[i] = code.at(i); r.mask[i] = QLatin1Char(' '); ++i;
                    continue;
                }
                if (d == quote) { r.clean[i] = d; r.mask[i] = d; ++i; break; }
                r.clean[i] = d;
                r.mask[i]  = (d == QLatin1Char('\n')) ? QLatin1Char('\n') : QLatin1Char(' ');
                ++i;
            }
        } else {
            r.clean[i] = c; r.mask[i] = c; ++i;
        }
    }
    return r;
}

// 找到"包含位置 p 的最内层 { } 块"的左括号位置;不在任何块里返回 -1
static int innermostBlockOpen(const QString& mask, int p) {
    QVector<int> stack;
    QVector<QPair<int, int>> pairs;
    for (int i = 0; i < mask.size(); ++i) {
        if (mask.at(i) == QLatin1Char('{')) {
            stack.push_back(i);
        } else if (mask.at(i) == QLatin1Char('}')) {
            if (!stack.isEmpty()) pairs.append(qMakePair(stack.takeLast(), i));
        }
    }
    int best = -1;
    for (const auto& pr : pairs) {
        if (pr.first < p && p < pr.second) {
            if (best < 0 || pr.first > best) best = pr.first;
        }
    }
    return best;
}

// ── 一次 QMessageBox 调用点 ──────────────────────────────────────────────
struct BoxSite {
    int     pos = -1;      // "QMessageBox" 这个词的位置
    QString args;          // 参数区原文(不含外层括号)
};

// 找出源码里所有 QMessageBox 调用点,连同它们的参数区
//   用 mask(字符串内容已抹)配括号 —— 免得字符串里的 '(' 把配对搞乱。
static QVector<BoxSite> findMessageBoxSites(const SrcPrep& s) {
    QVector<BoxSite> out;
    int from = 0;
    while (true) {
        const int q = s.clean.indexOf(QStringLiteral("QMessageBox"), from);
        if (q < 0) break;
        from = q + 1;
        const int open = s.mask.indexOf(QLatin1Char('('), q);
        if (open < 0) continue;
        int depth = 0, close = -1;
        for (int i = open; i < s.mask.size(); ++i) {
            const QChar c = s.mask.at(i);
            if (c == QLatin1Char('(')) {
                ++depth;
            } else if (c == QLatin1Char(')')) {
                --depth;
                if (depth == 0) { close = i; break; }
            }
        }
        if (close < 0) continue;
        BoxSite b;
        b.pos = q;
        b.args = s.clean.mid(open + 1, close - open - 1);
        out.append(b);
    }
    return out;
}

// ── 判定:这个弹窗有没有被 modalAlertsEnabled() 守住 ──────────────────────
//
//   本仓库有两种守卫写法,两种都得认:
//
//     ① 包住型: if (modalAlertsEnabled()) { QMessageBox::warning(...); }
//        → 守卫在【块头】(那个 '{' 之前的那句 if)
//     ② 早退型: if (!modalAlertsEnabled()) { 记日志; return; }
//                 QMessageBox::warning(...);
//        → 守卫在【同一个块里、弹窗之前】(兄弟语句)
//
//   所以判据是:【块头】或【块内到弹窗之前的那一段】里必须有 modalAlertsEnabled()。
//   两段都限定在【最内层块】的范围内 —— 不像第一版那样开一个几千字符的窗口,
//   窗口里混进别处的守卫,结果"删掉守卫测试还是绿的"。
static bool boxIsGuarded(const SrcPrep& s, const BoxSite& b, QString* why) {
    const int open = innermostBlockOpen(s.mask, b.pos);
    if (open < 0) {
        if (why) *why = "这个弹窗不在任何 { } 块里 —— 位置可能不对,或代码结构变了。";
        return false;
    }

    // 块头:从 '{' 往前退到上一个语句边界(';' '{' '}')
    int hdrStart = open;
    while (hdrStart > 0) {
        const QChar c = s.mask.at(hdrStart - 1);
        if (c == QLatin1Char(';') || c == QLatin1Char('{') || c == QLatin1Char('}')) break;
        --hdrStart;
    }
    const QString header = s.clean.mid(hdrStart, open - hdrStart);
    const QString body   = s.clean.mid(open, b.pos - open);

    const bool hit = header.contains(QStringLiteral("modalAlertsEnabled()"))
                  || body.contains(QStringLiteral("modalAlertsEnabled()"));
    if (why) {
        *why = QStringLiteral("块头: %1\n块内(到弹窗前): %2")
                   .arg(header.simplified(),
                        body.simplified().left(200));
    }
    return hit;
}

// ---------------------------------------------------------------------------
// 读源码文件(路径相对仓库根,根由编译期宏给出)
// ---------------------------------------------------------------------------
QString tst_ThrottleNotAnError::readSource(const QString& relPath) const {
    const QString full = QDir(kHmiSrcDir).absoluteFilePath(relPath);
    QFile f(full);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    return QString::fromUtf8(f.readAll());
}

// ============================================================================
// ① 协议层
// ============================================================================

// ---------------------------------------------------------------------------
// 读限流:第二帧被拒收时,不得发 errorOccurred
// ---------------------------------------------------------------------------
void tst_ThrottleNotAnError::readThrottle_doesNotEmitErrorOccurred() {
    SilentSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));

    ModbusMaster master;
    QSignalSpy connectedSpy(&master, &ModbusMaster::connected);
    QSignalSpy errorSpy(&master, &ModbusMaster::errorOccurred);
    QSignalSpy finishedSpy(&master, &ModbusMaster::readFinished);

    master.connectTo("127.0.0.1", slave.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "连不上自己起的从站 —— 后面全部无从谈起");
    QCOMPARE(errorSpy.count(), 0);   // 连接过程本身不该报错

    // 第 1 帧:发出去,从站装死不回 → pending_ 非空
    QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, 0, 1);
    QVERIFY(master.sendReadRequest(unit, 1, 1001) != nullptr);

    // ★ 从站【异步】收 —— 帧已经发出去了,但 TCP 投递 + readyRead 是事件循环里的事,
    //   立刻断言 requestCount 会读到 0(第一次写这条用例时就是这么红的)。
    //   限流的前提是 pending_ 非空,那个用 pendingCount() 断言更直接。
    QTRY_VERIFY2(slave.requestCount >= 1,
                 "从站没收到第一个请求 —— 测的不是限流,是别的东西");
    QCOMPARE(master.pendingCount(), 1);

    // 第 2 帧:必然撞上限流(pending_ 非空)
    QModbusDataUnit unit2(QModbusDataUnit::HoldingRegisters, 2, 1);
    QVERIFY(master.sendReadRequest(unit2, 1, 1002) == nullptr);

    // ★ 这就是断言点 ★
    //   把 spy 抓到的消息原文也带出来 —— 只说"有 N 条"没法定位,
    //   上一版就是因此卡了半天:到底是限流走了别的分支,还是别的东西报的错。
    QStringList got;
    for (const auto& a : errorSpy) got << a.at(0).toString();
    QVERIFY2(errorSpy.count() == 0,
             qPrintable(QStringLiteral(
                 "限流时 ModbusMaster 发了 %1 条 errorOccurred —— "
                 "限流是【内部调度细节】,不是错误。\n"
                 "  它一旦走 errorOccurred,在 hmi.cpp 那边会被接到"
                 "「改状态 Failed + 弹一个模态 QMessageBox」。\n"
                 "  后果:任何地址不连续的工程,第一次轮询之后永久停摆,\n"
                 "  而且全程零告警(那条消息走 qCDebug,INFO 级别看不见)。\n"
                 "  2026-10-23 的实测就是这个:70 秒只发了 1 个请求。\n"
                 "  抓到的消息原文:%2")
                 .arg(errorSpy.count())
                 .arg(got.isEmpty() ? QStringLiteral("(无)") : got.join(QStringLiteral(" | ")))));
}

// ---------------------------------------------------------------------------
// 写限流:同上。写请求和读请求抢同一个 driver,多 PLC 工程里撞车是常态。
//
// ★★★ 2026-10-26(BUGFIX-E1):这条用例的【契约变了】★★★
//
//   改之前:写撞上在途槽 → 【直接拒收】(返回 nullptr)。
//   改之后:写撞上在途槽 → 【排队】(返回非空,槽一空就发出去)。
//
//   【为什么改】2026-10-26 短跑实测(soaks/sweep_m85):
//     604 次写尝试【全部】被拒,0 帧到达从站;同一时刻 547 次读全部成功。
//     而同样配置的另外六轮写【一次都没被拒】——
//     决定成败的是两个 100ms 节拍的【启动相位差】,而它一辈子不变。
//     ⇒ 掷到坏的那一面,整轮(24 小时也是)一个字节都写不出去,
//       而读照常成功 ⇒ 长跑的活体判据【照样绿】。
//
//   【本用例的【原意】没有变】它要钉的是:
//     「限流是内部调度细节,不是错误」—— 撞车【不许】emit errorOccurred。
//   这一点改之前改之后【都必须成立】,而且现在【更重要】了:
//     排队这条路上一个 errorOccurred 都没有(走的是 qCDebug),
//     所以哪怕以后有人给排队分支加了个 errorOccurred,这条用例照样抓得住。
//
//   【为什么"返回 nullptr"这条断言被拿掉】
//     它钉的是【旧行为】。而那个旧行为就是那个洞本身。
//     留着它 = 要求代码回到"写会饿死"的状态。
//     ⇒ 改成断言"【排队】而不是拒收",并且【依然不发 errorOccurred】。
//     排队的完整语义(真的发出去、值不串、断线作废)在
//     tst_ModbusWriteQueue.cpp 里钉,那才是它的地盘。
// ---------------------------------------------------------------------------
void tst_ThrottleNotAnError::writeThrottle_doesNotEmitErrorOccurred() {
    SilentSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));

    ModbusMaster master;
    QSignalSpy connectedSpy(&master, &ModbusMaster::connected);
    QSignalSpy errorSpy(&master, &ModbusMaster::errorOccurred);

    master.connectTo("127.0.0.1", slave.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "连不上自己起的从站");
    QCOMPARE(errorSpy.count(), 0);

    // 第 1 个写请求:发出,从站不回 → pending_ 非空
    QModbusDataUnit wunit(QModbusDataUnit::HoldingRegisters, 0, 1);
    wunit.setValue(0, 0x1234);
    QVERIFY(master.sendWriteRequest(wunit, 1, 2001) != nullptr);

    // 第 2 个:必然撞上限流
    QModbusDataUnit wunit2(QModbusDataUnit::HoldingRegisters, 2, 1);
    wunit2.setValue(0, 0x5678);
    // ★ 2026-10-26:撞车【不再拒收】—— 它【排队】,等槽空出来。
    //   返回非空 = "已受理,必定会有一个 writeFinished 回来"。
    QVERIFY2(master.sendWriteRequest(wunit2, 1, 2002) != nullptr,
             "★ 写撞上在途槽时被【拒收】了 —— 这就是 2026-10-26 那个洞:"
             "拒收 ⇒ WriteScheduler 每帧重试 ⇒ 可能整轮(24 小时)一个字节都写不出去,"
             "而读照常成功、日志照常长、活体判据照常绿。");

    // ★ 撞车【不许】当错误报出去 —— 这条用例钉的就是这个。
    QVERIFY2(errorSpy.count() == 0,
             qPrintable(QStringLiteral(
                 "写限流时 ModbusMaster 发了 %1 条 errorOccurred —— 同理,写侧的撞车"
                 "在多 PLC 工程里是【常态】不是故障,弹窗会把主线程焊死。")
                 .arg(errorSpy.count())));
}

// ---------------------------------------------------------------------------
// 负向对照:【读】限流的行为一个字都没变 —— 还是拒收、还是不发 readFinished。
//   这一条很重要:光断言"没报错"不够,还得断言"该拒的还是拒了",
//   否则将来有人把整个限流分支删掉,测试也发现不了。
//
//   ★ 2026-10-26(BUGFIX-E1):这条【只管读】。
//     写的撞车已经【不是】拒收了(改成排队),所以这里【不能】再断言写也拒收。
//     写的对应断言在 writeThrottle_doesNotEmitErrorOccurred() 里
//     —— 它断言"排队"而不是"拒收",而且同样不发 errorOccurred。
// ---------------------------------------------------------------------------
void tst_ThrottleNotAnError::readThrottle_stillReturnsNullptr_andDoesNotReply() {
    SilentSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));

    ModbusMaster master;
    QSignalSpy connectedSpy(&master, &ModbusMaster::connected);
    QSignalSpy finishedSpy(&master, &ModbusMaster::readFinished);

    master.connectTo("127.0.0.1", slave.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "连不上自己起的从站");

    QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, 0, 1);
    QVERIFY(master.sendReadRequest(unit, 1, 3001) != nullptr);
    QVERIFY(master.pendingCount() == 1);   // 在途:限流的前提

    QModbusDataUnit unit2(QModbusDataUnit::HoldingRegisters, 2, 1);
    QVERIFY(master.sendReadRequest(unit2, 1, 3002) == nullptr);   // 仍然拒收

    QVERIFY2(master.pendingCount() == 1,
             "被拒收的请求不许登记进 pending_ —— "
             "登记进去就是一条永远等不到 reply 的记录(PollScheduler 会因此"
             "把那批变量永久留在在途态)。");

    QVERIFY2(finishedSpy.count() == 0,
             "被拒收的请求不许发 readFinished —— "
             "拒收就是拒收,不能伪装成一次读完了。");
}

// ============================================================================
// ② UI 层(静态守卫)
// ============================================================================

// ---------------------------------------------------------------------------
// hmi.cpp 的 errorOccurred lambda 必须有 modalAlertsEnabled() 守卫
// ---------------------------------------------------------------------------
void tst_ThrottleNotAnError::ui_errorOccurredHandler_isGuardedByModalAlerts() {
    const QString code = readSource(QStringLiteral("hmi.cpp"));
    QVERIFY2(!code.isEmpty(),
             "读不到 hmi.cpp —— 路径相对 HMI_SRC_DIR 算的,"
             "找不到就说明它被搬到别处去了,或者这个宏指错了目录。");

    const SrcPrep s = prepSource(code);

    // 定位 errorOccurred 的那个 lambda。
    // ★ 在【抹掉注释】的文本里找 —— 免得注释里的同名文字抢先命中。
    const int anchor = s.mask.indexOf(QStringLiteral("IPollDriver::errorOccurred"));
    QVERIFY2(anchor >= 0,
             "hmi.cpp 里找不到 IPollDriver::errorOccurred 的 connect —— "
             "这条线要是没了,通讯错误就彻底没人接了(比弹窗更糟)。");

    // 这个 lambda 的花括号块范围
    const int lambdaOpen = innermostBlockOpen(s.mask, anchor);
    QVERIFY2(lambdaOpen >= 0, "errorOccurred 的 lambda 不在任何 { } 块里 —— 位置不对?");
    int depth = 0, lambdaClose = -1;
    for (int i = lambdaOpen; i < s.mask.size(); ++i) {
        if (s.mask.at(i) == QLatin1Char('{')) ++depth;
        else if (s.mask.at(i) == QLatin1Char('}')) {
            --depth;
            if (depth == 0) { lambdaClose = i; break; }
        }
    }
    QVERIFY2(lambdaClose > 0, "找不到 errorOccurred lambda 的结束位置。");

    // 这个 lambda 里的 QMessageBox(必须【全部】查,不许只看第一处)
    const QVector<BoxSite> all = findMessageBoxSites(s);
    QVector<BoxSite> mine;
    for (const auto& b : all) {
        if (b.pos > anchor && b.pos < lambdaClose) mine.append(b);
    }
    QVERIFY2(mine.size() >= 1,
             "errorOccurred 的 lambda 里找不到 QMessageBox —— "
             "它可能被改成了别的方式弹窗,那也得由人来看一眼。");

    for (const auto& b : mine) {
        QString why;
        const bool ok = boxIsGuarded(s, b, &why);
        QVERIFY2(ok,
                 qPrintable(QStringLiteral(
                     "errorOccurred 的 lambda 里的弹窗没有 modalAlertsEnabled() 守卫!\n"
                     "  判据看的是【块头】或【同一块内、弹窗之前的那一段】,不看邻近几千字符。\n"
                     "  为什么这行不能少:QMessageBox 是模态阻塞的,\n"
                     "  长跑脚本没有人去点它 → 主线程就此焊死,\n"
                     "  现象是进程活着、日志停长、内存不涨,全是「正常」的假象。\n"
                     "  2026-10-23 的 P0 就是漏了这一行。\n"
                     "  实际结构:\n%1")
                     .arg(why)));
    }
}

// ---------------------------------------------------------------------------
// 别的机器触发型弹窗也必须受守卫约束
//   这几处都不是用户点按钮触发的,是程序自己触发的 —— 长跑时没人点。
// ---------------------------------------------------------------------------
void tst_ThrottleNotAnError::ui_machineTriggeredDialogs_areGuarded() {
    const QString code = readSource(QStringLiteral("hmi.cpp"));
    QVERIFY2(!code.isEmpty(), "读不到 hmi.cpp");

    const SrcPrep s = prepSource(code);
    const QVector<BoxSite> sites = findMessageBoxSites(s);

    // ★★★ 锚点怎么选,是这条用例的成败关键 ★★★
    //   第一版我把锚点写成弹窗【标题】("运行时画面" / "加载失败" …),
    //   结果第一次 indexOf 命中的是【注释里】的同名文字(hmi.cpp:189 的
    //   "演示画布 / 编辑器 / 运行时画面"),往回看 1200 字符当然找不到守卫,
    //   用例红了 —— 而且红得莫名其妙。
    //
    //   教训跟这仓库里反复踩的坑同形:【字符串匹配必须有明确的目标】。
    //   所以这里锚定的是 QMessageBox:: 调用本身,而且要求它出现在
    //   "if (modalAlertsEnabled())" 之后 —— 即守卫包住弹窗。
    //
    //   ★★★ 锚点不能用【弹窗标题】★★★ ——
    //   第二版我用了中文标题当锚点,结果 "加载失败" 这个标题在 hmi.cpp 里有
    //   【三处】:工程加载失败(869)、画面加载失败(1465)、--project 加载失败(2082)。
    //   循环只取第一处,查的就不是我说的那一处。
    //   标题会重复、会被改、注释里还可能同名 —— 现在改成锚定
    //   【能唯一定位这一处调用的代码特征】。
    //
    //   ★★★ 第三版:【全部命中都要查】+【按块结构判守卫】★★★ ——
    //   第二版只取第一个命中,而且判据是"往回看 1200 字符"。
    //   1200 字符的窗口同样会混进别处的守卫 —— 负向对照里把守卫删掉,
    //   这条用例还是绿的。绿的测试不等于有效的测试,必须自己咬自己一口。
    //
    //   判据:每处弹窗都要有对应的守卫,顺序也要对。
    const struct { const char* anchor; const char* what; } cases[] = {
        { "\"Modbus 连不上\"",                     "连接失败弹窗" },
        { "\"Modbus 断了\"",                       "连接中断弹窗" },
        { "\"Modbus 错误\"",                       "通讯错误弹窗" },
        { "canvas_->lastError()",                  "画面加载失败弹窗" },
        { "QStringLiteral(\"运行时画面\")",        "空画布提示弹窗" },
        { "QStringLiteral(\"部分控件没绑上变量\")", "变量名对不上弹窗" },
    };

    for (const auto& c : cases) {
        const QString anchor = QString::fromUtf8(c.anchor);

        QVector<BoxSite> matched;
        for (const auto& b : sites) {
            if (b.args.contains(anchor)) matched.append(b);
        }

        QVERIFY2(!matched.isEmpty(),
                 qPrintable(QStringLiteral(
                     "hmi.cpp 里找不到参数里带特征「%1」的 QMessageBox 调用(%2) —— "
                     "它可能被改名或删了,那就该由人来确认这处弹窗的去留。")
                     .arg(anchor, QString::fromUtf8(c.what))));

        // ★ 全部命中都要判,不许只看第一处 ★
        for (const auto& b : matched) {
            QString why;
            const bool ok = boxIsGuarded(s, b, &why);
            QVERIFY2(ok,
                     qPrintable(QStringLiteral(
                         "「%1」(%2)的弹窗没有 modalAlertsEnabled() 守卫!\n"
                         "  判据看的是【块头】或【同一块内、弹窗之前的那一段】。\n"
                         "  这类弹窗是【机器触发】的,不是用户点按钮触发的。\n"
                         "  长跑脚本没有人去点它 → 主线程永久卡死,而外面看全是"
                         "「正常」的假象。\n"
                         "  main.cpp:420 的全局开关 2026-10-21 就加了,当时漏了这几处。\n"
                         "  实际结构:\n%3")
                         .arg(anchor, QString::fromUtf8(c.what), why)));
        }
    }
}

QTEST_MAIN(tst_ThrottleNotAnError)
#include "tst_ThrottleNotAnError.moc"
