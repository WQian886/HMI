// ============================================================================
// ★ 多 PLC 2b(2026-10-19):按 plcId 提供 driver 的查询接口
//
// 文件职责:给【需要按 plcId 找 driver】的调度层一个最小抽象。
//   眼下只有 WriteScheduler 用它(读侧的 PollScheduler 自己持有 drivers_ 哈希,
//   不走这个接口)。抽出接口而不是直接依赖 PlcConnectionManager,是为了:
//     · WriteScheduler 不依赖具体实现类,测试能塞一个 10 行的假 provider
//     · 依赖方向干净:hmi 拼装 → manager 实现接口 → 调度器消费接口
//
// 【为什么不是 std::function 注入】
//   仓库已有 IPollDriver 这个抽象,测试里也已经是 mock driver 的路线
//   (tst_IPollDriver 里的 MockDriver)。换成 std::function 只是把同一件事
//   换成弱类型,没有额外好处。
//
// 【设计约束】
//   · 纯接口,无 Q_OBJECT,不进 moc —— 它不是信号槽参与方
//   · 只暴露"只读查询"。建/拆 driver 是 PlcConnectionManager + hmi 的事,
//     调度器不该有权限去建连接
//
// 关联:PlcConnectionManager.h(实现), WriteScheduler.h(消费)
// ============================================================================

#pragma once

#include <QString>

namespace qwhmi::drivers {

class IPollDriver;

class IPlcDriverProvider {
public:
    virtual ~IPlcDriverProvider() = default;

    // 取这台 PLC 的 driver。找不到(没配 / 没建起来 / 已删除)返回 nullptr。
    //   调用方必须自己处理 nullptr —— 不许退化成"取第一个 driver":
    //   那正是 2b 要消灭的静默写错设备(给 plc2 的变量写进 plc1 的 socket)。
    virtual IPollDriver* driverFor(const QString& plcId) const = 0;

    // ★ 这台 PLC 的 Modbus 从站号(unit id)。
    //
    //   缺省 1。plcs[] 里没有 slave_id、或者 slave_id 不是整数,都回 1 ——
    //   【不回 0】:0 是 Modbus 广播地址,发出去会收到一堆没人认领的响应,
    //   ModbusMaster 按 TID 匹配不上就丢掉,用户只看到超时,而且日志里
    //   什么线索都没有。
    //
    //   两个调度器(PollScheduler 读 / WriteScheduler 写)都从这里取。
    //   【任何一处再出现字面量 1 就是 bug】—— 那是 2b 修掉的两个硬编码点。
    virtual int slaveIdFor(const QString& plcId) const = 0;
};

} // namespace qwhmi::drivers
