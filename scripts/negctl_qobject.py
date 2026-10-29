#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""negctl_qobject.py — Q_OBJECT 判据的【负向对照】。

被 check_qobject_detection.py --negctl 调用,也可以单独跑。

★ 这个文件干什么 ★
  造一个【迷你工程】:目录结构跟真仓库一样(有 src/、有 hmi.vcxproj),
  里面放两个头:
    Real.h       —— 真的含 Q_OBJECT 宏
    Fake.h       —— Q_OBJECT 只出现在注释里
  故意让 vcxproj 漏登记 Real.h。

  然后断言:
    ① compute_diff() 报出 missing_moc 里有 Real.h(真缺被抓到了)
    ② Fake.h 落在 no_moc_needed 里(不是 missing_moc)
    ③ 【把守卫拆掉】—— 把 header_has_qobject 换成一个恒返回 False 的桩 ——
       断言它【不再】报 missing_moc

  第 ③ 步是关键:它证明 ① 不是"碰巧绿",而是真的靠这个判据撑着的。
  没有 ③,这个测试在判据整个坏掉的情况下依然是绿的。
"""
import shutil
import tempfile
from pathlib import Path


def _build_mini_repo(root: Path) -> tuple[Path, Path]:
    """造迷你工程,返回 (src_root, vcxproj_path)。"""
    src = root / "src"
    src.mkdir(parents=True)

    (src / "Real.h").write_text(
        "#pragma once\n"
        "#include <QObject>\n"
        "class Real : public QObject {\n"
        "    Q_OBJECT\n"
        "public:\n"
        "    void go();\n"
        "};\n",
        encoding="utf-8",
    )
    (src / "Fake.h").write_text(
        "#pragma once\n"
        "// 这个类没有 Q_OBJECT,不进 moc\n"
        "class Fake {\n"
        "public:\n"
        "    void go();\n"
        "};\n",
        encoding="utf-8",
    )
    (src / "Fake.cpp").write_text("#include \"Fake.h\"\n", encoding="utf-8")

    vcx = root / "hmi.vcxproj"
    # 故意【不】登记 Real.h —— 它是"真缺 QtMoc"的那个
    vcx.write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
        "  <ItemGroup>\n"
        '    <ClCompile Include="src\\Fake.cpp" />\n'
        '    <ClInclude Include="src\\Fake.h" />\n'
        "  </ItemGroup>\n"
        "</Project>\n",
        encoding="utf-8",
    )
    return src, vcx


def run(s) -> int:
    """s = sync_vcxproj 模块。返回退出码。"""
    fails = []

    def check(label, cond, detail=""):
        print("  %s %s %s" % ("OK  " if cond else "FAIL", label, detail))
        if not cond:
            fails.append(label)

    tmp = Path(tempfile.mkdtemp(prefix="qobject_negctl_"))
    try:
        src_root, vcx = _build_mini_repo(tmp)

        # ---- 守卫在位:应该抓到真缺 ----
        cpp_disk, h_disk = s.scan_source_files(src_root)
        cl_c, cl_i, qt_moc = s.parse_vcxproj_includes(vcx)
        diff = s.compute_diff(cpp_disk, h_disk, cl_c, cl_i, qt_moc,
                              src_root=src_root)

        print("=== 负向对照 A:守卫在位 ===")
        # 路径是相对 src_root 的。迷你工程里 src_root 就是 src/ 本身,
        # 所以是 "Real.h" 而不是 "src\\Real.h" —— 别写成后者,会白 FAIL。
        check("抓到真缺:Real.h 在 missing_moc 里",
              "Real.h" in diff["missing_moc"],
              "(missing_moc=%s)" % diff["missing_moc"])
        check("没把 Fake.h 当成真缺",
              "Fake.h" not in diff["missing_moc"],
              "(missing_moc=%s)" % diff["missing_moc"])
        check("Fake.h 落在 no_moc_needed 里",
              "Fake.h" in diff["no_moc_needed"],
              "(no_moc_needed=%s)" % diff["no_moc_needed"])

        # ---- 守卫拆掉:必须【抓不到了】 ----
        # 这才是"这个测试有牙齿"的证据。
        # 拆法:把 header_has_qobject 临时换成恒返回 False 的桩。
        print()
        print("=== 负向对照 B:守卫被拆掉(桩:恒返回 False) ===")
        real_fn = s.header_has_qobject
        s.header_has_qobject = lambda p: False
        try:
            diff2 = s.compute_diff(cpp_disk, h_disk, cl_c, cl_i, qt_moc,
                                   src_root=src_root)
        finally:
            s.header_has_qobject = real_fn

        check("守卫拆掉后,missing_moc 变空(证明 A 靠的是判据)",
              not diff2["missing_moc"],
              "(missing_moc=%s —— 非空说明 A 是碰巧绿的,这个测试没牙齿)"
              % diff2["missing_moc"])
        check("守卫拆掉后,Real.h 掉进 no_moc_needed",
              "Real.h" in diff2["no_moc_needed"],
              "(no_moc_needed=%s)" % diff2["no_moc_needed"])
        check("守卫装回去后行为恢复(桩没污染真函数)",
              s.header_has_qobject(src_root / "Real.h") is True)

        # ---- ★ 报告说有,--apply 就得真写进去 ★
        #
        #   这一条是 2026-10-24 抓到的真 bug:一个头已登记为 <ClInclude>、
        #   但缺 <QtMoc>(而它代码里真有 Q_OBJECT)时,compute_diff 报出
        #   missing_moc,可 --apply 里的 `if p in existing: continue`
        #   (existing 把三种标签混在一个池子里)会命中 —— <QtMoc> 永远写不进去。
        #
        #   ★ 这是本项目最危险的那一类:【报告说有,--apply 却不干】★
        #   差一点点就又是 G-12 那种"三个星期没人发现"。
        print()
        print("=== 负向对照 C:--apply 写不写进去(报告说有就得真写) ===")
        vcx2 = tmp / "hmi2.vcxproj"
        vcx2.write_text(
            '<?xml version="1.0" encoding="utf-8"?>\n'
            '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
            "  <ItemGroup>\n"
            '    <ClInclude Include="Real.h" />\n'      # ← 只有 ClInclude,故意不给 QtMoc
            "  </ItemGroup>\n"
            "</Project>\n", encoding="utf-8")
        cc2, ci2, qm2 = s.parse_vcxproj_includes(vcx2)
        d3 = s.compute_diff(cpp_disk, h_disk, cc2, ci2, qm2, src_root=src_root)
        check("compute_diff 报出 missing_moc",
              "Real.h" in d3["missing_moc"],
              "(missing_moc=%s)" % d3["missing_moc"])
        s.insert_include_into_vcxproj(vcx2, d3["missing_cpp"], d3["missing_h"],
                                       d3["missing_moc"])
        after = vcx2.read_text(encoding="utf-8-sig")
        check("--apply 真的写进了 <QtMoc>",
              '<QtMoc Include="Real.h" />' in after,
              "(报告说有,但文件里没有 —— 这就是『报告说有、apply 不干』)")
        check("--apply 没有写重复的 <ClInclude>",
              after.count('<ClInclude Include="Real.h" />') == 1,
              "(出现了 %d 次 —— 重复项会让 MSBuild 报『重复项』,项目打不开)"
              % after.count('<ClInclude Include="Real.h" />'))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print()
    print("NEGCTL RESULT:", "PASS" if not fails else "FAIL -> %s" % fails)
    return 0 if not fails else 1


if __name__ == "__main__":
    import sys
    from pathlib import Path as _P
    sys.path.insert(0, str(_P(__file__).resolve().parent))
    import sync_vcxproj as _s
    sys.exit(run(_s))
