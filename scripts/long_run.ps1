<#
.SYNOPSIS
    HMI 长时间运行(soak)验收脚手架 —— 阶段 7.7 / F2 的验收工具。

.DESCRIPTION
    为什么需要这个文件
    ------------------
    docs/现状.md 记着一条一直没人消掉的验收缺口:"F2 未真验证 —— 24h 长跑"。
    这个文件是那条缺口的工具,不是又一份"看起来在测"的东西。

    它测什么(全部来自实测,不是设想的)
    -----------------------------------
    2026-10-14 开工前先量了现状,发现四条路径从来没被真正跑过:

      1. 崩溃上报链条只做了前半段。
         crash/pending.txt 停在 2026-09-16,exception_code=0xC0000005,
         28 天没被任何一次启动消费掉 —— 取走 dump 的那段代码存在,
         但没人验证过它真的会执行。写 dump 和"崩了有人知道"是两回事。

      2. 日志滚动分支从没有执行过。
         127 个日志文件里最大的一个 42.6 KB,滚动阈值 100 MB ——
         差 2400 倍。Logging.cpp 里那段切文件的代码至今是死代码。

      3. 5 周 127 次启动。
         一次启动一个日志文件。这是"从来没连续跑过"最硬的物证,
         比文档里任何一句"待验证"都硬。

      4. 资源曲线(内存/句柄/线程)没有任何基线。
         没有基线就没有"涨了没涨"这个问题。

    这个脚本把这四条变成可断言的判据,而不是跑够小时数就算过。
    跑 24 小时但四项全过,和跑 24 小时发现 marker 被人清了,是两回事。

    它不做什么
    ----------
    · 不判定"掉没掉线"。日志里 hmi.comm 的具体文案没摸清之前,
      自动匹配中文等于闭着眼睛下结论。这里只把样本行打给人看。
    · 不假装 1 小时的斜率就是 24 小时的真相。报告里同时给
      实测斜率和外推值,并标明外推不可信(见下方 外推 警告)。

    它绝不会做的事
    --------------
    · 【绝不杀已有的 hmi.exe】。你自己开着的进程不是我的实验品。
      检测到就退出,除非显式 -AdoptExisting(只观察,不接管)。
    · 【绝不假绿】。每条判据后面必须打出支撑它的实际数字;
      数字不够就标 UNKNOWN,不许用"看起来正常"糊过去。

.PARAMETER Hours
    跑多久。默认 2 —— 第一次跑脚本,先验证脚本本身能跑起来,
    别一上来就 24 小时然后第二天发现脚本第 3 小时就崩了。
    确认无误后用 -Hours 24。

.PARAMETER IntervalSec
    采样间隔秒数。默认 60。24h 跑下来是 1440 个采样点,CSV 几 MB,可接受。

.PARAMETER AdoptExisting
    不启动新进程,观察已经在跑的 hmi.exe。
    用途:你今晚忘了关自己那个 hmi,又不想白跑 —— 观察也能拿到资源曲线,
    但要清楚这不是"受控长跑"(没有干净的基线,崩溃标记可能已经是脏的)。

.PARAMETER OutDir
    报告落盘目录。默认 soaks/<时间戳>/。

.EXAMPLE
    .\scripts\long_run.ps1
    默认 2 小时,发现你开着 hmi 就直接拒绝。

.EXAMPLE
    .\scripts\long_run.ps1 -Hours 24 -IntervalSec 60
    真正的 24h。启动前请确认机器今晚不会关机/重启。

.NOTES
    日志读取有两处必须显式指定,否则拿到的全是错的:
      · FileShare.ReadWrite —— hmi 运行中独占锁着日志文件,
        普通 ReadAllBytes 会抛 IOException(实测踩过)。
      · UTF-8 —— 日志本身是 UTF-8(前端看着乱码是 PowerShell 5.1
        的 Get-Content 默认按 GBK 读,那是读取端的锅,不是应用的锅)。
    这两条曾经让一次分析得出"日志编码坏了"的错误结论,写在这里防止重犯。
#>

[CmdletBinding()]
param(
    [double] $Hours        = 2,
    [int]    $IntervalSec  = 60,
    [switch] $AdoptExisting,
    [string] $OutDir,

    # ---- 判据阈值 ----
    # 做成参数不是为了"调",是为了能被【证伪】:
    # 把 MaxMemRatio 拧到 1.0001,内存曲线必须判红。
    # 一个拧到底都不红的判据,等于没有判据。
    # 默认值的来由见下面各自的使用处。
    [double] $MaxMemRatio    = 2.0,   # 内存超过这个倍数才考虑 FAIL
    [double] $MaxMemPerHour  = 20.0,  # 且每小时涨超过这么多 MB 才算 FAIL(倍数+斜率双条件)
    [double] $WarnMemRatio   = 1.3,   # 超过这个倍数先给 WARN
    [int]    $MaxHandleDelta = 50,    # 句柄净增超过这个数判 FAIL
    [int]    $WarnHandleDelta= 10
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

# ---- 事实常量(全部实测过,不是猜的)------------------------------------
# main.cpp:105-107 -> org=QianWei app=HMI
# QStandardPaths::AppLocalDataLocation 在 Windows 上是 Local 而非 Roaming
$dataDir = Join-Path $env:LOCALAPPDATA 'QianWei\HMI'
$logDir  = Join-Path $dataDir  'logs'
$crashDir = Join-Path $dataDir 'crash'
$marker  = Join-Path $crashDir 'pending.txt'

# 日志格式(Logging.cpp:104-152):[yyyy-MM-dd hh:mm:ss.zzz] [LEVEL] category - msg
$reLine = '^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\s+\[(\w+)\s*\]\s+([\w.]+)\s+-\s+(.*)$'

function Say($msg, $color = 'Gray') {
    Write-Host ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $msg) -ForegroundColor $color
}

# ---- 采样单个进程的一帧 ------------------------------------------------
function Get-Sample($proc) {
    $null = $proc.Refresh()          # 不 Refresh 拿到的是启动时的快照,不是当前值
    [pscustomobject]@{
        Time            = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
        WorkingSetMB    = [math]::Round($proc.WorkingSet64 / 1MB, 2)
        PrivateMB       = [math]::Round($proc.PrivateMemorySize64 / 1MB, 2)
        Handles         = $proc.HandleCount
        Threads         = $proc.Threads.Count
        CpuSeconds      = [math]::Round($proc.CPU, 2)
        RunMinutes      = [math]::Round(((Get-Date) - $proc.StartTime).TotalMinutes, 1)
    }
}

# 统计日志文件(UTF-8 + 共享读,两个都必须)
function Get-LogStats {
    if (-not (Test-Path $logDir)) { return $null }
    $files = Get-ChildItem $logDir -Filter '*.log' -File -ErrorAction SilentlyContinue
    if (-not $files) { return $null }
    $bytes = ($files | Measure-Object Length -Sum).Sum
    [pscustomobject]@{
        Count   = $files.Count
        Bytes   = $bytes
        # ★ 最大 和 最新 是两回事,曾经在这里混成一个数报出去,
        #   打出来是"最大 0.3 KB",而实际最大文件是 42.6 KB ——
        #   拿最新的那个文件冒充最大的,等于报告里写了一句没依据的话。
        #   分开算,谁是谁不许含糊。
        MaxFile = ($files | Sort-Object Length | Select-Object -Last 1)
        Newest  = ($files | Sort-Object LastWriteTime | Select-Object -Last 1)
    }
}

# 抽本次运行产生的日志行(按 category 计数)
function Get-LogTail($sinceMs, [int] $limit = 4000) {
    if (-not (Test-Path $logDir)) { return @() }
    $newest = Get-ChildItem $logDir -Filter '*.log' -File -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime | Select-Object -Last 1
    if (-not $newest) { return @() }
    $out = New-Object System.Collections.Generic.List[object]
    try {
        $fs = [System.IO.File]::Open($newest.FullName, 'Open', 'Read', 'ReadWrite')
        $sr = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8)
        $n = 0
        while (-not $sr.EndOfStream -and $n -lt $limit) {
            $line = $sr.ReadLine(); $n++
            if ($line -match $reLine) {
                $out.Add([pscustomobject]@{
                    Level = $matches[1]; Cat = $matches[2]; Msg = $matches[3]
                })
            }
        }
        $sr.Close(); $fs.Close()
    } catch {
        Say ("读日志失败: " + $_.Exception.Message) 'Red'
    }
    return $out
}

# ================================================================
Say "========== HMI 长跑验收 ==========" 'Cyan'
Say "数据目录: $dataDir"

# ---- 前置 1:你自己是不是已经开着 hmi -------------------------------
$existing = @(Get-Process -Name hmi -ErrorAction SilentlyContinue)
$owned = $null       # 本脚本启动的进程

if ($existing.Count -gt 0) {
    $p = $existing[0]
    Say ("检测到 hmi.exe 正在运行 —— PID={0} 启动于 {1} 已运行 {2:N1} 分钟 内存 {3} MB" -f `
         $p.Id, $p.StartTime.ToString('yyyy-MM-dd HH:mm:ss'),
         ((Get-Date) - $p.StartTime).TotalMinutes, [math]::Round($p.WorkingSet64/1MB,1)) 'Yellow'

    if (-not $AdoptExisting) {
        Say "拒绝启动第二个实例。原因有两个,都不是多余的:" 'Red'
        Say "  1) SingleInstance 会让第二次启动直接退出 —— 那样跑 2 小时采到的全是空气,还看不出异常;" 'Red'
        Say "  2) 那是你的进程,不是我的实验品。要跑长跑请先自己关掉它。" 'Red'
        Say "想改成观察它(拿不到干净基线,只算参考): -AdoptExisting" 'Yellow'
        exit 2
    }
    $owned = $p
    Say "采用 -AdoptExisting:只观察,不接管、不杀。" 'Yellow'
}

# ---- 前置 2:找可执行文件 -------------------------------------------
$exe = $null
if (-not $AdoptExisting) {
    $cands = @(
        (Join-Path $PSScriptRoot '..\hmi\out\build\debug\hmi.exe'),
        (Join-Path $PSScriptRoot '..\hmi\x64\Debug\hmi.exe')
    )
    $exe = $cands | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $exe) {
        Say "找不到 hmi.exe。先 build.ps1 编一份。" 'Red'; exit 1
    }
    Say ("可执行文件: {0} ({1} MB)" -f $exe, [math]::Round((Get-Item $exe).Length/1MB,2)) 'Gray'
}

# ---- 基线快照 --------------------------------------------------------
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot "..\soaks\$(Get-Date -Format 'yyyyMMdd_HHmmss')" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

$baselineLog = Get-LogStats
$baselineTime = Get-Date          # 一切"本次新增"的判定都以这一刻为界
$baselineMarker = if (Test-Path $marker) {
    [pscustomobject]@{ Exists = $true; Content = (Get-Content $marker -Raw -Encoding UTF8); Mtime = (Get-Item $marker).LastWriteTime }
} else { [pscustomobject]@{ Exists = $false; Content = ''; Mtime = $null } }

# ★ 存量脏标记:上一轮崩溃留下的、还没被任何启动消费掉的那个。
#   单独拎出来报 —— 免得被"本次没崩"盖过去。
#   2026-10-14 实测:它在 crash/pending.txt 停了 28 天。
Say "---- 基线 ----" 'Cyan'
Say ("日志: {0} 个文件 / 合计 {1} KB;最大单个 {2} KB({3});最新 {4}({5} KB)" -f `
     $baselineLog.Count, [math]::Round($baselineLog.Bytes/1KB,1),
     [math]::Round($baselineLog.MaxFile.Length/1KB,1), $baselineLog.MaxFile.Name,
     $baselineLog.Newest.Name, [math]::Round($baselineLog.Newest.Length/1KB,1))
if ($baselineMarker.Exists) {
    Say "★ 崩溃标记【已经存在】(上一轮崩溃没人清理):" 'Yellow'
    $baselineMarker.Content.Trim() -split "`n" | ForEach-Object { Say ("    " + $_) 'Yellow' }
} else {
    Say "崩溃标记: 无"
}

# ---- 启动 ------------------------------------------------------------
if (-not $AdoptExisting) {
    Say ("启动 hmi.exe,计划跑 {0} 小时,每 {1} 秒采一次" -f $Hours, $IntervalSec) 'Cyan'
    $owned = Start-Process -FilePath $exe -PassThru
    Start-Sleep -Seconds 5
    if ($owned.HasExited) {
        Say ("启动后 5 秒就退出了(exit={0}) —— 脚手架没跑起来,先查这个" -f $owned.ExitCode) 'Red'
        exit 1
    }
    Say ("已启动 PID={0}" -f $owned.Id) 'Green'
}

$samples   = New-Object System.Collections.Generic.List[object]
$csvPath   = Join-Path $OutDir 'samples.csv'
$deadline  = (Get-Date).AddHours($Hours)
$tick      = 0

# ---- 采样循环 --------------------------------------------------------
Say "---- 开始采样(Ctrl-C 可提前结束)----" 'Cyan'
while ($true) {
    if ($Hours -gt 0 -and (Get-Date) -gt $deadline) { Say "到时间了。" 'Green'; break }
    $null = $owned.Refresh()
    if ($owned.HasExited) {
        Say ("★ hmi.exe 自己在第 {0} 分钟退出了,exit code={1}" -f `
             [math]::Round(((Get-Date) - $owned.StartTime).TotalMinutes,1), $owned.ExitCode) 'Red'
        break
    }
    $samples.Add((Get-Sample $owned))
    $tick++
    # 每 10 次把 CSV 落一次盘 —— 机器半夜被谁强制重启也不至于全丢
    if ($tick % 10 -eq 0) {
        $samples | Export-Csv -Path $csvPath -NoTypeInformation -Encoding UTF8
        $last = $samples[$samples.Count - 1]
        Say ("  {0} 内存 {1} MB / 句柄 {2} / 线程 {3} / CPU {4}s" -f `
             $last.Time, $last.WorkingSetMB, $last.Handles, $last.Threads, $last.CpuSeconds) 'DarkGray'
    }
    Start-Sleep -Seconds $IntervalSec
}
$samples | Export-Csv -Path $csvPath -NoTypeInformation -Encoding UTF8

# ---- 收尾:停掉我们启动的进程 ----------------------------------------
if (-not $AdoptExisting -and -not $owned.HasExited) {
    Say "正常路径结束,关掉本次启动的进程(只关我自己启的那个)"
    try { $owned.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 3 } catch { }
    if (-not $owned.HasExited) { Stop-Process -Id $owned.Id -Force }
}

# ================================================================
#  判定
# ================================================================
$first = $samples[0]
$lastN = $samples[$samples.Count - 1]
$spanMin = [math]::Max(1.0, $lastN.RunMinutes - $first.RunMinutes)

$finalLog    = Get-LogStats
$finalMarker = if (Test-Path $marker) { (Get-Item $marker) } else { $null }

$newLogBytes = [math]::Max(0, $finalLog.Bytes - $baselineLog.Bytes)

$report = New-Object System.Collections.Generic.List[string]
function Verdict($id, $name, $state, $evidence) {
    $script:report.Add(("[{0}] {1,-22} {2}" -f $state, $id, $name))
    $script:report.Add(("        证据: " + $evidence))
}

Say "" ; Say "========== 判定 ==========" 'Cyan'

# --- 1. 崩溃链条 ---
#  ★ 窗口必须以【基线时刻】为起点,不能用 "现在往前推运行时长"。
#    最初写成 (Get-Date).AddHours(-$Hours),被负向对照逮到:
#    脚本从启动到判定会花掉基线快照 + 进程启动 + 收尾的开销,
#    那个窗口比实际运行时间【短了一截】,于是开头几秒内真发生的崩溃
#    被漏掉,报成 PASS —— 假阴性,是这类工具最不能犯的错:
#    它没有虚惊一场,而是让一次真事故安静地过去。
$newCrash = @(Get-ChildItem $crashDir -Filter '*.dmp' -File -ErrorAction SilentlyContinue |
              Where-Object { $_.LastWriteTime -gt $baselineTime })
if ($newCrash.Count -gt 0) {
    Verdict 'SOAK-1' '崩溃上报' 'FAIL' ("本次新增 {0} 个 dump: {1}" -f $newCrash.Count, ($newCrash.Name -join ', '))
} elseif (-not $baselineMarker.Exists -and -not $finalMarker) {
    # 全程既没有存量标记、也没产生新的 —— 这才是干净的 PASS。
    # 原来这个情况会掉进下面的 WARN,理由是 elseif 少了"-基线本来就没有"这一支。
    Verdict 'SOAK-1' '崩溃上报' 'PASS' '基线无残留标记,本次也未产生新 dump —— 干净'
} elseif ($finalMarker -and $baselineMarker.Exists -and $finalMarker.LastWriteTime -eq $baselineMarker.Mtime) {
    Verdict 'SOAK-1' '崩溃上报' 'PASS' '本次未产生新 dump'
    Say "★ 但注意:那个存量 pending.txt 跑完还在。" 'Yellow'
    Say "  它停了 28 天没被任何一次启动清掉 —— 这条链路的【消费端】是坏的," 'Yellow'
    Say "  写 dump 那半段是好的。一次不崩的长跑证明不了它,得专门验。" 'Yellow'
} else {
    Verdict 'SOAK-1' '崩溃上报' 'WARN' '未产生新 dump,但崩溃标记的存在状态在本次运行前后不一致,需人工确认'
}

# --- 2. 资源曲线 ---
#  判据用【倍数】不用【绝对值】:内存 20MB 涨到 30MB 在 2 小时里不算事,
#  50MB 涨到 1GB 就是泄漏。倍数跨机器可比。
#  为什么 FAIL 要【倍数 AND 斜率】两个条件同时成立:
#  只看倍数,一个刚启动就分配 200MB 的进程会被误判成泄漏;
#  只看斜率,缓慢爬升 0.1MB/小时的慢性泄漏永远抓不到(它 100 小时才翻倍)。
#  两个一起要,才既不误报又不漏报。
$memRatio = if ($first.WorkingSetMB -gt 0) { $lastN.WorkingSetMB / $first.WorkingSetMB } else { 0 }
$hdlDelta = $lastN.Handles - $first.Handles
$memPerHr = ($lastN.WorkingSetMB - $first.WorkingSetMB) / ($spanMin / 60.0)

$memState = 'PASS'
if ($memRatio -gt $MaxMemRatio -and $memPerHr -gt $MaxMemPerHour) { $memState = 'FAIL' }
elseif ($memRatio -gt $WarnMemRatio) { $memState = 'WARN' }
Verdict 'SOAK-2' '内存曲线' $memState ("{0} -> {1} MB ({2:N2}x),斜率 {3:N1} MB/小时;线程 {4}->{5};阈值 {6}x & {7}MB/h" -f `
    $first.WorkingSetMB, $lastN.WorkingSetMB, $memRatio, $memPerHr, $first.Threads, $lastN.Threads,
    $MaxMemRatio, $MaxMemPerHour)

$hdlState = 'PASS'
if ($hdlDelta -gt $MaxHandleDelta) { $hdlState = 'FAIL' } elseif ($hdlDelta -gt $WarnHandleDelta) { $hdlState = 'WARN' }
Verdict 'SOAK-3' '句柄/线程' $hdlState ("句柄 {0} -> {1}({2:+#;-#;0});线程 {3} -> {4}" -f `
    $first.Handles, $lastN.Handles, $hdlDelta, $first.Threads, $lastN.Threads)

# --- 4. 日志 ---
$lines = @(Get-LogTail 0)
$byCat = $lines | Group-Object Cat | Sort-Object Count -Descending |
         Select-Object -First 8 @{n='cat';e={$_.Name}}, @{n='n';e={$_.Count}}
$catTxt = ($byCat | ForEach-Object { "$($_.cat)=$($_.n)" }) -join '  '
$proj24h = [math]::Round(($newLogBytes / [math]::Max(1,$spanMin/60.0)) * 24 / 1MB, 1)

Verdict 'SOAK-4' '日志体量' 'INFO' ("本次新增 {0} KB;按此斜率 24h 约 {1} MB(外推,不可信 —— 见下方说明)" -f `
    [math]::Round($newLogBytes/1KB,1), $proj24h)
Say ("      按类别: " + $catTxt) 'DarkGray'
if ($finalLog.Count -gt $baselineLog.Count) {
    Verdict 'SOAK-5' '日志滚动' 'INFO' ("文件数 {0} -> {1};最大单个 {2} KB({3}),滚动阈值 100 MB" -f `
        $baselineLog.Count, $finalLog.Count,
        [math]::Round($finalLog.MaxFile.Length/1KB,1), $finalLog.MaxFile.Name)
} else {
    Verdict 'SOAK-5' '日志滚动' 'UNKNOWN' '没跑到跨天/切文件,滚动分支仍未被执行 —— 这是预期内的,不是通过'
}

# --- 6. 连接稳定性:只报不判 ---
# ⚠ 必须 @() 包起来:只有一条 hmi.comm 时,PowerShell 会把数组拆成
#   单个对象,$commLines.Count 在 StrictMode 下直接抛 PropertyNotFoundStrict。
#   踩过。同理下面 $(...).Count 全部都要先包数组。
$commLines = @($lines | Where-Object { $_.Cat -eq 'hmi.comm' })
Say "" ; Say "========== hmi.comm 样本(只报不判)==========" 'Yellow'
if ($commLines.Count -eq 0) {
    Say "  本次没有 hmi.comm 日志。" 'Yellow'
    Say "  两种可能:(a) 没连 PLC;(b) 连上了但本来就不 chatter。" 'Yellow'
    Say "  【没连 PLC 的长跑验不了掉线】—— 只验了资源曲线和崩溃链条。" 'Yellow'
} else {
    $commLines | Select-Object -First 15 | ForEach-Object { Say ("  [{0}] {1}" -f $_.Level, $_.Msg) 'DarkGray' }
    if ($commLines.Count -gt 15) { Say ("  ... 共 {0} 条,上面是头 15 条" -f $commLines.Count) 'DarkGray' }
    Say "  ⚠ 这些文案还没摸清,所以【不自动判定掉没掉线】。看完了再把判据补进来。" 'Yellow'
}

# ---- 落盘 ------------------------------------------------------------
$reportPath = Join-Path $OutDir 'verdict.txt'
$report | Out-File -FilePath $reportPath -Encoding UTF8

Say "" ; Say "========== 汇总 ==========" 'Cyan'
$report | ForEach-Object { Say $_ }
Say ""
Say "采样点 $tick 个,跨度 $([math]::Round($spanMin,1)) 分钟" 'Gray'
Say "样本: $csvPath" 'Gray'
Say "判定: $reportPath" 'Gray'
Say "" ; Say "⚠ 外推警告:上面 24h 的数字是拿 $spanMin 分钟的线性斜率乘出来的。" 'Yellow'
Say "  泄漏通常不是线性的(定时器累积、缓存到阈值才涨),线性外推会【低估】。" 'Yellow'
Say "  想真知道 24 小时什么样,就得真跑 24 小时。" 'Yellow'

# ---- 退出码:有 FAIL 就不能是 0 ----
#   写这条是因为实测踩到过:判定判了 FAIL,退出码还给 0。
#   任何拿 $LASTEXITCODE 做门禁的东西(CI、定时任务、一行 if 判断)
#   都会把一次 FAIL 读成成功 —— 报告里红着,程序说"没事"。
#   报告给人看,退出码给机器判,两个必须一致。
$failCount = @($report | Where-Object { $_ -match '^\[FAIL\]' }).Count
$warnCount = @($report | Where-Object { $_ -match '^\[WARN\]' }).Count
if ($failCount -gt 0) { exit 1 }
if ($warnCount -gt 0) { exit 3 }
exit 0