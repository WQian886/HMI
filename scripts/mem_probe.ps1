<#
.SYNOPSIS
    私有内存按"每请求"还是按"每单位时间"涨?—— 一个对照实验。

.DESCRIPTION
    24 小时长跑留下一个 WARN:私有内存 21.41 → 45.68 MB,斜率约 1.01 MB/小时,
    没有平台期,一直线性涨。这条 WARN 有两种完全不同的读法:

      (A) 每发一次 Modbus 请求泄漏一点 —— 修一处泄漏就完事;
      (B) 每过一段时间长一点(碎片 / 周期任务 / 累积容器)—— 跟请求量无关,
          缩请求率没用,得另找。

    为什么长跑数据自己分不出来:
      那 24 小时里轮询频率是恒定的,"每请求涨多少"和"每分钟涨多少"是同一个数。
      把两者画在一起,永远是同一条直线。要分开,必须【只改请求率、不改墙钟时间】。

    ── ★ 放大器为什么是【多 PLC】而不是【多变量】(2026-10-23 改) ★ ──────
    第一版设计是"对照组 3 个连续地址变量 vs 实验组 16 个非连续地址变量",
    指望后者因为合并不了而多发 15 倍请求。这个设计是【作废的】,原因:

      PollScheduler::onTick 里的单 in-flight 防御把整个 PLC 的变量【整批挡掉】:
        - 变量按地址排序后拆成多批,每批发一帧;
        - 第 1 帧发出去,pending_ 非空;
        - 第 2 帧起全部撞上限流被拒收 → BUGFIX-B1 回滚 inFlight_,
          nextPollAtMs = now + 100(100 ms 后重发)。
      所以【一台 PLC 上的非连续变量,请求率上限就是 10 次/秒】
      (1 帧 / 100 ms),跟变量个数基本无关。
      16 个变量实测只拿到约 2 倍,不是设计的 16 倍。
      —— 换句话说,那个实验跑出来的斜率【不可换算】。

    正确的放大器是【多 PLC】:
      PollScheduler::onTick 按 plcId 分组(dueByPlc),
      【每台 PLC 有自己的 driver、自己的 pending_】——
      跨 PLC 不撞限流,每台都能跑满自己的帧率。
      N 台 PLC ≈ N 倍请求率,而每台内部仍然合批(连续地址还是 1 帧)。

      对照组:1 台 PLC × 3 个连续地址变量   → 1 帧 / 200 ms  ≈  5 请求/秒
      实验组:8 台 PLC × 3 个连续地址变量   → 8 帧 / 200 ms ≈ 40 请求/秒
      倍数 = 8(= PLC 台数),可精确预期、可核对。

    ★ 每台 PLC 用自己的端口和自己的从站进程 ★
      两组都起 8 个从站(从站很便宜),但只有实验组那 8 台真的接了 PLC。
      不这么做的话,对照组那台 PLC 也要有从站 —— 端口分配不能撞。
      【每组各自的从站端口段,互不重叠】,这是本实验唯一不可协商的前提。

    判读:
      实验组斜率 ≈ 对照组 × 8    →  (A) 按请求漏,顺着请求路径查。
      两组斜率都接近 0           →  (B) 24 小时那条 1 MB/h 不是按请求的,
                                    得另跑更长的实验才看得见。
      两组都涨、比例不对         →  混着两件事,先拆 (A)。

    隔离:两个实例各有 HMI_DATA_DIR / HMI_INSTANCE_KEY,从站也各占一个端口。
    脚本只结束自己 start 时记下的那些 PID,不会碰机器上别的 hmi.exe。

.PARAMETER Minutes
    跑多少分钟。默认 20 —— 原始斜率 1 MB/h 在 20 分钟里只有 0.33 MB,
    对照组会显得"平",这正是要的:实验组要是按请求漏,应该涨到 5 MB 上下。

.PARAMETER PlcCount
    实验组几台 PLC。默认 8 —— 8 倍请求率,是 20 分钟里能测出来的量级。
    调大到 16 的话斜率更明显,但从站进程也翻倍(16 个 python)。

.EXAMPLE
    .\scripts\mem_probe.ps1 -Minutes 20
#>
[CmdletBinding()]
param(
    [int] $Minutes      = 20,
    [int] $IntervalSec  = 20,
    [int] $PlcCount     = 8
)

$ErrorActionPreference = 'Stop'
$root    = Split-Path -Parent $PSScriptRoot
$exe     = Join-Path $root 'hmi\out\build\debug2\hmi.exe'
$seed    = Join-Path $root 'soaks\20261021_164804\appdata'
$stamp   = Get-Date -Format 'yyyyMMdd_HHmmss'
$outDir  = Join-Path $root "memprobe\$stamp"

function Say([string]$m, [string]$c = 'Gray') { Write-Host $m -ForegroundColor $c }

# ---- 前置检查:缺一样都不跑 ----------------------------------------------
#  为什么要检查而不是"跑了再说":
#  --soak 靠 users.json 里真实存在的用户登录。数据目录要是空的,
#  hmi.exe 会弹模态登录框然后卡住 —— 而卡住的进程 CPU 很低、看着还"活着",
#  曲线会很漂亮,结论全是假的。宁可现在就停下来。
if (-not (Test-Path $exe))  { Say "找不到 $exe" 'Red'; exit 2 }
if (-not (Test-Path $seed)) { Say "找不到种子数据目录 $seed" 'Red'; exit 2 }
if ($PlcCount -lt 2) { Say "PlcCount 至少要 2 —— 倍数就是它。" 'Red'; exit 2 }

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# ---- 端口分配:两组各占一段,互不重叠 --------------------------------------
#  ★ 这里踩过两次 ★
#  ① 实验组工程是从长跑那份 soak_project.json 复制改的,里面写着 "port": 1502。
#     忘了改的后果:两个 hmi 都连 1502,从站那边看着很热闹,
#     而"实验组请求率是 N 倍"这个前提根本没成立 —— 两组其实一模一样。
#     而且【闸门抓不到】:轮询统计行照样每分钟出现,一切指标都正常。
#  ② 多 PLC 版:每组自己一段连续端口(对照 1510+, 实验 1520+),
#     两段之间留够,免得 PlcCount 调大时两组撞上。
#
#  端口是这个实验唯一的前提,不能靠"我记得改了" ——
#  所以下面每组的 plcs[] 都是用 $plan 里的端口【程序化】生成的。
$basePort = 1510
$plan = @(
    @{ tag = 'control';   plcCount = 1;       firstPort = $basePort },
    @{ tag = 'treatment'; plcCount = $PlcCount; firstPort = $basePort + 20 }
)

# ---- 工程的 plcs[] / variables[] 生成 ------------------------------------
#  对照组和实验组的【变量完全一样】(3 个连续地址,同 plc 合批成 1 帧),
#  唯一的差别是 PLC 台数。这样斜率差只可能来自"请求数",不会掺进别的变量。
function New-Project([hashtable]$p, [string]$path) {
    $plcs = @()
    for ($i = 0; $i -lt $p.plcCount; $i++) {
        $pid_ = "plc{0}" -f ($i + 1)
        $plcs += [pscustomobject]@{
            id       = $pid_
            host     = '127.0.0.1'
            port     = ($p.firstPort + $i)
            protocol = 'modbus_tcp'
            slave_id = 1
        }
    }

    # 变量:每台 PLC 3 个【连续】地址(0,1,2)→ 合批成 1 帧 FC3。
    #  【为什么不打散地址】打散了就撞单 in-flight 限流,请求率被钉在 10/秒,
    #  那正是第一版设计作废的原因(见头部说明)。
    $vars = @()
    for ($i = 0; $i -lt $p.plcCount; $i++) {
        $pid_ = "plc{0}" -f ($i + 1)
        for ($a = 0; $a -lt 3; $a++) {
            $vars += [pscustomobject]@{
                name            = ("P{0}V{1}" -f ($i + 1), $a)
                address         = $a
                data_type       = 'UInt16'
                access          = 'ReadOnly'
                plc_id          = $pid_
                poll_interval_ms= 200
                alwaysPoll      = $true
                deadband        = 0
                range           = [pscustomobject]@{ min = 0; max = 65535 }
            }
        }
    }

    $proj = [pscustomobject]@{
        schema_version = '1.0'
        project_name   = ("HMI mem-probe {0} ({1} PLC)" -f $p.tag, $p.plcCount)
        created_at     = '2026-10-23T00:00:00'
        updated_at     = '2026-10-23T00:00:00'
        variables      = $vars
        alarms         = @()
        actions        = @()
        plcs           = $plcs
    }
    $proj | ConvertTo-Json -Depth 8 | Set-Content -Path $path -Encoding UTF8
    return $path
}

foreach ($p in $plan) {
    $p.proj = New-Project $p (Join-Path $outDir "$($p.tag).json")
}

Say "工程文件:" 'Cyan'
foreach ($p in $plan) {
    Say ("  {0,-10} = {1}  ({2} 台 PLC,端口 {3}+)" -f `
         $p.tag, $p.proj, $p.plcCount, $p.firstPort)
}
Say ("  预期请求率倍数 = {0}(PLC 台数)" -f $PlcCount) 'Cyan'

# ---- 数据目录:复制种子,让 user 3 存在 ------------------------------------
#  ★★ 这里踩过一次,而且踩得很贵(白跑 4 分钟才发现)★★
#  HMI_DATA_DIR 要的是【完整的 ...\QianWei\HMI】,不是它的上级目录。
#  少写一层的后果不是"报错",而是:
#    · Logging 落到 <上级>\logs\,日志 0 字节(因为还没轮到写)
#    · UserMgr 在 <上级>\users.json 找用户,找不到
#    · 于是弹出【首次启动 · 创建管理员账号】模态框
#    · 主线程阻塞在这个框上 —— 进程活着、CPU 很低、句柄线程全平,
#      采样曲线【漂亮得很】,结论全是假的
#  long_run.ps1 里那条 "这里照抄同一份规则,一个字都不许差" 就是这个意思。
$dirs = @{}
foreach ($tag in @('control','treatment')) {
    $d = Join-Path $outDir "appdata_$tag"
    Copy-Item $seed $d -Recurse
    #  历史库是上一轮留下来的,会让"这轮涨了多少"对不上;先清掉,
    #  但 users.json 必须留 —— --soak 要靠它登录。
    $full = Join-Path $d 'QianWei\HMI'
    $db   = Join-Path $full 'history.db'
    if (Test-Path $db) { Remove-Item $db -Force }

    #  起飞前的闸:users.json 不在【我们要传进去的那个精确路径】上就别起飞。
    #  宁可现在退出 —— 卡在登录框里跑满 20 分钟,采样再漂亮也是废的。
    $users = Join-Path $full 'users.json'
    if (-not (Test-Path $users)) {
        Say "起飞前自检失败:$tag 的 users.json 不在 $users" 'Red'
        Say "  ——HMI_DATA_DIR 必须是完整的 ...\QianWei\HMI,不是它的上级目录。" 'Red'
        exit 3
    }
    $dirs[$tag] = $full
}

# ---- 启动从站 --------------------------------------------------------------
#  【每台 PLC 一个从站进程】—— 端口一一对应。
#  两组都起齐(对照组那台也要有从站),否则它连不上,闸三会误判。
$procs = @()
foreach ($p in $plan) {
    for ($i = 0; $i -lt $p.plcCount; $i++) {
        $port = $p.firstPort + $i
        $slaveLog = Join-Path $outDir ("slave_{0}_p{1}.log" -f $p.tag, $port)
        $sp = Start-Process -FilePath 'python' `
            -ArgumentList @('scripts/test_slave.py', '--port', "$port", '--log', $slaveLog) `
            -WorkingDirectory $root -PassThru -WindowStyle Hidden
        $procs += [pscustomobject]@{ tag = $p.tag; kind = 'slave'; proc = $sp; port = $port }
    }
    Say ("从站 {0,-10} {1} 个,端口 {2}+" -f $p.tag, $p.plcCount, $p.firstPort) 'DarkGray'
}
Start-Sleep -Seconds 2   # 给从站一点时间把 socket 监听起来

# ---- 启动 hmi --------------------------------------------------------------
#  [int] 显式强转:PowerShell 会把 hashtable 里的东西统统重建成字符串,
#  不转的话 Start-Process -ArgumentList 收到的是字符串数组而非对象数组,
#  参数拼接会出问题。
foreach ($p in $plan) {
    $env:HMI_DATA_DIR     = $dirs[$p.tag]
    $env:HMI_INSTANCE_KEY = "memprobe-$($p.tag)-$stamp"
    $env:HMI_CRASH_DIR    = Join-Path $outDir "crash_$($p.tag)"
    $hp = Start-Process -FilePath $exe `
        -ArgumentList @('--soak', '--soak-user', '3', '--soak-pass', '333333',
                        '--project', $p.proj) `
        -WorkingDirectory $root -PassThru
    $procs += [pscustomobject]@{ tag = $p.tag; kind = 'hmi'; proc = $hp; port = 0 }
    Say ("hmi  {0,-10} 起 (pid {1})  键={2}" -f $p.tag, $hp.Id, $env:HMI_INSTANCE_KEY) 'Cyan'
}
Remove-Item Env:\HMI_DATA_DIR, Env:\HMI_INSTANCE_KEY, Env:\HMI_CRASH_DIR -ErrorAction SilentlyContinue

# ---- 起飞后的闸:三道,缺一不可 -------------------------------------------
#  ★ 这三道闸是被两次失败各自逼出来的,每一条都对应一种【看起来完全正常】的废数据 ★
#
#  闸一「卡在模态框」—— 卡在【首次启动 · 创建管理员账号】上的进程是【看不出来】的:
#        进程在、句柄平、线程平、采样曲线漂亮。不查窗口标题就发现不了。
#        判据用「轮询统计」行而不是「进程还在」:进程活着只说明没崩,
#        有统计行才说明【登录过了、项目打开了、轮询真的在跑】。
#
#  闸二「等不够久」—— 统计行是 60 秒一行(statsTimer_),第一版闸门设 45 秒,
#        结果把两个好端端的实例判成「没起来」。判据没错,是【时间】错了。
#        这里改成轮询等待,最多 150 秒。
#
#  闸三「连错从站」—— 实验组工程是从长跑那份复制的,里面写着 "port": 1502。
#        忘改的话两个 hmi 都打 1502,而"实验组请求率 N 倍"这个前提根本没成立:
#        两组其实一模一样。可【闸一闸二全都过得去】—— 统计行照常每分钟出现。
#        所以必须直接查【它连的是哪个端口】,这是本实验唯一不可协商的前提。
#        ★ 多 PLC 版:【每组的每一台都要查】,不能只查第一台。
Say "起飞后自检(等轮询统计行 + 核端口)..." 'DarkGray'
$bad = @()
$deadlineCheck = (Get-Date).AddSeconds(150)
$statsOK = @{}

foreach ($p in $plan) {
    $statsOK[$p.tag] = $false
    while ((Get-Date) -lt $deadlineCheck -and -not $statsOK[$p.tag]) {
        Start-Sleep -Seconds 10
        $lg = Get-ChildItem (Join-Path $dirs[$p.tag] 'logs\*.log') -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime | Select-Object -Last 1
        if ($lg) {
            $statsOK[$p.tag] = (Select-String -Path $lg.FullName -Pattern '轮询统计' -SimpleMatch -Quiet -Encoding UTF8)
        }
    }
}

foreach ($p in $plan) {
    $lg = Get-ChildItem (Join-Path $dirs[$p.tag] 'logs\*.log') -ErrorAction SilentlyContinue |
          Sort-Object LastWriteTime | Select-Object -Last 1
    $lines = @()
    if ($lg) { $lines = Get-Content $lg.FullName -Encoding Default }

    #  闸一
    if (-not $statsOK[$p.tag]) {
        $bad += $p.tag
        Say ("  {0,-10} 闸一挂:150 秒内没有轮询统计行 —— 【它没真跑起来】" -f $p.tag) 'Red'
        continue
    }

    #  闸三:它必须连到【自己那几台】从站端口,一台都不能少
    #  ★ 这里原来写的是 `-Pattern ([regex]::Escape($want)) -SimpleMatch` ——
    #    两个开关互相抵消:Escape 把 "127.0.0.1:1510" 变成 "127\.0\.0\.1:1510",
    #    而 -SimpleMatch 找的是【字面串】,于是它去找带反斜杠的那串,
    #    日志里当然没有 → 闸三永远挂。
    #    (第一版只有 1 个端口时也是这么写的,只是从来没人跑过。)
    #    正确写法:要么用 -SimpleMatch 不转义,要么用 -Pattern 不加 -SimpleMatch。
    #    这里选后者:端口号里的 "." 需要转义才算"真的连到这台"。
    $miss = @()
    for ($i = 0; $i -lt $p.plcCount; $i++) {
        $want = "127.0.0.1:$($p.firstPort + $i)"
        if (-not ($lines | Select-String -Pattern ([regex]::Escape($want)) -Quiet)) {
            $miss += $want
        }
    }
    if ($miss.Count -eq 0) {
        Say ("  {0,-10} 闸一过 · 闸三过({1} 台全部连上)" -f $p.tag, $p.plcCount) 'Green'
    } else {
        $bad += $p.tag
        $seen = ($lines | Select-String -Pattern '127\.0\.0\.1:\d+' -AllMatches |
                 ForEach-Object { $_.Matches } | ForEach-Object { $_.Value } | Select-Object -Unique) -join ', '
        Say ("  {0,-10} 闸三挂:这些端口没出现 [{1}];日志里看到的是 [{2}]" -f `
             $p.tag, ($miss -join ', '), $seen) 'Red'
    }

    #  附带看一眼统计行本身,把两组请求率直接打出来(不用等 20 分钟才知道拉开没有)
    $statLine = $lines | Select-String -Pattern '轮询统计' -SimpleMatch | Select-Object -Last 1
    if ($statLine) { Say ("             $($statLine.Line.Trim())" -replace '\s+', ' ') 'DarkGray' }
}

if ($bad.Count -gt 0) {
    foreach ($p in $plan) {
        $proc = Get-Process -Id ($procs | Where-Object { $_.tag -eq $p.tag -and $_.kind -eq 'hmi' }).proc.Id -ErrorAction SilentlyContinue
        if ($proc) { Say ("    {0} 窗口标题 = '{1}'" -f $p.tag, $proc.MainWindowTitle) 'DarkGray' }
    }
    Say "自检不过,这次实验作废 —— 收工,别拿废数据去下结论。" 'Red'
    foreach ($x in $procs) {
        if ($x.proc -and -not $x.proc.HasExited) { try { Stop-Process -Id $x.proc.Id -Force } catch {} }
    }
    exit 4
}
Say "三道闸全过 —— 这轮数据能用。" 'Green'

# ---- 采样 ----------------------------------------------------------------
#  只采自己起的这两个 hmi。Get-Process 按 Id 取,拿不到别的进程的内存。
$csvs = @{}
foreach ($tag in @('control','treatment')) {
    $csvs[$tag] = Join-Path $outDir "samples_$tag.csv"
    "Time,WorkingSetMB,PrivateMB,Handles,Threads,RunMinutes" | Set-Content $csvs[$tag] -Encoding UTF8
}

$deadline = (Get-Date).AddMinutes($Minutes)
$start    = Get-Date
Say ("开始采样,共 {0} 分钟,每 {1} 秒一次" -f $Minutes, $IntervalSec) 'Green'

while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds $IntervalSec
    $mins = [math]::Round(((Get-Date) - $start).TotalMinutes, 1)
    foreach ($p in $plan) {
        $proc = Get-Process -Id ($procs | Where-Object { $_.tag -eq $p.tag -and $_.kind -eq 'hmi' }).proc.Id -ErrorAction SilentlyContinue
        if ($null -eq $proc) { Say ("{0} 这个 pid 已经没了" -f $p.tag) 'Red'; continue }
        "{0},{1},{2},{3},{4},{5}" -f `
            (Get-Date -Format 'HH:mm:ss'),
            [math]::Round($proc.WorkingSet64 / 1MB, 2),
            [math]::Round($proc.PrivateMemorySize64 / 1MB, 2),
            $proc.HandleCount,
            $proc.Threads.Count,
            $mins | Add-Content $csvs[$p.tag]
    }
}

# ---- 收尾:只停自己起的进程 ----------------------------------------------
Say "实验结束,收尾(只停本次启动的 pid)" 'Yellow'
foreach ($x in $procs) {
    if ($x.proc -and -not $x.proc.HasExited) {
        try { Stop-Process -Id $x.proc.Id -Force -ErrorAction Stop }
        catch { Say "停不掉 pid $($x.proc.Id): $_" 'Red' }
    }
}

Say ("产物在 $outDir") 'Cyan'
Say "对照组:$(Join-Path $outDir 'samples_control.csv')"
Say "实验组:$(Join-Path $outDir 'samples_treatment.csv')"
Say ""
Say "★ 下一步:看两组 samples 的 PrivateMB 斜率,以及 appdata_*\QianWei\HMI\logs 里"
Say "  【轮询统计】那行的'成功'计数 —— 用来把斜率换算成'每请求多少字节'。"
Say "  没有这个计数就换算不了,只能得到'涨/不涨',得不到'每请求多少'。"
Say ""
Say "★ 换算基准:实验组的请求率应当是对照组的 $PlcCount 倍。"
Say "  先用【轮询统计】里两组各自的'成功'计数除以运行时长,核对一下比值。"
Say "  比值对不上 $PlcCount 就别下结论 —— 前提没成立(见脚本头部的'三道闸')。"
