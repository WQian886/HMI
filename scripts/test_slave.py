#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_slave.py —— 自带的最小 Modbus TCP 从站(长跑用)

★ 为什么要自己写一个,而不是用 mbslave(那个 GUI 从站)
  ------------------------------------------------------
  1. mbslave 的数据表现在是空的:FC3 一律回 ExCode=0x03(Illegal Data Value),
     FC1/2/4 一律回 0x01。读必失败 —— 拿它跑长跑,验到的只是"重试正常",
     验不到"读得到数据"。
  2. mbslave 是 GUI 程序:行为受鼠标点击影响、不能脚本化、没法在 CI 里跑。
  3. 长跑要的是【可复现】的现场:寄存器表写死在脚本里,谁来跑都一样。

★ 这个从站能干什么
  - FC3/FC4 读保持/输入寄存器,值随时间变化(不是常数)——
    变化检测、趋势曲线、历史库才有东西可写。
  - FC6/FC16 写:写进去的值会留在寄存器里(能验"写-读回环")。
  - FC1/FC2 线圈:最小实现,读回固定状态。
  - 故障注入:--fail-from N / --silent-from N
    用来【主动制造】2026-10-21 查出的那个场景(从站失败),
    验证应用会重试、会恢复,而不是卡死在途态。

★ 零依赖
  只用 Python 标准库 —— 本机没有 pymodbus,也不该为了跑个从站去装它。
  (Python 3.12, `python scripts/test_slave.py --help`)

用法:
  python scripts/test_slave.py --port 1502 --duration 86400
  python scripts/test_slave.py --port 1502 --fail-from 100 --log slave.log

退出时打印一行汇总:总共收到多少请求、其中多少成功/异常/超时。
"""

import argparse
import math
import random
import socket
import socketserver
import struct
import sys
import threading
import time

# 寄存器表:名字 → (地址, 初值, 变化方式)
# ★ 跟 hmi/default.json 对齐:
#   Pressure@0(Int16) / Temperature@1(Int16) / Speed@2(UInt16)
#   长跑用 scripts/soak_project.json,那一份指向 127.0.0.1:1502。
REGISTERS = {
    0: ("Pressure",    0,    "wave"),
    1: ("Temperature", 22,   "wave"),
    2: ("Speed",       110,  "wave"),
    3: ("Counter",     0,    "ramp"),
    4: ("Status",      1,    "static"),
}


class RegisterBank:
    """寄存器组 + 值的演化。线程安全(读请求可能来自多个连接)。"""

    # ★ 改动之前的波形相位是 (t / 60.0),而 sin 的周期是 2π ——
    #   所以那【实际周期】是 2π×60 ≈ 377 秒,不是 60 秒。
    #   ★ 2026-10-26 实测印证:4 分钟短跑里温度只越限了 3 次,
    #     相邻两次间隔 ~188 秒 = 377/2(一个波峰/波谷)—— 对得上。★
    #   这里把"周期"直接做成【秒数】,默认值 = 2π×60(与改动之前【逐位相同】),
    #   这样 24h 长跑的行为一个字都没变。
    DEFAULT_WAVE_PERIOD = 2 * math.pi * 60.0   # ≈ 376.99111843077515

    def __init__(self, size=32, wave_period=None):
        self.size = size
        self.wave_period = (self.DEFAULT_WAVE_PERIOD if wave_period is None
                            else wave_period)
        self.lock = threading.Lock()
        self.values = [0] * size
        for addr, (_name, init, _kind) in REGISTERS.items():
            self.values[addr] = init
        self.counter = 0
        self.t0 = time.time()

    def tick(self):
        """把寄存器值往前推一格。"""
        with self.lock:
            t = time.time() - self.t0
            for addr, (_name, _init, kind) in REGISTERS.items():
                if kind == "wave":
                    # 慢三角波:慢到看不出一条直线,快到 24 小时里有明显起伏
                    # ★ phase 用 2π/周期 换算,让 --wave-period 的单位就是【秒】。
                    #   默认周期 = 2π×60 ⇒ 2π/周期 = 1/60 ⇒ 与改动之前完全一致。
                    phase = (2 * math.pi * t / self.wave_period) + addr
                    self.values[addr] = int(abs(math.sin(phase)) * 100)
                elif kind == "ramp":
                    self.counter += 1
                    self.values[addr] = self.counter % 65536
                # static 的不动

    def read(self, addr, count):
        with self.lock:
            out = []
            for i in range(count):
                a = addr + i
                if a < len(self.values):
                    out.append(self.values[a] & 0xFFFF)
                else:
                    out.append(0)
            return out

    def write(self, addr, values):
        with self.lock:
            for i, v in enumerate(values):
                a = addr + i
                if a < len(self.values):
                    self.values[a] = v & 0xFFFF


class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        self.requests = 0
        self.ok = 0
        self.exceptions = 0
        self.silent = 0
        self.by_fc = {}

    def bump(self, key, n=1):
        with self.lock:
            if key == "req":
                self.requests += n
            elif key == "ok":
                self.ok += n
            elif key == "exc":
                self.exceptions += n
            elif key == "silent":
                self.silent += n
            self.by_fc[key] = self.by_fc.get(key, 0) + n

    def snapshot(self):
        with self.lock:
            return dict(requests=self.requests, ok=self.ok,
                        exceptions=self.exceptions, silent=self.silent,
                        by_fc=dict(self.by_fc))


class Handler(socketserver.BaseRequestHandler):
    """一个连接。协议细节全在这里。"""

    def setup(self):
        self.bank = self.server.bank
        self.stats = self.server.stats
        self.log = self.server.log
        self.server.request_counter_lock.acquire()
        self.server.request_counter += 1
        self.conn_index = self.server.request_counter
        self.server.request_counter_lock.release()
        self.buf = b""
        self.failed = False   # --fail-from 之后开始回异常
        self.silent = False   # --silent-from 之后开始不回包

    def logline(self, msg):
        if self.log:
            stamp = time.strftime("%Y-%m-%d %H:%M:%S")
            with self.server.log_lock:
                with open(self.log, "a", encoding="utf-8") as f:
                    f.write("%s [slave] conn#%d %s\n" % (stamp, self.conn_index, msg))

    def handle(self):
        self.request.settimeout(1.0)
        while True:
            try:
                data = self.request.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                break
            if not data:
                break
            self.buf += data
            while len(self.buf) >= 8:
                # MBAP: TID(2) PID(2) LEN(2) | UID(1) FC(1) ...
                tid, pid, length = struct.unpack(">HHH", self.buf[0:6])
                if len(self.buf) < 6 + length:
                    break
                pdu = self.buf[6:6 + length]
                self.buf = self.buf[6 + length:]
                try:
                    self.dispatch(tid, pid, pdu)
                except Exception as e:          # 单个请求出错不能带崩整个从站
                    self.logline("处理请求时出错: %r" % (e,))
        try:
            self.request.close()
        except OSError:
            pass

    def dispatch(self, tid, pid, pdu):
        if len(pdu) < 1:
            return
        uid = pdu[0]
        fc = pdu[1]
        data = pdu[2:]

        self.stats.bump("req")
        self.stats.bump("fc%d" % fc)

        # ---- 故障注入 ----
        if self.server.silent_from and self.stats.snapshot()["requests"] >= self.server.silent_from:
            self.silent = True
        if self.server.fail_from and self.stats.snapshot()["requests"] >= self.server.fail_from:
            self.failed = True

        if self.silent:
            # 收下请求,一个字节都不回 —— 逼出应用的超时路径
            self.stats.bump("silent")
            self.logline("FC%d 静默(故障注入:不回答)" % fc)
            return

        # ★★ 回包延迟 --reply-delay-ms ★★
        #   【为什么要有这个】
        #     BUGFIX-E1 修的那个洞:写和读抢同一个在途槽,
        #     槽被读占着的时候写【被拒收】(修之前)⇒ 写被饿死。
        #     而"槽被占着"这件事,取决于【从站回包有多快】——
        #     立即回包的从站,槽几乎永远是空的 ⇒ 写永远撞不上
        #     ⇒ 那条【排队】通路【一次都不会被走到】。
        #     实测(2026-10-27):相位扫描 8 轮,"写请求【排队】"计数【全是 0】。
        #     ⇒ 也就是说:现场级扫描证明了"写能落地",
        #       但【没证明】"排队的写能不能落地"——那正是 BUGFIX-E1 的核心。
        #     加了回包延迟,就能【故意】制造撞车,让排队通路被真的走到。
        if self.server.reply_delay_ms:
            time.sleep(self.server.reply_delay_ms / 1000.0)

        if self.failed and fc in (0x03, 0x04, 0x01, 0x02):
            # 回 Modbus 异常响应:FC|0x80, ExCode=0x03 (Illegal Data Value)
            self.stats.bump("exc")
            self.logline("FC%d 回异常 ExCode=0x03(故障注入)" % fc)
            self.send(tid, uid, bytes([fc | 0x80, 0x03]))
            return

        if fc in (0x03, 0x04):        # 读保持/输入寄存器
            if len(data) < 4:
                self.send(tid, uid, bytes([fc | 0x80, 0x03]))
                return
            addr, count = struct.unpack(">HH", data[0:4])
            regs = self.bank.read(addr, count)
            payload = b"".join(struct.pack(">H", r) for r in regs)
            self.stats.bump("ok")
            self.logline("FC%d 读 addr=%d count=%d → %s" %
                         (fc, addr, count, [r for r in regs[:6]]))
            self.send(tid, uid, bytes([fc, len(payload)]) + payload)

        elif fc == 0x06:              # 写单个寄存器
            if len(data) < 4:
                self.send(tid, uid, bytes([fc | 0x80, 0x03]))
                return
            addr, value = struct.unpack(">HH", data[0:4])
            self.bank.write(addr, [value])
            self.stats.bump("ok")
            self.logline("FC6 写 addr=%d value=%d" % (addr, value))
            self.send(tid, uid, bytes([fc]) + data[0:4])

        elif fc == 0x10:              # 写多个寄存器
            if len(data) < 5:
                self.send(tid, uid, bytes([fc | 0x80, 0x03]))
                return
            addr, count = struct.unpack(">HH", data[0:4])
            byte_count = data[4]
            payload = data[5:5 + byte_count]
            values = [struct.unpack(">H", payload[i:i + 2])[0]
                      for i in range(0, len(payload) - 1, 2)]
            self.bank.write(addr, values[:count])
            self.stats.bump("ok")
            self.logline("FC16 写 addr=%d count=%d" % (addr, count))
            self.send(tid, uid, bytes([fc]) + data[0:4])

        elif fc in (0x01, 0x02):      # 线圈:最小实现,读回固定状态
            if len(data) < 4:
                self.send(tid, uid, bytes([fc | 0x80, 0x03]))
                return
            addr, count = struct.unpack(">HH", data[0:4])
            nbytes = (count + 7) // 8
            bits = bytearray(nbytes)
            if nbytes:
                bits[0] = 0x01        # 第 0 位 = ON
            self.stats.bump("ok")
            self.send(tid, uid, bytes([fc, nbytes]) + bytes(bits))

        else:
            # 不支持的功能码 → Illegal Function
            self.stats.bump("exc")
            self.logline("FC0x%02X 不支持 → ExCode=0x01" % fc)
            self.send(tid, uid, bytes([fc | 0x80, 0x01]))

    def send(self, tid, uid, pdu):
        # MBAP 长度字段 = UID(1) + PDU
        frame = struct.pack(">HHH", tid, 0, 1 + len(pdu)) + bytes([uid]) + pdu
        try:
            self.request.sendall(frame)
        except OSError:
            pass


class Slave(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, addr, bank, stats, log, fail_from, silent_from,
                 reply_delay_ms=0):
        super().__init__(addr, Handler)
        self.bank = bank
        self.stats = stats
        self.log = log
        self.log_lock = threading.Lock()
        self.fail_from = fail_from
        self.silent_from = silent_from
        # ★ 回包延迟(ms)。0 = 立即回(默认,保持改动前的行为逐位一致)。
        #   见 Handler.dispatch 里那段长注释:没有它,写排队那条通路
        #   【一次都不会被走到】。
        self.reply_delay_ms = reply_delay_ms
        self.request_counter = 0
        self.request_counter_lock = threading.Lock()


def main():
    ap = argparse.ArgumentParser(description="长跑用的最小 Modbus TCP 从站(零依赖)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=1502)
    ap.add_argument("--duration", type=int, default=0,
                    help="跑多少秒后自己退(0 = 一直跑)")
    ap.add_argument("--log", default="", help="把每条请求记到这个文件")
    ap.add_argument("--fail-from", type=int, default=0,
                    help="从第 N 个请求开始回 Modbus 异常(0 = 不注入)")
    ap.add_argument("--silent-from", type=int, default=0,
                    help="从第 N 个请求开始不回包(0 = 不注入)")
    ap.add_argument("--reply-delay-ms", type=int, default=0,
                    help=("回包延迟多少毫秒(0 = 立即回,默认)。\n"
                          "  ★ 为什么要它:BUGFIX-E1 修的【写排队】那条通路,\n"
                          "    走它的前提是【槽被读占着】。从站立即回包 ⇒ 槽几乎永远空着\n"
                          "    ⇒ 写永远撞不上 ⇒ 那条通路【一次都不会被走到】。\n"
                          "    实测(2026-10-27):相位扫描 8 轮,『写请求【排队】』计数全是 0。\n"
                          "    ⇒ 加了延迟才能【故意】制造撞车,把排队通路真的走一遍。\n"
                          "    ⚠ 加了延迟之后读也会变慢 ⇒ 轮询节奏会跟着变,数字不能跟默认跑法混着比。"))
    ap.add_argument("--tick", type=float, default=1.0,
                    help="寄存器值每几秒推一格")
    ap.add_argument("--wave-period", type=float, default=RegisterBank.DEFAULT_WAVE_PERIOD,
                    help=("波形周期(秒),单位就是秒。\n"
                          "  ★ 默认 = 2π×60 ≈ 377 秒 = 改动之前的实际行为(逐位相同)★\n"
                          "    (改动前的相位是 t/60,而 sin 的周期是 2π ——\n"
                          "     所以实际周期是 2π×60,不是 60。2026-10-26 实测印证:\n"
                          "     4 分钟短跑里温度只越限 3 次,相邻两次间隔 ~188 秒\n"
                          "     = 377/2,正好是一个波峰到波谷。)\n"
                          "  短跑要把它调小(用 8 秒):周期大的时候,一个完整的\n"
                          "  '越限→恢复'要 ~188 秒,4 分钟只够触发 ~3 次联动;\n"
                          "  而每次联动只有 ~45%% 的概率撞上'待写'窗口\n"
                          "  (轮询回包正好落在标脏和写出之间)——\n"
                          "  期望命中只有 ~1 次,抽到 0 次很正常。\n"
                          "  也就是说:那种短跑【根本没撞上竞态】,判据全绿是假绿。\n"
                          "  周期 8 秒 ⇒ 4 分钟约 38 次联动 ⇒ 期望命中 ~17 次。"))
    args = ap.parse_args()

    bank = RegisterBank(wave_period=args.wave_period)
    stats = Stats()
    slave = Slave((args.host, args.port), bank, stats, args.log,
                  args.fail_from or 0, args.silent_from or 0,
                  args.reply_delay_ms or 0)

    print("[slave] 监听 %s:%d" % (args.host, args.port), flush=True)
    print("[slave] 寄存器表: " + ", ".join(
        "%d=%s" % (a, n) for a, (n, _i, _k) in sorted(REGISTERS.items())), flush=True)
    print("[slave] 波形周期: %g 秒(tick=%g 秒)" % (args.wave_period, args.tick), flush=True)
    if args.fail_from:
        print("[slave] ★ 故障注入:第 %d 个请求开始回 ExCode=0x03" % args.fail_from, flush=True)
    if args.silent_from:
        print("[slave] ★ 故障注入:第 %d 个请求开始静默(逼超时路径)" % args.silent_from, flush=True)
    if args.reply_delay_ms:
        print("[slave] ★ 回包延迟 %d ms(故意让在途槽被占着 ⇒ 逼出写排队那条通路)"
              % args.reply_delay_ms, flush=True)

    stop = threading.Event()

    def ticker():
        while not stop.is_set():
            stop.wait(args.tick)
            bank.tick()

    t = threading.Thread(target=ticker, daemon=True)
    t.start()

    if args.duration > 0:
        def durater():
            time.sleep(args.duration)
            print("[slave] 到达 --duration %d 秒,自己退" % args.duration, flush=True)
            stop.set()
            try:
                slave.shutdown()
            except Exception:
                pass
        threading.Thread(target=durater, daemon=True).start()

    start = time.time()
    last_report = 0.0
    try:
        slave.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        s = stats.snapshot()
        print("[slave] 退出 —— 共 %d 个请求(成功 %d / 异常 %d / 静默 %d),跑了 %.1f 分钟"
              % (s["requests"], s["ok"], s["exceptions"], s["silent"],
                 (time.time() - start) / 60.0), flush=True)
        slave.server_close()


if __name__ == "__main__":
    main()
