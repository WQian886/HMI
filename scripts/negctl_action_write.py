# -*- coding: utf-8 -*-
"""负向对照:把"报警→联动→PLC 写"这条路上的关键守卫拆掉,测试必须变红。

★ 绿色的测试不一定是有效的测试 —— 它得"咬自己一口"才算数。★

这一条纪律本项目已经反复吃过亏:
  · G-11「测了 above 从没测 below」—— 一个方向漏了,判据重叠活了 24 小时
  · F1「只断言了唯一没坏的两样」
  · BUGFIX-C3「主画面报警表整张表都废掉,而 68 个测试全绿」
    —— 因为主画面那套【一条测试都没有】
  · ★ 而这次修的这条:24 小时长跑实测发现"温度高 → 速度写 0"(停机)
    【一次都没执行过】,而当时【一条测试都没有】

用法:
    python scripts/negctl_action_write.py

它会临时改源文件 → 编 → 跑 → 还原 → 打印结果 → 还原后再跑一次确认是绿的。
**任何时候都不会留下被改坏的源文件**(finally 里还原)。

退出码:
    0 = 基线绿、全部变异红、还原后绿
    1 = 基线就不绿 / 变异没变红 / 还原后不绿  ← 这一条才是真出事

★ 2026-10-26 加了 M5~M8(BUGFIX-D1:轮询回包不许覆盖"待写"的值):
    M5 拆掉 PollScheduler 的 isDirty 守卫
    M6 守卫写宽了(只判 isDirty 不判 access)
    M7 跳过整个分支(把报警系统一起打死)
    M8 审计改回在 reply 时刻现取 v->value()
"""

import io
import os
import shutil
import subprocess
import sys
import tempfile
import time

# ---- 路径:全部从本文件位置推出来,不写死 ----------------------------------
# ★ 写死 `E:\qianwie\hmi` 的话,别人克隆下来就跑不了。
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS = os.path.join(ROOT, "hmi", "tests")
BUILD = os.path.join(TESTS, "build_ninja")
TARGET = "tst_ActionWrite"

ACTION_MANAGER = os.path.join(ROOT, "hmi", "ActionManager.cpp")
VALUE_COERCION = os.path.join(ROOT, "hmi", "src", "util", "ValueCoercion.cpp")
ACTION_WIRING = os.path.join(ROOT, "hmi", "src", "alarm", "ActionWiring.cpp")
WRITE_SCHEDULER = os.path.join(ROOT, "hmi", "WriteScheduler.cpp")
POLL_SCHEDULER = os.path.join(ROOT, "hmi", "PollScheduler.cpp")

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
    # ★ 【踩过】这里的路径必须是【完整绝对路径】,不能用
    #   os.path.join("D:", "Program Files\\...") ——
    #   那样拼出来是 "D:Program Files\\...",是【驱动器相对路径】,
    #   不是 "D:\Program Files\..."。
    #   症状:cmd 报 "The system cannot find the path specified.",
    #   看起来像"环境坏了",其实是路径少了个反斜杠。
    #   (平时没撞上,是因为 vswhere 那条路先成功了 ——
    #    也就是说这条退路【从来没被验证过】,而它正是没有 vswhere 的机器上的唯一出路。)
    for cand in (
            r"D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
            r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
            r"E:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"):
        if os.path.isfile(cand):
            return cand
    return None


def find_cmake():
    # ★ 绝对路径(理由同 find_vcvars 的注释:驱动器相对路径会被 cmd 吃掉)。
    return find_tool("cmake", extra_hints=(
        r"D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE"
        r"\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE"
        r"\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    ))


# ---- 变异清单:(名字, 文件, 原片段, 替换片段)-----------------------------
#
# ★ 每个变异都对应一条【具体的、这次修的】守卫。
#   变异不是"随便改坏",而是"把这次修的那道闸拆掉"——
#   拆完了测试要是还绿,说明这条闸根本没被测到。
MUTATIONS = [
    (
        "M1 拆掉 ActionManager 里的类型适配(= 2026-10-25 那个 bug 的原样)",
        ACTION_MANAGER,
        """    const QVariant typed = qwhmi::util::coerceToDataType(it->value, v->dataType());
    if (!typed.isValid()) {""",
        """    const QVariant typed = it->value;   // ★ 变异:不做类型适配
    if (false) {""",
    ),
    (
        "M2 把适配的『负数拒绝』改成静默回绕",
        VALUE_COERCION,
        """        if (!ok || n < 0 || n > std::numeric_limits<quint16>::max()) return {};
        return QVariant::fromValue<quint16>(static_cast<quint16>(n));""",
        """        if (!ok || n > std::numeric_limits<quint16>::max()) return {};
        return QVariant::fromValue<quint16>(static_cast<quint16>(n));   // ★ 变异:负数回绕""",
    ),
    (
        "M3 拆掉 WriteScheduler 的 dirty 检查(= 不标脏也照发)",
        WRITE_SCHEDULER,
        """        if (!v->isDirty()) continue;                            // 只发 dirty""",
        """        // ★ 变异:不查 dirty""",
    ),
    (
        "M4 抽掉 bindActionToAlarms 里的 connect(= 接线没了)",
        ACTION_WIRING,
        """    QObject::connect(alarmMgr, &AlarmManager::alarmTriggered,
                     actionMgr, &ActionManager::onAlarmTriggered,
                     Qt::UniqueConnection);""",
        """    // ★ 变异:接线没了""",
    ),
    # ---- BUGFIX-D1(2026-10-26):轮询回包不许覆盖"待写"的值 ----
    #
    # ★ M5 ~ M8 是这次修的那道闸的四条边。
    #   拆完了测试要是还绿,说明这条闸根本没被测到。
    (
        "M5 拆掉 PollScheduler 的 isDirty 守卫(= 读回照常覆盖待写的值)",
        POLL_SCHEDULER,
        """        const bool writePending = v->isDirty() && v->access() == Variable::Access::ReadWrite;""",
        """        const bool writePending = false;   // ★ 变异:守卫整个没了""",
    ),
    (
        "M6 守卫写宽了(= 只判 isDirty,不判 access)",
        POLL_SCHEDULER,
        """        const bool writePending = v->isDirty() && v->access() == Variable::Access::ReadWrite;""",
        """        const bool writePending = v->isDirty();   // ★ 变异:只读变量也被保护住""",
    ),
    (
        "M7 跳过【整个】分支(= 只该跳 setValue,却把报警系统一起打死了)",
        POLL_SCHEDULER,
        """        if (writePending) {
            // ★ 用 DEBUG 不用 INFO:这个事件在正常写流程里【本该】发生,""",
        """        if (writePending) {
            continue;   // ★ 变异:整个分支都跳过(setQuality/rawRead/nextPollAt 也没了)
        }
        if (false) {
            // ★ 用 DEBUG 不用 INFO:这个事件在正常写流程里【本该】发生,""",
    ),
    (
        "M7c 只跳过 rawRead(= setQuality 还在,报警却已经哑了)",
        POLL_SCHEDULER,
        """        emit rawRead(v, raw);
        nextPollAtMs_[v] = now + v->pollIntervalMs();""",
        """        if (!writePending) emit rawRead(v, raw);   // ★ 变异:待写的变量不发 rawRead
        nextPollAtMs_[v] = now + v->pollIntervalMs();""",
    ),
    (
        "M8 审计改回在 reply 时刻现取 v->value()(= 证据是假的)",
        WRITE_SCHEDULER,
        """    e.newValue     = sentValue;""",
        """    e.newValue     = v->value().toString();   // ★ 变异:reply 时刻现取""",
    ),
]


def run(cmd, cwd=None, env=None):
    return subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True,
                          errors="replace")


def failed_count(totals_line):
    """从 "Totals: 21 passed, 2 failed, ..." 里把 failed 的数取出来。

    ★ 【踩过】原来是 `if "0 failed" in totals[0]` ——
      而 "0 failed" 是 "10 failed" 的【子串】!
      于是 M4 明明红了("Totals: 11 passed, 10 failed"),
      脚本却判成"还是绿的",报"这条守卫没被守住"。
      —— 判据自己坏了,差点让我去改一个没坏的守卫。

      教训跟 harness 那次一样(判据长什么样,坏起来就长什么样):
      判据必须【解析】,不能【搜子串】。
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
    """这个文件自己用 LF 还是 CRLF。

    ★ 【踩过】ActionManager.cpp 是 CRLF(仓库里一直就是 CRLF),
      而 ValueCoercion.cpp / ActionWiring.cpp / WriteScheduler.cpp 是 LF。
      变异片段是手写的、用 \\n 拼的 —— 直接 replace 到 CRLF 文件上
      【永远匹配不上】,脚本会报"原片段没找到",而那不是"守卫没被守住",
      是脚本自己坏了(2026-10-25 第一次跑就撞上这个)。
    """
    return "\r\n" if "\r\n" in text else "\n"


def with_newline(pattern, nl):
    """把片段里的 \\n 换成这个文件实际的换行符。"""
    return pattern.replace("\n", nl)


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


def touch(path):
    """把文件的 mtime 顶到现在。

    ★★ 为什么非做不可 —— 这次真的踩到了 ★★
      还原源文件用的是 shutil.copy(备份, 源文件)。
      copy 【保留 mtime】,而备份是从"改之前"拷的 —— 于是还原后
      源文件的 mtime 变【旧】了,比 .obj 还旧。
      ninja 靠 mtime 判断"要不要重编",于是在它眼里:
        源文件(旧) < .obj(新) → "不用重编"
      结果:还原之后跑的还是【被变异的那个二进制】。
      症状极隐蔽:最后一句"还原后:xx failed"莫名其妙地不绿,
      而所有变异都"红"了 —— 看起来像守卫太严,其实是二进制没重编。

      ★ 这个坑在 2026-10-26 手工跑变异时真的踩到过:
        手工还原用 Copy-Item(也保留 mtime),
        结果"只读变量照常被覆盖"那条用例在【没改过生产代码】的
        情况下红 —— 查下去才发现 lib 里还是上一次变异的产物。
        一度以为是守卫写错了。

      所以:每次还原之后,必须让 ninja 知道源文件变过了。
      copy 之后 touch 一下,让它重新编。
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
    for f in (ACTION_MANAGER, VALUE_COERCION, ACTION_WIRING, WRITE_SCHEDULER,
              POLL_SCHEDULER):
        if not os.path.isfile(f):
            print("找不到源文件: " + f)
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

    batfile = os.path.join(tempfile.gettempdir(), "negctl_action_write_build.bat")
    write_build_bat(batfile, vcvars, cmake)

    # 每个要改的文件各留一份备份
    backups = {}
    originals = {}
    for _, path, _, _ in MUTATIONS:
        if path in backups:
            continue
        backups[path] = path + ".negctl.bak"
        shutil.copy2(path, backups[path])
        originals[path] = io.open(backups[path], encoding="utf-8", newline="").read()

    try:
        # ---- 先量基线 ----
        res, out = build_and_run(batfile)
        if res is None:
            print("基线编译失败:\n", out[-3000:])
            return 1
        baseline_totals = [l for l in res if "Totals:" in l]
        print("基线:", baseline_totals[0] if baseline_totals else res)
        if not baseline_totals or failed_count(baseline_totals[0]) != 0:
            print("!! 基线就不绿(或者崩了),先修基线再谈负向对照")
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
                # ★ 没有 Totals 行 = 测试进程【崩了】(比如对 nullptr 解引用)——
                #   那也是红,而且是最响的那种红。
                #   绝不能因为"没读到 Totals"就判成绿。
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
            # 还原这一份,再跑下一个(不留脏状态)
            shutil.copy(backups[path], path)
            touch(path)      # ★ copy 保留 mtime → 不 touch 的话 ninja 不会重编
    finally:
        # ★ 必须 copy 而不是 copy2:copy2 会把【备份的 mtime】一起带回来,
        #   而备份是从改之前的文件拷的 —— mtime 变旧,ninja 判定"不用重编",
        #   于是还原后跑的还是被变异的二进制,最后一句"还原后:xx failed"就是这么来的。
        # ★ 而且 copy 之后【必须 touch】:copy 本身就保留 mtime,
        #   所以光换掉 copy2 还不够 —— 见 touch() 的注释。
        for path, bak in backups.items():
            shutil.copy(bak, path)
            touch(path)
            os.remove(bak)
        print("\n已还原源文件")

    # 还原后必须重新变绿
    res, out = build_and_run(batfile)
    if res is None:
        print("还原后编译失败:\n", out[-3000:])
        return 1
    totals = [l for l in res if "Totals:" in l]
    print("还原后:", totals[0] if totals else ("★ 没有 Totals 行(崩了)★"))
    green_after = bool(totals and failed_count(totals[0]) == 0)
    if not green_after:
        return 1
    if not all_red:
        print("\n★ 结论:有变异【没有变红】—— 对应的守卫没被守住 ★")
        return 1
    print("\n★ 结论:基线绿、%d 个变异全红、还原后绿 —— 守卫确实被守住了 ★"
          % len(MUTATIONS))
    try:
        os.remove(batfile)
    except OSError:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
