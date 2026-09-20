// ============================================================================
// 阶段 D · D.4 · 报表导出(CSV)— 零三方依赖
//
// 文件职责:把 HistoryRecorder::query 和 AlarmRecorder::query 的结果写成 .csv;
//   工业 HMI 90% 报表走 CSV(Excel 二次加工),QXlsx 推迟到商用化阶段。
//
// 关键设计:
//   - 用 QTextStream 写纯文本(零依赖,Qt 标库)
//   - RFC 4180 兼容:字段含逗号/换行/引号 → 用双引号包,内部引号 ×2 转义
//   - UTF-8 BOM 开头(Excel 中文不乱码)
//   - 写入失败返回 false,UI 可弹错误
//   - free functions,不持有状态,易测试
//
// 关联:D.1 HistoryRecorder, D.3 AlarmRecorder, hmi.cpp
// ============================================================================

#pragma once

#include <QString>
#include <QVector>
#include <QPair>
#include <QDateTime>

// ★ 前向声明必须带 namespace — 否则会跟全局 HistoryRecorder 类型冲突,导致
//   .cpp 里 `HistoryRecorder* hr` 引用全局 forward decl(incomplete)而不是
//   qwhmi::history::HistoryRecorder(完整定义)
namespace qwhmi::history { class HistoryRecorder; }
namespace qwhmi::history { class AlarmRecorder; }

namespace qwhmi::export_ {

// ★ 写变量历史 CSV — 列:variable_name, timestamp, value, quality, is_heartbeat
//   path = 用户选的 .csv 文件路径(若父目录不存在自动 mkdir)
//   返回:true 成功 / false 失败(可读 QString errMsg)
bool writeVariablesCsv(qwhmi::history::HistoryRecorder* hr,
                       const QString& variableName,
                       const QDateTime& from,
                       const QDateTime& to,
                       const QString& path,
                       QString* errMsg = nullptr);

// ★ 写报警历史 CSV — 列:alarm_id, name, level, action, action_at,
//                       variable_name, trigger_value, threshold, user
//   minLevel = 0 全收;> 0 过滤低等级
bool writeAlarmsCsv(qwhmi::history::AlarmRecorder* ar,
                    const QDateTime& from,
                    const QDateTime& to,
                    int minLevel,
                    const QString& path,
                    QString* errMsg = nullptr);

// ★ RFC 4180 字段转义:含 , " 换行 → 包双引号 + " → ""
//   暴露给测试用,internal 也要调
QString escapeCsvField(const QString& field);

} // namespace qwhmi::export_