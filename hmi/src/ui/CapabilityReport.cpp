// ============================================================================
// src/ui/CapabilityReport.cpp —— 见头文件"为什么抽出来"
//
//   【一句话】这段文字以前硬编码在 hmi.cpp 里,没人能测它,于是它说了三年假话。
//
// 关联:CapabilityReport.h(设计说明)、hmi.cpp(onShowCapabilities)
// ============================================================================

#include "ui/CapabilityReport.h"

#include <QStringList>

namespace qwhmi::ui {

QString buildCapabilitiesHtml(const CapabilityFacts& facts) {
    QStringList out;

    // ---- 能用的 ----------------------------------------------------------
    out << QStringLiteral("<h3>✅ 现在就能用</h3>");
    out << QStringLiteral("<ul>");
    out << QStringLiteral("<li>连接 Modbus TCP 的 PLC(填 IP + 端口)</li>");
    out << QStringLiteral("<li>读变量:温度 / 压力 / 转速等,绑到画面控件上</li>");
    out << QStringLiteral("<li>写变量:手动改一个可写值发回 PLC</li>");
    // ★ 报警这一条原来写的是"上下限、超时、确认、声音、导出"。
    //   "超时"不是一个独立的报警类型 —— 报警只有两类来源:
    //   阈值越限(ThresholdAlarmChecker)和写失败转报警(WriteFailChecker,
    //   通信超时走的是这一条)。写"超时"会让人以为有个"超时报警"
    //   可以单独配,配了找不到 —— 那是另一种撒谎。
    //   ⇒ 按【实际有的】写:上下限、写失败转报警、确认、声音、导出、报警历史查询。
    out << QStringLiteral("<li>报警:上下限、写失败转报警、确认、声音、导出、报警历史查询</li>");
    out << QStringLiteral("<li>画面编辑:22 种控件,拖拽排版、撤销重做、复制粘贴</li>");
    out << QStringLiteral("<li>数据导出:变量表、报警表存成 CSV(交接班/报表用)</li>");

    // ---- 多 PLC:这一段原来在说假话 -------------------------------------
    // ★ 原来这里是:
    //     "<li>多 PLC:可以配好存进工程文件,<b>但目前只连第 1 台</b></li>"
    //   以及"还没做"里的两条:
    //     "多台 PLC 同时在线 —— 在做"
    //     "界面上直接加/删 PLC —— 在做"
    //   三句【全是假的】(2026-11-02 核过,见头文件)。
    //   现在写的是真的,而且把【入口在哪儿】也写出来 ——
    //   一个用户要能验证的功能,他得知道去哪儿找它。
    out << QStringLiteral(
        "<li><b>多 PLC:能加、能同时在线</b> —— "
        "菜单「PLC → PLC 配置」里加/删 PLC,"
        "「数据 → 变量配置」里把变量分给哪台 PLC;"
        "工程文件里配了几台就同时连几台(当前在线 <b>%1</b> 台)</li>")
              .arg(facts.plcCount);

    out << QStringLiteral("</ul>");

    // ---- 还没做完的 ------------------------------------------------------
    // ★ 原来这段的标题是"⏳ 还没做(菜单里不出现,免得你反复试)"——
    //   那句话对"新建工程"成立,但对 S7/EIP 不成立:
    //   它们【出现在下拉框里】,只是被置灰了。
    //   ⇒ 标题改成不承诺具体形态的说法。
    out << QStringLiteral("<h3>⏳ 还没做完的(说清楚,免得你反复试)</h3>");
    out << QStringLiteral("<ul>");

    // S7 / EtherNet-IP:能不能用由 DriverFactory::isUsable 说了算。
    //   ★ 【为什么不由这里写死】
    //     写死"灰的"的话,哪天真接上了(Snap7 / libplctag 落地),
    //     这句话还在骗人 —— 而这个模块存在的全部意义就是"不许骗人"。
    if (facts.s7Usable && facts.eipUsable) {
        out << QStringLiteral("<li>西门子 S7 / AB EtherNet-IP —— 已支持,可以在下拉框里选</li>");
    } else {
        QStringList greyed;
        if (!facts.s7Usable)  greyed << QStringLiteral("西门子 S7");
        if (!facts.eipUsable) greyed << QStringLiteral("AB EtherNet-IP");
        out << QStringLiteral(
            "<li>%1 —— 下拉框里是<b>灰的</b>(带\"(未实现)\"后缀),"
            "鼠标悬上去能看到为什么;真接了才能选</li>")
                  .arg(greyed.join(QStringLiteral(" / ")));
    }

    // "新建工程" —— 【真的还没有】。
    //   证据:MenuBuilder.cpp:138 明确没做这一项,
    //   tst_MenuBar 扫全菜单禁止出现"新建"字样。
    //   ★ 这条不许被顺手删掉 —— 它现在是真的,
    //     而"只写真话"的意思是【真的没有的也要写出来】。
    out << QStringLiteral("<li>新建工程(现在只能打开/保存已有工程)</li>");

    out << QStringLiteral("</ul>");

    return out.join(QString());
}

} // namespace qwhmi::ui
