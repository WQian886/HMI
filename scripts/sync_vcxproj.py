#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sync_vcxproj.py — 自动同步 hmi/hmi/hmi.vcxproj 和 hmi.vcxproj.filters

按 docs/现状.md 约定 3:每次新增 .cpp/.h 都必须同步 vcxproj + filters。
手动维护容易漏,这个脚本扫描源码目录,跟 vcxproj 对比,报告差异。
默认 dry-run(只打印报告),--apply 才会真正改文件。

用法
----
    python scripts/sync_vcxproj.py                 # 默认 dry-run,打印报告
    python scripts/sync_vcxproj.py --apply         # 实际写文件
    python scripts/sync_vcxproj.py --src <dir>     # 指定源码根(默认 hmi/hmi)
    python scripts/sync_vcxproj.py --vcxproj <p>   # 指定 vcxproj(默认 hmi/hmi/hmi.vcxproj)
    python scripts/sync_vcxproj.py --filters <p>   # 指定 filters(默认 hmi/hmi/hmi.vcxproj.filters)
    python scripts/sync_vcxproj.py --help          # 帮助

退出码
------
    0  无差异 / dry-run 完成
    1  --apply 写入后仍存在差异(可能 filters 没同步)
    2  参数错误 / 文件不存在 / XML 解析失败

注意
----
- 跳过构建产物目录:x64/、out/、tests/、tests/build/
- 跳过非源码目录:installer/、docs/、.github/、.vs/、.git/
- 路径格式用反斜杠(vcxproj 风格),跟现有 vcxproj 保持一致
- .cpp → <ClCompile>;.h → <ClInclude>
- <QtMoc> 标记无法从源码判断 Q_OBJECT,本脚本不自动加,只报告"哪些 .h 缺 <QtMoc>"
- ui/qrc/iss/json 等非 C++ 源文件不在本脚本职责范围

作者:Wqian + AI 协作 / 创建:2026-09-10
"""
from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Iterable

# ---- 常量 ----------------------------------------------------------------

# vcxproj 默认路径(相对仓库根)
# 仓库结构:
#   E:\qianwie\hmi\                  ← 仓库根
#   ├── README.md / docs/ / scripts/
#   └── hmi/                         ← VS 工程根(单层,不是嵌套)
#       ├── main.cpp / hmi.cpp / *.cpp / *.h
#       ├── src/ installer/ tests/ docs/
#       ├── hmi.slnx / hmi.vcxproj / hmi.vcxproj.filters
#       ├── x64/ out/                ← 构建产物(跳过)
#       └── hmi/                     ← 旧 VS 构建目录副本(跳过)
DEFAULT_REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SRC_DIR = DEFAULT_REPO_ROOT / "hmi"
DEFAULT_VCXPROJ = DEFAULT_SRC_DIR / "hmi.vcxproj"
DEFAULT_FILTERS = DEFAULT_SRC_DIR / "hmi.vcxproj.filters"

# 要跳过的目录(相对源码根)
EXCLUDE_DIRS = {
    "x64",
    "out",
    "tests",            # 有自己的 CMakeLists.txt
    "tests/build",      # 兜底(防有人把 tests 改成 tests/build)
    "installer",        # Inno Setup 脚本,非 C++
    "docs",             # 文档
    ".github",          # CI 配置
    ".vs",              # VS IDE 缓存
    ".git",
    "__pycache__",
    "node_modules",
    # ⚠ 注意:hmi/ 下还有个嵌套的 hmi/ 目录,是旧的 VS 构建目录副本
    # 里面有 x64/ 子目录(moc_*.cpp 等),如果脚本在 hmi/ 根扫,
    # 会扫到 hmi/hmi/x64/ 里的 moc_*.cpp,需要排除掉 hmi/hmi/ 本身
    # 简单做法:用 rglob + 跳过包含 "x64" 路径的(已含)即可
}

# vcxproj 命名空间(MSBuild XML)
NS = "http://schemas.microsoft.com/developer/msbuild/2003"

# 报告用的颜色(Windows Terminal / PowerShell 支持 ANSI)
GREEN = "\033[32m"
YELLOW = "\033[33m"
RED = "\033[31m"
CYAN = "\033[36m"
BOLD = "\033[1m"
RESET = "\033[0m"


# ---- 扫描源码 ------------------------------------------------------------

def scan_source_files(src_root: Path) -> tuple[set[str], set[str]]:
    """扫描 src_root 下所有 .cpp 和 .h,返回 (cpp_set, h_set)。

    返回的路径是 vcxproj 风格(反斜杠),相对 src_root。
    """
    cpp_set: set[str] = set()
    h_set: set[str] = set()

    if not src_root.exists():
        raise FileNotFoundError(f"源码根不存在: {src_root}")

    for path in src_root.rglob("*"):
        if not path.is_file():
            continue

        # 跳过 EXCLUDE_DIRS 中的任意父目录
        rel = path.relative_to(src_root)
        parts = rel.parts
        if any(part in EXCLUDE_DIRS for part in parts):
            continue

        # 跳过隐藏文件(以 . 开头,除 .clang-tidy 等是配置不是源)
        if any(part.startswith(".") and part not in {".clang-tidy", ".clang-format"}
               for part in parts):
            continue

        # 跳过 vcxproj/filters 自身
        if path.suffix.lower() in {".vcxproj", ".filters"}:
            continue

        suffix = path.suffix.lower()
        if suffix == ".cpp":
            cpp_set.add(str(rel).replace("/", "\\"))
        elif suffix == ".h":
            h_set.add(str(rel).replace("/", "\\"))

    return cpp_set, h_set


# ---- 解析 vcxproj ---------------------------------------------------------

def parse_vcxproj_includes(vcxproj_path: Path) -> tuple[set[str], set[str], set[str]]:
    """解析 vcxproj,返回 (cl_compile_set, cl_include_set, qt_moc_set)。

    每个集合里的路径是 vcxproj 风格(反斜杠)。
    """
    if not vcxproj_path.exists():
        raise FileNotFoundError(f"vcxproj 不存在: {vcxproj_path}")

    tree = ET.parse(vcxproj_path)
    root = tree.getroot()

    cl_compile: set[str] = set()
    cl_include: set[str] = set()
    qt_moc: set[str] = set()

    for tag in ("ClCompile", "ClInclude", "QtMoc"):
        for elem in root.iter(f"{{{NS}}}{tag}"):
            include = elem.get("Include")
            if not include:
                continue
            normalized = include.replace("/", "\\")
            if tag == "ClCompile":
                cl_compile.add(normalized)
            elif tag == "ClInclude":
                cl_include.add(normalized)
            elif tag == "QtMoc":
                qt_moc.add(normalized)

    return cl_compile, cl_include, qt_moc


# ---- 报告与差异计算 -------------------------------------------------------

def compute_diff(
    cpp_disk: set[str], h_disk: set[str],
    cl_compile: set[str], cl_include: set[str], qt_moc: set[str],
) -> dict[str, list[str]]:
    """计算磁盘和 vcxproj 的差异。

    返回:
        {
            "missing_cpp": [...],   # 磁盘有,vcxproj 没 <ClCompile>
            "missing_h":   [...],   # 磁盘有,vcxproj 既没 <ClInclude> 也没 <QtMoc>
            "missing_moc": [...],   # 磁盘有 .h,vcxproj 没 <QtMoc>(提示:有 Q_OBJECT 才需要)
            "stale_cpp":   [...],   # vcxproj 有,磁盘没了
            "stale_h":     [...],   # vcxproj 有,磁盘没了
        }

    关键逻辑:
    - <ClCompile> 只能由 <ClCompile> 满足,<QtMoc> 不替代
    - <ClInclude> 可以由 <ClInclude> 或 <QtMoc> 满足(QtMoc 是 ClInclude 的超集)
    - <QtMoc> 只能由 <QtMoc> 满足(无法自动判断 Q_OBJECT,只报告不自动加)
    """
    # <QtMoc> 是 <ClInclude> 的超集:有 QtMoc 就算 ClInclude 已注册
    cl_include_eff = cl_include | qt_moc

    return {
        "missing_cpp": sorted(cpp_disk - cl_compile),
        "missing_h":   sorted(h_disk - cl_include_eff),
        "missing_moc": sorted(h_disk - qt_moc),  # 只是"可能需要 QtMoc"的提示
        "stale_cpp":   sorted(cl_compile - cpp_disk),
        "stale_h":     sorted((cl_include - h_disk) | (qt_moc - h_disk)),
    }


def print_report(diff: dict[str, list[str]]) -> None:
    """打印差异报告(彩色)。"""
    print(f"\n{BOLD}{'=' * 60}{RESET}")
    print(f"{BOLD}vcxproj 同步报告{RESET}")
    print(f"{BOLD}{'=' * 60}{RESET}\n")

    total_missing = len(diff["missing_cpp"]) + len(diff["missing_h"])
    total_stale = len(diff["stale_cpp"]) + len(diff["stale_h"])

    if total_missing == 0 and total_stale == 0:
        print(f"{GREEN}✅ vcxproj 与磁盘源码完全一致,无需同步{RESET}\n")
    else:
        if diff["missing_cpp"]:
            print(f"{RED}{BOLD}[缺 ClCompile]{RESET} {len(diff['missing_cpp'])} 个 .cpp 未注册:")
            for p in diff["missing_cpp"]:
                print(f"  {RED}+{RESET} {p}")
            print()

        if diff["missing_h"]:
            print(f"{RED}{BOLD}[缺 ClInclude]{RESET} {len(diff['missing_h'])} 个 .h 未注册:")
            for p in diff["missing_h"]:
                print(f"  {RED}+{RESET} {p}")
            print()

        if diff["missing_moc"]:
            # 注意:这只是"可能需要 QtMoc"的提示,不一定真有 Q_OBJECT
            has_real_missing = [p for p in diff["missing_moc"]
                                if p not in diff["missing_h"]]
            if has_real_missing:
                print(f"{YELLOW}{BOLD}[可能缺 QtMoc]{RESET} {len(has_real_missing)} 个 .h 没有 <QtMoc> 标记:")
                print(f"  {YELLOW}(本脚本无法判断 Q_OBJECT,请 Wqian 手动确认){RESET}")
                for p in has_real_missing:
                    print(f"  {YELLOW}?{RESET} {p}")
                print()

        if diff["stale_cpp"]:
            print(f"{CYAN}{BOLD}[残留 ClCompile]{RESET} {len(diff['stale_cpp'])} 个 .cpp 在 vcxproj 里但磁盘已删:")
            for p in diff["stale_cpp"]:
                print(f"  {CYAN}-{RESET} {p}")
            print()

        if diff["stale_h"]:
            print(f"{CYAN}{BOLD}[残留 ClInclude]{RESET} {len(diff['stale_h'])} 个 .h 在 vcxproj 里但磁盘已删:")
            for p in diff["stale_h"]:
                print(f"  {CYAN}-{RESET} {p}")
            print()

    # 总结
    print(f"{BOLD}汇总{RESET}: 缺 {total_missing} 个,残留 {total_stale} 个")
    if diff["missing_moc"]:
        print(f"  (QtMoc 提示 {len(diff['missing_moc'])} 个,需 Wqian 手动判断 Q_OBJECT)")
    print()


# ---- 写入 vcxproj ---------------------------------------------------------

def insert_include_into_vcxproj(
    vcxproj_path: Path, missing_cpp: list[str], missing_h: list[str],
) -> None:
    """把缺失的 <ClCompile>/<ClInclude> 插入 vcxproj。

    简化策略:把所有新增项追加到第一个 <ItemGroup> 里(跟现有风格混在一起即可)。
    """
    if not missing_cpp and not missing_h:
        return

    tree = ET.parse(vcxproj_path)
    root = tree.getroot()

    # 找第一个 ItemGroup(主代码 ItemGroup),追加到末尾
    # vcxproj 通常只有一个主 ItemGroup,其他是配置 / QtMoc
    first_itemgroup = None
    for elem in root.iter(f"{{{NS}}}ItemGroup"):
        # 跳过 ProjectConfigurations / QtSettings / ItemDefinitionGroup 类的
        if elem.find(f"{{{NS}}}ClCompile") is not None or elem.find(f"{{{NS}}}ClInclude") is not None:
            first_itemgroup = elem
            break

    if first_itemgroup is None:
        # 找不到就建一个新的
        first_itemgroup = ET.SubElement(root, f"{{{NS}}}ItemGroup")

    for cpp in missing_cpp:
        ET.SubElement(first_itemgroup, f"{{{NS}}}ClCompile",
                      {"Include": cpp.replace("/", "\\")})
    for h in missing_h:
        ET.SubElement(first_itemgroup, f"{{{NS}}}ClInclude",
                      {"Include": h.replace("/", "\\")})

    # 写回(保留 XML 声明 + UTF-8)
    tree.write(vcxproj_path, encoding="utf-8", xml_declaration=True)


# ---- 主流程 ---------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="扫描源码目录,对比 vcxproj,报告/同步差异。",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="默认 dry-run,--apply 才写文件。",
    )
    parser.add_argument(
        "--src", type=Path, default=DEFAULT_SRC_DIR,
        help=f"源码根目录(默认 {DEFAULT_SRC_DIR.relative_to(DEFAULT_REPO_ROOT)})",
    )
    parser.add_argument(
        "--vcxproj", type=Path, default=DEFAULT_VCXPROJ,
        help=f"vcxproj 路径(默认 {DEFAULT_VCXPROJ.relative_to(DEFAULT_REPO_ROOT)})",
    )
    parser.add_argument(
        "--filters", type=Path, default=DEFAULT_FILTERS,
        help=f"filters 路径(默认 {DEFAULT_FILTERS.relative_to(DEFAULT_REPO_ROOT)})",
    )
    parser.add_argument(
        "--apply", action="store_true",
        help="实际写文件(默认 dry-run,只打报告)",
    )
    args = parser.parse_args(argv)

    # 路径校验
    if not args.src.exists():
        print(f"{RED}错误: 源码根不存在 {args.src}{RESET}", file=sys.stderr)
        return 2
    if not args.vcxproj.exists():
        print(f"{RED}错误: vcxproj 不存在 {args.vcxproj}{RESET}", file=sys.stderr)
        return 2

    # 1. 扫描磁盘
    try:
        cpp_disk, h_disk = scan_source_files(args.src)
    except FileNotFoundError as e:
        print(f"{RED}错误: {e}{RESET}", file=sys.stderr)
        return 2

    # 2. 解析 vcxproj
    try:
        cl_compile, cl_include, qt_moc = parse_vcxproj_includes(args.vcxproj)
    except (FileNotFoundError, ET.ParseError) as e:
        print(f"{RED}错误: 解析 vcxproj 失败: {e}{RESET}", file=sys.stderr)
        return 2

    # 3. 计算差异
    diff = compute_diff(cpp_disk, h_disk, cl_compile, cl_include, qt_moc)

    # 4. 打印报告
    print(f"{CYAN}源码根:{RESET}  {args.src}")
    print(f"{CYAN}vcxproj:{RESET} {args.vcxproj}")
    print(f"{CYAN}filters:{RESET} {args.filters}")
    print(f"{CYAN}模式:{RESET}    {'--apply (会写文件)' if args.apply else 'dry-run (只报告)'}")
    print_report(diff)

    # 5. --apply 时写文件
    if args.apply:
        if diff["missing_cpp"] or diff["missing_h"]:
            try:
                insert_include_into_vcxproj(
                    args.vcxproj,
                    diff["missing_cpp"],
                    diff["missing_h"],
                )
                print(f"{GREEN}✅ vcxproj 已更新:{RESET}")
                print(f"  + <ClCompile> × {len(diff['missing_cpp'])}")
                print(f"  + <ClInclude> × {len(diff['missing_h'])}")
                print()
            except ET.ParseError as e:
                print(f"{RED}错误: 写回 vcxproj 失败: {e}{RESET}", file=sys.stderr)
                return 2

        # 残留项不自动删(Wqian 手工删更安全)
        if diff["stale_cpp"] or diff["stale_h"]:
            print(f"{YELLOW}⚠ 残留项未自动删,请 Wqian 手工从 vcxproj 删除:{RESET}")
            for p in diff["stale_cpp"]:
                print(f"  - <ClCompile Include=\"{p}\" />")
            for p in diff["stale_h"]:
                print(f"  - <ClInclude Include=\"{p}\" />")
            print()

        # 提示 filters 没动
        print(f"{YELLOW}⚠ 本脚本不动 filters(逻辑太复杂,留 Wqian 手工同步){RESET}")
        print(f"  需要同步的项已在上方 [缺 ClCompile] / [缺 ClInclude] 列出")
        print(f"  Wqian 在 {args.filters.name} 的对应 <Filter> 段加 <ClCompile>/<ClInclude> 子项")
        print()

        return 1 if (diff["stale_cpp"] or diff["stale_h"]) else 0

    # dry-run 完成
    total_diff = (len(diff["missing_cpp"]) + len(diff["missing_h"])
                  + len(diff["stale_cpp"]) + len(diff["stale_h"]))
    return 0 if total_diff == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
