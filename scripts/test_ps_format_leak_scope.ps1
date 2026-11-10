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
#  本脚本在临时目录里造一个【假仓库】,把四种现场各造一遍:
#
#    A  正片里有一个漏替换的脚本(受版本控制)      ⇒ 必须【红】(阳性对照)
#    B  正片里没有漏替换                           ⇒ 必须【绿】
#    C  被忽略的目录里藏着一个既有漏替换又语法错的 ⇒ 必须【绿】(G-29 正题)
#    D  新写的、还没 git add 的脚本里有漏替换       ⇒ 必须【红】
#                                                    (否则新脚本逃过检查 ⇒ 假绿)
#
#  ★ A 和 D 是【阳性对照】:证明这条判据会红,不是"怎么都绿"。
#    C 是【阴性对照】:证明它只对仓库代码敏感。
#    A 是 C 的前提 —— 没有 A,"C 是绿的"可能只是因为【整条判据死了】。
#
#  用法:
#    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test_ps_format_leak_scope.ps1
#  退出码:
#    0 = 四种现场全对
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

function Invoke-Checker([string]$root) {
    # ★ 调子进程时把错误偏好降成 Continue:子进程往 stderr 写东西
    #   在 $ErrorActionPreference='Stop' 下会被当成 NativeCommandError 抛出来,
    #   而"checker 正常地报红"正是要往 stderr 写。
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $checker -Root $root 2>&1
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

function Step([string]$name, [bool]$expectRed, [string]$root, [bool]$expectMention) {
    $r = Invoke-Checker $root
    $why = ''
    if ($expectRed) {
        if ($r.Exit -eq 0) { $why = '期望红,实际绿' }
    } else {
        if ($r.Exit -ne 0) { $why = ('期望绿,实际红(exit=' + $r.Exit + ')') }
    }
    # 红的原因还得对:得是"发现了漏替换",不是"碰巧语法错"之类。
    if ($expectMention -and ($r.Text -notmatch '漏替换')) {
        if ($why -eq '') { $why = '红了,但输出里没有『漏替换』三个字 —— 红的原因不对' }
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
    [void](Step 'B 正片干净' $false $tmp $false)

    # ---- 现场 A:正片里有漏替换 ⇒ 必须红(阳性对照)--------------------
    Reset-Repo $tmp
    Write-Utf8File (Join-Path $tmp 'leak.ps1') $leakBody
    [void](Invoke-Git $tmp @('add', '--', 'leak.ps1'))
    Write-Host 'A 正片有漏替换 ⇒ 期望【红】(阳性对照)'
    [void](Step 'A 正片有漏替换' $true $tmp $true)

    # ---- 现场 C:被忽略的目录里藏着坏脚本 ⇒ 必须绿(G-29 正题)--------
    Reset-Repo $tmp
    Write-Utf8File (Join-Path $tmp 'junk\bad.ps1') $junkBody
    Write-Host 'C 被忽略的目录里有坏脚本 ⇒ 期望【绿】(G-29 正题)'
    [void](Step 'C 被忽略的目录里有坏脚本' $false $tmp $false)

    # ---- 现场 D:新写的还没 git add ⇒ 必须红 --------------------------
    Reset-Repo $tmp
    Write-Utf8File (Join-Path $tmp 'brand_new.ps1') $leakBody
    Write-Host 'D 新写的未跟踪脚本里有漏替换 ⇒ 期望【红】(否则新脚本会逃过检查)'
    [void](Step 'D 新写的未跟踪脚本' $true $tmp $true)

    # ---- 现场 E:仓库根有个叫 build.ps1 的文件 ⇒ 必须红 ---------------
    # ★ 这条钉的是 G-29-b:排除"build 目录"的规则不能误伤
    #   【根目录下的 build.ps1 文件】—— 仓库里真有两个这种文件
    #   (build.ps1 / build_tests.ps1),它们从来没被这条判据看过。
    Reset-Repo $tmp
    Write-Utf8File (Join-Path $tmp 'build.ps1') $leakBody
    [void](Invoke-Git $tmp @('add', '--', 'build.ps1'))
    Write-Host 'E 根目录的 build.ps1 有漏替换 ⇒ 期望【红】(排除规则不能误伤它)'
    [void](Step 'E 根目录的 build.ps1' $true $tmp $true)

    # ---- 现场 F:build 目录里面的脚本 ⇒ 必须绿 -----------------------
    Reset-Repo $tmp
    Write-Utf8File (Join-Path $tmp 'build\inner.ps1') $leakBody
    Write-Host 'F build 目录里面的脚本 ⇒ 期望【绿】(编译产物目录仍然不扫)'
    [void](Step 'F build 目录里面的脚本' $false $tmp $false)
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
