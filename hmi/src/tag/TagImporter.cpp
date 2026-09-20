// ============================================================================
// 阶段 D · D.7 · TagImporter 实现(CSV-based)
// 对应 TagImporter.h
// ============================================================================

#include "tag/TagImporter.h"
#include "VariableManager.h"
#include "Variable.h"

#include <QFile>
#include <QHash>
#include <QStringList>
#include <QTextStream>

namespace qwhmi::tag {

QString TagImporter::csvHeader() {
    return "name,plcId,type,access,address,alias,expression";
}

// ★ 简单 RFC 4180 parser(单文件,不带转义引号 — 工业场景够用)
static QStringList parseCsvLine(const QString& line) {
    return line.split(',');
}

// ★ DataType 字符串 → 枚举
static Variable::DataType parseDataType(const QString& s, bool* ok) {
    *ok = true;
    if (s == "Bool")   return Variable::DataType::Bool;
    if (s == "Int16")  return Variable::DataType::Int16;
    if (s == "UInt16") return Variable::DataType::UInt16;
    if (s == "Int32")  return Variable::DataType::Int32;
    if (s == "Float")  return Variable::DataType::Float;
    *ok = false;
    return Variable::DataType::Float;  // 默认 fallback
}

// ★ Access 字符串 → 枚举(支持 R / RW)
static Variable::Access parseAccess(const QString& s) {
    if (s.trimmed() == "RW") return Variable::Access::ReadWrite;
    return Variable::Access::ReadOnly;  // 默认 R
}

TagImporter::Result TagImporter::importFromCsv(const QString& path, VariableManager* mgr) {
    Result r;
    if (!mgr) {
        r.errors << "VariableManager 为空";
        return r;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        r.errors << QString("无法打开文件: %1").arg(f.errorString());
        return r;
    }
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);

    // ★ 第一行 = 表头(列名定位)
    if (in.atEnd()) {
        r.errors << "CSV 为空";
        return r;
    }
    const QString header = in.readLine();
    const QStringList cols = parseCsvLine(header);
    const QHash<QString, int> idx = [&]() {
        QHash<QString, int> m;
        for (int i = 0; i < cols.size(); ++i) m[cols[i].trimmed()] = i;
        return m;
    }();
    // ★ 必需列检查
    if (!idx.contains("name") || !idx.contains("type")) {
        r.errors << "CSV 缺少必需列(name, type)";
        return r;
    }

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;  // 跳过空行
        ++r.total;
        const QStringList row = parseCsvLine(line);
        // ★ 容错:行比表头短(末尾空列省略)→ 补 "";比表头长 → 警告但继续
        if (row.size() < cols.size()) {
            r.errors << QString("第 %1 行末尾列省略(补空,继续)").arg(r.total);
            // 继续 — 后续 row.value(缺索引) 自动返 ""
        } else if (row.size() > cols.size()) {
            r.errors << QString("第 %1 行多余列(忽略)").arg(r.total);
        }
        const QString name      = row.value(idx.value("name")).trimmed();
        const QString plcId     = row.value(idx.value("plcId", -1)).trimmed();
        const QString typeStr   = row.value(idx.value("type")).trimmed();
        const QString accessStr = row.value(idx.value("access", -1)).trimmed();
        const QString addrStr   = row.value(idx.value("address", -1)).trimmed();
        const QString aliasStr  = row.value(idx.value("alias", -1)).trimmed();
        const QString expr      = row.value(idx.value("expression", -1)).trimmed();
        if (name.isEmpty()) {
            r.errors << QString("第 %1 行 name 为空").arg(r.total);
            ++r.skipped;
            continue;
        }
        bool typeOk = false;
        const Variable::DataType type = parseDataType(typeStr, &typeOk);
        if (!typeOk) {
            r.errors << QString("第 %1 行 type 无效: %2").arg(r.total).arg(typeStr);
            ++r.skipped;
            continue;
        }
        const Variable::Access access = parseAccess(accessStr);
        bool addrOk = false;
        const quint16 address = addrStr.toUShort(&addrOk);
        (void)addrOk;

        if (!expr.isEmpty()) {
            // ★ D.6 派生变量
            if (!mgr->addDerivedVariable(name, expr, type, access)) {
                r.errors << QString("第 %1 行 addDerivedVariable 失败: %2").arg(r.total).arg(name);
                ++r.skipped;
                continue;
            }
        } else {
            // ★ 普通 Variable
            auto* v = new Variable(name, address, type, access, mgr);
            if (!plcId.isEmpty()) v->setPlcId(plcId);
            // ★ D.5 别名
            for (const QString& a : aliasStr.split('|', Qt::SkipEmptyParts)) {
                v->addAlias(a.trimmed());
            }
            if (!mgr->addVariable(v)) {
                r.errors << QString("第 %1 行 addVariable 失败: %2 (name 或 alias 冲突)").arg(r.total).arg(name);
                ++r.skipped;
                continue;
            }
        }
        ++r.imported;
    }

    return r;
}

} // namespace qwhmi::tag