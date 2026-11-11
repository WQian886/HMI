# -*- coding: utf-8 -*-
# NEGCTL: manual — 它要临时改 C++ 源码再重编(判据在 C++ 里),在 ctest 里跑会动工作区,跟并行跑的目标打架
"""联动模板的负向对照:把判据改坏,ctest 【必须判红】。

★ 为什么需要这个脚本 ★
  ----------------------------------------------------------
  联动模板这条判据以前【根本不存在】:
    · 一条报警只能配一个动作(addMapping 是 `mappings_[alarmName] = m;`,
      后配的【覆盖】先配的,界面上什么提示都没有);
    · "动作有哪些种类"整段写死在 ActionManager::onAlarmTriggered 里。

  写完判据不等于判据【咬得住】。一条测试如果从来没红过,
  它证明不了任何事 —— 判据和实现"同时错"的时候,正向测试照样是绿的。

  所以这个脚本做六件事,都是"把闸拆了看它会不会响":

    M1(核心)把 addMapping 退回【覆盖】语义
            ⇒ tst_ActionTemplate 必须【红】
              (红的应是"配两个动作必须都在"那条)

    M2(执行)只执行第一步
            ⇒ tst_ActionTemplate 必须【红】
              (红的应是"整组动作全部执行"和"一步失败不许吞掉后一步"两条)
            ★ 这组最阴:只配了一个动作的工程【看不出来】——
              现场现象是"我配了停机 + 留痕,只剩停机了",
              而界面不报任何错。

    M3(类型)不认识的类型 fallback 成某个合法类型
            ⇒ tst_ActionTemplate 必须【红】
              (红的应是"不认识的类型不许被接受"那条)
            ★ 后果:工程文件来自更新版本时,一条【发邮件】动作
              会被当成【写变量】执行 —— 凭空多一个写 PLC 的动作。

    M4(加载)加载端忽略 action_templates
            ⇒ tst_ActionTemplate 必须【红】
              (红的应是"新格式赢过老格式"那条)
            ★ 后果:存的时候写进去了,打开的时候没读 ——
              联动配置"存下来就没了",而且不报任何错。

    M5(存盘)存盘端不写 action_templates
            ⇒ tst_ActionTemplate 必须【红】
              (红的应是"存出来的工程里有 action_templates"那条)

    M6(合法性)空的变量名也算合法
            ⇒ tst_ActionTemplate 必须【红】
              (红的应是"不合法的一步不许碰 sink"那条)
            ★ 后果:配置写错了(变量名漏填)照样往 PLC 写一个半截的值。

  判据红的唯一依据是【ctest 的退出码】——不是本脚本自己的判断。

用法:
    python -X utf8 -I scripts/negctl_action_template.py

退出码:
    0 = 基线绿、6 组变异全红、还原后绿、还原逐字节一致
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
TEMPLATE_CPP = os.path.join(ROOT, "hmi", "src", "action", "ActionTemplate.cpp")
ACTIONMGR_CPP = os.path.join(ROOT, "hmi", "ActionManager.cpp")
PROJECT_CPP = os.path.join(ROOT, "hmi", "Project.cpp")
TEST_BUILD_DIR = os.path.join(ROOT, "hmi", "tests", "build_ninja")
TEST_NAME = "tst_ActionTemplate"


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

    ★ 优先用【构建这个 build_ninja 时用的那个】——
      本机是 D:\\Qt\\Tools\\CMake_64\\bin。理由:ctest 的版本必须跟
      生成 build.ninja 的 cmake 配套,混用会出一些莫名其妙的解析错误。
    ★ 【踩过】路径必须是【完整绝对路径】,不能用
      os.path.join("D:", "Program Files\\...") ——
      那样拼出来是 "D:Program Files\\...",
      cmd 报 "The system cannot find the path specified.",
      看起来像"环境坏了",其实是路径少了个反斜杠。
    """
    for cand in (
            r"D:\Qt\Tools\CMake_64\bin\cmake.exe",
            r"D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE"
            r"\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
            r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE"
            r"\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"):
        if os.path.isfile(cand):
            return cand
    from shutil import which
    p = which("cmake")
    if p:
        return p
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
    batfile = os.path.join(tempfile.gettempdir(), "negctl_action_template_build.bat")
    with io.open(batfile, "w", encoding="ascii", newline="\r\n") as f:
        f.write("@echo off\r\n")
        f.write('call "%s" >nul 2>&1\r\n' % vcvars)
        # ★ Windows SDK 在 D: 而 vcvars64 找不到它(reg.exe 在沙箱里被挡),
        #   所以手工补上 include/lib 路径 —— 否则 cl.exe 找不到 windows.h。
        #   (跟 .scratch/_state_build.bat 同一套做法。)
        f.write("set SDK=D:\\Windows Kits\\10\r\n")
        f.write("set SDKVER=10.0.22621.0\r\n")
        f.write("set PATH=%SDK%\\bin\\%SDKVER%\\x64;%PATH%\r\n")
        f.write("set INCLUDE=%SDK%\\Include\\%SDKVER%\\um;%SDK%\\Include\\%SDKVER%\\shared;"
                "%SDK%\\Include\\%SDKVER%\\ucrt;%SDK%\\Include\\%SDKVER%\\winrt;%INCLUDE%\r\n")
        f.write("set LIB=%SDK%\\Lib\\%SDKVER%\\um\\x64;%SDK%\\Lib\\%SDKVER%\\ucrt\\x64;%LIB%\r\n")
        f.write("set PATH=D:\\Qt\\Tools\\Ninja;D:\\Qt\\6.8.3\\msvc2022_64\\bin;%PATH%\r\n")
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
#   为什么不用整段字符串:源码的对齐空格很容易在改代码时被顺手调一下,
#   整段匹配就会莫名其妙地"找不到原片段" —— 那是脚本在闹,不是变异生效了。
#   按行 + 按关键词定位,抗排版变化。

def mutate_lines(path, must_all, new_lines, must_not=None):
    """找到【同时包含 must_all 里所有子串】的那一行,把它替换成 new_lines(若干行)。

    new_lines —— 字符串列表(每一项一行)。用【这个文件自己的】换行符拼回去,
                 不然会出现 CRLF 文件里混进 LF 行。
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
    lines[hit:hit + 1] = list(new_lines)
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
    for f in (TEMPLATE_CPP, ACTIONMGR_CPP, PROJECT_CPP):
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

    watched = [TEMPLATE_CPP, ACTIONMGR_CPP, PROJECT_CPP]
    sha_before = {p: sha256_of(p) for p in watched}
    baks = {p: p + ".negctl.bak" for p in watched}
    for p in watched:
        shutil.copy(p, baks[p])

    results = {}

    def restore_all():
        for p in watched:
            if os.path.isfile(baks[p]):
                restore(p, baks[p])

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

        # ---- ② M1:addMapping 退回"覆盖" ----------------------------------
        print("\n=== ② M1(核心) addMapping 退回【覆盖】语义 ===")
        print("   ★ 这就是本次要修的那个 bug:配第二个动作时第一个被悄悄顶掉。")
        if not mutate_lines(
                ACTIONMGR_CPP,
                must_all=["it->steps.append(step);"],
                new_lines=["        it->steps = QList<qwhmi::action::ActionStep>{step};"
                           "   // ★ 变异:退回覆盖语义"]):
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
        restore_all()

        # ---- ③ M2:只执行第一步 --------------------------------------------
        print("\n=== ③ M2(执行) 只执行第一步 ===")
        print("   ★ 这组最阴:只配了一个动作的工程【看不出来】——")
        print("     现场现象是「我配了停机 + 留痕,只剩停机了」,而界面不报任何错。")
        if not mutate_lines(
                ACTIONMGR_CPP,
                must_all=["const qwhmi::action::StepResult r = qwhmi::action::executeStep(step, sink);"],
                new_lines=["        const qwhmi::action::StepResult r ="
                           " qwhmi::action::executeStep(step, sink);",
                           "        if (done + failed > 0) break;   // ★ 变异:只执行第一步"]):
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
        restore_all()

        # ---- ④ M3:不认识的类型 fallback -----------------------------------
        print("\n=== ④ M3(类型) 不认识的类型 fallback 成合法类型 ===")
        print("   ★ 后果:工程文件来自更新版本时,一条【发邮件】动作")
        print("     会被当成【写变量】执行 —— 凭空多一个写 PLC 的动作。")
        if not mutate_lines(
                TEMPLATE_CPP,
                must_all=['if (k == QLatin1String("log_only")) {'],
                new_lines=["    if (true) {   // ★ 变异:什么都认成合法类型"]):
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
        restore_all()

        # ---- ⑤ M4:加载端忽略 action_templates -----------------------------
        print("\n=== ⑤ M4(加载) 加载端忽略 action_templates ===")
        print("   ★ 后果:存的时候写进去了,打开的时候没读 ——")
        print("     联动配置「存下来就没了」,而且不报任何错。")
        if not mutate_lines(
                PROJECT_CPP,
                must_all=['QJsonArray tplArr = root["action_templates"].toArray();'],
                new_lines=["        QJsonArray tplArr;   // ★ 变异:不读 action_templates"]):
            print("!! 找那一行失败(源文件结构变了?)")
            return 1
        ok, out = build_test(vcvars, cmake)
        if not ok:
            print("  变异后编译失败(编译不过也算红):\n", out[-1500:])
            results["m4_red"] = True
        else:
            green, out = run_test()
            print("  ctest 通过=%s" % green)
            results["m4_red"] = (not green)
            if green:
                print("★ M4 变异后【仍然全绿】—— 判据没咬住 ★")
        restore_all()

        # ---- ⑥ M5:存盘端不写 action_templates -----------------------------
        print("\n=== ⑥ M5(存盘) 存盘端不写 action_templates ===")
        if not mutate_lines(
                PROJECT_CPP,
                must_all=['root["action_templates"] = tplsArr;'],
                new_lines=["        // ★ 变异:不写 action_templates"]):
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
                print("★ M5 变异后【仍然全绿】—— 判据没咬住 ★")
        restore_all()

        # ---- ⑦ M6:空的变量名也算合法 ---------------------------------------
        print("\n=== ⑦ M6(合法性) 空的变量名也算合法 ===")
        print("   ★ 后果:配置写错了(变量名漏填)照样往 PLC 写一个半截的值。")
        if not mutate_lines(
                TEMPLATE_CPP,
                must_all=['return fail(QStringLiteral("写变量动作缺少 variable(变量名)"));'],
                new_lines=["                return true;   // ★ 变异:空变量名也算合法"]):
            print("!! 找那一行失败(源文件结构变了?)")
            return 1
        ok, out = build_test(vcvars, cmake)
        if not ok:
            print("  变异后编译失败(编译不过也算红):\n", out[-1500:])
            results["m6_red"] = True
        else:
            green, out = run_test()
            print("  ctest 通过=%s" % green)
            results["m6_red"] = (not green)
            if green:
                print("★ M6 变异后【仍然全绿】—— 判据没咬住 ★")
        restore_all()

        # ---- ⑧ 还原后再跑一次 ---------------------------------------------
        print("\n=== ⑧ 还原后重跑 ===")
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
        for p in watched:
            if not os.path.isfile(baks[p]):
                continue
            try:
                restore(p, baks[p])
                os.remove(baks[p])
                print("已还原 " + p)
            except Exception as e:
                print("★★ 还原 %s 失败:%s" % (p, e))
                print("★★ 备份留在原地:%s" % baks[p])
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
        print("★ 还原后三个源文件逐字节一致(SHA256 对得上)")

    need = ("baseline_green", "m1_red", "m2_red", "m3_red", "m4_red", "m5_red",
            "m6_red", "restored_green", "restore_identical")
    if all(results.get(k) for k in need):
        print("\n★ 结论:基线绿、六组变异全红、还原后绿、还原逐字节一致 —— "
              "联动模板那条判据确实咬得住 ★")
        return 0
    print("\n★ 结论:不符合预期 ★")
    return 1


if __name__ == "__main__":
    sys.exit(main())
