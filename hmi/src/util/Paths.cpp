// ============================================================================
// util/Paths.cpp —— 应用数据目录
//
// 实现要点:
//   - 覆盖开关 HMI_DATA_DIR:设了就用它,没设(或设成空)就走默认。
//     "设成空"必须当没设 —— 空串会算成"根目录",那是灾难性的默认。
//   - 默认路径用【常量】拼,不用 AppLocalDataLocation,
//     免得 org/app 的设置时机影响结果(理由见 Paths.h)。
// ============================================================================

#include "util/Paths.h"

#include <QDir>
#include <QStandardPaths>

namespace qwhmi::util {

QString appDataDir() {
    const QByteArray v = qgetenv("HMI_DATA_DIR");
    if (!v.isEmpty()) {
        return QDir::toNativeSeparators(QString::fromLocal8Bit(v));
    }
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + QLatin1Char('/') + QLatin1String(kOrgName)
         + QLatin1Char('/') + QLatin1String(kAppName);
}

} // namespace qwhmi::util
