# -*- coding: utf-8 -*-
# NEGCTL: manual — 它要临时改仓库里的 .ps1 再调 ctest,在 ctest 里跑会动工作区(而且它自己就要起 ctest,套在一起没法算)
"""扫描范围判据的负向对照:把五个洞放回去,判据【必须判红】。

★ 为什么需要这个脚本 ★
  ----------------------------------------------------------
  G-29(2026-11-10):tst_PsFormatLeak 这条判据扫的是【整个工作区】,
  连被 .gitignore 掉的 .scratch/ soaks/ memprobe/ 一起扫。
  ⇒ 它的结论取决于本地垃圾,不取决于仓库 ⇒
    本地红、CI 绿,两边各说各话,谁也说不清哪个才是真的。
  (实测:2026-11-10 基线全套 ctest 红的就是这一条,红在 .scratch 里一个
   临时脚本上 —— 那不是仓库的代码。)

  修法:扫【属于本仓库的脚本】(受版本控制的 + 未跟踪但没被忽略的),
  并配一条判据的判据 tst_PsFormatLeakScope。

  但一条测试如果【从来没红过】,它证明不了任何事 ——
  它可能只是在测一个恒真的表达式。所以这个脚本做三件事:

    M1(范围层)把"被忽略的就不扫"这条去掉
            → tst_PsFormatLeakScope 必须【红】
              (红的是场景 C:被忽略的目录里藏着坏脚本,却被扫了)

    M2(存活层)让判据什么都不扫(范围空)
            → tst_PsFormatLeakScope 必须【红】
              (红的是场景 A:正片里有漏替换,它却一声不吭)
              ★ 这条防的是"判据自己死了却还是绿的"

    M3(排除层)把排除判断退回【绝对路径】
            → tst_PsFormatLeakScope 必须【红】
              (红的是场景 G:仓库住在 build* 祖先目录下时整仓扫不到)

    M4(标记层)把 ASCII 标记 [LEAK] 换回中文
            → tst_PsFormatLeakScope 必须【红】
              (红的是场景 A/D/I:输出里找不到 [LEAK] ⇒ "红的原因不对")

    M5(存活层)去掉『扫描范围为空 ⇒ 必须红』这条自检
            → tst_PsFormatLeakScope 必须【红】
              (红的是场景 H:扫不到东西却报 OK)

  判据红的唯一依据是【ctest 的退出码】—— 不是本脚本自己的判断。
  ★ 而且还要看 ctest 【真的跑了这个目标】——
    `ctest -R 名字` 在名字不存在时也会【返回 0】并打印 "No tests were found",
    那是一条会骗人的假绿(2026-11-10 实测过这个坑)。

用法:
    python -I scripts/negctl_ps_format_leak_scope.py

退出码:
    0 = 基线绿、五个变异全红、还原后绿、还原逐字节一致
    1 = 任一环节不符合预期
"""

import hashlib
import io
import os
import re
import shutil
import subprocess
import sys
import time

# ---- 路径:全部从本文件位置推出来,不写死 ----------------------------------
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHECKER = os.path.join(ROOT, "scripts", "check_ps_format_leak.ps1")
TEST_BUILD_DIR = os.path.join(ROOT, "hmi", "tests", "build_ninja")
TEST_NAME = "tst_PsFormatLeakScope"

# ---- 三个变异点 ------------------------------------------------------------
# M1:去掉"被忽略的就不扫" ⇒ 场景 C 会红
M1_NAME = "M1(范围层) 去掉『被忽略的就不扫』"
M1_OLD = "        if ($ig.ExitCode -eq 0) { continue }"
M1_NEW = "        if ($false) { continue }   # ★ 变异"

# M2:让范围空(什么都不扫)⇒ 场景 A 会红
M2_NAME = "M2(存活层) 让判据什么都不扫"
M2_OLD = "    if ($tracked.Contains($rel)) { $files += $f; continue }"
M2_NEW = "    if ($true) { continue }   # ★ 变异"

# M3:排除判断退回【绝对路径】—— 仓库只要住在 build* 祖先目录下就整仓扫不到
#    (G-31-a 修的就是这个;2026-11-11 改写了 checker 的排除块,
#     所以这个变异点也跟着换了位置 —— 变异点写死在源文件里是对的:
#     源文件结构一变,apply_mutation 会直接说"原片段没找到"而不是默默放过。)
M3_NAME = "M3(排除层) 排除判断退回按绝对路径"
M3_OLD = "                    $rel = $_.FullName.Substring($repo.Length) -replace '^[\\\\/]+', ''"
M3_NEW = "                    $rel = $_.FullName -replace '^[\\\\/]+', ''   # ★ 变异"

# M4:把 ASCII 标记 [LEAK] 换回中文(G-31-b:判定必须只认 ASCII)
M4_NAME = "M4(标记层) ASCII 标记 [LEAK] 换回中文"
M4_OLD = '        Write-Host ("  [LEAK][漏替换] {0}:{1}" -f $f.Name, $lit.Extent.StartLineNumber) -ForegroundColor Red'
M4_NEW = '        Write-Host ("  [漏替换] {0}:{1}" -f $f.Name, $lit.Extent.StartLineNumber) -ForegroundColor Red'

# M5:去掉『扫描范围为空 ⇒ 必须红』这条自检(G-31-a 的另一半)
M5_NAME = "M5(存活层) 去掉『扫描范围为空 ⇒ 红』自检"
M5_OLD = "if ($files.Count -eq 0 -and $tracked.Count -gt 0) {"
M5_NEW = "if ($false) {   # ★ 变异"

# ---- 输出用 UTF-8 ----------------------------------------------------------
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def find_ctest():
    """找 ctest.exe。cmake 不在本机 PATH 上,所以必须自己找。"""
    from shutil import which
    p = which("ctest")
    if p:
        return p
    for cand in (
            r"D:\Qt\Tools\CMake_64\bin\ctest.exe",
            r"D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE"
            r"\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe",
            r"C:\Program Files\CMake\bin\ctest.exe"):
        if os.path.isfile(cand):
            return cand
    return None


def read_text(path):
    # ★ encoding="utf-8-sig":这个 .ps1 是【带 BOM】的,
    #   用 plain utf-8 读会把 BOM 当成一个正文字符,
    #   写回去时再混进内容里就多了一个字节。
    # ★ newline="":不让 Python 自己做换行转换,CRLF 必须原样保留。
    with io.open(path, encoding="utf-8-sig", newline="") as f:
        return f.read()


def write_text(path, text):
    """写回源文件。带重试 —— 杀毒软件的实时扫描会短暂持有句柄。"""
    last = None
    for attempt in range(12):
        try:
            with io.open(path, "w", encoding="utf-8-sig", newline="") as f:
                f.write(text)
            return
        except PermissionError as e:
            last = e
            time.sleep(0.5)
    raise RuntimeError("写 %s 失败:%s" % (path, last))


def apply_mutation(path, old, new):
    original = read_text(path)
    if old not in original:
        print("!! 原片段没找到(源文件结构变了?):\n%s" % old)
        return False
    write_text(path, original.replace(old, new, 1))
    return True


def run_test(ctest):
    """跑一次被测测试。返回 (是否通过, ctest 输出)。

    ★ 判据是【ctest 的退出码】,不是本脚本对输出的解读。
    ★ 而且必须确认 ctest 【真的跑了这个目标】——
      `ctest -R 名字` 在名字不存在时也会返回 0 并打印
      "No tests were found!!!" ⇒ 那是一条会骗人的假绿。
    """
    r = subprocess.run(
        [ctest, "-R", TEST_NAME, "--output-on-failure"],
        cwd=TEST_BUILD_DIR, capture_output=True, text=True, errors="replace")
    out = (r.stdout or "") + (r.stderr or "")
    ran = (TEST_NAME in out) and ("No tests were found" not in out)
    if not ran:
        print("  ★ ctest 压根没跑这个目标(名字对不上?)—— 这次结果不算数 ★")
        print(out[-1500:])
        return False, out
    return (r.returncode == 0), out


def restore(path, backup):
    """还原源文件。

    ★ 用 shutil.copy 而不是 copy2:copy2 会把备份的 mtime 一起带回来,
      备份是从改之前拷的 —— mtime 变旧。这条判据是脚本测试,
      不吃 ninja 的时间戳,但还原必须【逐字节】一致,所以更要 copy 之后
      再补一次 mtime。
    """
    last = None
    for attempt in range(12):
        try:
            shutil.copy(backup, path)
            now = os.stat(path).st_mtime
            os.utime(path, (now + 1, now + 1))
            return True
        except PermissionError as e:
            last = e
            time.sleep(0.5)
    raise RuntimeError(
        "还原 %s 失败(重试 12 次仍是 PermissionError:%s)。"
        "备份还在:%s —— 请手动 copy 回去。" % (path, last, backup))


def main():
    if not os.path.isfile(CHECKER):
        print("找不到文件: " + CHECKER)
        return 1
    if not os.path.isdir(TEST_BUILD_DIR):
        print("找不到测试构建目录: " + TEST_BUILD_DIR + "(先编一次)")
        return 1
    ctest = find_ctest()
    if not ctest:
        print("找不到 ctest.exe")
        return 1

    sha_before = sha256_of(CHECKER)
    bak = CHECKER + ".negctl.bak"
    shutil.copy(CHECKER, bak)

    results = {}
    try:
        # ---- ① 基线 ----------------------------------------------------
        print("=== ① 基线(修好的判据)===")
        green, out = run_test(ctest)
        print("  ctest 通过=%s" % green)
        if not green:
            print("!! 基线就不绿 —— 这一轮什么也验不了(先修基线)")
            print(out[-2000:])
            return 1
        results["baseline_green"] = True

        # ---- ② M1:范围层 ----------------------------------------------
        print("\n=== ② %s===" % M1_NAME)
        if not apply_mutation(CHECKER, M1_OLD, M1_NEW):
            return 1
        green, out = run_test(ctest)
        print("  ctest 通过=%s" % green)
        results["m1_red"] = (not green)
        if green:
            print("★ M1 变异后【仍然全绿】—— 判据没咬住 ★")
        restore(CHECKER, bak)

        # ---- ③ M2:存活层 ----------------------------------------------
        print("\n=== ③ %s===" % M2_NAME)
        if not apply_mutation(CHECKER, M2_OLD, M2_NEW):
            return 1
        green, out = run_test(ctest)
        print("  ctest 通过=%s" % green)
        results["m2_red"] = (not green)
        if green:
            print("★ M2 变异后【仍然全绿】—— 判据没咬住 ★")
        restore(CHECKER, bak)

        # ---- ④ M3:排除层 ----------------------------------------------
        print("\n=== ④ %s===" % M3_NAME)
        if not apply_mutation(CHECKER, M3_OLD, M3_NEW):
            return 1
        green, out = run_test(ctest)
        print("  ctest 通过=%s" % green)
        results["m3_red"] = (not green)
        if green:
            print("★ M3 变异后【仍然全绿】—— 判据没咬住 ★")
        restore(CHECKER, bak)

        # ---- ⑤ M4:标记层 ----------------------------------------------
        print("\n=== ⑤ %s===" % M4_NAME)
        if not apply_mutation(CHECKER, M4_OLD, M4_NEW):
            return 1
        green, out = run_test(ctest)
        print("  ctest 通过=%s" % green)
        results["m4_red"] = (not green)
        if green:
            print("★ M4 变异后【仍然全绿】—— 判据没咬住 ★")
        restore(CHECKER, bak)

        # ---- ⑥ M5:存活层(范围为空也要红) -------------------------------
        print("\n=== ⑥ %s===" % M5_NAME)
        if not apply_mutation(CHECKER, M5_OLD, M5_NEW):
            return 1
        green, out = run_test(ctest)
        print("  ctest 通过=%s" % green)
        results["m5_red"] = (not green)
        if green:
            print("★ M5 变异后【仍然全绿】—— 判据没咬住 ★")
        restore(CHECKER, bak)

        # ---- ⑦ 还原后再跑一次 ------------------------------------------
        print("\n=== ⑦ 还原后重跑 ===")
        green, out = run_test(ctest)
        print("  ctest 通过=%s" % green)
        results["restored_green"] = green
    finally:
        # 无论如何都要还原源文件。
        if os.path.isfile(bak):
            try:
                restore(CHECKER, bak)
                os.remove(bak)
                print("\n已还原源文件")
            except Exception as e:
                print("★★ 还原 %s 失败:%s" % (CHECKER, e))
                print("★★ 备份留在原地:%s" % bak)
                print("★★ 请手动 copy 回去,别让它停在变异状态。")
                results["restore_identical"] = False
                return 1

    # ---- 还原必须【逐字节】一致 ------------------------------------------
    sha_after = sha256_of(CHECKER)
    if sha_after != sha_before:
        print("★ 还原后文件内容不一致(SHA256 对不上)")
        print("  跑前 %s" % sha_before)
        print("  跑后 %s" % sha_after)
        results["restore_identical"] = False
    else:
        results["restore_identical"] = True
        print("★ 还原后源文件逐字节一致(SHA256 对得上)")

    need = ("baseline_green", "m1_red", "m2_red", "m3_red",
            "m4_red", "m5_red",
            "restored_green", "restore_identical")
    if all(results.get(k) for k in need):
        print("\n★ 结论:基线绿、五个变异全红、还原后绿、还原逐字节一致 —— "
              "扫描范围判据确实咬得住 ★")
        return 0
    print("\n★ 结论:不符合预期 ★")
    return 1


if __name__ == "__main__":
    sys.exit(main())
