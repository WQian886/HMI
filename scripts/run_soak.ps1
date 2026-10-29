# ============================================================================
# run_soak.ps1 —— 一次完整的长跑:自己起从站 → 起 HMI → 采样 → 收尾
#
#  为什么要有这个壳
#  ------------------
#  long_run.ps1 只管"看着一个已经在跑的 hmi.exe 采样"。
#  而 2026-10-21 查出来的现场问题有三个,每一个都会让长跑白跑:
#
#    (1) 从站不可控 —— mbslave 是 GUI 程序,数据表现在是空的,
#        对每个 FC3 都回 ExCode=0x03。读必失败,这一轮只能验到"重试正常"。
#    (2) 二进制可能选错 —— 修好的版本编在 out/build/debug2,
#        旧版本在 out/build/debug。长跑跑一晚上,验的却是没修的代码,最难发现。
#    (3) 工程没打开 —— --project 之前是空壳,跑的是默认工程(端口 502),
#        不是你想验的那份。
#
#  这个脚本把三件事绑成一条命令:起自建从站(端口 1502)、
#  用指定的 exe + 工程启动 HMI、跑完收摊(从站一定关掉)。
#
#  ★ 2026-10-21:它【也不会】动已经在跑的 hmi.exe
#    上一轮长跑的进程(PID 42428)从 2026-10-20 17:05:40 一直挂到现在,
#    而它从启动后第一秒起就没再跟 PLC 说过一句话(第一批读失败 → 变量卡在在途态)。
#    用户的要求是"不想影响它"。于是这里用两件事做到"各跑各的":
#
#      · 独立单实例键(HMI_INSTANCE_KEY)—— 两把锁,不抢。
#        同一把键的第二个实例照样被挡(tst_SingleInstanceKey 钉着这条),
#        所以这不是"取消单实例",只是"这次长跑用另一把锁"。
#      · 私有数据目录(HMI_DATA_DIR 指到本轮自己的目录)—— 两边不共用
#        history.db / 日志目录 / 崩溃标记 / 审计日志。
#        不隔离的话,两个实例会同时写同一个 SQLite 文件,长跑的证据就废了。
#        ★ 2026-10-21:一开始试的是"改 LOCALAPPDATA 环境变量",那是【没用的】——
#          Qt 在 Windows 上用 SHGetKnownFolderPath 解析 LocalAppData,不认环境变量。
#          实测那个实例照样写进了真实的 %LOCALAPPDATA%\QianWei\HMI\,
#          还把同名的 audit-2026-10-21.log 截断了(在跑的那一轮的审计记录当场没了)。
#          现在走应用自己认的开关 HMI_DATA_DIR(hmi/src/util/Paths.h,tst_Paths 钉着)。
#
#  用法:
#    .\scripts\run_soak.ps1 -Hours 24 -SoakUser 3 -SoakPass 333333
#
#  前提(缺一个就别开跑):
#    · 修好的二进制已经编好(out/build/debug2/hmi.exe)
#    · python 在 PATH 上
# ============================================================================

[CmdletBinding()]
param(
    [double] $Hours       = 24,
    [int]    $IntervalSec = 60,
    [string] $SoakUser   = '',
    [string] $SoakPass   = '',
    [string] $Exe        = '',
    [string] $Project    = '',
    [int]    $SlavePort  = 1502,
    [string] $OutDir     = '',

    # ★ 2026-10-27(SOAK-W):从站回包延迟(ms)。
    #   为什么长跑要加这个 —— 见下面起从站那处的长注释。
    #   ★ 2026-10-28 更正:原文写"120 是实测撞车率最高的值(190/400 反而撞不到,
    #     方向跟直觉相反)"—— 那组实测是在 poll=50 下做的,结论被错误推广到了 poll=200。
    #     实测(poll=200,D=120 与 D=250,约 360 次写):排队【全是 0】。
    #     ⇒ "撞车"的杠杆是【m 与 poll 的先后】,不是延迟大小(见 $AlarmMinDurationMs)。
    #     这里保留 120 是因为它无害,且在 poll 较小的工程里仍然有用。
    #   设为 0 可以回到"不制造撞车"的旧行为,但那样 SOAK-W 验的是
    #   一条【没被走过的路】。
    [int]    $SlaveReplyDelayMs = 120,

    # ★★ 2026-10-28(SOAK-W):把工程副本里报警的 min_duration_ms 抬到多少 ★★
    #
    #   【为什么要有这个参数 —— 一条被实测推翻的设计】
    #     原来的想法:"给从站加 120ms 回包延迟 ⇒ 槽会被占着 ⇒ 写撞得上 ⇒
    #                 排队那条通路会被走到"。
    #     ★ 这个想法【被实测推翻了】(2026-10-28,docs/现状.md §六之六 9.7)★
    #       poll=200ms 时,连跑 6 轮、约 360 次写,排队【全是 0】。
    #     ⇒ 机制(从真日志时间戳量出来的):
    #       写【排队】的唯一条件是"发写那一刻在途槽被读占着"。
    #       而【下一读什么时候发】由 poll 决定,【写什么时候发】由 m 决定。
    #       两者的【先后】才是杠杆 —— 延迟大小根本不是。
    #       实测(poll=200,m=95):写滞后上一读回包 115~217ms,
    #         下一读发出滞后 312~437ms ⇒ 写【永远】在下一读之前 ⇒ 槽必然空着。
    #       实测(poll=200,m=190):排队 7/56、11/58 ⇒ 撞上了。
    #       实测(poll=200,m=250):排队 31/57 ⇒ 撞得更狠。
    #     ⇒ ★ 而仓库里的 soak_project.json 用的是 **m=50**,
    #       远小于 poll=200 ⇒ **结构上撞不上** ⇒ 24 小时里那条通路一次都不会被走到
    #       ⇒ 那道闸验的仍然是"没撞车时写是好的",不是"撞车时写也能落地"。
    #
    #   【这个参数做什么】
    #     把工程副本里两个报警的 min_duration_ms 改成这个值
    #     (工程【原件】一个字都不改 —— 仓库里那份永远是 m=50)。
    #     0 = 不改(保持原件)。
    #   【为什么默认值是 190,而不是 250】
    #     m=250 撞车率 54% ⇒ 24 小时里【几乎每次联动都撞】,
    #     那样"排队"变成常态,反而测不到"多数时候不撞"的那种状态。
    #     m=190 实测 12~19% ⇒ 24 小时约 458 次联动 ⇒ 期望撞上 55~87 次 ——
    #     【足够多,又不是常态】★ 这就是想要的那个量级。
    [int]    $AlarmMinDurationMs = 190,

    # ★ 2026-10-21:本轮的单实例键。
    #   不给就按时间戳生成一个(hmi-soak-<时间戳>),保证每轮都不一样 ——
    #   跟上一轮撞上就等于自己把自己挡了(而症状是"第二个实例直接退出",
    #   很难一眼看出是锁的问题)。
    [string] $InstanceKey = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$here = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $here '..')).Path

function Say($msg, $color = 'Gray') {
    Write-Host ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $msg) -ForegroundColor $color
}

# ---- 默认值 -------------------------------------------------------------
if (-not $Exe)     { $Exe     = Join-Path $repo 'hmi\out\build\debug2\hmi.exe' }
if (-not $Project) { $Project = Join-Path $here  'soak_project.json' }
if (-not $OutDir)  { $OutDir  = Join-Path $repo ("soaks\{0}" -f (Get-Date -Format 'yyyyMMdd_HHmmss')) }
if (-not $InstanceKey) {
    $InstanceKey = "hmi-soak-{0}" -f (Get-Date -Format 'yyyyMMdd-HHmmss')
}

$slaveScript = Join-Path $here 'test_slave.py'
$longRun     = Join-Path $here 'long_run.ps1'

# ---- 前置检查:一条一条说清楚,不猜 -------------------------------------
$problems = @()
if (-not (Test-Path $Exe))     { $problems += "找不到 exe: $Exe" }
if (-not (Test-Path $Project)) { $problems += "找不到工程文件: $Project" }
if (-not (Test-Path $slaveScript)) { $problems += "找不到从站脚本: $slaveScript" }
if (-not (Get-Command python -ErrorAction SilentlyContinue)) { $problems += "PATH 里没有 python" }
if ($problems.Count -gt 0) {
    Say "开跑前置检查没过:" 'Red'
    $problems | ForEach-Object { Say "  · $_" 'Red' }
    exit 2
}

# ---- 已经在跑的 hmi.exe:【不】当成拦路虎 --------------------------------
#  ★ 2026-10-21:以前这里直接 exit 2("有 hmi.exe 在跑就别开跑")。
#    现在不是了 —— 有了独立实例键和私有数据目录,两边可以各跑各的。
#    所以这里只【报告】,不动它:不结束它、不抢它的锁、不读它的数据目录。
$existing = @(Get-Process -Name hmi -ErrorAction SilentlyContinue)
if ($existing.Count -gt 0) {
    Say "检测到已经在跑的 hmi.exe —— 本轮用独立实例键和私有数据目录,各跑各的,不动它:" 'Yellow'
    foreach ($p in $existing) {
        Say ("  · PID={0} 启动于 {1} 已跑 {2:N1} 分钟 内存 {3} MB" -f `
             $p.Id, $p.StartTime.ToString('yyyy-MM-dd HH:mm:ss'),
             ((Get-Date) - $p.StartTime).TotalMinutes, [math]::Round($p.WorkingSet64/1MB,1)) 'DarkGray'
    }
    Say "  它继续跑它自己的,本轮不结束它、不接管它、不把它算进任何判定。" 'Green'
}

# ---- 输出目录 + 私有数据目录 -------------------------------------------
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$slaveLog = Join-Path $OutDir 'slave.log'

# 私有数据目录:本轮的应用数据一律落在这里。
#   应用的日志、history.db、崩溃标记、审计日志全从【数据目录】推出来
#   (hmi/src/util/Paths.h 的 util::appDataDir())。
#
#   ★ 2026-10-21:这里【不是】改 LOCALAPPDATA 环境变量。
#     那是【没用的】—— Qt 在 Windows 上用 SHGetKnownFolderPath 解析 LocalAppData,
#     那个 API 认的是"当前登录用户的真实路径",不认环境变量。
#     实测:把 LOCALAPPDATA 指到别处再起一个实例,它照样把日志写进真实的
#     %LOCALAPPDATA%\QianWei\HMI\,还把同名的 audit-2026-10-21.log 截断了
#     —— 正在跑的那一轮的审计记录当场没了。
#     ⇒ 隔离必须走应用自己认的开关:HMI_DATA_DIR。
#     这个开关是 2026-10-21 新加的(hmi/src/util/Paths.h),由 tst_Paths 钉着。
$dataDir = Join-Path $OutDir 'appdata'
$appData = Join-Path $dataDir 'QianWei\HMI'
New-Item -ItemType Directory -Force -Path $appData | Out-Null
$dataDir = (Resolve-Path $dataDir).Path

# users.json 要复制过去:--soak 需要一个【已存在】的用户,
# 用户库在 %LOCALAPPDATA%\QianWei\HMI\users.json。
# 复制一份(不修改原文件),长跑用的还是同一个账号、同一个密码。
$realUsers = Join-Path (Join-Path $env:LOCALAPPDATA 'QianWei\HMI') 'users.json'
if ($SoakUser -and -not (Test-Path $realUsers)) {
    Say ("找不到用户库: {0}" -f $realUsers) 'Red'
    Say "  --soak 需要一个已存在的用户(它不是免认证开关),请先建好用户再跑。" 'Red'
    exit 2
}
if (Test-Path $realUsers) {
    Copy-Item $realUsers (Join-Path $appData 'users.json') -Force
}

# 工程文件复制一份到输出目录:应用【不会】自动保存工程(只有 UI 的保存按钮会),
# 但复制一份能让"这一轮用的是哪个工程"在产物目录里一眼看得见,
# 也保证仓库里那份永远不被运行中的程序碰到。
$projectCopy = Join-Path $OutDir 'soak_project.json'
Copy-Item $Project $projectCopy -Force

# ★★ 2026-10-28:把副本里报警的 min_duration_ms 抬到 $AlarmMinDurationMs ★★
#   ★ 只改【副本】—— 仓库里 scripts\soak_project.json 一个字都不动。
#     (仓库那份永远保持 m=50:那是"现场默认工程"的真实样子,改了就是篡改现场。)
#   ★ 为什么必须抬(理由很长,见上面 $AlarmMinDurationMs 的参数注释):
#     m=50 远小于 poll=200 ⇒ 写【结构上】撞不上在途的读
#     ⇒ 24 小时里写排队那条通路一次都不会被走到
#     ⇒ SOAK-W 验的仍是"没撞车时写是好的",不是"撞车时写也能落地"。
#   ★ 改完必须【把改了什么记下来】—— 不然读报告的人
#     以为"这轮跑的是原工程",而其实节奏被改过。
if ($AlarmMinDurationMs -gt 0) {
    $proj = Get-Content -LiteralPath $projectCopy -Raw -Encoding UTF8 | ConvertFrom-Json
    $before = @($proj.alarms | ForEach-Object { [int]$_.min_duration_ms })
    foreach ($a in @($proj.alarms)) { $a.min_duration_ms = $AlarmMinDurationMs }
    # 写回去要 UTF-8 无 BOM —— Qt 读 JSON 时对 BOM 敏感(实测踩过)
    $json = ($proj | ConvertTo-Json -Depth 20)
    [System.IO.File]::WriteAllText($projectCopy, $json, (New-Object System.Text.UTF8Encoding($false)))
    Say ("报警延时     : min_duration_ms {0} → {1} ms(只改副本;原件不动)" -f `
         ($before -join '/'), $AlarmMinDurationMs) 'Cyan'
    Say ("  ★ 为什么:写排队那条通路要【撞得上】才验得到。m 必须逼近 poll(200ms)," +
         "否则写永远落在下一读之前 ⇒ 槽空着 ⇒ 一次都撞不上。") 'DarkGray'
}

# ★ 显式设置,不留隐式状态:报告里说"数据目录在哪",就必须真的在那。
$origDataDir = $env:HMI_DATA_DIR
$env:HMI_DATA_DIR = $appData

Say "========== 一次完整长跑 ==========" 'Cyan'
Say ("输出目录   : {0}" -f $OutDir)
Say ("HMI 可执行 : {0}" -f $Exe)
Say ("工程文件   : {0}(原件 {1})" -f $projectCopy, $Project)
Say ("从站       : python {0} --port {1}" -f $slaveScript, $SlavePort)
Say ("实例键     : {0}(与其它实例互不干扰)" -f $InstanceKey)
Say ("私有数据目录: {0}" -f $appData)
Say ("  (经 HMI_DATA_DIR 传给被测进程 —— Qt 不认 LOCALAPPDATA,只能走这个开关)") 'DarkGray'
Say ("计划时长   : {0} 小时" -f $Hours)

# ---- 起从站 ------------------------------------------------------------
Say "启动自建从站 ..." 'Cyan'
# ★ 2026-10-27(SOAK-W):从站加 120 ms 回包延迟。
#
#   【★ 2026-10-28 更正:这段说明原来把延迟说成"撞车的杠杆",那是错的 ★】
#     原文说"120ms 撞车率最高(3/38)、190/400ms 反而撞不到(方向反直觉)"。
#     ★ 那组实测是在 poll=50 下做的,结论被【错误地推广】到了 poll=200。
#     实测(poll=200,D=120 与 D=250,各 3 轮,约 360 次写):排队【全是 0】。
#     ⇒ 真正的杠杆是【写(m)与下一读(poll)的先后】,不是延迟大小。
#       详见 docs/现状.md §六之六 9.7。
#     ⇒ 所以现在把"撞车"这件事交给 $AlarmMinDurationMs(见上),
#       这里的延迟只保留一个作用:让从站慢一点,给"槽被占着"留出可能性,
#       以及顺带把 poll=50 那种密集轮询场景的读速压下来。
#       ★ 保留 120 而不是 0:它无害,而且在 poll 变小的那类工程里仍然有用。
#
#   【为什么至少要加一点】不加的话,长跑验的是"没撞车时写是好的",
#     而不是"撞车时写也能落地" —— 而后者才是 SOAK-W 要抓的东西。
#     0 ms ⇒ 槽几乎永远空着 ⇒ 【写排队那条通路一次都不会被走到】
#     ⇒ 24 小时里那道闸验的是一条【没被走过的路】。
$slave = Start-Process -FilePath 'python' `
    -ArgumentList @($slaveScript, '--port', "$SlavePort", '--log', $slaveLog,
                    '--reply-delay-ms', "$SlaveReplyDelayMs") `
    -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $OutDir 'slave.stdout.txt') `
    -RedirectStandardError  (Join-Path $OutDir 'slave.stderr.txt')

$slaveReady = $false
for ($i = 0; $i -lt 50; $i++) {
    Start-Sleep -Milliseconds 200
    try {
        $c = New-Object System.Net.Sockets.TcpClient
        $c.Connect('127.0.0.1', $SlavePort)
        $c.Close()
        $slaveReady = $true
        break
    } catch { }
}
if (-not $slaveReady) {
    Say "从站没起来(端口 $SlavePort 30 秒没人应答) —— 停。" 'Red'
    if (-not $slave.HasExited) { Stop-Process -Id $slave.Id -Force -ErrorAction SilentlyContinue }
    exit 1
}
Say ("从站已就绪(PID={0}),端口 {1}" -f $slave.Id, $SlavePort) 'Green'

# ---- 跑长跑 ------------------------------------------------------------
$exit = 0
try {
    $lrunArgs = @('-Hours', "$Hours", '-IntervalSec', "$IntervalSec",
                  '-Exe', $Exe, '-Project', $projectCopy, '-OutDir', $OutDir,
                  '-InstanceKey', $InstanceKey)
    if ($SoakUser -and $SoakPass) {
        $lrunArgs += @('-SoakUser', $SoakUser, '-SoakPass', $SoakPass)
    }
    # 用【子进程】跑 long_run.ps1,而不是 `& $longRun @lrunArgs`。
    #   原因:long_run.ps1 里用 exit N 表达判定结果(exit 1 应用故障 / exit 4 环境问题)。
    #   直接 `&` 调用时,exit 的传播行为在不同宿主下不一致,
    #   而且 $LASTEXITCODE 对 .ps1 的调用【不保证】被设置 ——
    #   判定码丢了就等于没判定。起一个 powershell 子进程,
    #   $LASTEXITCODE 就是这个进程的真实退出码,finally 也照常跑。
    #   LOCALAPPDATA 是父进程设的环境变量,子进程继承 ⇒ 数据目录照样隔离。
    & powershell -NoProfile -ExecutionPolicy Bypass -File $longRun @lrunArgs
    $exit = $LASTEXITCODE
}
finally {
    # ---- 收尾:从站一定要关 -----------------------------------------
    if ($slave -and (-not $slave.HasExited)) {
        Say "关掉自建从站 ..." 'Cyan'
        Stop-Process -Id $slave.Id -Force -ErrorAction SilentlyContinue
    }
    $env:HMI_DATA_DIR = $origDataDir
    Say ("长跑退出码: {0}" -f $exit) 'Cyan'
    Say ("产物目录  : {0}" -f $OutDir)
    Say ("判定文件  : {0}" -f (Join-Path $OutDir 'verdict.txt'))
    exit $exit
}
