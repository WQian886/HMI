// ============================================================================
// 阶段 E · E.1 · AlarmListControl(数据 — 报警列表)
//
// 文件职责:用 QGraphicsProxyWidget 包 QTableWidget 显示 AlarmManager activeAlarms_
//   列:时间 / 报警名 / 等级 / 值 / 状态
//
// 关联:ControlBase.h, ../../Alarm.h, ../../AlarmManager.h
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"
#include <QHash>
#include <QtGlobal>   // qint64(QHash 已间接拉入,这里显式 include 更明确)

// ★ Alarm / AlarmManager / AlarmLevel 定义在全局命名空间(没 namespace 包裹)。
//   在 qwhmi::widgets 外面做前向声明,这样下面的 slot 形参 `const Alarm&`
//   在 qwhmi::widgets 内部查找时会穿透到全局,不会触发"qwhmi::widgets::Alarm 已声明"误报。
//   (原版本用 `const struct Alarm&` 的 elaborated 形式,会把 Alarm 注入到
//    qwhmi::widgets,导致 .cpp 实现 `const Alarm&` 找不到全局 Alarm。)
class AlarmManager;
struct Alarm;
class AlarmChecker;  // 不直接用,只为 moc 完整

class QTableWidget;
class QGraphicsProxyWidget;

namespace qwhmi::widgets {

class AlarmListControl : public ControlBase {
    Q_OBJECT
public:
    explicit AlarmListControl(QGraphicsItem* parent = nullptr);
    ~AlarmListControl() override;

    // ★ 绑定 AlarmManager — 监听 alarmTriggered/Recovered 信号
    void bindAlarmManager(AlarmManager* mgr);

    // 立即刷新一次(读 current activeAlarms_)
    void refreshNow();

    QRectF boundingRect() const override;
    void   paint(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

private slots:
    void onAlarmTriggered(const Alarm& alarm);
    void onAlarmRecovered(qint64 id);
    void onAlarmAcknowledged(qint64 id);
    void onAllCleared();

private:
    void addAlarmRow(const Alarm& alarm);
    void updateAlarmRow(qint64 id, const QString& stateText);
    int  findRowByAlarmId(qint64 id) const;

    QTableWidget*         table_ = nullptr;
    QGraphicsProxyWidget* proxy_ = nullptr;
    AlarmManager*         alarmMgr_ = nullptr;
    QHash<qint64, int>    idToRow_;   // alarm.id_ → table row
};

} // namespace qwhmi::widgets
