#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_negctl_registry.py —— 负向对照的【登记状态】判据(2026-11-11 加,G-31-e)

为什么需要它
------------
本项目有一条纪律:**判据必须能被证伪** —— 也就是"把守卫拆掉,断言它变红"
这类负向对照。没有负向对照的判据,只能证明"现在是这样",不能证明"它会抓"。

★ 而 negctl_*.py 就是这套负向对照的载体 ★
  仓库里有一批 scripts/negctl_*.py,每个负责把某条守卫拆掉、断言判据变红。
  2026-11-11 实测:13 个 negctl_*.py 里【只有 1 个】被登记成 ctest 目标
  (negctl_qobject.py);CI 的 build.yml 里 "negctl" 一词出现 0 次。
  ⇒ 其余 12 条负向对照**在流水线上一次都没跑过**。

★ 为什么这不是"小事" ★
  文档里那些"全部按预期变红"的说法,流水线【无法证伪】——
  没人跑它。判据一旦"恒绿",CI 也是绿的,而没有任何东西会告诉你。
  更糟的是现状是【既不跑也不说】:文件躺在 scripts/ 里,看着像是有保障。

所以这条判据定的规矩是:**每个 negctl_*.py 二选一,必须说清楚**
  ① 登记进 ctest(流水线会跑它,判据能变红这件事【在 CI 上可证伪】);或者
  ② 在文件头写一行机器可读的声明:
        # NEGCTL: manual — <为什么只能手动跑>
     手动跑的理由要写清楚(比如"它要改源文件再重编,CI 里跑要动工作区")。
  ★ 不许"既不跑也不说" ★

用法
----
    python scripts/check_negctl_registry.py
退出码:0 全过 / 1 有 negctl 既没登记也没声明
"""
import re
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
ROOT = SCRIPTS.parent
CMAKE = ROOT / "hmi" / "tests" / "CMakeLists.txt"

# 声明行的形状:# NEGCTL: manual — 理由(理由不能空)
DECL = re.compile(r"^#\s*NEGCTL:\s*manual\b[ \t]*(?:—|--)?[ \t]*(.*)$", re.MULTILINE)
MIN_REASON = 8      # 理由至少 8 个字符 —— "太短"基本等于"没写"

fails: list[str] = []


def check(label: str, cond: bool, detail: str = "") -> None:
    print("  %s %s %s" % ("OK  " if cond else "FAIL", label, detail))
    if not cond:
        fails.append(label)


def main() -> int:
    negctls = sorted(p.name for p in SCRIPTS.glob("negctl_*.py"))
    print("=== 仓库里的负向对照:%d 个 ===" % len(negctls))
    for n in negctls:
        print("  %s" % n)
    check("至少找到一个负向对照", len(negctls) > 0,
          "(一个都没有 ⇒ 这条判据什么也没查)")

    # ---- 哪些被登记进 ctest -------------------------------------------
    cm = CMAKE.read_text(encoding="utf-8")
    statements = re.findall(r"add_test\s*\(([^)]*)\)", cm, re.S)
    registered = set()
    for st in statements:
        for n in negctls:
            if n in st:
                registered.add(n)

    # ---- 逐个核:登记了,还是声明了"手动跑" --------------------------
    print()
    print("=== 逐个核登记状态 ===")
    for n in negctls:
        text = (SCRIPTS / n).read_text(encoding="utf-8")
        if n in registered:
            check("%s 已登记进 ctest" % n, True, "(流水线会跑它)")
            continue
        m = DECL.search(text)
        if not m:
            check("%s 登记了或声明了" % n, False,
                  "(既没进 ctest,文件头也没有 '# NEGCTL: manual — 理由' 声明"
                  " ⇒ 既不跑也不说)")
            continue
        reason = m.group(1).strip()
        check("%s 声明为手动跑" % n, len(reason) >= MIN_REASON,
              "(理由:%s)" % (reason or "★空 —— 等于没写"))

    print()
    if not fails:
        print("RESULT: PASS —— 每条负向对照要么进 ctest,要么在文件头说清"
              "为什么只能手动跑(没有【既不跑也不说】的)")
        return 0
    print("RESULT: FAIL -> %s" % fails)
    return 1


if __name__ == "__main__":
    sys.exit(main())
