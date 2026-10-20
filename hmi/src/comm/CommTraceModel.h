#pragma once

// ============================================================================
// 通讯帧监视(2026-10-20)—— 帧列表 + 统计
//
// 文件职责:
//   输入:一帧一帧的事件(方向 / 原始字节 / 摘要 / 往返耗时)
//   输出:① 一个给 QTableView 用的列表模型
//         ② 一组统计(收发帧数 / 错误数 / 字节数 / 往返耗时 min/avg/max)
//
// 【为什么这个类是"数据层",界面一行都不碰】
//   本仓库的硬规矩:数据层方法里不许出现 QMessageBox。
//   理由见 AlarmConfigDialog / VariableConfigDialog 的头注释 ——
//   那些类把"要不要弹框"这个决定留给了界面层,于是同一份逻辑
//   能被对话框和自动化测试两边用。
//   本类比那两处还更严格:它连 QWidget 都不碰,纯 QtCore。
//   所以 tst_CommTraceModel 可以在无界面的测试里跑。
//
// 【为什么用 QAbstractListModel 而不是直接往 QTableWidget 里塞行】
//   ① QTableWidget 塞行是 O(n) 拷贝,几百行就开始卡;
//      模型是 beginInsertRows,插一行是 O(1)。
//   ② 行数上限(滚动淘汰最旧的)用模型做是 beginRemoveRows(0,0),
//      视图会自动更新;用 QTableWidget 做要自己管行号偏移,最容易写错。
//   ③ 模型能脱开界面单测 —— 这才是关键:
//      "行数超限后最旧的那行真的被淘汰了吗"这种断言,
//      只有在有模型的时候才写得出来。
//
// 【统计为什么不受"清空"影响】
//   clear() 只清帧列表,不清统计。理由:
//     错误计数是【本次运行的累计量】,而帧列表只是一个【滚动窗口】。
//     用户点"清空"的意图是"我要看新的一批帧",不是"把错误总数也抹掉"。
//     抹掉的话,一个持续报错的东西会看起来像刚启动 ——
//     而这正是"只看眼前"最容易骗人的地方。
//   所以统计标签上写的是"本次运行累计",统计要归零得重启程序。
//
// 关联:comm/CommFrame.h(方向枚举),ModbusMaster.h(帧的生产者)
// ============================================================================

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QVector>

#include "comm/CommFrame.h"

namespace qwhmi::comm {

// 一帧的记录
struct FrameRecord {
    QDateTime timestamp;          // 无效则由模型补当前时间
    FrameDir   dir      = FrameDir::Tx;
    QString    summary;            // 人话摘要,如 "FC03 读保持寄存器 addr=0x0000 count=2"
    qint64     elapsedMs = -1;     // 往返耗时(ms);Tx / Error 一般是 -1
    QByteArray raw;               // 原始字节(整帧)
};

class CommTraceModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Column {
        ColTime    = 0,   // 时间
        ColDir     = 1,   // 方向
        ColSummary = 2,   // 摘要
        ColElapsed = 3,   // 往返耗时(ms)
        ColRaw     = 4,   // 原始字节
        ColumnCount
    };

    // 附加角色:给视图做行着色(按方向上色)用
    enum Role {
        DirRole     = Qt::UserRole + 1,   // int(FrameDir)
        ElapsedRole = Qt::UserRole + 2,   // qint64
        RawRole     = Qt::UserRole + 3,   // QByteArray
    };

    explicit CommTraceModel(QObject* parent = nullptr);

    // ---- QAbstractListModel ----
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    /// ★★ 列数。**必须**覆写,否则整张表只有一列。
    ///   QAbstractListModel 的默认列数是 1(它本来是给单列列表用的),
    ///   而 QAbstractListModel::index() 走 hasIndex(),后者会检查
    ///   `column < columnCount()` —— 不覆写的话:
    ///     ① index(row, 1..4) 全部【无效】,data() 直接返回空 QVariant;
    ///     ② QTableView 只会画出【一列】,其余四列根本不会出现。
    ///   界面上看到的会是"表格里只有时间,别的列没有" ——
    ///   不报错、不崩溃,只是少东西。
    ///   ★ 这个 bug 是 tst_CommTraceModel 第一次跑就抓出来的(6 条红),
    ///     长得和"数据没进来"一模一样:data() 全返回空。
    ///     而它其实是少了一个虚函数。
    ///
    ///   ★★ 另一个坑:Qt 里 QAbstractListModel::columnCount 是【private】的
    ///     (qabstractitemmodel.h:499,在 private 段里)。
    ///     所以这个覆写必须放在本类的 public 段 ——
    ///     放 protected 的话,连测试里 m.columnCount() 都调不了
    ///     (C2660:找不到接受 0 个参数的函数)。
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    // ---- 数据入口 ----

    /// 追加一帧。返回 false = 没收下(暂停中)。
    /// 【为什么返回 bool】界面要能告诉用户"这一帧没进来"(暂停时),
    ///   静默丢弃的话用户会以为程序没在通信。
    bool appendFrame(const FrameRecord& rec);

    /// 清空帧列表。【不清统计】—— 理由见文件头。
    void clear();

    /// 暂停/继续采集。暂停期间 appendFrame 一律不收(统计也不动)。
    bool isCollecting() const { return collecting_; }
    void setCollecting(bool on);

    /// 帧列表上限。超过就淘汰【最旧的】。
    /// 【为什么必须有上限】Modbus 轮询 100ms 一帧,跑一夜就是 30 万行 ——
    ///   无限增长会把手柄内存吃光,而这恰恰是"长跑要验的东西"之一。
    int  maxRows() const { return maxRows_; }
    void setMaxRows(int n);

    // ---- 统计(本次运行累计,不受 clear() 影响)----
    int    txCount()    const { return txCount_; }
    int    rxCount()    const { return rxCount_; }
    int    errorCount() const { return errorCount_; }
    qint64 bytesOut()   const { return bytesOut_; }
    qint64 bytesIn()    const { return bytesIn_; }

    /// 往返耗时。样本为 0 时 min/max/last 都是 -1,avg 也是 -1。
    /// 【为什么用 -1 而不是 0】0 是一个合法的耗时(本机回环),
    ///   拿它当"没有样本"会让"最快 0ms"和"没测过"分不清。
    qint64 lastElapsedMs() const { return lastElapsedMs_; }
    qint64 minElapsedMs()  const { return minElapsedMs_; }
    qint64 maxElapsedMs()  const { return maxElapsedMs_; }
    double avgElapsedMs()  const;

    /// 给界面标签用的一行摘要
    QString statsText() const;

signals:
    /// 统计变了(界面刷新标签用)
    void statsChanged();

private:
    void dropOldest(int n);

    QVector<FrameRecord> rows_;
    int  maxRows_    = 500;
    bool collecting_ = true;

    int    txCount_    = 0;
    int    rxCount_    = 0;
    int    errorCount_ = 0;
    qint64 bytesOut_   = 0;
    qint64 bytesIn_    = 0;

    qint64 lastElapsedMs_ = -1;
    qint64 minElapsedMs_  = -1;
    qint64 maxElapsedMs_  = -1;
    qint64 elapsedSum_    = 0;
    int    elapsedSamples_ = 0;
};

} // namespace qwhmi::comm
