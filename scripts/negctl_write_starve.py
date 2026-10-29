# -*- coding: utf-8 -*-
"""负向对照(现场级):把 BUGFIX-E1 的【写排队】拆掉,短跑判据【必须】变红。

★ 为什么要有这个脚本(它跟 negctl_write_queue.py 是一对,但【层级不同】)★
  negctl_write_queue.py 在【单元层】拆闸:改源码 → 编测试库 → 跑 tst_ModbusWriteQueue。
  ★ 它验的是"那几条用例钉住了这个行为"。★
  本脚本在【现场层】拆闸:改源码 → 编【主程序 hmi.exe】 → 跑【真实短跑】
  → 看【从站收到的字节】会不会断。
  ★ 它验的是"整条通路(轮询→报警→联动→写→排队→从站收到的字节)真的被钉住了"。★

★★ 一、两种变异,各验一件事 ★★
  ① revert      —— 把"排队"整段换成"直接拒收" = 【BUGFIX-E1 修之前的原样】。
                   验的是"当年那个洞,判据抓不抓得住"。
                   ★ 抓不抓得住【取决于相位】:撞上槽被占着的时候才拒收。
  ② always-reject —— 让【每一次】写都被拒(不管槽空不空)。
                   验的是"判据对'写被饿死'这种故障【有没有牙】"——
                   【不依赖相位】⇒ 一定能红。
  两个都跑:① 是"当年的洞",② 是"判据的牙"。
  只跑 ① 而它没撞上 ⇒ 【什么都证明不了】(不是绿,是无效)⇒ 必须重试。

★★ 二、★ 回包延迟的方向性:实测推翻了直觉 ★★
  第一版我以为"延迟越大 ⇒ 槽占得越久 ⇒ 撞车越多",于是先试 190ms、再试 400ms。
  ★ 实测:190ms → 排队 0 次;400ms → 排队 1 次;120ms → 排队 3 次。★
  ⇒ 方向【正好相反】。原因(实测时序,不是推演):
    写尝试发生在【读回包之后】约 100~150ms(写节拍 100ms 与轮询节拍 100ms 相位锁定),
    而读占槽的窗口是 [发出, 回包] = D。
    ⇒ 写落在窗口内,需要"下一轮读已经发出、但还没回包"——
      D 越大,写落下时那一轮读【早就回包了】⇒ 越不容易撞上。
    ⇒ 撞车是【相位问题】,不是【延迟大小问题】:D 只是挪动了相位。
  ⇒ 实战取值:120ms 撞车率最高(3/38)。脚本默认用它。
  ⇒ ★ 这个"越大越容易撞"的直觉是【错的】,已经写进 docs/现状.md ——★
    不然下一个人会照着错的方向调参,调半天撞车还是 0。

★★ 三、每一轮都要先验"咬到了没有" ★★
  变异轮如果【一次都没拒收】⇒ 变异体没被走到 ⇒ 判据当然不会红
  ⇒ 那种"没红"【不能证明判据好】,只能证明"这一轮没验到"。
  ⇒ 脚本每一阶段都检查"咬到了没有",没咬到就重试;重试到底还没咬到 ⇒ 判【无效】。

用法:
    python scripts/negctl_write_starve.py
    python scripts/negctl_write_starve.py --minutes 2 --reply-delay-ms 120

退出码:
    0 = 每一环都成立(基线绿且咬到 / 变异红且咬到 / 还原后绿)
    1 = 任何一环没成立

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
import time

# ★ Windows 控制台默认是 GBK —— 直接 print 中文会 UnicodeEncodeError。
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPTS = os.path.join(ROOT, "scripts")
MODBUS_MASTER = os.path.join(ROOT, "hmi", "ModbusMaster.cpp")
BUILD_BAT = os.path.join(ROOT, "hmi", "out", "build", "debug2", "_build_main.bat")
SHORT_RUN = os.path.join(SCRIPTS, "short_action_run.ps1")

# ---- 变异锚点:必须【唯一】 ----
# ★★ "if (!pending_.isEmpty()) {" 在这个文件里出现【3 次】(第 76/328/473 行)。★★
#   第一版用它做锚点 + find() ⇒ 抓到第 76 行那个,
#   结束标记从那里一路找到 430 行外的 return queuedReply
#   ⇒ 一口气替换了 17898 个字符,把文件中间一大段都吃掉了。
#   ★ 而这种错误【不会报错】—— 脚本照样跑,变异体照样编得过,
#     只是它已经不是"拆 BUGFIX-E1 的闸"了。★
#   ⇒ 改法:锚点用【排队分支独有的那一行】(全文件只出现一次),再从它往两边扩。
#   ⇒ 并且【替换完必须自检】:区间长度 + 区间里必须含哪些标记 ——
#     不符合就当场报错退出,绝不"替换了再说"。
MUT_ANCHOR = "queuedWrites_.size() >= kMaxQueuedWrites"
MUT_BEGIN = "    if (!pending_.isEmpty()) {"
MUT_END = "        return queuedReply;\n    }"
# ★ 被替换区间必须包含这些标记 —— 少一个就说明锚点抓错了地方。
MUT_MUST_CONTAIN = [
    "queuedWrites_.size() >= kMaxQueuedWrites",
    "QueuedWrite qw;",
    "qw.values        = writeUnit.values();",
    "写请求【排队】",
    "return queuedReply;",
]
MUT_MIN_LEN = 800
MUT_MAX_LEN = 6000

# ---- 两种变异的替换体 ----
REPL_REVERT = """    if (!pending_.isEmpty()) {
        // ★★ 变异体① revert(BUGFIX-E1 现场级负向对照)★★
        //   回到修之前的语义:槽被读占着 → 【直接拒收】(返回 nullptr),不排队。
        //   这几行就是 BUGFIX-E1 修掉的那个洞本身。
        qCDebug(hmiComm) << "[ModbusMaster] 槽被占着,直接拒收本次写请求"
                         << "(★ 变异体 = BUGFIX-E1 之前的原样)";
        return nullptr;
    }"""

REPL_ALWAYS = """    if (true) {
        // ★★ 变异体② always-reject(BUGFIX-E1 现场级负向对照)★★
        //   不管槽空不空,【每一次】写都拒收。
        //   ⇒ 用来看判据对"写被饿死"这种故障【有没有牙】(不依赖相位)。
        qCDebug(hmiComm) << "[ModbusMaster] ★变异体:直接拒收本次写请求";
        return nullptr;
    }"""


def run(cmd, cwd=None, timeout=2400):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                          errors="replace", timeout=timeout)


def touch(path):
    """把 mtime 顶到现在 —— 还原之后 ninja 才知道要重编。
    (shutil.copy 保留 mtime;源文件比 .obj 还旧 ⇒ ninja 判定"不用重编",
     于是跑的还是被变异的二进制。踩过好几次。)
    """
    now = time.time()
    os.utime(path, (now, now))


def build_main():
    r = run(["cmd", "/c", BUILD_BAT], cwd=ROOT)
    return r.returncode, (r.stdout or "") + (r.stderr or "")


def read_text(path):
    if not os.path.isfile(path):
        return ""
    return io.open(path, encoding="utf-8-sig", errors="replace").read()


def app_log_lines(outdir):
    """应用日志在 <OutDir>\\appdata\\QianWei\\HMI\\logs\\*.log
    ★ 路径必须对:在 <OutDir> 根下找 *.log 找到的是 hmi.stderr.txt,
      于是所有诊断计数【永远是 0】—— 判据自己瞎了。第一版就栽在这儿。★
    """
    log_dir = os.path.join(outdir, "appdata", "QianWei", "HMI", "logs")
    lines = []
    if not os.path.isdir(log_dir):
        return lines
    for name in sorted(os.listdir(log_dir)):
        if not name.endswith(".log"):
            continue
        try:
            with io.open(os.path.join(log_dir, name), encoding="utf-8",
                         errors="replace") as f:
                for ln in f:
                    lines.append(ln.rstrip("\n"))
        except OSError:
            pass
    return lines


def count_lines(lines, needle):
    """★ 必须逐行数,不能把整个数组丢给 Select-String/-InputObject ——
    那样它会把数组【拼成一个字符串】再匹配,数出来的是"文件里有几处",
    不是"有几行"。(第一版就是这么写的,数字看着像那么回事,其实是错的。)"""
    n = 0
    for ln in lines:
        if needle in ln:
            n += 1
    return n


def slave_fc6(outdir):
    p = os.path.join(outdir, "slave.log")
    n = 0
    if os.path.isfile(p):
        with io.open(p, encoding="utf-8", errors="replace") as f:
            for ln in f:
                if "FC6 写" in ln:
                    n += 1
    return n


def parse_verdict(text):
    out = {}
    m = re.search(r"FC6 写帧\s*:\s*(\d+)", text)
    out["fc6"] = int(m.group(1)) if m else -1
    m = re.search(r"值不等于期望值的写帧\s+(\d+)\s*/\s*(\d+)", text)
    out["bad_value"] = int(m.group(1)) if m else -1
    out["value_total"] = int(m.group(2)) if m else -1
    out["green"] = "结论:【绿】" in text
    out["red"] = "结论:【红】" in text
    # 哪几条判据没过 —— 负向对照必须知道"红在哪一条"
    out["failed"] = re.findall(r"✗\s*(J\d+[a-z]?)\s+([^:：\n]+)", text)
    return out


def collect(outdir):
    text = read_text(os.path.join(outdir, "verdict.txt"))
    p = parse_verdict(text)
    lines = app_log_lines(outdir)
    p["queued"] = count_lines(lines, "写请求【排队】")
    p["queued_sent"] = count_lines(lines, "排队写已发出")
    p["rejected"] = count_lines(lines, "的 driver 拒收了")
    p["queue_full"] = count_lines(lines, "写队列已满")
    p["write_ok"] = count_lines(lines, "[Write] OK:")
    p["slave_fc6"] = slave_fc6(outdir)
    p["verdict"] = text
    return p


def describe(tag, p):
    print("  %s:" % tag)
    print("     FC6 写帧(从站收到) = %d" % p["slave_fc6"])
    print("     [Write] OK 行      = %d" % p["write_ok"])
    print("     排队(入队)         = %d" % p["queued"])
    print("     排队写已发出       = %d" % p["queued_sent"])
    print("     driver 拒收        = %d" % p["rejected"])
    print("     整轮判定           = %s" % ("绿" if p["green"]
                                          else ("红" if p["red"] else "?")))
    if p["failed"]:
        for name, why in p["failed"]:
            print("       ✗ %s %s" % (name, why.strip()))


def run_short(tag, minutes, reply_delay_ms, port, alarm_min_ms):
    outdir = os.path.join(ROOT, "soaks", "negctl_write_starve_%s" % tag)
    if os.path.isdir(outdir):
        shutil.rmtree(outdir, ignore_errors=True)
    os.makedirs(outdir, exist_ok=True)
    cmd = [
        "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
        "-File", SHORT_RUN,
        "-Minutes", str(minutes),
        "-ReplyDelayMs", str(reply_delay_ms),
        "-AlarmMinMs", str(alarm_min_ms),
        "-SlavePort", str(port),
        "-OutDir", outdir,
    ]
    r = run(cmd, cwd=ROOT)
    p = collect(outdir)
    p["outdir"] = outdir
    p["exit_code"] = r.returncode
    return p


def mutate(original, repl_body):
    """把"排队"整段换成 repl_body。返回 (新文本, 错误信息)"""
    nl = "\r\n" if "\r\n" in original else "\n"

    # ---- 1. 先用【唯一标记】定位排队分支 ----
    hits = [i for i in range(len(original))
            if original.startswith(MUT_ANCHOR, i)]
    if len(hits) != 1:
        return None, ("锚点『%s』出现 %d 次(必须恰好 1 次)—— "
                      "ModbusMaster.cpp 变了?先改脚本。" % (MUT_ANCHOR, len(hits)))
    anchor = hits[0]

    # ---- 2. 从锚点往【上】找这个 if 的开头,往【下】找它的结尾 ----
    begin = original.rfind(MUT_BEGIN, 0, anchor)
    if begin < 0:
        return None, "找不到排队分支的 if 开头"
    endmark = MUT_END.replace("\n", nl)
    end = original.find(endmark, anchor)
    if end < 0:
        return None, "找不到排队分支的结尾(return queuedReply)"
    end += len(endmark)
    if end - anchor > MUT_MAX_LEN:
        return None, ("从锚点到结尾 %d 字符,太远了(上限 %d)—— 找错分支了"
                      % (end - anchor, MUT_MAX_LEN))

    # ---- 3. ★ 自检:被替换的区间必须【就是】排队分支 ★ ----
    seg = original[begin:end]
    if len(seg) < MUT_MIN_LEN:
        return None, ("被替换区间只有 %d 字符(下限 %d)—— 锚点没抓全"
                      % (len(seg), MUT_MIN_LEN))
    missing = [m for m in MUT_MUST_CONTAIN if m not in seg]
    if missing:
        return None, ("被替换区间缺少标记 %s —— 抓错了地方,拒绝替换"
                      % ", ".join(missing))
    print("   变异区间: %d 字符,标记齐全 ✓" % len(seg))

    repl = repl_body.replace("\n", nl)
    return original[:begin] + repl + original[end:], None


def restore(orig_text):
    with io.open(MODBUS_MASTER, "w", encoding="utf-8", newline="") as f:
        f.write(orig_text)
    touch(MODBUS_MASTER)


def phase(args, tag, minutes, delay, port, alarm, want_bite, bite_name, want_red):
    """跑一个阶段,最多重试 attempts 次,直到【咬到】(并且符合预期颜色)。

    返回 (结果dict, 说明文字)  说明文字 == "ok" 表示这一阶段成立。
    """
    last = None
    for attempt in range(1, args.attempts + 1):
        p = run_short(tag, minutes, delay, port, alarm)
        port += 1
        describe("%s 第 %d 轮" % (tag, attempt), p)
        last = p
        if p[want_bite] <= 0:
            print("   ★ 这一轮【没咬到】(%s = 0)—— 变异体/排队通路没被走到 ⇒ "
                  "这一轮验不到东西(【不是绿,是无效】)。重试。" % bite_name)
            continue
        if want_red and p["green"]:
            print("   ★ 咬到了,但判据【还是绿的】—— 这才是真问题:判据没牙。")
            return p, "咬到了但没红"
        if (not want_red) and (not p["green"]):
            print("   ★ 咬到了,但这一轮【不绿】—— 先修基线再谈负向对照。")
            print(p["verdict"][-1200:])
            return p, "咬到了但不绿"
        return p, "ok"
    return last, "没咬到(无效)"


def main():
    ap = argparse.ArgumentParser(description="现场级负向对照:拆掉写排队,判据必须变红")
    ap.add_argument("--minutes", type=int, default=2)
    ap.add_argument("--reply-delay-ms", type=int, default=120,
                    help="从站回包延迟(ms)。★ 实测:120 撞车率最高;"
                         "190/400 反而撞不到(方向跟直觉相反,见文件头)")
    ap.add_argument("--alarm-min-ms", type=int, default=85,
                    help="报警阈值(用 85:历史上写被饿死的那一档)")
    ap.add_argument("--port", type=int, default=1590)
    ap.add_argument("--attempts", type=int, default=3,
                    help="每一阶段最多重试几轮(撞车相位是开机掷一次硬币)")
    args = ap.parse_args()

    if not os.path.isfile(MODBUS_MASTER):
        print("找不到源文件: " + MODBUS_MASTER)
        return 1

    original = io.open(MODBUS_MASTER, encoding="utf-8", newline="").read()
    print("=== 准备:生成两种变异体 ===")
    mut_revert, err = mutate(original, REPL_REVERT)
    if err:
        print("!! 变异①定位失败: " + err)
        return 1
    mut_always, err = mutate(original, REPL_ALWAYS)
    if err:
        print("!! 变异②定位失败: " + err)
        return 1

    port = args.port
    results = {}
    try:
        # ================= 1. 基线(没改代码) =================
        print("\n=== 1/4 基线(未改代码)===")
        rc, out = build_main()
        if rc != 0:
            print("基线编译失败:\n", out[-3000:])
            return 1
        base, why = phase(args, "base", args.minutes, args.reply_delay_ms, port,
                          args.alarm_min_ms, "queued", "排队", False)
        port += args.attempts
        if why != "ok":
            print("!! 基线阶段不成立: " + why)
            print("   ⇒ 加大 --reply-delay-ms 再试(默认 120,实测撞车率最高)。")
            return 1
        # 排队的写必须【真的到了从站】—— 这才是 BUGFIX-E1 的核心
        if base["slave_fc6"] < base["queued_sent"]:
            print("!! 排队的写没有全部到达从站(发出 %d / 到达 %d)"
                  % (base["queued_sent"], base["slave_fc6"]))
            return 1
        results["base"] = base

        # ================= 2. 变异①:退回拒收 =================
        print("\n=== 2/4 变异①:把『排队』换成『直接拒收』(BUGFIX-E1 之前的原样)===")
        with io.open(MODBUS_MASTER, "w", encoding="utf-8", newline="") as f:
            f.write(mut_revert)
        touch(MODBUS_MASTER)
        rc, out = build_main()
        if rc != 0:
            print("变异后编译失败:\n", out[-3000:])
            return 1
        mut1, why = phase(args, "revert", args.minutes, args.reply_delay_ms, port,
                          args.alarm_min_ms, "rejected", "拒收", True)
        port += args.attempts
        if why != "ok":
            print("!! 变异①阶段不成立: " + why)
            return 1
        results["revert"] = mut1

        # ================= 3. 变异②:恒拒收 =================
        print("\n=== 3/4 变异②:让【每一次】写都被拒(判据的牙,不依赖相位)===")
        with io.open(MODBUS_MASTER, "w", encoding="utf-8", newline="") as f:
            f.write(mut_always)
        touch(MODBUS_MASTER)
        rc, out = build_main()
        if rc != 0:
            print("变异后编译失败:\n", out[-3000:])
            return 1
        mut2, why = phase(args, "always", args.minutes, args.reply_delay_ms, port,
                          args.alarm_min_ms, "rejected", "拒收", True)
        port += args.attempts
        if why != "ok":
            print("!! 变异②阶段不成立: " + why)
            return 1
        results["always"] = mut2

        # ================= 4. 还原 =================
        print("\n=== 4/4 还原 ===")
        restore(original)
        rc, out = build_main()
        if rc != 0:
            print("还原后编译失败:\n", out[-3000:])
            return 1
        res, why = phase(args, "restore", args.minutes, args.reply_delay_ms, port,
                         args.alarm_min_ms, "queued", "排队", False)
        port += args.attempts
        if why != "ok":
            print("!! 还原阶段不成立: " + why)
            return 1
        results["restore"] = res

        print("\n★ 结论 —— 现场级判据对『写被饿死』【有牙】 ★")
        print("  基线(没改):  排队 %d 次 → 从站收到 FC6 %d 条,整轮【绿】"
              % (base["queued"], base["slave_fc6"]))
        print("  变异①(退回拒收): 拒收 %d 次 → 从站收到 FC6 %d 条,整轮【红】"
              % (mut1["rejected"], mut1["slave_fc6"]))
        print("  变异②(恒拒收):   拒收 %d 次 → 从站收到 FC6 %d 条,整轮【红】"
              % (mut2["rejected"], mut2["slave_fc6"]))
        print("  还原后:      排队 %d 次 → 从站收到 FC6 %d 条,整轮【绿】"
              % (res["queued"], res["slave_fc6"]))
        print("★ ⇒ 判据抓得住『写被饿死』 ⇒ 那几个绿是真的绿。 ★")
        return 0
    finally:
        restore(original)
        print("\n已还原源文件")


if __name__ == "__main__":
    sys.exit(main())
