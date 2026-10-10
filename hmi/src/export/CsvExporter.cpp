// ============================================================================
// 阶段 D · D.4 · CsvExporter 实现
// 对应 CsvExporter.h
// ============================================================================

#include "export/CsvExporter.h"
// ★ include 完整定义(不是 forward decl) — 调 hr->query() 需要完整类型
#include "history/HistoryRecorder.h"
#include "history/AlarmRecorder.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStringConverter>

namespace qwhmi::export_ {

QString escapeCsvField(const QString& field) {
    // ★ RFC 4180:若字段含 , " 换行(CR/LF) → 用双引号包,内部 " → ""
    const bool needsQuote = field.contains(',') || field.contains('"')
                            || field.contains('\n') || field.contains('\r');
    if (!needsQuote) return field;
    QString escaped = field;
    escaped.replace('"', "\"\"");   // " → ""
    return QStringLiteral("\"%1\"").arg(escaped);
}

static bool openCsvFile(const QString& path, QString* errMsg) {
    // ★ 确保父目录存在
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errMsg) *errMsg = QStringLiteral("无法打开文件: %1").arg(f.errorString());
        return false;
    }
    return true;
}

bool writeVariablesCsv(qwhmi::history::HistoryRecorder* hr, const QString& variableName,
                       const QDateTime& from, const QDateTime& to,
                       const QString& path, QString* errMsg)
{
    if (!hr) {
        if (errMsg) *errMsg = QStringLiteral("HistoryRecorder 为空");
        return false;
    }
    QFile file(path);
    if (!openCsvFile(path, errMsg)) return false;
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errMsg) *errMsg = file.errorString();
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);   // ★ Excel 中文不乱码

    // ★ 表头
    out << "variable_name,timestamp,value,quality,is_heartbeat\r\n";

    // ★ 数据行(query 直接拉历史 + 实时心跳已合并写入,无需区分)
    const auto records = hr->query(variableName, from, to);
    for (const auto& [ts, val] : records) {
        // ★ is_heartbeat 列我们 query 不返回 — 简化:全填 0
        //   真要做"区分变化/心跳"导出会拖慢 query;后续 H 阶段加专门的 export 接口
        out << escapeCsvField(variableName) << ','
            << ts.toString(Qt::ISODateWithMs) << ','
            << QString::number(val, 'f', 6) << ','
            << "1,"       // quality:历史库未存精确 quality,统一填 1=Good
            << "0\r\n";   // is_heartbeat:简化全填 0
    }

    out.flush();
    if (out.status() != QTextStream::Ok) {
        if (errMsg) *errMsg = QStringLiteral("写流出错");
        return false;
    }
    return true;
}

bool writeAlarmsCsv(qwhmi::history::AlarmRecorder* ar, const QDateTime& from, const QDateTime& to,
                    int minLevel, const QString& path, QString* errMsg)
{
    if (!ar) {
        if (errMsg) *errMsg = QStringLiteral("AlarmRecorder 为空");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errMsg) *errMsg = file.errorString();
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);

    // ★ 表头
    // area 追加在【末尾】,理由同 AlarmRecorder::kColumns:
    //   插在中间会让老的下游脚本(按列号取值的那些)全部串列。
    //   末尾追加只影响"知道最后多了一列"的脚本,那正是加列的正常代价。
    out << "alarm_id,name,level,action,action_at,variable_name,trigger_value,threshold,user,area\r\n";

    // ★ 4 个 action 的可读字符串
    static const char* kActionNames[] = {"triggered", "acknowledged", "recovered", "cleared"};
    static const char* kLevelNames[]  = {"Info", "Warning", "Critical", "Emergency"};

    // ★ 注意:这里【不】带区域筛选(和 writeVariablesCsv 一样只按时间+等级)。
    //   导出是"把这段时间的全量记录搬走",不是"把当前界面的筛选结果搬走"——
    //   用户在筛选状态下点导出,拿到全量才可能自己再做透视。
    //   这条是既有约定,不是这里顺手改的。
    const auto records = ar->query(from.toMSecsSinceEpoch(), to.toMSecsSinceEpoch(), minLevel);
    for (const auto& r : records) {
        const char* actionName = (r.action >= 0 && r.action < 4) ? kActionNames[r.action] : "?";
        const char* levelName  = (r.level  >= 0 && r.level  < 4) ? kLevelNames[r.level]   : "?";

        out << r.alarmId << ','
            << escapeCsvField(r.name) << ','
            << levelName << ','
            << actionName << ','
            << r.at.toString(Qt::ISODateWithMs) << ','
            << escapeCsvField(r.variableName) << ','
            << escapeCsvField(r.triggerValue) << ','
            << escapeCsvField(r.threshold) << ','
            << escapeCsvField(r.user) << ','
            // ★ 未分配导出成【空】,不是"未分配"三个字。
            //   用户拿到 CSV 是要拿去透视/按区域分组,"未分配"会被当成
            //   一个真的区域名,还和用户手填的"未分配"混在一起。
            << escapeCsvField(r.area) << "\r\n";
    }

    out.flush();
    if (out.status() != QTextStream::Ok) {
        if (errMsg) *errMsg = QStringLiteral("写流出错");
        return false;
    }
    return true;
}

} // namespace qwhmi::export_