# -*- coding: utf-8 -*-
# NEGCTL: manual — 它要篡改 git 索引和工作区里的源文件(还要重跑 ctest),
# 在 ctest 里跑会跟并行跑的目标打架;手动跑。
"""G-31 的负向对照:把四条新判据的守卫拆掉,断言它们【必须判红】。

这一轮修的是同一族病:**判据跟环境没脱钩**。
  a  扫描范围看【绝对路径】⇒ 仓库住在 build* 祖先目录下就整仓扫不到
  b  判定看【中文】⇒ 控制台代码页一变,同一份仓库结论相反
  c  .ps1 的落盘形态(BOM/行尾)【从来没有任何东西执行过】
  d  Q_OBJECT 判据读【工作区】⇒ 结论取决于本地状态
  e  45 处 qputenv 写在 initTestCase 里 ⇒ 【全是无效的】
     + 负向对照【既不跑也不说】

★ 为什么要这个脚本 ★
  判据写完不等于判据咬得住。一条判据如果从来没红过,它证明不了任何事 ——
  它可能只是在测一个恒真的表达式。本项目吃过这个亏的次数已经数不清:
  G-11、F1、BUGFIX-C3、BUGFIX-E1、G-15、G-26……

★ 本脚本拆四个守卫 ★
  M1(落盘形态)把索引里某个 .ps1 的 BOM 去掉
             ⇒ tst_Ps1Encoding 必须【红】
  M2(登记状态)把一个 negctl 的『# NEGCTL: manual』声明行删掉
             ⇒ tst_NegctlRegistry 必须【红】
  M3(运行环境)把 CMakeLists 里那段『给所有目标钉死环境/TIMEOUT』的
             守卫循环整段删掉
             ⇒ tst_CtestEnv 必须【红】
  M4(索引 vs 工作区)往【工作区】放一个没登记的 Q_OBJECT 头
             ⇒ tst_QObjectDetection 必须【绿】(它不看工作区)
             再 git add 把它【放进索引】
             ⇒ tst_QObjectDetection 必须【红】(它确实在判索引,而且有牙)
             还原后必须【绿】

★ M4 的两头都要有 ★
  只测"红"不够:那只说明判据会红,没说明它判的是索引。
  只测"绿"也不够:那只说明它没在工作区里看见那个文件 ——
  但如果它压根什么都不判,也是绿的。
  ⇒ 两头一起测:工作区里【看不见】、索引里【看得见且会红】。

用法:
    python -X utf8 -I scripts/negctl_g31_env_closure.py

退出码:
    0 = 基线绿、四个变异全对、还原后绿、还原逐字节一致
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
SCRIPTS = os.path.join(ROOT, "scripts")
CMAKE = os.path.join(ROOT, "hmi", "tests", "CMakeLists.txt")
PROBE = "negctl_write_starve.py"          # M2:删掉它的声明行
PS1_FOR_BOM = "scripts/check_ps_format_leak.ps1"   # M1:去掉它的 BOM
PROBE_HEADER = os.path.join(ROOT, "hmi", "src", "drivers", "G31Probe.h")  # M4
TEST_BUILD_DIR = os.path.join(ROOT, "hmi", "tests", "build_ninja")

TESTS = {
    "M1": "tst_Ps1Encoding",
    "M2": "tst_NegctlRegistry",
    "M3": "tst_CtestEnv",
    "M4": "tst_QObjectDetection",
}

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
    from shutil import which
    p = which("ctest")
    if p:
        return p
    for cand in (
        r"D:\Qt\Tools\CMake_64\bin\ctest.exe",
        r"C:\Program Files\CMake\bin\ctest.exe"):
        if os.path.isfile(cand):
            return cand
    return None


def read_text(path):
    # ★ encoding="utf-8-sig":这些文件里有的带 BOM,
    #   用 plain utf-8 读会把 BOM 当成一个正文字符,写回去就多了一个字节。
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


def restore_file(path, backup):
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
    raise RuntimeError("还原 %s 失败:%s" % (path, last))


def git(*args, stdin=None):
    p = subprocess.run(["git", "-C", ROOT] + list(args),
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       input=stdin)
    if p.returncode != 0:
        raise RuntimeError("git %s 失败:%s"
                           % (" ".join(args),
                              p.stderr.decode("utf-8", "replace").strip()))
    return p.stdout


def run_test(ctest, name):
    """跑一条 ctest 目标,返回 (是否通过, 输出)。

    ★ 必须确认 ctest 【真的跑了这个目标】——
      `ctest -R 名字` 在名字不存在时也会返回 0 并打印 "No tests were found",
      那是一条会骗人的假绿。
    """
    r = subprocess.run(
        [ctest, "-R", "^%s$" % name, "--output-on-failure"],
        cwd=TEST_BUILD_DIR, capture_output=True, text=True, errors="replace")
    out = (r.stdout or "") + (r.stderr or "")
    ran = (name in out) and ("No tests were found" not in out)
    if not ran:
        print("  ★ ctest 压根没跑这个目标(名字对不上?)—— 这次结果不算数 ★")
        print(out[-1500:])
        return False, out
    return (r.returncode == 0), out


def main():
    if not os.path.isdir(TEST_BUILD_DIR):
        print("找不到测试构建目录: " + TEST_BUILD_DIR + "(先编一次)")
        return 1
    ctest = find_ctest()
    if not ctest:
        print("找不到 ctest.exe")
        return 1

    results = {}
    backups = {}

    def backup(path):
        bak = path + ".negctl.bak"
        shutil.copy(path, bak)
        backups[path] = bak
        return bak

    try:
        # ================= ① 基线 =================
        print("=== ① 基线(四条新判据都必须绿)===")
        for tag, name in TESTS.items():
            green, out = run_test(ctest, name)
            print("  %s %-22s 通过=%s" % (tag, name, green))
            if not green:
                print("!! 基线就不绿 —— 这一轮什么也验不了(先修基线)")
                print(out[-2500:])
                return 1
        results["baseline_green"] = True

        # ================= ② M1:BOM =================
        print()
        print("=== ② M1(落盘形态)把索引里 %s 的 BOM 去掉 ===" % PS1_FOR_BOM)
        mode_line = git("ls-files", "-s", "--", PS1_FOR_BOM).decode().split()
        mode = mode_line[0] if mode_line else "100644"
        old_blob = git("rev-parse", ":" + PS1_FOR_BOM).decode().strip()
        raw = git("show", ":" + PS1_FOR_BOM)
        stripped = raw[3:] if raw.startswith(b"\xef\xbb\xbf") else raw
        new_blob = git("hash-object", "-w", "--stdin", stdin=stripped).decode().strip()
        git("update-index", "--cacheinfo", "%s,%s,%s" % (mode, new_blob, PS1_FOR_BOM))
        green, out = run_test(ctest, TESTS["M1"])
        print("  ctest 通过=%s" % green)
        results["m1_red"] = (not green)
        if green:
            print("★ M1 变异后【仍然全绿】—— 判据没咬住 ★")
        # 还原索引
        git("update-index", "--cacheinfo", "%s,%s,%s" % (mode, old_blob, PS1_FOR_BOM))
        back = git("rev-parse", ":" + PS1_FOR_BOM).decode().strip()
        results["m1_restored"] = (back == old_blob)
        if back != old_blob:
            print("★ 索引没还原干净(%s != %s)" % (back, old_blob))

        # ================= ③ M2:声明行 =================
        print()
        print("=== ③ M2(登记状态)删掉 %s 的『# NEGCTL: manual』声明行 ===" % PROBE)
        p2 = os.path.join(SCRIPTS, PROBE)
        backup(p2)
        sha2_before = sha256_of(p2)
        text = read_text(p2)
        lines = [ln for ln in text.split("\n")
                 if not ln.strip().startswith("# NEGCTL: manual")]
        write_text(p2, "\n".join(lines))
        green, out = run_test(ctest, TESTS["M2"])
        print("  ctest 通过=%s" % green)
        results["m2_red"] = (not green)
        if green:
            print("★ M2 变异后【仍然全绿】—— 判据没咬住 ★")
        restore_file(p2, backups[p2])
        results["m2_restored"] = (sha256_of(p2) == sha2_before)
        os.remove(backups[p2])
        del backups[p2]

        # ================= ④ M3:CMake 的 ENVIRONMENT =================
        print()
        print("=== ④ M3(运行环境)把 CMakeLists 里那段『钉死环境』的守卫循环整段删掉 ===")
        backup(CMAKE)
        sha3_before = sha256_of(CMAKE)
        text = read_text(CMAKE)
        # 把那段"给所有目标钉死环境/TIMEOUT"的守卫循环【整段】删掉 ——
        # 这才是"有人把守卫删了"的真实形态(而不是只改一个值)。
        # ★ \r?\n:这个文件是 CRLF 的 —— 只写 \n 的话一条都匹配不上,
        #   而"没匹配上"会让本脚本直接退出(它宁可失败也不放过),
        #   但那是个假失败,不是判据的问题。
        # ★ 连 list(LENGTH ...) / message(STATUS ...) 一起删:
        #   它们依赖 HMI_ALL_TESTS,留着就是一段引用了不存在变量的代码。
        #   那个 message() 里还有一句"已给 N 个目标钉死 QT_QPA_PLATFORM=offscreen"
        #   —— 那是【给人看的话】,判据不许拿它当证据(它会让删光守卫的
        #   CMakeLists 照样"通过"判据) ⇒ 这里也一并删掉,不留话术。
        mutated, n = re.subn(
            r'(?ms)^get_property\(HMI_ALL_TESTS[^\n]*\r?\n'
            r'foreach\(HMI_T[^\n]*\r?\n.*?^endforeach\(\)[^\n]*\r?\n'
            r'list\(LENGTH[^\n]*\r?\n'
            r'message\(STATUS[^\n]*\r?\n',
            '', text)
        if n == 0 or mutated == text:
            print("!! 原片段没找到(CMakeLists 的结构变了?)")
            return 1
        write_text(CMAKE, mutated)
        green, out = run_test(ctest, TESTS["M3"])
        print("  ctest 通过=%s" % green)
        results["m3_red"] = (not green)
        if green:
            print("★ M3 变异后【仍然全绿】—— 判据没咬住 ★")
        restore_file(CMAKE, backups[CMAKE])
        results["m3_restored"] = (sha256_of(CMAKE) == sha3_before)
        os.remove(backups[CMAKE])
        del backups[CMAKE]

        # ================= ⑤ M4:索引 vs 工作区 =================
        print()
        print("=== ⑤ M4(索引 vs 工作区)往工作区放一个没登记的 Q_OBJECT 头 ===")
        probe_src = (
            "#pragma once\n"
            "#include <QObject>\n"
            "// G-31 负向对照的探针:工作区里有、索引里(暂时)没有。\n"
            "class G31Probe : public QObject {\n"
            "    Q_OBJECT\n"
            "public:\n"
            "    void go();\n"
            "};\n")
        os.makedirs(os.path.dirname(PROBE_HEADER), exist_ok=True)
        with io.open(PROBE_HEADER, "w", encoding="utf-8", newline="\n") as f:
            f.write(probe_src)
        green, out = run_test(ctest, TESTS["M4"])
        print("  工作区里有、索引里没有 ⇒ 通过=%s(必须【绿】:判据不看工作区)" % green)
        results["m4a_green"] = green
        if not green:
            print("★ M4a:判据居然被【未跟踪的工作区文件】弄红了 ⇒ 它还在看工作区 ★")

        # 再把它放进索引 ⇒ 必须红
        git("add", "--", os.path.relpath(PROBE_HEADER, ROOT).replace("\\", "/"))
        green, out = run_test(ctest, TESTS["M4"])
        print("  放进索引以后 ⇒ 通过=%s(必须【红】:它确实在判索引,而且有牙)" % green)
        results["m4b_red"] = (not green)
        if green:
            print("★ M4b:索引里多了一个没登记的 Q_OBJECT 头,判据却没红 —— 判据没牙 ★")
        # 还原
        rel = os.path.relpath(PROBE_HEADER, ROOT).replace("\\", "/")
        git("rm", "-q", "--cached", "--", rel)
        os.remove(PROBE_HEADER)
        green, out = run_test(ctest, TESTS["M4"])
        print("  还原后 ⇒ 通过=%s(必须【绿】)" % green)
        results["m4c_green"] = green
        status = git("status", "--porcelain", "--", rel).decode().strip()
        results["m4_clean"] = (status == "")
        if status:
            print("★ 探针没清干净:git status 里有 %r" % status)

        # ================= ⑥ 还原后再跑一次 =================
        print()
        print("=== ⑥ 还原后重跑 ===")
        for tag, name in TESTS.items():
            green, out = run_test(ctest, name)
            print("  %s %-22s 通过=%s" % (tag, name, green))
            if not green:
                results["restored_green"] = False
                print(out[-1500:])
        results.setdefault("restored_green", True)
    finally:
        for path, bak in list(backups.items()):
            try:
                restore_file(path, bak)
                os.remove(bak)
                print("\n已还原 %s" % path)
            except Exception as e:
                print("★★ 还原 %s 失败:%s" % (path, e))
                print("★★ 备份留在原地:%s" % bak)
                results["restore_ok"] = False

    need = ("baseline_green", "m1_red", "m1_restored",
            "m2_red", "m2_restored", "m3_red", "m3_restored",
            "m4a_green", "m4b_red", "m4c_green", "m4_clean",
            "restored_green")
    print()
    bad = [k for k in need if not results.get(k)]
    if not bad:
        print("★ 结论:基线绿、四个变异全对、还原后绿、还原逐字节一致 —— "
              "这一轮的新判据确实咬得住 ★")
        return 0
    print("★ 结论:不符合预期 —— %s ★" % bad)
    return 1


if __name__ == "__main__":
    sys.exit(main())
