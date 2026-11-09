# -*- coding: utf-8 -*-
"""状态机迁移表的负向对照:把表改坏,判据【必须判红】。

★ 为什么需要这个脚本 ★
  ----------------------------------------------------------
  状态迁移表(4 状态 × 6 触发源 = 24 种组合)以前整张写在 hmi.h / hmi.cpp 里,
  而它们不在 hmi_core 里 ⇒ 没有任何一条单元测试能看见它
  ⇒ 它一直只是"设计",从来不是"钉子"。

  G-26 就是在这张表旁边出的事:判据写在 hmi.cpp 里没人看得见
  ⇒ 一条 Info 报警就把机器打成 Fault。

  现在表搬到了 src/state/StateMachinePolicy.{h,cpp},并配了 tst_StateMachinePolicy。
  但一条测试如果【从来没红过】,它证明不了任何事。

  所以这个脚本做四件事,都是"把闸拆了看它会不会响":

    M1(删条目)删掉 Running + AlarmCritical → Fault
            ⇒ tst_StateMachinePolicy 必须【红】
              (红的应是"整张真值表"那条)

    M2(改目标)把 Fault + ResetButton 的目标从 Stopped 改成 Resetting
            ⇒ tst_StateMachinePolicy 必须【红】
              (红的应是"故障态复位直接回 Stopped"那条)

    M3(加条目)让 AlarmCleared 也能迁移
            ⇒ tst_StateMachinePolicy 必须【红】
              (红的应是"报警恢复故意没有迁移"那条)
            ★ 这组最要紧:真加上了,机器就会在报警反复越限时
              【自己爬起来、跑两秒、又自己停】—— 比停着更危险。

    M4(接线层)让 hmi.cpp 的 tryTransition 不再调共享模块
            ⇒ tst_StateMachinePolicy 必须【红】
              (红的应是"hmi.cpp 真的用了共享模块"那条源码文本断言)
            ★ M4 【不需要重编】:hmi.cpp 不在 hmi_core 里,
              测试是【运行时读磁盘上的 hmi.cpp 文本】。

    M5(格式)把迁移留痕那行的格式改掉("--(X)-->" 改成 "->(X)->")
            ⇒ tst_StateMachinePolicy 必须【红】
              (红的应是"格式逐字钉住"那条)
            ★ 这组最阴:格式一改,进程级判据 J13 就【数不到】那些行了,
              而"数不到"在报告里跟"没迁移"长得一模一样 —— 假绿。

  判据红的唯一依据是【ctest 的退出码】——不是本脚本自己的判断。

用法:
    python -X utf8 -I scripts/negctl_state_machine.py

退出码:
    0 = 基线绿、4 组变异全红、还原后绿、还原逐字节一致
    1 = 任一环节不符合预期
"""

import hashlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import time

# ---- 路径:全部从本文件位置推出来,不写死 ----------------------------------
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
POLICY_CPP = os.path.join(ROOT, "hmi", "src", "state", "StateMachinePolicy.cpp")
HMI_CPP = os.path.join(ROOT, "hmi", "hmi.cpp")
TEST_BUILD_DIR = os.path.join(ROOT, "hmi", "tests", "build_ninja")
TEST_NAME = "tst_StateMachinePolicy"


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


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
    """找 cmake.exe(ctest.exe 跟它同目录)。

    ★ 【踩过】路径必须是【完整绝对路径】,不能用
      os.path.join("D:", "Program Files\\...") ——
      那样拼出来是 "D:Program Files\\...",是【驱动器相对路径】,
      cmd 报 "The system cannot find the path specified.",
      看起来像"环境坏了",其实是路径少了个反斜杠。
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


def read_text(path):
    # ★ newline="" —— 不让 Python 自己做换行转换,
    #   否则写回去的时候整个文件的换行符会被改一遍(CRLF → LF),
    #   还原就【不彻底】了。
    with io.open(path, encoding="utf-8", newline="") as f:
        return f.read()


def write_text(path, text):
    """写回源文件。带重试 —— 杀毒软件的实时扫描会在刚改过的文件上
    短暂持有句柄,那个窗口里写会抛 PermissionError。"""
    last = None
    for attempt in range(12):
        try:
            with io.open(path, "w", encoding="utf-8", newline="") as f:
                f.write(text)
            return
        except PermissionError as e:
            last = e
            time.sleep(0.5)
    raise RuntimeError("写 %s 失败:%s" % (path, last))


def build_test(vcvars, cmake):
    """重编被测测试。返回 (ok, 输出)。"""
    batfile = os.path.join(tempfile.gettempdir(), "negctl_state_machine_build.bat")
    with io.open(batfile, "w", encoding="ascii", newline="\r\n") as f:
        f.write("@echo off\r\n")
        f.write('call "%s" >nul 2>&1\r\n' % vcvars)
        f.write('cd /d "%s"\r\n' % TEST_BUILD_DIR)
        f.write('"%s" --build . --target %s\r\n' % (cmake, TEST_NAME))
        f.write("exit /b %ERRORLEVEL%\r\n")
    r = subprocess.run(["cmd", "/c", batfile], cwd=ROOT,
                       capture_output=True, text=True, errors="replace")
    return (r.returncode == 0), (r.stdout + r.stderr)[-3000:]


def run_test():
    """跑一次被测测试。返回 (是否通过, ctest 输出)。

    ★ 判据是【ctest 的退出码】,不是本脚本对输出的解读 ——
      "看输出像不像红了"是本项目反复吃过的那种自欺。
    """
    ctest = os.path.join(os.path.dirname(find_cmake() or ""), "ctest.exe")
    r = subprocess.run(
        [ctest, "-R", TEST_NAME, "--output-on-failure"],
        cwd=TEST_BUILD_DIR, capture_output=True, text=True, errors="replace")
    return (r.returncode == 0), (r.stdout + r.stderr)


# ---- 变异工具:按【行】改,不按整段字符串改 ------------------------------
#   为什么不用整段字符串:那张表的对齐空格很容易在改代码时被顺手调一下,
#   整段匹配就会莫名其妙地"找不到原片段" —— 那是脚本在闹,不是变异生效了。
#   按行 + 按关键词定位,抗排版变化。

def mutate_line(path, must_all, new_line=None, must_not=None, replace_in_line=None):
    """找到【同时包含 must_all 里所有子串】的那一行,把它改掉。

    new_line      —— 整行替换成它
    replace_in_line —— 行内替换 (旧, 新);跟 new_line 二选一
    返回 True/False(找没找到)
    """
    original = read_text(path)
    nl = "\r\n" if "\r\n" in original else "\n"
    lines = original.split(nl)
    hit = -1
    for i, line in enumerate(lines):
        if all(s in line for s in must_all):
            if must_not and any(s in line for s in must_not):
                continue
            hit = i
            break
    if hit < 0:
        return False
    if new_line is not None:
        lines[hit] = new_line
    else:
        old, new = replace_in_line
        if old not in lines[hit]:
            return False
        lines[hit] = lines[hit].replace(old, new, 1)
    write_text(path, nl.join(lines))
    return True


def restore(path, backup):
    """还原源文件。

    ★ 必须 shutil.copy 而不是 copy2:copy2 会把【备份的 mtime】一起带回来,
      而备份是从改之前的文件拷的 —— mtime 变旧,ninja 判定"不用重编",
      于是还原后跑的还是被变异的二进制。
      (见 memory:hmi-restore-file-mtime-trap。)

    ★ 重试是实测踩出来的:Windows 的杀毒软件会在"刚被改过的文件"上
      短暂持有句柄,那个窗口里 copy 直接抛 PermissionError ——
      第一次跑 negctl_alarm_trip.py 时,还原 hmi.cpp 就是这么失败的,
      结果源码被留在【变异状态】。那才是最坏的结果。
    """
    last = None
    for attempt in range(12):
        try:
            shutil.copy(backup, path)
            return True
        except PermissionError as e:
            last = e
            time.sleep(0.5)
    raise RuntimeError(
        "还原 %s 失败(重试 12 次仍是 PermissionError:%s)。"
        "备份还在:%s —— 请手动 copy 回去。" % (path, last, backup))


def main():
    for f in (POLICY_CPP, HMI_CPP):
        if not os.path.isfile(f):
            print("找不到文件: " + f)
            return 1
    if not os.path.isdir(TEST_BUILD_DIR):
        print("找不到测试构建目录: " + TEST_BUILD_DIR + "(先编一次)")
        return 1
    vcvars = find_vcvars()
    cmake = find_cmake()
    if not vcvars or not cmake:
        print("找不到 vcvars64.bat / cmake.exe")
        return 1

    sha_before = {POLICY_CPP: sha256_of(POLICY_CPP),
                  HMI_CPP: sha256_of(HMI_CPP)}
    bak_policy = POLICY_CPP + ".negctl.bak"
    bak_hmi = HMI_CPP + ".negctl.bak"
    shutil.copy(POLICY_CPP, bak_policy)
    shutil.copy(HMI_CPP, bak_hmi)

    results = {}
    try:
        # ---- ① 基线 ----------------------------------------------------
        print("=== ① 基线(修好的代码)===")
        ok, out = build_test(vcvars, cmake)
        if not ok:
            print("基线编译失败:\n", out)
            return 1
        green, out = run_test()
        print("  ctest 通过=%s" % green)
        if not green:
            print("!! 基线就不绿 —— 这一轮什么也验不了(先修基线)")
            print(out[-2000:])
            return 1
        results["baseline_green"] = True

        # ---- ② M1:删掉一条迁移 -------------------------------------------
        print("\n=== ② M1(删条目) 删掉 Running + AlarmCritical → Fault ===")
        if not mutate_line(
                POLICY_CPP,
                must_all=["MachineState::Running", "MachineTrigger::AlarmCritical"],
                new_line="        // ★ 变异:Running + AlarmCritical 这一条被删掉了"):
            print("!! 找那一行失败(源文件结构变了?)")
            return 1
        ok, out = build_test(vcvars, cmake)
        if not ok:
            print("  变异后编译失败(编译不过也算红):\n", out[-1500:])
            results["m1_red"] = True
        else:
            green, out = run_test()
            print("  ctest 通过=%s" % green)
            results["m1_red"] = (not green)
            if green:
                print("★ M1 变异后【仍然全绿】—— 判据没咬住 ★")
        restore(POLICY_CPP, bak_policy)

        # ---- ③ M2:改一条的目标状态 ---------------------------------------
        print("\n=== ③ M2(改目标) Fault + ResetButton → Resetting(应为 Stopped) ===")
        if not mutate_line(
                POLICY_CPP,
                must_all=["MachineState::Fault", "MachineTrigger::ResetButton"],
                replace_in_line=("MachineState::Stopped", "MachineState::Resetting")):
            print("!! 找那一行失败(源文件结构变了?)")
            return 1
        ok, out = build_test(vcvars, cmake)
        if not ok:
            print("  变异后编译失败(编译不过也算红):\n", out[-1500:])
            results["m2_red"] = True
        else:
            green, out = run_test()
            print("  ctest 通过=%s" % green)
            results["m2_red"] = (not green)
            if green:
                print("★ M2 变异后【仍然全绿】—— 判据没咬住 ★")
        restore(POLICY_CPP, bak_policy)

        # ---- ④ M3:让 AlarmCleared 也能迁移 --------------------------------
        print("\n=== ④ M3(加条目) 让 AlarmCleared 也能迁移 ===")
        print("   ★ 这组最要紧:真加上了,机器会在报警反复越限时"
              "【自己爬起来、跑两秒、又自己停】")
        if not mutate_line(
                POLICY_CPP,
                must_all=["const auto it = transitionTable().find("],
                new_line=(
                    "    if (t == MachineTrigger::AlarmCleared) {"
                    " if (out) *out = from; return true; }   // ★ 变异:报警恢复也放行\n"
                    "    const auto it = transitionTable().find(qMakePair(from, t));")):
            print("!! 找那一行失败(源文件结构变了?)")
            return 1
        ok, out = build_test(vcvars, cmake)
        if not ok:
            print("  变异后编译失败(编译不过也算红):\n", out[-1500:])
            results["m3_red"] = True
        else:
            green, out = run_test()
            print("  ctest 通过=%s" % green)
            results["m3_red"] = (not green)
            if green:
                print("★ M3 变异后【仍然全绿】—— 判据没咬住 ★")
        restore(POLICY_CPP, bak_policy)

        # ---- ⑤ M4:接线层(不用重编)---------------------------------------
        print("\n=== ⑤ M4(接线层) hmi.cpp 的 tryTransition 不再调共享模块 ===")
        if not mutate_line(
                HMI_CPP,
                must_all=["qwhmi::state::nextState(from, t, &to)"],
                replace_in_line=("if (!qwhmi::state::nextState(from, t, &to)) {",
                                 "if (false) {   // ★ 变异:绕过共享判据")):
            print("!! 找那一行失败(源文件结构变了?)")
            return 1
        green, out = run_test()
        print("  ctest 通过=%s" % green)
        results["m4_red"] = (not green)
        if green:
            print("★ M4 变异后【仍然全绿】—— 源码文本那条断言没咬住 ★")
        restore(HMI_CPP, bak_hmi)

        # ---- ⑥ M5:改迁移留痕那行的格式 -----------------------------------
        print("\n=== ⑥ M5(格式) 把迁移留痕的 '--(X)-->' 改成 '->(X)->' ===")
        print("   ★ 这组最阴:格式一改,进程级判据 J13 就数不到那些行了,")
        print("     而『数不到』在报告里跟『没迁移』长得一模一样 —— 假绿。")
        if not mutate_line(
                POLICY_CPP,
                must_all=['QStringLiteral("[状态机] %1'],
                replace_in_line=("--(%2)-->", "->(%2)->")):
            print("!! 找那一行失败(源文件结构变了?)")
            return 1
        ok, out = build_test(vcvars, cmake)
        if not ok:
            print("  变异后编译失败(编译不过也算红):\n", out[-1500:])
            results["m5_red"] = True
        else:
            green, out = run_test()
            print("  ctest 通过=%s" % green)
            results["m5_red"] = (not green)
            if green:
                print("★ M5 变异后【仍然全绿】—— 格式那条断言没咬住 ★")
        restore(POLICY_CPP, bak_policy)

        # ---- ⑦ 还原后再跑一次 ---------------------------------------------
        print("\n=== ⑦ 还原后重跑 ===")
        ok, out = build_test(vcvars, cmake)
        if not ok:
            print("还原后编译失败:\n", out)
            return 1
        green, out = run_test()
        print("  ctest 通过=%s" % green)
        results["restored_green"] = green
    finally:
        # 无论如何都要还原源文件。
        # ★ 这里【不能】抛异常把备份吞掉:还原失败时必须让备份留在原地并大声报出来。
        for path, bak in ((POLICY_CPP, bak_policy), (HMI_CPP, bak_hmi)):
            if not os.path.isfile(bak):
                continue
            try:
                restore(path, bak)
                os.remove(bak)
                print("已还原 " + path)
            except Exception as e:
                print("★★ 还原 %s 失败:%s" % (path, e))
                print("★★ 备份留在原地:%s" % bak)
                print("★★ 请手动 copy 回去,别让它停在变异状态。")
                results["restore_identical"] = False
                return 1
        print("\n已还原源文件")

    # ---- 还原必须【逐字节】一致 ------------------------------------------
    for path, sha in sha_before.items():
        now = sha256_of(path)
        if now != sha:
            print("★ 还原后文件内容不一致:%s" % path)
            print("  跑前 %s" % sha)
            print("  跑后 %s" % now)
            results["restore_identical"] = False
            break
    else:
        results["restore_identical"] = True
        print("★ 还原后两个源文件逐字节一致(SHA256 对得上)")

    need = ("baseline_green", "m1_red", "m2_red", "m3_red", "m4_red", "m5_red",
            "restored_green", "restore_identical")
    if all(results.get(k) for k in need):
        print("\n★ 结论:基线绿、五组变异全红、还原后绿、还原逐字节一致 —— "
              "状态机那张表的判据确实咬得住 ★")
        return 0
    print("\n★ 结论:不符合预期 ★")
    return 1


if __name__ == "__main__":
    sys.exit(main())
