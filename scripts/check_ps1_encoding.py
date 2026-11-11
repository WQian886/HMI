#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_ps1_encoding.py —— .ps1 的【落盘形态】判据(2026-11-11 加,G-31-c)

为什么需要它
------------
本仓库有一条硬规矩:**含中文的 .ps1 必须存成 UTF-8 with BOM + CRLF**
(见 CLAUDE.md「Shell 传参纪律」那一节,以及 G-4:2026-10-14 / 2026-10-24 各踩过一次)。

★ 以前这条规矩【没有任何东西执行它】★
  全仓搜过 BOM/utf-8-sig/CRLF/0x0D —— 49 处命中全是别的意思
  (check_vcxproj_writeback.py 查的是 .vcxproj 的 BOM/CRLF,
   fix_filters_entries.py 是它自己的写回器,
   tst_CsvExporter.cpp 查的是 CSV 输出的 BOM)。
  **没有一条判据检查任何 .ps1 的 BOM 或行尾。**

  而 `check_ps_format_leak.ps1` 之所以"好像"在管这件事,靠的是一个**副作用**:
  它用 `Parser::ParseFile()` 读文件,PS 5.1 按【系统 ANSI 代码页】读无 BOM 的
  UTF-8 ⇒ 乱码可能吃掉引号 ⇒ 报「[解析失败]」。
  那个红跟"BOM 缺失"这个真问题**毫无关系**,而且它**依赖机器的 ACP**:
  实测同一份无 BOM 的中文 .ps1,ACP=936(本机)报解析错,ACP=1252(US 机器)解析干净
  ⇒ 本地红、CI 绿,而红的理由是"脚本写坏了"(误诊)。
  更糟的一层:像 `中文测试` 这种内容在两台机器上都不报解析错
  ⇒ BOM-less 文件在两边都**静默通过**这条判据。

  2026-11-11 实测:仓库里 18 个 .ps1 全带 BOM,但
  `hmi/installer/package.ps1` 是**唯一一个 LF 文件** —— 这条规矩今天已经被违反,
  而**没有任何东西会红**。

这条判据怎么做的
----------------
★ 判【git 索引里的 blob】,不判工作区文件 ★
  这是让它"本地和 CI 必然一致"的唯一办法:
    · blob 是提交的一部分 ⇒ 任何机器上跑都是同一份字节;
    · 工作区文件会被 autocrlf / 编辑器 / 上次 checkout 的残留影响
      ⇒ 拿它当判据 = 判据依赖本地状态(就是本轮要消灭的那一族)。
  行尾怎么保证?靠 **.gitattributes 的 `*.ps1 text eol=crlf`**:
    索引里存 LF(纯 LF,没有 CR)⇒ checkout 时落成 CRLF。
    所以判据查两件事:
      ① 属性在位(git check-attr 亲口回答 text=set / eol=crlf)
      ② blob 里有 BOM、没有裸 CR、是合法 UTF-8
    两件事合起来 ⇒ 任何机器上 checkout 出来的 .ps1 一定是【UTF-8+BOM+CRLF】。

用法
----
    python scripts/check_ps1_encoding.py
退出码:0 全过 / 1 有断言没过 / 2 环境不对(没有 git)
"""
import subprocess
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
ROOT = SCRIPTS.parent
BOM = b"\xef\xbb\xbf"

fails: list[str] = []


def check(label: str, cond: bool, detail: str = "") -> None:
    print("  %s %s %s" % ("OK  " if cond else "FAIL", label, detail))
    if not cond:
        fails.append(label)


def git_bytes(*args: str) -> bytes:
    p = subprocess.run(
        ["git", "-C", str(ROOT)] + list(args),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    if p.returncode != 0:
        raise RuntimeError(p.stderr.decode("utf-8", "replace").strip())
    return p.stdout


def main() -> int:
    try:
        git_bytes("rev-parse", "--is-inside-work-tree")
    except FileNotFoundError:
        print("FAIL 找不到 git —— 这条判据必须在 git 仓库里跑")
        return 2
    except RuntimeError as e:
        print("FAIL 不是 git 仓库:%s" % e)
        return 2

    # ---- 权威集合:受版本控制的 .ps1 ----------------------------------
    files = [ln for ln in git_bytes("ls-files", "--cached", "--", "*.ps1")
             .decode("utf-8", "replace").splitlines() if ln.strip()]
    print("=== 受版本控制的 .ps1:%d 个 ===" % len(files))
    for f in files:
        print("  %s" % f)
    check("至少找到一个受版本控制的 .ps1", len(files) > 0,
          "(一个都没有 ⇒ 这条判据什么也没查,跟『没查』没法区分)")

    # ---- 属性:.gitattributes 必须在索引里,且真的管着 *.ps1 -----------
    attrs_file = git_bytes("ls-files", "--cached", "--", ".gitattributes")
    check(".gitattributes 在索引里(规则要跟着提交走)",
          b".gitattributes" in attrs_file,
          "(只有工作区里有 ⇒ 别的机器上这条规则不存在)")

    # git check-attr 亲口回答 —— 不自己解析 .gitattributes 的语法
    attr_lines = git_bytes("check-attr", "text", "eol", "--", *files).decode(
        "utf-8", "replace").splitlines()
    attr: dict[str, dict[str, str]] = {}
    for ln in attr_lines:
        # 格式:<path>: <attr>: <value>(路径里也可能带冒号 ⇒ 贪婪匹配路径)
        parts = ln.rsplit(": ", 2)
        if len(parts) != 3:
            continue
        attr.setdefault(parts[0], {})[parts[1]] = parts[2]

    bad_text = [f for f in files if attr.get(f, {}).get("text") != "set"]
    bad_eol = [f for f in files if attr.get(f, {}).get("eol") != "crlf"]
    check("每个 .ps1 都带 text 属性", not bad_text,
          "(没带 ⇒ checkout 不会统一行尾:%s)" % (", ".join(bad_text) or "无"))
    check("每个 .ps1 都是 eol=crlf", not bad_eol,
          "(不是 ⇒ 同一个提交在不同机器上落盘形态不同:%s)" % (", ".join(bad_eol) or "无"))

    # ---- 逐字节查 blob ------------------------------------------------
    print()
    print("=== 逐字节查 blob(BOM / 裸 CR / UTF-8 合法性) ===")
    for f in files:
        blob = git_bytes("show", ":" + f)
        has_bom = blob.startswith(BOM)
        has_cr = b"\r" in blob
        try:
            blob.decode("utf-8")
            utf8_ok = True
        except UnicodeDecodeError:
            utf8_ok = False
        check("%s" % f,
              has_bom and (not has_cr) and utf8_ok,
              "(BOM=%s 裸CR=%s 合法UTF-8=%s)" % (
                  "有" if has_bom else "★缺",
                  "★有" if has_cr else "无",
                  "是" if utf8_ok else "★否"))
        if not has_bom:
            print("      ⇒ PS 5.1 会按系统 ANSI 代码页读它 ⇒ 中文变乱码、"
                  "引号可能被吃掉 ⇒ 变成语法错(而且报的错跟真因无关)")
        if has_cr:
            print("      ⇒ blob 里还有 CR ⇒ 没有被规范化成 LF ⇒ "
                  "checkout 出来会是 CRCRLF")

    print()
    if not fails:
        print("RESULT: PASS —— .ps1 的落盘形态判得住"
              "(UTF-8 + BOM + CRLF,与代码页无关、与工作区无关)")
        return 0
    print("RESULT: FAIL -> %s" % fails)
    return 1


if __name__ == "__main__":
    sys.exit(main())
