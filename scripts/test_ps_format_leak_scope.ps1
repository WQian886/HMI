# ============================================================================
# test_ps_format_leak_scope.ps1 —— 判据的判据(G-29)
#
#  这条脚本在回答一个问题:
#    "check_ps_format_leak.ps1 扫的范围,是不是【仓库的代码】?"
#
#  为什么必须有它
#  ------------
#  2026-11-10 实测:基线全套 ctest 红了一条 —— tst_PsFormatLeak。
#  红的理由是【工作区里一个 .scratch 下的临时脚本】语法不对,
#  而那个目录是被 .gitignore 掉的。
#  ⇒ 那条判据扫的是整个工作区,包括临时区/产物区/备份区。
#  ⇒ 它的结论取决于【本地垃圾】而不是仓库 ⇒
#     本地红、CI 绿,两边各说各话,谁也说不清哪个才是真的。
#
#  本脚本在临时目录里造一个【假仓库】,把现场各造一遍:
#
#    A  正片里有一个漏替换的脚本(受版本控制)      ⇒ 必须【红】(阳性对照)
#    B  正片里没有漏替换                           ⇒ 必须【绿】
#    C  被忽略的目录里藏着一个既有漏替换又语法错的 ⇒ 必须【绿】(G-29 正题)
#    D  新写的、还没 git add 的脚本里有漏替换       ⇒ 必须【红】
#                                                    (否则新脚本逃过检查 ⇒ 假绿)
#    E  仓库根有个叫 build.ps1 的文件              ⇒ 必须【红】(G-29-b 正题)
#    F  build 目录里面的脚本                       ⇒ 必须【绿】
#    G  仓库【自己】落在 build* 祖先目录下          ⇒ 必须【红】(G-31-a 正题)
#    H  仓库里只有 build 目录下的 .ps1             ⇒ 必须【红】(G-31-a:
#                                                    扫不到东西不许报绿)
#    I  用 CP437 代码页跑 checker                   ⇒ 必须【红】且 [LEAK] 仍在
#                                                    (G-31-b 正题:中文被代码页
#                                                     弄坏,ASCII 标记不受影响)
#
#  ★ A 和 D 是【阳性对照】:证明这条判据会红,不是"怎么都绿"。
#    C 是【阴性对照】:证明它只对仓库代码敏感。
#    A 是 C 的前提 —— 没有 A,"C 是绿的"可能只是因为【整条判据死了】。
#    I 自带反向确认:中文必须【确实被弄坏】—— 不然 I 就是"因为没生效所以绿"。
#
#  ★ 2026-11-11 (G-31-b):判定只认【ASCII 标记】
#    [LEAK] / [SCOPE-EMPTY] / [PARSE-ERROR] —— 这些是 checker 输出的机器可读契约。
#    以前这里 grep 的是中文『漏替换』,而中文能不能被 grep 到取决于
#    控制台输出代码页(CP437 下全变 '?')⇒ 同一份仓库在两台机器上结论相反。
#    ★ 摘要行故意写成 (LEAK=N PARSE-ERROR=M) 而不是 ([LEAK]=N ...):
#      判定方是拿 "[LEAK]" 当子串匹配的,摘要行也带方括号 ⇒ 永远匹配得上
#      ⇒ 判据恒绿。
#
#  用法:
#    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test_ps_format_leak_scope.ps1
#  退出码:
#    0 = 九种现场全对
#    1 = 有现场不符合预期
#
#  ★ 本文件必须存成【带 BOM 的 UTF-8 + CRLF】
#    Windows PowerShell 5.1 读 .ps1 时,没有 BOM 就按【系统 ANSI 代码页】读,
#    文件里的中文会变乱码,乱码后的字节还可能把引号吃掉 ⇒ 语法错。
#    (G-4:2026-10-14 / 2026-10-24 各踩过一次。)
# ============================================================================

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$global:LASTEXITCODE = 0

$checker = Join-Path $PSScriptRoot 'check_ps_format_leak.ps1'
if (-not (Test-Path $checker)) {
    Write-Host "找不到 checker: $checker" -ForegroundColor Red
    exit 1
}

# ---- 小工具 ---------------------------------------------------------------

function Write-Utf8File([string]$path, [string]$text) {
    $dir = Split-Path -Parent $path
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    # ★ 带 BOM 写:本仓库的规矩(见文件头)。WriteAllText 遇到 UTF8Encoding($true)
    #   会自己写 BOM 前缀 —— 跟 GetBytes() 不一样,那个只编码内容。
    [System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding($true)))
}

function Invoke-Checker([string]$root, [int]$codePage = 0) {
    # ★ 调子进程时把错误偏好降成 Continue:子进程往 stderr 写东西
    #   在 $ErrorActionPreference='Stop' 下会被当成 NativeCommandError 抛出来,
    #   而"checker 正常地报红"正是要往 stderr 写。
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        if ($codePage -eq 0) {
            $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $checker -Root $root 2>&1
        } else {
            # ★ 强制输出代码页(现场 I 用)
            #   必须在【同一个进程】里跑 checker —— 用 & '脚本.ps1' 而不是
            #   -File,这样 [Console]::OutputEncoding 真的作用到 checker 的输出上。
            #   (用 -File 起子进程的话,子进程会按自己的默认代码页输出,
            #    父进程设的编码对它无效。)
            $cmd = "[Console]::OutputEncoding=[Text.Encoding]::GetEncoding(" + $codePage +
                   "); & '" + $checker + "' -Root '" + $root + "'"
            $out = & powershell -NoProfile -ExecutionPolicy Bypass -Command $cmd 2>&1
        }
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prev
    }
    return @{ Exit = $code; Text = ($out | Out-String) }
}

function Invoke-Git([string]$repoPath, [string[]]$gitArgs) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & git -C $repoPath @gitArgs 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prev
    }
    return @{ Exit = $code; Text = ($out | Out-String) }
}

$script:failed = 0

function Reset-Repo([string]$root) {
    # ★ 每个场景都要【各自复位】——
    #   否则前一个场景留下的坏文件会让后一个场景"红"的原因不对
    #   (2026-11-10 第一次写就踩了这个:场景 F 期望绿,却因为
    #    场景 D/E 的漏替换还留在仓库里而红了 —— 测的不是它声称在测的东西)。
    Get-ChildItem -Path $root -Recurse -Include *.ps1 -File | Remove-Item -Force
    Write-Utf8File (Join-Path $root 'good.ps1') $goodBody
    [void](Invoke-Git $root @('add', '--', 'good.ps1'))
}

function Step([string]$name, [bool]$expectRed, [string]$root, [string]$expectMarker, [int]$codePage = 0) {
    $r = Invoke-Checker $root $codePage
    $why = ''
    if ($expectRed) {
        if ($r.Exit -eq 0) { $why = '期望红,实际绿' }
    } else {
        if ($r.Exit -ne 0) { $why = ('期望绿,实际红(exit=' + $r.Exit + ')') }
    }
    # ★ 2026-11-11 (G-31-b):红的原因还得对 —— 但【只能看 ASCII 标记】。
    #   以前这里 grep 的是中文『漏替换』,而中文能不能被 grep 到取决于
    #   控制台输出代码页:实测 [Console]::OutputEncoding=CP437 时
    #   checker's 输出里中文全变成 '?' ⇒ 这里必然判"红的原因不对" ⇒ 假红。
    #   (US 机器上本地绿、CI 红,两边对不上。)
    #   ⇒ 判定一律走 ASCII 标记:[LEAK] / [PARSE-ERROR] / [SCOPE-EMPTY]。
    if ($expectMarker -ne '') {
        $has = ($r.Text -match [regex]::Escape($expectMarker))
        if ($expectRed -and -not $has) {
            if ($why -eq '') { $why = ('红了,但输出里没有 ' + $expectMarker + ' 标记 —— 红的原因不对') }
        }
        if ((-not $expectRed) -and $has) {
            if ($why -eq '') { $why = ('绿了,但输出里却有 ' + $expectMarker + ' 标记 —— 绿的原因不对') }
        }
    }
    $tag = if ($why -eq '') { 'OK  ' } else { 'FAIL' }
    if ($why -ne '') { $script:failed++ }
    Write-Host ("  [{0}] {1}{2}" -f $tag, $name, $(if ($why -eq '') { '' } else { '  —— ' + $why })) -ForegroundColor $(if ($why -eq '') { 'Green' } else { 'Red' })
    if ($why -ne '') {
        Write-Host ("        checker 输出:`n" + $r.Text.Trim()) -ForegroundColor DarkGray
    }
    return ($why -eq '')
}

# ---- 造现场 ---------------------------------------------------------------

# ★ 这两段是"带 {0} 的拼接" —— 写进文件之后就是一条真正的漏替换。
#   但它们在【本文件】里只是普通的字符串常量(没参与 + 运算),
#   所以本文件自己不会变成"漏替换"而被判据自己报红。
$leakBody = @'
$s = "sent {0} times" + "done"
'@

# 既有漏替换、又有语法错 —— 放在被忽略的目录里。
$junkBody = @'
$bad = "sent {0} times" + "done"
$oops = (
'@

$goodBody = @'
Write-Host "hello"
'@

$tmp = Join-Path $env:TEMP ('hmi_ps_scope_' + [guid]::NewGuid().ToString('N'))
Write-Host ("临时仓库: " + $tmp) -ForegroundColor Cyan

try {
    New-Item -ItemType Directory -Path $tmp -Force | Out-Null

    $init = Invoke-Git $tmp @('init', '-q')
    if ($init.Exit -ne 0) {
        Write-Host ("  [FAIL] git init 失败:" + $init.Text) -ForegroundColor Red
        exit 1
    }

    # 被忽略的目录 —— 作者声明过"这不是仓库代码"
    Write-Utf8File (Join-Path $tmp '.gitignore') "junk/`r`n"

    # ---- 现场 B:正片干净 ⇒ 必须绿 ------------------------------------
    Reset-Repo $tmp
    Write-Host 'B 正片干净 ⇒ 期望【绿】'
    [void](Step 'B 正片干净' $false $tmp '')

    # ---- 现场 A:正片里有漏替换 ⇒ 必须红(阳性对照)--------------------
    Reset-Repo $tmp
    Write-Utf8File (Join-Path $tmp 'leak.ps1') $leakBody
    [void](Invoke-Git $tmp @('add', '--', 'leak.ps1'))
    Write-Host 'A 正片有漏替换 ⇒ 期望【红】(阳性对照)'
    [void](Step 'A 正片有漏替换' $true $tmp '[LEAK]')

    # ---- 现场 C:被忽略的目录里藏着坏脚本 ⇒ 必须绿(G-29 正题)--------
    Reset-Repo $tmp
    Write-Utf8File (Join-Path $tmp 'junk\bad.ps1') $junkBody
    Write-Host 'C 被忽略的目录里有坏脚本 ⇒ 期望【绿】(G-29 正题)'
    [void](Step 'C 被忽略的目录里有坏脚本' $false $tmp '')

    # ---- 现场 D:新写的还没 git add ⇒ 必须红 --------------------------
    Reset-Repo $tmp
    Write-Utf8File (Join-Path $tmp 'brand_new.ps1') $leakBody
    Write-Host 'D 新写的未跟踪脚本里有漏替换 ⇒ 期望【红】(否则新脚本会逃过检查)'
    [void](Step 'D 新写的未跟踪脚本' $true $tmp '[LEAK]')

    # ---- 现场 E:仓库根有个叫 build.ps1 的文件 ⇒ 必须红 ---------------
    # ★ 这条钉的是 G-29-b:排除"build 目录"的规则不能误伤
    #   【根目录下的 build.ps1 文件】—— 仓库里真有两个这种文件
    #   (build.ps1 / build_tests.ps1),它们从来没被这条判据看过。
    Reset-Repo $tmp
    Write-Utf8File (Join-Path $tmp 'build.ps1') $leakBody
    [void](Invoke-Git $tmp @('add', '--', 'build.ps1'))
    Write-Host 'E 根目录的 build.ps1 有漏替换 ⇒ 期望【红】(排除规则不能误伤它)'
    [void](Step 'E 根目录的 build.ps1' $true $tmp '[LEAK]')

    # ---- 现场 F:build 目录里面的脚本 ⇒ 必须绿 -----------------------
    Reset-Repo $tmp
    Write-Utf8File (Join-Path $tmp 'build\inner.ps1') $leakBody
    Write-Host 'F build 目录里面的脚本 ⇒ 期望【绿】(编译产物目录仍然不扫)'
    [void](Step 'F build 目录里面的脚本' $false $tmp '')

    # ---- 现场 G:仓库【自己】落在 build* 祖先目录下 ⇒ 仍必须扫得到 ----
    # ★ 这条钉的是 G-31-a:排除规则以前判的是【绝对路径】,
    #   于是仓库只要 clone 到 D:\build\... 这类目录下,
    #   整仓的 .ps1 就【一个都扫不到】,判据却照样报 "OK"、退出码 0
    #   (2026-11-11 实测:真含 {0} 的 .ps1 照样"扫描 0 个 .ps1" + OK + EXIT=0)。
    #   ⇒ "仓库住在哪"不该影响判据的结论。
    $gRoot = Join-Path $tmp ('build' + [guid]::NewGuid().ToString('N').Substring(0, 8) + '\repo')
    New-Item -ItemType Directory -Path $gRoot -Force | Out-Null
    [void](Invoke-Git $gRoot @('init', '-q'))
    Write-Utf8File (Join-Path $gRoot '.gitignore') "junk/`r`n"
    Reset-Repo $gRoot
    Write-Utf8File (Join-Path $gRoot 'leak.ps1') $leakBody
    [void](Invoke-Git $gRoot @('add', '--', 'leak.ps1'))
    Write-Host 'G 仓库落在 build* 祖先目录下 ⇒ 期望【红】(祖先目录名不该让判据瞎掉)'
    [void](Step 'G 仓库住在 build* 祖先目录' $true $gRoot '[LEAK]')

    # ---- 现场 H:扫不到任何文件 ⇒ 必须红(不许"没看就当没问题") -------
    # ★ 这条钉的是 G-31-a 的另一半:排除规则把整仓排掉时,
    #   判据必须【红】并说清是"扫描范围为空",而不是报 OK。
    Reset-Repo $tmp
    # 把根目录那个 good.ps1 从索引里摘掉,只留 build 目录下的一个
    [void](Invoke-Git $tmp @('rm', '-q', '--cached', '--', 'good.ps1'))
    Remove-Item (Join-Path $tmp 'good.ps1') -Force -ErrorAction SilentlyContinue
    Write-Utf8File (Join-Path $tmp 'build\inner.ps1') $goodBody
    [void](Invoke-Git $tmp @('add', '--', 'build\inner.ps1'))
    Write-Host 'H 仓库里只有 build 目录下的 .ps1 ⇒ 期望【红】(扫不到东西不许报绿)'
    [void](Step 'H 扫描范围为空' $true $tmp '[SCOPE-EMPTY]')

    # ---- 现场 I:用 CP437 代码页跑 checker ⇒ ASCII 标记必须还在 --------
    # ★ 这条钉的是 G-31-b:判定一律走 ASCII 标记。
    #   中文能不能被 grep 到取决于控制台输出代码页 ——
    #   实测把 [Console]::OutputEncoding 设成 CP437,checker 输出里的中文
    #   全变成 '?',而 ASCII 的 [LEAK] 一个字都不受影响。
    #   ⇒ 判据只要还在看中文,同一份仓库在中文机器和英文机器上结论相反
    #     (本地绿、CI 红,两边对不上)。
    #   ⇒ 这条现场同时钉住两件事:
    #     ① [LEAK] 在 CP437 下【仍然找得到】;
    #     ② 中文【确实被弄坏了】—— 不然这条现场就是"因为没生效所以绿",
    #        等于什么都没测。
    Reset-Repo $tmp
    Write-Utf8File (Join-Path $tmp 'leak.ps1') $leakBody
    [void](Invoke-Git $tmp @('add', '--', 'leak.ps1'))
    Write-Host 'I 用 CP437 代码页跑 checker ⇒ 期望【红】且 [LEAK] 仍在(中文会变乱码)'
    $stepI = Step 'I CP437 代码页下的标记' $true $tmp '[LEAK]' 437
    # 反向确认:中文确实被代码页弄坏了(否则这条现场是空的)
    $rI = Invoke-Checker $tmp 437
    $cnGone = ($rI.Text -notmatch '漏替换')
    if (-not $cnGone) {
        Write-Host '  [FAIL] I CP437 代码页下的标记  —— 中文『漏替换』居然还看得见 ⇒ 代码页没生效,这条现场等于没测' -ForegroundColor Red
        $script:failed++
    } else {
        Write-Host '  [OK  ] I 的前置确认:中文已被 CP437 弄坏(ASCII 标记不受影响)' -ForegroundColor Green
    }
} finally {
    if (Test-Path $tmp) {
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    }
}

Write-Host ''
if ($script:failed -eq 0) {
    Write-Host 'OK — 扫描范围判据咬得住:仓库的正片会被扫,临时区不会,新脚本逃不掉' -ForegroundColor Green
    exit 0
}
Write-Host ("RED — {0} 个现场不符合预期" -f $script:failed) -ForegroundColor Red
exit 1
