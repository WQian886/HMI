# -*- coding: utf-8 -*-
"""负向对照:把 BUGFIX-E1「写排队」的闸拆掉,tst_ModbusWriteQueue 必须变红。

★ 绿色的测试不一定是有效的测试 —— 它得"咬自己一口"才算数。★

这一条纪律本项目已经反复吃过亏:
  · G-11「测了 above 从没测 below」—— 一个方向漏了,判据重叠活了 24 小时
  · F1「只断言了唯一没坏的两样」
  · BUGFIX-C3「主画面报警表整张表都废掉,而 68 个测试全绿」
  · G-15「长跑的 idle 判据只看 CPU,分不出'冻住'和'真在跑'」
  · ★ 而这次修的这条:写和读抢同一个在途槽,写【可能整轮 24 小时都出不去】,
    而当时【一条测试都没有】,读还照常成功 ⇒ 活体判据照样绿。

用法:
    python scripts/negctl_write_queue.py

它会临时改源文件 → 编 → 跑 → 还原 → 打印结果 → 还原后再跑一次确认是绿的。
**任何时候都不会留下被改坏的源文件**(finally 里还原 + touch)。

退出码:
    0 = 基线绿、全部变异红、还原后绿
    1 = 基线就不绿 / 变异没变红 / 还原后不绿  ← 这一条才是真出事

★ 2026-10-26 加的 W1~W7:
  BUGFIX-E1(写排队,不再跟读抢在途槽):
    W1 拆掉排队分支(= 改之前的原样:直接拒收)
    W2 排队时存【现取的】值(= 发送时刻取 v->value(),排队期间变了就发错值)
    W3 重连时不作废排队里的写(= 它会被发到【旧连接】那台 PLC 上去)
    W4 作废时【不发】writeFinished(= WriteScheduler 的 pending_ 永远清不掉)
    W5 把校验挪到排队【后面】(= 不合法的写也会被排队,发出畸形帧)
  BUGFIX-E2(在途请求在断线/重连时也必须收到失败通知):
    W6 断线时不通知在途请求(= PollScheduler/WriteScheduler 永久泄漏)
    W7 重连时不通知在途请求(= 同上)

★ 【这个脚本自己抓到过一个"测试漏了"】★
  W7 第一版【没有变红】—— 因为测试【只】覆盖了断线路径,没覆盖重连路径。
  那不代表重连路径是对的,只代表【没测到】。
  ⇒ 补了 ⑨ 用例(重连时在途写必须收到 writeFinished),W7 才变红。
  这正是本项目反复吃亏的地方:"没测到"被当成了"没问题"。
"""

import io
import os
import shutil
import subprocess
import sys
import tempfile
import time

# ---- 路径:全部从本文件位置推出来,不写死 ----------------------------------
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS = os.path.join(ROOT, "hmi", "tests")
BUILD = os.path.join(TESTS, "build_ninja")
TARGET = "tst_ModbusWriteQueue"

MODBUS_MASTER = os.path.join(ROOT, "hmi", "ModbusMaster.cpp")


# ---- 工具链 ---------------------------------------------------------------
def find_tool(name, extra_hints=()):
    from shutil import which
    p = which(name)
    if p:
        return p
    for hint in extra_hints:
        if os.path.isfile(hint):
            return hint
    return None


def find_vcvars():
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
            r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
            r"E:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"):
        if os.path.isfile(cand):
            return cand
    return None


def find_cmake():
    return find_tool("cmake", extra_hints=(
        r"D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE"
        r"\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        r"C:\Program Files\Microsoft Visual Studio\2022\Common7\IDE"
        r"\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    ))


# ---- 变异清单:(名字, 文件, 原片段, 替换片段)-----------------------------
#
# ★ 每个变异都对应这次修的【一条具体的守卫】。
#   变异不是"随便改坏",而是"把这次修的那道闸拆掉"——
#   拆完了测试要是还绿,说明这条闸根本没被测到。
MUTATIONS = [
    (
        "W1 拆掉排队分支(= 改之前的原样:槽被占就拒收)",
        MODBUS_MASTER,
        """    if (!pending_.isEmpty()) {
        if (queuedWrites_.size() >= kMaxQueuedWrites) {""",
        """    if (!pending_.isEmpty()) {
        if (true) {   // ★ 变异:直接拒收,不排队""",
    ),
    (
        "W2 排队时不存值(= 发送时刻才取,排队期间变了就发错值)",
        MODBUS_MASTER,
        """        qw.values        = writeUnit.values();""",
        """        qw.values.clear();   // ★ 变异:不存入队时的值""",
    ),
    (
        "W3 重连时不作废排队里的写(= 它会被发到【旧连接】上去)",
        MODBUS_MASTER,
        """    failQueuedWrites(QStringLiteral("新连接前清空旧请求,排队的写作废"));""",
        """    // ★ 变异:重连时不作废""",
    ),
    (
        "W4 作废时【不发】writeFinished(= 调用方永远等一个不来的 reply)",
        MODBUS_MASTER,
        """        emit writeFinished(qw.requestId, false);
    }
    logCommError(QStringLiteral("排队的写作废(%1 条):%2").arg(doomed.size()).arg(reason));""",
        """        // ★ 变异:不发 writeFinished
    }
    logCommError(QStringLiteral("排队的写作废(%1 条):%2").arg(doomed.size()).arg(reason));""",
    ),
    (
        "W5 把校验挪到排队【后面】(= 不合法的写也会被排队,发出畸形帧)",
        MODBUS_MASTER,
        """    if (serverAddress < 0 || serverAddress > 0xFF) {
        emit errorOccurred(QString("serverAddress 非法:%1").arg(serverAddress));
        return nullptr;
    }
    if (writeUnit.startAddress() < 0 || writeUnit.startAddress() > 0xFFFF) {
        emit errorOccurred(QString("addr 非法:%1").arg(writeUnit.startAddress()));
        return nullptr;
    }
    if (writeUnit.values().isEmpty()) {
        emit errorOccurred("写请求 values 不能为空");
        return nullptr;
    }""",
        """    // ★ 变异:校验被挪到排队【后面】了(不合格的写也会进队列)""",
    ),
    (
        "W6 断线时不通知在途请求(= PollScheduler/WriteScheduler 永久泄漏)",
        MODBUS_MASTER,
        """        failPendingRequests(QStringLiteral("TCP 断开,在途请求作废"));""",
        """        // ★ 变异:在途请求不发失败通知""",
    ),
    (
        "W7 重连时不通知在途请求(= 同上)",
        MODBUS_MASTER,
        """    failPendingRequests(QStringLiteral("新连接前清空旧请求"));""",
        """    // ★ 变异:在途请求不发失败通知""",
    ),
]


def run(cmd, cwd=None, env=None):
    return subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True,
                          errors="replace")


def failed_count(totals_line):
    """从 "Totals: 6 passed, 0 failed, ..." 里把 failed 的数取出来。

    ★ 【踩过】不能用 `if "0 failed" in totals[0]` ——
      "0 failed" 是 "10 failed" 的【子串】!判据自己坏掉的时候,
      长得跟守卫没坏一模一样。
    """
    if not totals_line:
        return None
    for part in totals_line.split(","):
        part = part.strip()
        if part.endswith("failed"):
            try:
                return int(part[:-len("failed")].strip())
            except ValueError:
                return None
    return None


def detect_newline(text):
    return "\r\n" if "\r\n" in text else "\n"


def with_newline(pattern, nl):
    return pattern.replace("\n", nl)


def write_build_bat(batfile, vcvars, cmake):
    with io.open(batfile, "w", encoding="ascii", newline="\r\n") as f:
        f.write("@echo off\r\n")
        f.write('call "%s" >nul 2>&1\r\n' % vcvars)
        f.write('cd /d "%s"\r\n' % TESTS)
        f.write('"%s" --build build_ninja --target %s\r\n' % (cmake, TARGET))
        f.write("exit /b %ERRORLEVEL%\r\n")


def touch(path):
    """把 mtime 顶到现在 —— 还原之后 ninja 才知道要重编。
    (shutil.copy 保留 mtime;源文件比 .obj 还旧 ⇒ ninja 判定"不用重编",
     于是跑的还是被变异的二进制。踩过好几次。)
    """
    now = time.time()
    os.utime(path, (now, now))


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
    if not os.path.isfile(MODBUS_MASTER):
        print("找不到源文件: " + MODBUS_MASTER)
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
        return 1

    batfile = os.path.join(tempfile.gettempdir(), "negctl_write_queue_build.bat")
    write_build_bat(batfile, vcvars, cmake)

    backups = {}
    originals = {}
    for _n, path, _o, _w in MUTATIONS:
        if path in backups:
            continue
        backups[path] = path + ".negctl.bak"
        shutil.copy2(path, backups[path])
        originals[path] = io.open(backups[path], encoding="utf-8", newline="").read()

    try:
        res, out = build_and_run(batfile)
        if res is None:
            print("基线编译失败:\n", out[-3000:])
            return 1
        baseline_totals = [l for l in res if "Totals:" in l]
        print("基线:", baseline_totals[0] if baseline_totals else res)
        if not baseline_totals or failed_count(baseline_totals[0]) != 0:
            print("!! 基线就不绿(或者崩了),先修基线再谈负向对照")
            print(out[-2000:])
            return 1

        all_red = True
        for name, path, old, new in MUTATIONS:
            original = originals[path]
            nl = detect_newline(original)
            old_nl = with_newline(old, nl)
            new_nl = with_newline(new, nl)
            if old_nl not in original:
                print("!! %s:原片段没找到(源文件结构变了?)" % name)
                all_red = False
                continue
            mutated = original.replace(old_nl, new_nl, 1)
            io.open(path, "w", encoding="utf-8", newline="").write(mutated)
            res, out = build_and_run(batfile)
            if res is None:
                print("%s:编译失败(编译不过也算红,但要看清原因)" % name)
                print(out[-1500:])
                all_red = False
            else:
                totals = [l for l in res if "Totals:" in l]
                fails = [l for l in res if l.startswith("FAIL!")]
                nf = failed_count(totals[0]) if totals else None
                if not totals:
                    status = "红 ✓(进程崩了,连 Totals 都没打出来)"
                elif nf == 0:
                    status = "★ 还是绿的 ★  ← 这条守卫没被守住"
                    all_red = False
                else:
                    status = "红 ✓"
                print("%s: %s" % (name, status))
                if totals:
                    print("     ", totals[0])
                for f in fails[:4]:
                    print("     ", f[:170])
            shutil.copy(backups[path], path)
            touch(path)      # ★ copy 保留 mtime → 不 touch 的话 ninja 不会重编
    finally:
        for path, bak in backups.items():
            shutil.copy(bak, path)
            touch(path)
            os.remove(bak)
        print("\n已还原源文件")

    res, out = build_and_run(batfile)
    if res is None:
        print("还原后编译失败:\n", out[-3000:])
        return 1
    totals = [l for l in res if "Totals:" in l]
    print("还原后:", totals[0] if totals else res)
    if not totals or failed_count(totals[0]) != 0:
        print(out[-2500:])
        return 1
    if not all_red:
        print("\n★ 结论:有变异【没有变红】—— 对应的守卫没被守住 ★")
        return 1
    print("\n★ 结论:基线绿、%d 个变异全红、还原后绿 —— 闸确实被守住了 ★" % len(MUTATIONS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
