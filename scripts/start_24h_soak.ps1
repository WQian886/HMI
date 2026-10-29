# ============================================================================
# start_24h_soak.ps1 —— 起一次 24 小时长跑,然后【立刻回来】
#
#  为什么要有这个壳
#  ------------------
#  上一轮长跑跑了 950.9 分钟,最后什么都没留下:不是被测程序崩了,
#  是【harness 自己】在 08:56 死了。而它当时只在脚本最后一行写 verdict.txt,
#  没有增量落盘 —— 于是 16 小时的证据退化成一份没有结论的 CSV。
#
#  两件事堵这个洞,都在 long_run.ps1 里:
#    · 每个采样 tick 刷新一次 verdict.txt(状态=RUNNING)
#    · 采样循环包 try/catch/finally
#  ⇒ 就算 harness 死了,现场也留得下"跑到哪了、采到第几点"。
#
#  而这个脚本管的是另一件事:【怎么把 24 小时挂上又不占着谁】
#  ------------------------------------------------------------------
#  24 小时不能挂在一次交互会话上 —— 会话一断,那一串就跟着完。
#  所以这里用 Start-Process 起一个【完全独立】的 powershell:
#    · -WindowStyle Hidden  —— 不弹窗
#    · 重定向 stdout/stderr 到文件 —— 会话断了,输出也不会跟着没
#    · 立刻返回,并把 PID / 目录 / 日志路径写下来
#  之后想看进度,看那个目录里的 verdict.txt(状态行)就够了,不用守着。
#
#  ★ 2026-10-21:它【不碰】已经在跑的 hmi.exe。
#    run_soak.ps1 用独立单实例键 + 私有数据目录,两边各跑各的。
#    这台机器上还挂着一个 2026-10-20 17:05 起的 hmi.exe(PID 42428),
#    它从启动后第一秒起就没再跟 PLC 说过一句话 —— 但那是它自己的事,
#    本脚本不结束它、不接管它、不把它算进任何判定。
#
#  用法:
#    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\start_24h_soak.ps1
#    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\start_24h_soak.ps1 -Hours 0.2
#
#  看进度:
#    Get-Content soaks\<目录>\verdict.txt -Tail 20
#    Get-Content soaks\<目录>\runner.log -Tail 40
# ============================================================================

[CmdletBinding()]
param(
    [double] $Hours       = 24,
    [int]    $IntervalSec = 60,
    [string] $SoakUser   = '',
    [string] $SoakPass   = '',
    [string] $OutDir     = '',
    [string] $Exe        = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$here = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $here '..')).Path

function Say($msg, $color = 'Gray') {
    Write-Host ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $msg) -ForegroundColor $color
}

if (-not $OutDir) {
    $OutDir = Join-Path $repo ("soaks\{0}" -f (Get-Date -Format 'yyyyMMdd_HHmmss'))
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

$runnerLog = Join-Path $OutDir 'runner.log'
$runnerErr = Join-Path $OutDir 'runner.err.log'

# ---- 先自己看一眼:能不能起 -------------------------------------------
#  ★ 为什么要在这儿就拦一道:
#    24 小时挂上之后才发现"exe 路径写错了",代价是 24 小时。
#    这道检查花两秒。
$runSoak = Join-Path $here 'run_soak.ps1'
if (-not (Test-Path $runSoak)) { Say "找不到 run_soak.ps1: $runSoak" 'Red'; exit 2 }
if (-not $Exe) { $Exe = Join-Path $repo 'hmi\out\build\debug2\hmi.exe' }
if (-not (Test-Path $Exe)) {
    Say ("找不到被测二进制: {0}" -f $Exe) 'Red'
    Say "  24 小时挂上之后才发现这个,代价就是 24 小时 —— 所以在这里就停。" 'Red'
    exit 2
}
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    Say "PATH 里没有 python(自建从站要它)" 'Red'; exit 2
}
if (-not $SoakUser -or -not $SoakPass) {
    Say "没给 -SoakUser / -SoakPass —— --soak 不是免认证开关,需要一个已存在的账号。" 'Red'
    exit 2
}
$realUsers = Join-Path (Join-Path $env:LOCALAPPDATA 'QianWei\HMI') 'users.json'
if (-not (Test-Path $realUsers)) {
    Say ("找不到用户库: {0}" -f $realUsers) 'Red'
    Say "  --soak 需要一个【已存在】的用户。请先建好用户再跑。" 'Red'
    exit 2
}

# ---- 起之前先把"已经在跑的那些"报一遍(不碰它们)-----------------------
$existing = @(Get-Process -Name hmi -ErrorAction SilentlyContinue)
if ($existing.Count -gt 0) {
    Say "已经在跑的 hmi.exe(本轮不结束、不接管、不计入判定):" 'Yellow'
    foreach ($p in $existing) {
        Say ("  · PID={0} 启动于 {1} 已跑 {2:N1} 分钟" -f `
             $p.Id, $p.StartTime.ToString('yyyy-MM-dd HH:mm:ss'),
             ((Get-Date) - $p.StartTime).TotalMinutes) 'DarkGray'
    }
}

# ---- 起 ---------------------------------------------------------------
#  ★ 用 Start-Process 而不是 `&` 或后台作业:
#    那些都跟【当前这个 powershell 进程】绑着 —— 它一退,子进程就没了。
#    Start-Process 起的是独立进程,不随本脚本的退出而结束。
$args = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', $runSoak,
    '-Hours', "$Hours",
    '-IntervalSec', "$IntervalSec",
    '-SoakUser', $SoakUser,
    '-SoakPass', $SoakPass,
    '-Exe', $Exe,
    '-OutDir', $OutDir
)

Say "========== 起 24 小时长跑 ==========" 'Cyan'
Say ("输出目录 : {0}" -f $OutDir)
Say ("被测二进制: {0}" -f $Exe)
Say ("计划时长 : {0} 小时,每 {1} 秒采一次" -f $Hours, $IntervalSec)
Say ("运行日志 : {0}" -f $runnerLog)

$p = Start-Process -FilePath 'powershell' -ArgumentList $args -PassThru `
        -WindowStyle Hidden `
        -RedirectStandardOutput $runnerLog `
        -RedirectStandardError  $runnerErr

# 把关键信息落一份在这个目录里 —— 万一会话断了,靠它找回来。
$info = @"
长跑启动于   : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
harness PID  : $($p.Id)
被测二进制   : $Exe
计划时长     : $Hours 小时(每 $IntervalSec 秒采一次)
运行日志     : $runnerLog
错误输出     : $runnerErr
判定文件     : $(Join-Path $OutDir 'verdict.txt')
采样文件     : $(Join-Path $OutDir 'samples.csv')
★ 进度看 verdict.txt 的"状态"那一行:
  RUNNING = 还在跑(正常);DONE = 跑完了,下面就是最终判定。
★ 24 小时里不用守着 —— verdict.txt 每个采样 tick 都会刷新,
  harness 死了也留得下"跑到哪了"。
"@
[System.IO.File]::WriteAllText((Join-Path $OutDir 'RUN_INFO.txt'), $info, (New-Object System.Text.UTF8Encoding($true)))

Say ""
Say ("已启动,harness PID = {0}" -f $p.Id) 'Green'
Say ("查进度: Get-Content '{0}\verdict.txt' -Tail 20" -f $OutDir) 'DarkGray'
Say ("看日志: Get-Content '{0}' -Tail 40" -f $runnerLog) 'DarkGray'
exit 0
