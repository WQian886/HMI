# ============================================================================
# long_run.ps1 判据的负向对照(2026-10-14)
#
# 拿【第一轮真实采到的 18 个样本】重放两遍判据,证明:
#   1. 改之前的判据会给 idle 进程报 PASS(假绿)
#   2. 改之后的判据会把同一份数据报成 UNKNOWN + 退出码 4(没结论)
#   3. 把 MinCpuPercent 拧到 0,UNKNOWN 必须消失、回到 PASS —— 证明它会红,
#      一个拧到底都不变的判据等于没有判据
#   4. 人工注入一条线性增长的私有内存,判据必须转 WARN/FAIL —— 证明它
#      真的能抓到泄漏,而不是只会报"没涨"
#
# 用第一轮的 samples.csv 而不是现编的数字:那条曲线是实测的,里面有
# 工作集中途掉 13.6 MB、句柄中途涨 4 个这些真实形状,编的数字不会有。
# ============================================================================

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$csv = Join-Path $PSScriptRoot '..\soaks\20261014_110219\samples.csv'
if (-not (Test-Path $csv)) { Write-Output "找不到第一轮样本: $csv"; exit 1 }

$samples = @(Import-Csv $csv | ForEach-Object {
    [pscustomobject]@{
        WorkingSetMB = [double]$_.WorkingSetMB
        PrivateMB    = [double]$_.PrivateMB
        Handles      = [int]$_.Handles
        Threads      = [int]$_.Threads
        CpuSeconds   = [double]$_.CpuSeconds
        RunMinutes   = [double]$_.RunMinutes
    }
})
$first = $samples[0]
$lastN = $samples[$samples.Count - 1]
$spanMin = [math]::Max(1.0, $lastN.RunMinutes - $first.RunMinutes)

$MaxMemRatio = 2.0; $MaxMemPerHour = 20.0; $WarnMemRatio = 1.3
$MaxHandleDelta = 50; $WarnHandleDelta = 10; $MinCpuPercent = 5.0

function Judge($s, $minCpu) {
    $memRatio = if ($s[0].PrivateMB -gt 0) { $s[$s.Count-1].PrivateMB / $s[0].PrivateMB } else { 0 }
    $memPerHr = ($s[$s.Count-1].PrivateMB - $s[0].PrivateMB) / ($spanMin / 60.0)
    $cpuPct = if ($spanMin -gt 0) { (($s[$s.Count-1].CpuSeconds - $s[0].CpuSeconds) / ($spanMin * 60.0)) * 100.0 } else { 0 }
    $isIdle = ($cpuPct -lt $minCpu)

    $mem = if ($isIdle) { 'UNKNOWN' }
           elseif ($memRatio -gt $MaxMemRatio -and $memPerHr -gt $MaxMemPerHour) { 'FAIL' }
           elseif ($memRatio -gt $WarnMemRatio) { 'WARN' } else { 'PASS' }

    $hdlMax = ($s | Measure-Object Handles -Maximum).Maximum
    $hdlPeak = $hdlMax - $s[0].Handles
    $hdl = if ($isIdle) { 'UNKNOWN' }
           elseif ($hdlPeak -gt $MaxHandleDelta) { 'FAIL' }
           elseif ($hdlPeak -gt $WarnHandleDelta) { 'WARN' } else { 'PASS' }
    [pscustomobject]@{ Mem=$mem; Hdl=$hdl; CpuPct=[math]::Round($cpuPct,2); Idle=$isIdle
                       Ratio=[math]::Round($memRatio,4); PerHr=[math]::Round($memPerHr,2)
                       HdlPeak=$hdlPeak }
}

$red = 0; $ok = 0
function Check($name, $cond, $detail) {
    if ($cond) { Write-Host "  [OK]   $name  $detail" -ForegroundColor Green; $script:ok++ }
    else       { Write-Host "  [RED]  $name  $detail" -ForegroundColor Red;   $script:red++ }
}

Write-Host "`n=== 样本(第一轮实测 $($samples.Count) 点 / $([math]::Round($spanMin,1)) 分钟) ===" -ForegroundColor Cyan
Write-Host "  私有内存 首$($first.PrivateMB) 尾$($lastN.PrivateMB) MB"
Write-Host "  工作集   首$($first.WorkingSetMB) 尾$($lastN.WorkingSetMB) MB"
Write-Host "  句柄     首$($first.Handles) 尾$($lastN.Handles) 峰$(($samples|Measure-Object Handles -Maximum).Maximum)"
Write-Host "  CPU      首$($first.CpuSeconds) 尾$($lastN.CpuSeconds) s"

Write-Host "`n=== 对照 1:【改之前】的老判据会说什么 ===" -ForegroundColor Cyan
$oldRatio = $lastN.WorkingSetMB / $first.WorkingSetMB
$oldPerHr = ($lastN.WorkingSetMB - $first.WorkingSetMB) / ($spanMin/60.0)
$oldNet   = $lastN.Handles - $first.Handles
$oldMem = if ($oldRatio -gt $MaxMemRatio -and $oldPerHr -gt $MaxMemPerHour) {'FAIL'}
          elseif ($oldRatio -gt $WarnMemRatio) {'WARN'} else {'PASS'}
Write-Host "  工作集斜率 $([math]::Round($oldPerHr,1)) MB/小时;句柄首尾差 $oldNet"
Write-Host "  → SOAK-2 = $oldMem,SOAK-3 = PASS,退出码 0" -ForegroundColor Yellow
Check "老判据确实报了 PASS(假绿)" ($oldMem -eq 'PASS' -and $oldNet -eq 0) ""
Write-Host "  ↑ 这就是第一轮那个 PASS。它没测错数,是【测的根本不是该测的量】。" -ForegroundColor DarkGray

Write-Host "`n=== 对照 2:【改之后】同一份数据 ===" -ForegroundColor Cyan
$r = Judge $samples $MinCpuPercent
Write-Host "  CPU $([math]::Round($r.CpuPct,2))% (阈值 $MinCpuPercent%)  idle=$($r.Idle)"
Write-Host "  私有内存比 $($r.Ratio)x  斜率 $($r.PerHr) MB/h  句柄峰值净增 $($r.HdlPeak)"
Write-Host "  → SOAK-2 = $($r.Mem),SOAK-3 = $($r.Hdl)"
Check "idle 被检出" $r.Idle ""
Check "SOAK-2 降级为 UNKNOWN" ($r.Mem -eq 'UNKNOWN') ""
Check "SOAK-3 降级为 UNKNOWN" ($r.Hdl -eq 'UNKNOWN') ""
Check "句柄峰值被报出(首尾差是 0)" ($r.HdlPeak -eq 4) "峰值净增=$($r.HdlPeak)"

Write-Host "`n=== 对照 3:把 MinCpuPercent 拧到 0 —— 判据必须会红 ===" -ForegroundColor Cyan
$r0 = Judge $samples 0
Check "拧到 0 后 UNKNOWN 消失" ($r0.Mem -ne 'UNKNOWN' -and $r0.Hdl -ne 'UNKNOWN') "SOAK-2=$($r0.Mem) SOAK-3=$($r0.Hdl)"
Check "且回到 PASS" ($r0.Mem -eq 'PASS' -and $r0.Hdl -eq 'PASS') ""
Write-Host "  ↑ 证明这条判据是被阈值控制的,不是恒为 UNKNOWN。" -ForegroundColor DarkGray

Write-Host "`n=== 对照 4:注入私有内存线性增长 —— 必须能抓到泄漏 ===" -ForegroundColor Cyan
$leak = @()
foreach ($s in $samples) {
    $k = [array]::IndexOf($samples, $s)
    $leak += [pscustomobject]@{
        PrivateMB  = [math]::Round($s.PrivateMB + ($k * 3.0), 2)   # 每点 +3MB,18 点 = +54MB
        WorkingSetMB = $s.WorkingSetMB; Handles = $s.Handles; Threads = $s.Threads
        CpuSeconds = $s.CpuSeconds + ($k * 60.0 * 0.25)              # CPU 拉满,排除 idle 干扰
    }
}
$rl = Judge $leak 5.0
Write-Host "  注入后:私有内存 $($leak[0].PrivateMB) -> $($leak[$leak.Count-1].PrivateMB) MB"
Write-Host "  → SOAK-2 = $($rl.Mem)(比 $($rl.Ratio)x,斜率 $($rl.PerHr) MB/h)"
Check "泄漏被判 WARN 或 FAIL" ($rl.Mem -eq 'WARN' -or $rl.Mem -eq 'FAIL') ""
Check "且不是被 idle 误降级" (-not $rl.Idle) ""

Write-Host "`n=== 小计: OK=$ok  RED=$red ===" -ForegroundColor $(if($red -eq 0){'Green'}else{'Red'})
if ($red -gt 0) { exit 1 }
exit 0