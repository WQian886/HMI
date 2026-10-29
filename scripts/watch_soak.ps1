# ============================================================================
# watch_soak.ps1 —— 长跑看门狗(只看不碰)
#
#  它要堵的洞是什么
#  --------------
#  2026-10-21 翻 WPF 项目的旧账时翻到一条记录:那一轮 24 小时验收批在
#  00:40 死了,58 个采样点之后就再没人知道,而 README 还"跑批中"挂了两天。
#  那条记录自己写下的结论是一句话:
#
#      "跑批是 session-only,崩了无人发现。"
#
#  这一轮(soaks\20261021_164804)已经把"session-only"那半句解决了:
#  harness 是 detached 的独立进程,我的会话断了它照跑;verdict.txt 每个采样
#  tick 都刷新,死了也留得下"跑到哪了"。但还剩【最后一层】没人管:
#
#      harness 在 03:00 死了 ⇒ 什么都不响,一直安静到 2026-10-22 17:13 的提醒。
#
#  这个脚本补的就是这一层:每隔几分钟【只读】地看一眼,任何一个不对就往
#  watchdog.log 里写一行"出了什么事",并把当前状态写进 watchdog_ALERT.txt。
#
#  ★ 它【绝不】结束任何进程
#    - 不 kill hmi.exe(新的、旧的都不动)
#    - 不 kill python 从站
#    - 不 kill harness
#    - 进程存活用 Get-Process 查,数据只读文件
#    理由有二:
#      ① 正在跑的那一轮不能被"看门狗"搅黄;
#      ② 结束别人起的进程需要你明确点头,看门狗自己没这个权限。
#
#  看什么(五件)
#  ------------
#  (A) verdict.txt 的修改时间 —— harness 每 60 秒刷新一次
#      ⇒ 超过 StaleMinutes 没动 = harness 不在了(卡死/被杀/机器睡了)
#  (B) harness 进程还在不在(RUN_INFO.txt 里的 PID)
#  (C) 被测 hmi.exe 进程还在不在(verdict.txt 里的 PID)
#  (D) 从站还活着没(slave.log 还在长) —— 从站死了,应用会开始报错
#  (E) ★ 业务还活着没 —— 见下面那段,这是【上一轮真正的死法】
#
#  (E) 为什么单看进程存活不够
#  --------------------------
#  上一轮(2026-10-20)那 16.5 小时零轮询流量,进程一直是活的、句柄正常、
#  CPU 也不高 —— 唯一不对劲的是【一次都没跟 PLC 说过话】。
#  根因是 2026-10-21 查出来的:协议级失败不发 readFinished,而 PollScheduler
#  清 inFlight_ 的唯一入口就是 readFinished ⇒ 一次失败之后那台 PLC 就再也
#  收不到任何读请求,而且【日志里一声不吭】。
#
#  ⇒ "进程还在"根本不是活着的证据,"成功读次数还在涨"才是。
#  应用日志每 60 秒打一行轮询统计:
#      轮询统计:发出=10902 成功=10902 失败=0 在途=0 PLC=1 变量=3
#  看门狗就盯这个"成功"字段:窗口内没涨 ⇒ 报"程序活着但业务停了"。
#
#  怎么用
#  ------
#    .\watch_soak.ps1 -RunDir <长跑目录>            # 循环盯着(默认 5 分钟一轮)
#    .\watch_soak.ps1 -RunDir <长跑目录> -Once      # 只看一次就退出
#    .\watch_soak.ps1 -SelfTest                     # 自测(不碰真长跑)
#
#  它自己会退场
#  ------------
#  ① verdict 状态变成 DONE ⇒ 这一轮跑完了,看门狗退出。
#  ② 超过"计划时长 + 1 小时" ⇒ 到点没跑完也算收场,看门狗退出并留下记录。
#  (免得看门狗自己变成一个没人认领的孤儿进程。)
#
#  ★ 本文件必须是 UTF-8 with BOM
#    Windows PowerShell 5.1 读无 BOM 的 .ps1 会按 ANSI/GBK 解析,中文变乱码,
#    乱码的字节还可能吃掉一个引号,报出 "Unexpected token ')'" 这种跟内容
#    毫不相干的语法错。2026-10-21 在 check_ps_format_leak.ps1 上踩过一次。
# ============================================================================

[CmdletBinding()]
param(
    # 长跑产物目录(也就是有 verdict.txt / RUN_INFO.txt 的那个目录)
    [string] $RunDir = '',
    # 每一轮隔多少秒看一次
    [int]    $IntervalSec = 300,
    # 多久没动静算异常(分钟)
    [int]    $StaleMinutes = 20,
    # 只看一次就退出
    [switch] $Once,
    # 自测:造假现场,不动真长跑
    [switch] $SelfTest
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Continue'

# ============================================================================
# 小工具
# ============================================================================

function Test-ProcAlive {
    # 进程在不在。用 try/catch 而不是 -ErrorAction SilentlyContinue:
    # 后者会把"查不到"变成一条非终止错误,脚本整体退出码被带成 1,
    # 在 ctest 里会被误读成"测试失败"。
    param([int] $ProcId)
    if ($ProcId -le 0) { return $false }
    $p = $null
    try   { $p = Get-Process -Id $ProcId -ErrorAction Stop }
    catch { $p = $null }
    return ($null -ne $p)
}

function Get-DeadPid {
    # 找一个【当前确实不存在】的 PID,给自测当"死掉的进程"用。
    # 写死一个数字是有风险的 —— 万一那个 PID 正好是谁家的,自测就会时绿时红。
    param([int] $Start = 60000)
    $cand = $Start
    while ($cand -lt 65000) {
        $alive = $false
        try   { $null = Get-Process -Id $cand -ErrorAction Stop; $alive = $true }
        catch { $alive = $false }
        if (-not $alive) { return $cand }
        $cand += 4
    }
    return -1
}

function Write-WatchLine {
    param(
        [string] $Dir,
        [string] $Text,
        [bool]   $IsAlert = $false
    )
    $stamp = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
    $tag = 'INFO'
    if ($IsAlert) { $tag = 'ALERT' }
    $line = "[$stamp] [$tag] $Text"
    try {
        Add-Content -LiteralPath (Join-Path $Dir 'watchdog.log') -Value $line -Encoding UTF8
    } catch {
        # ★ 日志写不下去也不能让看门狗自己死掉 —— 它死了就等于没人看门了。
    }
    Write-Output $line
}

function Write-WatchAlertFile {
    # 永远反映【最近一次检查】的状态:有问题就写问题,没问题就写"正常"。
    # 这样光看这个文件就知道现在是什么情况,不用去翻 watchdog.log。
    param(
        [string]   $Dir,
        [string[]] $Problems
    )
    $body = New-Object System.Collections.Generic.List[string]
    $body.Add('长跑看门狗 —— 最近一次检查')
    $body.Add('检查时刻 : ' + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))
    $body.Add('长跑目录 : ' + $Dir)
    $body.Add('')
    if ($null -eq $Problems -or $Problems.Count -eq 0) {
        $body.Add('状态: 正常,没有发现问题。')
    } else {
        $body.Add("状态: ★ 有问题,共 $($Problems.Count) 条:")
        foreach ($p in $Problems) { $body.Add('  - ' + $p) }
    }
    try {
        Set-Content -LiteralPath (Join-Path $Dir 'watchdog_ALERT.txt') -Value $body -Encoding UTF8
    } catch {
        # 同上:写不下去也不能死。
    }
}

# ============================================================================
# 读现场
# ============================================================================

function Get-PollStats {
    # 把应用日志里的轮询统计行解析成结构。
    # 这一段是 (E) 的数据来源 —— "成功读次数还在涨"是活着的证据,
    # 进程还在不是。
    param([string] $LogPath)
    $out = New-Object System.Collections.Generic.List[object]
    if ([string]::IsNullOrWhiteSpace($LogPath)) { return $out }
    if (-not (Test-Path -LiteralPath $LogPath)) { return $out }

    foreach ($l in @(Get-Content -LiteralPath $LogPath -Encoding UTF8)) {
        if ($l -notmatch '轮询统计') { continue }
        # ★ 允许 "发出= 100" 这种带空格的写法:
        #   2026-10-20 之前程序打的就是带空格的那种(QDebug 在相邻两项之间插空格),
        #   2026-10-21 才修掉。看门狗不能因为程序换了写法就瞎报。
        if ($l -notmatch '发出=\s*(\d+)\s+成功=\s*(\d+)\s+失败=\s*(\d+)\s+在途=\s*(\d+)') { continue }
        $sent  = [int]$Matches[1]
        $ok    = [int]$Matches[2]
        $fail  = [int]$Matches[3]
        $infl  = [int]$Matches[4]

        $t = [datetime]::MinValue
        if ($l -match '^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})') {
            $ts = $Matches[1]
            try {
                $t = [datetime]::ParseExact($ts, 'yyyy-MM-dd HH:mm:ss.fff',
                                           [Globalization.CultureInfo]::InvariantCulture)
            } catch {
                $t = [datetime]::MinValue
            }
        }
        $out.Add([pscustomobject]@{
            Time = $t; Sent = $sent; Ok = $ok; Fail = $fail; Inflight = $infl
        })
    }
    return $out
}

function Get-SoakSnapshot {
    # 把一个长跑目录读成一份"现在什么情况"的快照。
    # 全部是只读:读文件、查进程,不动任何东西。
    param([string] $Dir)

    $verdictPath = Join-Path $Dir 'verdict.txt'
    $infoPath    = Join-Path $Dir 'RUN_INFO.txt'
    $slavePath   = Join-Path $Dir 'slave.log'

    $snap = [pscustomobject]@{
        Dir           = $Dir
        VerdictExists = $false
        State         = 'UNKNOWN'
        AppPid        = 0
        AppStart      = [datetime]::MinValue
        HarnessPid    = 0
        PlanHours     = 0
        DataDir       = (Join-Path $Dir 'appdata\QianWei\HMI')
        AppLogPath    = ''
        LastStat      = $null
        VerdictAgeMin = -1.0
        SlaveAgeMin   = -1.0
        SlaveExists   = $false
        AppAlive      = $false
        HarnessAlive  = $false
    }

    if (Test-Path -LiteralPath $verdictPath) {
        $snap.VerdictExists = $true
        $snap.VerdictAgeMin = [math]::Round(
            ((Get-Date) - (Get-Item -LiteralPath $verdictPath).LastWriteTime).TotalMinutes, 1)

        foreach ($l in @(Get-Content -LiteralPath $verdictPath -Encoding UTF8)) {
            if ($snap.State -eq 'UNKNOWN' -and $l -match '^\s*状态\s*[:：]\s*(\S+)\s*$') {
                $snap.State = $Matches[1]
            }
            if ($snap.AppPid -eq 0 -and
                $l -match '被测进程\s*[:：]\s*PID=(\d+)\s+启动于\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})') {
                $snap.AppPid = [int]$Matches[1]
                try {
                    $snap.AppStart = [datetime]::ParseExact($Matches[2], 'yyyy-MM-dd HH:mm:ss',
                                                            [Globalization.CultureInfo]::InvariantCulture)
                } catch {
                    $snap.AppStart = [datetime]::MinValue
                }
            }
            if ($snap.PlanHours -eq 0 -and $l -match '计划时长\s*[:：]\s*(\d+)\s*小时') {
                $snap.PlanHours = [int]$Matches[1]
            }
            if ($l -match '数据目录\s*[:：]\s*(.+?)\s*$') {
                $snap.DataDir = $Matches[1]
            }
        }
    }

    if (Test-Path -LiteralPath $infoPath) {
        foreach ($l in @(Get-Content -LiteralPath $infoPath -Encoding UTF8)) {
            if ($l -match 'harness PID\s*[:：]\s*(\d+)') {
                $snap.HarnessPid = [int]$Matches[1]
                break
            }
        }
    }

    if (Test-Path -LiteralPath $slavePath) {
        $snap.SlaveExists = $true
        $snap.SlaveAgeMin = [math]::Round(
            ((Get-Date) - (Get-Item -LiteralPath $slavePath).LastWriteTime).TotalMinutes, 1)
    }

    # 应用日志:取最新的那个(有滚动阈值,理论上会有第二个文件)
    $logDir = Join-Path $snap.DataDir 'logs'
    if (Test-Path -LiteralPath $logDir) {
        $newest = Get-ChildItem -LiteralPath $logDir -Filter '*.log' -File -ErrorAction SilentlyContinue |
                  Sort-Object LastWriteTime -Descending |
                  Select-Object -First 1
        if ($null -ne $newest) { $snap.AppLogPath = $newest.FullName }
    }

    # 最后一条轮询统计 —— 用来在日志里报出【真实数字】。
    # 光写"一切正常"是没用的:24 小时无人值守时,这条日志本身就是证据。
    $snap.LastStat = $null
    if (-not [string]::IsNullOrWhiteSpace($snap.AppLogPath) -and (Test-Path -LiteralPath $snap.AppLogPath)) {
        $stats = @(Get-PollStats -LogPath $snap.AppLogPath)
        if ($stats.Count -gt 0) { $snap.LastStat = $stats[-1] }
    }

    $snap.HarnessAlive = Test-ProcAlive $snap.HarnessPid
    $snap.AppAlive     = Test-ProcAlive $snap.AppPid

    return $snap
}

function Get-SoakProblem {
    # 把快照翻译成"问题清单"。空清单 = 一切正常。
    param(
        $Snap,
        [int] $StaleMinutes
    )
    $probs = New-Object System.Collections.Generic.List[string]

    if (-not $Snap.VerdictExists) {
        $probs.Add('判定文件 verdict.txt 不在 —— 长跑目录被清过?')
        return $probs
    }

    # 跑完了就没什么可担心的了 —— 收工。
    if ($Snap.State -eq 'DONE') { return $probs }

    if ($Snap.State -eq 'UNKNOWN') {
        $probs.Add('verdict.txt 里认不出状态字段 —— 文件可能被写坏了')
    } elseif ($Snap.State -ne 'RUNNING') {
        $probs.Add("状态不是 RUNNING,而是 '$($Snap.State)' —— 这一轮可能已经出问题了")
    }

    if (-not $Snap.HarnessAlive) {
        $probs.Add("harness 进程(PID $($Snap.HarnessPid))不在了 —— 采样循环已经死了")
    }
    if ($Snap.VerdictAgeMin -gt $StaleMinutes) {
        $probs.Add("verdict.txt 已经 $($Snap.VerdictAgeMin) 分钟没刷新(阈值 $StaleMinutes)—— harness 不动了")
    }
    if ($Snap.AppPid -gt 0 -and -not $Snap.AppAlive) {
        $probs.Add("被测 hmi.exe(PID $($Snap.AppPid))不在了 —— 被测程序退了")
    }
    if ($Snap.SlaveExists -and $Snap.SlaveAgeMin -gt $StaleMinutes) {
        $probs.Add("从站 slave.log 已经 $($Snap.SlaveAgeMin) 分钟没长 —— 从站可能死了")
    }

    # ★ (E) 业务活体 —— 上一轮真正的死法就在这一条里。
    #   进程活着不等于在干活:上一轮 16.5 小时零轮询流量,进程全程健在。
    #   所以这里盯【成功读次数有没有在涨】,而不是盯进程。
    $appAgeMin = -1.0
    if ($Snap.AppStart -ne [datetime]::MinValue) {
        $appAgeMin = [math]::Round(((Get-Date) - $Snap.AppStart).TotalMinutes, 1)
    }
    if ($appAgeMin -ge 0 -and $appAgeMin -lt $StaleMinutes) {
        # 程序刚起来还没到阈值,现在判"没在轮询"是不公平的(它在等自检)。
        # 这一轮跳过,下一轮再看。
        return $probs
    }

    if ([string]::IsNullOrWhiteSpace($Snap.AppLogPath) -or -not (Test-Path -LiteralPath $Snap.AppLogPath)) {
        # 日志目录在,日志文件却没有 —— 说明程序压根没在写日志。
        # (这跟"日志有但没有统计行"是两种病,分开说,免得看的人查错方向。)
        $probs.Add("应用连日志文件都没有(目录 $($Snap.DataDir)\logs)—— 程序活着但没在写日志")
    } else {
        $stats = @(Get-PollStats -LogPath $Snap.AppLogPath)
        if ($stats.Count -eq 0) {
            $probs.Add('应用日志里一条轮询统计都没有 —— 程序活着但没在轮询')
        } else {
            $cut = (Get-Date).AddMinutes(-$StaleMinutes)
            $recent = @($stats | Where-Object { $_.Time -ge $cut })
            if ($recent.Count -eq 0) {
                $lastStat = $stats[-1]
                $probs.Add("最近 $StaleMinutes 分钟里没有一条轮询统计(最后一条 $($lastStat.Time.ToString('MM-dd HH:mm:ss')))—— 程序活着但业务停了")
            } else {
                $firstOk  = $recent[0].Ok
                $lastStat = $recent[-1]
                if ($lastStat.Ok -le $firstOk) {
                    $probs.Add("轮询成功次数在 $StaleMinutes 分钟内没涨(还是 $($lastStat.Ok))—— 发出=$($lastStat.Sent) 失败=$($lastStat.Fail) 在途=$($lastStat.Inflight);这正是上一轮 16.5 小时零流量的死法")
                }
            }
        }
    }

    return $probs
}

# ============================================================================
# 主循环
# ============================================================================

function Invoke-Watch {
    param(
        [string] $Dir,
        [int]    $IntervalSec,
        [int]    $StaleMinutes,
        [bool]   $Once
    )

    Write-WatchLine -Dir $Dir -Text "看门狗启动: 目录=$Dir 间隔=${IntervalSec}s 阈值=${StaleMinutes}分钟"

    # 到点收场:计划时长 + 1 小时宽限。免得看门狗自己变成没人认领的孤儿。
    $deadline = [datetime]::MaxValue
    $snap0 = Get-SoakSnapshot -Dir $Dir
    $runStart = [datetime]::MinValue
    if ($snap0.AppStart -ne [datetime]::MinValue) { $runStart = $snap0.AppStart }
    if ($snap0.PlanHours -gt 0 -and $runStart -ne [datetime]::MinValue) {
        $deadline = $runStart.AddHours($snap0.PlanHours).AddHours(1)
        Write-WatchLine -Dir $Dir -Text "到点收场时间: $($deadline.ToString('yyyy-MM-dd HH:mm:ss'))"
    }

    while ($true) {
        $state = 'UNKNOWN'
        $problems = @()
        $snap = $null      # ★ StrictMode 2.0:try 抛异常时这个变量必须已经存在
        try {
            $snap = Get-SoakSnapshot -Dir $Dir
            $state = $snap.State
            $problems = @(Get-SoakProblem -Snap $snap -StaleMinutes $StaleMinutes)
        } catch {
            $problems = @('看门狗自己读现场时出错了: ' + $_.Exception.Message)
        }

        if ($state -eq 'DONE') {
            Write-WatchLine -Dir $Dir -Text '长跑已结束(状态 DONE)—— 看门狗退场。'
            Write-WatchAlertFile -Dir $Dir -Problems @()
            break
        }

        if ($problems.Count -gt 0) {
            Write-WatchLine -Dir $Dir -IsAlert $true `
                -Text ("★ 异常($($problems.Count) 条): " + ($problems -join ' | '))
        } else {
            # ★ 健康的时候也把【真实数字】写进去。
            #   光写"一切正常"是没用的 —— 24 小时无人值守时,
            #   这条日志本身就是证据:看门狗每一轮都记下当时观测到的
            #   成功读次数/在途数,事后翻一眼就知道业务是一直在涨、
            #   还是中途停住了又恢复。
            $detail = ''
            try {
                if ($null -ne $snap -and $null -ne $snap.LastStat) {
                    $detail = ("  成功=" + $snap.LastStat.Ok +
                               " 发出=" + $snap.LastStat.Sent +
                               " 失败=" + $snap.LastStat.Fail +
                               " 在途=" + $snap.LastStat.Inflight +
                               " (统计时刻 " + $snap.LastStat.Time.ToString('MM-dd HH:mm:ss') + ")")
                }
                if ($null -ne $snap) {
                    $detail += ("  判定刷新于 " + $snap.VerdictAgeMin + " 分钟前")
                }
            } catch {
                $detail = '  (取数字失败,但没有异常)'
            }
            Write-WatchLine -Dir $Dir -Text ('一切正常(状态 RUNNING,无异常)。' + $detail)
        }
        Write-WatchAlertFile -Dir $Dir -Problems $problems

        if ($Once) { break }

        if ((Get-Date) -gt $deadline) {
            Write-WatchLine -Dir $Dir -IsAlert $true `
                -Text "★ 到了收场时间但状态还是 '$state' —— 长跑没跑完,看门狗退场(判定不算数)。"
            break
        }

        Start-Sleep -Seconds $IntervalSec
    }
}

# ============================================================================
# 自测:造假现场,验证"该报的报、不该报的不报"
# ============================================================================

function New-FixtureDir {
    param([string] $Root, [string] $Name)
    $d = Join-Path $Root $Name
    if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force }
    New-Item -ItemType Directory -Path $d -Force | Out-Null
    return $d
}

function Write-Fixture {
    # 造一份"看起来像长跑目录"的东西。
    # $AppLogMode: 'growing' = 成功在涨; 'stuck' = 成功不涨; 'none' = 压根没有统计行
    param(
        [string] $Dir,
        [string] $State,
        [int]    $AppPid,
        [int]    $HarnessPid,
        [int]    $VerdictAgeMin,
        [int]    $SlaveAgeMin,
        [string] $AppLogMode = 'growing',
        [bool]   $WithSlave = $true
    )
    $dataDir = Join-Path $Dir 'appdata\QianWei\HMI'
    $logDir  = Join-Path $dataDir 'logs'
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null

    $started = (Get-Date).AddMinutes(-60).ToString('yyyy-MM-dd HH:mm:ss')
    Set-Content -LiteralPath (Join-Path $Dir 'verdict.txt') -Encoding UTF8 -Value @(
        '================ HMI 长跑 verdict ================',
        "状态              : $State",
        '本次落盘          : ' + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss'),
        '计划时长          : 24 小时',
        "数据目录          : $dataDir",
        "被测进程          : PID=$AppPid 启动于 $started"
    )
    Set-Content -LiteralPath (Join-Path $Dir 'RUN_INFO.txt') -Encoding UTF8 -Value @(
        '长跑启动于   : ' + $started,
        "harness PID  : $HarnessPid"
    )

    if ($WithSlave) {
        Set-Content -LiteralPath (Join-Path $Dir 'slave.log') -Encoding UTF8 -Value 'slave start'
        if ($SlaveAgeMin -gt 0) {
            (Get-Item -LiteralPath (Join-Path $Dir 'slave.log')).LastWriteTime = (Get-Date).AddMinutes(-$SlaveAgeMin)
        }
    }

    if ($AppLogMode -eq 'none') {
        # 日志文件在,但里面一条轮询统计都没有 —— 这是"程序活着却没在轮询"的样子。
        # (另有一种是连日志文件都没有,那种更糟,自测第 12 条覆盖。)
        $t1 = (Get-Date).AddMinutes(-10)
        $t2 = (Get-Date).AddMinutes(-1)
        Set-Content -LiteralPath (Join-Path $logDir 'hmi_test.log') -Encoding UTF8 -Value @(
            ($t1.ToString('yyyy-MM-dd HH:mm:ss.fff') + ' [INFO ] default - [HistoryRecorder] 心跳写了 3 行'),
            ($t2.ToString('yyyy-MM-dd HH:mm:ss.fff') + ' [INFO ] default - [HistoryRecorder] 心跳写了 3 行')
        )
    } elseif ($AppLogMode -eq 'nolog') {
        # 连日志文件都不造
    } else {
        $t1 = (Get-Date).AddMinutes(-10)
        $t2 = (Get-Date).AddMinutes(-1)
        $ok1 = 100
        $ok2 = 200
        if ($AppLogMode -eq 'stuck') { $ok2 = $ok1 }   # 成功不涨 —— 上一轮的死法
        $lines = @(
            ($t1.ToString('yyyy-MM-dd HH:mm:ss.fff') + ' [INFO ] hmi.poll - [PollScheduler] 轮询统计:发出=' + $ok1 + ' 成功=' + $ok1 + ' 失败=0 在途=0 PLC=1 变量=3'),
            ($t2.ToString('yyyy-MM-dd HH:mm:ss.fff') + ' [INFO ] hmi.poll - [PollScheduler] 轮询统计:发出=' + $ok2 + ' 成功=' + $ok2 + ' 失败=1 在途=1 PLC=1 变量=3')
        )
        Set-Content -LiteralPath (Join-Path $logDir 'hmi_test.log') -Encoding UTF8 -Value $lines
    }

    if ($VerdictAgeMin -gt 0) {
        (Get-Item -LiteralPath (Join-Path $Dir 'verdict.txt')).LastWriteTime = (Get-Date).AddMinutes(-$VerdictAgeMin)
    }
}

function Test-Expect {
    param(
        $Results,
        [string] $Name,
        [bool]   $Ok,
        [string] $Detail = ''
    )
    $Results.Add([pscustomobject]@{ Name = $Name; Ok = $Ok; Detail = $Detail })
}

function Invoke-SelfTest {
    $root = Join-Path ([System.IO.Path]::GetTempPath()) ('watch_soak_selftest_' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    $results = New-Object System.Collections.Generic.List[object]

    $alive = $PID
    $dead  = Get-DeadPid
    if ($dead -lt 0) {
        Write-Host 'RED  找不到一个空闲的 PID 当"死进程"用,自测没法做'
        Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
        return 1
    }

    # 1) 健康的一轮 —— 什么也不该报
    $d = New-FixtureDir $root 'healthy'
    Write-Fixture -Dir $d -State 'RUNNING' -AppPid $alive -HarnessPid $alive -VerdictAgeMin 0 -SlaveAgeMin 0 -AppLogMode 'growing'
    $p = @(Get-SoakProblem -Snap (Get-SoakSnapshot -Dir $d) -StaleMinutes 20)
    Test-Expect $results '健康的一轮不该报任何问题' ($p.Count -eq 0) ('实际: ' + ($p -join ' | '))

    # 2) harness 死了
    $d = New-FixtureDir $root 'dead_harness'
    Write-Fixture -Dir $d -State 'RUNNING' -AppPid $alive -HarnessPid $dead -VerdictAgeMin 0 -SlaveAgeMin 0 -AppLogMode 'growing'
    $p = @(Get-SoakProblem -Snap (Get-SoakSnapshot -Dir $d) -StaleMinutes 20)
    Test-Expect $results 'harness 死了要报出来' (($p -join ' ') -match 'harness') ('实际: ' + ($p -join ' | '))

    # 3) verdict 停更(机器睡了 / harness 卡死)
    $d = New-FixtureDir $root 'stale_verdict'
    Write-Fixture -Dir $d -State 'RUNNING' -AppPid $alive -HarnessPid $alive -VerdictAgeMin 60 -SlaveAgeMin 0 -AppLogMode 'growing'
    $p = @(Get-SoakProblem -Snap (Get-SoakSnapshot -Dir $d) -StaleMinutes 20)
    Test-Expect $results 'verdict 停更要报出来' (($p -join ' ') -match '没刷新') ('实际: ' + ($p -join ' | '))

    # 4) 被测程序退了
    $d = New-FixtureDir $root 'dead_app'
    Write-Fixture -Dir $d -State 'RUNNING' -AppPid $dead -HarnessPid $alive -VerdictAgeMin 0 -SlaveAgeMin 0 -AppLogMode 'growing'
    $p = @(Get-SoakProblem -Snap (Get-SoakSnapshot -Dir $d) -StaleMinutes 20)
    Test-Expect $results '被测程序退了要报出来' (($p -join ' ') -match '被测') ('实际: ' + ($p -join ' | '))

    # 5) 从站死了
    $d = New-FixtureDir $root 'dead_slave'
    Write-Fixture -Dir $d -State 'RUNNING' -AppPid $alive -HarnessPid $alive -VerdictAgeMin 0 -SlaveAgeMin 60 -AppLogMode 'growing'
    $p = @(Get-SoakProblem -Snap (Get-SoakSnapshot -Dir $d) -StaleMinutes 20)
    Test-Expect $results '从站死了要报出来' (($p -join ' ') -match '从站') ('实际: ' + ($p -join ' | '))

    # 6) ★ 业务不涨 —— 上一轮真正的死法(进程活着,但一次都没跟 PLC 说过话)
    $d = New-FixtureDir $root 'business_stuck'
    Write-Fixture -Dir $d -State 'RUNNING' -AppPid $alive -HarnessPid $alive -VerdictAgeMin 0 -SlaveAgeMin 0 -AppLogMode 'stuck'
    $p = @(Get-SoakProblem -Snap (Get-SoakSnapshot -Dir $d) -StaleMinutes 20)
    Test-Expect $results '成功读次数不涨要报出来(进程还活着的那种死法)' (($p -join ' ') -match '没涨') ('实际: ' + ($p -join ' | '))

    # 7) 应用日志里一条统计都没有
    $d = New-FixtureDir $root 'no_poll_stats'
    Write-Fixture -Dir $d -State 'RUNNING' -AppPid $alive -HarnessPid $alive -VerdictAgeMin 0 -SlaveAgeMin 0 -AppLogMode 'none'
    $p = @(Get-SoakProblem -Snap (Get-SoakSnapshot -Dir $d) -StaleMinutes 20)
    Test-Expect $results '一条轮询统计都没有要报出来' (($p -join ' ') -match '轮询统计') ('实际: ' + ($p -join ' | '))

    # 8) 跑完了不算问题(哪怕 PID 都不在了)
    $d = New-FixtureDir $root 'done'
    Write-Fixture -Dir $d -State 'DONE' -AppPid $dead -HarnessPid $dead -VerdictAgeMin 600 -SlaveAgeMin 600 -AppLogMode 'growing'
    $p = @(Get-SoakProblem -Snap (Get-SoakSnapshot -Dir $d) -StaleMinutes 20)
    Test-Expect $results '状态 DONE 时不该报错' ($p.Count -eq 0) ('实际: ' + ($p -join ' | '))

    # 9) 状态既不是 RUNNING 也不是 DONE
    $d = New-FixtureDir $root 'preflight_failed'
    Write-Fixture -Dir $d -State 'PREFLIGHT_FAILED' -AppPid $alive -HarnessPid $alive -VerdictAgeMin 0 -SlaveAgeMin 0 -AppLogMode 'growing'
    $p = @(Get-SoakProblem -Snap (Get-SoakSnapshot -Dir $d) -StaleMinutes 20)
    Test-Expect $results '状态不是 RUNNING 要报出来' (($p -join ' ') -match 'PREFLIGHT_FAILED') ('实际: ' + ($p -join ' | '))

    # 10) 刚起来还没到阈值 —— 不能因为"还没开始轮询"就瞎报
    $d = New-FixtureDir $root 'just_started'
    Write-Fixture -Dir $d -State 'RUNNING' -AppPid $alive -HarnessPid $alive -VerdictAgeMin 0 -SlaveAgeMin 0 -AppLogMode 'none'
    $snap = Get-SoakSnapshot -Dir $d
    $snap.AppStart = (Get-Date).AddMinutes(-2)   # 模拟"程序才起来 2 分钟"
    $p = @(Get-SoakProblem -Snap $snap -StaleMinutes 20)
    Test-Expect $results '程序刚起来不该被判成没在轮询' ($p.Count -eq 0) ('实际: ' + ($p -join ' | '))

    # 11) 带空格的旧格式("发出= 100")也要能认出来
    $d = New-FixtureDir $root 'legacy_spaced_format'
    Write-Fixture -Dir $d -State 'RUNNING' -AppPid $alive -HarnessPid $alive -VerdictAgeMin 0 -SlaveAgeMin 0 -AppLogMode 'growing'
    $lg = Join-Path $d 'appdata\QianWei\HMI\logs\hmi_test.log'
    $t1 = (Get-Date).AddMinutes(-10)
    $t2 = (Get-Date).AddMinutes(-1)
    Set-Content -LiteralPath $lg -Encoding UTF8 -Value @(
        ($t1.ToString('yyyy-MM-dd HH:mm:ss.fff') + ' [INFO ] hmi.poll - [PollScheduler] 轮询统计:发出= 100 成功= 100 失败= 0 在途= 0 PLC=1 变量=3'),
        ($t2.ToString('yyyy-MM-dd HH:mm:ss.fff') + ' [INFO ] hmi.poll - [PollScheduler] 轮询统计:发出= 200 成功= 200 失败= 0 在途= 0 PLC=1 变量=3')
    )
    $p = @(Get-SoakProblem -Snap (Get-SoakSnapshot -Dir $d) -StaleMinutes 20)
    Test-Expect $results '旧格式(带空格)也要认得出"在涨"' ($p.Count -eq 0) ('实际: ' + ($p -join ' | '))

    # 12) 连日志文件都没有 —— 比"没统计行"还糟一档
    $d = New-FixtureDir $root 'no_log_at_all'
    Write-Fixture -Dir $d -State 'RUNNING' -AppPid $alive -HarnessPid $alive -VerdictAgeMin 0 -SlaveAgeMin 0 -AppLogMode 'nolog'
    $p = @(Get-SoakProblem -Snap (Get-SoakSnapshot -Dir $d) -StaleMinutes 20)
    Test-Expect $results '应用连日志文件都没有要报出来' (($p -join ' ') -match '日志文件都没有') ('实际: ' + ($p -join ' | '))

    $okCount  = @($results | Where-Object { $_.Ok }).Count
    $redCount = @($results | Where-Object { -not $_.Ok }).Count
    foreach ($r in $results) {
        if ($r.Ok) { Write-Host ('OK   ' + $r.Name) }
        else      { Write-Host ('RED  ' + $r.Name + '  ' + $r.Detail) }
    }
    Write-Host ("---- OK=$okCount RED=$redCount")
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    if ($redCount -gt 0) { return 1 }
    return 0
}

# ============================================================================
# 入口
# ============================================================================

if ($SelfTest) {
    exit (Invoke-SelfTest)
}

if ([string]::IsNullOrWhiteSpace($RunDir)) {
    Write-Host '用法: .\watch_soak.ps1 -RunDir <长跑目录>   (或 -SelfTest)'
    exit 2
}
if (-not (Test-Path -LiteralPath $RunDir)) {
    Write-Host ("长跑目录不存在: " + $RunDir)
    exit 2
}

$resolved = (Resolve-Path -LiteralPath $RunDir).Path
Invoke-Watch -Dir $resolved -IntervalSec $IntervalSec -StaleMinutes $StaleMinutes -Once:$Once
exit 0
