// ============================================================================
// 阶段 D · D.4 · CsvExporter 单测
//
// 验证 5 个核心行为:
//   1. escapeCsvField 转义逗号 + 引号 + 换行
//   2. writeVariablesCsv 写正确表头 + 行数 + UTF-8 BOM
//   3. writeAlarmsCsv 写正确表头 + action/level 字符串映射
//   4. 无数据时只写表头
//   5. 路径不存在时自动创建父目录
//
// 关联:CsvExporter.h, HistoryRecorder.h, AlarmRecorder.h
// ============================================================================

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTextStream>
#include "export/CsvExporter.h"
// ★ include 完整定义(不是 forward decl) — 需要 hr->query() 完整类型
#include "history/HistoryRecorder.h"
#include "history/AlarmRecorder.h"
#include "Variable.h"
#include "VariableManager.h"
#include "Alarm.h"
#include "AlarmManager.h"

using namespace qwhmi::export_;

class TestCsvExporter : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void escapeCsvField_handlesCommasAndQuotes();
    void writeVariablesCsv_writesHeaderAndRows();
    void writeAlarmsCsv_writesActionNames();
    void writeVariablesCsv_emptyHistoryWritesOnlyHeader();
    void writeVariablesCsv_createsParentDirectory();

    // ---- ★ F6(2026-10-10):区域必须跟得出库 ----
    void writeAlarmsCsv_carriesAreaColumn();
    void writeAlarmsCsv_unassignedAreaExportsAsEmptyNotLabel();
    // ★ slot 名必须是纯 ASCII —— moc 解析不了中文标识符,
    //   报的是 C2039 "不是 TestCsvExporter 的成员",和"名字里有汉字"看不出关系。
    void writeAlarmsCsv_everyRowHasHeaderColumnCount();

private:
    // ★ readAll helper
    static QString readAllUtf8(const QString& path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QString::fromUtf8(f.readAll());
    }

    // ★ 按 CSV 规则切一行 —— 【引号内的逗号不算分隔符】。
    //   【为什么必须写这个,而不是 QString::split(",")】
    //   一版就是拿 count(',') 数字段的,结果"1号泵房,A区"这种带逗号的区域名
    //   让那一行数出 11 列,测试红了 —— 而导出其实是对的。
    //   这就是"用错误的尺子量东西,尺子坏了却怪被量的东西"。
    //   顺带一提:Excel 自己就是这么解析的,所以这才是用户实际看到的结果。
    static QStringList splitCsvLine(const QString& line) {
        QStringList out;
        QString cur;
        bool inQuotes = false;
        for (int i = 0; i < line.size(); ++i) {
            const QChar c = line.at(i);
            if (c == QLatin1Char('"')) {
                if (inQuotes && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                    cur += QLatin1Char('"');   // "" 是转义后的一个引号
                    ++i;
                } else {
                    inQuotes = !inQuotes;
                }
            } else if (c == QLatin1Char(',') && !inQuotes) {
                out << cur;
                cur.clear();
            } else {
                cur += c;
            }
        }
        out << cur;
        return out;
    }

    // ★ F6 的两个小夹具。做成【类成员】而不是文件里的自由函数,
    //   因为 exportAlarms 要用 readAllUtf8 —— 而它是本类的 private static。
    //   放进匿名 namespace 就调不到了,得再复制一份读文件的代码。
    static void insertAlarmRow(const QString& dbPath, const QString& name,
                               const QString& area, qint64 nowMs);
    static QString exportAlarms(const QString& db, const QString& csvPath, QString* errOut);
};

void TestCsvExporter::initTestCase() {
    qInfo() << "[tst_CsvExporter] init";
}

void TestCsvExporter::cleanupTestCase() {
    qInfo() << "[tst_CsvExporter] cleanup";
}

// ★ 用例 1:escapeCsvField
void TestCsvExporter::escapeCsvField_handlesCommasAndQuotes() {
    // 无特殊字符 → 原样返回
    QCOMPARE(escapeCsvField("hello"), QString("hello"));

    // 含逗号 → 包双引号
    QCOMPARE(escapeCsvField("a,b,c"), QString("\"a,b,c\""));

    // 含双引号 → 包双引号 + 内部 " → ""
    QCOMPARE(escapeCsvField("say \"hi\""), QString("\"say \"\"hi\"\"\""));

    // 含换行 → 包双引号
    QCOMPARE(escapeCsvField("line1\nline2"), QString("\"line1\nline2\""));

    // 含中文 → 原样(UTF-8 编码后处理)
    QCOMPARE(escapeCsvField("温度高"), QString("温度高"));
}

// ★ 用例 2:writeVariablesCsv
void TestCsvExporter::writeVariablesCsv_writesHeaderAndRows() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/test_history.db";
    const QString csvPath = tmp.path() + "/out.csv";

    qwhmi::history::HistoryRecorder hr;
    QVERIFY(hr.open(dbPath));
    hr.setSampleInterval(0);
    hr.setChangeThreshold(0.0);

    VariableManager vm;
    Variable v("Speed", 0, Variable::DataType::Float, Variable::Access::ReadOnly, &vm);
    v.setPlcId("plc1");
    vm.addVariable(&v);
    hr.setSource(&vm);

    // 写 3 个值
    v.setValue(QVariant::fromValue<float>(1.0f));  QTest::qWait(50);
    v.setValue(QVariant::fromValue<float>(2.5f));  QTest::qWait(50);
    v.setValue(QVariant::fromValue<float>(3.14f)); QTest::qWait(50);

    // 导出
    QString err;
    const bool ok = writeVariablesCsv(&hr, "plc1.Speed",
                                       QDateTime::currentDateTime().addSecs(-3600),
                                       QDateTime::currentDateTime().addSecs(60),
                                       csvPath, &err);
    QVERIFY2(ok, qPrintable(err));

    const QString csv = readAllUtf8(csvPath);

    // ★ UTF-8 BOM 检查(QTextStream + setGenerateByteOrderMark 不一定生效,
    //   改读 raw bytes 检查 0xEF 0xBB 0xBF 前缀)
    QFile raw(csvPath);
    QVERIFY(raw.open(QIODevice::ReadOnly));
    const QByteArray head = raw.read(3);
    QCOMPARE(head.size(), 3);
    QCOMPARE(static_cast<quint8>(head[0]), quint8(0xEF));
    QCOMPARE(static_cast<quint8>(head[1]), quint8(0xBB));
    QCOMPARE(static_cast<quint8>(head[2]), quint8(0xBF));

    // ★ 表头
    QVERIFY(csv.contains("variable_name,timestamp,value,quality,is_heartbeat"));

    // ★ 3 行数据(按行计数)
    const auto lines = csv.split("\r\n");
    // lines[0] = header (with BOM), lines[1..3] = 3 data, lines[4] = empty
    QCOMPARE(lines.size(), 5);

    // ★ 数据含 plc1.Speed
    for (int i = 1; i <= 3; ++i) {
        QVERIFY(lines[i].startsWith("plc1.Speed,"));
    }
}

// ★ 用例 3:writeAlarmsCsv
void TestCsvExporter::writeAlarmsCsv_writesActionNames() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/test_alarm.csv.db";
    const QString csvPath = tmp.path() + "/alarms.csv";

    qwhmi::history::AlarmRecorder ar;
    QVERIFY(ar.open(dbPath));

    // 直接 SQL INSERT 几条,避免 AlarmManager 权限检查麻烦
    QFile db(dbPath);
    // 直接用 insertAction 不可访问,改用 QSqlDatabase
    {
        QSqlDatabase d(QSqlDatabase::addDatabase("QSQLITE", "test_insert"));
        d.setDatabaseName(dbPath);
        QVERIFY(d.open());
        QSqlQuery q(d);
        q.prepare(R"(
            INSERT INTO alarm_history
                (alarm_id, name, level, action, action_at, variable_name, trigger_value, threshold, user)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        )");
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        q.addBindValue(101LL); q.addBindValue("温度高"); q.addBindValue(2);
        q.addBindValue(0); q.addBindValue(nowMs);
        q.addBindValue("plc1.Temp"); q.addBindValue("85.5"); q.addBindValue(">=80.0");
        q.addBindValue("operator1"); QVERIFY(q.exec());

        q.addBindValue(101LL); q.addBindValue("温度高"); q.addBindValue(2);
        q.addBindValue(1); q.addBindValue(nowMs + 1000);
        q.addBindValue("plc1.Temp"); q.addBindValue("85.5"); q.addBindValue(">=80.0");
        q.addBindValue("operator1"); QVERIFY(q.exec());
        d.close();
        QSqlDatabase::removeDatabase("test_insert");
    }

    QString err;
    const bool ok = writeAlarmsCsv(&ar,
                                   QDateTime::currentDateTime().addSecs(-3600),
                                   QDateTime::currentDateTime().addSecs(60),
                                   /*minLevel=*/0, csvPath, &err);
    QVERIFY2(ok, qPrintable(err));

    const QString csv = readAllUtf8(csvPath);

    QVERIFY(csv.contains("alarm_id,name,level,action,action_at"));
    QVERIFY(csv.contains("温度高"));              // 中文名
    QVERIFY(csv.contains("plc1.Temp"));
    QVERIFY(csv.contains("Critical"));             // level=2 → "Critical"
    QVERIFY(csv.contains("triggered"));            // action=0
    QVERIFY(csv.contains("acknowledged"));         // action=1
}

// ★ 用例 4:空数据只写表头
void TestCsvExporter::writeVariablesCsv_emptyHistoryWritesOnlyHeader() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/empty.db";
    const QString csvPath = tmp.path() + "/empty.csv";

    qwhmi::history::HistoryRecorder hr;
    QVERIFY(hr.open(dbPath));
    // 不写任何数据

    QString err;
    QVERIFY(writeVariablesCsv(&hr, "no.such.variable",
                              QDateTime::currentDateTime().addSecs(-3600),
                              QDateTime::currentDateTime().addSecs(60),
                              csvPath, &err));

    const QString csv = readAllUtf8(csvPath);
    QVERIFY(csv.contains("variable_name,timestamp,value,quality,is_heartbeat"));

    const auto lines = csv.split("\r\n");
    QCOMPARE(lines.size(), 2);   // header + empty line
}

// ★ 用例 5:父目录自动创建
void TestCsvExporter::writeVariablesCsv_createsParentDirectory() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/h.db";
    const QString csvPath = tmp.path() + "/subdir/that/does/not/exist/out.csv";

    qwhmi::history::HistoryRecorder hr;
    QVERIFY(hr.open(dbPath));

    QString err;
    QVERIFY(writeVariablesCsv(&hr, "x", QDateTime::currentDateTime(),
                              QDateTime::currentDateTime().addSecs(60), csvPath, &err));
    QVERIFY(QFile::exists(csvPath));
}

// ============================================================================
// ★ F6:区域必须跟得出库
//
// 【为什么 CSV 这一条不能漏】
//   报警历史的价值一半在程序里,一半在【表格软件里】——
//   事故分析要的是"按区域分组看次数""对比两个区域一周谁的报警多",
//   这种活没人回 HMI 里点,是把 CSV 拖进 Excel 做透视。
//   区域只进了数据库、没进 CSV,等于这个功能对真正的使用者不存在。
//
// 【为什么这几条断得这么细】
//   漏一列在 CSV 里【不会报错】。Excel 照开,只是少一列,
//   用户多半还以为是"这批数据本来就没区域"。
//   所以必须断列名、断空、断列数,而不是断"文件写成功了"。
// ============================================================================

// 造一条带区域 / 不带区域的报警记录,直接 SQL INSERT 绕开 AlarmManager。
// 【为什么还是手写 SQL 而不是走 AlarmRecorder】
//   本文件测的是【CSV 这一层】,不是落库那一层。落库的测试在
//   tst_AlarmRecorder.cpp(area_roundTripsFromAlarmToTable)。
//   在这里再把 AlarmManager 也串一遍,两层测试互相掩护,真断了也定位不到。
void TestCsvExporter::insertAlarmRow(const QString& dbPath, const QString& name,
                                     const QString& area, qint64 nowMs)
{
    QSqlDatabase d(QSqlDatabase::addDatabase("QSQLITE", "csv_area_insert"));
    d.setDatabaseName(dbPath);
    d.open();
    QSqlQuery q(d);
    q.prepare(R"(
        INSERT INTO alarm_history
            (alarm_id, name, level, action, action_at, variable_name,
             trigger_value, threshold, user, area)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    q.addBindValue(201LL);
    q.addBindValue(name);
    q.addBindValue(2);
    q.addBindValue(0);
    q.addBindValue(nowMs);
    q.addBindValue("plc1.Temp");
    q.addBindValue("85.5");
    q.addBindValue(">=80.0");
    q.addBindValue("operator1");
    // ★ area 传 QString()(null)时 addBindValue 绑的是【真 NULL】,不是空串。
    //   库里两种都可能出现,而"NULL → 空串"的转换是 query() 的活,
    //   本文件只管 CSV 那一段,不该顺手把上一层也测了。
    q.addBindValue(area);
    q.exec();
    d.close();
    QSqlDatabase::removeDatabase("csv_area_insert");
}

// 跑一次导出,返回整个 CSV 文本
QString TestCsvExporter::exportAlarms(const QString& db, const QString& csvPath,
                                      QString* errOut)
{
    qwhmi::history::AlarmRecorder ar;
    if (!ar.open(db)) { if (errOut) *errOut = QStringLiteral("open failed"); return {}; }
    QString err;
    const bool ok = writeAlarmsCsv(&ar,
                                   QDateTime::currentDateTime().addSecs(-3600),
                                   QDateTime::currentDateTime().addSecs(60),
                                   /*minLevel=*/0, csvPath, &err);
    if (errOut) *errOut = err;
    if (!ok) return {};
    ar.close();
    return readAllUtf8(csvPath);
}

void TestCsvExporter::writeAlarmsCsv_carriesAreaColumn() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath  = tmp.path() + "/area.db";
    const QString csvPath = tmp.path() + "/area.csv";

    qwhmi::history::AlarmRecorder seed;
    QVERIFY(seed.open(dbPath));
    seed.close();   // 建好表就够,数据另外插

    insertAlarmRow(dbPath, QStringLiteral("泵房温度高"), QStringLiteral("一号泵房"),
                   QDateTime::currentMSecsSinceEpoch());

    QString err;
    const QString csv = exportAlarms(dbPath, csvPath, &err);
    QVERIFY2(!csv.isEmpty(), qPrintable(err));

    // ★ 断表头里有这一列,而且名字和库里那一列一致。
    //   名字对不上的时候,用户在 Excel 里筛 "area" 会得到 0 行,
    //   而 CSV 明明是有内容的 —— 他只会以为自己操作错了。
    QVERIFY2(csv.contains(QStringLiteral("area")),
             "CSV 表头没有 area 列 —— 区域出了库就在这里断了");
    QVERIFY(csv.contains(QStringLiteral("一号泵房")));
}

void TestCsvExporter::writeAlarmsCsv_unassignedAreaExportsAsEmptyNotLabel() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath  = tmp.path() + "/na.db";
    const QString csvPath = tmp.path() + "/na.csv";

    qwhmi::history::AlarmRecorder seed;
    QVERIFY(seed.open(dbPath));
    seed.close();

    // ★ 这条老记录没有区域 —— F6 上线前库里【全部】记录都是这个状态,
    //   所以它不是边角情况,是绝大多数行。
    insertAlarmRow(dbPath, QStringLiteral("老报警"), QString(),
                   QDateTime::currentMSecsSinceEpoch());

    QString err;
    const QString csv = exportAlarms(dbPath, csvPath, &err);
    QVERIFY2(!csv.isEmpty(), qPrintable(err));

    // ★ 断言"空",而且断言不是"未分配"那三个字。
    //   界面里显示"未分配"是对的(人要能看懂),
    //   但导出是给【机器】读的:"未分配"会被 Excel 当成一个正经的区域名,
    //   于是按区域做透视时它自己变成了一行 —— 而那行什么都不是。
    const QStringList lines = csv.split(QStringLiteral("\r\n"));
    // ★ 3 不是 2:最后一行后面那个 \r\n 会分出一个空串。
    //   少写这条断言直接 QCOMPARE(lines.size(), 2) 会让我以为实现有 bug ——
    //   其实是我把"末尾换行"数漏了。先断条数,是为了让下面的 cols.last()
    //   不会因为行数不对而取到空串上去。
    QCOMPARE(lines.size(), 3);           // 表头 + 一条记录 + 末尾换行分出的空串
    QVERIFY2(!csv.contains(QStringLiteral("未分配")),
             "未分配被写成了字面量 —— 导出会多出一个假区域");
    const QStringList cols = lines.at(1).split(QLatin1Char(','));
    QCOMPARE(cols.size(), 10);
    QVERIFY2(cols.last().isEmpty(), "未分配的那一格必须是空的");
}

void TestCsvExporter::writeAlarmsCsv_everyRowHasHeaderColumnCount() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath  = tmp.path() + "/cols.db";
    const QString csvPath = tmp.path() + "/cols.csv";

    qwhmi::history::AlarmRecorder seed;
    QVERIFY(seed.open(dbPath));
    seed.close();

    // ★ 区域名里【带逗号】的那一条 ——
    //   现场分组名写成 "1号泵房,A区" 是很正常的,
    //   而转义漏了的话,这一行会多出两列:不报错,只是 Excel 里错位。
    //   用列数相等来断,是因为错位【肉眼很难发现】——
    //   用户看到的是"数据有点怪",不会想到是转义。
    insertAlarmRow(dbPath, QStringLiteral("带逗号的区域"), QStringLiteral("1号泵房,A区"),
                   QDateTime::currentMSecsSinceEpoch());
    insertAlarmRow(dbPath, QStringLiteral("普通区域"), QStringLiteral("二号泵房"),
                   QDateTime::currentMSecsSinceEpoch());

    QString err;
    const QString csv = exportAlarms(dbPath, csvPath, &err);
    QVERIFY2(!csv.isEmpty(), qPrintable(err));

    const QStringList lines = csv.split(QStringLiteral("\r\n"));
    QCOMPARE(lines.size(), 4);   // 表头 + 两条 + 末尾换行分出的空串

    const int headerCols = splitCsvLine(lines.at(0)).size();
    QCOMPARE(headerCols, 10);

    QStringList areas;
    for (int i = 1; i < lines.size(); ++i) {
        if (lines.at(i).isEmpty()) continue;
        const QStringList cols = splitCsvLine(lines.at(i));
        QVERIFY2(cols.size() == headerCols,
                 QStringLiteral("第 %1 行列数(%2)和表头(%3)对不上 —— 某个字段的逗号没转义")
                     .arg(i).arg(cols.size()).arg(headerCols).toUtf8().constData());
        areas << cols.last();
    }
    // ★ 不只是数量对得上,还得【值原样】在里面。
    //   只断列数的话,一个把 "1号泵房,A区" 写成 "1号泵房" 的实现
    //   (悄悄吞掉了后半段)列数也是对的,但数据已经没了。
    //   按【集合】比而不是按行序 —— 导出顺序不在这个功能的契约里。
    areas.sort();
    QCOMPARE(areas, (QStringList{QStringLiteral("1号泵房,A区"), QStringLiteral("二号泵房")}));
    // 带逗号的那个值在文件里是加了引号的 —— Excel 靠引号才知道那是一个字段
    QVERIFY(csv.contains(QStringLiteral("\"1号泵房,A区\"")));
}

// ★ QTEST_GUILESS_MAIN(创建 QCoreApplication,Qt SQL + QFile 需要)
QTEST_GUILESS_MAIN(TestCsvExporter)
#include "tst_CsvExporter.moc"