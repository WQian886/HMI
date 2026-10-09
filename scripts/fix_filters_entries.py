#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fix_filters_entries.py — 一次性修补工具(2026-10-09)

补 hmi.vcxproj.filters 里【已在 vcxproj 注册、但 filters 漏了】的条目。

【为什么需要它】
    约定 3 要求新增 .cpp/.h 同步登记到 vcxproj AND filters。
    实际执行时,登记往往只做了 vcxproj 那一半 —— filters 漏了一大批,
    症状是:这些文件在 VS 解决方案资源管理器里【根本看不到】。
    编译不受影响(filters 只管显示),所以 ctest 全绿也照样漏。

    sync_vcxproj.py 明确不动 filters(它自己第 378 行写着"逻辑太复杂")。
    这个脚本补上那一半 —— 两者合起来才算把约定 3 落实了。

【为什么不直接手改 filters】
    文件是 UTF-8 **带 BOM** + 全 CRLF。用编辑器/脚本手改容易把 BOM 弄丢,
    结果 git diff 顶部凭空多一行删除,审查时真假难辨(本项目踩过一次)。
    这个脚本按字节读写,原样保留 BOM 与行尾。

【也查反向:filters 有、vcxproj 没有的残留条目】
    实测抓到 2 条 —— HighAlarmChecker.h / .cpp,文件在 369451f 就删了,
    改名成 ThresholdAlarmChecker 了,但 filters 里的旧名没人清。
    症状同上:VS 资源管理器里点开是个不存在的文件。
    --prune 才会删,默认只报告 —— 删条目是不可逆操作,留给 Wqian 拍板。

【幂等】
    已存在的条目不重复添加,可重复运行。

用法
----
    python scripts/fix_filters_entries.py           # 默认 dry-run
    python scripts/fix_filters_entries.py --apply   # 真写(补缺失)
    python scripts/fix_filters_entries.py --prune   # 真删(清残留,不动补)
    python scripts/fix_filters_entries.py --apply --prune   # 补 + 删
"""
from __future__ import annotations

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
FILTERS = REPO_ROOT / "hmi" / "hmi.vcxproj.filters"
VCXPROJ = REPO_ROOT / "hmi" / "hmi.vcxproj"

# 标签 → filters 里该用的分类名
TAG_TO_FILTER = {
    "ClCompile": "Source Files",
    "ClInclude": "Header Files",
    "QtMoc": "Header Files",
    "QtUic": "Form Files",
    "QtRcc": "Resource Files",
}

GREEN = "\033[32m"
YELLOW = "\033[33m"
RED = "\033[31m"
RESET = "\033[0m"


def read_text(path: Path) -> str:
    """按 UTF-8-sig 读(自动吃掉 BOM),返回正文。"""
    return path.read_text(encoding="utf-8-sig")


def extract_includes(text: str, tag: str) -> list[str]:
    """取出 <tag Include="..." /> 的路径,保持出现顺序、去重。"""
    seen: set[str] = set()
    out: list[str] = []
    for m in re.finditer(r'<' + tag + r'\s+Include="([^"]+)"', text):
        p = m.group(1).replace("/", "\\")
        if p not in seen:
            seen.add(p)
            out.append(p)
    return out


def build_missing(vcx_text: str, filters_text: str) -> list[tuple[str, str]]:
    """算出 (tag, path) 列表 —— 在 vcxproj 里但 filters 里没有的。"""
    missing: list[tuple[str, str]] = []
    for tag in TAG_TO_FILTER:
        in_v = extract_includes(vcx_text, tag)
        in_f = set(extract_includes(filters_text, tag))
        for p in in_v:
            if p not in in_f:
                missing.append((tag, p))
    return missing


def build_stale(vcx_text: str, filters_text: str) -> list[tuple[str, str]]:
    """算出 (tag, path) 列表 —— 在 filters 里但 vcxproj 没有的残留条目。

    ★ 判据是【vcxproj 没有】,不是【磁盘上没有】。
      两者多数时候一致,但不完全:
      vcxproj 是"这个工程实际编/用的东西"的权威,filters 只是显示用的镜像。
      拿磁盘当基准会误伤"vcxproj 有意保留但磁盘暂时没有"的条目。
    """
    stale: list[tuple[str, str]] = []
    for tag in TAG_TO_FILTER:
        in_f = extract_includes(filters_text, tag)
        in_v = set(extract_includes(vcx_text, tag))
        for p in in_f:
            if p not in in_v:
                stale.append((tag, p))
    return stale


def remove_entries(text: str, targets: list[tuple[str, str]]) -> str:
    """删掉指定的完整条目块(含其 <Filter> 子节点和注释)。

    逐块匹配而不是单行删 —— <Filter> 是子节点,只删 Include 那行会留下
    一个孤立的 </QtMoc> 把 XML 搞坏。
    """
    for tag, path in targets:
        # 转义路径里可能出现的 XML 特殊字符
        safe = (path.replace("&", "&amp;").replace("<", "&lt;").replace('"', "&quot;"))
        block = re.compile(
            r'[ \t]*<' + tag + r'\s+Include="' + re.escape(safe) + r'"[^>]*>'
            r'(?:(?!</' + tag + r'>).)*?</' + tag + r'>[ \t]*\r?\n',
            re.DOTALL,
        )
        text, n = block.subn("", text, count=1)
        if n != 1:
            raise RuntimeError(
                f"删条目失败:{tag} {path}(匹配到 {n} 处,预期 1)—— 已中止,文件未改"
            )
    return text


def render_entry(tag: str, path: str, filter_name: str) -> str:
    return (
        f'    <{tag} Include="{path}">\r\n'
        f"      <Filter>{filter_name}</Filter>\r\n"
        f"    </{tag}>\r\n"
    )


def write_filters(text: str) -> None:
    """按字节写回,原样保留 UTF-8 BOM + CRLF。

    ★ 丢 BOM 会让 git diff 顶部凭空多一行删除,审查时真假难辨 ——
      本项目踩过一次,所以这里不依赖任何库的默认编码行为。
    """
    with open(FILTERS, "wb") as f:
        f.write(b"\xef\xbb\xbf")          # UTF-8 BOM
        f.write(text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="补齐 / 清理 hmi.vcxproj.filters 条目。")
    ap.add_argument("--apply", action="store_true", help="真写(补缺失条目)")
    ap.add_argument("--prune", action="store_true", help="真写(删残留条目)")
    args = ap.parse_args(argv)

    if not FILTERS.exists():
        print(f"{RED}错误:找不到 {FILTERS}{RESET}", file=sys.stderr)
        return 2
    if not VCXPROJ.exists():
        print(f"{RED}错误:找不到 {VCXPROJ}{RESET}", file=sys.stderr)
        return 2

    vcx_text = read_text(VCXPROJ)
    filters_text = read_text(FILTERS)
    missing = build_missing(vcx_text, filters_text)
    stale = build_stale(vcx_text, filters_text)
    ghosts: list[tuple[str, str]] = []

    changed = False

    # ---- 报告缺失 -------------------------------------------------------
    if missing:
        print(f"{YELLOW}filters 缺 {len(missing)} 条(vcxproj 有、filters 没有):{RESET}")
        for tag, p in missing:
            print(f"  + <{tag}> {p}")
        print()
        if args.apply:
            block = "".join(
                render_entry(tag, p, TAG_TO_FILTER[tag]) for tag, p in missing
            )
            marker = "  <ItemGroup>\n"
            idx = filters_text.find(marker)
            if idx < 0:
                print(f"{RED}错误:filters 里找不到 <ItemGroup>,无法确定插入点{RESET}",
                      file=sys.stderr)
                return 2
            insert_at = idx + len(marker)
            header = (
                "    <!-- ★ 2026-10-09 补:以下条目 vcxproj 里早已注册,filters 一直漏着,\r\n"
                "         症状是它们在 VS 解决方案资源管理器里看不见(编译不受影响,\r\n"
                "         所以 ctest 全绿也照样漏)。由 scripts/fix_filters_entries.py 补齐。 -->\r\n"
            )
            filters_text = filters_text[:insert_at] + header + block + filters_text[insert_at:]
            changed = True
            print(f"{GREEN}✅ 已补 {len(missing)} 条{RESET}")
        else:
            print(f"  (dry-run,加 --apply 真写)")
        print()
    else:
        print(f"{GREEN}✅ 无缺失条目{RESET}\n")

    # ---- 报告残留 -------------------------------------------------------
    if stale:
        # ★ 残留里要再分两类,只删第一类。
        #   幽灵:磁盘上也没有了 —— 删掉是纯粹清理,没有争议。
        #   在册:磁盘上还有,只是 vcxproj 没单独登记 —— 这通常是本工程
        #         "Q_OBJECT 头只登记 <QtMoc> 不登记 <ClInclude>" 惯例造成的
        #         (实测 29 个头都是这样)。删了不会坏,但也没理由删,
        #         属于 Wqian 的口味,不由我替他定。
        ghosts = [(t, p) for t, p in stale if not (REPO_ROOT / "hmi" / p).exists()]
        alive = [(t, p) for t, p in stale if (REPO_ROOT / "hmi" / p).exists()]

        if ghosts:
            print(f"{YELLOW}filters 有 {len(ghosts)} 条幽灵条目(磁盘上也没了):{RESET}")
            for tag, p in ghosts:
                print(f"  - <{tag}> {p}")
            print()
        if alive:
            print(f"{YELLOW}filters 有 {len(alive)} 条 vcxproj 未登记、但磁盘上还在:{RESET}")
            print(f"  {YELLOW}(大概率是 Q_OBJECT 头只登记 <QtMoc> 的惯例,不是错){RESET}")
            for tag, p in alive:
                print(f"  ? <{tag}> {p}")
            print()

        if args.prune and ghosts:
            try:
                filters_text = remove_entries(filters_text, ghosts)
            except RuntimeError as e:
                print(f"{RED}错误:{e}{RESET}", file=sys.stderr)
                return 2
            changed = True
            print(f"{GREEN}✅ 已删 {len(ghosts)} 条幽灵条目{RESET}\n")
        elif ghosts:
            print("  (dry-run,加 --prune 真删)\n")
    else:
        print(f"{GREEN}✅ 无残留条目{RESET}\n")

    # ★ 退出码只看【能自动修的】两类差异(缺失 / 幽灵)。
    #   "vcxproj 未登记但磁盘还在" 是本工程的既有惯例,修不了也不该修,
    #   拿它算失败会让这个脚本永远红,久而久之就没人看它的输出了。
    if not changed:
        return 0 if not (missing or ghosts) else 1
    return 0

    # 写回前最后一次自查:XML 能不能解析。解析不了就不落盘,宁可什么都不做。
    try:
        ET.fromstring(filters_text)
    except ET.ParseError as e:
        print(f"{RED}错误:改完的 XML 解析失败({e})—— 已中止,文件未改{RESET}",
              file=sys.stderr)
        return 2

    write_filters(filters_text)
    return 0


if __name__ == "__main__":
    sys.exit(main())