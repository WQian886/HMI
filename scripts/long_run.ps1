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

    2026-10-21 变更(两处,都是被同一次失败逼出来的)
    -------------------------------------------------
    那次跑:950.9 分钟、950 个采样点,最后【什么都没留下】。
    被测程序没崩(它一直活到第二天上午还在心跳),崩的是这个 harness。
    两处改动都是从这一件事来的:

    (1) verdict.txt 不再只在最后一行写。
        原来唯一写它的地方是文件末尾,没有 finally、没有中间落盘。
        harness 一死,16 小时的证据就变成一份没有结论的 CSV —— 和没跑一样。
        现在:基线取完就写一次,每个采样 tick 刷新一次,采样循环包
        try/catch/finally。verdict.txt 从此【不再可能不存在】,
        并且里面带着崩溃基线时刻(SOAK-1 的时间原点),harness 死后仍可离线复判。
        中途落盘的文件自己写着"这是运行中状态,不是结论",不会被误当成 PASS。

    (2) idle 判据第三次改:心跳日志不再算"活体证据"。
        原来判 idle 的第二条腿是"日志总量在不在长"。
        而 "[HistoryRecorder] 心跳写了 3 行" 每 60 秒一条,那是本地 SQLite 写盘 ——
        PLC 断着它在写,界面冻在模态框上它也在写。
        那次跑 CPU 0.74%(低于阈值)、心跳一直在长 ⇒ isIdle=FALSE ⇒ 给了 PASS;
        同一份日志里 hmi.comm 一条都没有,PLC 地址是 127.0.0.1:502(回环)。
        那个 PASS 证明的是"程序会自己给自己记心跳",不是"程序跑得住"。
        现在按【类别】看:静默期之后新产生的日志行,去掉本地定时器噪声还剩多少。
        并新增 SOAK-6「业务活体」独立编号 —— 它判的不是资源曲线,是【这一轮的前提】。

    所以那 950.9 分钟【不能用来关闭 F2】:它证明的是"挂机 16 小时不崩不泄漏",
    不是"跑业务 16 小时不崩不泄漏"。判据修好后重跑才有意义。
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

    # ---- 2026-10-21:可执行文件 / 工程文件 覆盖 --------------------------
    #  为什么需要这两个参数
    #  ------------------------
    #  1) -Exe:修好的二进制编在 out/build/debug2(Qt-Debug-Alt preset),
    #     因为 out/build/debug/hmi.exe 当时被正在跑的旧进程占着(Windows 锁 exe)。
    #     而这段脚本原来只认 out/build/debug 和 x64/Debug 两个候选路径 ——
    #     不给覆盖入口,它会挑中【旧二进制】,跑一晚上验的是没修的版本。
    #     ★ 这种"脚本跑起来了、验的却是旧代码"最难发现:所有指标都正常。
    #
    #  2) -Project:--project 原来是个空壳(只打一行日志,什么都不做),
    #     2026-10-21 才真的能打开指定工程。脚本得有个入口把它传下去。
    [string] $Exe     = '',
    [string] $Project = '',

    # ---- 2026-10-21:单实例锁的键可指定 --------------------------------
    #  为什么需要这个参数
    #  -------------------
    #  单实例锁用的是一把【固定的】共享内存键 "hmi-single-instance-v1"。
    #  机器上只要有另一个 hmi.exe 在跑,第二个实例一启动就退出 ——
    #  于是这个脚本只有两个选择:"拒绝开跑"(exit 2),或者"把那个结束掉"。
    #  2026-10-21 的现场正是卡在这:上一轮的长跑进程还挂着
    #  (而且从它启动后【第一秒】起就没再跟 PLC 说过一句话),
    #  用户明确要求"不想影响它"。
    #
    #  给了 -InstanceKey 之后:长跑实例用另一把键 → 两把锁互不相干 →
    #  两个实例各跑各的,谁也不动谁 —— 已有进程一个字节都不动。
    #
    #  【安全边界】
    #   · 不给这个参数 = 行为和以前完全一样(固定键,撞上就退)。
    #   · 给了它也【不是】"取消单实例":同一把键的第二个实例照样被挡
    #     (tst_SingleInstanceKey::sameKeyStillCollides 钉着这一条)。
    #   · 它只换【锁】,不换【数据目录】。两个实例共用一份 history.db
    #     会互相写坏 —— 所以真要用它,必须同时把 LOCALAPPDATA 指到
    #     一份私有目录(scripts\run_soak.ps1 就是干这个的)。
    [string] $InstanceKey = '',

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
    [double] $MinCpuPercent = 5.0,    # 低于这个占用率【且日志也不长】才判定为 idle
                                       # ★ 2026-10-20:单看 CPU 在当前负载下没有分辨力
                                       #   (实测:真在跑 0.86% / 卡在登录框 0.5%),所以 idle
                                       #   现在要求两条证据同时成立。见下面 静默期快照。
    [int]    $SettleSec     = 90      # 启动后静默多久再取"活体快照"(等启动日志写完)
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

# ---- 事实常量(全部实测过,不是猜的)------------------------------------
# main.cpp:105-107 -> org=QianWei app=HMI
# QStandardPaths::AppLocalDataLocation 在 Windows 上是 Local 而非 Roaming
#
# ★ 2026-10-21:数据目录的算法必须跟应用【完全一致】。
#   脚本读一份、应用写另一份 ⇒ 判据全废(而且是那种"看着在跑、其实在读空气"的废)。
#   应用那边是 util::appDataDir()(见 hmi/src/util/Paths.h):
#     · HMI_DATA_DIR 设了 → 用它(长跑隔离走这条)
#     · 没设 → %LOCALAPPDATA%\QianWei\HMI
#   这里照抄同一份规则,一个字都不许差。
#
#   ★ 为什么【不是】直接读 $env:LOCALAPPDATA:
#     2026-10-21 实测:把 LOCALAPPDATA 指到别处再起一个实例,
#     它照样把日志写进真实的 %LOCALAPPDATA%\QianWei\HMI\,
#     还把同名的 audit-2026-10-21.log 截断了。
#     Qt 在 Windows 上用 SHGetKnownFolderPath 解析 LocalAppData —— 不认环境变量。
if ($env:HMI_DATA_DIR) {
    $dataDir = $env:HMI_DATA_DIR
} else {
    $dataDir = Join-Path $env:LOCALAPPDATA 'QianWei\HMI'
}
$logDir  = Join-Path $dataDir  'logs'
$crashDir = Join-Path $dataDir 'crash'
$marker  = Join-Path $crashDir 'pending.txt'

# ★★ 2026-10-27:数据目录是【私有】的吗?★★
#   私有 = HMI_DATA_DIR 设了(长跑隔离走这条,见 run_soak.ps1)。
#   ★ 这个标记后面决定两件事:
#     (1) 【2026-10-27 之前】能不能往 <数据目录>/hmi.json 写日志级别配置 ——
#         写进【用户真实的 %LOCALAPPDATA%】等于改了他机器上正式应用的行为
#         (把 debug 日志打开,永久生效),那是别人的机器,不能我替他决定。
#         ★★ 那一整块已经【删掉】了 —— 写侧统计行改成 INFO 级,默认就落地。
#     (2) 写侧判据(SOAK-W)的证据跟正式应用的日志会不会混在一起。
#   不是私有 ⇒ 证据会混,但【不影响】SOAK-W 能不能拿到证据
#   (2026-10-27:写侧统计行是 INFO 级,默认就落地,不再依赖 hmi.json)。
$dataDirIsPrivate = [bool]$env:HMI_DATA_DIR

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

# ---- "本次运行期间新产生的日志" --------------------------------------
#  为什么 Get-LogStats 不够用
#  -------------------------
#  Get-LogStats 只回答"日志目录一共多少字节",回答不了
#  "从静默期到现在,是【哪几类】日志在长"。
#  而 idle 判据要的恰恰是后者:
#  总量在长,可能是本地定时器在写;业务类在长,才是程序真的在干活。
#  这两件事必须能分开,否则判据看不见自己脚下那个洞。
#
#  ★★ 为什么按【时间戳】过滤,而不是按字节偏移(2026-10-21 改)★★
#
#  第一版是按字节偏移增量:记下静默期时文件长度 mark.Length,之后从那儿往后读。
#  看着更省,实际踩了一个坑 —— 【Length / LastWriteTime 会读到过期值】。
#
#  2026-10-21 实测(同一个 PowerShell 进程内,Get-Item / [FileInfo] /
#  [FileStream] / Get-ChildItem 四条读法彼此一致,但全都落后真实值好几分钟):
#      09:55:09  读到 len=47520  mtime=09:53:41
#      09:57:39  读到 len=47520  mtime=09:53:41   ← 与上一行一模一样
#  而真相是心跳每 60 秒写 80 字节、603 次零断档;
#  09:58:13 另一个新进程读到的就是 len=47840 mtime=09:57:41。
#  也就是说按偏移取增量会拿到一个"看起来没长"的假象 ——
#  而这个假象的方向正好是"进程没在干活",也就是判据最容易假绿的方向。
#
#  改成按日志行【自带的开头时间戳】过滤:
#    · 只依赖行内容,不依赖任何文件系统元数据
#    · 元数据过期多久都无所谓,最多多读进几条新行(再被时间戳滤掉)
#    · 代价是遍历全部日志文件 —— 实测 154 个文件共 602 KB,读一次几毫秒。
#      24h 长跑这点开销可以忽略;读错一次判据的代价不行。
$reLineTs = '^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\.\d{3}\s+\[(\w+)\s*\]\s+([\w.]+)\s+-\s+(.*)$'

function Get-LogSince($sinceTime, [int] $limit = 50000) {
    if ($null -eq $sinceTime) { return @() }
    $out   = New-Object System.Collections.Generic.List[object]
    $files = @(Get-ChildItem $logDir -Filter '*.log' -File -ErrorAction SilentlyContinue)
    foreach ($f in $files) {
        try {
            $fs = [System.IO.File]::Open($f.FullName, 'Open', 'Read', 'ReadWrite')
            $sr = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8)
            while (-not $sr.EndOfStream -and $out.Count -lt $limit) {
                $line = $sr.ReadLine()
                if ($line -match $reLineTs) {
                    $ts = [datetime]::ParseExact($matches[1], 'yyyy-MM-dd HH:mm:ss', $null)
                    if ($ts -ge $sinceTime) {
                        $out.Add([pscustomobject]@{
                            Time = $ts; Level = $matches[2]; Cat = $matches[3]; Msg = $matches[4]
                        })
                    }
                }
            }
            $sr.Close(); $fs.Close()
        } catch {
            Say ("读日志失败(" + $f.Name + "): " + $_.Exception.Message) 'Red'
        }
    }
    return $out
}

# ★ 不算"活体证据"的本地定时器日志(2026-10-21 实测得出)
#   这些日志在任何情况下都会长 —— PLC 断着、界面冻在模态框上、
#   业务全停,它们照样每 60 秒写一行。
#   2026-10-20 那次 950.9 分钟的实测:
#       "[HistoryRecorder] 心跳写了 3 行"  ≈ 每分钟一条,写了几百条
#       hmi.comm                          整个过程【一条都没有】
#   而 PLC 地址是 127.0.0.1:502 —— 回环,那儿没有设备。
#   旧判据 idle = CPU低 AND 日志不长,日志被心跳撑住了 ⇒ 判定资格成立 ⇒ 给了 PASS。
#   那个 PASS 证明的是"程序会自己给自己记心跳",不是"程序跑得住"。
#   ⚠ 这正是脚本 :530 自己写过的那句话,只是心跳日志让那条路没被走到。
$noiseRe = '^\[(HistoryRecorder|TrendWidget)\]'

# ---- 运行状态落盘(2026-10-21 新增)------------------------------------
#  ★ 为什么加这个函数
#  ------------------
#  2026-10-20 那次长跑跑了 950.9 分钟、950 个采样点,最后什么都没留下:
#  原因不在被测程序,是【这个 harness 自己】在 08:56~09:06 之间死了,
#  而 verdict.txt 只在脚本最后一行写(原来的 :605),没有 finally、没有增量落盘。
#  于是 16 小时的证据变成一份没有结论的 CSV —— 和没跑一样。
#
#  这类失效比程序崩溃更难发现:程序崩了日志里看得见;
#  【测它的工具崩了,现场只剩下"它没报错"这个假象】。
#  2026-10-20 那次的真实后果:hmi.exe PID 42428 一直活到第二天上午还在心跳,
#  看起来"跑得很好",实际上从 08:56 起就没有任何人在记录了。
#
#  三个动作堵这个洞:
#   1) 基线取完就写一次 —— 让"没有 verdict.txt"永远不再是可能状态
#   2) 每个采样 tick 刷新一次 —— 被强杀也留得下"跑到哪了、跑了什么"
#   3) 采样循环包 try/catch/finally —— 异常和 Ctrl-C 都落盘
#
#  【中途只写状态,不写判定】
#  判定要等全程数据齐了才算。强行在中途算,就是又一次"用半截数据下结论"。
#  所以 state=RUNNING 的文件【永远不能被当成 PASS】—— 它自己写着还没跑完。
function Write-RunState {
    param(
        [Parameter(Mandatory = $true)][string] $State,
        [string] $Detail = ''
    )
    $path = Join-Path $OutDir 'verdict.txt'
    $sb = New-Object System.Collections.Generic.List[string]
    $sb.Add('================ HMI 长跑 verdict ================')
    $sb.Add("状态              : $State")
    if ($State -eq 'DONE') {
        $sb.Add("★ 本次是最终判定。中途落盘的文件只代表进度,不构成结论。" )
    } else {
        $sb.Add("★ 本文件是【运行中状态】,不是结论 —— 判定尚未完成。")
    }
    $sb.Add("本次落盘          : " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
    $sb.Add("计划时长          : $Hours 小时")
    $sb.Add("单实例键          : " + $(if ($InstanceKey) { $InstanceKey } else { "默认(hmi-single-instance-v1)" }))
    $sb.Add("数据目录          : $dataDir")
    # ★ 2026-10-21:把"这一轮实际跑的是哪个二进制"落进状态文件。
    #   上面那段注释一直写着"把实际用的 exe 落到状态文件里",但其实【没落】——
    #   于是 2026-10-21 那次 -Exe 被静默忽略(变量名大小写撞车)的现场,
    #   报告里【没有任何一处】写着"跑的是旧二进制"。
    #   报告必须能自证:跑的是哪个 exe、哪个数据目录、哪把锁,三样都在文件里。
    $sb.Add("被测二进制        : " + $(if ($exePath) { $exePath } else { "(观察模式:未启动新进程)" }))
    if ($null -ne $owned -and $null -ne $owned.StartTime) {
        $sb.Add("被测进程          : PID=" + $owned.Id + " 启动于 " + $owned.StartTime.ToString('yyyy-MM-dd HH:mm:ss'))
    } else {
        $sb.Add("被测进程          : (尚未接管进程)")
    }
    $sb.Add("已跑分钟          : " + $(if ($samples.Count -gt 0) {
        [math]::Round(((Get-Date) - $owned.StartTime).TotalMinutes, 1) } else { '-' }))
    $sb.Add("采样点数          : " + $samples.Count)
    if ($samples.Count -gt 0) {
        $lastS = $samples[$samples.Count - 1]
        $sb.Add(("最后采样          : {0}  RunMinutes={1}  PrivateMB={2}  Handles={3}  Threads={4}  CpuSeconds={5}" -f `
                 $lastS.Time, $lastS.RunMinutes, $lastS.PrivateMB, $lastS.Handles, $lastS.Threads, $lastS.CpuSeconds))
    }
    # 崩溃基线时刻随状态一起落盘:SOAK-1 判的是"相对基线有没有新 dump",
    # harness 死后这个时间原点丢了就再也补不回来。写进文件 = 可离线复判。
    $sb.Add("崩溃基线时刻      : " + $baselineTime.ToString('yyyy-MM-dd HH:mm:ss'))
    $sb.Add("基线崩溃标记      : " + $(if ($baselineMarker.Exists) { "存在(" + $baselineMarker.Mtime + ")" } else { "无" }))
    if ($Detail -ne '') { $sb.Add("说明              : $Detail") }
    if ($State -eq 'DONE') {
        foreach ($rl in $report) { $sb.Add($rl) }
    }
    ($sb -join "`r`n") | Out-File -FilePath $path -Encoding UTF8
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

    # ★ 2026-10-21:给了独立实例键 → 允许共存,不碰那个进程。
    #   【为什么这条是安全的】
    #     锁不同 → 两个进程的共享内存不是同一块 → 谁也不会把对方当 sender。
    #     数据目录不同(见 run_soak.ps1 的私有 LOCALAPPDATA)→ 不抢同一个 history.db。
    #     被测进程由本脚本自己启动、自己采样、自己收尾 —— 那个已有进程
    #     【不参与本轮任何判定】,它继续跑它自己的,本轮一个字节都不动它。
    if ($InstanceKey) {
        Say "给了 -InstanceKey,本轮用【独立的单实例键】—— 允许与已有实例共存:" 'Cyan'
        Say ("  本轮实例键  : {0}" -f $InstanceKey) 'Cyan'
        Say "  已有进程    : 不结束它、不抢它的锁、不读它的数据目录 —— 一个字节都不动。" 'Green'
        Say "  ★ 前提:两边的数据目录必须分开(本脚本读 $dataDir)。" 'Yellow'
        Say "    直接跑本脚本而没设 HMI_DATA_DIR 的话,两边会抢同一个 history.db ——" 'Yellow'
        Say "    请用 scripts\run_soak.ps1 跑长跑,它会准备一份私有数据目录。" 'Yellow'
    }
    elseif (-not $AdoptExisting) {
        Say "拒绝启动第二个实例。原因有两个,都不是多余的:" 'Red'
        Say "  1) SingleInstance 会让第二次启动直接退出 —— 那样跑 2 小时采到的全是空气,还看不出异常;" 'Red'
        Say "  2) 那是你的进程,不是我的实验品。要跑长跑请先自己关掉它。" 'Red'
        Say "想改成观察它(拿不到干净基线,只算参考): -AdoptExisting" 'Yellow'
        Say "想和它共存(各跑各的,不动它): -InstanceKey <任意键名>" 'Yellow'
        exit 2
    }
    else {
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
}

# ---- 前置 2:找可执行文件 -------------------------------------------
# ★ 2026-10-21:本地变量【必须】叫 $exePath,不能叫 $exe ——
#   PowerShell 变量名【不区分大小写】,$exe 和 $Exe 是【同一个变量】。
#   原来这里写的是 `$exe = $null`,那一句把参数 $Exe 直接【抹成空了】,
#   紧接着的 `if ($Exe)` 永远为假 ⇒ -Exe 指定的路径【被静默忽略】,
#   脚本退回默认候选路径,挑中了【没修的】out\build\debug\hmi.exe。
#   而报告里还打着一行"可执行文件: ...debug\hmi.exe" —— 一本正经地跑错了二进制,
#   所有指标都正常,这一轮白跑一夜都没人看得出来。
#   这正是那段注释里写的"最难发现"的那一类错,自己撞上了。
#   变量改名 + 下面那句"选了哪个"照打,以后再撞不回来看一眼就知道。
$exePath = $null
if (-not $AdoptExisting) {
    if ($Exe) {
        if (-not (Test-Path $Exe)) {
            Say ("-Exe 指定的文件不存在: {0}" -f $Exe) 'Red'; exit 1
        }
        $exePath = (Resolve-Path $Exe).Path
    } else {
        $cands = @(
            (Join-Path $PSScriptRoot '..\hmi\out\build\debug\hmi.exe'),
            (Join-Path $PSScriptRoot '..\hmi\x64\Debug\hmi.exe')
        )
        $exePath = $cands | Where-Object { Test-Path $_ } | Select-Object -First 1
        if (-not $exePath) {
            Say "找不到 hmi.exe。先 build.ps1 编一份。" 'Red'; exit 1
        }
    }
    Say ("可执行文件: {0} ({1} MB)" -f $exePath, [math]::Round((Get-Item $exePath).Length/1MB,2)) 'Gray'
    # ★ 2026-10-21:把实际用的 exe 落到状态文件里。
    #   报告里必须能一眼看出"这一轮跑的是哪个二进制"——
    #   修好的版本在 debug2、没修的在 debug,认错了就等于白跑一夜。
    if ($Exe) {
        Say "  (由 -Exe 指定)" 'DarkGray'
    } else {
        Say "  (⚠ 没给 -Exe,用的是默认候选路径 —— 可能挑到旧二进制)" 'Yellow'
    }
}

# ---- 基线快照 --------------------------------------------------------
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot "..\soaks\$(Get-Date -Format 'yyyyMMdd_HHmmss')" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

# ★ 2026-10-21:$samples / $csvPath / $report 提前到这里初始化。
#   原来在静默期之后才建,可 Write-RunState 需要它们在任何时刻都存在 ——
#   于是"开跑前就崩了"这种最需要留痕的场景,恰恰成了唯一写不出状态的场景。
#   （StrictMode 2.0 下引用还没建的变量会直接抛异常,不是给个 null 就完事。）
#
#   ⚠ $deadline 仍然留在采样循环前面,它没跟着搬:它算的是"从采样开始起算",
#     提前到这里会让实际采样时长比 -Hours 短一截。
$samples = New-Object System.Collections.Generic.List[object]
$csvPath = Join-Path $OutDir 'samples.csv'
$report  = New-Object System.Collections.Generic.List[string]

$baselineLog = Get-LogStats
$baselineTime = Get-Date          # 一切"本次新增"的判定都以这一刻为界
$baselineMarker = if (Test-Path $marker) {
    [pscustomobject]@{ Exists = $true; Content = (Get-Content $marker -Raw -Encoding UTF8); Mtime = (Get-Item $marker).LastWriteTime }
} else { [pscustomobject]@{ Exists = $false; Content = ''; Mtime = $null } }

# ================================================================
# ★★ 2026-10-27:【不再】往 <数据目录>/hmi.json 写日志级别配置 ★★
# ================================================================
#  ★ 这一整块被【删掉】了 —— 因为它是 2026-10-27 实测出来的一颗雷 ★
#
#  【它原来要干什么】
#    应用读 <数据目录>/hmi.json 决定各类别的日志级别(Logging.cpp:loadConfig),
#    没这个文件就是 default_level=info。
#    而"写被排队"/"排队写已发出"/"[Write] OK"这几行【全是 qCDebug】——
#    info 级别下一行都不落地 ⇒ 写侧判据会读到"排队 0 次、OK 0 次、拒收 0 次",
#    然后判 PASS —— 而它其实【什么都没看见】。
#
#  ★★ 为什么最后把它删了(三条实测,不是推演)★★★
#    (1) 【前提就是错的】
#        实测:WriteScheduler.cpp:240 那行"driver 拒收了"是 qCWarning(hmiWrite),
#        严重度排名 2 > info 的 1 ⇒ 【默认级别就看得见】。
#        当时"必须开 debug 才看得见"这个前提本身就不成立。
#    (2) ★ 它会【毁掉整轮长跑】★
#        实测:开了 hmi.comm=debug 之后 893 行/分钟 ⇒ 24 小时约 129 万行。
#        而 Get-LogSince 原来 limit=50000 且【到顶就整个停止读】
#        ⇒ 只看得到前 0.9 小时 ⇒ 【所有】判据(SOAK-6 也包括)全被截断,
#          而且报告上看不出任何异常 —— 它只是安静地判了个 UNKNOWN。
#        ⇒ "开 debug 才能看见写侧"这个做法,代价是【所有判据都瞎】。
#    (3) ★ 现在不需要它了 ★
#        2026-10-27:写侧统计行([ModbusMaster] 写侧统计:…)是【qCInfo】——
#        默认级别就落地。而它带的是【累计计数】,不是一次性的 debug 行。
#        ⇒ 60 秒一条,24 小时约 1440 行,跟日志本底(实测 1440 行/24h)一个量级。
#        ⇒ 根本不需要改日志级别。
#
#  ★ 现在的判定闸换成了什么 ★
#    窗口内【一条写侧统计行都没有】⇒ UNKNOWN(看不见证据 ≠ 没问题)。
#    这个闸比"有没有写 hmi.json"更直接:它问的是"证据在不在",而不是"配置在不在"。
#
#  ★ 私有数据目录这个标记还要留着 ★
#    它不决定日志级别了,但它决定"这一轮的数据跟正式应用隔不隔离" ——
#    隔离没做的话,长跑的证据会跟正式应用的日志混在一起,那证据就不算证据。
if ($dataDirIsPrivate) {
    Say ("数据目录   : 私有({0})—— 与正式应用完全隔离,证据不混。" -f $dataDir) 'Cyan'
} else {
    Say "★ 数据目录【不是私有的】(%LOCALAPPDATA%\QianWei\HMI)—— 本轮日志会跟正式应用的混在一起。" 'Yellow'
    Say "  写侧统计行是 INFO 级、默认就落地,所以这一项【不再】影响 SOAK-W 的证据。" 'Yellow'
    Say "  但隔离没做 ⇒ 长跑的证据会跟正式应用的日志混在同一批文件里。" 'Yellow'
    Say "  ⇒ 要干净的证据,请用 scripts\run_soak.ps1(它会准备私有数据目录)。" 'Yellow'
}

# ★ 存量脏标记:上一轮崩溃留下的、还没被任何启动消费掉的那个。
#   单独拎出来报 —— 免得被"本次没崩"盖过去。
#   2026-10-14 实测:它在 crash/pending.txt 停了 28 天。
Say "---- 基线 ----" 'Cyan'
if ($baselineLog.Count -gt 0) {
    Say ("日志: {0} 个文件 / 合计 {1} KB;最大单个 {2} KB({3});最新 {4}({5} KB)" -f `
         $baselineLog.Count, [math]::Round($baselineLog.Bytes/1KB,1),
         [math]::Round($baselineLog.MaxFile.Length/1KB,1), $baselineLog.MaxFile.Name,
         $baselineLog.Newest.Name, [math]::Round($baselineLog.Newest.Length/1KB,1))
} else {
    # ★ 空目录也是【一种状态】,得说出来 —— 不说就等于"日志统计这行没跑"。
    Say "日志: 0 个文件(数据目录是干净的 —— 这一轮的日志从零开始)" 'Gray'
}
if ($baselineMarker.Exists) {
    Say "★ 崩溃标记【已经存在】(上一轮崩溃没人清理):" 'Yellow'
    $baselineMarker.Content.Trim() -split "`n" | ForEach-Object { Say ("    " + $_) 'Yellow' }
} else {
    Say "崩溃标记: 无"
}

# ★ 开跑前先落一次盘。从这一刻起,"没有 verdict.txt"就不再是可能状态 ——
#   哪怕脚本在下面任何一行崩掉、被 Ctrl-C、被关掉终端,这个文件都在,
#   而且里面带着崩溃基线时刻(SOAK-1 的时间原点)。
Write-RunState 'BASELINE' '基线已取,尚未启动被测进程'

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

# ---- 静默期快照:判断"进程还在干活"的分界点 -------------------------
#  ★ 为什么需要它(2026-10-20 改)
#  ---------------------------------------------
#  原来的 idle 判据只有一条:平均 CPU < MinCpuPercent(默认 5%)→ 判 idle。
#  那条判据是【从一个猜测来的】,原注释写的是
#  "真正跑着报警引擎 + Modbus 轮询的 HMI,CPU 至少要高一个数量级"。
#  2026-10-20 实测把这个猜测证伪了:
#      真的在跑(MBSlave 已连上、3 个变量 1Hz 轮询、历史在写)→ 平均 CPU 0.86%
#      卡在登录框不动(第一轮实测)                      → 平均 CPU 0.5%
#  0.5% 和 0.86% 差 1.7 倍,而且这个差值还会随日志量、GC、机器负载漂。
#  【拿它当判据 = 分不出"真在跑"和"冻住了"】—— 这不是阈值没调好,
#  是这个量在当前负载下【根本没有分辨力】。
#  而它判错的方向是:真在跑 → 判 idle → 报告写"进程没在跑业务"。
#  那是【一句假话】,比判不出来更坏。
#
#  改成:idle = 【CPU 低】 AND 【日志不再增长】。
#  日志增长是"这个进程还在做它该做的事"的【客观证据】——
#  进程被模态框冻住时事件循环停了,定时器不跑,日志就不增长;
#  真在跑的时候 HistoryRecorder 每 60 秒写一行心跳,日志就在长。
#  判据因此是可以失败的:进程一冻,立刻判 idle → UNKNOWN。
#
#  ★ 为什么快照要放在"启动之后"而不是用开头的基线
#  启动那几秒本身也会写日志(24 行启动日志)。拿【启动前】的基线去比,
#  卡在登录框的进程也会显得"日志长了" → 判成不 idle → 假绿。
#  所以要在启动之后、确认"启动噪声已经写完"的位置再取一次快照,
#  之后拿它跟最终快照比。只看【中途之后还有没有新日志】,
#  启动噪声就被排除在外了。
Say ("静默期 {0} 秒(等启动日志写完,再取活体快照)..." -f $SettleSec) 'Cyan'
Start-Sleep -Seconds $SettleSec
$settledLog = Get-LogStats
# ★ 活体判据的时间原点:静默期结束的那一刻。
#   之后按日志行开头的时间戳过滤,取"这一时刻之后"的行。
#   不用字节偏移 —— 文件元数据会读到过期值,详见 Get-LogSince 上面的实测。
$settleTime = Get-Date
if ($null -eq $settledLog) {
    Say "静默期快照:没有日志目录 —— 拿不到活体证据,按'没在干活'处理" 'Yellow'
} else {
    Say ("静默期快照:日志 {0} 个文件 / 合计 {1} KB" -f `
         $settledLog.Count, [math]::Round($settledLog.Bytes/1KB,1)) 'Cyan'
}
Write-RunState 'SETTLED' '静默期结束,即将开始采样'

$deadline  = (Get-Date).AddHours($Hours)
$tick      = 0

# ---- 采样循环 --------------------------------------------------------
#  ★ 包 try/catch/finally(2026-10-21)
#    原来这个 while 是裸的:采样过程中任何一次抛异常
#    (Get-Sample 碰到已退出的进程、Export-Csv 撞上文件锁……)
#    都会直接冲出脚本,后面 200 行判定一行都不执行,verdict.txt 永远不写。
#    这是"什么都没有"的另一条可能路径 —— 而且它连崩溃现场都不留。
#    现在:循环怎么结束都先落一次盘,再决定要不要继续判定。
$loopState  = 'COMPLETED'
$loopDetail = ''
Say "---- 开始采样(Ctrl-C 可提前结束)----" 'Cyan'
try {
    while ($true) {
        if ($Hours -gt 0 -and (Get-Date) -gt $deadline) { Say "到时间了。" 'Green'; break }
        $null = $owned.Refresh()
        if ($owned.HasExited) {
            Say ("★ hmi.exe 自己在第 {0} 分钟退出了,exit code={1}" -f `
                 [math]::Round(((Get-Date) - $owned.StartTime).TotalMinutes,1), $owned.ExitCode) 'Red'
            $loopState = 'APP_EXITED'
            break
        }
        $samples.Add((Get-Sample $owned))
        $tick++
        # ★ 每个 tick 都落盘(原来每 10 次)。
        #   10 个 tick 就是 10 分钟,机器半夜被谁强制重启就丢 10 行;
        #   verdict.txt 更是一个字节都舍不得丢 —— 它是"跑了没有、跑成什么样"的唯一凭据。
        $samples | Export-Csv -Path $csvPath -NoTypeInformation -Encoding UTF8
        Write-RunState 'RUNNING'
        if ($tick % 10 -eq 0) {
            $last = $samples[$samples.Count - 1]
            Say ("  {0} 内存 {1} MB / 句柄 {2} / 线程 {3} / CPU {4}s" -f `
                 $last.Time, $last.WorkingSetMB, $last.Handles, $last.Threads, $last.CpuSeconds) 'DarkGray'
        }
        Start-Sleep -Seconds $IntervalSec
    }
} catch {
    $loopState  = 'THREW'
    $loopDetail = $_.Exception.Message
    Say ("★ 采样循环异常终止: " + $loopDetail) 'Red'
    Say "  已采到 $($samples.Count) 个点,下面照常出判定 —— 这次不是白跑。" 'Yellow'
} finally {
    $samples | Export-Csv -Path $csvPath -NoTypeInformation -Encoding UTF8
    if ($loopState -ne 'COMPLETED') {
        Write-RunState $loopState ("采样循环非正常结束($loopState)。已采 $($samples.Count) 点,判定仍会继续并覆盖本文件。")
    }
}

# ★ 样本不足 2 个就没法算首尾差 —— 而原来的写法会在这里直接抛异常:
#   $samples[0] 在空 List 上越界,StrictMode 2.0 下整个脚本带崩。
#   也就是说"程序启动就死"这种最需要结论的场景,最容易白跑(连崩溃记录都没有)。
#   现在提前收尾,并把"为什么判不了"写进文件。
if ($samples.Count -lt 2) {
    Say ("★ 只采到 {0} 个样本(至少要 2 个才能算首尾差),不出判定。" -f $samples.Count) 'Red'
    if ($null -eq $owned -or $owned.HasExited) {
        Say "  本次确实是被测进程提前退出 —— 去看 $logDir 里最新那个日志文件。" 'Yellow'
    } else {
        Say "  多半是 -Hours 设得太小(采样循环一次都没转完)。" 'Yellow'
    }
    Write-RunState 'NO_SAMPLES' ("只采到 {0} 个样本,无法计算首尾差,不构成判定。" -f $samples.Count)
    exit 3
}

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

# ($samples / $csvPath / $report 已在前面提前初始化 —— 见 OutDir 之后那段注释)
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

# ★ idle 检测(2026-10-21 第三次改:把"活着"和"在干活"分开)
#
#   三代判据,连同各自被什么证伪,一起留在这里 ——
#   因为第三代也会被第四次实测证伪,而那时候需要知道前面三代错在哪:
#
#     第一代:只看 CPU < 5% 就判 idle
#       被 2026-10-20 实测证伪:真在跑 0.86% / 卡在登录框 0.5%。
#       1.7 倍的差距,还会随日志量和机器负载漂 —— 这个量没有分辨力。
#
#     第二代:CPU 低 AND 日志不长(commit 7588a36)
#       解决了"冻在模态框 vs 真在跑",但漏了第三种状态。
#
#     第三代(本次):按日志【类别】看,本地定时器心跳不算活体证据
#
#   第二代为什么还漏:它只看日志【总量】在不在长。
#   而 "[HistoryRecorder] 心跳写了 3 行" 每 60 秒写一行,那是本地 SQLite 写盘 ——
#   PLC 断着它在写,界面冻在模态框上它也在写,业务全停它照样在写。
#
#   2026-10-20 那次 950.9 分钟的实测(就是 harness 自己死掉的那次):
#       CPU 0.74%(低于 5% 阈值)      ⇒ 第一条腿说 idle
#       心跳日志一直在长              ⇒ 第二条腿说不是 idle
#       ⇒ isIdle = FALSE ⇒ 给了 PASS
#   而同一份日志里 hmi.comm【一条都没有】,PLC 地址 127.0.0.1:502(回环,那儿没有设备)。
#   那个 PASS 证明的是"程序会自己给自己记心跳",不是"程序跑得住"。
#   —— 这是假绿的第三种形态:量没算错,被量的东西不对。
$cpuSpan = $lastN.CpuSeconds - $first.CpuSeconds
$cpuPct  = if ($spanMin -gt 0) { ($cpuSpan / ($spanMin * 60.0)) * 100.0 } else { 0 }

$logGrewBytes = if ($null -ne $finalLog -and $null -ne $settledLog) { $finalLog.Bytes - $settledLog.Bytes } else { 0 }
# ⚠ $logGrewBytes 只作报告里的参考数,不参与判定 ——
#   它来自 Length,而 Length 会读到过期值(见 Get-LogSince 上面的实测)。
#   下面的 $logGrew / $bizGrew 一律由日志行本身算出来。
$sinceLines = @(Get-LogSince $settleTime)
$logGrew    = ($sinceLines.Count -gt 0)
$noiseLines = @($sinceLines | Where-Object { $_.Msg -match $noiseRe })
$bizLines   = @($sinceLines | Where-Object { $_.Msg -notmatch $noiseRe })
$bizGrew    = ($bizLines.Count -gt 0)

# 三种"没在干活",分开报 —— 它们指向的排查方向不一样,混成一句"idle"就废了
$isFrozen    = ((-not $logGrew) -and ($cpuPct -lt $MinCpuPercent))  # 不记日志且不吃 CPU:冻住了
$isBusyQuiet = ((-not $logGrew) -and ($cpuPct -ge $MinCpuPercent))  # 不记日志但在忙
$isDormant   = ($logGrew -and (-not $bizGrew))                       # 在长,但只有本地心跳
$notQualified = $isFrozen -or $isDormant                            # 本轮【不构成】稳定性验收
# 兼容旧变量名:下面 SOAK-3 还在用它
$isIdle = $notQualified

$sinceByCat = @($bizLines | Group-Object Cat | Sort-Object Count -Descending |
                Select-Object -First 8 @{n='cat';e={$_.Name}}, @{n='n';e={$_.Count}})
$sinceCatTxt = ($sinceByCat | ForEach-Object { "$($_.cat)=$($_.n)" }) -join '  '
if ($sinceCatTxt -eq '') { $sinceCatTxt = '(无)' }

# 没资格下结论时降级为 UNKNOWN:不是"通过",是"这一轮没有资格"。
# 一个没连上 PLC、没跑报警引擎的进程,内存当然不涨 ——
# 拿它的平稳去证明"程序不泄漏"是偷换前提。
if ($notQualified) {
    $memState = 'UNKNOWN'
} elseif ($memRatio -gt $MaxMemRatio -and $memPerHr -gt $MaxMemPerHour) { $memState = 'FAIL' }
elseif ($memRatio -gt $WarnMemRatio) { $memState = 'WARN' }
else { $memState = 'PASS' }

$memTxt = ("私有内存 {0} -> {1} MB ({2:N3}x),斜率 {3:N2} MB/小时;线程 {4}->{5};阈值 {6}x & {7}MB/h" -f `
    $first.PrivateMB, $lastN.PrivateMB, $memRatio, $memPerHr, $first.Threads, $lastN.Threads,
    $MaxMemRatio, $MaxMemPerHour)
$memTxt += ("`n        静默期以来新增日志 {0} 行 = 业务类 {1} + 本地心跳 {2};业务类按类别:{3}" -f `
    $sinceLines.Count, $bizLines.Count, $noiseLines.Count, $sinceCatTxt)
if ($isFrozen) {
    $memTxt += ("`n        ★ 冻住:平均 CPU {0:N2}%(低于阈值 {1}%),且静默期之后日志【一条都没长】。" -f $cpuPct, $MinCpuPercent)
    $memTxt += "`n          —— 事件循环停了。冻住的进程本来就不涨内存,这一轮【不构成验收】。"
    $memTxt += "`n          查:是不是卡在某个模态框上(登录框?配置对话框?),或被谁挡住了。"
} elseif ($isDormant) {
    # ★ 这一支就是 2026-10-21 加的。第二代判据在这里会漏判成 PASS。
    $memTxt += ("`n        ★ 只在自转:日志在长(净增 {0} KB),但【业务类日志一条都没有】。" -f [math]::Round($logGrewBytes/1KB,1))
    $memTxt += ("`n          全是本地定时器心跳({0} 行 HistoryRecorder/TrendWidget),平均 CPU {1:N2}%。" -f $noiseLines.Count, $cpuPct)
    $memTxt += "`n          进程活着,但它没在做业务 —— PLC 大概率没连上,轮询和报警引擎都是停的。"
    $memTxt += "`n          这种状态内存当然不涨,所以本轮【不构成对程序稳定性的验收】,判 UNKNOWN。"
    $memTxt += "`n          ★ 2026-10-20 那 950.9 分钟就是在这一支上骗过去的:旧判据只看日志总量,"
    $memTxt += "`n            心跳把'总量在长'撑住了,于是给了 PASS。本次实测已复现。"
    $memTxt += "`n          查:PLC 地址是不是回环(127.0.0.1)?启动有没有带 --soak-user/--soak-pass?"
    $memTxt += "`n              登录账号的角色是不是 viewer(只读)?hmi.comm 一共几条?"
} elseif ($isBusyQuiet) {
    $memTxt += ("`n        进程在忙但不记业务日志(平均 CPU {0:N2}%) —— 本轮【无法判定活体】,不下结论。" -f $cpuPct)
} elseif ($cpuPct -lt $MinCpuPercent) {
    # ★ 这条分支要写出来,不然读报告的人会以为脚本判错了:
    #   CPU 明明低于 5% 阈值,怎么不判成没在干活?
    $memTxt += ("`n        CPU {0:N2}%(低于阈值 {1}%),但静默期之后【业务类日志在长】({2} 行) —— 判定资格成立。" -f `
        $cpuPct, $MinCpuPercent, $bizLines.Count)
    $memTxt += "`n        —— 进程确实在干业务,只是这个活儿本身不吃 CPU,所以【不判没在干活】。"
    $memTxt += "`n        (旧判据只看 CPU,这里会被误判成 idle,报出'进程没在跑业务'这句假话 —— 2026-10-20 已改)"
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
if ($notQualified) { $hdlTxt += "`n        (同 SOAK-2:本轮没有资格下结论 —— 详见 SOAK-6 业务活体)" }
Verdict 'SOAK-3' '句柄/线程' $hdlState $hdlTxt

# --- 4. 业务活体(SOAK-6,2026-10-21 新增)--------------------------------
#  为什么把它单列一条,而不是塞在 SOAK-2 的说明里
#  --------------------------------------------------
#  SOAK-2/3 判的是"没资格下结论",读报告的人会问"那到底有什么问题"。
#  根因值得一条独立编号:它不是资源曲线的一部分,是【这一轮的前提】。
#
#  ★ 这条是本脚本目前唯一能回答"这轮到底跑没跑上业务"的判据。
#    它明确不判定掉没掉线(文案还没摸清,见文件头),
#    只判定"静默期之后有没有业务类日志在长" ——
#    有 ⇒ 有资格继续看 SOAK-2/3 的数字;没有 ⇒ 那些数字【全部作废】。
$liveTxt = ("静默期以来日志增量 {0} 行 = 业务类 {1} + 本地心跳 {2};平均 CPU {3:N2}%" -f `
    $sinceLines.Count, $bizLines.Count, $noiseLines.Count, $cpuPct)
if ($isFrozen) {
    $liveState = 'FAIL'
    $liveTxt += "`n        冻住:一条日志都没长,事件循环停了(可能是模态框)。这一轮什么都不用看了。"
} elseif ($isDormant) {
    $liveState = 'FAIL'
    $liveTxt += "`n        ★ 只在自转:业务类日志 0 条,全是本地心跳。进程活着但没跑业务。"
    $liveTxt += "`n        这一轮【不能用来证明程序稳定】—— 内存/句柄平坦是因为什么都没干。"
} elseif ($isBusyQuiet) {
    $liveState = 'UNKNOWN'
    $liveTxt += "`n        不记业务日志但 CPU 有占用 —— 判不出活体,不下结论。"
} elseif ($bizGrew) {
    $liveState = 'PASS'
    $liveTxt += "`n        业务类日志在长,判定资格成立;SOAK-2/3 的数字这一轮有效。"
} else {
    $liveState = 'UNKNOWN'
    $liveTxt += "`n        分类结果为空,判不出活体。"
}
if ($sinceCatTxt -ne '(无)') {
    $liveTxt += ("`n        业务类按类别:" + $sinceCatTxt)
}
if ($noiseLines.Count -gt 0) {
    $noiseByCat = @($noiseLines | Group-Object Msg | Sort-Object Count -Descending |
                    Select-Object -First 3 @{n='m';e={$_.Name}}, @{n='n';e={$_.Count}})
    $liveTxt += "`n        噪声 Top3:" + (($noiseByCat | ForEach-Object { "$($_.m) x$($_.n)" }) -join ' | ')
}
Verdict 'SOAK-6' '业务活体' $liveState $liveTxt

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
# ★ 走 Write-RunState 而不是直接 Out-File(2026-10-21):
#   现在 verdict.txt 从 BASELINE 起就存在,这行只是把它升级成"最终判定"。
#   就算前面任何一步崩了,那个中途文件也还在 —— 状态写着崩在哪儿、采到第几点。
Write-RunState 'DONE'

Say "" ; Say "========== 汇总 ==========" 'Cyan'
$report | ForEach-Object { Say $_ }
Say ""
Say "采样点 $tick 个,跨度 $([math]::Round($spanMin,1)) 分钟" 'Gray'
Say "样本: $csvPath" 'Gray'
Say "判定: $(Join-Path $OutDir 'verdict.txt')" 'Gray'

# ★ 横幅:放最显眼的位置。这轮如果没资格下结论,上面所有 UNKNOWN 都是同一个原因,
#   不单独说一句的话,读报告的人容易只扫到别的 PASS 就以为验过了。
#   ★ 三种情况分三句话说(2026-10-21)—— 它们指向的排查方向完全不一样,
#     混成一句"进程 idle"的话,拿到的人只能从头猜。
if ($notQualified) {
    Say "" ; Say "════════════════════════════════════════════════════" 'Red'
    if ($isFrozen) {
        Say ("★ 本轮没有结论:进程冻住了(CPU {0:N2}%,静默期之后日志一条都没长)" -f $cpuPct) 'Red'
        Say "  事件循环停了 —— 查是不是卡在某个模态框上(登录框?某个配置对话框?)。" 'Red'
    } elseif ($isDormant) {
        Say ("★ 本轮没有结论:进程只在自转(CPU {0:N2}%,日志在长但业务类 0 条)" -f $cpuPct) 'Red'
        Say ("  新增日志 {0} 行全是本地心跳 —— 进程活着,业务没跑起来。" -f $noiseLines.Count) 'Red'
        Say "  查:PLC 连上没有(地址是不是 127.0.0.1 回环?)、登录角色是不是 viewer、" 'Red'
        Say "      轮询变量有没有启用。资源曲线平坦是'什么都没干'的必然,不是'稳定'的证据。" 'Red'
    } else {
        Say ("★ 本轮没有结论(平均 CPU {0:N2}%,静默期之后无业务日志)" -f $cpuPct) 'Red'
    }
    Say "  退出码 4 表示'没验成' —— 和 1(验出问题)、3(有告警)是两回事,别混。" 'Red'
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
$failCount = @($report | Where-Object { $_ -match '^\[FAIL\]' -and $_ -notmatch 'SOAK-6' }).Count
$warnCount = @($report | Where-Object { $_ -match '^\[WARN\]' -and $_ -notmatch 'SOAK-6' }).Count
$unkCount  = @($report | Where-Object { $_ -match '^\[UNKNOWN\]' }).Count

if ($failCount -gt 0) { exit 1 }
if ($warnCount -gt 0) { exit 3 }
# SOAK-6 FAIL = 没验成(进程没在跑业务),和 UNKNOWN 归为一类
if ($liveState -eq 'FAIL' -or $unkCount -gt 0) { exit 4 }
exit 0

# ★ 2026-10-21:SOAK-6「业务活体」判 FAIL 走 exit 4,不进 exit 1。
#   它的含义是"这一轮没有资格下结论",不是"验出了故障"。
#   混进 failCount 的话,调用方会把"没验成"当成"验出问题",
#   两条完全不同的结论共用一个退出码,门禁就没法做了。
#   (上面的 $failCount 统计只认 SOAK-1/2/3,SOAK-6 在这里单独处理。)