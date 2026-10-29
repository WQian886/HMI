# -*- coding: utf-8 -*-
"""负向对照(现场级):把 G-16 的两道闸拆掉,短跑判据【必须】变红。

★ 为什么要有这个脚本(它跟 negctl_action_write.py 是一对,但【层级不同】)★
  negctl_action_write.py 在【单元层】拆闸:改源码 → 编测试库 → 跑 tst_ActionWrite。
  ★ 它验的是"那几条用例钉住了这个行为"。★
  本脚本在【现场层】拆闸:改源码 → 编【主程序 hmi.exe】 → 跑【真实短跑】
  → 看【短跑判据 J3(写出去的值对不对)】会不会红。
  ★ 它验的是"整条通路(轮询→报警→联动→写→从站收到的字节)真的被钉住了"。★

★ 为什么要拆两次(两个反例)★
  CX1「写出去的值是错的」:
      把 buildWritePayload 里的值改成 +1 ⇒ 从站收到的字节就是错的
      ⇒ J3 必须红。这是"判据能抓住值错"的直接证据。
  CX2「守卫没了」:
      把 PollScheduler 的 writePending 恒为 false ⇒ 轮询回包照常覆盖待写的值
      ⇒ 写出去的值会变成被覆盖后的旧值 ⇒ J3 必须红。
      ★ 这是【竞态】的反例:只有在"真的撞上了竞态"时它才会红 ——
        所以本脚本【必须】先确认基线那轮守卫命中 ≥ 1 次,
        否则 CX2 的红是"碰巧",不是"因为拆了闸"。★

用法:
    python scripts/negctl_action_race.py                # 全跑
    python scripts/negctl_action_race.py --minutes 2    # 短一点(但守卫可能撞不上)

退出码:
    0 = 基线绿、两个反例都红、还原后绿
    1 = 基线不绿 / 反例没红 / 还原后不绿   ← 这一条才是真出事

★ 任何时候都不会留下被改坏的源文件(finally 里还原 + touch)。★
★ 只改代码、只编译、只跑测试 —— 不碰 git,不碰别人的进程。★
"""

import argparse
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPTS = os.path.join(ROOT, "scripts")
WRITE_SCHEDULER = os.path.join(ROOT, "hmi", "WriteScheduler.cpp")
POLL_SCHEDULER = os.path.join(ROOT, "hmi", "PollScheduler.cpp")
BUILD_BAT = os.path.join(ROOT, "hmi", "out", "build", "debug2", "_build_main.bat")

# ---- 变异清单 ----------------------------------------------------------
# ★ 每条都只改【一处】,而且改完之后那处代码仍然【编译得过】——
#   编译不过也算红,但那是"另一种红",证明力弱得多。
MUTATIONS = [
    (
        "CX1 写出去的值 +1(发出去的数就是错的)",
        WRITE_SCHEDULER,
        """        case Variable::DataType::Int16:
        case Variable::DataType::UInt16: {
            // 16 位 → 0x06 写单寄存器
            *outArea = RegisterArea::HoldingReg;
            payload = {static_cast<quint16>(v->value().toUInt())};""",
        """        case Variable::DataType::Int16:
        case Variable::DataType::UInt16: {
            // 16 位 → 0x06 写单寄存器
            *outArea = RegisterArea::HoldingReg;
            payload = {static_cast<quint16>(v->value().toUInt() + 1)};  // ★ 变异:值 +1""",
    ),
    (
        "CX2 拆掉 PollScheduler 的守卫(读回照常覆盖待写的值)",
        POLL_SCHEDULER,
        """        const bool writePending = v->isDirty() && v->access() == Variable::Access::ReadWrite;""",
        """        const bool writePending = false;   // ★ 变异:守卫整个没了""",
    ),
]


def run(cmd, cwd=None, env=None, timeout=1800):
    return subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True,
                          errors="replace", timeout=timeout)


def detect_newline(text):
    return "\r\n" if "\r\n" in text else "\n"


def with_newline(pattern, nl):
    return pattern.replace("\n", nl)


def touch(path):
    """把 mtime 顶到现在 —— 还原之后 ninja 才知道要重编。
    (shutil.copy 保留 mtime;源文件比 .obj 还旧 ⇒ ninja 判定"不用重编",
     于是跑的还是被变异的二进制。踩过好几次。)
    """
    now = time.time()
    os.utime(path, (now, now))


def build_main():
    """编主程序 hmi.exe(不是测试库!)—— 短跑跑的是它。"""
    if not os.path.isfile(BUILD_BAT):
        print("找不到主程序构建脚本: " + BUILD_BAT)
        return False, "no build bat"
    r = run(["cmd", "/c", BUILD_BAT], cwd=ROOT)
    if r.returncode != 0:
        return False, (r.stdout + r.stderr)[-3000:]
    return True, ""


def parse_verdict(outdir):
    """从 verdict.txt 里读出:守卫命中次数 + 结论。"""
    vp = os.path.join(outdir, "verdict.txt")
    if not os.path.isfile(vp):
        return None
    with io.open(vp, encoding="utf-8", errors="replace") as f:
        text = f.read()
    hits = None
    m = re.search(r"守卫命中行.*?:\s*(\d+)", text)
    if m:
        hits = int(m.group(1))
    green = "【绿】" in text
    return {"text": text, "hits": hits, "green": green}


def short_run(tag, minutes, port):
    """跑一轮短跑,返回 (判据文件解析结果, 原始输出)。"""
    outdir = os.path.join(ROOT, "soaks", "negctl_%s_%s" % (tag, time.strftime("%H%M%S")))
    cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
           "-File", os.path.join(SCRIPTS, "short_action_run.ps1"),
           "-Minutes", str(minutes), "-SlavePort", str(port),
           "-OutDir", outdir]
    print("    跑短跑: %s" % outdir)
    r = run(cmd, cwd=ROOT)
    return parse_verdict(outdir), r.stdout, outdir


def main():
    ap = argparse.ArgumentParser(description="现场级负向对照:拆掉 G-16 的闸,短跑判据必须变红")
    ap.add_argument("--minutes", type=int, default=2)
    ap.add_argument("--port", type=int, default=1502)
    args = ap.parse_args()

    for f in (WRITE_SCHEDULER, POLL_SCHEDULER):
        if not os.path.isfile(f):
            print("找不到源文件: " + f)
            return 1

    # ---- 备份 ----
    backups = {}
    originals = {}
    for _n, path, _o, _w in MUTATIONS:
        if path in backups:
            continue
        backups[path] = path + ".negrace.bak"
        shutil.copy2(path, backups[path])
        originals[path] = io.open(backups[path], encoding="utf-8", newline="").read()

    def restore():
        for path, bak in backups.items():
            shutil.copy(bak, path)
            touch(path)
        for path in backups:
            try:
                os.remove(backups[path])
            except OSError:
                pass

    try:
        # ================= 基线 =================
        print("== 基线:未改任何源码 ==")
        ok, err = build_main()
        if not ok:
            print("基线编译失败:\n" + err)
            return 1
        v, out, outdir = short_run("base", args.minutes, args.port)
        if v is None:
            print("基线没产出 verdict.txt —— 短跑自己没跑起来")
            print(out[-2000:])
            return 1
        print("   基线: %s,守卫命中 %s 次" %
              ("绿" if v["green"] else "红", v["hits"]))
        if not v["green"]:
            print("!! 基线就不绿 —— 先修基线再谈负向对照")
            print(v["text"][-2000:])
            return 1
        if v["hits"] is None or v["hits"] < 1:
            print("!! 基线这轮【没撞上竞态】(守卫命中 %s 次) ——" % v["hits"])
            print("   CX2(拆守卫)就没有撞上的机会,那个反例证明不了任何东西。")
            print("   ⇒ 加大 --minutes,或者调 -WavePeriod / -AlarmMinMs。")
            return 1

        all_red = True

        # ================= 反例 =================
        for name, path, old, new in MUTATIONS:
            print("\n== %s ==" % name)
            original = originals[path]
            nl = detect_newline(original)
            old_nl = with_newline(old, nl)
            new_nl = with_newline(new, nl)
            if old_nl not in original:
                print("!! 原片段没找到(源文件结构变了?): %s" % name)
                all_red = False
                continue
            mutated = original.replace(old_nl, new_nl, 1)
            io.open(path, "w", encoding="utf-8", newline="").write(mutated)
            touch(path)

            ok, err = build_main()
            if not ok:
                print("   编译失败(编译不过也算红,但证明力弱):\n" + err[-1500:])
                all_red = False
                continue

            v2, out2, od2 = short_run(name.split()[0].lower(), args.minutes, args.port)
            if v2 is None:
                print("   ★ 短跑没产出 verdict.txt —— 算红(连判据都没跑起来)")
                all_red = False
                continue
            print("   判据: %s,守卫命中 %s 次" %
                  ("绿" if v2["green"] else "红", v2["hits"]))
            if v2["green"]:
                print("   ★ 还是绿的 ★  ← 这条闸没被守住(判据瞎了)")
                all_red = False
            else:
                print("   红 ✓")

            # 还原这一个,再跑下一个
            shutil.copy(backups[path], path)
            touch(path)
            ok, err = build_main()
            if not ok:
                print("   还原后编译失败:\n" + err[-1500:])
                all_red = False
    finally:
        restore()
        print("\n已还原源文件")

    # ---- 还原后再跑一次,必须绿 ----
    ok, err = build_main()
    if not ok:
        print("还原后编译失败:\n" + err[-3000:])
        return 1
    v3, out3, _od3 = short_run("restore", args.minutes, args.port)
    if v3 is None:
        print("还原后短跑没产出 verdict.txt")
        return 1
    print("还原后: %s,守卫命中 %s 次" % ("绿" if v3["green"] else "红", v3["hits"]))
    if not v3["green"]:
        print(v3["text"][-2000:])
        return 1
    if not all_red:
        print("\n★ 结论:有反例【没有变红】—— 对应的闸没被守住 ★")
        return 1
    print("\n★ 结论:基线绿(且撞上了竞态)、%d 个反例全红、还原后绿 —— "
          "闸确实被守住了 ★" % len(MUTATIONS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
