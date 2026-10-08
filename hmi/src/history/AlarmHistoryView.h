// ============================================================================
// 阶段 F · F4(2026-10-08):报警历史查询视图
//
// 文件职责:把 alarm_history 表里的东西【查出来给人看】。
//   落库那一半 D.3 就做完了(AlarmRecorder),但从 D.3 到现在,
//   主窗口那个"报警历史"tab 一直是个占位 Label ——
//   【存了但看不见】,而看不见的历史等于没有历史。
//
// ★ 这一版做到了什么、没做到什么(先说清楚,免得下一个人以为做完了)
//   ✅ 时间范围过滤
//   ✅ 等级过滤(准确说是"最低等级",见下方说明)
//   ❌ 区域过滤 —— 整个代码库里【没有"区域"这个概念】
//      (Alarm 结构里没有这个字段,工程文件里也没有)。
//      要支持就得先给报警加字段,那是会动工程文件格式的设计决定,
//      不是界面层能顺手加的东西。详见下方"区域"一节。
//
// ★ 为什么等级那一栏叫"最低等级"而不是"等级"
//   AlarmRecorder::query 的第三个参数是 minLevel —— 它是【下限】,
//   不是集合。选"严重及以上"会把危急一起带出来。
//   如果界面上写"等级 = 严重",用户会以为危急的记录被弄丢了,
//   而实际上只是没显示。这是界面在撒谎,比缺功能更糟。
//
//   【区域】的取舍留在这里,等 Wqian 拍板:
//     a) 给 Alarm + 工程文件加 area 字段(改 schema,有迁移问题)
//     b) 暂时只按时间/等级过滤,"区域"等真有需求再说
//     c) 拿画面(screen)当区域复用 —— 但报警和画面目前没有关联
//   在拍板之前,界面上【不出现】"区域"这个字眼,
//   免得用户去找一个还不存在的功能。
//
//   关联:./AlarmRecorder.h, ../../Alarm.h
// ============================================================================

#pragma once

#include <QWidget>
#include <QDateTime>

class QComboBox;
class QDateTimeEdit;
class QLabel;
class QPushButton;
class QTableWidget;
class QVBoxLayout;
// ★ QVBoxLayout 的前向声明必须写在【命名空间外面】。
//   写在参数里写成 `class QVBoxLayout*` 的话,它声明的是
//   qwhmi::history::QVBoxLayout —— 一个同名的新类型,
//   于是 .cpp 里 #include 来的真 QVBoxLayout 和它对不上,
//   报"未定义类型 qwhmi::history::QVBoxLayout",而报错点离病根很远。

namespace qwhmi::history {

class AlarmRecorder;

class AlarmHistoryView : public QWidget {
    Q_OBJECT
public:
    // recorder 不接管生命周期(主窗口持有)。传 nullptr 时整个界面 disabled ——
    // 界面能开但什么都查不出来,用户只会以为"没报警",而真相是数据库没开。
    explicit AlarmHistoryView(AlarmRecorder* recorder, QWidget* parent = nullptr);

    // ★ 给测试用的只读出口。
    //   反射 private 成员去断言的话,测的就是实现细节,改个成员名就红,
    //   却证明不了界面真的对 —— F3 在 AlarmListControl 上已经踩过这条。
    void   refresh();                       // 按当前筛选条件重查并刷新表格
    int    rowCount() const;
    QString timeAt(int row) const;
    QString nameAt(int row) const;
    QString levelTextAt(int row) const;
    QString actionTextAt(int row) const;
    QString variableAt(int row) const;
    QString statusText() const;              // 底部状态标签,如"共 12 条"

    // 筛选条件的只读出口(测试要能直接验"我改了时间范围,查出来的条数变了")
    QDateTime from() const;
    QDateTime to() const;
    int minLevel() const;

private:
    void setupUi();
    void setupFilterRow(QWidget* parent, QVBoxLayout* layout);
    void applyShortRange(int hours);         // "最近 N 小时"快捷按钮

    AlarmRecorder* recorder_ = nullptr;
    QDateTimeEdit*  fromEdit_     = nullptr;
    QDateTimeEdit*  toEdit_       = nullptr;
    QComboBox*      levelCombo_   = nullptr;
    QPushButton*    queryBtn_     = nullptr;
    QTableWidget*   table_        = nullptr;
    QLabel*         statusLabel_  = nullptr;
};

} // namespace qwhmi::history