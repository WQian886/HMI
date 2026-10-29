// ============================================================================
// AlarmTablePresenter 实现 —— 见头文件"为什么要有这个文件"
// ============================================================================

#include "alarm/AlarmTablePresenter.h"

#include "../../Alarm.h"      // Alarm / AlarmLevel / AlarmState

#include "logging/Logging.h"  // hmiAlarm(qCWarning 用)

#include <QTableWidget>
#include <QTableWidgetItem>

namespace qwhmi::alarm {

QString stateText(AlarmState st) {
    switch (st) {
        case AlarmState::Active:       return QStringLiteral("未确认");
        case AlarmState::Acknowledged: return QStringLiteral("已确认");
        case AlarmState::Cleared:      return QStringLiteral("已恢复");
    }
    return QStringLiteral("?");   // 不该到这
}

// ★ 追加一行 —— 5 列【全部】创建。
//
//   ★★ 状态列(第 5 列)必须在这里创建,不能等到"要改状态时"才去拿 ★★
//
//   2026-10-14 的 G-1 重构就是把这一行删掉了(改成一个"读 item(row,4)"的
//   读法),而读一个从来没被创建过的 item 恒为 nullptr —— 于是状态列空白、
//   确认无效、无配色、不闪烁、"确认"菜单恒灰。详见头文件的说明。
void appendAlarmRow(QTableWidget* table, const Alarm& a, const QString& levelText) {
    if (!table) return;
    const int row = table->rowCount();
    table->insertRow(row);

    table->setItem(row, kColTime, new QTableWidgetItem(a.triggeredAt.toString("HH:mm:ss")));

    // 报警名那一格额外存 ID( Qt::UserRole )——
    // 确认/恢复都靠它匹配行。同名报警存在时会错配,5.5 BUGFIX 就是修这个。
    auto* nameItem = new QTableWidgetItem(a.name);
    nameItem->setData(Qt::UserRole, a.id_);
    table->setItem(row, kColName, nameItem);

    // 等级列:文字由调用方给(两套 UI 语言不同),enum 单独存 kLevelRole。
    //   配色只认 enum —— 见 AlarmRowPalette.h"为什么必须存,不能靠解析文字反查"。
    auto* levelItem = new QTableWidgetItem(levelText);
    levelItem->setData(kLevelRole, static_cast<int>(a.level));
    table->setItem(row, kColLevel, levelItem);

    table->setItem(row, kColValue, new QTableWidgetItem(a.triggerValue));

    // ★ 状态列:文字 + kStateRole 一起写。
    //   kStateRole 存的是【视觉状态】(visualStateFor),不是 AlarmState ——
    //   视觉状态把"已确认"和"已恢复"归成同一档(都已有人管过,一律淡化成灰),
    //   而 AlarmState 是三档。见 AlarmRowPalette.h 里的说明。
    auto* statusItem = new QTableWidgetItem(stateText(a.state));
    statusItem->setData(kStateRole, static_cast<int>(visualStateFor(a.state)));
    table->setItem(row, kColStatus, statusItem);
}

void setRowState(QTableWidget* table, int row, AlarmState st) {
    if (!table || row < 0 || row >= table->rowCount()) return;
    auto* statusItem = table->item(row, kColStatus);
    if (!statusItem) {
        // 兜底:状态列不该缺失。真缺了说明写行的那段代码漏了 setItem ——
        //   静默返回的话,操作员看到的是"确认了但状态没变",又是静默失败。
        qCWarning(hmiAlarm) << "[AlarmTablePresenter] 第" << row
                            << "行没有状态列 —— 写这一行的代码漏了 setItem";
        return;
    }
    statusItem->setText(stateText(st));
    statusItem->setData(kStateRole, static_cast<int>(visualStateFor(st)));
}

void repaintRow(QTableWidget* table, int row, bool blinkOn) {
    if (!table || row < 0 || row >= table->rowCount()) return;
    const RowColors c = colorsFor(rowLevel(table, row), rowVisualState(table, row), blinkOn);
    for (int col = 0; col < table->columnCount(); ++col) {
        if (auto* it = table->item(row, col)) {
            it->setBackground(c.bg);
            it->setForeground(c.fg);
        }
    }
}

bool rowShouldBlink(QTableWidget* table, int row) {
    if (!table || row < 0 || row >= table->rowCount()) return false;
    return shouldBlink(rowLevel(table, row), rowVisualState(table, row));
}

bool anyRowShouldBlink(QTableWidget* table) {
    if (!table) return false;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (rowShouldBlink(table, row)) return true;
    }
    return false;
}

AlarmLevel rowLevel(QTableWidget* table, int row) {
    auto* it = (table && row >= 0 && row < table->rowCount()) ? table->item(row, kColLevel)
                                                              : nullptr;
    if (!it) return AlarmLevel::Warning;
    const QVariant v = it->data(kLevelRole);
    if (!v.isValid()) {
        qCWarning(hmiAlarm) << "[AlarmTablePresenter] 第" << row
                            << "行等级列没有 UserRole —— 写这一行的代码漏了 setData";
        return AlarmLevel::Warning;
    }
    return static_cast<AlarmLevel>(v.toInt());
}

RowVisualState rowVisualState(QTableWidget* table, int row) {
    auto* it = (table && row >= 0 && row < table->rowCount()) ? table->item(row, kColStatus)
                                                              : nullptr;
    if (!it) return RowVisualState::Unacknowledged;
    const QVariant v = it->data(kStateRole);
    if (!v.isValid()) {
        qCWarning(hmiAlarm) << "[AlarmTablePresenter] 第" << row
                            << "行状态列没有 UserRole —— 写这一行的代码漏了 setData";
        return RowVisualState::Unacknowledged;
    }
    return static_cast<RowVisualState>(v.toInt());
}

int trimToLimit(QTableWidget* table, const QSet<qint64>& liveIds, int maxRows) {
    if (!table || maxRows <= 0) return 0;
    int dropped = 0;
    // ★ 从最旧的一行开始丢 —— 丢最旧的,保最新的。
    //   每丢一行,后面所有行的下标都要往前挪一格,所以从头重新扫
    //   (表最多几百行,这个 O(n²) 完全不疼;而"从后往前删"要处理下标乱序,
    //   那种写法容易在加了一行之后悄悄丢错行)。
    while (table->rowCount() > maxRows) {
        int victim = -1;
        for (int row = 0; row < table->rowCount(); ++row) {
            auto* nameItem = table->item(row, kColName);
            const qint64 id = nameItem ? nameItem->data(Qt::UserRole).toLongLong() : 0;
            if (!liveIds.contains(id)) { victim = row; break; }
        }
        if (victim < 0) {
            // ★ 全都是还在活动的报警 —— 一条都不丢。
            //   宁可表超上限,也不把操作员还没处理的报警从他眼前拿走。
            //   (本项目的原则:漏报比多报危险得多。)
            break;
        }
        table->removeRow(victim);
        ++dropped;
    }
    return dropped;
}

} // namespace qwhmi::alarm
