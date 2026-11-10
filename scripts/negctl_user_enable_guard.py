# -*- coding: utf-8 -*-
"""G-30 负向对照:把"禁用/启用"这条判据改坏,ctest 【必须判红】。

★ 为什么需要这个脚本 ★
  ----------------------------------------------------------
  G-30(2026-11-10):用户管理的判据本来只活在 UserManagementDialog 里 ——
  而且是拿【显示文本】比角色名("管理员")和禁用状态("是")。
  同时"禁用"是【单向】的:被禁用的账号永远回不来(没有启用按钮,
  UserManager 也没有对应的 API),而确认框还在跟用户说
  "(可后续用密码恢复)" —— 密码根本救不回来(authenticate 对 disabled 一律拒)。

  修法:判据搬进公共层(UserManager::canDisableUser / removeUser / enableUser),
  对话框只留接线;并补上"启用"这条路。

  写完判据不等于判据【咬得住】。一条测试如果从来没红过,它证明不了任何事 ——
  判据和实现"同时错"的时候,正向测试照样是绿的。
  所以这个脚本把闸拆了看它会不会响:

    M1(公共层·数据完整性)去掉"不能禁用最后一个 enabled Admin"这条护栏
            ⇒ tst_UserManager 必须【红】
              (红的是 removeUser_refusesLastEnabledAdmin /
               canDisableUser_judgesInSharedLayer)

    M2(公共层·自保护)去掉"不能禁自己"这条护栏
            ⇒ tst_UserManager 必须【红】
              (红的是 removeUser_refusesSelf)

    M3(公共层·启用)enableUser 不把 disabled 清掉
            ⇒ tst_UserManager 必须【红】
              (红的是 enableUser_restoresAccess)
              ★ 这条最要命:它对应现场那个症状 —— "点了启用,用户还是登不上",
                而界面一片正常。

    M4(接线层)对话框不调公共层的 canDisableUser(自己判 / 不判)
            ⇒ tst_UserManagementDialog 必须【红】
              (红的是 judgmentComesFromSharedLayer)

    M5(接线层)把那句假的"(可后续用密码恢复)"放回去
            ⇒ tst_UserManagementDialog 必须【红】
              (红的是 falseRecoveryPromise_isGone)

  判据红的唯一依据是【ctest 的退出码】——不是本脚本自己的判断。
  ★ 而且每次变异之后都要【重编】——
    只改源码不重编,跑的还是上轮的二进制,那就是一条会骗人的假绿。
    (本仓库第 6 次栽在"跑的是旧二进制"上;见 memory:hmi-restore-file-mtime-trap。)

用法:
    python -X utf8 -I scripts/negctl_user_enable_guard.py

退出码:
    0 = 基线绿、五组变异全红、还原后绿、还原逐字节一致
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
USERMGR_CPP = os.path.join(ROOT, "hmi", "src", "auth", "UserManager.cpp")
DIALOG_CPP = os.path.join(ROOT, "hmi", "src", "auth", "UserManagementDialog.cpp")
TEST_BUILD_DIR = os.path.join(ROOT, "hmi", "tests", "build_ninja")
# 两个目标都要跑:公共层的判据在 tst_UserManager,接线在 tst_UserManagementDialog
TEST_NAMES = ("tst_UserManager", "tst_UserManagementDialog")
TEST_RE = "tst_UserManager|tst_UserManagementDialog"
BUILD_TARGETS = "tst_UserManager tst_UserManagementDialog"


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


def mutate(path, old, new):
    """把 old 换成 new。old 必须【恰好出现一次】—— 出现 0 次说明源码结构变了
    (脚本在闹,不是变异生效);出现多次说明我挑的片段不够唯一,会误伤别处。"""
    original = read_text(path)
    n = original.count(old)
    if n != 1:
        print("!! 片段出现 %d 次(要求恰好 1 次):%s" % (n, old))
        return False
    write_text(path, original.replace(old, new, 1))
    return True


def build_test(vcvars, cmake):
    """重编被测测试。返回 (ok, 输出)。

    ★ 必须【重编】才能验变异:只改源码不重编,跑的还是上轮的二进制。
    """
    batfile = os.path.join(tempfile.gettempdir(), "negctl_user_enable_guard_build.bat")
    with io.open(batfile, "w", encoding="ascii", newline="\r\n") as f:
        f.write("@echo off\r\n")
        f.write('call "%s" >nul 2>&1\r\n' % vcvars)
        # ★ Windows SDK 在 D: 而 vcvars64 找不到它(reg.exe 在沙箱里被挡),
        #   所以手工补上 include/lib 路径 —— 否则 cl.exe 找不到 windows.h。
        f.write("set SDK=D:\\Windows Kits\\10\r\n")
        f.write("set SDKVER=10.0.22621.0\r\n")
        f.write("set PATH=%SDK%\\bin\\%SDKVER%\\x64;%PATH%\r\n")
        f.write("set INCLUDE=%SDK%\\Include\\%SDKVER%\\um;%SDK%\\Include\\%SDKVER%\\shared;"
                "%SDK%\\Include\\%SDKVER%\\ucrt;%SDK%\\Include\\%SDKVER%\\winrt;%INCLUDE%\r\n")
        f.write("set LIB=%SDK%\\Lib\\%SDKVER%\\um\\x64;%SDK%\\Lib\\%SDKVER%\\ucrt\\x64;%LIB%\r\n")
        f.write("set PATH=D:\\Qt\\Tools\\Ninja;D:\\Qt\\6.8.3\\msvc2022_64\\bin;%PATH%\r\n")
        f.write('cd /d "%s"\r\n' % TEST_BUILD_DIR)
        f.write('"%s" --build . --target %s\r\n' % (cmake, BUILD_TARGETS))
        f.write("exit /b %ERRORLEVEL%\r\n")
    r = subprocess.run(["cmd", "/c", batfile], cwd=ROOT,
                       capture_output=True, text=True, errors="replace")
    return (r.returncode == 0), (r.stdout + r.stderr)[-3000:]


def run_test():
    """跑一次被测测试。返回 (是否通过, ctest 输出)。

    ★ 判据是【ctest 的退出码】,不是本脚本对输出的解读 ——
      "看输出像不像红了"是本项目反复吃过的那种自欺。
    ★ 而且必须确认 ctest 【真的跑了这些目标】——
      `ctest -R 名字` 在名字不存在时也会返回 0 并打印
      "No tests were found!!!" ⇒ 那是一条会骗人的假绿(2026-11-10 实测过)。
    """
    ctest = os.path.join(os.path.dirname(find_cmake() or ""), "ctest.exe")
    r = subprocess.run(
        [ctest, "-R", TEST_RE, "--output-on-failure"],
        cwd=TEST_BUILD_DIR, capture_output=True, text=True, errors="replace")
    out = (r.stdout or "") + (r.stderr or "")
    ran = all(n in out for n in TEST_NAMES) and ("No tests were found" not in out)
    if not ran:
        print("  ★ ctest 压根没跑这些目标(名字对不上?)—— 这次结果不算数 ★")
        print(out[-1500:])
        return False, out
    return (r.returncode == 0), out


def restore(path, backup):
    """还原源文件。

    ★ 必须 shutil.copy 而不是 copy2:copy2 会把【备份的 mtime】一起带回来,
      而备份是从改之前的文件拷的 —— mtime 变旧,ninja 判定"不用重编",
      于是还原后跑的还是被变异的二进制。
      (见 memory:hmi-restore-file-mtime-trap。)

    ★ 重试是实测踩出来的:Windows 的杀毒软件会在"刚被改过的文件"上
      短暂持有句柄,那个窗口里 copy 直接抛 PermissionError ——
      结果源码被留在【变异状态】。那才是最坏的结果。
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


# ---- 五个变异点 ------------------------------------------------------------
# M1:公共层去掉"最后一个 enabled Admin"护栏
M1_PATH = USERMGR_CPP
M1_OLD = "if (users_[idx].role == Role::Admin && enabledAdminCount() <= 1) {"
M1_NEW = "if (false) {   // ★ 变异:去掉「最后一个 enabled Admin」护栏"

# M2:公共层去掉"不能禁自己"护栏
M2_PATH = USERMGR_CPP
M2_OLD = "if (!actor.isEmpty() && User::namesEqual(trimmed, actor)) {"
M2_NEW = "if (false) {   // ★ 变异:去掉「不能禁自己」护栏"

# M3:启用不生效(disabled 没被清掉)
M3_PATH = USERMGR_CPP
M3_OLD = "users_[idx].disabled = false;"
M3_NEW = "users_[idx].disabled = true;   // ★ 变异:启用不生效"

# M4:对话框不调公共层的判据
M4_PATH = DIALOG_CPP
M4_OLD = "if (!mgr_.canDisableUser(name, actor, &reason)) {"
M4_NEW = "if (false) {   // ★ 变异:判据不回公共层了"

# M5:把那句假的"可后续用密码恢复"放回去
M5_PATH = DIALOG_CPP
M5_OLD = "需要恢复请用【启用选中】"
M5_NEW = "可后续用密码恢复"

MUTATIONS = [
    ("M1(公共层·数据完整性) 去掉「最后一个 enabled Admin」护栏",
     M1_PATH, M1_OLD, M1_NEW),
    ("M2(公共层·自保护) 去掉「不能禁自己」护栏",
     M2_PATH, M2_OLD, M2_NEW),
    ("M3(公共层·启用) enableUser 不把 disabled 清掉",
     M3_PATH, M3_OLD, M3_NEW),
    ("M4(接线层) 对话框不调公共层的 canDisableUser",
     M4_PATH, M4_OLD, M4_NEW),
    ("M5(接线层) 把假的「可后续用密码恢复」放回去",
     M5_PATH, M5_OLD, M5_NEW),
]


def main():
    for f in (USERMGR_CPP, DIALOG_CPP):
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

    watched = [USERMGR_CPP, DIALOG_CPP]
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

        # ---- ②~⑥ 五组变异 -----------------------------------------------
        for i, (name, path, old, new) in enumerate(MUTATIONS, start=2):
            print("\n=== %s %s ===" % (chr(0x2461 + i - 2), name))
            if not mutate(path, old, new):
                print("!! 找那一行失败(源文件结构变了?)")
                restore_all()
                return 1
            ok, out = build_test(vcvars, cmake)
            if not ok:
                print("  变异后编译失败(编译不过也算红):\n", out[-1500:])
                results["m%d_red" % (i - 1)] = True
            else:
                green, out = run_test()
                print("  ctest 通过=%s" % green)
                results["m%d_red" % (i - 1)] = (not green)
                if green:
                    print("★ 变异后【仍然全绿】—— 判据没咬住 ★")
            restore_all()

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
        print("★ 还原后两个源文件逐字节一致(SHA256 对得上)")

    need = ("baseline_green", "m1_red", "m2_red", "m3_red", "m4_red", "m5_red",
            "restored_green", "restore_identical")
    if all(results.get(k) for k in need):
        print("\n★ 结论:基线绿、五组变异全红、还原后绿、还原逐字节一致 —— "
              "G-30 那条判据确实咬得住 ★")
        return 0
    print("\n★ 结论:不符合预期 ★")
    return 1


if __name__ == "__main__":
    sys.exit(main())
