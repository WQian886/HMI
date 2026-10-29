<#
.SYNOPSIS
    报警轴对照实验:私有内存是【每报警一行】涨,还是【每 Modbus 请求】涨?

.DESCRIPTION
    ★ 为什么需要这个实验 ★
    24 小时长跑留下 PrivateMB 21.41 → 45.68 MB(斜率 ~1.01 MB/h)。
    长跑数据【分不开】两个假设,因为:
        · 请求率全程恒定 ~4.53 次/秒(390369 次)
        · 报警率全程恒定 ~8.5 次/分钟(12028 次)
    两个都是均匀的,拿它们当自变量拟合,R² 都是 0.998+。
    —— 这不是"证据",这是恒定变量的数学后果。

    已经做过的两个排除(各自的局限):
      · mem_probe 20 分钟(20261024_200525):8× 请求,0 报警 → 排除了"纯请求轴"
        局限:窗口只有 20 分钟,而且 0 报警,它测不到报警轴。
      · PID 42428(950 分钟):0 请求【也】0 报警 → 排除了"纯时间轴"
        局限:它同时 0 请求和 0 报警,所以两个模型都排除,却分不出哪个。

    ★ 本实验补的就是缺的那一格 ★
    设计:【1 台 PLC,N 个连续地址的变量,每个变量配一条报警】
        · 请求帧数:连续地址合批成 1 帧(PollScheduler.cpp:373-381)→ 恒为 1
        · 报警行数:每个变量一条独立报警 → 随 N 线性增长
    于是【请求数恒定、报警数可调】—— 这正是长跑数据缺的那个自由度。

    三组:
        group A(0 条报警) —— 基线
        group B(8 条报警) —— 1 PLC,8 个连续变量
        group C(32 条报警)—— 1 PLC,32 个连续变量
    ★ 全部 1 台 PLC ★ —— 请求帧数三组都是 1,【完全相同】。
      所以三组之间唯一的差别就是"报警行数"。

    判读:
      斜率随报警数线性上升,差值 / 报警数 = 每报警多少字节  → 报警轴成立
      三组斜率都≈0                                      → 两个模型都排除
      斜率随报警数上升但和请求数也对得上               → 还有别的东西,继续查

    ★ 报警怎么才会一直触发 ★
    从站的寄存器值是慢三角波,阈值取在【波的中段】,值来回穿越阈值
    → 触发 → 恢复 → 再触发。min_duration_ms 取 50ms(和长跑工程一样),
    滞回 hysteresis=0,所以每次穿越都报一次。
    报警频率由【值穿越频率】决定,跟请求率无关 —— 这正是要的。

.PARAMETER Minutes
    跑多少分钟。默认 60。
    预期:每报警 ~2000 字节,报警率约 X 次/分,斜率 = X × 2000 字节/分。
    60 分钟足够把 2000 字节/报警 的信号从噪声里拉出来。

.PARAMETER IntervalSec
    采样间隔秒。默认 20。

.EXAMPLE
    .\scripts\mem_probe_alarm.ps1 -Minutes 60
#>
[CmdletBinding()]
param(
    [int] $Minutes     = 60,
    [int] $IntervalSec = 20
)

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$exe   = Join-Path $root 'hmi\out\build\debug2\hmi.exe'
$seed  = Join-Path $root 'soaks\20261021_164804\appdata'
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$outDir = Join-Path $root "memprobe_alarm\$stamp"

function Say([string]$m, [string]$c = 'Gray') { Write-Host $m -ForegroundColor $c }

if (-not (Test-Path $exe))  { Say "找不到 $exe" 'Red'; exit 2 }
if (-not (Test-Path $seed)) { Say "找不到种子数据目录 $seed" 'Red'; exit 2 }

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# ---- 三组:只有【报警条数】不同,PLC 台数恒为 1 --------------------------
#  ★ 关键:PLC 台数恒为 1 → 请求帧数三组完全相同 ★
#    如果像 mem_probe.ps1 那样用多 PLC 放大,请求数就跟着变了,
#    那个实验量的是"请求轴",测不到"报警轴"。
$plan = @(
    @{ tag = 'A0alarm';  nAlarms = 0  },
    @{ tag = 'B8alarm';  nAlarms = 8  },
    @{ tag = 'C32alarm'; nAlarms = 32 }
)

# ---- 端口:每组一段,互不重叠 -------------------------------------------
$basePort = 1610
$pi = 0
foreach ($p in $plan) {
    $p.port    = $basePort + ($pi * 10)
    $p.proj    = Join-Path $outDir "$($p.tag).json"
    $p.appdata = Join-Path $outDir "appdata_$($p.tag)"
    $pi++
}

# ---- 工程生成:1 台 PLC + N 个连续变量 + N 条报警 ----------------------
function New-Project([hashtable]$p, [string]$path) {
    # 变量数 = max(3, nAlarms)。至少 3 个是为了让"有值可读"这件事跟长跑一致。
    $nVars = [Math]::Max(3, $p.nAlarms)

    $vars = @()
    for ($a = 0; $a -lt $nVars; $a++) {
        $vars += [pscustomobject]@{
            name             = ("V{0}" -f $a)
            address          = $a
            data_type        = 'UInt16'
            access           = 'ReadOnly'
            plc_id           = 'plc1'
            poll_interval_ms = 200
            alwaysPoll       = $true
            deadband         = 0
            range            = [pscustomobject]@{ min = 0; max = 65535 }
        }
    }

    # 报警:每个变量一条,方向 Above,阈值 50。
    #  ★ 阈值 50 的用意 ★
    #   从站的值是 int(abs(sin(t/60 + addr)) * 100) —— 0..100 的三角波。
    #   阈值 50 正好在【中段】,值每分钟来回穿越 1 次 → 每分钟触发+恢复一轮。
    #   阈值 90 只会偶尔碰一下,阈值 10 会被卡在边界上。
    $alarms = @()
    for ($a = 0; $a -lt $p.nAlarms; $a++) {
        $alarms += [pscustomobject]@{
            name            = ("A{0}" -f $a)
            variable        = ("V{0}" -f $a)
            direction       = 'Above'
            threshold       = 50
            level           = 'Critical'
            min_duration_ms = 50
        }
    }

    # actions 留空:动作会写 PLC,而写请求会引入新的变量(FC6/FC16)。
    # 长跑工程有两条 action,但实测 24 小时里【一条写请求都没发出去】
    # (slave.log 里 FC6/FC16 计数 = 0),所以它对内存没贡献 —— 这里保持一致。
    $proj = [pscustomobject]@{
        schema_version = '1.0'
        project_name   = ("HMI alarm-probe {0} ({1} alarms)" -f $p.tag, $p.nAlarms)
        created_at     = '2026-10-24T00:00:00'
        updated_at     = '2026-10-24T00:00:00'
        variables      = $vars
        alarms         = $alarms
        actions        = @()
        plcs           = @([pscustomobject]@{
            id = 'plc1'; host = '127.0.0.1'; port = $p.port
            protocol = 'modbus_tcp'; slave_id = 1
        })
    }
    $proj | ConvertTo-Json -Depth 8 | Set-Content -Path $path -Encoding UTF8
    return $path
}

foreach ($p in $plan) { New-Project $p $p.proj | Out-Null }

Say "工程文件:" 'Cyan'
foreach ($p in $plan) {
    Say ("  {0,-10} 报警 {1,2} 条  变量 {2,2} 个  端口 {3}" -f `
         $p.tag, $p.nAlarms, [Math]::Max(3, $p.nAlarms), $p.port)
}
Say "  ★ 三组都是 1 台 PLC → 请求帧数完全相同,唯一差别是报警条数 ★" 'Cyan'

# ---- 数据目录:复制种子(要 users.json 供 --soak 登录) ------------------
foreach ($p in $plan) {
    Copy-Item $seed $p.appdata -Recurse
    $full = Join-Path $p.appdata 'QianWei\HMI'
    $db = Join-Path $full 'history.db'
    if (Test-Path $db) { Remove-Item $db -Force }
    $users = Join-Path $full 'users.json'
    if (-not (Test-Path $users)) {
        Say "起飞前自检失败:$($p.tag) 的 users.json 不在 $users" 'Red'
        exit 3
    }
    $p.dataDir = $full
}

# ---- 启动从站 ----------------------------------------------------------
$procs = @()
foreach ($p in $plan) {
    $slaveLog = Join-Path $outDir ("slave_{0}_p{1}.log" -f $p.tag, $p.port)
    $sp = Start-Process -FilePath 'python' `
        -ArgumentList @('scripts/test_slave.py', '--port', "$($p.port)", '--log', $slaveLog) `
        -WorkingDirectory $root -PassThru -WindowStyle Hidden
    $procs += [pscustomobject]@{ tag = $p.tag; kind = 'slave'; proc = $sp }
    Say ("从站 {0,-10} 端口 {1}" -f $p.tag, $p.port) 'DarkGray'
}
Start-Sleep -Seconds 2

# ---- 启动 hmi ----------------------------------------------------------
foreach ($p in $plan) {
    $env:HMI_DATA_DIR     = $p.dataDir
    $env:HMI_INSTANCE_KEY = "memprobe-alarm-$($p.tag)-$stamp"
    $env:HMI_CRASH_DIR    = Join-Path $outDir "crash_$($p.tag)"
    $hp = Start-Process -FilePath $exe `
        -ArgumentList @('--soak', '--soak-user', '3', '--soak-pass', '333333',
                        '--project', $p.proj) `
        -WorkingDirectory $root -PassThru
    $procs += [pscustomobject]@{ tag = $p.tag; kind = 'hmi'; proc = $hp }
    Say ("hmi  {0,-10} 起 (pid {1})" -f $p.tag, $hp.Id) 'Cyan'
}
Remove-Item Env:\HMI_DATA_DIR, Env:\HMI_INSTANCE_KEY, Env:\HMI_CRASH_DIR -ErrorAction SilentlyContinue

# ---- 闸:三组都要出现轮询统计行,且连到自己的端口 -----------------------
Say "起飞后自检(等轮询统计行 + 核端口)..." 'DarkGray'
$bad = @()
$deadline = (Get-Date).AddSeconds(150)
$statsOK = @{}
foreach ($p in $plan) {
    $statsOK[$p.tag] = $false
    while ((Get-Date) -lt $deadline -and -not $statsOK[$p.tag]) {
        Start-Sleep -Seconds 10
        $lg = Get-ChildItem (Join-Path $p.dataDir 'logs\*.log') -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime | Select-Object -Last 1
        if ($lg) {
            $statsOK[$p.tag] = (Select-String -Path $lg.FullName -Pattern '轮询统计' -SimpleMatch -Quiet -Encoding UTF8)
        }
    }
}
foreach ($p in $plan) {
    $lg = Get-ChildItem (Join-Path $p.dataDir 'logs\*.log') -ErrorAction SilentlyContinue |
          Sort-Object LastWriteTime | Select-Object -Last 1
    $lines = @()
    if ($lg) { $lines = Get-Content $lg.FullName -Encoding Default }
    if (-not $statsOK[$p.tag]) {
        $bad += $p.tag
        Say ("  {0,-10} 闸一挂:没有轮询统计行" -f $p.tag) 'Red'
        continue
    }
    $want = "127.0.0.1:$($p.port)"
    if ($lines | Select-String -Pattern ([regex]::Escape($want)) -Quiet) {
        Say ("  {0,-10} 闸一过 · 闸三过(连到 {1})" -f $p.tag, $want) 'Green'
    } else {
        $bad += $p.tag
        Say ("  {0,-10} 闸三挂:没看到 {1}" -f $p.tag, $want) 'Red'
    }
}
if ($bad.Count -gt 0) {
    Say "自检不过,这次实验作废。" 'Red'
    foreach ($x in $procs) {
        if ($x.proc -and -not $x.proc.HasExited) { try { Stop-Process -Id $x.proc.Id -Force } catch {} }
    }
    exit 4
}
Say "三道闸全过 —— 这轮数据能用。" 'Green'

# ---- 采样 --------------------------------------------------------------
$csvs = @{}
foreach ($p in $plan) {
    $csvs[$p.tag] = Join-Path $outDir "samples_$($p.tag).csv"
    "Time,WorkingSetMB,PrivateMB,Handles,Threads,RunMinutes" | Set-Content $csvs[$p.tag] -Encoding UTF8
}
$deadline = (Get-Date).AddMinutes($Minutes)
$start = Get-Date
Say ("开始采样,共 {0} 分钟,每 {1} 秒一次" -f $Minutes, $IntervalSec) 'Green'

while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds $IntervalSec
    $mins = [math]::Round(((Get-Date) - $start).TotalMinutes, 1)
    foreach ($p in $plan) {
        $proc = Get-Process -Id ($procs | Where-Object { $_.tag -eq $p.tag -and $_.kind -eq 'hmi' }).proc.Id -ErrorAction SilentlyContinue
        if ($null -eq $proc) { Say "$($p.tag) 这个 pid 已经没了" 'Red'; continue }
        "{0},{1},{2},{3},{4},{5}" -f `
            (Get-Date -Format 'HH:mm:ss'),
            [math]::Round($proc.WorkingSet64 / 1MB, 2),
            [math]::Round($proc.PrivateMemorySize64 / 1MB, 2),
            $proc.HandleCount,
            $proc.Threads.Count,
            $mins | Add-Content $csvs[$p.tag]
    }
}

# ---- 收尾:只停自己起的进程 ---------------------------------------------
Say "实验结束,收尾(只停本次启动的 pid)" 'Yellow'
foreach ($x in $procs) {
    if ($x.proc -and -not $x.proc.HasExited) {
        try { Stop-Process -Id $x.proc.Id -Force -ErrorAction Stop }
        catch { Say "停不掉 pid $($x.proc.Id): $_" 'Red' }
    }
}

Say ("产物在 $outDir") 'Cyan'
Say ""
Say "★ 判读 ★" 'Cyan'
Say "  1) 三组的【请求数】应当几乎相同(各 1 台 PLC,连续地址合批成 1 帧)。"
Say "     先用 slave_*.log 的行数核对 —— 不对就别下结论。"
Say "  2) 三组的【报警数】从各自 appdata 的 history.db 数(action=0)。"
Say "  3) 斜率差 ÷ 报警数差 = 每报警多少字节。"
Say "     跟长跑拟合出来的 2034 字节/行 对得上,这条就闭合了。"
