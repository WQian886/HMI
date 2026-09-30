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
class QWidget;
class QLabel;

namespace qwhmi::widgets {

class AlarmListControl : public ControlBase {
    Q_OBJECT
public:
    explicit AlarmListControl(QGraphicsItem* parent = nullptr);
    ~AlarmListControl() override;

    // ★ 绑定 AlarmManager — 监听 alarmTriggered/Recovered 信号
    //   【F3 注意】现在也可以走基类的 setAlarmManager()(自动断连旧 manager),
    //   直接调本函数则不会 —— 重复 bind 会被 if(alarmMgr_) return 挡掉。
    //   RuntimeCanvas 用的是 setAlarmManager 那条路。
    void bindAlarmManager(AlarmManager* mgr);

    // 立即刷新一次(读 current activeAlarms_)
    void refreshNow();

    // ===== ★ F3(2026-09-30):给测试/宿主看的只读访问器 =====
    //   为什么要暴露:计数、配色、确认这三样都是【看得见的行为】,
    //   断言只能落在它们身上 —— 反射 private 成员去断言,测的就成了实现细节,
    //   改个成员名就红,却证明不了用户看到的东西变了。
    int  unacknowledgedCount() const;         // 当前未确认条数(= 计数标签显示的值)
    QString countText() const;                // 计数标签的完整文字
    QString levelTextAt(int row) const;       // 第 row 行的等级文字
    QColor levelColorAt(int row) const;       // 第 row 行的等级底色(配色断言用)
    int  rowCount() const;

    QRectF boundingRect() const override;
    void paintContent(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

protected:
    // ★ F3:运行时模式切换 → 决定代理吃不吃鼠标事件(见 .cpp 里与 E.4b 的冲突说明)
    void onRuntimeModeChanged(bool runtime) override;
    // ★ F3:拿到 / 失去 AlarmManager → 重新连信号、清空表格
    void onAlarmManagerChanged(AlarmManager* mgr) override;

private slots:
    void onAlarmTriggered(const Alarm& alarm);
    void onAlarmRecovered(qint64 id);
    void onAlarmAcknowledged(qint64 id);
    void onAllCleared();
    // ★ F3:双击某一行 → 确认这条报警
    void onCellDoubleClicked(int row, int col);

private:
    void addAlarmRow(const Alarm& alarm);
    void updateAlarmRow(qint64 id, const QString& stateText);
    int  findRowByAlarmId(qint64 id) const;
    // ★ F3:重算"未确认"计数并刷新标签。所有会改变确认状态的路径都要过这里。
    void refreshCount();
    // ★ F3:把某一行按等级+状态重新上色(配色与闪烁的唯一入口)
    void repaintRow(int row);
    // ★ F3:行 → alarmId。之前只在 QHash 里,拿不到"按行号反查 id",
    //    而双击给的正是行号。
    qint64 alarmIdAtRow(int row) const;
    void   setAlarmIdAtRow(int row, qint64 id);

    QWidget*              container_ = nullptr;   // ★ F3:承载 计数标签 + 表格 的父 widget
    QLabel*               countLabel_ = nullptr;  // ★ F3:未确认计数
    QTableWidget*         table_ = nullptr;
    QGraphicsProxyWidget* proxy_ = nullptr;
    AlarmManager*         alarmMgr_ = nullptr;
    QHash<qint64, int>    idToRow_;   // alarm.id_ → table row

    // ★ F3:反向索引 row → alarmId。
    //   【为什么两个方向都要】idToRow_ 是"信号来了,这条 id 在第几行",
    //   方向是 id → row;而双击只给行号,要拿 id 去调 acknowledge,方向是 row → id。
    //   只留一个方向,另一个就得靠遍历全表找 —— 那正是 hmi.cpp 现在
    //   onAlarmRecovered / alarmAcknowledged 的做法(F1 之前还因此出过错配)。
    QHash<int, qint64>     rowToId_;
};

} // namespace qwhmi::widgets
