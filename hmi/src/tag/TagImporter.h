// ============================================================================
// 阶段 D · D.7 · 标签导入器(CSV-based, 零三方依赖)
//
// 文件职责:从 CSV 文件批量导入 Variable 配置(name, plcId, type, access, address,
//   alias, expression)。支持普通 variable + D.6 派生 variable。
//
// 关键设计:
//   - 零三方依赖 — QTextStream 读 CSV(同 D.4 CsvExporter 风格)
//   - CSV 表头: name,plcId,type,access,address,alias,expression
//   - 必需列: name, type(其他列空 = 默认值)
//   - type 字符串: Bool/Int16/UInt16/Int32/Float
//   - access 字符串: R/RW(默认 R)
//   - alias 用 | 分隔多个("转速|RPM")
//   - 有 expression 列 → 调 VariableManager::addDerivedVariable
//   - 无 expression 列 → new Variable + addVariable
//   - 真 Xlsx 支持推迟到商用化(引入 QXlsx 三方)
//
// 关联:D.5 别名,D.6 派生,VariableManager, hmi.cpp(后续接 UI 按钮)
// ============================================================================

#pragma once

#include <QString>
#include <QStringList>

class VariableManager;

namespace qwhmi::tag {

class TagImporter {
public:
    struct Result {
        int total = 0;          // CSV 总行数(不含表头)
        int imported = 0;        // 成功导入
        int skipped = 0;         // 跳过(已存在/格式错)
        QStringList errors;      // 每行错误信息
    };

    // ★ 从 CSV 文件导入 Variable/派生 Variable
    //   path: CSV 文件路径
    //   mgr: 目标 VariableManager
    //   返回:Result(无失败错误返 true,errors 列表为空)
    static Result importFromCsv(const QString& path, VariableManager* mgr);

    // ★ CSV 表头常量(供 UI 展示 + 测试用)
    static QString csvHeader();
};

} // namespace qwhmi::tag