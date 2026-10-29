# ============================================================================
# long_run.ps1 活体判据(SOAK-6)的负向/正向对照(2026-10-21)
#
#  为什么要有这个脚本
#  ------------------
#  判据是被测的【工具】,工具自己得先被测过。
#  旧判据(数日志行)两头都骗过人,而且骗的是同一个跑法:
#    · 假红:健康的轮询【不产生业务类日志】—— 协议层收发全是 qCDebug,
#            默认 INFO 级别下一行都不落。业务跑得好好的,判据说"只在自转"。
#    · 假绿:本地心跳(HistoryRecorder/TrendWidget)也在写日志,能把
#            "静默期之后日志在长"这个前提撑起来 ⇒ 2026-10-20 那次 950.9 分钟
#            拿到 PASS —— 但那个 PASS 证明的是"程序会自己给自己记心跳"。
#  一条判据如果两头都错,它就没有资格给结论。
#
#  这个脚本干什么
#  --------------
#  从 long_run.ps1 里【原样抠出】Get-PollStats / Get-LivenessVerdict 两个函数
#  和 $noiseRe 这个正则(统一用 Extract-Def),喂几组人造日志,检查状态判得对不对。
#  ★ 不复制粘贴一份实现 —— 复制的那份迟早跟本体对不上,而对不上的时候没人会发现。
#
#  跑的用例
#  --------
#   0. ★ 真实日志的黄金样本 —— 判据能不能从【真日志】里读出数字
#   1. 健康轮询(成功读计数在涨)                → PASS
#   2. 从站不回包(发出在涨、成功不涨)           → FAIL 且【不】算应用故障(exit 4)
#   3. 应用卡死(发出不涨、在途不为 0)           → FAIL 且【是】应用故障(exit 1)
#   4. 一条统计行都没有(旧二进制)               → UNKNOWN(判不出来,不下结论)
#   5. 回归·假红:健康轮询在【旧二进制】下的日志形态 → 旧判据判 FAIL,新判据不乱下结论
#   6. 回归·假绿:2026-10-20 的现场(只有心跳)     → 旧"资格"判据被骗过,新判据不判 PASS
#   7. 新旧两种格式都判得出来                    → 判据对两个二进制版本都有效
#   8. 格式漂移(改了分隔符)                      → UNKNOWN(判不出,而不是判错)
#   9. 每一支结论里的数字必须真的在               → 不许有漏掉的 {0} 占位符
#
#  ★★ 用例 0 是这个脚本【原来最缺的那一条】★★
#    这个脚本原来喂给判据的是"发出=123"(等号后没空格)——
#    那是【它自己想象的格式】。而程序实际打出来的是"发出= 123":
#    QDebug 在相邻两项之间插一个空格,`<< "发出=" << count` 就变成了
#    "发出= 123"。于是正则在真实日志上一次都匹配不上,
#    而这个脚本因为喂的是自己想象的格式,【一直绿】。
#    2026-10-21 实测:长跑的启动自检等满 120 秒判"没资格跑",
#    而那 120 秒里程序一直在正常轮询(成功 271 次、失败 0 次)。
#    一个拿自己当参照的测试,证明不了它和现实对得上。
#    所以现在:黄金样本是【从真日志里原样抄下来的】。
# ============================================================================

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$longRun = Join-Path $PSScriptRoot 'long_run.ps1'
if (-not (Test-Path $longRun)) { Write-Output "找不到 long_run.ps1: $longRun"; exit 1 }

# ---- 从 long_run.ps1 里抠出判据本体(原样,不复制实现) --------------------
$src = Get-Content -LiteralPath $longRun -Raw -Encoding UTF8

function Extract-Def([string] $Text, [string] $Name) {
    # 函数体是缩进的,所以"行首的 }"就是函数结束位置
    $m = [regex]::Match($Text, "(?s)function\s+$Name\s*\{.*?\r?\n\}\r?\n")
    if (-not $m.Success) { throw "在 long_run.ps1 里找不到函数 $Name —— 判据改名了?这个脚本要一起改。" }
    return $m.Value
}
function Extract-Var([string] $Text, [string] $Name) {
    $m = [regex]::Match($Text, "(?m)^\`$$Name\s*=.*$")
    if (-not $m.Success) { throw "在 long_run.ps1 里找不到变量 `$$Name —— 判据改了?这个脚本要一起改。" }
    return $m.Value
}
foreach ($fn in @('Get-PollStats', 'Get-LivenessVerdict', 'Get-UnformattedPlaceholders')) { Invoke-Expression (Extract-Def $src $fn) }
Invoke-Expression (Extract-Var $src 'noiseRe')
# ★ 2026-10-21:Get-PollStats 里的正则改成用脚本级的 $rePollStats
#   (启动自检和 SOAK-6 共用同一份 —— 见 long_run.ps1 里的说明)。
#   所以这个变量也得一起抠出来,否则抠出来的函数跑起来会拿到空正则,
#   表现是"一条统计行都匹配不上" —— 那是判据失灵,不是用例失败。
Invoke-Expression (Extract-Var $src 'rePollStats')

# ---- 造日志行(格式跟 Get-LogSince 产出的对象一样)------------------------
function L([string] $msg, [string] $cat = 'hmi.app') {
    return [pscustomobject]@{ Time = (Get-Date); Level = 'Info'; Cat = $cat; Msg = $msg }
}

# ★★★ 黄金样本:从【真日志】里原样抄下来的那一行 ★★★
#   出处(2026-10-21 16:06:32,自建从站 127.0.0.1:1502 那一轮):
#     soaks\20261021_160531\appdata\QianWei\HMI\logs\hmi_20261021_160532.log
#       2026-10-21 16:06:32.216 [INFO ] hmi.poll - [PollScheduler] 轮询统计:发出= 271  成功= 271  失败= 0  在途= 0  PLC= 1  变量= 3
#   ★ 特征:等号后【有】空格,字段之间【两个】空格。
#     空格哪来的:QDebug 在相邻两项之间插一个(等号后那个),
#     字段之间那两个是字面量 " 成功=" 自带的前导空格 + QDebug 插的那个。
#   ★ 抄的时候【不许"顺手整理"成没有空格的版本】——
#     整理过的样本就又变回"自己想象的格式"了,这个脚本就又回到自欺欺人。
$goldenRealLine = '[PollScheduler] 轮询统计:发出= 271  成功= 271  失败= 0  在途= 0  PLC= 1  变量= 3'

# 旧格式(2026-10-21 之前编译的二进制):等号后有空格。
#   —— 判据必须对【它】也有效:那台机器上还挂着一个旧二进制在跑。
function StatLine([int] $sent, [int] $ok, [int] $fail, [int] $inflight, [int] $plc = 1) {
    return L "[PollScheduler] 轮询统计:发出= $sent  成功= $ok  失败= $fail  在途= $inflight  PLC= $plc  变量= 3" 'hmi.poll'
}
# 新格式(2026-10-21 起):整行拼成一个字符串再输出,等号后【没有】空格。
function StatLineNew([int] $sent, [int] $ok, [int] $fail, [int] $inflight, [int] $plc = 1) {
    return L "[PollScheduler] 轮询统计:发出=$sent 成功=$ok 失败=$fail 在途=$inflight PLC=$plc 变量=3" 'hmi.poll'
}
function Heartbeat([int] $n) {
    return L "[HistoryRecorder] 心跳写了 $n 行" 'hmi.history'
}

# ---- 旧判据(照抄主干里那段逻辑,只用于回归对照)--------------------------
function Get-LegacyLiveness([array] $Lines) {
    $noiseLines = @($Lines | Where-Object { $_.Msg -match $noiseRe })
    $bizLines   = @($Lines | Where-Object { $_.Msg -notmatch $noiseRe })
    return [pscustomobject]@{
        BizLines  = $bizLines.Count
        NoiseLines = $noiseLines.Count
        LogGrew   = ($Lines.Count -gt 0)                 # 旧"资格"前提:日志在长
        IsDormant = ($bizLines.Count -eq 0 -and $noiseLines.Count -gt 0)
    }
}

$red = 0; $ok = 0
function Check($name, $cond, $detail) {
    if ($cond) { Write-Host "  [OK]   $name  $detail" -ForegroundColor Green; $script:ok++ }
    else       { Write-Host "  [RED]  $name  $detail" -ForegroundColor Red;   $script:red++ }
}

Write-Host "`n=== 用例 0:真实日志的黄金样本 —— 判据读得懂真日志吗 ===" -ForegroundColor Cyan
#   这一条是【整个脚本的地基】。它绿,后面几条才有意义;
#   它红 —— 哪怕下面全绿,也是"判据在拿自己想象的格式自娱"。
$golden = @(L $goldenRealLine 'hmi.poll')
$stg = Get-PollStats -Lines $golden
$vg  = Get-LivenessVerdict -Stats $stg -IsFrozen $false
Write-Host "  样本: $goldenRealLine"
Write-Host "  → 统计行 $($stg.N) 条;发出 $($stg.SentTotal) 成功 $($stg.OkTotal) 失败增量 $($stg.FailDelta) 在途 $($stg.Inflight)"
Check "黄金样本能被匹配到" ($stg.N -eq 1) "统计行 $($stg.N) 条 —— 匹配不上就是正则和现实对不上"
Check "发出读对了"     ($stg.SentTotal -eq 271) "实际 $($stg.SentTotal)"
Check "成功读对了"     ($stg.OkTotal   -eq 271) "实际 $($stg.OkTotal)"
Check "在途读对了"     ($stg.Inflight  -eq 0)   "实际 $($stg.Inflight)"

Write-Host "`n=== 用例 1:健康轮询(成功读计数在涨) ===" -ForegroundColor Cyan
$lines = @()
1..10 | ForEach-Object { $lines += (StatLine ($_ * 10) ($_ * 10) 0 0); $lines += (Heartbeat 3) }
$st = Get-PollStats -Lines $lines
$v  = Get-LivenessVerdict -Stats $st -IsFrozen $false
Write-Host "  统计行 $($st.N) 条;发出 +$($st.SentDelta) 成功 +$($st.OkDelta) 在途 $($st.Inflight)"
Write-Host "  → $($v.State) / AppDefect=$($v.AppDefect)"
Check "判 PASS" ($v.State -eq 'PASS') ""
Check "不是应用故障" (-not $v.AppDefect) ""
Check "成功增量算对了" ($st.OkDelta -eq 90) "实际 $($st.OkDelta)"

Write-Host "`n=== 用例 2:从站不回包(发出在涨、成功不涨) ===" -ForegroundColor Cyan
$lines = @()
1..10 | ForEach-Object { $lines += (StatLine ($_ * 10) 0 ($_ * 10) 0) }   # 全失败,在途已清(修好之后会重试)
$st = Get-PollStats -Lines $lines
$v  = Get-LivenessVerdict -Stats $st -IsFrozen $false
Write-Host "  发出 +$($st.SentDelta) 成功 +$($st.OkDelta) 失败 +$($st.FailDelta) 在途 $($st.Inflight)"
Write-Host "  → $($v.State) / AppDefect=$($v.AppDefect)"
Check "判 FAIL" ($v.State -eq 'FAIL') ""
Check "【不】算应用故障 —— 从站不回包是环境问题,归 exit 4" (-not $v.AppDefect) ""
Check "失败增量算对了" ($st.FailDelta -eq 90) "实际 $($st.FailDelta)"

Write-Host "`n=== 用例 3:应用卡死(发出不涨、在途不为 0) ===" -ForegroundColor Cyan
#   这就是 2026-10-21 查出的根因的现场形态:
#   一次协议级失败之后变量卡在在途态,再也发不出去 —— 发出数不再涨,在途不为 0。
$lines = @()
1..10 | ForEach-Object { $lines += (StatLine 30 30 0 3) }
$st = Get-PollStats -Lines $lines
$v  = Get-LivenessVerdict -Stats $st -IsFrozen $false
Write-Host "  发出 +$($st.SentDelta) 成功 +$($st.OkDelta) 在途 $($st.Inflight)"
Write-Host "  → $($v.State) / AppDefect=$($v.AppDefect)"
Check "判 FAIL" ($v.State -eq 'FAIL') ""
Check "【是】应用故障 —— 应用把自己锁死了,归 exit 1" ($v.AppDefect) ""
Check "在途数读出来了" ($st.Inflight -eq 3) "实际 $($st.Inflight)"

Write-Host "`n=== 用例 4:一条统计行都没有(跑的是 2026-10-21 之前的旧二进制) ===" -ForegroundColor Cyan
$lines = @()
1..10 | ForEach-Object { $lines += (Heartbeat 3) }
$st = Get-PollStats -Lines $lines
$v  = Get-LivenessVerdict -Stats $st -IsFrozen $false
Write-Host "  → $($v.State) / AppDefect=$($v.AppDefect)"
Check "判 UNKNOWN(判不出来,不下结论,归 exit 4)" ($v.State -eq 'UNKNOWN') ""
Check "不是应用故障 —— 没有证据就不指控应用" (-not $v.AppDefect) ""

Write-Host "`n=== 用例 5:回归·假红 —— 健康轮询在【旧二进制】下的日志形态 ===" -ForegroundColor Cyan
#   旧二进制没有"轮询统计"这一行,而协议层收发是 qCDebug(默认 INFO 一行都不落)
#   ⇒ 一个跑得好好的 24 小时,在旧判据眼里就是"业务类日志 0 条 = 只在自转 = FAIL"。
$oldBinaryHealthy = @()
1..10 | ForEach-Object { $oldBinaryHealthy += (Heartbeat 3) }
$leg = Get-LegacyLiveness -Lines $oldBinaryHealthy
Write-Host "  旧判据:业务类 $($leg.BizLines) 行 / 本地心跳 $($leg.NoiseLines) 行 ⇒ 只在自转=$($leg.IsDormant) ⇒ 判 FAIL"
Check "旧判据确实会把'业务跑得好好的'判成 FAIL(这就是它会否掉真跑法的那一刀)" $leg.IsDormant ""

Write-Host "`n=== 用例 6:回归·假绿 —— 2026-10-20 那 950.9 分钟的现场 ===" -ForegroundColor Cyan
#   现场:进程活着、CPU 低、日志在长(全被本地心跳撑起来的),旧脚本因此判"有资格验收" ⇒ PASS。
$idle = @()
1..10 | ForEach-Object { $idle += (Heartbeat 3) }
$leg = Get-LegacyLiveness -Lines $idle
$st2 = Get-PollStats      -Lines $idle
$v2  = Get-LivenessVerdict -Stats $st2 -IsFrozen $false
Write-Host "  旧判据:日志在长=$($leg.LogGrew) ⇒ 通过'静默期之后日志在长'的前提 ⇒ 本轮【有资格验收】"
Write-Host "  新判据:统计行 $($st2.N) 条,成功读增量 $($st2.OkDelta) ⇒ $($v2.State)"
Check "旧'资格'前提确实被骗过(日志在长 = 业务在跑)" $leg.LogGrew "心跳也能撑起这个前提"
Check "新判据不判 PASS —— 成功读没涨就不构成验收" ($v2.State -ne 'PASS') "实际 $($v2.State)"

Write-Host "`n=== 用例 7:新旧两种格式都判得出来 ===" -ForegroundColor Cyan
#   为什么要两条都测:机器上还挂着一个【旧二进制】(2026-10-21 之前编译的),
#   它的统计行等号后带空格;新二进制不带。
#   判据如果只认一种,碰上另一种就会判成 UNKNOWN ——
#   而 UNKNOWN 的含义是"这一轮没有结论",于是那轮长跑白跑。
#   两条都必须 PASS,才说明判据对【两个版本】都有效。
$oldFmt = @()
$newFmt = @()
1..10 | ForEach-Object {
    $oldFmt += (StatLine    ($_ * 10) ($_ * 10) 0 0)
    $newFmt += (StatLineNew ($_ * 10) ($_ * 10) 0 0)
}
$stOld = Get-PollStats -Lines $oldFmt
$stNew = Get-PollStats -Lines $newFmt
$vOld  = Get-LivenessVerdict -Stats $stOld -IsFrozen $false
$vNew  = Get-LivenessVerdict -Stats $stNew -IsFrozen $false
Write-Host "  旧格式(等号后有空格):统计行 $($stOld.N) 条 成功 +$($stOld.OkDelta) ⇒ $($vOld.State)"
Write-Host "  新格式(等号后无空格):统计行 $($stNew.N) 条 成功 +$($stNew.OkDelta) ⇒ $($vNew.State)"
Check "旧格式判 PASS" ($vOld.State -eq 'PASS') "实际 $($vOld.State)"
Check "新格式判 PASS" ($vNew.State -eq 'PASS') "实际 $($vNew.State)"
Check "两种格式读出的成功增量一致" ($stOld.OkDelta -eq $stNew.OkDelta) "$($stOld.OkDelta) vs $($stNew.OkDelta)"

Write-Host "`n=== 用例 8:格式漂移 —— 判据要【判不出】,而不是【判错】 ===" -ForegroundColor Cyan
#   如果哪天有人把程序里的分隔符从 "=" 改成 ":"(或者改了字段名),
#   判据必须老老实实说"我读不出来"(UNKNOWN),而不是拿着默认值乱判。
#   ★ 这条钉的是"失配要【响】"这个性质 ——
#     2026-10-21 那次失配之所以能潜伏那么久,就是因为它【不响】:
#     判据默默判不出,报告上写着"没资格跑",读的人以为是程序的问题。
$drift = @(L '[PollScheduler] 轮询统计:发出: 271  成功: 271  失败: 0  在途: 0  PLC: 1' 'hmi.poll')
$stD = Get-PollStats -Lines $drift
$vD  = Get-LivenessVerdict -Stats $stD -IsFrozen $false
Write-Host "  漂移后的行匹配到 $($stD.N) 条 ⇒ $($vD.State)"
Check "匹配不上统计行" ($stD.N -eq 0) "匹配了 $($stD.N) 条 —— 正则太松,漂移了也照样判"
Check "判 UNKNOWN(不乱下结论)" ($vD.State -eq 'UNKNOWN') "实际 $($vD.State)"
Check "不是应用故障 —— 判不出证据时不指控应用" (-not $vD.AppDefect) ""

Write-Host "`n=== 用例 9:判据的每一句结论里,数字【必须真的在】 ===" -ForegroundColor Cyan
#   ★ 为什么要有这一条
#   ------------------
#   2026-10-21 的冒烟跑完,verdict.txt 里 SOAK-6 那一行是这么写的:
#       ★ 成功读 +{0} 次 —— 业务真的在跑,判定资格成立。
#   {0} 没被替换 —— 写那一行的人少写了一个 -f。
#   它没有报错、没有红、判据照常判了 PASS。而这行字的全部作用
#   就是把"业务真的在跑"【量化】给读报告的人看 —— 数字没了,它就退化成一句自我声明,
#   恰好是它本该避免的东西。
#   ★ 这和"轮询统计行格式对不上"是同一类病、同一个教训:
#       判据坏掉的样子,和被测物坏掉的样子,一模一样。
#   ⇒ 判据的每一句结论,数字都必须在。
#
#   检测函数 Get-UnformattedPlaceholders 也是从 long_run.ps1 里【原样抠出来的】,
#   跟落盘前那道守卫用的是同一份 —— 不复制实现,免得两份各说各话。
$branches = @(
    @{ Name = 'PASS(业务在跑)';  Lines = (1..10 | ForEach-Object { StatLine ($_ * 10) ($_ * 10) 0 0 }) }
    @{ Name = 'PASS(带失败)';    Lines = (1..10 | ForEach-Object { StatLine ($_ * 10) ($_ * 10) 1 0 }) }
    @{ Name = 'FAIL(从站不回包)'; Lines = (1..10 | ForEach-Object { StatLine ($_ * 10) 0 ($_ * 10) 0 }) }
    @{ Name = 'FAIL(应用卡死)';  Lines = (1..10 | ForEach-Object { StatLine 30 30 0 3 }) }
    @{ Name = 'UNKNOWN(读不出)'; Lines = (1..10 | ForEach-Object { Heartbeat 3 }) }
)
foreach ($b in $branches) {
    $stB = Get-PollStats -Lines @($b.Lines)
    $vB  = Get-LivenessVerdict -Stats $stB -IsFrozen $false
    $leak = @(Get-UnformattedPlaceholders -Text $vB.Reason)
    Write-Host "  $($b.Name) ⇒ $($vB.State)"; Write-Host "    $($vB.Reason -replace "`r?`n", ' ')"
    Check "$($b.Name):结论里没有漏掉的占位符" ($leak.Count -eq 0) ("漏了: " + ($leak -join ', '))
}
# ★ PASS 这一支还要额外钉住:数字【得是真的那个数】,不是"有个数字就行"。
$stP = Get-PollStats -Lines @((1..10 | ForEach-Object { StatLine ($_ * 10) ($_ * 10) 0 0 }))
$vP  = Get-LivenessVerdict -Stats $stP -IsFrozen $false
Check "PASS 的结论里写的是真的那个数(90)" ($vP.Reason -match '\+\s*90\s*次') "实际:$($vP.Reason -replace "`r?`n", ' ')"
Check "PASS 的结论里没有 {0} 这种占位符" ($vP.Reason -notmatch '\{\d+\}') ""

Write-Host "`n=== 小计: OK=$ok  RED=$red ===" -ForegroundColor $(if ($red -eq 0) { 'Green' } else { 'Red' })
if ($red -gt 0) { exit 1 }
exit 0