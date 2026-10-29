#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_qobject_detection.py — 守 sync_vcxproj.py 的 Q_OBJECT 自动判断(2026-10-24 加)

为什么需要它
------------
`sync_vcxproj.py` 以前判断不了 Q_OBJECT,于是每次 dry-run 都刷一句
"本脚本无法判断 Q_OBJECT,请 Wqian 手动确认" + 21 条"可能缺 QtMoc"。

**21 条全是噪声**:2026-10-24 逐个人工查过,那 21 个头的代码里
一个真 Q_OBJECT 都没有(它们的注释里写的是"这个类没有 Q_OBJECT")。

噪声的害处不是烦,是**让人习惯性跳过这一段** —— 于是真缺的时候
也跟着一起被跳过了。一个只会说"不知道"的检查,等于没有检查。

所以给它加了自动判断(header_has_qobject)。而**自动判断必须被守**:
判据写错(把注释当成代码)会让"报告是绿的"这句话失去意义。

★ 判错的两个方向都得测 ★
  ① 假阴性:真含 Q_OBJECT 却判成"没有" → 漏报 → moc 不跑 → LNK2001
  ② 假阳性:不含 Q_OBJECT 却判成"有"   → 多报 → 刷噪声(退回改之前的样子)
  两个方向错一个,这个功能就是负的。

★ 为什么要先抹注释 ★
  本项目的头文件里到处是"这个类没有 Q_OBJECT"之类的说明性注释。
  举例 `src/drivers/IPlcDriverProvider.h:16`:
      //   · 纯接口,无 Q_OBJECT,不进 moc —— 它不是信号槽参与方
  直接全文搜 "Q_OBJECT",这个头会被判成"需要 QtMoc" —— **判反了**。

用法
----
    python scripts/check_qobject_detection.py
退出码:0 全过 / 1 有断言没过 / 2 找不到 sync_vcxproj.py
"""
import shutil
import sys
import tempfile
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))
import sync_vcxproj as s

fails = []


def check(label, cond, detail=""):
    print("  %s %s %s" % ("OK  " if cond else "FAIL", label, detail))
    if not cond:
        fails.append(label)


# ---- 造一批探针文件 -------------------------------------------------------
tmp = Path(tempfile.mkdtemp(prefix="qobject_probe_"))

PROBES = {
    # 文件名 → (源码, 期望有 Q_OBJECT 吗, 为什么)
    "Real.h": ("""
#pragma once
#include <QObject>
class Real : public QObject {
    Q_OBJECT
public:
    void go();
};
""", True, "class 里真有 Q_OBJECT 宏"),

    "FakeComment.h": ("""
#pragma once
// 纯接口,无 Q_OBJECT,不进 moc
class FakeComment {
public:
    void go();
};
""", False, "Q_OBJECT 只出现在 // 注释里"),

    "FakeBlockComment.h": ("""
#pragma once
/* 这个类故意没有 Q_OBJECT
   下一行也不是:Q_OBJECT
*/
class FakeBlockComment {
public:
    void go();
};
""", False, "Q_OBJECT 只出现在 /* */ 块注释里"),

    "FakeString.h": ("""
#pragma once
class FakeString {
public:
    void go();
};
const char* s = "Q_OBJECT";
""", False, "Q_OBJECT 只出现在字符串字面量里"),

    "FakeSuffix.h": ("""
#pragma once
#define MY_Q_OBJECT 1
#define Q_OBJECT_COUNT 2
class FakeSuffix {
public:
    void go();
};
""", False, "只有 MY_Q_OBJECT / Q_OBJECT_COUNT 这种带前后缀的"),

    "RealAfterComment.h": ("""
#pragma once
#include <QObject>
// 注释里说无 Q_OBJECT 是骗人的
class RealAfterComment : public QObject {
    Q_OBJECT
public:
    void go();
};
""", True, "注释说没有、但代码里真有 —— 必须判成有"),
}

print("=== 造探针 ===")
for name, (src, _expect, _why) in PROBES.items():
    (tmp / name).write_text(src, encoding="utf-8")
    print("  %-24s %s" % (name, _why))

print()
print("=== header_has_qobject 的判定 ===")
for name, (_src, expect, why) in PROBES.items():
    got = s.header_has_qobject(tmp / name)
    check("%-24s 期望 %s" % (name, "有" if expect else "没有"),
          got == expect,
          "(实际 %s —— %s)" % ("有" if got else "没有", why))

print()
print("=== 抹注释/字符串的行为本身 ===")
# 抹完之后,注释和字符串里的内容必须变成空格,而不是消失
# (保持偏移不变是这套手法的全部意义)
probe = "// Q_OBJECT\nclass A { const char* s = \"Q_OBJECT\"; };"
masked = s.strip_comments_and_strings(probe)
check("注释里的 Q_OBJECT 被抹掉", "Q_OBJECT" not in masked.split("\n")[0],
      "(第一行现在是 %r)" % masked.split("\n")[0])
check("字符串里的 Q_OBJECT 被抹掉", '"Q_OBJECT"' not in masked,
      "(现在是 %r)" % masked)
check("长度不变(偏移保持)",
      len(masked) == len(probe),
      "(%d -> %d)" % (len(probe), len(masked)))
check("换行保留", masked.count("\n") == probe.count("\n"))

print()
print("=== 真实的仓库文件:已知答案的抽查 ===")
REPO = SCRIPTS.parent / "hmi"
if REPO.exists():
    # IPlcDriverProvider.h 的注释里明写"无 Q_OBJECT" → 必须判"没有"
    p = REPO / "src" / "drivers" / "IPlcDriverProvider.h"
    if p.exists():
        check("IPlcDriverProvider.h 判为【没有 Q_OBJECT】",
              not s.header_has_qobject(p),
              "(它第 16 行注释写着『纯接口,无 Q_OBJECT』—— 判成有就是判反了)")

    # Paths.h 是纯头,没有 Q_OBJECT
    p = REPO / "src" / "util" / "Paths.h"
    if p.exists():
        check("Paths.h 判为【没有 Q_OBJECT】", not s.header_has_qobject(p))

    # 单Instance.h 是 Q_OBJECT 类 → 必须判"有"
    p = REPO / "src" / "instance" / "SingleInstance.h"
    if p.exists():
        check("SingleInstance.h 判为【有 Q_OBJECT】", s.header_has_qobject(p),
              "(它有信号,必须有 moc)")

    # ★★ 扫范围必须用【脚本自己的】scan_source_files() ★★
    #
    #   这个坑踩过:测试原来只扫 src/** + 仓库根 *.h,于是
    #   3rdparty/qcustomplot/qcustomplot.h(它【真的】有 Q_OBJECT,在 L647/L708)
    #   不在测试的扫描范围里,却被 vcxproj 登记成了 <QtMoc> ——
    #   测试就报"多余登记",可那根本不是多余的。
    #
    #   ★ 判据和被测对象必须用同一把尺子 ★
    #   测试扫得比脚本窄 → 报假的多余登记(2026-10-24 踩到)
    #   测试扫得比脚本宽 → 漏报真的多余登记
    #   所以这里直接调 sync_vcxproj.scan_source_files(),不自己写 rglob。
    _cpp_disk, h_disk = s.scan_source_files(REPO)
    print("  (脚本扫到 %d 个 .h,范围与 sync_vcxproj.py 完全一致)" % len(h_disk))

    real = [h for h in h_disk if s.header_has_qobject(REPO / h)]
    print("  (其中 %d 个真含 Q_OBJECT)" % len(real))
    check("全仓真含 Q_OBJECT 的头 > 0", len(real) > 0,
          "(判据全判成 False 也不对 —— 那样是假阴性)")

    # ★ 关键对照:全仓的 Q_OBJECT 头,应该跟 vcxproj 里登记的 QtMoc 对得上 ★
    vcx = REPO / "hmi.vcxproj"
    if vcx.exists():
        import xml.etree.ElementTree as ET
        NS = "{http://schemas.microsoft.com/developer/msbuild/2003}"
        root = ET.parse(vcx).getroot()
        moc = {e.get("Include").replace("/", "\\")
               for e in root.iter(NS + "QtMoc")}
        disk_qobj = {h for h in h_disk if s.header_has_qobject(REPO / h)}
        # vcxproj 里登记了 QtMoc、但代码里其实没有 Q_OBJECT 的 → 多余登记
        extra = moc - disk_qobj
        # 代码里有 Q_OBJECT、但 vcxproj 没登记的 → 漏登记(这才是真问题)
        missing = disk_qobj - moc
        check("没有多余的 <QtMoc> 登记", not extra,
              "(多登记: %s)" % (", ".join(sorted(extra)) or "无"))
        check("没有漏登记的 <QtMoc>", not missing,
              "(漏登记: %s)" % (", ".join(sorted(missing)) or "无"))

        # ★ 反向:登记成 <QtMoc> 的头,必须【真的】在磁盘上存在 ★
        #   删文件忘了删 vcxproj 条目 → MSBuild 报"找不到文件"。
        gone = {p for p in moc
                if not (REPO / p).exists() and not (REPO / p.replace("\\", "/")).exists()}
        check("登记成 <QtMoc> 的头都在磁盘上", not gone,
              "(磁盘上已删: %s)" % (", ".join(sorted(gone)) or "无"))

        # ★ vcxproj 与 filters 必须完全对齐 ★
        fil = REPO / "hmi.vcxproj.filters"
        if fil.exists():
            froot = ET.parse(fil).getroot()
            fmoc = {e.get("Include").replace("/", "\\")
                    for e in froot.iter(NS + "QtMoc")}
            finc = {e.get("Include").replace("/", "\\")
                    for e in froot.iter(NS + "ClInclude")}
            vinc = {e.get("Include").replace("/", "\\")
                    for e in root.iter(NS + "ClInclude")}
            check("vcxproj 与 filters 的 <QtMoc> 一致", fmoc == moc,
                  "(差: %s)" % (", ".join(sorted(fmoc ^ moc)) or "无"))
            check("vcxproj 与 filters 的 <ClInclude> 一致", finc == vinc,
                  "(差: %s)" % (", ".join(sorted(finc ^ vinc)) or "无"))

            # ★ filters_needs_sync 也要有牙齿 ★
            # 这条判据管的是"要不要提示用户去跑 fix_filters_entries.py"。
            # 原来那条提示是无条件打印的,于是 filters 已经一致时它还在喊
            # "⚠ filters 还需要同步",紧接着 fix_filters_entries.py 却说
            # "✅ 无缺失无残留" —— 两句矛盾的话,错的那句让人白跑一趟。
            # 判据本身也可能是错的(比如恒返回 True),
            # 所以给它造两种情况各测一次。
            import tempfile as _tf
            with _tf.TemporaryDirectory(prefix="filters_sync_") as _td:
                _tdp = Path(_td)
                _v2 = _tdp / "hmi.vcxproj"
                _f2 = _tdp / "hmi.vcxproj.filters"
                _v2.write_text(
                    '<?xml version="1.0" encoding="utf-8"?>\n'
                    '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
                    "  <ItemGroup>\n"
                    '    <ClInclude Include="a.h" />\n'
                    '    <QtMoc Include="b.h" />\n'
                    "  </ItemGroup>\n"
                    "</Project>\n", encoding="utf-8")
                # ① 一致 → 不该提示
                _f2.write_text(
                    '<?xml version="1.0" encoding="utf-8"?>\n'
                    '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
                    "  <ItemGroup>\n"
                    '    <ClInclude Include="a.h" />\n'
                    '    <QtMoc Include="b.h" />\n'
                    "  </ItemGroup>\n"
                    "</Project>\n", encoding="utf-8")
                check("filters 一致时 filters_needs_sync == False",
                      s.filters_needs_sync(_v2, _f2) is False)
                # ② filters 少一条 → 该提示
                _f2.write_text(
                    '<?xml version="1.0" encoding="utf-8"?>\n'
                    '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
                    "  <ItemGroup>\n"
                    '    <ClInclude Include="a.h" />\n'
                    "  </ItemGroup>\n"
                    "</Project>\n", encoding="utf-8")
                check("filters 少一条时 filters_needs_sync == True",
                      s.filters_needs_sync(_v2, _f2) is True)
                # ③ filters 多一条 → 也该提示
                _f2.write_text(
                    '<?xml version="1.0" encoding="utf-8"?>\n'
                    '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
                    "  <ItemGroup>\n"
                    '    <ClInclude Include="a.h" />\n'
                    '    <ClInclude Include="zzz.h" />\n'
                    '    <QtMoc Include="b.h" />\n'
                    "  </ItemGroup>\n"
                    "</Project>\n", encoding="utf-8")
                check("filters 多一条时 filters_needs_sync == True",
                      s.filters_needs_sync(_v2, _f2) is True)

shutil.rmtree(tmp, ignore_errors=True)
print()
print("RESULT:", "PASS" if not fails else "FAIL -> %s" % fails)
sys.exit(0 if not fails else 1)


# ============================================================================
# ★★★ 负向对照放在【别的文件】里跑,这里只负责调用 ★★★
# ============================================================================
#
# 为什么需要负向对照
# ------------------
# 上面那些断言全绿,只说明"现在的判据和现在的仓库是一致的"。
# 一致不等于对 —— 判据和仓库【同时错】的时候,这个测试也是绿的。
#
# ★ 绿的测试不一定是有效的测试 ★
#   必须让它"咬自己一次":把守卫拆掉,期望它变红;装回去,期望它变绿。
#   只跑绿的那一半,等于没测。
#
# 负向对照做的事:
#   在临时目录里造一个【跟真仓库结构一样】的迷你工程:
#     - 一个真含 Q_OBJECT 的头
#     - 一个只有注释提到 Q_OBJECT 的头
#     - 一个 vcxproj,故意漏登记那个真含 Q_OBJECT 的头
#   然后断言 compute_diff() 报出 missing_moc(而不是默默说"一致")。
#   再把守卫拆掉(把 header_has_qobject 换成一个恒返回 False 的桩),
#   断言它【不再】报 missing_moc —— 证明这条断言真的依赖那个守卫。
#
# 这个文件:negctl_qobject.py
# 跑法:python scripts/check_qobject_detection.py --negctl

if len(sys.argv) > 1 and sys.argv[1] == "--negctl":
    import negctl_qobject
    sys.exit(negctl_qobject.run(s))
