// ============================================================================
// 2026-10-10 · 协议下拉框的"未实现"标注 —— 实现
//
// 关联:ProtocolAvailabilityUi.h, drivers/DriverFactory.{h,cpp}
// ============================================================================

#include "drivers/ProtocolAvailabilityUi.h"
#include "drivers/DriverFactory.h"

#include <QComboBox>
#include <QBrush>
#include <QColor>
#include <QVariant>
#include <QDebug>

namespace qwhmi::drivers {

namespace {
// 置灰用的灰:三通道接近,亮度在 100~200 之间。
// 太黑会跟"禁用"的深色主题撞,太亮则跟正常文字分分开看不出来 —— 两头都不合格。
constexpr int kGreyLevel = 0x9E;

QColor greyColor() { return QColor(kGreyLevel, kGreyLevel, kGreyLevel); }

// 去掉可能已经存在的后缀,避免重复调用时叠成 "S7(未实现)(未实现)"。
// 后缀常量在 DriverFactory.cpp 里(加后缀和拆后缀共用同一份)。
QString stripSuffix(const QString& text) {
    const QString& sfx = protocolUnimplementedSuffix();
    QString out = text;
    while (out.endsWith(sfx)) {
        out.chop(sfx.size());
        out = out.trimmed();
    }
    return out;
}
} // namespace

void applyProtocolAvailabilityStyling(QComboBox* combo) {
    if (!combo) {
        return;
    }
    for (int i = 0; i < combo->count(); ++i) {
        const QString base = stripSuffix(combo->itemText(i));
        if (DriverFactory::isUsable(base)) {
            // 可用项:顺手清掉可能存在的旧标注,保证这段可重复调用
            combo->setItemData(i, QVariant(), Qt::ForegroundRole);
            combo->setItemData(i, QVariant(), Qt::ToolTipRole);
            if (combo->itemText(i) != base) {
                combo->setItemText(i, base);
            }
            continue;
        }
        combo->setItemText(i, base + protocolUnimplementedSuffix());
        combo->setItemData(i, QBrush(greyColor()), Qt::ForegroundRole);
        combo->setItemData(i, DriverFactory::unavailableReason(base), Qt::ToolTipRole);
    }
}

} // namespace qwhmi::drivers
