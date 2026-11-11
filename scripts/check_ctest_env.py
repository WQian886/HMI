#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_ctest_env.py —— ctest 目标的【运行环境】判据(2026-11-11 加,G-31-e)

为什么需要它
------------
  以前 45 个测试各写一行 `qputenv("QT_QPA_PLATFORM","offscreen")`,
  全都写在 initTestCase()/init() 里 —— 而那两处是 QTest::qExec 内部调的,
  也就是在 **QApplication 构造【之后】** ⇒ 那 45 行【全是无效的】。

  实测(2026-11-11):tst_BarControl(第 19 行就写着那行)在
  QT_QPA_PLATFORM=bogusplatform 下【挂死 20 秒】
  (QPA 插件加载失败后弹对话框等输入),offscreen 下才 EXIT=0
  —— 证明那行什么也没做。

  ⇒ 真正的开关在 CMake 的 ENVIRONMENT 属性上(进程起之前就有)。
  ⇒ 但【钉在 CMake 里】还不够:CMakeLists.txt 里那几行也可能被谁删掉,
    而删掉之后【没有一条判据会红】—— 删守卫的人正是靠"没有判据"才敢删。

  所以这条判据查两处:
    ① 源  (hmi/tests/CMakeLists.txt)—— 那条"给【所有】目标钉死"的循环在不在
    ② 果  (构建目录里的 CTestTestfile.cmake)—— 【配置后的真实结果】
       每个 ctest 目标是不是真的带着 QT_QPA_PLATFORM=offscreen 和 TIMEOUT
    ★ 查"果"而不是只查"源",是因为 CMake 语义多一层:
      属性写在循环里,循环写在文件末尾 —— 只要有一个 add_test 写在
      循环【之后】,那个目标就漏了。查源看不出来,查生成的 CTestTestfile
      一眼就看见。

用法
----
    python scripts/check_ctest_env.py --source <CMakeLists.txt> --generated <CTestTestfile.cmake>
退出码:0 全过 / 1 有断言没过
"""
import re
import sys
from pathlib import Path

fails: list[str] = []


def check(label: str, cond: bool, detail: str = "") -> None:
    print("  %s %s %s" % ("OK  " if cond else "FAIL", label, detail))
    if not cond:
        fails.append(label)


def strip_cmake_comments(text: str) -> str:
    """去掉 CMake 源码里的注释,只留代码。

    ★ 为什么要抹注释 ★
      这是本轮负向对照真抓出来的一个自伤:
      判据查的是"那段代码在不在",而**注释里也会出现同样的字符串** ——
      我给这个循环写的说明里就有 `ENVIRONMENT "QT_QPA_PLATFORM=offscreen"`
      这一行字面量。
      ⇒ 不抹注释的话:把代码里那两行删掉、注释里还留着 ⇒ 判据照样绿
        ⇒ 负向对照 M3 咬不住(实测:它就是这么放过去的)。
      这跟 sync_vcxproj.py 抹注释/字符串是同一个手法,同一个道理:
      **显示给人看的东西不许参与判定。**
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "#":
            # 本仓库没有用括号注释 #[[ ]] ⇒ 只处理行注释
            while i < n and text[i] != "\n":
                i += 1
        elif ch == '"':
            # 字符串:原样保留(字符串里的 # 不是注释)
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == '"':
                    j += 1
                    break
                j += 1
            out.append(text[i:j])
            i = j
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def parse_args(argv):
    src = gen = None
    i = 0
    while i < len(argv):
        if argv[i] == "--source":
            src = argv[i + 1]
            i += 2
        elif argv[i] == "--generated":
            gen = argv[i + 1]
            i += 2
        else:
            print("不认识的参数:%s" % argv[i])
            return None, None
    return src, gen


# 生成文件里的两行长这样:
#   add_test([=[tst_Variable]=] "E:/.../tst_Variable.exe")
#   set_tests_properties([=[tst_Variable]=] PROPERTIES ENVIRONMENT "..." TIMEOUT 600 ...)
RE_ADD = re.compile(r"add_test\(\s*\[=\[(.*?)\]=\]", re.S)
# 判据认的是 ENVIRONMENT "..." 这个【赋值】形状 —— 见 main() 里那段的说明
RE_ENV_ASSIGN = re.compile(r'ENVIRONMENT\s+"QT_QPA_PLATFORM=offscreen"')
# ★ 属性全在【一行】里 ⇒ 用 [^\n]* 而不是 .*
#   (加 re.S 的 .* 会一路吃到文件末尾 ⇒ finditer 只匹配得到 1 条,
#    116 个目标里 115 个会被当成"没有属性行" —— 判据自己成了假红。)
RE_PROP = re.compile(r"set_tests_properties\(\s*\[=\[(.*?)\]=\]\s*PROPERTIES\s+([^\n]*)")


def main() -> int:
    src, gen = parse_args(sys.argv[1:])
    if not src or not gen:
        print("用法:check_ctest_env.py --source <CMakeLists.txt> "
              "--generated <CTestTestfile.cmake>")
        return 1

    sp = Path(src)
    gp = Path(gen)

    # ================= ① 源:那条"给所有目标钉死"的循环 =================
    print("=== ① 源:%s ===" % sp)
    if not sp.is_file():
        print("FAIL 源文件不存在:%s" % sp)
        return 1
    cm = sp.read_text(encoding="utf-8")
    # ★ 查【代码】,不查注释 —— 见 strip_cmake_comments 的说明
    code = strip_cmake_comments(cm)
    # ★ 判定匹配的是 ENVIRONMENT "..." 这个【赋值】形状,不是裸字符串 ——
    #   裸字符串会撞上两种"给人看的东西":
    #     ① 注释里的说明(已由 strip_cmake_comments 抹掉)
    #     ② message(STATUS "... 钉死 QT_QPA_PLATFORM=offscreen ...")
    #        —— 那是给构建日志看的一句话(实测:守卫删光之后,
    #        判据因为这一行还在,照样报绿 ⇒ 负向对照 M3 咬不住)
    #   ⇒ 同一个道理:**显示给人看的东西不许参与判定**。
    has_env_assign = RE_ENV_ASSIGN.search(code) is not None
    has_loop = ("get_property(" in code and "DIRECTORY PROPERTY TESTS" in code
                and "set_tests_properties(" in code
                and has_env_assign)
    check("CMakeLists.txt 里有『给所有 ctest 目标钉死环境』的循环", has_loop,
          "(get_property(DIRECTORY PROPERTY TESTS) + foreach + set_tests_properties)")
    check("环境变量钉的是 QT_QPA_PLATFORM=offscreen",
          has_env_assign,
          "(必须是 offscreen —— windows 平台会弹真窗口;"
          " 判定看的是 ENVIRONMENT \"...\" 这个赋值形状,不是裸字符串)")
    check("还钉了 TIMEOUT", re.search(r"TIMEOUT\s+\d+", code) is not None,
          "(没有超时上限 ⇒ 一个挂死的测试会一路吃到 CI 的全局超时,"
          "整条流水线被掐成 cancelled —— 跟『测试超时』长得几乎一样)")

    # ★ 循环必须在【所有 add_test 之后】★
    #   属性是在【配置时】写进生成文件的;循环写在前面的话,
    #   后面的 add_test 就没赶上 ⇒ 生成文件里那个目标就没有 ENVIRONMENT。
    #   这一条查的是源里的先后次序,①查不到、②查得到 —— 两边都留。
    last_add = None
    for m in re.finditer(r"add_test\s*\(", code):
        last_add = m.start()
    loop_pos = code.find("get_property(")
    if last_add is None or loop_pos < 0:
        check("循环在所有 add_test 之后", False,
              "(源里找不到 add_test 或那个循环)")
    else:
        check("循环在所有 add_test 之后", loop_pos > last_add,
              "(循环在第 %d 个字符,最后一个 add_test 在第 %d 个字符)"
              % (loop_pos, last_add))

    # ================= ② 果:配置后的真实结果 =================
    print()
    print("=== ② 果:%s ===" % gp)
    if not gp.is_file():
        print("FAIL 生成文件不存在:%s" % gp)
        print("     (没有它 ⇒ 这条判据什么也没查 —— 『没查』和『查过』必须能区分)")
        return 1
    gt = gp.read_text(encoding="utf-8", errors="replace")

    tests = RE_ADD.findall(gt)
    props = {m.group(1): m.group(2) for m in RE_PROP.finditer(gt)}
    print("  (生成文件里有 %d 个 add_test、%d 条 set_tests_properties)"
          % (len(tests), len(props)))
    check("至少找到一个 ctest 目标", len(tests) > 0,
          "(一个都没有 ⇒ 这条判据什么也没查)")

    no_env = [t for t in tests
              if "QT_QPA_PLATFORM=offscreen" not in props.get(t, "")]
    check("每个 ctest 目标都带 QT_QPA_PLATFORM=offscreen", not no_env,
          "(漏了:%s)" % (", ".join(sorted(no_env)) or "无"))

    no_timeout = [t for t in tests
                  if not re.search(r"\bTIMEOUT\s+\"?\d+", props.get(t, ""))]
    check("每个 ctest 目标都有 TIMEOUT 上限", not no_timeout,
          "(漏了:%s)" % (", ".join(sorted(no_timeout)) or "无"))

    no_prop = [t for t in tests if t not in props]
    check("每个 ctest 目标都有 set_tests_properties", not no_prop,
          "(没有属性行:%s)" % (", ".join(sorted(no_prop)) or "无"))

    # ★ 阳性对照:判据自己必须能看见"缺属性"这件事 ★
    #   拿一个【假的目标名】去查,必须报"缺"—— 不然"没漏"可能只是
    #   因为它压根没在查。
    probe = "tst__不存在的目标__"
    check("阳性对照:查一个不存在的目标会报『缺』",
          "QT_QPA_PLATFORM=offscreen" not in props.get(probe, ""),
          "(判据必须能说出『缺』,不然上面前面的『没漏』就没有意义)")

    print()
    if not fails:
        print("RESULT: PASS —— 每个 ctest 目标都跑在 QT_QPA_PLATFORM=offscreen 下,"
              "且都有 TIMEOUT 上限(挂死的测试会变成一条红色失败,而不是掐掉整条流水线)")
        return 0
    print("RESULT: FAIL -> %s" % fails)
    return 1


if __name__ == "__main__":
    sys.exit(main())
