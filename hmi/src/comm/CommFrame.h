#pragma once

// ============================================================================
// 通讯帧监视(2026-10-20)—— 帧方向的定义
//
// 【为什么单独拆一个头文件】
//   两边都要用这个枚举:
//     · ModbusMaster(项目根,hmi/ModbusMaster.h)—— 帧的【生产者】
//     · CommTraceModel(src/comm/CommTraceModel.h)—— 帧的【消费者】
//   而 ModbusMaster 那个文件在项目根、CommTraceModel 在 src/comm/,
//   两边互相 include 对方的头文件会成环。
//   所以方向枚举放这里:它没有任何依赖,谁都能 include。
//
// 【为什么是 enum class 而不是 int】
//   方向只有三种,用一个 int 传来传去迟早有人把 1 写成 2。
//   enum class 的隐式转换被禁掉了 —— 要传就写 FrameDir::Rx,写错编译不过。
//
// 【关于信号里带枚举】
//   本项目【全部在主线程】(单线程架构,全仓没有一个 QThread),
//   所以连接都是直接连接,枚举不需要 qRegisterMetaType。
//   ★ 如果哪天把这个类搬进线程,必须补:
//       qRegisterMetaType<qwhmi::comm::FrameDir>();
//     忘了的话症状是:编译链接全过,运行时那句连接【静默不生效】——
//     帧永远不显示,界面上一片空白,而没有任何报错。
//     这跟仓库里已经踩过的 "moc 只认 slots: 段" 是同一类坑。
// ============================================================================

namespace qwhmi::comm {

enum class FrameDir {
    Tx,      // 发出(请求)
    Rx,      // 收到(响应)
    Error,   // 出错(超时 / 异常响应 / 帧对不上)
};

// 界面上的中文名。inline 是为了不为此单开一个 .cpp。
inline const char* frameDirLabel(FrameDir d) {
    switch (d) {
        case FrameDir::Tx:    return "发送";
        case FrameDir::Rx:    return "接收";
        case FrameDir::Error: return "错误";
    }
    return "未知";
}

} // namespace qwhmi::comm
