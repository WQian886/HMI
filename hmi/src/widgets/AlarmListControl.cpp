// ============================================================================
// AlarmListControl 实现
// ============================================================================

#include "widgets/AlarmListControl.h"
#include "../../Alarm.h"
#include "../../AlarmManager.h"

#include <QColor>
#include <QGraphicsProxyWidget>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>

namespace qwhmi::widgets {

AlarmListControl::AlarmListControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    setSize(QSizeF(320, 100));
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);

    table_ = new QTableWidget;
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({"时间", "报警名", "等级", "值", "状态"});
    table_->setMinimumSize(static_cast<int>(size().width()),
                          static_cast<int>(size().height()));
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);   // 只读

    proxy_ = new QGraphicsProxyWidget(this);
    proxy_->setWidget(table_);

    registerControlType("AlarmList", []() -> ControlBase* { return new AlarmListControl(); });
}

AlarmListControl::~AlarmListControl() = default;

void AlarmListControl::bindAlarmManager(AlarmManager* mgr) {
    if (alarmMgr_) return;
    alarmMgr_ = mgr;
    if (!alarmMgr_) return;
    connect(alarmMgr_, &AlarmManager::alarmTriggered, this, &AlarmListControl::onAlarmTriggered);
    connect(alarmMgr_, &AlarmManager::alarmRecovered, this, &AlarmListControl::onAlarmRecovered);
    connect(alarmMgr_, &AlarmManager::alarmAcknowledged, this, &AlarmListControl::onAlarmAcknowledged);
    connect(alarmMgr_, &AlarmManager::allCleared, this, &AlarmListControl::onAllCleared);
    refreshNow();
}

void AlarmListControl::refreshNow() {
    if (!alarmMgr_ || !table_) return;
    table_->setRowCount(0);
    idToRow_.clear();
    // Alarm 在全局命名空间(Alarm.h 没有 namespace 包裹)
    // 头文件用裸 Alarm(配前向声明),这里也用裸名 — 编译器穿透 qwhmi::widgets 找到 ::Alarm
    for (const Alarm& a : alarmMgr_->activeAlarms()) {
        addAlarmRow(a);
    }
}

void AlarmListControl::addAlarmRow(const Alarm& alarm) {
    if (!table_) return;
    const int row = table_->rowCount();
    table_->insertRow(row);
    idToRow_[alarm.id_] = row;

    table_->setItem(row, 0, new QTableWidgetItem(alarm.triggeredAt.toString("HH:mm:ss")));
    table_->setItem(row, 1, new QTableWidgetItem(alarm.name));
    // 等级数字 → 字符串
    const char* lvl = "?";
    switch (alarm.level) {
        case AlarmLevel::Info:     lvl = "Info"; break;
        case AlarmLevel::Warning:  lvl = "Warning"; break;
        case AlarmLevel::Critical: lvl = "Critical"; break;
        case AlarmLevel::Emergency:lvl = "Emergency"; break;
    }
    auto* lvlItem = new QTableWidgetItem(lvl);
    if (alarm.level == AlarmLevel::Critical || alarm.level == AlarmLevel::Emergency) {
        lvlItem->setBackground(QColor(255, 100, 100));
        lvlItem->setForeground(QColor(255, 255, 255));
    }
    table_->setItem(row, 2, lvlItem);
    table_->setItem(row, 3, new QTableWidgetItem(alarm.triggerValue));
    table_->setItem(row, 4, new QTableWidgetItem("未确认"));
}

int AlarmListControl::findRowByAlarmId(qint64 id) const {
    return idToRow_.value(id, -1);
}

void AlarmListControl::onAlarmTriggered(const Alarm& alarm) {
    addAlarmRow(alarm);
}

void AlarmListControl::onAlarmRecovered(qint64 id) {
    updateAlarmRow(id, "已恢复");
}

void AlarmListControl::onAlarmAcknowledged(qint64 id) {
    updateAlarmRow(id, "已确认");
}

void AlarmListControl::updateAlarmRow(qint64 id, const QString& stateText) {
    const int row = findRowByAlarmId(id);
    if (row < 0 || !table_) return;
    auto* item = table_->item(row, 4);
    if (item) item->setText(stateText);
}

void AlarmListControl::onAllCleared() {
    if (!table_) return;
    table_->setRowCount(0);
    idToRow_.clear();
}

QRectF AlarmListControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void AlarmListControl::paint(QPainter* p,
                             const QStyleOptionGraphicsItem* option,
                             QWidget* widget) {
    Q_UNUSED(p); Q_UNUSED(option); Q_UNUSED(widget);
    // ★ proxy 自己渲染 — paint 不画东西
}

} // namespace qwhmi::widgets
