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
    //   knownPlcIds:当前工程里【已配置】的 plcId 集合。
    //     传空集合 = 调用方不知道有哪些 PLC(或不关心),不做检查。
    //     传了就检查,发现变量指向一个不存在的 PLC 时【警告但不拒绝】。
    //
    //     ★【为什么只警告不拒绝】"先导 CSV、再配 PLC"是完全合理的顺序,
    //       而变量 plcId 至今【没有 UI 入口】(整个变量编辑界面都不存在),
    //       用户只能靠 CSV 或手改 JSON 来写 plc_id。
    //       拒绝的话,"我导了 200 个变量,plc_id 先写 plc1/plc2/plc3,
    //       等会儿在 PLC 配置里配上"这条正常路径直接走不通。
    //
    //     ★【为什么必须警告】静默接受的后果是:导入全部"成功",
    //       界面上一片绿,但那些变量永远不通讯 —— 而 plc_id 那一格
    //       在界面上【根本没有显示】,用户没有任何地方能看到"plc3 不存在"。
    //       运行时它们一直是无连接状态(值停在 Bad),那份坏配置就永远查不到了。
    //       至少在导入那一刻把话说出来。
    //
    //   返回:Result(无失败错误返 true,errors 列表为空)
    static Result importFromCsv(const QString& path,
                                VariableManager* mgr,
                                const QStringList& knownPlcIds = QStringList());

    // ★ CSV 表头常量(供 UI 展示 + 测试用)
    static QString csvHeader();
};

} // namespace qwhmi::tag