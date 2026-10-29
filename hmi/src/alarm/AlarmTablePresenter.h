// ============================================================================
// 报警表【行】的呈现 —— 主画面 ui.alarmTable 与运行时画布 AlarmListControl 共用
//
// ★ 为什么要有这个文件 —— 2026-10-24
//
//   报警列表在这个产品里【有两套 UI】,表格形状一样(时间/报警名/等级/值/状态):
//     · 主画面的 ui.alarmTable      (hmi.cpp)
//     · 运行时画布上的 AlarmListControl (src/widgets/)
//   形状一样、逻辑也一样,却各写一份 —— 于是漂了。
//
//   ★★ 漂出来的后果不是"看着不一样",是主画面的报警表【整张废了】★★
//
//   2026-10-14 的 G-1 重构(commit 2f51374)要把两套 UI 的配色收成一个权威,
//   做法是对的(抽出 AlarmRowPalette,等级/状态改存 Qt::UserRole)。
//   但那一次重构在主画面这边【把状态列的"创建"删掉了】:
//     原来: ui.alarmTable->setItem(row, 4, new QTableWidgetItem("未确认"));
//     改后: auto* statusItem = ui.alarmTable->item(row, 4);  // 读一个【从来没被创建过】的 item
//   一行连锁反应:
//
//     ① 状态列永远是空白              —— 表头写着"状态",格子一直空着
//     ② 确认之后那一行纹丝不动        —— setAlarmRowState 改的是空气
//     ③ repaintAlarmRow 取不到状态列 → 提前 return
//        → 主画面报警表【一格颜色都没有】
//          而 G-1 那天的文档里写着"现在两套 UI 都有四档静止色"
//     ④ updateAlarmBlinkTimer 读不到状态列 → 认为没有行该闪
//        → 【严重级警报不闪】(而闪烁正是这个产品最核心的报警表现)
//     ⑤ 右键菜单"确认"的判据读的是同一个 UserRole → 恒为 false
//        → 【"确认"菜单项恒灰】,操作员根本没法单条确认一条报警
//
//   也就是说:2026-10-14 那次重构把主画面的报警表整个废掉了,
//   而当时 68 个测试【全绿】—— 因为主画面那套【一条测试都没有】。
//   (跟"测试没覆盖"是同一类病,但这次得先让"行怎么造出来"变成【可测】的东西。)
//
//   ★ 本文件只管【行】:怎么写一行、怎么改状态、怎么上色、该不该闪、留多少行。
//     不管 UI 装配(那是 hmi.cpp / AlarmListControl 各自的事)。
//
//   ★ 两套 UI 唯一【故意】不同的地方:等级列的文字语言
//     (主画面中文"严重"、画布英文"Critical")—— 那是已记录在 docs/现状.md 的
//     一处产品口径不一致,不在本次范围。所以等级文字由调用方传进来。
//     状态文字两边都是中文,可以共用。
//
// 纯逻辑 + 纯 QTableWidget 操作,不依赖任何主窗口 —— 可直接单测。
// ============================================================================

#pragma once

#include "alarm/AlarmRowPalette.h"   // RowVisualState / kLevelRole / kStateRole

#include <QSet>
#include <QString>

class QTableWidget;

enum class AlarmLevel;
enum class AlarmState;
struct Alarm;

namespace qwhmi::alarm {

// ★ 行数上限 —— 报警表【不能无界长】。
//
//   【为什么必须有】这是台 7×24 不关机的机器。
//   2026-10-24 实测:24 小时长跑里报警表长到 12028 行,只涨不消
//   (只有"全部清除"会清表,而它 24 小时里一次都没触发),
//   每行约 2034 字节 → 约 24.3 MB。这就是 SOAK-2 内存线性增长的【一半】。
//
//   【为什么是 500】跟 AlarmManager::kMaxArchived(500)取同一个量级:
//   内存里留最近这么多条,够操作员回看;再往前的去"报警历史"tab 查 SQLite。
constexpr int kMaxAlarmRows = 500;

// ★ 列下标(两套 UI 的表形状一样:时间/报警名/等级/值/状态)
constexpr int kColTime   = 0;
constexpr int kColName   = 1;
constexpr int kColLevel  = 2;
constexpr int kColValue  = 3;
constexpr int kColStatus = 4;

// ★ 业务状态 → 状态列文字。
//
//   文字从【AlarmState】派生,颜色从【它映射出的视觉状态】派生 ——
//   两者出自同一个 st,不可能各说各话。
//   (主画面与画布两边都改成调这一个函数 —— 原来两边各写一份,
//    运行时画布那份在 src/widgets/AlarmListControl.cpp 里。)
QString stateText(AlarmState st);

// ★ 追加一行(5 列【全部】创建,含状态列)。
//
//   @param levelText  等级列要显示的文字 —— 由调用方给,因为两套 UI 的语言不同。
//                     只传文字、不传 enum:enum 走 kLevelRole 单独存,
//                     配色只认 enum,不认文字(见 AlarmRowPalette.h)。
void appendAlarmRow(QTableWidget* table, const Alarm& a, const QString& levelText);

// ★ 改某一行状态:文字 + UserRole 一起改。
//   只改文字不改 UserRole → 配色还按"未确认"算,那一行既灰着又在闪。
void setRowState(QTableWidget* table, int row, AlarmState st);

// ★ 重画某一行(配色唯一入口 = AlarmRowPalette)。
void repaintRow(QTableWidget* table, int row, bool blinkOn);

// ★ 这一行此刻该不该闪(未确认 + 严重/危急)。
bool rowShouldBlink(QTableWidget* table, int row);

// ★ 表里还有没有该闪的行(一个都没有就该停闪烁定时器)。
bool anyRowShouldBlink(QTableWidget* table);

// ★ 读回一行的等级 / 视觉状态。
//   UserRole 读不到时【向"更像未处理报警"的方向兜底】+ qCWarning(不静默)。
//   —— 兜底偏向【多报】而不是【漏报】,理由见 AlarmRowPalette.h。
AlarmLevel      rowLevel(QTableWidget* table, int row);
RowVisualState  rowVisualState(QTableWidget* table, int row);

// ★ 裁剪:把表裁到 maxRows 行以内,返回丢掉的行数。
//
//   ★★ 只丢【已经不在 liveIds 里】的旧行 —— 还在活动的行【一律不丢】★★
//
//   【为什么不能"超了就丢最旧的"】
//     丢掉的如果是一条【还没确认、还没恢复】的报警,等于把它从操作员眼前拿走。
//     本项目的原则是"漏报比多报危险得多"(见 AlarmRowPalette.h /
//     AlarmListControl::levelAt 的兜底方向)—— 宁可表超上限,也不藏报警。
//
//   【那 liveIds 是谁】活动报警的 ID 集合(通常来自 AlarmManager::activeAlarms())。
//   已经恢复的报警不在里面 —— 它们的记录进 SQLite 了,表里留着只是回看用的。
//
//   【丢弃顺序】从最旧的一行开始找,丢【最旧的、不在活动列表里的】那行。
//   所以日志保持"越新越靠后"的连续感,中间可能留一条活动的(那是刻意的)。
int trimToLimit(QTableWidget* table, const QSet<qint64>& liveIds, int maxRows);

} // namespace qwhmi::alarm
