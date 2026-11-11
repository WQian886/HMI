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
#  扫哪些文件(2026-11-10 G-29 定下的规则)
#  ------------------------------------------
#    · 受版本控制的 .ps1            —— 仓库的正片,必扫
#    · 未跟踪但【没被忽略】的 .ps1  —— 新写的脚本在 git add 之前也要被扫到
#    · 被 gitignore 的                —— .scratch/ soaks/ memprobe/ 这些临时区,不扫
#
#    为什么不能"扫整个工作区":那会让判据的结论取决于本地垃圾 ——
#    本地红、CI 绿,两边各说各话。这条规则的判据是
#    scripts/test_ps_format_leak_scope.ps1(ctest 目标 tst_PsFormatLeakScope)。
#
#  机器可读标记(2026-11-11 G-31-b)—— 【判定不许看中文】
#    ---------------------------------------------
#    [LEAK]          有一处格式化占位符漏替换
#    [PARSE-ERROR]   某个 .ps1 解析失败
#    [SCOPE-EMPTY]   扫描范围为空(排除规则把整仓都排掉了 ⇒ 必须红)
#    摘要行: RED — leaks=N broken=M  (LEAK=N PARSE-ERROR=M)
#    ★ 摘要行【故意不带方括号】——
#      判定方是拿"[LEAK]"当子串去匹配的:摘要行要是也写成 "[LEAK]=0",
#      那一行就会把"有没有漏替换"这个问题答成"永远有" ⇒ 判据恒绿。
#      方括号只留给【逐条】的明细行。
#
#    这三个标记是本脚本与【判据的判据】
#    (scripts/test_ps_format_leak_scope.ps1,ctest 目标 tst_PsFormatLeakScope)
#    之间的【契约】—— 判定一律走 ASCII。
#    中文标记([漏替换]/[解析失败])还在,但只给人看:
#    中文能不能被 grep 到取决于控制台输出代码页 —— 实测 CP437 下
#    checker 的输出里中文全变成 '?',靠 grep 中文判定的脚本会【假红】。
#    ⇒ 一句话:【显示给人看的东西不许参与判定】。
#
#  用法:
#    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check_ps_format_leak.ps1
#    powershell ... -File scripts\check_ps_format_leak.ps1 -Root <目录>
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

param(
    # ★ 2026-11-10 (G-29):扫描根目录。留空 = 本仓库(本脚本所在目录的上一级)。
    #   加这个参数是为了让【判据自己也能被测】:测试要拿一个临时目录当仓库,
    #   造出"被忽略的目录里藏着坏脚本"这种现场 —— 不给参数就没法造。
    [string]$Root = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$global:LASTEXITCODE = 0

if ([string]::IsNullOrWhiteSpace($Root)) {
    $repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
} else {
    $repo = (Resolve-Path $Root).Path
}

# ============================================================================
# 扫描范围:只扫【属于本仓库的脚本】
# ============================================================================
# ★ 2026-11-10 (G-29)修的坑:
#   原来这里扫的是【整个工作区】—— 连被 .gitignore 忽略的
#   .scratch/ soaks/ memprobe/ 这些临时区/产物区/备份区一起扫。
#   ⇒ 判据的结论取决于【本地垃圾】,不取决于仓库 ⇒
#     本地红、CI 绿,两边各说各话,谁也说不清哪个是真的。
#   (2026-11-10 实测:本地那次红,红在一个 .scratch 里的临时脚本上。)
#
#   现在的规则:
#     · 受版本控制的 .ps1            —— 仓库的正片,必扫
#     · 未跟踪但【没被忽略】的 .ps1  —— 新写的脚本在 git add 之前也要被扫到
#     · 被忽略的                       —— 临时区/长跑产物/备份,不归我们管
#
#   ★ 为什么"未跟踪但没被忽略"也要扫:
#     只扫已跟踪文件 ⇒ 新写的脚本在 git add 之前【逃过检查】⇒ 又是假绿。
#     而"被忽略"是作者自己声明的"这不是仓库代码" ⇒ 排除它是尊重这个声明。
#
#   没有 git(或不在 git 仓库里)时退回"扫整个目录"(老行为),
#   并打印一行说明 —— 别让人以为扫全了。

function Invoke-Git([string]$repoPath, [string[]]$gitArgs) {
    # ★ 原生命令的 stderr 在 $ErrorActionPreference='Stop' 下会被当成
    #   NativeCommandError 抛出来 —— 而 git 的"正常失败"(不是仓库/没匹配)
    #   正是往 stderr 写东西。所以调 git 时把错误偏好降成 Continue。
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & git -C $repoPath @gitArgs 2>$null
        return @{ Output = @($out); ExitCode = $LASTEXITCODE }
    } finally {
        $ErrorActionPreference = $prev
    }
}

$hasGit = ($null -ne (Get-Command git -ErrorAction SilentlyContinue))
if ($hasGit) {
    $probe = Invoke-Git $repo @('rev-parse', '--is-inside-work-tree')
    $hasGit = ($probe.ExitCode -eq 0 -and ($probe.Output -join '').Trim() -eq 'true')
    if (-not $hasGit) {
        Write-Host ("  [范围] {0} 不是 git 仓库 ⇒ 退回『扫整个目录』" -f $repo) -ForegroundColor Yellow
    }
}

# 排除编译产物目录 —— 里面可能有从别处拷来的脚本,不是本仓库的代码。
# ★ 2026-11-10 (G-29-b):排除的是【目录名】,不是【路径里含 build 的那一段】。
#   原来写的是 '\\out\\|\\build|\\node_modules\\|\\\.git\\' ——
#   其中 '\\build' 后面没有跟分隔符,于是仓库根的
#   build.ps1 / build_tests.ps1 也被一起排除了。
#   ⇒ 这两个正片脚本【从来没被这条判据看过】(今天实测它们没有漏替换,
#     所以今天还不是 bug —— 但判据一直是瞎的,跟 tst_ProjectSchema
#     那个"硬编码路径"是同一族:它测的不是它声称在测的东西)。
#   现在改成按【目录段】判断:build / build-* / build_ninja / cmake-build-*
#   / out / node_modules / .git 这些【目录】才排除,
#   而根目录下的 build.ps1 这种【文件】照扫不误。
#
# ★ 2026-11-11 (G-31-a):再修一层 —— 排除判断必须只看【仓库根以下】的路径段。
#   原来判的是【绝对路径】$_.DirectoryName ⇒ 仓库【自己】所在的祖先目录
#   只要叫 build*/out/node_modules,整仓的 .ps1 就【一个都扫不到】,
#   而判据照样打印 "OK — 没有漏替换"、退出码 0 ⇒ 判据彻底瞎了还报绿。
#   实测(2026-11-11):把仓库放到 C:\...\build674cde85\hmi 下,
#   一个真含 {0} 的 .ps1 照样"扫描 0 个 .ps1" + OK + EXIT=0。
#   ⇒ 现在先算【相对仓库根】的路径,再按目录段判断。
#     仓库根自己的名字(以及更上层的祖先目录名)不再参与判断 ——
#     "仓库住在哪"不该影响判据的结论。
$candidates = @(Get-ChildItem -Path $repo -Recurse -Include *.ps1 -File |
                Where-Object {
                    $rel = $_.FullName.Substring($repo.Length) -replace '^[\\/]+', ''
                    $relDir = Split-Path -Parent $rel
                    $segs = @()
                    if (-not [string]::IsNullOrEmpty($relDir)) {
                        $segs = @($relDir -split '[\\/]' | Where-Object { $_ -ne '' })
                    }
                    $excluded = $false
                    foreach ($sg in $segs) {
                        if ($sg -match '^(?:out|node_modules|\.git|cmake-build-.+|build.*)$') {
                            $excluded = $true
                            break
                        }
                    }
                    -not $excluded
                })

# 受版本控制的 .ps1(索引里的;一行一个,相对仓库根,正斜杠)
$tracked = New-Object 'System.Collections.Generic.HashSet[string]'
if ($hasGit) {
    $ls = Invoke-Git $repo @('ls-files', '--cached', '--', '*.ps1')
    foreach ($p in $ls.Output) {
        if ($null -eq $p) { continue }   # git 一行没输出时,数组里会带一个 $null
        [void]$tracked.Add((($p -replace '\\', '/')).Trim())
    }
}

$files = @()
foreach ($f in $candidates) {
    $rel = ($f.FullName.Substring($repo.Length) -replace '^[\\/]+', '') -replace '\\', '/'
    if ($tracked.Contains($rel)) { $files += $f; continue }
    if ($hasGit) {
        # 被忽略 ⇒ 作者声明过"这不是仓库代码" ⇒ 不归我们管
        $ig = Invoke-Git $repo @('check-ignore', '-q', '--', $rel)
        if ($ig.ExitCode -eq 0) { continue }
    }
    $files += $f
}
$files = @($files | Sort-Object FullName)

# ★ 2026-11-11 (G-31-a):扫描范围【自检】—— "我什么都没看" ≠ "没问题"。
#   扫到 0 个文件、而仓库里明明有受版本控制的 .ps1 ⇒ 一定是排除规则把整仓
#   都排掉了(见上面 G-31-a 那段)。这时候必须【红】。
#   以前这里没有自检 ⇒ 判据在"扫了 0 个文件"的情况下也报 OK —— 假绿。
if ($files.Count -eq 0 -and $tracked.Count -gt 0) {
    Write-Host ("RED — [SCOPE-EMPTY] 扫描范围为空,但仓库里有 {0} 个受版本控制的 .ps1 ⇒ 排除规则把整仓都排掉了" -f $tracked.Count) -ForegroundColor Red
    exit 1
}

Write-Host "扫描 $($files.Count) 个 .ps1(仓库内的;被 gitignore 的临时区不算)" -ForegroundColor Cyan

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
        Write-Host ("  [PARSE-ERROR][解析失败] {0}:{1} {2}" -f `
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
            Write-Host ("  [LEAK][漏替换] {0}:{1}" -f $f.Name, $lit.Extent.StartLineNumber) -ForegroundColor Red
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
# ★ 2026-11-11 (G-31-b):摘要行也带【ASCII 机器可读】的计数 ——
#   这条判据的"判据"(test_ps_format_leak_scope.ps1)以前靠 grep 中文
#   『漏替换』来确认"红的原因对不对",而中文能不能被 grep 到取决于
#   控制台输出代码页:实测把 [Console]::OutputEncoding 设成 CP437,
#   输出里的中文全变成 '?',判据就误判成"红的原因不对" ⇒ 假红
#   (US 机器 = 本地绿、CI 红,两边对不上)。
#   ⇒ 判定一律走 ASCII 标记([LEAK] / [PARSE-ERROR] / [SCOPE-EMPTY]),
#     中文只留给人看 —— 【显示给人看的东西不许参与判定】。
Write-Host ("RED — leaks={0} broken={1}  (LEAK={0} PARSE-ERROR={1})" -f $leaks, $broken) -ForegroundColor Red
exit 1
