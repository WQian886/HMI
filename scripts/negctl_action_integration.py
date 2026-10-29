# -*- coding: utf-8 -*-
"""整条链路的负向对照:把类型适配拆掉,短跑实测【必须判红】。

★ 为什么还要再做一次(单测层已经做过 negctl_action_write.py)★
  ----------------------------------------------------------
  单测层的负向对照证明的是:"tst_ActionWrite 守得住那道闸"。
  它【没有】证明的是:"整条链在真实进程 + 真实 socket 上,
  拆掉那道闸之后,【从站真的会收不到写帧】"。

  这两件事不是一回事。本项目反复吃亏的形态就是:
    界面正常、报警正常响、审计正常记"已执行",
    而 PLC 一个字节都没收到 —— 而【一条测试都没有】。
  只有把整条链跑起来、从站那边数写帧,才验得到这一层。

  所以这里做的是:
    ① 基线:修好的二进制 → 短跑 → 从站收到 FC6 写帧 → 判绿
    ② 变异:拆掉类型适配 → 短跑 → 从站【收不到】写帧 → 判红
    ③ 还原:源码还原 + 重编 → 短跑 → 又收到写帧 → 判绿

  ★ 判定红不红的唯一依据是【从站日志里的写帧计数】★
    —— 不是应用日志里那行 "[Action] ... = 0"(那是应用【自称】执行了),
    是从站【真的收到】了。从站不会撒谎。

用法:
    python scripts/negctl_action_integration.py

退出码:
    0 = 基线绿、变异红、还原后绿 —— 整条链的判据确实咬得住
    1 = 任一环节不符合预期
"""

import io
import os
import shutil
import subprocess
import sys
import tempfile

# ---- 路径:全部从本文件位置推出来,不写死 ----------------------------------
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = ROOT
ACTION_MANAGER = os.path.join(ROOT, "hmi", "ActionManager.cpp")
BUILD_DIR = os.path.join(ROOT, "hmi", "out", "build", "debug2")
EXE = os.path.join(BUILD_DIR, "hmi.exe")
SCRIPT = os.path.join(ROOT, "scripts", "short_action_run.ps1")

# ★ 变异:把"按变量声明类型适配"这一步拆掉。
#   这正是 2026-10-25 修的那个 bug 的原样 ——
#   联动值是 QVariant(LongLong),变量是 UInt16,
#   setValue 严格按 typeId 相等校验 → Rejected → 不标脏 → 不写。
MUTATION_NAME = "M1(整链层) 拆掉 ActionManager 里的类型适配"
MUTATION_OLD = """    const QVariant typed = qwhmi::util::coerceToDataType(it->value, v->dataType());
    if (!typed.isValid()) {"""
MUTATION_NEW = """    const QVariant typed = it->value;   // ★ 变异:不做类型适配
    if (false) {"""


def find_vcvars():
    """找 vcvars64.bat。先用 vswhere(问 VS 装在哪),找不到再退到常见固定路径。"""
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
                cand = os.path.join(inst, "VC", "Auxiliary", "Build", "vcvars64.bat")
                if os.path.isfile(cand):
                    return cand
        except Exception:
            pass
    for cand in (
            r"D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
            r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"):
        if os.path.isfile(cand):
            return cand
    return None


def find_cmake():
    """找 cmake.exe。
    ★ 【踩过】这里的路径拼法必须是【完整绝对路径】,不能用
      os.path.join("D:", "Program Files\\...") ——
      那样拼出来是 "D:Program Files\\...",是【驱动器相对路径】,
      cmd 报的是 "The system cannot find the path specified.",
      看起来像"环境坏了",其实是路径少了个反斜杠。
      ⇒ 一律写完整的 r"D:\\Program Files\\..." 绝对路径。
    """
    from shutil import which
    p = which("cmake")
    if p:
        return p
    for cand in (
            r"D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE"
            r"\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
            r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE"
            r"\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"):
        if os.path.isfile(cand):
            return cand
    return None


def detect_newline(text):
    """这个文件自己用 LF 还是 CRLF。
    ★ 【踩过】ActionManager.cpp 是 CRLF(仓库里一直就是 CRLF),
      而手写的变异片段用 \n 拼 —— 直接 replace 到 CRLF 文件上【永远匹配不上】,
      脚本会报"原片段没找到",而那不是"守卫没被守住",是脚本自己坏了。
    """
    return "\r\n" if "\r\n" in text else "\n"


def with_newline(pattern, nl):
    return pattern.replace("\n", nl)


def build(vcvars, cmake):
    """重编主程序。返回 (ok, 输出)。"""
    batfile = os.path.join(tempfile.gettempdir(), "negctl_action_integration_build.bat")
    with io.open(batfile, "w", encoding="ascii", newline="\r\n") as f:
        f.write("@echo off\r\n")
        f.write('call "%s" >nul 2>&1\r\n' % vcvars)
        f.write('cd /d "%s"\r\n' % BUILD_DIR)
        f.write('"%s" --build . --target hmi\r\n' % cmake)
        f.write("exit /b %ERRORLEVEL%\r\n")
    r = subprocess.run(["cmd", "/c", batfile], cwd=ROOT,
                       capture_output=True, text=True, errors="replace")
    ok = (r.returncode == 0)
    return ok, (r.stdout + r.stderr)[-3000:]


def run_short(minutes=2, tag="x"):
    """跑一次短跑实测。返回 (退出码, 从站写帧计数, 应用告警行, 输出)。

    ★★★ 每一轮必须有自己的 -OutDir ★★★
      【踩过】三个阶段共用一个 OutDir,从站日志是【追加模式】写的,
      于是第②轮数出来的"写帧"里混着第①轮的 —— 变异明明咬住了,
      判据却报"绿"。这种假绿比假红更坏:它会让人以为验证过了。
      每轮一个独立目录,写帧数就是这一轮自己的,谁也污染不了谁。

    ★ 顺带把【应用自己的告警行】也数出来(第二重证据)★
      判据仍然是"从站写帧数"(从站不会撒谎),这一重是给人看的:
      变异那一轮应当能看见 "[Action] 联动失败" —— 也就是说,
      这次失败【留下了线索】,而不是像 2026-10-25 那样一声不吭。
      (那次一声不吭的真正原因,后来查出来是日志级别过滤的 bug,
       见 docs/现状.md 的 BUGFIX-C6。)
    """
    outdir = os.path.join(ROOT, "soaks", "negctl_int_%s_%d" % (tag, os.getpid()))
    if os.path.isdir(outdir):
        shutil.rmtree(outdir)
    r = subprocess.run(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
         "-File", SCRIPT, "-Minutes", str(minutes), "-OutDir", outdir],
        cwd=ROOT, capture_output=True, text=True, errors="replace")
    out = r.stdout + r.stderr
    # 从站日志里的写帧计数 —— 这是【唯一】的判据(从站不会撒谎)
    nwrite = 0
    slave_log = os.path.join(outdir, "slave.log")
    if os.path.isfile(slave_log):
        with io.open(slave_log, encoding="utf-8", errors="replace") as f:
            for line in f:
                if "FC6 写" in line or "FC16 写" in line:
                    nwrite += 1
    # 应用日志里的告警行(第二重证据,给人看)
    warns = []
    logdir = os.path.join(outdir, "appdata", "QianWei", "HMI", "logs")
    if os.path.isdir(logdir):
        for name in sorted(os.listdir(logdir)):
            if not name.endswith(".log"):
                continue
            with io.open(os.path.join(logdir, name), encoding="utf-8",
                         errors="replace") as f:
                for line in f:
                    if "[WARN" in line and ("Action" in line or "mismatch" in line):
                        warns.append(line.strip()[:160])
    return r.returncode, nwrite, warns, out


def main():
    for f in (ACTION_MANAGER, SCRIPT):
        if not os.path.isfile(f):
            print("找不到文件: " + f)
            return 1
    if not os.path.isfile(EXE):
        print("找不到 exe: " + EXE + "(先编)")
        return 1
    vcvars = find_vcvars()
    cmake = find_cmake()
    if not vcvars or not cmake:
        print("找不到 vcvars64.bat / cmake.exe")
        return 1

    original = io.open(ACTION_MANAGER, encoding="utf-8", newline="").read()
    nl = detect_newline(original)
    old_nl = with_newline(MUTATION_OLD, nl)
    new_nl = with_newline(MUTATION_NEW, nl)
    if old_nl not in original:
        print("!! 原片段没找到(源文件结构变了?)")
        return 1

    bak = ACTION_MANAGER + ".negctl.bak"
    shutil.copy2(ACTION_MANAGER, bak)
    try:
        # ---- ① 基线 ----------------------------------------------------
        print("=== ① 基线(修好的二进制)===")
        ok, out = build(vcvars, cmake)
        if not ok:
            print("基线编译失败:\n", out)
            return 1
        rc, nwrite, warns, out = run_short(tag="base")
        print("  退出码=%s  从站写帧=%d" % (rc, nwrite))
        for line in out.splitlines():
            if "结论" in line or "FC6 写帧" in line or "FC16 写帧" in line:
                print("  " + line.strip())
        for w in warns[:3]:
            print("  告警: " + w)
        baseline_ok = (nwrite > 0)
        if not baseline_ok:
            print("!! 基线就没有写帧 —— 这一轮什么也验不了(先修基线)")
            return 1

        # ---- ② 变异 ----------------------------------------------------
        print("\n=== ② 变异(%s)===" % MUTATION_NAME)
        io.open(ACTION_MANAGER, "w", encoding="utf-8", newline="").write(
            original.replace(old_nl, new_nl, 1))
        ok, out = build(vcvars, cmake)
        if not ok:
            print("  变异后编译失败(编译不过也算红,但要看清原因):\n", out[-1500:])
            return 1
        rc, nwrite, warns, out = run_short(tag="mut")
        print("  退出码=%s  从站写帧=%d" % (rc, nwrite))
        for line in out.splitlines():
            if "结论" in line or "FC6 写帧" in line or "FC16 写帧" in line:
                print("  " + line.strip())
        # ★ 第二重证据(不是判据,是给人看的):★
        #   变异这一轮必须【留下线索】—— "联动失败"的告警得看得见。
        #   2026-10-25 那次判据咬住了却"一声不吭",根因就是告警被日志
        #   级别过滤掉了(见 BUGFIX-C6)—— 失败连线索都不留,现场只能靠猜。
        for w in warns[:3]:
            print("  告警: " + w)
        if not warns:
            print("  ★ 没有任何告警行 —— 这次失败【连线索都没留下】★")
        # ★ 判据:变异后【必须收不到写帧】。
        #   收到了 = 这道闸根本没被测到 —— 那这次整链验证就是空的。
        mutated_red = (nwrite == 0)
        if not mutated_red:
            print("★ 变异后【仍然收到了写帧】—— 判据没咬住 ★")

        # ---- ③ 还原 ----------------------------------------------------
        print("\n=== ③ 还原 ===")
        # ★ 必须 copy 而不是 copy2:copy2 会把【备份的 mtime】一起带回来,
        #   而备份是从改之前的文件拷的 —— mtime 变旧,ninja 判定"不用重编",
        #   于是还原后跑的还是被变异的二进制。
        shutil.copy(bak, ACTION_MANAGER)
        ok, out = build(vcvars, cmake)
        if not ok:
            print("还原后编译失败:\n", out)
            return 1
        rc, nwrite, warns, out = run_short(tag="restore")
        print("  退出码=%s  从站写帧=%d" % (rc, nwrite))
        for line in out.splitlines():
            if "结论" in line or "FC6 写帧" in line or "FC16 写帧" in line:
                print("  " + line.strip())
        for w in warns[:3]:
            print("  告警: " + w)
        restored_green = (nwrite > 0)
        if not restored_green:
            print("★ 还原后【收不到写帧】—— 还原没成功,或基线本身不稳 ★")
    finally:
        # 无论如何都要还原源文件
        if os.path.isfile(bak):
            shutil.copy(bak, ACTION_MANAGER)
            os.remove(bak)
        print("\n已还原源文件")

    if baseline_ok and mutated_red and restored_green:
        print("\n★ 结论:基线绿(收到写帧)、变异红(收不到写帧)、还原后绿 —— "
              "整条链的判据确实咬得住 ★")
        return 0
    print("\n★ 结论:不符合预期 ★")
    return 1


if __name__ == "__main__":
    sys.exit(main())
