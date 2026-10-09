#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_vcxproj_writeback.py — 守 sync_vcxproj.py 的写回路径(2026-10-10 加)

为什么需要它
--------
本项目已经被 vcxproj 写坏过两次,两次都是"脚本报告成功,文件其实坏了":

  1. fix_filters_entries.py 里 return 0 排在 write_filters() 前面,
     打印"✅ 已补 2 条"、退出码 0,文件一个字节没动。
  2. sync_vcxproj.py 用 ElementTree tree.write() 写回 vcxproj,
     BOM 没了、CRLF 变 LF、整个文件重排(395 增 403 删)。

第 2 次还挖出更深一层:即使保住 BOM 和 CRLF,ElementTree 依然把
<Project xmlns="..."> 写成 <ns0:Project xmlns:ns0="...">,并且
【丢掉全部 68 条 XML 注释】—— 文件从 23814 字节缩到 20103。

★ 所以这里逐条对着真实文件断言,不靠"脚本自己说成功"。
  守的判据只有一句话:写回必须是【纯增行、零删行】。
  真的 diff 就该是"加了两条",不是"重排了四百行"。

在 TEMP 里的副本上跑,绝不碰真的 hmi.vcxproj。

用法
----
    python scripts/check_vcxproj_writeback.py
退出码:0 全过 / 1 有断言没过 / 2 找不到 hmi.vcxproj
"""
import re
import shutil
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))
import sync_vcxproj as s

REAL = SCRIPTS.parent / "hmi" / "hmi.vcxproj"
if not REAL.exists():
    print("错误:找不到 %s" % REAL, file=sys.stderr)
    sys.exit(2)

tmp = Path(tempfile.mkdtemp(prefix="vcxproj_strict_"))
dst = tmp / "hmi.vcxproj"
shutil.copy2(REAL, dst)

before_b = dst.read_bytes()
before_t = before_b.decode("utf-8-sig")

s.insert_include_into_vcxproj(dst, ["src\\probe\\Fake.cpp"], ["src\\probe\\Fake.h"])

after_b = dst.read_bytes()
after_t = after_b.decode("utf-8-sig")

fails = []


def check(label, cond, detail=""):
    print("  %s %s %s" % ("OK  " if cond else "FAIL", label, detail))
    if not cond:
        fails.append(label)


print("=== 编码 / 行尾 ===")
check("BOM 保留", after_b[:3] == b"\xef\xbb\xbf", repr(after_b[:3]))
check("无裸 LF", after_b.count(b"\n") - after_b.count(b"\r\n") == 0,
      "(CRLF=%d bareLF=%d)" % (after_b.count(b"\r\n"),
                               after_b.count(b"\n") - after_b.count(b"\r\n")))

print("=== 内容零损伤(这是上一版漏掉的)===")
bl = before_t.split("\r\n")
al = after_t.split("\r\n")
check("无 ns0: 前缀", "ns0:" not in after_t)
check("<Project 开标签原样", "<Project DefaultTargets=" in after_t)
check("原有注释一条没丢",
      all(c in after_t for c in re.findall(r"<!--.*?-->", before_t, re.S)),
      "(%d -> %d,含本脚本自己加的 1 条)" % (before_t.count("<!--"), after_t.count("<!--")))
check("纯增行、零删行", len(al) > len(bl), "(%d -> %d 行,净增 %d)"
      % (len(bl), len(al), len(al) - len(bl)))
check("原有每一行都还在",
      all(line in after_t for line in bl if line.strip()),
      "(抽查 %d 行)" % len([l for l in bl if l.strip()]))

print("=== 新条目真的进去了 ===")
root = ET.parse(dst)
ns = "{http://schemas.microsoft.com/developer/msbuild/2003}"
cpp = [e.get("Include") for e in root.iter(ns + "ClCompile")]
hdr = [e.get("Include") for e in root.iter(ns + "ClInclude")]
check("ClCompile 数量 92->93", len(cpp) == len([e for e in ET.parse(REAL).iter(ns + "ClCompile")]) + 1)
check("Fake.cpp 在册", "src\\probe\\Fake.cpp" in cpp)
check("Fake.h 在册", "src\\probe\\Fake.h" in hdr)

print("=== 幂等:再写一次同一个文件,不该重复加 ===")
s.insert_include_into_vcxproj(dst, ["src\\probe\\Fake.cpp"], ["src\\probe\\Fake.h"])
again = dst.read_bytes().decode("utf-8-sig")
check("重复调用不加第二遍", again.count("Fake.cpp") == 1,
      "(出现 %d 次)" % again.count("Fake.cpp"))

shutil.rmtree(tmp, ignore_errors=True)
print()
print("RESULT:", "PASS" if not fails else "FAIL -> %s" % fails)
sys.exit(0 if not fails else 1)