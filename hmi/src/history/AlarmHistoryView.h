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
//   ✅ 区域过滤(2026-10-10 F6 补上 —— 拍板过程见下)
//
// ★ 为什么等级那一栏叫"最低等级"而不是"等级"
//   AlarmRecorder::query 的第三个参数是 minLevel —— 它是【下限】,
//   不是集合。选"严重及以上"会把危急一起带出来。
//   如果界面上写"等级 = 严重",用户会以为危急的记录被弄丢了,
//   而实际上只是没显示。这是界面在撒谎,比缺功能更糟。
//
// ★ 【区域】的拍板过程(2026-10-10 Wqian 决定,留档免得下一个人重新推一遍)
//   当时留的三个选项:a) 加 area 字段改 schema / b) 暂不做 /
//   c) 拿画面当区域复用。选了 a 的一个变体:【手填 + 历史值补全】。
//   理由三条:
//     ① 不复用 plcId —— plcId 是【通信归属】不是【组织归属】。
//        一台 PLC 常同时带一号泵房和二号泵房,一个区域也可能横跨两台 PLC。
//        plcId 经常不是一个正确的区域;一个经常筛错的筛选框比没有更糟。
//     ② 不做工程级区域清单 —— 自由文本是清单方案的【真子集】:
//        现在只多一列 TEXT,将来想收窄成下拉清单,只改 UI 不动 schema;
//        反过来先做清单、日后发现多数项目只有两三个区域,就得迁移第二次。
//     ③ 老库迁移不再是障碍 —— F2 已建好探测式加列
//        (AlarmRecorder::addColumnIfMissing),加 TEXT 列是它的常规用法。
//
//   【落地时冒出来的一个额外要求】
//     老库里【全部】记录的 area 都是 NULL。用户选"一号泵房"会得到 0 行,
//     他会以为"没有历史" —— 那是拿筛选器制造了一次静默失败。
//     所以下拉框里必须有"未分配"这一项,而且必须真的能筛出来。
//     区分靠 QString 的 null(不筛)/ 空串(筛未分配),见 AlarmRecorder::unassigned()。
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
    QString areaAt(int row) const;           // ★ F6:区域列(未分配显示"未分配")
    QString statusText() const;              // 底部状态标签,如"共 12 条"

    // 筛选条件的只读出口(测试要能直接验"我改了时间范围,查出来的条数变了")
    QDateTime from() const;
    QDateTime to() const;
    int minLevel() const;
    // ★ F6:当前选中的区域。null = 不筛,空串 = 只看未分配,其余 = 那个区域名。
    //   直接透传 AlarmRecorder::query 的那套语义,中间不翻译 ——
    //   界面一旦自己发明一套"空 = 未分配"的表示,两边的空就不是同一个空了。
    QString selectedArea() const;
    void    setAreaFilter(const QString& area);   // 测试用:按区域筛

private:
    void setupUi();
    void setupFilterRow(QWidget* parent, QVBoxLayout* layout);
    void applyShortRange(int hours);         // "最近 N 小时"快捷按钮
    // ★ F6:下拉框重建。历史是只增不减的,开一次下拉不能全表扫 ——
    //   数据源走 AlarmRecorder::distinctAreas()。
    void reloadAreaCombo();

    AlarmRecorder* recorder_ = nullptr;
    QDateTimeEdit*  fromEdit_     = nullptr;
    QDateTimeEdit*  toEdit_       = nullptr;
    QComboBox*      levelCombo_   = nullptr;
    QComboBox*      areaCombo_    = nullptr;   // ★ F6
    // ★ F6:areaCombo_ 每一项对应的【真实区域值】,下标一一对应。
    //   索引 0 是 QString()(null = 不筛),"未分配"那一项是空串(非 null)。
    //   ★ 刻意【不】用 QComboBox::itemData 存:QVariant 比较把 null 和空串
    //     判成相等,于是 findData(空串)会命中 null 那项,"筛未分配"静默
    //     退化成"不筛"。旁路一份,这个区别才活得下来。
    QStringList     areaValues_;
    QPushButton*    queryBtn_     = nullptr;
    QTableWidget*   table_        = nullptr;
    QLabel*         statusLabel_  = nullptr;
};

} // namespace qwhmi::history