// ============================================================================
// src/ui/CapabilityReport.h —— "当前支持的功能"对话框的【唯一出处】
//
// ============================================================================
// 【为什么抽出来】
//   原来这段文字【整段硬编码在 hmi.cpp 的 onShowCapabilities 里】。
//   而 hmi.cpp 不在 hmi_core 里 ⇒ 没有任何一条测试能看见它 ⇒
//   它说什么就是什么 —— 于是它【说谎了】,而且是三处(2026-11-02 查出来的):
//
//     ✗ "多 PLC:可以配好存进工程文件,<b>但目前只连第 1 台</b>"
//     ✗ "多台 PLC 同时在线 —— 在做"
//     ✗ "界面上直接加/删 PLC —— 在做"
//
//   而这三件事【早就做完了】:
//     · PlcConnectionManager::applyConfig 会按工程里的 plcs 数组建 N 台 driver
//       —— tst_PlcConnectionManager::threePlcs_getThreeDistinctDrivers 钉着;
//     · 菜单「PLC → PLC 配置」里有【添加】/【删除】按钮(PlcConfigDialog);
//     · 菜单「数据 → 变量配置」里能把变量分给哪台 PLC(VariableConfigDialog)。
//
//   ⇒ "界面上写着'在做',而界面上早就有了" ——
//     用户按说明去找,找不到,只会以为是自己没找到菜单,
//     于是【整个功能被当成不存在】。
//     这跟"界面在撒谎"是同一类问题,DriverFactory::isUsable 那次也是它
//     (见 src/drivers/DriverFactory.h 里 2026-10-10 那一段)。
//
// 【抽出来之后】
//   ① 文字只有一份,hmi.cpp 与测试调的是【同一个函数】;
//   ② 跟运行时有关的部分(在线几台 PLC、S7/EIP 能不能用)由【事实】驱动,
//     不是写死的 —— 写死的话,哪天协议真接上了,这句话还在骗人;
//   ③ 单测钉住"不许再出现那三句谎话",
//     也钉住"新建工程确实还没有"(这条【现在是真的】,不许顺手删掉)。
//
// 【★ 仍然为真的那些,一个字都没动】
//   · "22 种控件" —— 2026-11-02 核过:src/widgets/ 下 registerControlType
//     注册的类型正好 22 个(AlarmList … Valve)。
//   · "西门子 S7 / AB EtherNet-IP —— 下拉框里是灰的" —— 真的:
//     DriverFactory::isUsable 只认 modbus,其余两个是骨架,
//     下拉框里被置灰并加了"(未实现)"后缀(tst_ProtocolComboGrey 钉着)。
//   · "新建工程(现在只能打开/保存已有工程)" —— 真的:
//     MenuBuilder.cpp:138 明确没做这一项,tst_MenuBar 还扫全菜单
//     禁止出现"新建"字样。
//
// 关联:hmi.cpp(onShowCapabilities)、src/plc/PlcConnectionManager.h、
//      src/drivers/DriverFactory.h、tst_CapabilityReport
// ============================================================================

#pragma once

#include <QString>

namespace qwhmi::ui {

// ★ 能力清单的【事实快照】—— 由调用方从真实运行状态取,不是写死的。
struct CapabilityFacts {
    // 当前在线的 PLC 台数(PlcConnectionManager::driverCount())
    int  plcCount = 0;

    // 协议能不能【真用】(DriverFactory::isUsable)——
    //   S7 / EtherNet-IP 至今是骨架,isUsable 恒 false。
    bool s7Usable  = false;
    bool eipUsable = false;
};

// ★ 渲染"当前支持的功能"对话框的正文(HTML 片段)。
//
//   【为什么正文不整个塞进参数】
//     静态部分(哪些功能做完了、菜单在哪儿)写在本模块里 ——
//     那是【代码事实】,只有一处,改了这里测试就会知道。
//     动态部分(在线几台 PLC、协议能不能用)由参数带进来 ——
//     那是【运行时事实】,调用方取。
QString buildCapabilitiesHtml(const CapabilityFacts& facts);

} // namespace qwhmi::ui
