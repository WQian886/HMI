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
    ⚠ 本模式下 -SoakUser/-SoakPass 无效(不重启进程就加不上启动参数),
    给了会直接报错退出 —— 不是静默忽略。

.PARAMETER SoakUser
.PARAMETER SoakPass
    2026-10-14 新增,用来拆掉长跑的两道人工阻塞:
      (1) LoginDialog::exec() 是模态阻塞的,脚本点不了它;
      (2) InactivityWatcher 5 分钟无操作会自动注销并【再次弹出登录框】,
          长跑期间没人碰键盘鼠标 —— 24 小时会弹 288 次,每次都把
          Modbus 轮询和报警引擎堵停一次。
    给了这两个参数,启动时会带上 hmi.exe 的 --soak,自动登录并关闭自动注销。
    【安全边界】不是免认证:要求该用户已存在于 users.json,
    且调的是同一个 UserManager::authenticate 核密码,错则拒绝启动。
    【必须成对给】只给一个会认证失败退出,脚本会提前拦下并说明原因。

.PARAMETER OutDir
    报告落盘目录。默认 soaks/<时间戳>/。

.EXAMPLE
    .\scripts\long_run.ps1
    默认 2 小时,发现你开着 hmi 就直接拒绝。

.EXAMPLE
    .\scripts\long_run.ps1 -Hours 24 -IntervalSec 60
    真正的 24h。启动前请确认机器今晚不会关机/重启。

.EXAMPLE
    .\scripts\long_run.ps1 -Hours 24 -SoakUser admin -SoakPass '你的密码'
    真正的 24h 受控长跑(自动登录,不需人工干预)。启动前请确认:
      · 机器今晚不会关机/重启
      · 【没有别的 hmi.exe 在跑】—— 单实例锁会让第二次启动直接退出
      · 已连上 PLC(否则验不了「不掉线」,只能验资源曲线和崩溃链条)

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

    # ---- 2026-10-14:把长跑的两道人工阻塞拆掉 --------------------------
    #  为什么需要这两个参数
    #  -------------------
    #  之前三次长跑全判 UNKNOWN,根因不在被测代码,在人机界面:
    #
    #    (1) LoginDialog::exec() 是模态阻塞的(main.cpp:254)。
    #        长跑脚本没法点它,进程停在登录框,CPU 约 0.5%,
    #        报警引擎和 Modbus 轮询根本没跑起来。
    #
    #    (2) ★ 就算人工登录了,InactivityWatcher 会在 5 分钟无操作后
    #        自动注销并【再次弹出模态登录框】(main.cpp:265-282)。
    #        长跑期间没人碰鼠标键盘 —— 24 小时里会弹 288 次,
    #        每弹一次主流程就阻塞一次,轮询全停。
    #        这一条比第一条更隐蔽:它不是"没登录",是"周期性冻结",
    #        资源曲线会呈锯齿状,看着像泄漏其实不是。
    #
    #  所以 -SoakUser/-SoakPass 启动时会带上 hmi.exe 的 --soak 开关。
    #  【安全边界】它不是免认证:仍然要求 users.json 里已存在该用户,
    #  并且调的是同一个 UserManager::authenticate 核密码,错则拒绝启动。
    #  被跳过的只有那个 QDialog,以及长跑期间的自动注销计时。
    #  【为什么不给默认账号】凭据必须显式给 —— 脚本替用户猜账号密码,
    #  一旦猜中就等于默认放行,那才是后门。
    [string] $SoakUser = '',
    [string] $SoakPass = '',

    # ---- 判据阈值 ----
    # 做成参数不是为了"调",是为了能被【证伪】:
    # 把 MaxMemRatio 拧到 1.0001,内存曲线必须判红。
    # 一个拧到底都不红的判据,等于没有判据。
    # 默认值的来由见下面各自的使用处。
    [double] $MaxMemRatio    = 2.0,   # 私有内存超过这个倍数才考虑 FAIL
    [double] $MaxMemPerHour  = 20.0,  # 且每小时涨超过这么多 MB 才算 FAIL(倍数+斜率双条件)
    [double] $WarnMemRatio   = 1.3,   # 超过这个倍数先给 WARN
    [int]    $MaxHandleDelta = 50,    # 句柄【峰值净增】超过这个数判 FAIL(不是首尾差)
    [int]    $WarnHandleDelta= 10,

    # ---- idle 检测(2026-10-14 第一轮跑完加的)--------------------------
    #  为什么加:第一轮 6 分钟跑出 PASS,但同一份数据里 CPU 只从 11.97s
    #  涨到 13.91s —— 6 分钟才 1.94s,约 0.5% 单核。进程当时停在登录框,
    #  根本没进业务。一个 idle 进程本来就不涨内存,"没涨"验不出任何东西,
    #  报 PASS 是把"没能力验"说成"验过了"。这比假绿更隐蔽:
    #  假绿是量错了,这个是压根没量。
    #
    #  默认值 5.0 的来由:第一轮实测 idle 约 0.5%。真正跑着报警引擎 +
    #  Modbus 轮询的 HMI,CPU 至少要高一个数量级。5.0 留足了余量,
    #  又不会把"低负载但确实在跑"的现场误判成 idle。
    [double] $MinCpuPercent = 5.0     # 低于这个占用率判定为 idle
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

    # ★ 静默失效要堵:观察模式下【不会重启进程】,所以下面启动段那整套
    #   --soak 参数根本不会被使用。给了 -SoakUser/-SoakPass 却不报错,
    #   等于让人以为"我配了自动登录",实际跑的是那个还停在登录框的旧进程 ——
    #   然后收获一个 UNKNOWN,还要花半小时想为什么。
    #   这类"参数被接受但没用"比直接报错坏得多。
    if ($SoakUser -or $SoakPass) {
        Say "-SoakUser/-SoakPass 在 -AdoptExisting 下【无效】:" 'Red'
        Say "  观察模式不重启进程,没法给它加启动参数 —— 你观察的还是原来那个。" 'Red'
        Say "  要真正用 --soak 长跑,请先关掉已有 hmi,再去掉 -AdoptExisting。" 'Red'
        exit 1
    }
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

    # ★ 凭据必须【成对】给。给一半就启动,程序会因认证失败退出,
    #   而退出发生在 QApplication 之后、日志就绪之后 —— 但也可能更早,
    #   届时日志里什么都没有,现场只剩一句"启动后 5 秒就退出了(exit=)"。
    #   在这里挡住,比让它跑起来再崩要好排查得多。
    if ([bool]$SoakUser -xor [bool]$SoakPass) {
        Say "-SoakUser 和 -SoakPass 必须【成对】给(只给一个会认证失败退出)。" 'Red'
        exit 1
    }

    $launchArgs = @()
    if ($SoakUser -and $SoakPass) {
        $launchArgs = @('--soak', '--soak-user', $SoakUser, '--soak-pass', $SoakPass)
        Say "长跑模式:已启用 --soak(自动登录 + 关闭自动注销)" 'Green'
        Say "  → 不再需要人工点登录框" 'Green'
        Say "  → 不再需要每 5 分钟动一下鼠标(否则会自动注销并弹框阻塞)" 'Green'
    } else {
        Say "★ 未给 -SoakUser/-SoakPass —— 进程会停在登录框,本轮必然判 UNKNOWN。" 'Yellow'
        Say "  长跑要用:.\scripts\long_run.ps1 -Hours 24 -SoakUser <用户名> -SoakPass <密码>" 'Yellow'
    }

    $owned = Start-Process -FilePath $exe -ArgumentList $launchArgs -PassThru
    Start-Sleep -Seconds 5
    if ($owned.HasExited) {
        # ★ ExitCode 有可能是空的 —— 实测踩过:Start-Process -PassThru 拿到的
        #   对象在不同时机读 ExitCode 会返回空字符串,格式化成 "exit=" 什么也
        #   没有。判不出来就明说"取不到",别打一个空串上去装作拿到了 ——
        #   那正是"看起来在报,其实没信息"的一类假绿。
        $code = try { $owned.ExitCode } catch { $null }
        if ($null -eq $code -or "$code" -eq '') {
            Say "启动后 5 秒就退出了(取不到 exit code) —— 脚手架没跑起来,先查这个。" 'Red'
        } else {
            Say ("启动后 5 秒就退出了(exit={0}) —— 脚手架没跑起来,先查这个。" -f $code) 'Red'
        }
        if ($SoakUser -and $SoakPass) {
            Say "  给了 --soak 还退,最可能是【用户名或密码不对】。" 'Red'
            Say "  程序会打 '[main] --soak 认证失败' 到日志,去 $logDir 里看最新那个。" 'Red'
        }
        Say "  也可能是还有别的 hmi.exe 占着单实例锁(见上面的检测)。" 'Red'
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
#
#  ★ 2026-10-14 改判据基准:WorkingSetMB → PrivateMB
#    第一轮实测把这条判据说穿了:
#        工作集 141.38 → 127.72 MB,斜率 -146.4 MB/小时
#        私有内存 46.43 → 46.43 MB,全程平
#    那个 -146.4 不是程序释放了什么,是 Windows 把工作集还给操作系统
#    (采样中途 11:05:59 一次掉了 13.6 MB,私有内存纹丝不动就是证据)。
#    按它外推 24h 会得出"-3.5 GB"这种荒谬数,判据没被骗(阈值只看涨),
#    但它证明的是"没涨",不是"稳" —— 而工作集这个量本来就不该拿来判泄漏:
#    它含共享页,会被系统随时回收。私有内存才是"这个进程自己占了多少"。
#    工作集保留在报告里,但只作参考,不参与判定。
$memRatio = if ($first.PrivateMB -gt 0) { $lastN.PrivateMB / $first.PrivateMB } else { 0 }
$memPerHr = ($lastN.PrivateMB - $first.PrivateMB) / ($spanMin / 60.0)
$wsPerHr = ($lastN.WorkingSetMB - $first.WorkingSetMB) / ($spanMin / 60.0)

# ★ idle 检测:由相邻采样点的 CPU 增量算平均占用率。
#   取【全程平均】而不是峰值 —— 峰值会被一次性的启动/加载尖峰顶高,
#   让一个实际在闲置的进程蒙混过关。
$cpuSpan = $lastN.CpuSeconds - $first.CpuSeconds
$cpuPct  = if ($spanMin -gt 0) { ($cpuSpan / ($spanMin * 60.0)) * 100.0 } else { 0 }
$isIdle  = ($cpuPct -lt $MinCpuPercent)

# idle 时降级为 UNKNOWN:不是"通过",是"这一轮没有资格下结论"。
# 一个没登录、没连 PLC、没跑报警引擎的进程,内存当然不涨 ——
# 拿它的平稳去证明"程序不泄漏"是偷换前提。
if ($isIdle) {
    $memState = 'UNKNOWN'
} elseif ($memRatio -gt $MaxMemRatio -and $memPerHr -gt $MaxMemPerHour) { $memState = 'FAIL' }
elseif ($memRatio -gt $WarnMemRatio) { $memState = 'WARN' }
else { $memState = 'PASS' }

$memTxt = ("私有内存 {0} -> {1} MB ({2:N3}x),斜率 {3:N2} MB/小时;线程 {4}->{5};阈值 {6}x & {7}MB/h" -f `
    $first.PrivateMB, $lastN.PrivateMB, $memRatio, $memPerHr, $first.Threads, $lastN.Threads,
    $MaxMemRatio, $MaxMemPerHour)
if ($isIdle) {
    $memTxt += ("`n        ★ 本轮进程平均 CPU 仅 {0:N2}%,低于阈值 {1}% —— 判定为 idle。" -f $cpuPct, $MinCpuPercent)
    $memTxt += "`n        idle 进程本来就不涨内存,这一轮【不构成对程序稳定性的验收】。"
    $memTxt += "`n        要真验,先登录进主界面、连上 PLC,让报警引擎和轮询跑起来再重跑。"
}
$memTxt += ("`n        参考(不参与判定):工作集 {0} -> {1} MB,斜率 {2:N1} MB/小时 —— 这个量会被 Windows 回收" -f `
    $first.WorkingSetMB, $lastN.WorkingSetMB, $wsPerHr)
Verdict 'SOAK-2' '内存曲线' $memState $memTxt

# ★ 句柄/线程改用【峰值净增】而不是首尾差。
#   第一轮实测:句柄 813 → 817 → 813,首尾相等,老算法报"净增 0,PASS"。
#   但中途确实多过 4 个句柄、3 个线程 —— 首尾相等把"涨过又落回去"盖住了。
#   泄漏常常正是这种形状(周期性建资源、用完释放),只看首尾永远看不见。
#   峰值法还能区分"一路单调涨"(真泄漏)和"来回抖动"(正常业务)。
$hdlMax = ($samples | Measure-Object Handles -Maximum).Maximum
$thrMax = ($samples | Measure-Object Threads  -Maximum).Maximum
$hdlPeak = $hdlMax - $first.Handles
$thrPeak = $thrMax - $first.Threads
$hdlNet  = $lastN.Handles - $first.Handles

$hdlState = 'PASS'
if ($isIdle) {
    $hdlState = 'UNKNOWN'
} elseif ($hdlPeak -gt $MaxHandleDelta) { $hdlState = 'FAIL' }
elseif ($hdlPeak -gt $WarnHandleDelta) { $hdlState = 'WARN' }

$hdlTxt = ("句柄 首{0}/峰{1}/尾{2}(峰值净增 {3},首尾差 {4});线程 首{5}/峰{6}/尾{7}" -f `
    $first.Handles, $hdlMax, $lastN.Handles, $hdlPeak, $hdlNet,
    $first.Threads, $thrMax, $lastN.Threads)
if ($hdlPeak -gt 0 -and $hdlNet -eq 0) {
    $hdlTxt += "`n        ★ 中途涨过又落回去了 —— 首尾相等会把它掩盖成'没变',所以这里报峰值。"
}
if ($isIdle) { $hdlTxt += "`n        (同 SOAK-2:进程 idle,本轮不构成验收)" }
Verdict 'SOAK-3' '句柄/线程' $hdlState $hdlTxt

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

# ★ idle 横幅:放最显眼的位置。这轮如果 idle,上面所有 UNKNOWN 都是同一个原因,
#   不单独说一句的话,读报告的人容易只扫到别的 PASS 就以为验过了。
if ($isIdle) {
    Say "" ; Say "════════════════════════════════════════════════════" 'Red'
    Say ("★ 本轮没有结论(进程 idle,平均 CPU {0:N2}%)" -f $cpuPct) 'Red'
    Say "  进程没在跑业务 —— 资源曲线平坦是必然的,不是'稳定'的证据。" 'Red'
    Say "  先登录进主界面并连上 PLC,再重跑。退出码 4 表示'没验成'。" 'Red'
    Say "════════════════════════════════════════════════════" 'Red'
}
Say "" ; Say "⚠ 外推警告:上面 24h 的数字是拿 $spanMin 分钟的线性斜率乘出来的。" 'Yellow'
Say "  泄漏通常不是线性的(定时器累积、缓存到阈值才涨),线性外推会【低估】。" 'Yellow'
Say "  想真知道 24 小时什么样,就得真跑 24 小时。" 'Yellow'

# ---- 退出码:有 FAIL 就不能是 0 ----
#   写这条是因为实测踩到过:判定判了 FAIL,退出码还给 0。
#   任何拿 $LASTEXITCODE 做门禁的东西(CI、定时任务、一行 if 判断)
#   都会把一次 FAIL 读成成功 —— 报告里红着,程序说"没事"。
#   报告给人看,退出码给机器判,两个必须一致。
#
#   ★ 2026-10-14 新增 exit 4 = 本轮没有结论。
#   idle 时所有资源判据都是 UNKNOWN,拿 0 告诉调用方"验过了"是撒谎:
#   调用方无法区分"验过没问题"和"根本没能力验"。
#   给它一个专属码,想把它当门禁的人必须显式决定怎么办(通常是不该算通过)。
#   4 不与 1/3 冲突,调用方可以分别对待"验出问题"和"没验成"。
$failCount = @($report | Where-Object { $_ -match '^\[FAIL\]' }).Count
$warnCount = @($report | Where-Object { $_ -match '^\[WARN\]' }).Count
$unkCount  = @($report | Where-Object { $_ -match '^\[UNKNOWN\]' }).Count

if ($failCount -gt 0) { exit 1 }
if ($warnCount -gt 0) { exit 3 }
if ($unkCount  -gt 0) { exit 4 }
exit 0