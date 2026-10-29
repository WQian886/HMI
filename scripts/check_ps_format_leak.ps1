# ============================================================================
# check_ps_format_leak.ps1 —— 静态检查"格式化占位符漏替换"
#
#  这个坑是什么
#  ------------
#  一段文字里写着 {0},是想让 -f 把数字填进去。漏了,那句话就还停在
#  "该有数字的地方是空的",而且【不报错】—— 脚本照跑,退出码照给,
#  只是内容是错的。而判据/报告里那些话的全部作用,就是把结论量化出来。
#
#  两种漏法(2026-10-21 在 scripts/long_run.ps1 里各踩到过)
#  --------------------------------------------------
#  (A) 压根没写 -f:
#          "★ 成功读 +{0} 次 —— 业务真的在跑" + "SOAK-2/3 的数字这一轮有效。"
#      ⇒ 报告里落下来的是 "★ 成功读 +{0} 次 …"
#
#  (B) 写了 -f,但【-f 的优先级比 + 高】:
#          "★ 发出 +{0} 次但成功 0 次" + "在途变量 = {1}" -f $sent, $inflight
#      实际是  "★ 发出 +{0} 次但成功 0 次" + ("在途变量 = {1}" -f $sent, $inflight)
#      ⇒ 只有【最后一个】字面量被格式化,前面几段的 {0}/{1} 原样留着。
#      ⇒ 而且这一支恰恰是【真出问题时才走的那一支】——平时跑得再顺也发现不了。
#
#  为什么值得做成一个【脚本】而不是顺手看一眼
#  -------------------------------------------
#  这个错的特征是"肉眼看不出来":每段字符串单独看都完全正常,
#  合起来的优先级顺序要查文档才知道。而它【不报错】,所以能一直活着 ——
#  2026-10-21 之前它就在那儿,连着 950.9 分钟那次长跑都带着它。
#  静态扫一遍是几秒钟的事,比在报告里读到一个空数字便宜得多。
#
#  怎么检的
#  --------
#  不用正则(正则分不清括号和字符串),用 PowerShell 自己的【AST】:
#
#    1) 找出所有 Format 节点(-f)。它【管辖】的字符串字面量 =
#       它 Left 里的那些(剥掉括号后)。
#    2) 找出所有"拼接出来的字符串字面量"(某个 Plus 节点的左右操作数里的字面量)。
#    3) 规则:上面第 2 类里,凡是带着 {N} 形状占位符、又【不在】任何一个
#       Format 的管辖范围内的 ⇒ 漏替换,报出来。
#
#  ★ 为什么只查"拼接出来的"字面量
#    因为像 $reLine = '^\d{4}-\d{2}...' 这种【正则】里的 {4} {2} 是量词,
#    不是格式化占位符。把所有带 {N} 的字面量都扫一遍必然误报。
#    只看拼接里的,既抓得住 (A)(B) 两种真坑,又不碰正则。
#
#  ★ "{{0}}" 这种转义写法要放行:PowerShell 的 -f 把 {{  当成一个字面的 {。
#
#  用法:
#    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check_ps_format_leak.ps1
#  退出码:
#    0 = 没有漏替换
#    1 = 有漏替换(或某个文件解析失败)
#
#  ★ 本文件必须存成【带 BOM 的 UTF-8】
#    Windows PowerShell 5.1 读 .ps1 时,没有 BOM 就按【系统 ANSI 代码页】(中文上是 GBK)读,
#    于是文件里的中文全变乱码 —— 而且乱码后的字节可能把引号吃掉,直接变成语法错误。
#    (2026-10-21 写这个文件时踩了一次:存成无 BOM 的 UTF-8,一跑就是
#     "Unexpected token ')'" —— 报的还是个跟实际原因毫无关系的行号。)
#    本仓库其它 .ps1 也都是带 BOM 的,保持一致。
# ============================================================================

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

# 排除编译产物目录 —— 里面可能有从别处拷来的脚本,不是本仓库的代码。
$files = @(Get-ChildItem -Path $repo -Recurse -Include *.ps1 -File |
           Where-Object { $_.FullName -notmatch '\\out\\|\\build|\\node_modules\\|\\\.git\\' })
$files = @($files | Sort-Object FullName)

Write-Host "扫描 $($files.Count) 个 .ps1" -ForegroundColor Cyan

# 找出一个表达式子树里的全部【字符串字面量】节点
function Get-StringLiterals($node) {
    if ($null -eq $node) { return @() }
    return @($node.FindAll({ param($n)
        $n -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true))
}

# 剥掉左操作数外面那层括号。
#   ("a" + "b") -f $x   → 剥完之后是 Plus ⇒ 【正确】的写法:管辖整段拼接
#   "a" + "b" -f $x     → 左操作数只是 "b" ⇒ 只管辖 "b"
# ★ 必须有次数上限:万一 AST 形状出乎意料,不能把脚本挂死。
function Unwrap-Paren($node) {
    $n = $node
    $i = 0
    while ($n -is [System.Management.Automation.Language.ParenExpressionAst] -and $i -lt 20) {
        $n = $n.Pipeline.PipelineElements[0]
        $i++
    }
    return $n
}

# 一段文字里有没有 {N} 形状的占位符
#   ★ 先把 {{ 和 }} 还原成字面的 { 和 } —— 那是 -f 的转义写法,不是占位符。
function Test-HasPlaceholder([string] $text) {
    if ([string]::IsNullOrEmpty($text)) { return $false }
    $t = $text -replace '\{\{', "`x01" -replace '\}\}', "`x02"
    return ($t -match '\{\d+\}')
}

$leaks  = 0
$broken = 0

foreach ($f in $files) {
    $errs   = $null
    $tokens = $null
    $ast    = [System.Management.Automation.Language.Parser]::ParseFile(
                 $f.FullName, [ref]$tokens, [ref]$errs)

    if ($errs -and $errs.Count -gt 0) {
        $broken++
        Write-Host ("  [解析失败] {0}:{1} {2}" -f `
            $f.Name, $errs[0].Extent.StartLineNumber, $errs[0].Message) -ForegroundColor Red
        continue
    }

    $bins = $ast.FindAll({ param($n)
        $n -is [System.Management.Automation.Language.BinaryExpressionAst] }, $true)

    # ---- 第 1 步:哪些字面量被某个 -f 管辖 -------------------------------
    #   用 Extent 的起始偏移做身份标识 —— 同一个字面量节点在两个集合里
    #   指的是同一个对象,但拿它的 Offset 当 key 更省事也更好读。
    $covered = New-Object 'System.Collections.Generic.HashSet[int]'
    foreach ($b in $bins) {
        if ($b.Operator -ne 'Format') { continue }
        $left = Unwrap-Paren $b.Left
        foreach ($lit in (Get-StringLiterals $left)) {
            [void]$covered.Add($lit.Extent.StartOffset)
        }
    }

    # ---- 第 2 步:拼接里的字面量,凡是带占位符又没被管辖 ⇒ 漏替换 ------
    foreach ($b in $bins) {
        if ($b.Operator -ne 'Plus') { continue }
        foreach ($lit in (Get-StringLiterals $b)) {
            if (-not (Test-HasPlaceholder $lit.Value)) { continue }
            if ($covered.Contains($lit.Extent.StartOffset)) { continue }
            $leaks++
            Write-Host ("  [漏替换] {0}:{1}" -f $f.Name, $lit.Extent.StartLineNumber) -ForegroundColor Red
            Write-Host ("             " + $lit.Value) -ForegroundColor DarkGray
            Write-Host ("             ⇒ 这段本该是数字,现在空着。修法:把整个拼接括起来再 -f") -ForegroundColor DarkGray
        }
    }
}

Write-Host ""
if ($leaks -eq 0 -and $broken -eq 0) {
    Write-Host "OK — 没有漏替换的格式化占位符" -ForegroundColor Green
    exit 0
}
Write-Host ("RED — 漏替换 {0} 处,解析失败 {1} 个" -f $leaks, $broken) -ForegroundColor Red
exit 1
