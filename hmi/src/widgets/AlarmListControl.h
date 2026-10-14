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
// ★ 2026-10-14:配色决策已上移到公共层(两套报警 UI 各写了一套,必然漂)。
//   本头文件要用 qwhmi::alarm::RowVisualState 做私有方法的返回类型,所以必须包含。
#include "alarm/AlarmRowPalette.h"
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
// ★ 2026-10-14:updateAlarmRow 收 AlarmState、levelAt 返回 AlarmLevel。
//   只前向声明就够 —— 形参/返回值按值传,声明处不需要完整定义。
enum class AlarmState;
enum class AlarmLevel;

class QTableWidget;
class QGraphicsProxyWidget;
class QWidget;
class QLabel;
class QTimer;
// ★ 必须和上面几个一样声明在【全局作用域】。
//   写成 `class QPushButton*` 出现在 namespace qwhmi::widgets 内部的话,
//   等于把 QPushButton 注入本 namespace,声明出 qwhmi::widgets::QPushButton ——
//   于是 .cpp 里真正的 ::QPushButton 找不到,报的是
//   "使用了未定义类型 qwhmi::widgets::QPushButton"。
//   本文件头注释记的 elaborated 形式陷阱,同一个坑第二次踩到。
class QPushButton;

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

    // ★ 2026-10-14:一次确认全部未确认报警(按钮直接调这个)。
    //   【公开出来是为了让测试打的是生产代码】——
    //   按钮连的是同一个函数,不是测试专用入口。
    void acknowledgeAll();
    // ★ 2026-10-14:"全部确认"按钮此刻该不该可点(测试断言用)。
    //   没有未确认时它必须是灰的:一直亮着的按钮按下去没反应,像坏了。
    bool ackAllEnabled() const;

    // ===== ★ F3(2026-09-30):给测试/宿主看的只读访问器 =====
    //   为什么要暴露:计数、配色、确认这三样都是【看得见的行为】,
    //   断言只能落在它们身上 —— 反射 private 成员去断言,测的就成了实现细节,
    //   改个成员名就红,却证明不了用户看到的东西变了。
    int  unacknowledgedCount() const;         // 当前未确认条数(= 计数标签显示的值)
    QString countText() const;                // 计数标签的完整文字
    QString levelTextAt(int row) const;       // 第 row 行的等级文字
    QColor levelColorAt(int row) const;       // 第 row 行的等级底色(配色断言用)
    // ★ F3-b:第 row 行的字色。
    //   【为什么单独暴露】对比度是"底色 + 字色"两个数算出来的,
    //   只给底色就断言不了对比度 —— 而"闪烁时字看得清吗"恰恰是
    //   F3-b 最该守住的一条(见 repaintRow 里踩过的坑)。
    QColor levelTextColorAt(int row) const;
    int  rowCount() const;

    // ===== ★ F3-b(2026-10-08):闪烁 =====
    // 【为什么单列一组访问器】闪烁的可见效果有两个:
    //   「定时的灯是不是亮的」和「这一格现在是什么颜色」。
    //   后者 levelColorAt 已经能看,前者只能靠等 500ms ——
    //   而"等 500ms 再看一眼"在测试里既慢又不稳(负载一高就红)。
    //   所以额外暴露灯态和"该不该闪的行数",让这两件事可以被直接断言。
    bool blinkOn() const;                     // 当前处在闪烁的哪一相(亮/暗)
    int  blinkingRowCount() const;            // 当前【正在闪】的行数(未确认 + 严重/危急)
    bool blinkEnabled() const;                // 定时器是否在跑(没东西可闪时必须停)
    // ★ 手动走一次闪烁相位 —— 给测试用。
    //   【为什么不让测试干等 500ms】"睡 500ms 再看一眼颜色"这种测法
    //   既慢又不稳:CI 上一旦机器忙,500ms 里定时器可能没被派发,
    //   断言就会随机红 —— 而红的原因和被测逻辑毫无关系。
    //   这里暴露"走一相"的入口,断言的是【一相之后颜色确实变了】这件事本身。
    void advanceBlinkForTest();

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
    // ★ F3-b:500ms 一次,切亮/暗两相
    void onBlinkTick();

private:
    void addAlarmRow(const Alarm& alarm);
    // ★ 2026-10-14:收 AlarmState 而不是状态文字字符串 —— 文字与配色都由它派生,
    //   不存在"文字说已确认、配色说未确认"这种各说各话。详见 .cpp。
    void updateAlarmRow(qint64 id, AlarmState st);
    int  findRowByAlarmId(qint64 id) const;
    // ★ 2026-10-14:从行里读回等级 / 视觉状态(读 UserRole,不解析文字)。
    //   缺失时向"更像未处理报警"的方向兜底 + qCWarning,理由见 .cpp。
    AlarmLevel levelAt(int row) const;
    qwhmi::alarm::RowVisualState visualStateAt(int row) const;
    // ★ F3:重算"未确认"计数并刷新标签。所有会改变确认状态的路径都要过这里。
    void refreshCount();
    // ★ F3/F3-b:把某一行按等级+状态+闪烁相位重新上色。
    //   ★ 配色的【唯一入口】 —— 闪烁也必须走这里。
    //   闪烁若自己 setBackground,就成了第二个改颜色的地方:
    //   两处配色各改各的,必然出现"闪一下之后颜色停在错误档位"这类 bug。
    void repaintRow(int row);
    // ★ F3-b:这一行此刻该不该闪(未确认 + 严重/危急)。
    //   抽成函数是因为定时器启动、计数统计都要用同一个判据 ——
    //   两处各写一遍 if,改判据时漏一处就变成"闪的行和统计的行对不上"。
    bool rowShouldBlink(int row) const;
    // ★ F3-b:有该闪的行就把定时器开起来,一个都没有就停。
    //   【为什么必须停】没东西可闪还在跑,等于让整个进程醒着做无用功 ——
    //   而这套程序是要在工控机上 7×24 跑的,不是开完就关的桌面软件。
    void updateBlinkTimer();
    // ★ F3-b:行 → alarmId。之前只在 QHash 里,拿不到"按行号反查 id",
    //    而双击给的正是行号。
    qint64 alarmIdAtRow(int row) const;
    void   setAlarmIdAtRow(int row, qint64 id);

    // ★ F3-b(2026-10-08):闪烁
    //   【为什么 500ms】和 hmi.cpp:423 的 alarmBlinkTimer_ 取同一个值。
    //   两套报警 UI 各闪各的、速度还不一样,操作员的目光在两者间来回时
    //   会觉得"这边快了一点" —— 同一个产品里同类信号必须是同一个节奏。
    //   500ms 是"慢到不刺眼、快到余光能捕捉到"的那一档。
    QTimer* blinkTimer_ = nullptr;
    bool    blinkOn_    = false;   // 当前处在亮相还是暗相

    QWidget*              container_ = nullptr;   // ★ F3:承载 计数标签 + 表格 的父 widget
    QLabel*               countLabel_ = nullptr;  // ★ F3:未确认计数
    // ★ 2026-10-14:"全部确认"按钮。与 countLabel_ 同一行(计数在左,按钮在右)。
    QPushButton*           ackAllButton_ = nullptr;
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
