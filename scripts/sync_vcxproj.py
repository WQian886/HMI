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
import re
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

    # ★ QCustomPlot 的【示例程序】(2026-10-09 加)
    #
    #   【为什么不直接排除 "3rdparty" —— 那会闯大祸】
    #     待办清单里原来写的是"EXCLUDE_DIRS 应跳过 3rdparty/"。
    #     照做会【直接编译失败】:3rdparty 下只有 qcustomplot 真的有源文件,
    #     而 qcustomplot/qcustomplot.cpp 是【必须】编进 hmi 的
    #     (CMakeLists.txt:37 也在编它,hmi.vcxproj 也已收录)。
    #     把 3rdparty 整个排掉,等于把唯一要编的那个也排了。
    #
    #   【真正要排除的是什么】
    #     3rdparty/qcustomplot/examples/ 下有 5 个示例程序,
    #     实测共 21 个源文件、其中【5 个定义了 main()】
    #     (axis-tags / interactions / plots /
    #      scrollbar-axis-range-control / text-document-integration 各一个)。
    #     hmi 自己已经有一个 main.cpp —— 一旦 --apply 把这 5 个也塞进 vcxproj,
    #     链接期直接 "multiple definition of main",而且这个错【只在链接期爆】,
    #     编译阶段一路绿灯。
    #   所以排除的是 examples/ 这个子目录,不是 3rdparty/。
    "examples",
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

    ★★ 这里【不用 ElementTree 写回】,改成按文本做纯追加。
      原因不是 BOM 一条,是三条,全都实测过:

      1. BOM + 行尾被毁。原来的 tree.write() 写完是 60,63,120(即 "<?x"),
         403 行 CRLF 变成 0 行。
      2. 整个文件被重排。git diff 395 增 403 删,连带着 hmi.vcxproj.filters
         那边立刻报出 222 条"假的不一致",排查成本极高(踩过一次,git checkout 撤销)。
      3. ★ 名字空间被打上 ns0: 前缀,而且【注释全被丢掉】。
         实测输出是 <ns0:Project xmlns:ns0="..."> 而不是 <Project xmlns="...">,
         文件从 23814 字节缩到 20103 —— 少掉的 3.7 KB 全是 <!-- --> 注释。
         要消掉这条得 register_namespace + TreeBuilder(insert_comments=True),
         但那还是在跟库的默认行为赌。

      追加一个 <ItemGroup> 在 </Project> 之前,MSBuild 完全接受(ItemGroup 在
      Project 下放哪都行),而且是【纯增行、零删行】:git diff 里只有新加的那几行,
      任何意外都能一眼看出来。真的 diff 应该是"加了两条",不是"重排了四百行"。
    """
    if not missing_cpp and not missing_h:
        return

    text = vcxproj_path.read_text(encoding="utf-8-sig")

    # 已有的 Include 一律跳过 —— 双保险,正常情况下 compute_diff 已经滤掉了。
    existing: set[str] = set()
    for tag in ("ClCompile", "ClInclude"):
        for m in re.finditer(r"<" + tag + r'\s+Include="([^"]+)"', text):
            existing.add(m.group(1))

    lines = ['  <ItemGroup>']
    for cpp in missing_cpp:
        p = cpp.replace("/", "\\")
        if p in existing:
            continue
        lines.append(f'    <ClCompile Include="{p}" />')
    for h in missing_h:
        p = h.replace("/", "\\")
        if p in existing:
            continue
        lines.append(f'    <ClInclude Include="{p}" />')
    lines.append("  </ItemGroup>")

    if len(lines) == 2:      # 全部都已在册,没真新增
        return

    block = "\r\n".join(lines) + "\r\n"
    idx = text.rfind("</Project>")
    if idx < 0:
        raise ET.ParseError("vcxproj 里找不到 </Project>,无法确定插入点")

    new_text = text[:idx] + "  <!-- ★ sync_vcxproj.py 追加 -->\r\n" + block + text[idx:]

    # 落盘前自检:解析不了就不写。宁可什么都不做。
    ET.fromstring(new_text)

    with open(vcxproj_path, "wb") as f:
        f.write(b"\xef\xbb\xbf")               # UTF-8 BOM
        f.write(new_text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))


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
