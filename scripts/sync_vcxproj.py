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
    "hmi",               # ★ 见上:嵌套的旧 VS 构建目录副本(2026-10-24 落实)

    # ★ CMake 构建产物目录(2026-10-24 补)
    #
    #   hmi/build-menu/ 是 CMake 的构建树,里面有 76 个 .cpp/.h,
    #   几乎全是 moc_*.cpp / qrc_*.cpp / CompilerIdCXX.cpp 这些
    #   【生成物】。它不在 .gitignore 的 "build/" 里,但被 "build-*/" 兜住了
    #   (git check-ignore 实测:hmi/.gitignore:3:build-*/ 命中 hmi/build-menu)。
    #
    #   ★ 为什么这条必须补 ★
    #     漏了它,dry-run 会报 75 个"缺 ClCompile"的假差异,
    #     而 --apply 是【纯追加】—— 那 75 条 moc_*.cpp 会真的写进 vcxproj,
    #     后果是:MSBuild 编译 moc 文件(它们自己带 #include "moc_X.h",
    #     路径还指向 build-menu/)→ 编译不过;就算编过了,
    #     moc_*.cpp 里定义的 moc_XXX::staticMetaObject 会跟
    #     Qt 自己的 moc 产物【重复定义】。
    #     也就是说:--apply 一旦跑过,vcxproj 就废了。
    #     现有 EXCLUDE_DIRS 只写了字面量目录名,build-menu 这种
    #     "前缀式"的构建目录名一个都没覆盖。
    "build-menu",

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

# ★ 构建目录的【前缀规则】(2026-10-24 补)
#
#   EXCLUDE_DIRS 是字面量集合,只能挡固定名字。
#   但构建目录名是【前缀式】的:build/、build-menu/、build-debug/、
#   build_ninja/、cmake-build-debug/ …… 一个一个列,列不全那天就又中招。
#   .gitignore 里用的是同样两条规则:
#       build/  和  build-*/
#   这里的 EXCLUDE_PREFIXES 跟它对齐,免得"git 忽略了但脚本扫到了"。
EXCLUDE_PREFIXES = (
    "build",
    "cmake-build",
)

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

        # 跳过构建目录(前缀式):build/、build-menu/、cmake-build-*/ …
        # 见 EXCLUDE_PREFIXES 上方的说明。
        if any(part.startswith(EXCLUDE_PREFIXES) for part in parts):
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


# ---- Q_OBJECT 自动判断(2026-10-24 加)-------------------------------------

def strip_comments_and_strings(code: str) -> str:
    """把注释和字符串【内容】抹成空格,返回可安全做"找宏"的文本。

    ★ 为什么必须先抹注释 ★
    本项目的头文件里到处是"这个类没有 Q_OBJECT"之类的说明性注释。
    举例 `src/drivers/IPlcDriverProvider.h:16`:
        //   · 纯接口,无 Q_OBJECT,不进 moc —— 它不是信号槽参与方
    直接全文搜 "Q_OBJECT",这个头会被判成"需要 QtMoc" —— **判反了**。
    而如果因此给它加了 <QtMoc>,moc 会去生成 moc_IPlcDriverProvider.cpp,
    那个文件里没有任何可 moc 的类,多出来的是一份没用的产物。
    (不致命,但判据不能是错的 —— 错的判据比没有判据更坏,
     因为它会让"报告是绿的"这句话失去意义。)

    ★ 同样的坑在字符串里也有 ★
    注释里说"不含 Q_OBJECT"会骗人;字符串里写 Q_OBJECT 同样会骗人。
    所以字符串【内容】也抹掉 —— 只保留引号本身,免得打乱位置。

    抹成【空格】而不是删掉,是为了保持字符偏移不变,便于定位。
    手法跟 hmi/tests/tst_ThrottleNotAnError.cpp 里的 prepSource() 一样
    (那是 C++ 版,这里是 Python 版,逻辑一一对应)。
    """
    out = list(code)
    n = len(code)
    i = 0
    while i < n:
        c = code[i]
        # 行注释
        if c == '/' and i + 1 < n and code[i + 1] == '/':
            while i < n and code[i] != '\n':
                out[i] = ' '
                i += 1
        # 块注释
        elif c == '/' and i + 1 < n and code[i + 1] == '*':
            out[i] = ' '
            out[i + 1] = ' '
            i += 2
            while i < n:
                if code[i] == '*' and i + 1 < n and code[i + 1] == '/':
                    out[i] = ' '
                    out[i + 1] = ' '
                    i += 2
                    break
                out[i] = '\n' if code[i] == '\n' else ' '
                i += 1
        # 字符串 / 字符字面量:内容抹掉,引号保留
        elif c in ('"', "'"):
            quote = c
            i += 1
            while i < n:
                d = code[i]
                if d == '\\' and i + 1 < n:
                    out[i] = ' '
                    out[i + 1] = ' '
                    i += 2
                    continue
                if d == quote:
                    i += 1
                    break
                out[i] = '\n' if d == '\n' else ' '
                i += 1
        else:
            i += 1
    return ''.join(out)


# Q_OBJECT 作为宏出现:前面不能紧跟着标识符字符,否则 Q_OBJECT_FOO 之类会被误判。
_QOBJECT_RE = re.compile(r'(?<![A-Za-z0-9_])Q_OBJECT(?![A-Za-z0-9_])')


def header_has_qobject(path: Path) -> bool:
    """判断一个头文件里【真的有】Q_OBJECT 宏。

    判据:抹掉注释和字符串内容后,还能搜到独立的 Q_OBJECT 宏。

    ★ 什么算"真" ★
    - `Q_OBJECT` 出现在 class 定义里           → 真,需要 <QtMoc>
    - `Q_OBJECT` 只出现在注释里               → 假(本项目一大半是这种)
    - `Q_OBJECT` 只出现在字符串字面量里       → 假
    - `Q_OBJECT_FOO` / `MY_Q_OBJECT`          → 假(词边界挡住了)

    ★ 为什么要自动判 ★
    以前这里是"本脚本无法判断 Q_OBJECT,请 Wqian 手动确认",
    于是每次 dry-run 都刷 21 条"可能缺 QtMoc"—— 全是噪声:
    2026-10-24 逐个人工查过,**21 个头的代码里一个真 Q_OBJECT 都没有**。
    噪声让人习惯性忽略这一段,真缺的时候也就一起被忽略了。
    """
    try:
        raw = path.read_text(encoding="utf-8-sig", errors="replace")
    except OSError:
        return False
    return bool(_QOBJECT_RE.search(strip_comments_and_strings(raw)))


# ---- 解析 vcxproj ---------------------------------------------------------

def _extract_includes(text: str, tag: str) -> set[str]:
    """从 XML 文本里抽出 <tag Include="..." /> 的路径集合(反斜杠风格)。

    用文本正则而不是 ElementTree —— 跟 insert_include_into_vcxproj() 一致,
    纯文本读,不动 BOM / 行尾 / 注释。
    """
    return {m.group(1).replace("/", "\\")
            for m in re.finditer(r'<' + tag + r'\s+Include="([^"]+)"', text)}


def filters_needs_sync(vcxproj_path: Path, filters_path: Path) -> bool:
    """filters 跟 vcxproj 对不上吗?

    ★ 为什么要有这个判据 ★
      原来"filters 还需要同步"那句是无条件打印的,于是 filters 已经一致时
      它还在喊要同步,而紧接着跑 fix_filters_entries.py 却说"无缺失无残留"。
      两句矛盾的话里,错的那句会让人白跑一趟,还会让人开始不信这个脚本 ——
      跟 21 条噪声是同一个毛病:**说错话比不说话更贵**。

    判据(fix_filters_entries.py 的同一套):
      - filters 缺了 vcxproj 有的条目 → 要补
      - filters 多了 vcxproj 没有的条目 → 要删
    两种都算"要同步"。
    """
    try:
        v_text = vcxproj_path.read_text(encoding="utf-8-sig")
        f_text = filters_path.read_text(encoding="utf-8-sig")
    except OSError:
        # 读不了就当"要同步" —— 宁可多提醒一次,别漏。
        return True

    for tag in ("ClCompile", "ClInclude", "QtMoc"):
        in_v = _extract_includes(v_text, tag)
        in_f = _extract_includes(f_text, tag)
        if in_v != in_f:
            return True
    return False


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
    src_root: Path | None = None,
) -> dict[str, list[str]]:
    """计算磁盘和 vcxproj 的差异。

    返回:
        {
            "missing_cpp":    [...],  # 磁盘有,vcxproj 没 <ClCompile>
            "missing_h":      [...],  # 磁盘有,vcxproj 既没 <ClInclude> 也没 <QtMoc>
            "missing_moc":    [...],  # 【真缺】:磁盘有 .h,代码里确有 Q_OBJECT,但 vcxproj 没 <QtMoc>
            "no_moc_needed":  [...],  # 磁盘有 .h,代码里【没有】Q_OBJECT → 不需要 <QtMoc>(不用报)
            "stale_cpp":      [...],  # vcxproj 有,磁盘没了
            "stale_h":        [...],  # vcxproj 有,磁盘没了
        }

    关键逻辑:
    - <ClCompile> 只能由 <ClCompile> 满足,<QtMoc> 不替代
    - <ClInclude> 可以由 <ClInclude> 或 <QtMoc> 满足(QtMoc 是 ClInclude 的超集)
    - <QtMoc> 只有【代码里真的有 Q_OBJECT 宏】才需要(2026-10-24 起自动判断,
      见 header_has_qobject() —— 以前这里是"请 Wqian 手动确认",刷 21 条噪声)
    """
    # <QtMoc> 是 <ClInclude> 的超集:有 QtMoc 就算 ClInclude 已注册
    cl_include_eff = cl_include | qt_moc

    # 真缺 QtMoc 的 = 代码里确有 Q_OBJECT,但 vcxproj 没登记
    missing_moc: list[str] = []
    no_moc_needed: list[str] = []
    for rel in sorted(h_disk - qt_moc):
        if src_root is not None and header_has_qobject(src_root / rel):
            missing_moc.append(rel)
        else:
            no_moc_needed.append(rel)

    return {
        "missing_cpp":   sorted(cpp_disk - cl_compile),
        "missing_h":     sorted(h_disk - cl_include_eff),
        "missing_moc":   missing_moc,
        "no_moc_needed": no_moc_needed,
        "stale_cpp":     sorted(cl_compile - cpp_disk),
        "stale_h":       sorted((cl_include - h_disk) | (qt_moc - h_disk)),
    }


def print_report(diff: dict[str, list[str]]) -> None:
    """打印差异报告(彩色)。"""
    print(f"\n{BOLD}{'=' * 60}{RESET}")
    print(f"{BOLD}vcxproj 同步报告{RESET}")
    print(f"{BOLD}{'=' * 60}{RESET}\n")

    total_missing = (len(diff["missing_cpp"]) + len(diff["missing_h"])
                     + len(diff["missing_moc"]))
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
            print(f"{RED}{BOLD}[缺 QtMoc]{RESET} {len(diff['missing_moc'])} 个 .h 代码里确有 Q_OBJECT 但没登记:")
            print(f"  {RED}(这些是真缺 —— moc 不跑就没有 staticMetaObject,链接期 LNK2001){RESET}")
            for p in diff["missing_moc"]:
                print(f"  {RED}+{RESET} {p}")
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
    if diff["no_moc_needed"]:
        # 这条【不】算缺失,也不算残留 —— 只是记一笔"这些查过了,确实不需要"。
        # 以前这里刷的是"可能缺 QtMoc,请手动确认",21 条全是噪声,
        # 每次都让人习惯性跳过这一段 —— 真缺的时候也就一起被跳过了。
        print(f"  (另有 {len(diff['no_moc_needed'])} 个 .h 代码里没有 Q_OBJECT,"
              f"确认过【不需要】<QtMoc> —— 这是结论,不是待办)")
    print()


# ---- 写入 vcxproj ---------------------------------------------------------

def insert_include_into_vcxproj(
    vcxproj_path: Path, missing_cpp: list[str], missing_h: list[str],
    missing_moc: list[str] | None = None,
) -> None:
    """把缺失的 <ClCompile>/<ClInclude>/<QtMoc> 插入 vcxproj。

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

    ★ missing_moc(2026-10-24 加)★
      以前这个脚本不写 <QtMoc> —— 理由是"判断不了 Q_OBJECT"。
      现在能判断了(见 header_has_qobject),所以真缺的要真补。
      只补 <QtMoc> 不补 <ClInclude> 是不行的:本项目 76 个 Q_OBJECT 头
      在 vcxproj 里【两个都登记】(实测 76 ↔ 76 一一对应),
      filters 里也是 76 ↔ 76 —— 只补一半就成了"登记只做前一半"那类不一致。
    """
    missing_moc = missing_moc or []
    if not missing_cpp and not missing_h and not missing_moc:
        return

    text = vcxproj_path.read_text(encoding="utf-8-sig")

    # ★ 已有的条目要【分标签】记,不能混成一个池子(2026-10-24 修)★
    #
    #   原来是一个 `existing` 集合,三种标签的路径全塞进去。
    #   后果:一个头已登记为 <ClInclude>、但缺 <QtMoc>(而它代码里真有
    #   Q_OBJECT)时,QtMoc 循环里的 `if p in existing: continue` 会命中 ——
    #   **<QtMoc> 永远写不进去**。
    #   而 compute_diff 那头【是】会报出来的(它按 qt_moc 集合判),
    #   于是就成了本项目最危险的那一类:**报告说有,--apply 却不干**。
    #   实测复现:造一个只登记 <ClInclude Include="HasObj.h" /> 的 vcxproj,
    #   missing_moc 报出 HasObj.h,--apply 之后文件里【还是没有 <QtMoc>】。
    #
    #   修法:三个标签各记各的,判重只看【同一个标签】。
    existing_by_tag: dict[str, set[str]] = {tag: set() for tag in
                                            ("ClCompile", "ClInclude", "QtMoc")}
    for tag in existing_by_tag:
        for m in re.finditer(r"<" + tag + r'\s+Include="([^"]+)"', text):
            existing_by_tag[tag].add(m.group(1).replace("/", "\\"))
    existing = existing_by_tag["ClCompile"] | existing_by_tag["ClInclude"] \
        | existing_by_tag["QtMoc"]

    lines = ['  <ItemGroup>']
    # 本轮已经写进 lines 的路径 —— 防止同一个头被写两遍
    # (missing_h 和 missing_moc 可能指向同一个文件)
    written: set[str] = set()
    for cpp in missing_cpp:
        p = cpp.replace("/", "\\")
        if p in existing_by_tag["ClCompile"]:
            continue
        lines.append(f'    <ClCompile Include="{p}" />')
        written.add(p)
    for h in missing_h:
        p = h.replace("/", "\\")
        if p in existing_by_tag["ClInclude"] or p in written:
            continue
        lines.append(f'    <ClInclude Include="{p}" />')
        written.add(p)
    for h in missing_moc:
        p = h.replace("/", "\\")
        if p in existing_by_tag["QtMoc"]:
            # vcxproj 里已经登记过 <QtMoc>(compute_diff 已滤过,这里是双保险)
            continue
        # ★ 顺序:QtMoc 在 ClInclude 【前面】——
        #   跟仓库里现成的写法一致(例:P146-147 的 QtMoc 在 ClCompile 之前,
        #   P214-215 的 QtMoc 在 ClInclude 之前)。
        lines.append(f'    <QtMoc Include="{p}" />')
        # ★ <ClInclude> 只在还没写过时才补 ★
        #   同一个头可能同时落在 missing_h 和 missing_moc 两个列表里
        #   (它没登记,而代码里又有 Q_OBJECT)—— 无条件补就会写出两条
        #   <ClInclude>,MSBuild 报"重复项",项目直接打不开。
        #   实测踩到过:ProbeReal.h 被写了两次(462 行和 464 行)。
        #   判据:这一轮已经写过 <ClInclude> 的,就不再写第二遍。
        #   ★ 这里的判重看的是【ClInclude 标签】,不是混合池 ——
        #   已经登记过 <ClInclude> 的头,这里【不该再写一条】,
        #   但 <QtMoc> 该写还是要写(见上面 existing_by_tag 那段注释)。
        if p not in existing_by_tag["ClInclude"] and p not in written:
            lines.append(f'    <ClInclude Include="{p}" />')
            written.add(p)
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
    diff = compute_diff(cpp_disk, h_disk, cl_compile, cl_include, qt_moc,
                         src_root=args.src)

    # 4. 打印报告
    print(f"{CYAN}源码根:{RESET}  {args.src}")
    print(f"{CYAN}vcxproj:{RESET} {args.vcxproj}")
    print(f"{CYAN}filters:{RESET} {args.filters}")
    print(f"{CYAN}模式:{RESET}    {'--apply (会写文件)' if args.apply else 'dry-run (只报告)'}")
    print_report(diff)

    # 5. --apply 时写文件
    if args.apply:
        if diff["missing_cpp"] or diff["missing_h"] or diff["missing_moc"]:
            try:
                insert_include_into_vcxproj(
                    args.vcxproj,
                    diff["missing_cpp"],
                    diff["missing_h"],
                    diff["missing_moc"],
                )
                print(f"{GREEN}✅ vcxproj 已更新:{RESET}")
                print(f"  + <ClCompile> × {len(diff['missing_cpp'])}")
                print(f"  + <ClInclude> × {len(diff['missing_h'])}")
                print(f"  + <QtMoc>     × {len(diff['missing_moc'])}"
                      f"(每个同时补 <ClInclude>,跟仓库里 76 个 Q_OBJECT 头的写法一致)")
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

        # ★ filters 的说明改了(2026-10-24)★
        # 以前写的是"本脚本不动 filters,请手工同步" —— 那正是
        # 2026-10-09 那次"登记只做前一半"的成因:vcxproj 补了,filters 忘了,
        # 结果这些文件在 VS 资源管理器里【看不见】,而编译不受影响,
        # 所以 ctest 全绿也照样漏。
        #
        # 现在 filters 的补齐由 fix_filters_entries.py 负责
        # (它以 vcxproj 为基准,能把每个 <QtMoc>/<ClInclude> 都镜像过去)。
        # 缺的那一步是"记得跑它" —— 所以这里直接把命令打出来。
        #
        # ★ 但只在 filters 【真的】需要同步时才提示(2026-10-24 补)★
        #   原来这条提示是无条件打印的,于是 filters 已经一致的时候,
        #   它还在喊"⚠ filters 还需要同步" —— 而紧接着跑
        #   fix_filters_entries.py 却说"✅ 无缺失条目 / ✅ 无残留条目"。
        #   两句矛盾的话里,错的那句会让人白跑一趟,还会让人开始
        #   不信这个脚本的输出 —— 跟 21 条噪声同一个毛病。
        #   判据:filters 里缺 vcxproj 有的条目,或者 filters 里多了
        #   vcxproj 没有的条目(两种都算"要同步")。
        if filters_needs_sync(args.vcxproj, args.filters):
            print(f"{YELLOW}⚠ filters 还需要同步 —— 跑这个:{RESET}")
            print(f"  python scripts/fix_filters_entries.py --apply")
            print(f"  (它以 vcxproj 为基准,把每个 <QtMoc>/<ClInclude> 镜像到 filters)")
            print()
        else:
            print(f"{GREEN}✅ filters 与 vcxproj 一致,不用跑 fix_filters_entries.py{RESET}")
            print()

        return 1 if (diff["stale_cpp"] or diff["stale_h"]) else 0

    # dry-run 完成
    total_diff = (len(diff["missing_cpp"]) + len(diff["missing_h"])
                  + len(diff["missing_moc"])
                  + len(diff["stale_cpp"]) + len(diff["stale_h"]))
    return 0 if total_diff == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
