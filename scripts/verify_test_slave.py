#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_test_slave.py —— test_slave.py 的自检(负向/正向对照)

  为什么要有:从站是长跑现场的【地基】。地基塌了,长跑跑出来的全是废数据,
  而且塌得【很安静】 —— 应用只是读不到值,日志里"一切正常"。
  所以从站必须先被单独验过:能读、能写、能制造异常、能制造超时。

  用法: python scripts/verify_test_slave.py
  退出码: 0 = 全过, 1 = 有红的
"""

import os
import socket
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SLAVE = os.path.join(HERE, "test_slave.py")
PY = sys.executable

ok_count = 0
red_count = 0


def check(name, cond, detail=""):
    global ok_count, red_count
    if cond:
        ok_count += 1
        print("  [OK]   %s  %s" % (name, detail))
    else:
        red_count += 1
        print("  [RED]  %s  %s" % (name, detail))


def mbap(tid, uid, pdu):
    return struct.pack(">HHH", tid, 0, 1 + len(pdu)) + bytes([uid]) + pdu


def read_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise RuntimeError("对端关闭,只收到 %d/%d 字节" % (len(buf), n))
        buf += chunk
    return buf


def read_response(sock):
    head = read_exact(sock, 7)          # TID(2) PID(2) LEN(2) UID(1)
    tid, _pid, length = struct.unpack(">HHH", head[0:6])
    body = read_exact(sock, length - 1)  # 去掉 UID
    return tid, body


def request(sock, tid, uid, pdu, expect_reply=True, timeout=5.0):
    sock.settimeout(timeout)
    sock.sendall(mbap(tid, uid, pdu))
    if not expect_reply:
        return None
    return read_response(sock)


def start_slave(port, extra=None):
    args = [PY, SLAVE, "--port", str(port)]
    if extra:
        args += extra
    proc = subprocess.Popen(args, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True,
                            encoding="utf-8", errors="replace")
    # 等它把端口监听起来
    for _ in range(50):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            s.close()
            return proc
        except OSError:
            time.sleep(0.1)
    proc.kill()
    raise RuntimeError("从站没起来")


def main():
    port = 15021

    # ------------------------------------------------------------------
    print("=== 用例 1:正常读(FC3) ===")
    proc = start_slave(port)
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=5)
        # FC3:读保持寄存器 addr=0 count=3
        tid, body = request(s, 1, 1, bytes([0x03]) + struct.pack(">HH", 0, 3))
        fc = body[0]
        byte_count = body[1]
        regs = [struct.unpack(">H", body[2 + i * 2:4 + i * 2])[0] for i in range(byte_count // 2)]
        check("TID 原样回来", tid == 1, "TID=%d" % tid)
        check("功能码是 FC3", fc == 0x03, "fc=0x%02X" % fc)
        check("回了 3 个寄存器", len(regs) == 3, "regs=%s" % regs)
        check("byteCount = 6", byte_count == 6, "byteCount=%d" % byte_count)

        # ------------------------------------------------------------------
        print("=== 用例 2:写(FC6)再读回 ===")
        tid, body = request(s, 2, 1, bytes([0x06]) + struct.pack(">HH", 5, 1234))
        check("FC6 回显", body[0] == 0x06 and body[1:5] == struct.pack(">HH", 5, 1234),
              "body=%s" % body.hex(' '))
        tid, body = request(s, 3, 1, bytes([0x03]) + struct.pack(">HH", 5, 1))
        val = struct.unpack(">H", body[2:4])[0]
        check("写进去的值读得回来", val == 1234, "读到 %d" % val)

        # ------------------------------------------------------------------
        print("=== 用例 3:值会变(不是常数) ===")
        s.close()
        s = socket.create_connection(("127.0.0.1", port), timeout=5)
        seen = set()
        for i in range(6):
            tid, body = request(s, 10 + i, 1, bytes([0x03]) + struct.pack(">HH", 1, 1))
            seen.add(struct.unpack(">H", body[2:4])[0])
            time.sleep(0.6)
        check("温度寄存器在动", len(seen) > 1, "取到 %d 个不同值" % len(seen))
        s.close()
    finally:
        proc.terminate()
        proc.wait(timeout=5)

    # ------------------------------------------------------------------
    print("=== 用例 4:故障注入 --fail-from(回 ExCode=0x03) ===")
    proc = start_slave(port, ["--fail-from", "2"])
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=5)
        tid, body = request(s, 1, 1, bytes([0x03]) + struct.pack(">HH", 0, 3))
        check("第 1 个请求还是正常回答", body[0] == 0x03, "fc=0x%02X" % body[0])
        tid, body = request(s, 2, 1, bytes([0x03]) + struct.pack(">HH", 0, 3))
        check("第 2 个请求开始回异常", body[0] == 0x83 and body[1] == 0x03,
              "body=%s" % body.hex(' '))
        s.close()
    finally:
        proc.terminate()
        proc.wait(timeout=5)

    # ------------------------------------------------------------------
    print("=== 用例 5:故障注入 --silent-from(不回包,逼超时路径) ===")
    proc = start_slave(port, ["--silent-from", "2"])
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=5)
        tid, body = request(s, 1, 1, bytes([0x03]) + struct.pack(">HH", 0, 3))
        check("第 1 个请求正常", body[0] == 0x03, "fc=0x%02X" % body[0])
        # 第 2 个请求:对端不回,读响应必须超时(不能死等)
        t0 = time.time()
        timed_out = False
        try:
            request(s, 2, 1, bytes([0x03]) + struct.pack(">HH", 0, 3), timeout=2.0)
        except (socket.timeout, RuntimeError):
            timed_out = True
        check("第 2 个请求确实没人回(读超时)", timed_out, "等了 %.1f 秒" % (time.time() - t0))
        s.close()
    finally:
        proc.terminate()
        proc.wait(timeout=5)

    # ------------------------------------------------------------------
    print("=== 用例 6:不支持的功能码 → Illegal Function ===")
    proc = start_slave(port)
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=5)
        tid, body = request(s, 1, 1, bytes([0x2B]) + b"\x00\x00")
        check("回 ExCode=0x01", body[0] == 0xAB and body[1] == 0x01,
              "body=%s" % body.hex(' '))
        s.close()
    finally:
        proc.terminate()
        proc.wait(timeout=5)

    print("\n=== 小计: OK=%d  RED=%d ===" % (ok_count, red_count))
    return 1 if red_count else 0


if __name__ == "__main__":
    sys.exit(main())
