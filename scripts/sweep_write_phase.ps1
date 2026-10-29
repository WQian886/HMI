# ============================================================================
# sweep_write_phase.ps1 —— 写 starvation 修复(BUGFIX-E1)的【相位无关性】扫描
#
#  为什么要有这个脚本(它不是 24h 长跑的缩水版,是另一件事)
#  ----------------------------------------------------------
#  ★ BUGFIX-E1(2026-10-26):写和读共用同一个在途槽,
#    槽被读占着的时候写【被直接拒收】。
#
#  ★ 为什么"一个配置跑绿了"证明不了修好了:
#    修之前实测(soaks/sweep_m85,2026-10-26):604 次写尝试【全部】被拒、
#    FC6 写帧 0 条、[Write] OK 行 0 条 —— 而读照常成功(轮询统计 发出=547 成功=547)。
#    而同样参数、另六轮(sweep_m70/m88/m92/m95/m97/m99)写【一次都没被拒】。
#    ⇒ 决定成败的是两个 100ms 节拍的【启动相位差】,那个相位差一辈子不变。
#    ⇒ 掷到坏的那一面,整轮(24 小时也是)一个字节都写不出去,
#      而读照常成功 ⇒ 长跑的活体判据【照样绿】。
#
#  ★ 所以判据必须是【跨相位都成立】,而且必须看【写有没有落地】:
#    从站日志里的 FC6 写帧条数 > 0、应用日志里的 [Write] OK 行数 > 0。
#    只要有一轮落地 0 条,就说明相位还是决定因素 ⇒ 没修好。
#
#  ★ 为什么 J10(守卫命中)【不】参与这个脚本的判定:
#    J10 量的是"这一轮有没有撞上 G-16 那个竞态",跟本脚本要验的不是一件事。
#    ★ 而且修之前 m85 那轮的"604 次守卫命中"【就是 starvation 的症状本身】——
#      写永远发不出去 ⇒ dirty 永远清不掉 ⇒ 每次轮询都命中守卫。
#    ⇒ 修之后守卫命中会变少(甚至 0),这【是好事】,不是回归。
#      本脚本【只记录】它,不拿它当判据。
#
#  ★ 为什么同一个 m 值要【重复】跑好几遍:
#    相位是【进程启动那一刻】定下来的,不同的 m 只是在换参数,
#    真正换相位靠的是"每次都是新起的进程"。
#    ⇒ 同一个 m 重复跑 N 遍 = 换 N 次相位。
#    ⇒ 下面默认:扫一遍 5 个 m,再把 m=85 【重复】2 遍(那一档是修之前暴雷的那一档)。
#
#  用法:
#     powershell -File scripts\sweep_write_phase.ps1 -Minutes 2
#     powershell -File scripts\sweep_write_phase.ps1 -Minutes 2 -MValues 85 -Repeats 5
# ============================================================================
[CmdletBinding()]
param(
    [int]    $Minutes  = 2,
    [int]    $PollMs   = 50,
    [double] $WavePeriod = 8,
    [string] $MValues  = '70,85,90,95,97',
    [int]    $Repeats  = 2,          # ★ 额外把每一个 m 再跑几遍(换相位)
    [int]    $BasePort = 1510,       # ★ 每轮换端口 —— 上一轮的从站刚被杀会留 TIME_WAIT
    [int]    $ReplyDelayMs = 0,     # ★ 从站回包延迟(ms)—— 见下面那段说明
    [string] $Tag      = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = (Resolve-Path (Join-Path $here '..')).Path

if (-not $Tag) { $Tag = (Get-Date -Format 'yyyyMMdd_HHmmss') }
$root = Join-Path $repo ("soaks\sweep_write_phase_{0}" -f $Tag)
New-Item -ItemType Directory -Force -Path $root | Out-Null
$summary = Join-Path $root 'summary.txt'

function Say($msg, $color = 'Gray') {
    Write-Host ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $msg) -ForegroundColor $color
}

# ---- 跑一轮 ---------------------------------------------------------------
#  返回一个 hashtable:全部指标
function Invoke-OneRun {
    param([int]$M, [int]$Index, [int]$Port)

    $out = Join-Path $root ("m{0}_{1}" -f $M, $Index)
    # ★ 目录必须先建好 —— *> 重定向在目标目录不存在时会直接抛
    #   "Could not find a part of the path"(第一版就栽在这儿)。
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    $log = Join-Path $out 'run.log'
    Say ("--- m={0} 第 {1} 遍 (端口 {2}) ---" -f $M, $Index, $Port) 'Cyan'

    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'short_action_run.ps1') `
        -Minutes $Minutes -PollMs $PollMs -AlarmMinMs $M -WavePeriod $WavePeriod `
        -ReplyDelayMs $ReplyDelayMs -SlavePort $Port -OutDir $out *> $log

    $verdictPath = Join-Path $out 'verdict.txt'
    $text = if (Test-Path $verdictPath) { Get-Content $verdictPath -Raw -Encoding UTF8 } else { '' }

    # ---- 证据 A:从站日志里的 FC6 写帧(唯一不能被应用自己骗的证据) ----
    $slaveLog = Join-Path $out 'slave.log'
    $fc6 = 0
    $fc16 = 0
    $slaveLines = @()
    if (Test-Path $slaveLog) {
        $slaveLines = @(Get-Content $slaveLog -Encoding UTF8)
        $fc6  = @($slaveLines | Where-Object { $_ -match 'FC6 写' }).Count
        $fc16 = @($slaveLines | Where-Object { $_ -match 'FC16 写' }).Count
    }

    # ---- 证据 B:应用日志(在 <OutDir>\appdata\QianWei\HMI\logs\*.log)----
    #   ★ 路径必须对:第一版这个脚本在 <OutDir> 根下找 *.log,
    #     找到的是 hmi.stderr.txt,于是"拒收"计数永远是 0 —— 判据自己瞎了。
    $appLogDir = Join-Path $out 'appdata\QianWei\HMI\logs'
    $appFiles = @(Get-ChildItem $appLogDir -Filter '*.log' -File -ErrorAction SilentlyContinue)
    $appText = ''
    foreach ($f in $appFiles) {
        try { $appText += (Get-Content $f.FullName -Raw -Encoding UTF8) } catch { }
    }
    $lines = if ($appText) { @($appText -split "`r?`n") } else { @() }
    # ★ 计数必须逐行数,不能把整个数组丢给 Select-String -InputObject ——
    #   那样它会把数组【拼成一个字符串】再匹配,数出来的是"文件里有几处",
    #   不是"有几行"。(第一版就是这么写的,数字看着像那么回事,其实是错的。)
    $countOf = {
        param($pat)
        if (-not $lines) { return 0 }
        $n = 0
        foreach ($l in $lines) { if ($l -match $pat) { $n++ } }
        return $n
    }
    # 拒收(修之前的症状):WriteScheduler 说 driver 拒收了写请求
    $rejected  = & $countOf '的 driver 拒收了'
    # 队列满(修之后【唯一】还能拒收的理由)—— 出现即说明写又堵住了
    $queueFull = & $countOf '写队列已满'
    # 排队(修之后应该大量出现 —— 证明写【在等】而不是【被拒】)
    $queued    = & $countOf '写请求【排队】'
    # 写成功(应用自己的记账)
    $okLines   = & $countOf '\[Write\]\s+OK:'
    # 发写(尝试)
    $sendLines = & $countOf '\[WriteScheduler\] 发写'
    # ★★ 从站自己有没有崩 ★★
    #   2026-10-27 踩到:扫描跑到一半,我改了 test_slave.py,
    #   正好改在"改了 dispatch()"和"改了 Slave.__init__()"之间,
    #   于是扫描起了一个【半改过的】从站 ⇒ 从站每个请求都抛
    #   AttributeError ⇒ 那一轮 FC6 写帧 0 条。
    #   ★ 而那一轮长得【跟"产品坏了"一模一样】——
    #   所以必须把"脚手架自己坏了"跟"被测程序坏了"分开,
    #   否则下一个人会去改一个没坏的产品。
    #   (从站的异常处理是"记一行、继续",所以它不会崩,
    #    只会一直不回答 —— 这恰恰是最容易误判的一种坏法。)
    $slaveErrors = @($slaveLines | Where-Object { $_ -match '处理请求时出错|Traceback' }).Count
    # 守卫命中(只记录,不当判据)
    $guard     = & $countOf '跳过本次读回覆盖'
    # 轮询统计最后一条
    $pollLast  = ''
    foreach ($l in $lines) { if ($l -match '轮询统计') { $pollLast = $l } }

    $j3    = if ($text -match 'J3 .*?值不等于期望值的写帧\s+(\d+)\s*/\s*(\d+)') { "$($Matches[1])/$($Matches[2])" } else { '?' }
    $j8    = if ($text -match 'J8 写次数跟得上联动次数: 联动\s+(\d+)\s*次\s*/\s*写\s+(\d+)\s*次') { "$($Matches[1])/$($Matches[2])" } else { '?' }
    $j10   = if ($text -match '守卫命中\s+(\d+)') { $Matches[1] } else { '?' }
    $green = if ($text -match '结论:【绿】') { '绿' } elseif ($text -match '结论:【红】') { '红' } else { '?' }

    return [pscustomobject]@{
        m = $M; 遍 = $Index; 端口 = $Port
        FC6写帧 = $fc6; FC16写帧 = $fc16
        发写 = $sendLines; OK = $okLines
        拒收 = $rejected; 队列满 = $queueFull; 排队 = $queued
        从站报错 = $slaveErrors
        J3 = $j3; J8 = $j8; 守卫命中 = $j10; 整轮 = $green
        轮询末条 = $pollLast
    }
}

# ---- 主流程 ---------------------------------------------------------------
$rows = @()
$port = $BasePort
# ★ MValues 用字符串收，是为了兼容 `powershell -File xxx.ps1 -MValues 70,85,90`
#   的调用方式 —— 那样传进来的是一个【字符串】,"70,85,90" 直接转 int[] 会炸。
#   (第一版用 [int[]],从命令行传就是上面那个 ArgumentTransformationError。)
$mList = @()
foreach ($tok in ($MValues -split ',')) {
    $t = $tok.Trim()
    if ($t -ne '') { $mList += [int]$t }
}
if ($mList.Count -eq 0) { Say "MValues 是空的 —— 至少给一个 m。" 'Red'; exit 2 }

Say ("扫描开始: m = {0}, 每档 {1} 遍, 每轮 {2} 分钟, 起始端口 {3}" -f `
     ($mList -join ','), (1 + $Repeats), $Minutes, $BasePort) 'Cyan'

foreach ($m in $mList) {
    for ($i = 1; $i -le (1 + $Repeats); $i++) {
        $r = Invoke-OneRun -M $m -Index $i -Port $port
        $port++
        $rows += $r
        Say ("    ⇒ FC6写帧={0} OK={1} 拒收={2} 队列满={3} 排队={4} 从站报错={5} J3={6} J8={7} 守卫={8} 整轮={9}" -f `
             $r.FC6写帧, $r.OK, $r.拒收, $r.队列满, $r.排队, $r.从站报错, $r.J3, $r.J8, $r.守卫命中, $r.整轮) 'White'
    }
}

# ---- 汇总 -----------------------------------------------------------------
Say "================= 扫描汇总 =================" 'Cyan'
$rows | Format-Table m,遍,FC6写帧,发写,OK,拒收,队列满,排队,从站报错,J3,J8,守卫命中,整轮 -AutoSize |
    Out-String | Write-Host

# ★ 判据:【每一轮】写都必须落地。
#   FC6 写帧 0 条  ⇒ 写一个字节都没出去 ⇒ 相位还是决定因素 ⇒ 没修好。
#   FC6 写帧 > 0 但 [Write] OK 行 0 条 ⇒ 帧出去了但应用没记成功 ⇒ 记账链断了,也是坏。
#
# ★★ 但先要把【脚手架自己坏了】的轮次【剔出去】★★
#   从站自己抛异常时,那一轮也会"FC6 写帧 0 条"——
#   ★ 而这跟"产品坏了"长得【一模一样】。
#   ⇒ 这类轮次【不算产品的问题】,但也【绝不能当成"没问题"】——
#     它【什么都没验到】。所以:剔出统计,并在汇总里点名。
$harnessBroken = @($rows | Where-Object { $_.从站报错 -gt 0 })
$bad = @($rows | Where-Object { $_.从站报错 -eq 0 -and $_.FC6写帧 -le 0 })
$badOk = @($rows | Where-Object { $_.从站报错 -eq 0 -and $_.FC6写帧 -gt 0 -and $_.OK -le 0 })
$queueFullRuns = @($rows | Where-Object { $_.从站报错 -eq 0 -and $_.队列满 -gt 0 })
$queuedRuns = @($rows | Where-Object { $_.从站报错 -eq 0 -and $_.排队 -gt 0 })
$totalQueued = ($rows | Measure-Object -Property 排队 -Sum).Sum

$lines = @()
$lines += "写 starvation 相位无关性扫描 (BUGFIX-E1 修复后)"
$lines += ("tag={0}  m={1}  每档 {2} 遍  每轮 {3} 分钟  poll={4}ms  wave={5}s  回包延迟={6}ms" -f `
            $Tag, ($mList -join ','), (1 + $Repeats), $Minutes, $PollMs, $WavePeriod, $ReplyDelayMs)
$lines += ""
$lines += ("{0,-4} {1,-3} {2,-9} {3,-6} {4,-5} {5,-6} {6,-7} {7,-6} {8,-9} {9,-7} {10,-6} {11,-8} {12}" -f `
            'm','遍','FC6写帧','发写','OK','拒收','队列满','排队','从站报错','J3','J8','守卫命中','整轮')
foreach ($r in $rows) {
    $lines += ("{0,-4} {1,-3} {2,-9} {3,-6} {4,-5} {5,-6} {6,-7} {7,-6} {8,-9} {9,-7} {10,-6} {11,-8} {12}" -f `
                $r.m, $r.遍, $r.FC6写帧, $r.发写, $r.OK, $r.拒收, $r.队列满, $r.排队, $r.从站报错, $r.J3, $r.J8, $r.守卫命中, $r.整轮)
}
$lines += ""
$lines += "★★ 先分清:哪些轮次【根本没验到】★★"
$lines += ("  · 从站自己报错的轮次(脚手架坏了,不是产品坏了): {0}" -f $harnessBroken.Count)
foreach ($r in $harnessBroken) {
    $lines += ("      m={0} 第 {1} 遍 —— 从站报错 {2} 行 ⇒ 那一轮【什么都没验到】" -f `
                $r.m, $r.遍, $r.从站报错)
}
$validRuns = $rows.Count - $harnessBroken.Count
$lines += ("  · 有效轮次: {0} / {1}" -f $validRuns, $rows.Count)
$lines += ""
$lines += "★ 判据(只看写有没有落地,不看 J10 守卫命中 —— 见文件头):"
$lines += ("  · 写落地 0 条的轮次: {0}" -f $bad.Count)
$lines += ("  · 帧落地但 [Write] OK 行 0 条的轮次: {0}" -f $badOk.Count)
$lines += ("  · 出现『写队列已满』的轮次: {0}" -f $queueFullRuns.Count)
$lines += ""
$lines += "★★ 判据的有效性自检:排队通路【有没有被真的走到】★★"
$lines += ("  · 『写请求【排队】』总计: {0} 次,出现在 {1} / {2} 个有效轮次里" -f `
              $totalQueued, $queuedRuns.Count, $validRuns)
if ($totalQueued -le 0) {
    $lines += "  ★★ 【本轮没有证明什么】—— 排队那条通路【一次都没被走到】。"
    $lines += "     从站立即回包 ⇒ 在途槽几乎总是空的 ⇒ 写永远撞不上读。"
    $lines += "     ⇒ 『写落地』证明的是【没有撞车时写是好的】,"
    $lines += "       【不是】『撞车时写也能落地』—— 而后者才是 BUGFIX-E1 的核心。"
    $lines += "     ⇒ 要验那条,必须给从站加回包延迟(-ReplyDelayMs),故意制造撞车。"
} else {
    $lines += "  ★ 排队通路【被真的走到了】⇒ 『写落地』这个结论覆盖了撞车的情况。"
}
$lines += ""
if ($bad.Count -eq 0 -and $badOk.Count -eq 0) {
    $lines += "★ 结论:【绿】—— 每一个有效轮次写都落地了 ⇒ 相位不再是决定因素。"
} else {
    $lines += ("★ 结论:【红】—— 写落地 0 条 {0} 轮 / 记账断链 {1} 轮 ⇒ 相位还是决定因素,没修好。" -f `
                $bad.Count, $badOk.Count)
}
if ($harnessBroken.Count -gt 0) {
    $lines += ""
    $lines += ("★ 另有 {0} 轮因为【从站自己坏了】被剔出统计 —— 那几轮【什么都没验到】," -f `
                $harnessBroken.Count)
    $lines += "  【不能算绿】,也不能算产品的问题。要让它们有结论,重跑那一轮。"
}
$lines += ""
$lines += "★ 说明:『守卫命中』这一列【不参与判定】。"
$lines += "  修之前 m85 那轮 604 次守卫命中【就是 starvation 的症状本身】"
$lines += "  (写永远发不出去 ⇒ dirty 永远清不掉 ⇒ 每次轮询都命中守卫)。"
$lines += "  修之后它变少甚至为 0,是【好事】。"
$lines | Out-File -FilePath $summary -Encoding utf8
Say ("汇总已写: " + $summary) 'Cyan'
if ($bad.Count -ne 0 -or $badOk.Count -ne 0) { exit 1 }
exit 0
