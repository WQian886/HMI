<#
.SYNOPSIS
    复现「地址不连续 → 轮询永久停摆」这个 P0(2026-10-23,G-11)。

.DESCRIPTION
    这个脚本是 2026-10-23 那个 P0 的【复现配方】。
    它的存在理由不是"再跑一次看看",而是:
      将来有人把 ModbusMaster 的限流分支改回 emit errorOccurred,
      或者把 hmi.cpp 里那几处 modalAlertsEnabled() 守卫删了,
      【这个脚本要能立刻给出"红色证据"】,而不是靠回忆。

    ── 复现的是什么 ──────────────────────────────────────────────────────
    任何一个"变量地址不连续"的工程(跳过几个寄存器),都会让
    PollScheduler::onTick 把变量拆成多批发请求。第 2 批起撞上
    ModbusMaster 的单 in-flight 限流。修之前,限流会 emit errorOccurred,
    hmi.cpp 那边接到的是:
        modbusState_ = Failed  +  QMessageBox::warning(...)(模态阻塞)
    主线程就此卡在弹窗上 —— 轮询永久停摆,而外面看全是"正常"的假象:
      · 进程活着
      · CPU 很低
      · 内存平
      · 轮询统计行照常每 60 秒打印
      · 计数永远停在 发出=1 成功=1
      · 【零告警】—— 那条消息走 qCDebug,INFO 级别下一行都不落

    ── 怎么判"停摆了"而不是"跑得慢" ─────────────────────────────────────
    三个互相独立的证据,少一个都不算:
      ① 从站侧:70 秒里收到的请求数。停摆组 = 1;正常组 = 数百。
      ② 窗口侧:进程的全部顶层窗口里有没有【可见的】QMessageBox。
      ③ 日志侧:轮询统计行的"发出/成功"计数有没有在涨。

    ★ 曾经的坑 ★
    · P/Invoke 的 GetWindowTextW / GetClassNameW 必须写 CharSet = CharSet.Unicode,
      否则标题被截成第一个字母("M" 而不是 "Modbus 错误")——
      而"看到 M 就当成 QMessageBox"是【推断,不是证据】。窗口类名才是硬证据。
    · 窗口【总数】会变(6 vs 5),别拿总数下结论;
      稳定的判据是"A 组比 B 组多一个【可见】窗口"。

.PARAMETER Seconds
    跑多少秒。默认 70 —— 至少要够出一条 60 秒的轮询统计行。

.EXAMPLE
    .\scripts\repro_noncontiguous_stall.ps1
    .\scripts\repro_noncontiguous_stall.ps1 -Seconds 120
#>
[CmdletBinding()]
param(
    [int] $Seconds = 70
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $root 'hmi\out\build\debug2\hmi.exe'
$seed = Join-Path $root 'soaks\20261021_164804\appdata'
$out  = Join-Path $root ('memprobe\dlg_' + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force -Path $out | Out-Null

# ---- 前置检查 -------------------------------------------------------------
if (-not (Test-Path $exe))  { Write-Host "找不到 $exe" -ForegroundColor Red; exit 2 }
if (-not (Test-Path $seed)) { Write-Host "找不到种子数据目录 $seed" -ForegroundColor Red; exit 2 }

$sig = @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class Win {
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  public static List<string> ForPid(uint want) {
    var outp = new List<string>();
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p == want) {
        var t = new StringBuilder(512); GetWindowTextW(h, t, 512);
        var c = new StringBuilder(256); GetClassNameW(h, c, 256);
        outp.Add("  class=" + c.ToString() + "  visible=" + IsWindowVisible(h) + "  title=[" + t.ToString() + "]");
      }
      return true;
    }, IntPtr.Zero);
    return outp;
  }
}
'@
Add-Type -TypeDefinition $sig -Language CSharp

function MakeVars([int[]]$addrs) {
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine('  "variables": [')
    for ($i = 0; $i -lt $addrs.Count; $i++) {
        [void]$sb.AppendLine(("    {{ ""name"": ""V{0:D2}"", ""address"": {1}, ""data_type"": ""UInt16"", ""access"": ""ReadOnly"", ""poll_interval_ms"": 200, ""alwaysPoll"": true, ""deadband"": 0, ""range"": {{ ""min"": 0, ""max"": 65535 }}}}" -f $i, $addrs[$i]))
        if ($i -lt $addrs.Count - 1) { [void]$sb.Append(',') }
    }
    [void]$sb.AppendLine('  ],')
    $sb.ToString()
}

# A 组:地址不连续(0,2)→ 拆成 2 批 → 撞限流
# B 组:地址连续(0..15)→ 合批成 1 帧 → 不撞限流
$arms = @(
    @{ name='A2gap';  port=1530; addrs=@(0,2) },
    @{ name='B16ok'; port=1531; addrs=(0..15) }
)

$procs = @()
foreach ($a in $arms) {
    $proj = Join-Path $out "$($a.name).json"
    $json = "{`n  ""schema_version"": ""1.0"",`n  ""project_name"": ""$($a.name)"",`n  ""created_at"": ""2026-10-23T09:00:00"",`n  ""updated_at"": ""2026-10-23T09:00:00"",`n" + (MakeVars $a.addrs) + @"
  "alarms": [],
  "actions": [],
  "communications": { "host": "127.0.0.1", "port": $($a.port), "protocol": "modbus_tcp",
    "modbus": { "max_retries": 5, "poll_interval_ms": 200, "timeout_ms": 3000 } }
}
"@
    $json | Set-Content -Path $proj -Encoding UTF8

    $sp = Start-Process python -ArgumentList @('scripts/test_slave.py','--port',"$($a.port)",'--log',(Join-Path $out "slave_$($a.name).log")) -WorkingDirectory $root -PassThru -WindowStyle Hidden
    $procs += $sp

    $d = Join-Path $out "data_$($a.name)"
    Copy-Item $seed $d -Recurse
    $full = Join-Path $d 'QianWei\HMI'
    if (Test-Path (Join-Path $full 'history.db')) { Remove-Item (Join-Path $full 'history.db') -Force }

    $env:HMI_DATA_DIR     = $full
    $env:HMI_INSTANCE_KEY = "dlg-$($a.name)"
    $env:HMI_CRASH_DIR    = Join-Path $out "crash_$($a.name)"
    $hp = Start-Process $exe -ArgumentList @('--soak','--soak-user','3','--soak-pass','333333','--project',$proj) -WorkingDirectory $root -PassThru
    $procs += $hp
    Write-Host ("[{0}] port {1}  hmi pid {2}" -f $a.name, $a.port, $hp.Id)
}
Remove-Item Env:\HMI_DATA_DIR, Env:\HMI_INSTANCE_KEY, Env:\HMI_CRASH_DIR -ErrorAction SilentlyContinue

Write-Host "wait ${Seconds}s (long enough for one 60s stats line) ..."
Start-Sleep -Seconds $Seconds

# ---- 收证据 ---------------------------------------------------------------
$report = @()
foreach ($a in $arms) {
    $hp = $procs | Where-Object { $_.ProcessName -eq 'hmi' } | Select-Object -Skip ([array]::IndexOf($arms, $a)) -First 1
    Write-Host ("--- {0}  pid {1} ---" -f $a.name, $hp.Id)
    $ws = [Win]::ForPid([uint32]$hp.Id)
    if ($ws.Count -eq 0) { Write-Host "  (no top-level window at all)" }
    foreach ($w in $ws) { Write-Host $w }

    $lg = Get-ChildItem (Join-Path $out "data_$($a.name)\QianWei\HMI\logs\*.log") | Sort-Object LastWriteTime | Select-Object -Last 1
    $stat = Get-Content $lg.FullName -Encoding UTF8 | Select-String -Pattern ([char]0x8F6E + [char]0x8BE2 + [char]0x7EDF + [char]0x8BA1) -SimpleMatch | Select-Object -Last 1
    $slave = Join-Path $out "slave_$($a.name).log"
    $n = if (Test-Path $slave) { (Get-Content $slave | Measure-Object -Line).Lines } else { 0 }
    Write-Host ("  slave requests = {0}" -f $n)
    if ($stat) { Write-Host ("  " + ($stat.Line -replace '.*' + [char]0x8F6E, 'stats:')) }
    Write-Host ""

    $report += [pscustomobject]@{
        arm = $a.name
        slaveRequests = $n
        visibleBoxes = @($ws | Where-Object { $_ -match 'visible=True' -and $_ -match 'class=QMessageBox' }).Count
    }
}

Write-Host "---- 判据 ----" 'Cyan'
$report | Format-Table -AutoSize | Out-String | Write-Host
$a = $report | Where-Object { $_.arm -eq 'A2gap' }
$b = $report | Where-Object { $_.arm -eq 'B16ok' }
if ($a.slaveRequests -le 2 -and $b.slaveRequests -gt 100) {
    Write-Host "★ 停摆已复现:A 组 70 秒只发了 $($a.slaveRequests) 个请求,B 组发了 $($b.slaveRequests) 个。" 'Red'
    Write-Host "  修复前这就是 P0 的现场。修复后【应该两组都正常】。" 'Yellow'
} elseif ($a.slaveRequests -gt 100 -and $b.slaveRequests -gt 100) {
    Write-Host "★ 两组都正常 —— 修复生效,没再停摆。" 'Green'
} else {
    Write-Host "★ 数据不典型,两组都没跑出预期形状 —— 别下结论,先看日志。" 'Yellow'
}

foreach ($p in $procs) { if ($p -and -not $p.HasExited) { try { Stop-Process -Id $p.Id -Force } catch {} } }
Write-Host "artifacts: $out"
