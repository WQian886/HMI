// ============================================================================
// 通讯帧监视(2026-10-20)—— CommTraceModel 实现
//
// 【数据层不含任何界面代码】理由见头文件。
// ============================================================================

#include "comm/CommTraceModel.h"

#include <QStringList>

namespace qwhmi::comm {

CommTraceModel::CommTraceModel(QObject* parent)
    : QAbstractListModel(parent) {
}

// ============================================================
// QAbstractListModel
// ============================================================
int CommTraceModel::rowCount(const QModelIndex& parent) const {
    // 有父索引说明问的是"子项",本模型是平表,没有子项
    if (parent.isValid()) return 0;
    return rows_.size();
}

int CommTraceModel::columnCount(const QModelIndex& parent) const {
    // 有父索引说明问的是"子项",本模型是平表,没有子项
    if (parent.isValid()) return 0;
    // ★ 不覆写的话默认是 1 —— 表格就只剩"时间"一列,
    //   而且 index(row, 1..4) 全部无效(data() 返回空 QVariant)。
    //   理由见头文件里那段。
    return ColumnCount;
}

QVariant CommTraceModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size())
        return QVariant();

    const FrameRecord& r = rows_.at(index.row());

    switch (role) {
        case DirRole:
            return static_cast<int>(r.dir);
        case ElapsedRole:
            return r.elapsedMs;
        case RawRole:
            return r.raw;
        default:
            break;
    }

    if (role != Qt::DisplayRole && role != Qt::ToolTipRole)
        return QVariant();

    switch (index.column()) {
        case ColTime:
            return r.timestamp.toString(QStringLiteral("HH:mm:ss.zzz"));
        case ColDir:
            return QString::fromUtf8(frameDirLabel(r.dir));
        case ColSummary:
            return r.summary;
        case ColElapsed:
            // 【为什么 -1 显示成短横而不是 0】
            //   0 也是一个合法耗时(本机回环常常 0~1ms)。
            //   显示成 0 会让人以为"测出来是 0ms",而真实含义是"这一行没有往返耗时"。
            return r.elapsedMs >= 0 ? QString::number(r.elapsedMs)
                                    : QStringLiteral("-");
        case ColRaw:
            // 跟 ModbusMaster 打日志用的格式一致(大写、空格分隔):
            // 界面上看到的和日志里搜到的必须是同一种写法,
            // 否则用户拿着界面上的串去日志里搜不到。
            return QString::fromLatin1(r.raw.toHex(' ').toUpper());
        default:
            return QVariant();
    }
}

QVariant CommTraceModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const {
    if (role != Qt::DisplayRole) return QVariant();
    if (orientation != Qt::Horizontal) return QVariant();

    switch (section) {
        case ColTime:    return QStringLiteral("时间");
        case ColDir:     return QStringLiteral("方向");
        case ColSummary: return QStringLiteral("摘要");
        case ColElapsed: return QStringLiteral("耗时(ms)");
        case ColRaw:     return QStringLiteral("原始字节");
        default:         return QVariant();
    }
}

// ============================================================
// 数据入口
// ============================================================
bool CommTraceModel::appendFrame(const FrameRecord& rec) {
    // 暂停中:一帧都不收,统计也不动。
    // 【为什么不"暂停但继续统计"】那样界面会显示"暂停中"却数字还在涨,
    //   用户会以为暂停没生效,或者以为统计坏了。
    if (!collecting_) return false;

    FrameRecord r = rec;
    if (!r.timestamp.isValid()) r.timestamp = QDateTime::currentDateTime();

    // 先腾位置再插 —— 反过来的话会超一行上限才去淘汰,
    // 而且 beginInsertRows 的区间算式要多绕一圈,容易写错。
    if (rows_.size() >= maxRows_) {
        dropOldest(rows_.size() - maxRows_ + 1);
    }

    beginInsertRows(QModelIndex(), rows_.size(), rows_.size());
    rows_.append(r);
    endInsertRows();

    // ---- 统计 ----
    switch (r.dir) {
        case FrameDir::Tx:
            ++txCount_;
            bytesOut_ += r.raw.size();
            break;
        case FrameDir::Rx:
            ++rxCount_;
            bytesIn_ += r.raw.size();
            break;
        case FrameDir::Error:
            ++errorCount_;
            // 错误帧也可能是收到的字节(异常响应),
            // 超时错误没有字节(raw 为空),加 0 不影响。
            bytesIn_ += r.raw.size();
            break;
    }

    // 往返耗时只统计【真的有值】的样本。
    // Tx / Error 一般是 -1,把它们当 0 参与平均会把平均值拉到假的 0。
    if (r.elapsedMs >= 0) {
        lastElapsedMs_ = r.elapsedMs;
        if (minElapsedMs_ < 0 || r.elapsedMs < minElapsedMs_) minElapsedMs_ = r.elapsedMs;
        if (maxElapsedMs_ < 0 || r.elapsedMs > maxElapsedMs_) maxElapsedMs_ = r.elapsedMs;
        elapsedSum_ += r.elapsedMs;
        ++elapsedSamples_;
    }

    emit statsChanged();
    return true;
}

void CommTraceModel::clear() {
    if (rows_.isEmpty()) return;
    beginRemoveRows(QModelIndex(), 0, rows_.size() - 1);
    rows_.clear();
    endRemoveRows();
    // 【统计刻意不清】理由见头文件:错误计数是本次运行的累计量,
    // 帧列表只是滚动窗口。抹掉它会让"持续报错"看起来像"刚启动"。
    emit statsChanged();
}

void CommTraceModel::setCollecting(bool on) {
    if (collecting_ == on) return;
    collecting_ = on;
    emit statsChanged();
}

void CommTraceModel::setMaxRows(int n) {
    // 下限 1:0 会让 appendFrame 每次进来先把全部行删掉再插一行,
    // 界面上看起来就是"永远只有一行而且在闪"。
    if (n < 1) n = 1;
    if (n == maxRows_) return;
    maxRows_ = n;
    if (rows_.size() > maxRows_) {
        dropOldest(rows_.size() - maxRows_);
    }
    emit statsChanged();
}

void CommTraceModel::dropOldest(int n) {
    if (n <= 0 || rows_.isEmpty()) return;
    if (n > rows_.size()) n = rows_.size();
    beginRemoveRows(QModelIndex(), 0, n - 1);
    rows_.remove(0, n);
    endRemoveRows();
}

// ============================================================
// 统计
// ============================================================
double CommTraceModel::avgElapsedMs() const {
    if (elapsedSamples_ <= 0) return -1.0;
    return double(elapsedSum_) / double(elapsedSamples_);
}

QString CommTraceModel::statsText() const {
    QString s = QStringLiteral("本次运行累计:发 %1 / 收 %2 / 错 %3 帧;字节 出 %4 / 入 %5")
                    .arg(txCount_)
                    .arg(rxCount_)
                    .arg(errorCount_)
                    .arg(bytesOut_)
                    .arg(bytesIn_);

    if (elapsedSamples_ > 0) {
        s += QStringLiteral(";往返 ms 最小 %1 / 平均 %2 / 最大 %3")
                 .arg(minElapsedMs_)
                 .arg(avgElapsedMs(), 0, 'f', 1)
                 .arg(maxElapsedMs_);
    } else {
        // 没有样本就说没有样本 —— 不写"0",免得跟"实测 0ms"分不清。
        s += QStringLiteral(";往返 尚无样本");
    }

    if (!collecting_) s += QStringLiteral("  【已暂停采集】");
    return s;
}

} // namespace qwhmi::comm
