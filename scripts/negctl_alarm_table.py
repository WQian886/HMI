# -*- coding: utf-8 -*-
"""负向对照:把 AlarmTablePresenter 的关键守卫拆掉,测试必须变红。

★ 绿色的测试不一定是有效的测试 —— 它得"咬自己一口"才算数。★

这一条纪律本项目已经反复吃过亏:
  · G-11「测了 above 从没测 below」—— 一个方向漏了,判据重叠活了 24 小时
  · F1「只断言了唯一没坏的两样」
  · BUGFIX-C3「主画面报警表整张表都废掉,而 68 个测试全绿」
    —— 因为主画面那套【一条测试都没有】

用法:
    python scripts/negctl_alarm_table.py

它会临时改源文件 → 编 → 跑 → 还原 → 打印结果 → 还原后再跑一次确认是绿的。
**任何时候都不会留下被改坏的源文件**(finally 里还原)。

退出码:
    0 = 基线绿、四个变异全红、还原后绿
    1 = 基线就不绿 / 变异没变红 / 还原后不绿  ← 这一条才是真出事
"""

import io
import os
import shutil
import subprocess
import sys
import tempfile

# ---- 路径:全部从本文件位置推出来,不写死 ----------------------------------
# ★ 写死 `E:\qianwie\hmi` 的话,别人克隆下来就跑不了。
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRESENTER = os.path.join(ROOT, "hmi", "src", "alarm", "AlarmTablePresenter.cpp")
TESTS = os.path.join(ROOT, "hmi", "tests")
BUILD = os.path.join(TESTS, "build_ninja")
TARGET = "tst_AlarmTablePresenter"

# ---- 工具链:能自动找到就用自动的,找不到再问人 ---------------------------
def find_tool(name, extra_hints=()):
    """在 PATH 和常见位置里找可执行文件。找不到就返回 None(让调用方报人话)。"""
    from shutil import which
    p = which(name)
    if p:
        return p
    for hint in extra_hints:
        if os.path.isfile(hint):
            return hint
    return None


def find_vcvars():
    """找 vcvars64.bat。

    ★ 顺序很重要:先用 vswhere(问 VS 装在哪),
      找不到再退到常见的固定路径 —— 本机 VS 18 装在 D 盘。
    """
    vswhere = os.path.join(
        os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
        "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if os.path.isfile(vswhere):
        try:
            r = subprocess.run(
                [vswhere, "-latest", "-products", "*",
                 "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                 "-property", "installationPath"],
                capture_output=True, text=True, errors="replace")
            inst = r.stdout.strip()
            if inst:
                cand = os.path.join(
                    inst, "VC", "Auxiliary", "Build", "vcvars64.bat")
                if os.path.isfile(cand):
                    return cand
        except Exception:
            pass
    for drive in ("D:", "C:", "E:"):
        cand = os.path.join(
            drive, r"Program Files\Microsoft Visual Studio\18\Community",
            "VC", "Auxiliary", "Build", "vcvars64.bat")
        if os.path.isfile(cand):
            return cand
        cand = os.path.join(
            drive, r"Program Files\Microsoft Visual Studio\2022\Community",
            "VC", "Auxiliary", "Build", "vcvars64.bat")
        if os.path.isfile(cand):
            return cand
    return None


def find_cmake():
    return find_tool("cmake", extra_hints=(
        r"D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE"
        r"\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE"
        r"\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    ))


# ---- 变异清单:(名字, 原片段, 替换片段)------------------------------------
MUTATIONS = [
    (
        "M1 状态列不创建(还原 2026-10-14 那个 bug 的原样)",
        """    auto* statusItem = new QTableWidgetItem(stateText(a.state));
    statusItem->setData(kStateRole, static_cast<int>(visualStateFor(a.state)));
    table->setItem(row, kColStatus, statusItem);""",
        """    // ★ 变异:不创建状态列(= 2026-10-14 那个 bug 的原样)""",
    ),
    (
        "M2 裁剪时把活动报警也丢掉",
        """            if (!liveIds.contains(id)) { victim = row; break; }""",
        """            victim = row; break;   // ★ 变异:连活动报警一起丢""",
    ),
    (
        "M3 裁剪丢最【新】的行而不是最旧的",
        """        int victim = -1;
        for (int row = 0; row < table->rowCount(); ++row) {""",
        """        int victim = -1;
        for (int row = table->rowCount() - 1; row >= 0; --row) {   // ★ 变异:倒着扫""",
    ),
    (
        "M4 状态改动只改文字、不改 UserRole",
        """    statusItem->setText(stateText(st));
    statusItem->setData(kStateRole, static_cast<int>(visualStateFor(st)));""",
        """    statusItem->setText(stateText(st));   // ★ 变异:漏了 setData""",
    ),
]


def run(cmd, cwd=None, env=None):
    return subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True,
                          errors="replace")


def write_build_bat(batfile, vcvars, cmake):
    """★ 走一个 .bat 批处理:cmd /c 里直接串 vcvars64 + cmake 会因为
    嵌套引号被 cmd 吃掉(症状:rc=1 且【一行输出都没有】)。
    批处理把这件事变成它自己最擅长的形式。
    """
    with io.open(batfile, "w", encoding="ascii", newline="\r\n") as f:
        f.write("@echo off\r\n")
        f.write('call "%s" >nul 2>&1\r\n' % vcvars)
        f.write('cd /d "%s"\r\n' % TESTS)
        f.write('"%s" --build build_ninja --target %s\r\n' % (cmake, TARGET))
        f.write("exit /b %ERRORLEVEL%\r\n")


def build_and_run(batfile):
    r = run(["cmd", "/c", batfile], cwd=ROOT)
    if r.returncode != 0:
        return None, r.stdout + r.stderr
    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    exe = os.path.join(BUILD, TARGET + ".exe")
    if not os.path.isfile(exe):
        return None, "测试可执行文件不存在: " + exe
    r2 = run([exe], cwd=BUILD, env=env)
    out = r2.stdout + r2.stderr
    failed = []
    for line in out.splitlines():
        if line.startswith("FAIL!") or "Totals:" in line:
            failed.append(line.strip())
    return failed, out


def main():
    if not os.path.isfile(PRESENTER):
        print("找不到源文件: " + PRESENTER)
        return 1
    vcvars = find_vcvars()
    cmake = find_cmake()
    if not vcvars:
        print("找不到 vcvars64.bat —— 装 Visual Studio 时要勾选「使用 C++ 的桌面开发」")
        return 1
    if not cmake:
        print("找不到 cmake.exe")
        return 1
    if not os.path.isdir(BUILD):
        print("找不到测试构建目录: " + BUILD)
        print("先跑 scripts/build_tests.ps1(或 cmake -S hmi/tests -B hmi/tests/build_ninja)")
        return 1

    batfile = os.path.join(tempfile.gettempdir(), "negctl_alarm_table_build.bat")
    write_build_bat(batfile, vcvars, cmake)

    backup = PRESENTER + ".negctl.bak"
    shutil.copy2(PRESENTER, backup)
    original = io.open(backup, encoding="utf-8", newline="").read()

    try:
        # ---- 先量基线 ----
        res, out = build_and_run(batfile)
        if res is None:
            print("基线编译失败:\n", out[-3000:])
            return 1
        baseline_totals = [l for l in res if "Totals:" in l]
        print("基线:", baseline_totals[0] if baseline_totals else res)
        if not baseline_totals or "0 failed" not in baseline_totals[0]:
            print("!! 基线就不绿(或者崩了),先修基线再谈负向对照")
            return 1

        all_red = True
        for name, old, new in MUTATIONS:
            if old not in original:
                print("!! %s:原片段没找到(源文件结构变了?)" % name)
                all_red = False
                continue
            mutated = original.replace(old, new, 1)
            io.open(PRESENTER, "w", encoding="utf-8", newline="").write(mutated)
            res, out = build_and_run(batfile)
            if res is None:
                print("%s:编译失败(编译不过也算红,但要看清原因)" % name)
                print(out[-1500:])
                all_red = False
            else:
                totals = [l for l in res if "Totals:" in l]
                fails = [l for l in res if l.startswith("FAIL!")]
                # ★ 没有 Totals 行 = 测试进程【崩了】(比如对 nullptr 解引用)——
                #   那也是红,而且是最响的那种红。
                #   绝不能因为"没读到 Totals"就判成绿。
                if not totals:
                    status = "红 ✓(进程崩了,连 Totals 都没打出来)"
                elif "0 failed" in totals[0]:
                    status = "★ 还是绿的 ★  ← 这条守卫没被守住"
                    all_red = False
                else:
                    status = "红 ✓"
                print("%s: %s" % (name, status))
                if totals:
                    print("     ", totals[0])
                for f in fails[:4]:
                    print("     ", f[:160])
    finally:
        # ★ 必须 copy 而不是 copy2:copy2 会把【备份的 mtime】一起带回来,
        #   而备份是从改之前的文件拷的 —— mtime 变旧,ninja 判定"不用重编",
        #   于是还原后跑的还是被变异的二进制,最后一句"还原后:xx failed"就是这么来的。
        shutil.copy(backup, PRESENTER)
        os.remove(backup)
        print("\n已还原源文件")

    # 还原后必须重新变绿
    res, out = build_and_run(batfile)
    if res is None:
        print("还原后编译失败:\n", out[-3000:])
        return 1
    totals = [l for l in res if "Totals:" in l]
    print("还原后:", totals[0] if totals else ("★ 没有 Totals 行(崩了)★"))
    green_after = bool(totals and "0 failed" in totals[0])
    if not green_after:
        return 1
    if not all_red:
        print("\n★ 结论:有变异【没有变红】—— 对应的守卫没被守住 ★")
        return 1
    print("\n★ 结论:基线绿、四个变异全红、还原后绿 —— 守卫确实被守住了 ★")
    try:
        os.remove(batfile)
    except OSError:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
