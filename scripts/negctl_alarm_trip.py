# -*- coding: utf-8 -*-
# NEGCTL: manual — 它要临时改 C++ 源码再重编,在 ctest 里跑会动工作区,跟并行跑的目标打架
"""G-26 的负向对照:把"只有严重报警才抢停机"拆掉,判据【必须判红】。

★ 为什么需要这个脚本 ★
  ----------------------------------------------------------
  一条测试如果【从来没红过】,那它证明不了任何事 ——
  它可能只是在测一个恒真的表达式。

  本项目吃过的亏都是这个形状:
    · 主画面报警表整张是废的(状态列从来没被创建过),
      而当时 68 个测试【全绿】 —— 因为主画面那套一条测试都没有;
    · 24 小时长跑实测:"温度高 → 速度写 0"这条联动
      【24 小时一次都没执行过】,而界面正常、报警正常响、
      审计正常记"已执行" —— 从站那边一个字节都没收到。

  G-26 是同一个形状的第三次:
    【任何等级的报警都会把整台机器打成 Fault】,
    而 106 个单测全绿 —— 因为 hmi.cpp 不在 hmi_core 里,
    没有一条测试链接得到主窗口。

  所以这个脚本做两件事,都是"把闸拆了看它会不会响":

    M1(判据层)把 AlarmTriggerPolicy::shouldTripMachine 改成恒返回 true
            → tst_AlarmTriggerPolicy 必须【红】
              (红的应是"Info/Warning 不该抢停机"那两条)

    M2(接线层)把 hmi.cpp 的 if (shouldTripMachine(...)) 改成 if (true)
            → tst_AlarmTriggerPolicy 必须【红】
              (红的应是"hmi.cpp 真的按等级过滤"那条源码文本断言)
            ★ M2 【不需要重编】:hmi.cpp 不在 hmi_core 里,
              测试是【运行时读磁盘上的 hmi.cpp 文本】——
              所以这一轮专门证明"源码文本那条断言"本身咬得住。

  判据红的唯一依据是【ctest 的退出码】——不是本脚本自己的判断。

用法:
    python -I scripts/negctl_alarm_trip.py

退出码:
    0 = 基线绿、M1 红、M2 红、还原后绿 —— 判据确实咬得住
    1 = 任一环节不符合预期
"""

import hashlib
import io
import os
import shutil
import subprocess
import sys
import tempfile

# ★ 输出用 UTF-8:这个脚本的中文提示全是给人看的,
#   在 GBK 控制台上全是"���"就等于没写。
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

# ---- 路径:全部从本文件位置推出来,不写死 ----------------------------------
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
POLICY_CPP = os.path.join(ROOT, "hmi", "src", "alarm", "AlarmTriggerPolicy.cpp")
HMI_CPP = os.path.join(ROOT, "hmi", "hmi.cpp")
TEST_BUILD_DIR = os.path.join(ROOT, "hmi", "tests", "build_ninja")
TEST_NAME = "tst_AlarmTriggerPolicy"

# ★ M1:判据恒返回 true —— 相当于"回到 G-26 修之前"的行为。
M1_NAME = "M1(判据层) shouldTripMachine 恒返回 true"
M1_OLD = "    return level >= AlarmLevel::Critical;"
M1_NEW = "    return true;   // ★ 变异:任何等级都抢停机"

# ★ M2:调用点去掉等级过滤 —— hmi.cpp 又变回"无条件抢停机"。
M2_NAME = "M2(接线层) hmi.cpp 去掉 shouldTripMachine 这道闸"
M2_OLD = "    if (qwhmi::alarm::shouldTripMachine(a.level)) {"
M2_NEW = "    if (true) {   // ★ 变异:无条件抢停机"


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
    """找 cmake.exe(以及跟它同目录的 ctest.exe)。

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


def detect_newline(text):
    """这个文件自己用 LF 还是 CRLF。

    ★ 【踩过】ActionManager.cpp 是 CRLF、AlarmTriggerPolicy.cpp 是 LF,
      而脚本里手写的变异片段用 \\n 拼 —— 直接 replace 到 CRLF 文件上
      【永远匹配不上】,脚本会报"原片段没找到",
      而那不是"守卫没被守住",是脚本自己坏了。
    """
    return "\r\n" if "\r\n" in text else "\n"


def with_newline(pattern, nl):
    return pattern.replace("\n", nl)


def build_test(vcvars, cmake):
    """重编被测测试。返回 (ok, 输出)。"""
    batfile = os.path.join(tempfile.gettempdir(), "negctl_alarm_trip_build.bat")
    with io.open(batfile, "w", encoding="ascii", newline="\r\n") as f:
        f.write("@echo off\r\n")
        f.write('call "%s" >nul 2>&1\r\n' % vcvars)
        f.write('cd /d "%s"\r\n' % TEST_BUILD_DIR)
        f.write('"%s" --build . --target %s\r\n' % (cmake, TEST_NAME))
        f.write("exit /b %ERRORLEVEL%\r\n")
    r = subprocess.run(["cmd", "/c", batfile], cwd=ROOT,
                       capture_output=True, text=True, errors="replace")
    ok = (r.returncode == 0)
    return ok, (r.stdout + r.stderr)[-3000:]


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


def read_text(path):
    # ★ newline="" —— 不让 Python 自己做换行转换,
    #   否则写回去的时候整个文件的换行符会被改一遍(CRLF → LF),
    #   还原就【不彻底】了。
    with io.open(path, encoding="utf-8", newline="") as f:
        return f.read()


def write_text(path, text):
    """写回源文件。带重试 —— 见 restore() 里的说明(杀毒软件会短暂持有句柄)。"""
    import time
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


def apply_mutation(path, old, new):
    """把一段片段替换掉。返回是否成功。"""
    original = read_text(path)
    nl = detect_newline(original)
    old_nl = with_newline(old, nl)
    new_nl = with_newline(new, nl)
    if old_nl not in original:
        print("!! 原片段没找到(源文件结构变了?): %s" % path)
        return False
    write_text(path, original.replace(old_nl, new_nl, 1))
    return True


def restore(path, backup):
    """还原源文件。

    ★ 必须 shutil.copy 而不是 copy2:copy2 会把【备份的 mtime】一起带回来,
      而备份是从改之前的文件拷的 —— mtime 变旧,ninja 判定"不用重编",
      于是还原后跑的还是被变异的二进制。
      (这条坑本项目踩过两次,见 memory:hmi-restore-file-mtime-trap。)

    ★ 【踩过】Windows 上写回源文件可能撞上 PermissionError ——
      杀毒软件的实时扫描会在"刚被改过的文件"上短暂持有句柄,
      那个窗口里 shutil.copy 直接抛 PermissionError。
      ⇒ 这里的重试不是"防御性编程",是实测踩出来的:
        第一次跑这个脚本时,还原 hmi.cpp 就是这么失败的,
        结果源码被留在【变异状态】(if (true))—— 那才是最坏的结果。
      ⇒ 重试若干次;还不行就把备份留在原地并大声报出来,
        绝不能"删掉备份然后报告成功"。
    """
    import time
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
        for line in out.splitlines():
            if "tests passed" in line or "Failed" in line:
                print("  " + line.strip())
        if not green:
            print("!! 基线就不绿 —— 这一轮什么也验不了(先修基线)")
            return 1
        results["baseline_green"] = True

        # ---- ② M1:判据层变异 ------------------------------------------
        print("\n=== ② %s===" % M1_NAME)
        if not apply_mutation(POLICY_CPP, M1_OLD, M1_NEW):
            return 1
        ok, out = build_test(vcvars, cmake)
        if not ok:
            print("  变异后编译失败(编译不过也算红,但要看清原因):\n", out[-1500:])
            results["m1_red"] = True
        else:
            green, out = run_test()
            print("  ctest 通过=%s" % green)
            for line in out.splitlines():
                if "Failed" in line or "tests passed" in line:
                    print("  " + line.strip())
            results["m1_red"] = (not green)
            if green:
                print("★ M1 变异后【仍然全绿】—— 判据没咬住 ★")
        restore(POLICY_CPP, bak_policy)

        # ---- ③ M2:接线层变异 ------------------------------------------
        print("\n=== ③ %s===" % M2_NAME)
        if not apply_mutation(HMI_CPP, M2_OLD, M2_NEW):
            return 1
        # ★ 这一轮【不重编】:hmi.cpp 不在 hmi_core 里,
        #   测试是运行时读磁盘上的 hmi.cpp 文本 —— 重编也不会变。
        green, out = run_test()
        print("  ctest 通过=%s" % green)
        for line in out.splitlines():
            if "Failed" in line or "tests passed" in line:
                print("  " + line.strip())
        results["m2_red"] = (not green)
        if green:
            print("★ M2 变异后【仍然全绿】—— 源码文本那条断言没咬住 ★")
        restore(HMI_CPP, bak_hmi)

        # ---- ④ 还原后再跑一次 ------------------------------------------
        print("\n=== ④ 还原后重跑 ===")
        ok, out = build_test(vcvars, cmake)
        if not ok:
            print("还原后编译失败:\n", out)
            return 1
        green, out = run_test()
        print("  ctest 通过=%s" % green)
        results["restored_green"] = green
    finally:
        # 无论如何都要还原源文件。
        # ★ 这里【不能】抛异常把备份吞掉:还原失败时必须让备份留在原地,
        #   并大声报出来 —— 源码停在变异状态是最坏的结果,
        #   而"脚本自己以为成功了"比那更坏。
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

    need = ("baseline_green", "m1_red", "m2_red", "restored_green",
            "restore_identical")
    if all(results.get(k) for k in need):
        print("\n★ 结论:基线绿、M1 红、M2 红、还原后绿、还原逐字节一致 —— "
              "G-26 的判据确实咬得住 ★")
        return 0
    print("\n★ 结论:不符合预期 ★")
    return 1


if __name__ == "__main__":
    sys.exit(main())
