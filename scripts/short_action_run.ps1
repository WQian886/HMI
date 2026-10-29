# ============================================================================
# short_action_run.ps1 —— 几分钟的短跑实测:验"报警 → 联动 → PLC 真的收到写"
#
#  为什么要有这个脚本(它不是 24h 长跑的缩水版,是另一件事)
#  ----------------------------------------------------------
#  2026-10-21 的 24 小时长跑实测发现:工程里配的联动
#    "温度高 → 把 Speed 写 0"(也就是"停机")
#    【一次都没执行过】。从站日志里 390369 次 FC3 读、FC6/FC16 写 = 0,
#    应用日志里 "[Action]" 行 = 0。
#
#  根因是类型不匹配:联动值来自 JSON("write_value": 0 → QVariant(LongLong)),
#  而 Variable::setValue 严格按 typeId 相等校验,UInt16 变量只认 UShort(36) ——
#  LongLong(4) ≠ UShort(36) → Rejected → 不标脏 → WriteScheduler 看不见它。
#  修法是在【调用方】适配(util/ValueCoercion),不是放宽 Variable 的校验。
#
#  ★ 这个脚本验的是"改完之后真的通了",不是"改完编译过了"★
#    单测绿只证明 ActionManager 那一层绿;
#    这里验的是【整条链在真实进程 + 真实 socket 上走通】:
#      轮询读到 Temperature → 越限 → 报警触发 → 联动 → 标脏
#      → WriteScheduler 发帧 → Modbus TCP → 从站寄存器真的变成 0
#
#  ★ 它不跑 24 小时,是因为要验的是"通没通",不是"稳不稳"★
#    稳定性是 24h 那件事。这里几分钟就够:从站的 Temperature 波形
#    第一拍就 > 50(阈值),报警在启动后几秒内就会触发。
#
#  ★ 隔离:两把锁 + 一份私有数据目录(同 run_soak.ps1)★
#    · HMI_INSTANCE_KEY —— 独立单实例键,跟机器上已在跑的 hmi.exe 不抢锁
#    · HMI_DATA_DIR    —— 私有数据目录,history.db / 日志 / 审计日志各写各的
#      (Qt 在 Windows 上不认 LOCALAPPDATA 环境变量,只能走应用自己认的开关,
#       见 hmi/src/util/Paths.h)
#    ★ 已经在跑的 hmi.exe 一个字节都不动 —— 本脚本只结束自己启动的那两个进程。
#
# ============================================================================
#  ★★★ 2026-10-26:判据升级 —— 从"数写帧"改成"查写出去的值" ★★★
# ============================================================================
#
#  【旧判据的洞(为什么必须改)】
#    旧判据只做两件事:
#      ① 从站日志里数 FC6/FC16 写帧的【条数】 > 0
#      ② 从站日志里找一条 FC3 读回、第 3 个寄存器(Speed)是 0
#    它【从来没有检查过写出去的值是多少】。
#
#    ★ 而 BUGFIX-D1(G-16)那个 bug 的现场形态恰恰是"写出去的数是错的"★
#      轮询回包把联动刚写的 0 冲回旧值(比如 100),
#      WriteScheduler 下一拍现取 v->value() → 发出去的是 100。
#      旧判据判它【绿】:
#        ① 写帧确实有(条数 > 0)          ✓ 绿
#        ② 读回时 Speed 是 0              ✓ 绿(从站的 tick 每 5 秒会把寄存器
#                                            重新写成波形值,读回本来就常是 0)
#      ⇒ 一个"停机写成了 100"的现场,旧判据判"联动写通路通了"。
#      这跟 2026-10-21 那个"联动一次都没执行过"是同一类错误:
#      判据量的是【有没有动作】,不是【动作对不对】。
#
#  【新判据量的是什么】
#    从站日志里那一行:
#        "FC6 写 addr=2 value=0"
#    ★ 这个 value 是【从站从网线上收到的字节】——
#      从站不会撒谎,这是唯一不能被应用自己骗的证据。★
#    所以新判据的核心是一条:
#        ★ 每一个写帧的值,都必须等于【工程里配的那个值】 ★
#    期望值不是写死在脚本里的常量 —— 是从【工程文件】里读出来的:
#      actions[].variable → variables[].address / actions[].write_value
#    理由:判据要是跟被测对象配成一样的,那它就只会"验证自己"。
#      期望值写死 = 万一工程里的 write_value 变了,判据还在按老值判,判红。
#      从工程读 = 判据永远跟着被测对象走,而且【写错值】一定会被抓出来。
#
#  【判据清单(全部是硬判据,任何一条不过就是红)】
#    J1 有写帧            —— 没有 = 联动压根没执行(2026-10-21 的症状)
#    J2 写的【地址】都对   —— 写错寄存器 = 改错机器
#    J3 写的【值】都对     —— ★ 本次新增的核心 ★ 写出去的是别的数 = G-16
#    J4 [Action] 行的值对  —— 联动自己算出来的值对不对
#    J5 [Write] 发写行的值对 —— WriteScheduler 真正编进帧里的值对不对
#    J6 [Write] OK 行的值对 —— 回执时记下来的值对不对(= 审计记的那个值)
#    J7 读回 Speed=0       —— 寄存器真的变成 0 了
#    J8 写次数 ≥ 联动次数  —— 每次联动都得有对应的写
#
#  【J5/J6 为什么要开着 hmi.write 的 DEBUG 才能看见】
#    WriteScheduler 里的两行是 qCDebug:
#      "[WriteScheduler] 发写:Speed ... addr=0x2 value=0 reqId=1"   ← 发出时的值
#      "[Write] OK:Speed=0(plc=plc1)"                                ← 回执时记下的值
#    默认级别是 Info ⇒ 这两行默认【看不见】。
#    本脚本在【私有数据目录】里写一份 hmi.json 把 hmi.write / hmi.poll 调到 debug。
#    ★ 注意:日志配置文件是【数据目录里的 hmi.json】,不是工程文件 ——
#      loadConfig() 读的是 appDataDir()/hmi.json(Logging.cpp:217)。
#      所以改工程文件里的 logging 段【一点用都没有】。
#
#  【审计那一半怎么验 —— 说清楚,不夸大】
#    写审计条目(WriteAuditEntry)只进【界面表格】,【从不落文件】
#    (hmi.cpp:927-937:auditAppended 只 insertRow 到 ui.auditTable)。
#    审计条目里的 newValue 就是 sentValue —— 跟 J6 那行 "[Write] OK:" 打印的
#    是【同一个变量】(WriteScheduler.cpp:328 与 :297)。
#    所以本脚本用 J6 那一行作为审计值的【代理证据】,
#    审计条目本身那一行的证据在单元层(tst_ActionWrite 的
#    auditRecords_theValueActuallySent,负向对照 M8 把审计改回"reply 时刻现取",
#    那条用例会红)。
#    ★ 这个分工必须写出来:短跑证明的是"【发出去】的值对",
#      单元测试证明的是"【审计记的】值对"。★
#
#  ★★ 时序:为什么短跑要改工程的轮询节奏(★ 这一段是【被实测推翻后重写的】★★)
#    G-16 是个【竞态】:要复现它,必须有一次轮询回包正好落在
#    "联动已经 setValue+markDirty" 和 "WriteScheduler 把这帧发出去" 之间。
#
#    【原以为】(2026-10-25 写这段时的推理,已被 2026-10-26 的实测推翻)
#      "原工程变量轮询间隔是 200ms —— 一次回包都落不进这个窗口,
#       所以把副本收紧到 50ms,下一次回包【几乎必然】落在窗口里"
#    【实测推翻】(2026-10-26:16:31 那轮 4 分钟 + 17:09 那轮 1 分钟,
#      守卫命中都是 0 次 —— 一次都没撞上)
#      收紧到 50ms 之后,一次都没撞上。原因是【两个定时器的相位被锁死了】,
#      而"下一次回包"落在联动前还是联动后,取决于 min_duration 的大小,
#      不是取决于轮询有多密。推导见下面"竞态的时序模型"。
#
#    所以本脚本做两件事:
#      ① 复制出去的工程把变量轮询间隔改成 $PollMs(默认 50ms)、
#         报警最小持续改成 $AlarmMinMs(默认 95ms)——
#         ★ min_duration 要【大】:命中条件是 D > g - m,
#           m 越大越容易命中(推导见下)。m=95 时 g-m ≈ 18ms,
#           而实测 D ∈ [20,60] ⇒ 落在窗口里。
#      ② 从站的波形周期改成 $WavePeriod(默认 8 秒)——
#         这才是"命中次数"的主杠杆:4 分钟的联动次数从 ~3 次提到 ~60 次。
#    ★ 仓库里的 scripts/soak_project.json 【一个字都不改】——
#      改的只是本轮 $OutDir 里那份副本。24h 长跑用的仍是原节奏。★
#
#  ★★★ 竞态的时序模型(★ 实测出来的,不是推的 ★★★)★★★
#    一次"越限 → 联动 → 写出去"的时间线(单位 ms):
#
#      R         一次轮询回包到达,值越过阈值 → 报警起 min_duration 计时器
#      A = R+m   计时器到 → 报警触发 → 联动 setValue + markDirty
#                ★ 从这一刻起本地值就是"待写"的值,窗口【开始】★
#      A+(g-m)   下一次轮询回包到达   ← ★ 落在这窗口里 = 撞上竞态 ★
#      A+D       下一个 100ms 写节拍 → 帧发出去   ← 窗口【结束】★
#
#    记 g = 两次轮询回包的间隔,D = 写节拍相对联动的相位。
#    命中条件(把上面两个时刻比一下就出来):
#        ★ 命中 ⟺ A+(g-m) < A+D ⟺ D > g - m ★
#
#    实测值(2026-10-26 那轮 1 分钟 smoke):
#      g ≈ 113ms(528 次发 / 60 秒;不是 100 —— 写请求跟读抢同一个在途槽)
#      D ∈ [20, 60]ms,而且【在一次运行里几乎不变】——
#      因为轮询节拍和写节拍都是 100ms 定时器,相位是【锁死】的。
#      ⇒ D 决定于 m 的取值:D = (φ - m) mod 100,φ = 两个定时器的相位差。
#
#    ★ 关键结论(我原来的推理是【反的】,被实测推翻)★
#      我原先写的是"把轮询收紧、让 min_duration 小一点,下一次回包就落进窗口"。
#      实测:min_duration=45 ⇒ 需要 D > 68,而 D 最大才 60 ⇒ 【一次都撞不上】。
#      ⇒ 不是 m 越小越好,是【m 越大越好】:m 越大,"g-m"越小,越容易命中。
#      m=99 ⇒ 需要 D > 14,而 D = (φ-99) mod 100 = φ+1 ⇒
#              只要 φ ∈ [14, 98] 就命中(约 85% 的相位),而且命中时
#              D 有几十毫秒的余量,不是擦边。
#      ★ m 必须 < g(~113ms),否则"下一次回包"落在联动【之前】,
#        那次回包进来时还没标脏,照样撞不上。
#      ★ 轮询间隔必须 ≤ 100ms:PollScheduler 的定时器硬编码 100ms
#        (PollScheduler.cpp:48),poll_interval_ms 只能让回包【更密】,
#        不能更疏 —— 更疏了 g 变大,窗口就够不着了。
#
#    ⇒ 期望命中次数 ≈ 联动次数 × 命中率。
#      波形周期 377s(默认):4 分钟只有 ~3 次联动 ⇒ 期望 ~1.3 次 ⇒ 【抽到 0 次很正常】
#      波形周期   8s(短跑):4 分钟约 ~60 次联动 ⇒ m=99 时期望命中 ~50 次
#    ★ 这就是为什么 J10(守卫命中 ≥ 1)必须是一条硬判据:
#      没撞上竞态的"全绿",跟 G-15 那次"轮询统计的正则坏了"是同一类假绿。★
#
#  用法:
#    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\short_action_run.ps1
#    powershell ... -Minutes 6
#    powershell ... -PollMs 200 -AlarmMinMs 50     # 恢复成 24h 长跑的节奏
#
#  退出码:
#    0 = 每一个写出去的值都对(且所有硬判据都过) ★通过★
#    1 = 有硬判据没过(写错值 / 少写 / 看不见证据)
#    2 = 前置检查没过(没编 / 没工程 / 端口被占)
#    3 = 被测程序自己退出了
# ============================================================================

[CmdletBinding()]
param(
    # 跑几分钟。默认 4 —— 从站第一拍就越限,几秒内就该触发;
    # 多给的时间是留给"写完之后还能再读回一次"看的。
    [int]    $Minutes    = 4,
    [int]    $SlavePort  = 1502,
    # 从站的寄存器波形推进间隔(秒)。
    # ★ 默认 1.0(原 5.0):
    #   从站的 tick 会【重新写一遍】寄存器 —— 写进去的 0 会在下一个 tick 被
    #   波形覆盖回去。这是【测试从站的性质,不是 HMI 的问题】。
    #   间隔越大,"寄存器保持 0"的窗口越长,读回证据越容易看到;
    #   但间隔太大,波形就【采样太稀】,可能整个跳过阈值区间(见 $WavePeriod)。
    [double] $SlaveTick  = 1.0,
    # ★ 从站的波形周期(秒)。默认 8。
    #   ★ 为什么要它:2026-10-26 那轮 4 分钟短跑,守卫命中【0 次】——
    #     不是因为没那个 bug,而是【一次都没撞上】。
    #     原因:从站改之前的波形周期其实是 2π×60 ≈ 377 秒
    #     (相位是 t/60,而 sin 的周期是 2π —— 详见 test_slave.py 的注释),
    #     温度一个完整的"越限→恢复"要 ~188 秒,4 分钟只够触发 ~3 次联动;
    #     而每次联动只有 ~45% 的概率撞上"待写"窗口(推导见文件头)。
    #     期望命中 ~1.3 次,抽到 0 次很正常
    #     ⇒ 那轮的"全绿"是【假绿】:它压根没验到 G-16。
    #   周期调到 8 秒 ⇒ 4 分钟约 38 次联动 ⇒ 期望命中 ~17 次,
    #     "一次都没撞上"的概率极低 —— 这时候的绿才有意义。
    #   ★ 24h 长跑(run_soak.ps1)【不传这个参数】,从站默认仍是
    #     2π×60 ≈ 377 秒,波形行为跟改动之前逐位一致。★
    [double] $WavePeriod = 8,
    # ★ 复制出去的那份工程里,变量的轮询间隔(ms)。
    #   默认 50 = 收紧后的节奏,目的是让 G-16 那个竞态的窗口真的能被撞上。
    #   传 200 就恢复成 24h 长跑工程的原值(但那样 G-16 在几分钟内撞不上)。
    [int]    $PollMs     = 50,
    # ★ 复制出去的那份工程里,报警的 min_duration_ms。
    #   默认 95(★ 必须【大于】80 且【小于】100 ★)——
    #   原因见文件头"竞态的时序模型"那一段:
    #   守卫要命中,必须让"下一次轮询回包"落在"已标脏、还没写出"的窗口里。
    #   而这个窗口的宽度 = 100ms 写节拍的相位 D,
    #   命中条件是 r < φ < r + min_duration(φ = 写节拍与轮询节拍的相位差)。
    #   min_duration 越大,这个命中窗口越宽 ⇒ 命中率越高。
    #   ★ 上限 100:再大就跨过下一拍回包了,那次回包【先于】联动到达,
    #     不在"已标脏"的窗口里,同样撞不上。★
    [int]    $AlarmMinMs = 95,
    # ★ 从站的回包延迟(ms)。0 = 立即回(默认)。
    #   ★ 为什么要它:BUGFIX-E1 修的【写排队】那条通路,走它的前提是
    #     【槽被读占着】。从站立即回包 ⇒ 槽几乎永远空着 ⇒ 写永远撞不上
    #     ⇒ 那条通路【一次都不会被走到】。
    #     实测(2026-10-27):相位扫描 8 轮,"写请求【排队】"计数【全是 0】。
    #   ⇒ 加了延迟就能【故意】制造撞车,把排队通路真的走一遍。
    #   ⚠ 加了延迟读也会变慢 ⇒ 轮询节奏跟着变,数字不能跟默认跑法混着比。
    [int]    $ReplyDelayMs = 0,
    [string] $Exe        = '',
    [string] $Project    = '',
    [string] $OutDir     = '',
    [string] $SoakUser   = '3',
    [string] $SoakPass   = '333333'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = (Resolve-Path (Join-Path $here '..')).Path

function Say($msg, $color = 'Gray') {
    Write-Host ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $msg) -ForegroundColor $color
}

# ---- 默认值 -------------------------------------------------------------
if (-not $Exe)     { $Exe     = Join-Path $repo 'hmi\out\build\debug2\hmi.exe' }
if (-not $Project) { $Project = Join-Path $here  'soak_project.json' }
if (-not $OutDir)  { $OutDir  = Join-Path $repo ("soaks\short_{0}" -f (Get-Date -Format 'yyyyMMdd_HHmmss')) }

$slaveScript = Join-Path $here 'test_slave.py'

# ---- 前置检查:一条一条说清楚,不猜 -------------------------------------
$problems = @()
if (-not (Test-Path $Exe))            { $problems += "找不到 exe: $Exe(要先编)" }
if (-not (Test-Path $Project))        { $problems += "找不到工程文件: $Project" }
if (-not (Test-Path $slaveScript))    { $problems += "找不到从站脚本: $slaveScript" }
if (-not (Get-Command python -ErrorAction SilentlyContinue)) { $problems += "PATH 里没有 python" }
if ($AlarmMinMs -ge 100) {
    $problems += ("AlarmMinMs({0}) 必须【小于】100 —— 100ms 是写节拍的周期," +
                  "再大就跨过下一拍回包了,那次回包在联动【之前】到,同样撞不上") -f $AlarmMinMs
}
if ($AlarmMinMs -le 50) {
    # ★ 不是硬错,但要提醒:m 太小 ⇒ 命中条件 D > g-m 很难满足。
    #   g ≈ 113ms,m=45 ⇒ 需要 D > 68,而实测 D ≤ 60 ⇒ 【一次都撞不上】。
    #   (2026-10-26 实测:min_duration=45 时,19 次联动、守卫命中 0 次。)
    $problems += ("AlarmMinMs({0}) 太小 —— 命中条件是 D > g-m(g≈113ms,实测 D≤60)," +
                  "m≤50 时几乎撞不上竞态(实测 m=45 命中 0 次)。建议 90~99。") -f $AlarmMinMs
}
if ($problems.Count -gt 0) {
    Say "开跑前置检查没过:" 'Red'
    $problems | ForEach-Object { Say "  · $_" 'Red' }
    exit 2
}

# 端口占用:从站起不来就是"读不到证据",不是"联动没通" —— 这两种必须分开。
$portBusy = $false
try {
    $c = New-Object System.Net.Sockets.TcpClient
    $c.Connect('127.0.0.1', $SlavePort)
    $c.Close()
    $portBusy = $true
} catch { }
if ($portBusy) {
    Say ("端口 {0} 已经有人在应答 —— 换一个(-SlavePort),或者先把它停掉。" -f $SlavePort) 'Red'
    exit 2
}
# ★ 端口【能不能绑】也要先查 ★(2026-10-26 踩到)
#   上面那个探测只查"有没有人在应答",查不出"能不能 bind"。
#   上一轮跑完、从站被杀之后,Windows 会留一段时间的 TIME_WAIT,
#   这期间 bind 同一个端口会报:
#       PermissionError: [WinError 10013] 以一种访问权限不允许的方式做了一个访问套接字的尝试
#   ★ 而这个报错长得跟"端口被别的程序占了"完全不一样 ——
#     现场看到的是"从站没起来",脚本说"端口没人应答" ⇒ 查不到原因。
#   所以这里先自己 bind 一次试试,绑不上就把原因说清楚。
$bindProbe = $null
try {
    $bindProbe = New-Object System.Net.Sockets.Socket([System.Net.Sockets.AddressFamily]::InterNetwork,
                                                     [System.Net.Sockets.SocketType]::Stream,
                                                     [System.Net.Sockets.ProtocolType]::Tcp)
    $bindProbe.Bind((New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Loopback, $SlavePort)))
    $bindProbe.Listen(1)
} catch {
    $why = $_.Exception.Message
    if ($_.Exception.InnerException) { $why = $_.Exception.InnerException.Message }
    Say ("端口 {0} 绑不上:{1}" -f $SlavePort, $why) 'Red'
    Say "  常见原因:上一轮的从站刚被杀,Windows 还留着 TIME_WAIT(等一两分钟就好);" 'Red'
    Say "  或者这个端口落在 Windows 的保留区间里(netsh interface ipv4 show excludedportrange protocol=tcp)。" 'Red'
    exit 2
} finally {
    if ($bindProbe) { $bindProbe.Close() }
}

# ---- 输出目录 + 私有数据目录 -------------------------------------------
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$slaveLog = Join-Path $OutDir 'slave.log'

# 私有数据目录(同 run_soak.ps1 的做法):
#   HMI_DATA_DIR 指到本轮自己的 <OutDir>\appdata\QianWei\HMI
$dataDir = Join-Path $OutDir 'appdata\QianWei\HMI'
New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
$dataDir = (Resolve-Path $dataDir).Path

# users.json 要复制过去:--soak 需要一个【已存在】的用户(它不是免认证开关)。
# 复制一份(不改原文件),用的还是同一个账号、同一个密码。
$realUsers = Join-Path (Join-Path $env:LOCALAPPDATA 'QianWei\HMI') 'users.json'
if (-not (Test-Path $realUsers)) {
    Say ("找不到用户库: {0}" -f $realUsers) 'Red'
    Say "  --soak 需要一个已存在的用户(它不是免认证开关),请先建好用户再跑。" 'Red'
    exit 2
}
Copy-Item $realUsers (Join-Path $dataDir 'users.json') -Force

# ============================================================================
#  工程副本:改节奏 + 从它读出【期望值】
# ============================================================================
#  ★ 为什么期望值要从工程里读,而不是写死在脚本里 ★
#    判据跟被测对象配成一样的,那它就只会"验证自己"。
#    写死 0 的话:哪天工程里的 write_value 改成 1,判据还在按 0 判,
#    于是每一次写都判红 —— 判据自己坏了,不是程序坏了。
#    从工程读:判据永远跟着被测对象走,而且"写出去的数跟配的不一样"必被抓出来。
$projectCopy = Join-Path $OutDir 'soak_project.json'
Copy-Item $Project $projectCopy -Force

# 改节奏:只改这份【副本】,仓库里的那份一个字都不动。
try {
    $proj = Get-Content $projectCopy -Encoding UTF8 -Raw | ConvertFrom-Json
} catch {
    Say ("工程文件解析失败: {0}" -f $_.Exception.Message) 'Red'
    exit 2
}
foreach ($v in $proj.variables) { $v.poll_interval_ms = $PollMs }
foreach ($a in $proj.alarms)    { $a.min_duration_ms  = $AlarmMinMs }
# ★ 端口也必须改 ★(2026-10-26 踩到)
#   之前 -SlavePort 只用来【起从站】,工程副本里的 communications.port 纹丝不动。
#   端口一致时看起来一切正常(所以一直没人发现),一旦不一致:
#     从站在 1503 起来了,应用却照着工程里的 1502 去连 ⇒
#     那边什么都没有 ⇒ 从站日志 0 行、轮询统计"发出= 0"、
#     判据 J1/J4/J6/J7/J10 全红,而现场看着像"联动彻底坏了"。
#   ★ 症状跟"程序坏了"一模一样,而原因在脚手架上 —— 跟 2026-10-25 那次
#     "Out-File 写 BOM 导致工程加载失败"是同一类:判据自己坏了。★
$proj.communications.port = $SlavePort

# ★ 写文件必须【不带 BOM】★
#   PowerShell 5.1 的 Out-File -Encoding UTF8 会写 BOM,
#   而 QJsonDocument::fromJson 遇到 UTF-8 BOM 会直接报"非法值"——
#   工程加载失败 → 程序退出码 2 → 看起来像"联动没通",其实是文件坏了。
#   (Windows PowerShell 5.1 的 Out-File 默认就是 UTF-8+BOM,这条是踩过的。)
$jsonText = ($proj | ConvertTo-Json -Depth 20)
[System.IO.File]::WriteAllText($projectCopy, $jsonText,
                              (New-Object System.Text.UTF8Encoding($false)))

# 自检:这份副本必须【能被 JSON 解析器读】,而且【没有 BOM】。
#   不自检的话,补丁写坏了 → 工程加载失败 → 判据全红,
#   而现场看到的是"联动坏了" —— 判据自己坏了的样子跟程序坏了的样子一模一样。
$head3 = [System.IO.File]::ReadAllBytes($projectCopy)[0..2]
if ($head3[0] -eq 0xEF -and $head3[1] -eq 0xBB -and $head3[2] -eq 0xBF) {
    Say "工程副本带了 UTF-8 BOM —— 应用会拒绝加载(退出码 2)。这属于脚本自己的 bug。" 'Red'
    exit 2
}
$jsonProbe = & python -c "import json,sys; json.load(open(sys.argv[1],encoding='utf-8')); print('OK')" $projectCopy
if ($LASTEXITCODE -ne 0 -or $jsonProbe -ne 'OK') {
    Say ("工程副本自检失败(python 解析不了): {0}" -f $jsonProbe) 'Red'
    exit 2
}
# ★ 自检 2:副本里的端口必须跟从站实际监听的端口一致 ★
#   (见上面改端口那段注释:不一致时症状像"联动彻底坏了",实际是脚手架坏了。)
if ([int]$proj.communications.port -ne $SlavePort) {
    Say ("工程副本端口 {0} != 从站端口 {1} —— 应用会去连一个没人监听的端口。" -f
         $proj.communications.port, $SlavePort) 'Red'
    exit 2
}

# ---- 从工程里导出【期望值】:地址 → 变量名 → 该变量被要求写入的值集合 ----
$expectByAddr = @{}      # 地址(int) → @{ Name = 'Speed'; Values = @(0) }
$targetVar    = $null
foreach ($v in $proj.variables) {
    $addr = [int]$v.address
    $expectByAddr[$addr] = @{ Name = [string]$v.name; Values = @(); Access = [string]$v.access }
    if ([string]$v.access -eq 'ReadWrite') { $targetVar = [string]$v.name }
}
foreach ($a in $proj.actions) {
    $vn = [string]$a.variable
    foreach ($v in $proj.variables) {
        if ([string]$v.name -ne $vn) { continue }
        $addr = [int]$v.address
        $val  = [string]$a.write_value
        if ($expectByAddr.ContainsKey($addr)) {
            $expectByAddr[$addr].Values += $val
        }
    }
}
if ($null -eq $targetVar) {
    Say "工程里没有一个 ReadWrite 变量 —— 联动没有可写的靶子。" 'Red'
    exit 2
}

# ============================================================================
#  日志配置:把 hmi.write / hmi.poll / hmi.comm 调到 DEBUG
# ============================================================================
#  ★ 这个文件叫 hmi.json,放在【私有数据目录】里,不是工程文件 ★
#    loadConfig() 读的是 appDataDir()/hmi.json(Logging.cpp:217),
#    appDataDir() 认 HMI_DATA_DIR —— 也就是本轮的私有目录。
#    往工程文件里加 "logging" 段【一点用都没有】(工程文件里没有这个键)。
#
#  ★ 2026-10-27:加上 hmi.comm ★
#    写排队的关键诊断在 hmi.comm 里(Logging.cpp:36 定义 "hmi.comm"):
#      · "写请求【排队】(队列长度=...)"  —— 写【在等】,不是【被拒】
#      · "写队列已满(N),拒收本次写请求" —— 队列撑爆了(这才是真正的拒收)
#      · "排队写已发出 FC=0x06"          —— 排队的写【真的出去了】
#    之前只开了 write/poll,comm 停在默认 INFO ⇒ 这三条 DEBUG 一条都落不了地,
#    而判据"看不见证据"的时候长得跟"没问题"一模一样。
$logCfgPath = Join-Path $dataDir 'hmi.json'
$logCfg = @'
{
  "logging": {
    "default_level": "info",
    "categories": {
      "hmi.write": "debug",
      "hmi.poll":  "debug",
      "hmi.comm":  "debug"
    }
  }
}
'@
[System.IO.File]::WriteAllText($logCfgPath, $logCfg,
                              (New-Object System.Text.UTF8Encoding($false)))

# 独立单实例键:机器上可能还有别的 hmi.exe 在跑,两把锁互不干涉。
# ★ 同一个键的第二个实例照样被挡(tst_SingleInstanceKey 钉着这条)——
#   这不是"取消单实例",只是"这一轮用另一把锁"。
$instanceKey = "hmi-actioncheck-{0}" -f (Get-Date -Format 'yyyyMMdd-HHmmss')

Say "========== 联动写通路 · 短跑实测 ==========" 'Cyan'
Say ("输出目录   : {0}" -f $OutDir)
Say ("HMI 可执行 : {0}" -f $Exe)
Say ("工程文件   : {0}(副本,节奏已改)" -f $projectCopy)
Say ("  变量轮询  : {0} ms(原 200)" -f $PollMs)
Say ("  报警最短  : {0} ms(原 50)" -f $AlarmMinMs)
Say ("  联动靶子  : {0} → 地址 {1},期望写入值 {2}" -f $targetVar,
     (($expectByAddr.GetEnumerator() | Where-Object { $_.Value.Name -eq $targetVar } |
        ForEach-Object { $_.Key }) -join ','),
     (($expectByAddr.GetEnumerator() | Where-Object { $_.Value.Name -eq $targetVar } |
        ForEach-Object { $_.Value.Values -join '/' }) -join ','))
Say ("从站       : python test_slave.py --port {0} --tick {1} --wave-period {2}" -f $SlavePort, $SlaveTick, $WavePeriod)
Say ("日志级别   : hmi.write=debug, hmi.poll=debug({0})" -f $logCfgPath)
Say ("实例键     : {0}(跟其它实例互不干扰)" -f $instanceKey)
Say ("私有数据目录: {0}" -f $dataDir)
Say ("计划时长   : {0} 分钟" -f $Minutes)

# ---- 起从站 ------------------------------------------------------------
Say "启动自建从站 ..." 'Cyan'
# --duration 给得比计划长一点:收摊时先停 HMI,从站自己再退。
$slaveDuration = ($Minutes * 60) + 60
$slave = Start-Process -FilePath 'python' `
    -ArgumentList @($slaveScript, '--port', "$SlavePort", '--tick', "$SlaveTick",
                    '--wave-period', "$WavePeriod",
                    '--reply-delay-ms', "$ReplyDelayMs",
                    '--duration', "$slaveDuration", '--log', $slaveLog) `
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
    Say ("从站没起来(端口 {0} 10 秒没人应答) —— 停。" -f $SlavePort) 'Red'
    if (-not $slave.HasExited) { Stop-Process -Id $slave.Id -Force -ErrorAction SilentlyContinue }
    exit 2
}
Say ("从站已就绪(PID={0}),端口 {1}" -f $slave.Id, $SlavePort) 'Green'

# ---- 起 HMI ------------------------------------------------------------
# 显式设置,不留隐式状态:报告里说"数据目录在哪",就必须真的在那。
$origDataDir = $env:HMI_DATA_DIR
$origKey     = $env:HMI_INSTANCE_KEY
$env:HMI_DATA_DIR    = $dataDir
$env:HMI_INSTANCE_KEY = $instanceKey

Say "启动 HMI(带 --soak 自动登录 + --project 指定工程)..." 'Cyan'
$appErr = Join-Path $OutDir 'hmi.stderr.txt'
$hmi = Start-Process -FilePath $Exe `
    -ArgumentList @('--soak', '--soak-user', $SoakUser, '--soak-pass', $SoakPass,
                    '--project', $projectCopy) `
    -PassThru -RedirectStandardError $appErr

$appExited = $false
$appExitCode = $null
try {
    # 等一小会儿:5 秒内退出的多半是"工程加载失败"(退出码 2)或认证失败。
    Start-Sleep -Seconds 5
    if ($hmi.HasExited) {
        $appExited = $true
        try { $appExitCode = $hmi.ExitCode } catch { $appExitCode = $null }
        Say ("★ HMI 启动后 5 秒就退出了(exit={0})—— 脚手架没跑起来。" -f $appExitCode) 'Red'
        Say "  常见原因:① --project 工程加载失败(退出码 2);② 用户名/密码不对;" 'Red'
        Say "            ③ 还有别的 hmi.exe 占着同一把单实例锁。" 'Red'
        Say ("  ★ 工程副本: {0}" -f $projectCopy) 'Red'
    } else {
        Say ("HMI 已启动(PID={0})" -f $hmi.Id) 'Green'

        # ---- 等它跑完这几分钟 ------------------------------------------
        $deadline = (Get-Date).AddMinutes($Minutes)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Seconds 10
            if ($hmi.HasExited) {
                $appExited = $true
                try { $appExitCode = $hmi.ExitCode } catch { $appExitCode = $null }
                Say ("★ HMI 在中途退出了(exit={0})" -f $appExitCode) 'Red'
                break
            }
        }
    }
}
finally {
    # ---- 收尾:只结束【本脚本自己启动的】那两个进程 ----------------------
    #   ★ 绝不碰别的 hmi.exe —— 那是用户自己的进程,不是实验品。
    if ($hmi -and -not $hmi.HasExited) {
        Say "关掉本轮启动的 HMI ..." 'Cyan'
        try { $hmi.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2 } catch { }
        if (-not $hmi.HasExited) { Stop-Process -Id $hmi.Id -Force -ErrorAction SilentlyContinue }
    }
    if ($slave -and -not $slave.HasExited) {
        Say "关掉本轮启动的从站 ..." 'Cyan'
        Stop-Process -Id $slave.Id -Force -ErrorAction SilentlyContinue
    }
    $env:HMI_DATA_DIR     = $origDataDir
    $env:HMI_INSTANCE_KEY = $origKey
}

# ================================================================
#  证据提取
# ================================================================
Say ""
Say "========== 证据 ==========" 'Cyan'

$report = New-Object System.Collections.Generic.List[string]
function Rep($s) { $script:report.Add($s); Write-Host $s }

# ---- 证据 1:从站日志里的写帧(★ 唯一不能被应用自己骗的证据 ★)----------
$allSlave = @()
if (Test-Path $slaveLog) {
    $allSlave = @(Get-Content $slaveLog -Encoding UTF8)
}
# "FC6 写 addr=2 value=0" —— value 是从站从网线上收到的那个数
$reFc6 = 'FC6 写 addr=(\d+) value=(\d+)'
$fc6Writes = @()      # 每项:@{ Addr=int; Value=int; Line=string }
$fc6BadParse = @()
foreach ($l in $allSlave) {
    if ($l -notmatch 'FC6 写') { continue }
    if ($l -match $reFc6) {
        $fc6Writes += @{ Addr = [int]$matches[1]; Value = [int]$matches[2]; Line = $l }
    } else {
        $fc6BadParse += $l
    }
}
# FC16 写:从站【不】打印值(test_slave.py:242 只打 addr/count)。
#   本工程写的是 UInt16 → 走 FC6,所以 FC16 出现就是异常,单独报出来。
$fc16Lines = @($allSlave | Where-Object { $_ -match 'FC16 写' })

Rep ("从站日志    : {0} 行" -f $allSlave.Count)
Rep ("FC6 写帧    : {0}" -f $fc6Writes.Count)
Rep ("FC16 写帧   : {0}" -f $fc16Lines.Count)
if ($fc6BadParse.Count -gt 0) {
    Rep ("★ 有 {0} 行 FC6 写帧解析不出 value —— 判据看不见证据,不能算过 ★" -f $fc6BadParse.Count)
    foreach ($l in ($fc6BadParse | Select-Object -First 3)) { Rep ("    " + $l) }
}
foreach ($l in ($fc6Writes | Select-Object -First 8)) { Rep ("    " + $l.Line) }

# ---- 证据 2:写完之后,FC3 读回的第 3 个寄存器(Speed@2)是不是 0 --------
# 从站的读是批量的:FC3 读 addr=0 count=3 → [Pressure, Temperature, Speed]
$reFc3 = 'FC3 读 addr=(\d+) count=(\d+) → \[([\d,\s]*)\]'
$readbackZero = @()
if ($allSlave.Count -gt 0) {
    foreach ($l in $allSlave) {
        if ($l -notmatch $reFc3) { continue }
        $addr = [int]$matches[1]
        $vals = @($matches[3] -split ',' | ForEach-Object { [int]$_.Trim() })
        if ($addr -eq 0 -and $vals.Count -ge 3 -and $vals[2] -eq 0) {
            $readbackZero += $l
        }
    }
}
Rep ("读回 Speed=0 的 FC3 行 : {0}" -f $readbackZero.Count)
foreach ($l in ($readbackZero | Select-Object -First 3)) { Rep ("    " + $l) }

# ---- 证据 3:应用日志里的 [Action] / [Write] / 守卫命中的行 -------------
$logDir = Join-Path $dataDir 'logs'
$actionLines = @()
$sendLines   = @()   # "[WriteScheduler] 发写:..."
$okLines     = @()   # "[Write] OK:Speed=0(plc=plc1)"
$guardLines  = @()   # "[PollScheduler] 变量Speed有待写的值,跳过本次读回覆盖"
$pollStatLines = @()
# ★★ 写被【拒收】的行 —— 2026-10-27 新增,理由见下面 J11 ★★
#   "driver 拒收了" = WriteScheduler 说 driver 没收下这个写。
#   ★ 修好之后这条【应该】是 0 —— 写被挡住了会【排队】,不是被拒。
$rejectLines  = @()
# ★★ 写【排队】的行(J11 的另一半)★★
$queuedLines  = @()
$appLogFiles = @(Get-ChildItem $logDir -Filter '*.log' -File -ErrorAction SilentlyContinue |
                  Sort-Object LastWriteTime)
foreach ($f in $appLogFiles) {
    try {
        $fs = [System.IO.File]::Open($f.FullName, 'Open', 'Read', 'ReadWrite')
        $sr = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8)
        while (-not $sr.EndOfStream) {
            $line = $sr.ReadLine()
            if ($line -match '\[Action\]')        { $actionLines  += $line }
            if ($line -match '\[WriteScheduler\] 发写') { $sendLines += $line }
            if ($line -match '\[Write\] OK:')    { $okLines    += $line }
            if ($line -match '跳过本次读回覆盖')     { $guardLines += $line }
            if ($line -match '轮询统计')            { $pollStatLines += $line }
            if ($line -match '的 driver 拒收了')     { $rejectLines += $line }
            if ($line -match '写请求【排队】')        { $queuedLines += $line }
        }
        $sr.Close(); $fs.Close()
    } catch { }
}
Rep ("应用日志文件: {0} 个" -f $appLogFiles.Count)
Rep ("[Action] 行 : {0}" -f $actionLines.Count)
foreach ($l in ($actionLines | Select-Object -First 6)) { Rep ("    " + $l) }
Rep ("[Write] 发写行 : {0}" -f $sendLines.Count)
foreach ($l in ($sendLines | Select-Object -First 4)) { Rep ("    " + $l) }
Rep ("[Write] OK 行  : {0}" -f $okLines.Count)
foreach ($l in ($okLines | Select-Object -First 4)) { Rep ("    " + $l) }
Rep ("★ 写被【拒收】行: {0}" -f $rejectLines.Count)
foreach ($l in ($rejectLines | Select-Object -First 3)) { Rep ("    " + $l) }
Rep ("★ 写【排队】行  : {0}" -f $queuedLines.Count)
foreach ($l in ($queuedLines | Select-Object -First 3)) { Rep ("    " + $l) }
Rep ("★ 守卫命中行(有值待写时跳过读回覆盖): {0}" -f $guardLines.Count)
foreach ($l in ($guardLines | Select-Object -First 3)) { Rep ("    " + $l) }
Rep ("轮询统计行  : {0}(最后一条: {1})" -f $pollStatLines.Count,
     $(if ($pollStatLines.Count -gt 0) { $pollStatLines[-1] } else { '(无)' }))

# ================================================================
#  判据
# ================================================================
Say ""
Say "========== 判定 ==========" 'Cyan'

$fail = New-Object System.Collections.Generic.List[string]
function Judge($ok, $name, $detail) {
    $mark = '✓'
    $col  = 'Green'
    if (-not $ok) { $script:fail.Add("$name —— $detail"); $mark = '✗'; $col = 'Red' }
    $line = "  {0} {1}: {2}" -f $mark, $name, $detail
    $script:report.Add($line)
    Write-Host $line -ForegroundColor $col
}

# ---- 期望值表:地址 → 该地址被要求写入的值集合 --------------------------
function ExpectedValuesFor($addr) {
    if (-not $script:expectByAddr.ContainsKey($addr)) { return $null }
    return $script:expectByAddr[$addr].Values
}
# ★ 把哈希表挂到脚本作用域,让上面的函数能看见(PS 5.1 的函数看不到
#   调用者的局部变量,只看得见脚本作用域的)。
$script:expectByAddr = $expectByAddr

# ---- J1:有写帧 ---------------------------------------------------------
Judge ($fc6Writes.Count -gt 0) "J1 有写帧" `
       ("FC6 写帧 {0} 条" -f $fc6Writes.Count)

# ---- J2/J3:每一个写帧的【地址】和【值】都必须是期望的那个 ---------------
# ★ 这是本次新增的核心判据 ★
$badAddr = @()
$badVal  = @()
$unknownAddr = @()
foreach ($w in $fc6Writes) {
    $exp = ExpectedValuesFor $w.Addr
    if ($null -eq $exp) {
        $unknownAddr += $w.Line
        continue
    }
    if ($exp.Count -eq 0) {
        $badAddr += $w.Line
        continue
    }
    if (-not ($exp -contains [string]$w.Value)) {
        $badVal += $w.Line
    }
}
Judge ($unknownAddr.Count -eq 0) "J2 写的地址都对" `
       ("写到【期望之外】的地址的写帧 {0} 条" -f $unknownAddr.Count)
foreach ($l in ($unknownAddr | Select-Object -First 3)) { Rep ("      " + $l) }
Judge ($badVal.Count -eq 0) "J3 ★写出去的值都对★" `
       ("值不等于期望值的写帧 {0} / {1} 条" -f $badVal.Count, $fc6Writes.Count)
foreach ($l in ($badVal | Select-Object -First 5)) { Rep ("      " + $l) }

# ---- J4:[Action] 行里的值必须是对的那个 --------------------------------
# 格式:[Action] "温度高" → "Speed" = "0"
$reAction = '\[Action\].*=\s*"([^"]*)"\s*$'
$actionBad = @()
$actionVals = @()
foreach ($l in $actionLines) {
    if ($l -notmatch $reAction) { $actionBad += ("(解析不出值) " + $l); continue }
    $v = $matches[1]
    $actionVals += $v
    $exp = @()
    foreach ($a in $proj.actions) { $exp += [string]$a.write_value }
    if (-not ($exp -contains $v)) { $actionBad += $l }
}
Judge ($actionLines.Count -gt 0) "J4 联动确实执行了" `
       ("[Action] 行 {0} 条" -f $actionLines.Count)
Judge ($actionBad.Count -eq 0) "J4b [Action] 行的值都对" `
       ("值不对的 [Action] 行 {0} 条(出现过的值: {1})" -f $actionBad.Count,
        $(if ($actionVals.Count -gt 0) { ($actionVals | Select-Object -Unique) -join ',' } else { '(无)' }))
foreach ($l in ($actionBad | Select-Object -First 4)) { Rep ("      " + $l) }

# ---- J5:[WriteScheduler] 发写行里的值必须是对的那个 --------------------
# 格式:[WriteScheduler] 发写: Speed plc=plc1 slaveId=1 addr=0x2 value=0 reqId=1
#   ★ 这一行打的是【编进帧里】的值(取自 buildWritePayload 之后)——
#     它是"发出去的那个数"最贴近源头的一行。
#
# ★★ 空格:QDebug 会在【相邻两项之间插一个空格】★
#   代码里写的是 << "addr=0x" << QString::number(...) << "value=" << sentValue,
#   而 QDebug 把每个操作数都当成独立的一段,段与段之间【自动插空格】。
#   于是日志里落的是:  addr=0x "2" value= "0" reqId= 2
#   —— 不是 addr=0x2 value=0。
#   ★ 这正是 G-15 那次栽过的坑(轮询统计行"发出= 271"顶掉了 harness 的正则)★
#     判据的正则必须按【日志里真实的样子】写,不能按【代码里想的样子】写。
#   所以下面两处都用了 \s* 和可选引号,两种写法都能吃。
$reSend = 'addr=0x\s*"?([0-9A-Fa-f]+)"?\s+value=\s*"?([^"\s]+)"?'
$sendBad = @()
$sendVals = @()
$sendMissing = ($fc6Writes.Count -gt 0 -and $sendLines.Count -eq 0)
foreach ($l in $sendLines) {
    if ($l -notmatch $reSend) { $sendBad += ("(解析不出 value) " + $l); continue }
    $v = $matches[2]
    $sendVals += $v
    $exp = @()
    foreach ($a in $proj.actions) { $exp += [string]$a.write_value }
    if (-not ($exp -contains $v)) { $sendBad += $l }
}
# ★ 写帧有、但"发写"行一条都没有 = 【看不见证据】——
#   那多半是 hmi.json 里的日志级别没生效(或者根本没发写)。
#   "看不见"不等于"对",这种红是必须的:否则就是假绿。
Judge (-not $sendMissing) "J5 [Write] 发写行看得见" `
       ("发写行 {0} 条(写帧 {1} 条)" -f $sendLines.Count, $fc6Writes.Count)
Judge ($sendBad.Count -eq 0) "J5b [Write] 发写行的值都对" `
       ("值不对的发写行 {0} 条(出现过的值: {1})" -f $sendBad.Count,
        $(if ($sendVals.Count -gt 0) { ($sendVals | Select-Object -Unique) -join ',' } else { '(无)' }))
foreach ($l in ($sendBad | Select-Object -First 4)) { Rep ("      " + $l) }

# ---- J6:[Write] OK 行里的值必须是对的那个 ------------------------------
# 格式:[Write] OK: Speed=0(plc=plc1)
# ★ 这一行打的就是审计条目里 newValue 那个变量(sentValue),
#   所以它是"审计记的值"的代理证据(审计条目本身只进界面表格,从不落文件)。
#
# ★ 同样要吃 QDebug 的空格:日志里落的是  [Write] OK: "Speed" = "0" (plc= "plc1" )
$reOk = '\[Write\]\s+OK:.*?=\s*"?([^"\s]+)"?\s*\('
$okBad = @()
$okVals = @()
foreach ($l in $okLines) {
    if ($l -notmatch $reOk) { $okBad += ("(解析不出 value) " + $l); continue }
    $v = $matches[1]
    $okVals += $v
    $exp = @()
    foreach ($a in $proj.actions) { $exp += [string]$a.write_value }
    if (-not ($exp -contains $v)) { $okBad += $l }
}
Judge ($okLines.Count -gt 0) "J6 [Write] OK 行看得见" `
       ("OK 行 {0} 条" -f $okLines.Count)
Judge ($okBad.Count -eq 0) "J6b [Write] OK 行的值都对(=审计记的值)" `
       ("值不对的 OK 行 {0} 条(出现过的值: {1})" -f $okBad.Count,
        $(if ($okVals.Count -gt 0) { ($okVals | Select-Object -Unique) -join ',' } else { '(无)' }))
foreach ($l in ($okBad | Select-Object -First 4)) { Rep ("      " + $l) }

# ---- J7:读回 Speed=0 ---------------------------------------------------
Judge ($readbackZero.Count -gt 0) "J7 读回 Speed=0" `
       ("读回为 0 的 FC3 行 {0} 条" -f $readbackZero.Count)

# ---- J8:写次数 ≥ 联动次数 ----------------------------------------------
# ★ 容忍 1 次差额:收摊时正好有一次联动还没来得及发出去(程序被关了)。
#   差超过 1 就是"联动执行了但没写出去"—— 2026-10-21 那个症状。
$writeCount = $fc6Writes.Count
$actionCount = $actionLines.Count
Judge ($writeCount -ge ($actionCount - 1)) "J8 写次数跟得上联动次数" `
       ("联动 {0} 次 / 写 {1} 次" -f $actionCount, $writeCount)

# ---- 附加:FC16 不该出现(本工程写 UInt16 → 走 FC6) --------------------
Judge ($fc16Lines.Count -eq 0) "J9 没有 FC16 写" `
       ("FC16 写帧 {0} 条(本工程写 UInt16,应当走 FC6)" -f $fc16Lines.Count)
foreach ($l in ($fc16Lines | Select-Object -First 3)) { Rep ("      " + $l) }

# ---- J10:★这一轮【真的撞上竞态】了吗 ★---------------------------------
# ★ 这条判据是本脚本最重要的一条,原因见文件头"竞态的时序模型"★
#   G-16 是个竞态:只有当"轮询回包"正好落在"已标脏、还没写出"的窗口里,
#   那个 bug 才有机会发作。
#   ⇒ 如果这一轮【一次都没撞上】,那么 J1~J9 全绿【什么也证明不了】:
#     它跟 2026-10-25 那轮一样,是"没撞上所以没出错",
#     不是"撞上了也没出错"。
#   ★ 这跟 G-15 那次是同一类假绿:判据量的是【有没有动作】,
#     不是【动作对不对】。所以没撞上,不许判绿。★
#   期望命中 ≈ 联动次数 × 45%(实测的碰撞概率),所以:
#     联动 < 5 次  → 期望 < 2.3 次 → 一次没撞上很正常 → 判红(要求重跑)
#     联动 ≥ 5 次  → 期望 ≥ 2.3 次 → 一次没撞上的概率 ~7%,仍要求重跑
#   (这里不设"越多越好"的上限:命中越多,验得越充分。)
$minExpectedHits = 1
Judge ($guardLines.Count -ge $minExpectedHits) "J10 ★这一轮真的撞上竞态了★" `
       ("守卫命中 {0} 次(联动 {1} 次,期望 ≥ {2} 次)" -f $guardLines.Count, $actionCount, $minExpectedHits)
if ($guardLines.Count -lt $minExpectedHits) {
    Rep ("      ★ 没撞上竞态 —— 本轮的 J1~J9 全绿【什么也证明不了】。" +
         "波形周期是不是太大?($WavePeriod 秒)") "Red"
    Rep ("      ★ 这正是 G-15 那类假绿:量的是【有没有动作】,不是【动作对不对】。"
    )
}

# ---- J11:★★ 写【没有被拒收】,而且【排队的写真的出去了】★★ ----------
# ★★★ 这条判据是 2026-10-27 现场级负向对照【逼出来的】★★★ #
#
#   【它抓到了什么】
#     负向对照把 BUGFIX-E1 拆回"直接拒收"(= 修之前的原样)之后,
#     那一轮:拒收 12 次、发写 51 次、FC6 写帧 39 条、值全对
#     ⇒ J1~J10 【全绿】!!! 判据【没牙】!!!
#     ⇒ 为什么?因为【重试机制把问题盖住了】:
#       写被拒 → dirty 保留 → 下一拍重发 → 这次槽空着 → 发出去了 → 值落地。
#     ⇒ 也就是说:J1/J3/J8 量的是【最终结果】,
#       而 BUGFIX-E1 修的是【路径】—— 结果对了,路径是错的。
#     ⇒ ★ 这跟 G-15/G-16 同一个形状:量的是【有没有动作】/【结果对不对】,
#       不是【动作走的是不是那条路】。★
#
#   【为什么"拒收 > 0"必须判红,而不是"看最终有没有落地"】
#     拒收意味着:有一拍,这个写【没发出去】。
#     在多 PLC 工程里,那台 PLC 那一拍就【真的没收到停机指令】。
#     而"下一拍补上"在工业现场是不成立的:
#       · 设备可能已经进入下一个状态;
#       · 写的是"停机"这种一次性动作,补发的是另一个时刻的语义;
#       · 修好之后(BUGFIX-E1)写【排队】而不是【被拒】——
#         排队的写最后【一定会出去】,不存在"这一拍丢了"的情况。
#     ⇒ 所以"拒收"本身就是缺陷,不管后面有没有补上。
#
#   【另一半:排队的写必须真的出去了】
#     修好之后,写被挡住时【排队】(不是拒收)⇒ 队列里的写最后必须发出去。
#     ⇒ 如果"排队 > 0"但"排队写已发出"对不上,那就是【排队的写丢了】
#       —— 那比拒收更严重(调用方会永远等一个不来的 writeFinished)。
Judge ($rejectLines.Count -eq 0) "J11 ★写没有被拒收过★" `
       ("写被 driver 拒收 {0} 次(修好之后写应该【排队】,不是被拒)" -f $rejectLines.Count)
foreach ($l in ($rejectLines | Select-Object -First 3)) { Rep ("      " + $l) }
if ($rejectLines.Count -gt 0) {
    Rep ("      ★ 【值最后落地了】也掩盖不了这个问题 —— 重试机制把缺陷盖住了。" +
         "判据量的是【最终结果】,BUGFIX-E1 修的是【路径】。") "Red"
    Rep ("      ★ 修好之后这条【必须】是 0:写被挡住会【排队】,不是【被拒】。")
}
# 排队的写必须真的都发出去了(排队 > 0 时才检查)
if ($queuedLines.Count -gt 0) {
    $queuedSent = @()
    foreach ($f in $appLogFiles) {
        try {
            $fs2 = [System.IO.File]::Open($f.FullName, 'Open', 'Read', 'ReadWrite')
            $sr2 = New-Object System.IO.StreamReader($fs2, [System.Text.Encoding]::UTF8)
            while (-not $sr2.EndOfStream) {
                $line2 = $sr2.ReadLine()
                if ($line2 -match '排队写已发出') { $queuedSent += $line2 }
            }
            $sr2.Close(); $fs2.Close()
        } catch { }
    }
    Judge ($queuedSent.Count -ge $queuedLines.Count) "J11b ★排队的写都真的发出去了★" `
           ("排队 {0} 次 / 排队写已发出 {1} 次" -f $queuedLines.Count, $queuedSent.Count)
    if ($queuedSent.Count -lt $queuedLines.Count) {
        Rep ("      ★ 排队的写【丢了】—— 比拒收更严重:调用方会永远等一个不来的 writeFinished。") "Red"
    }
} else {
    Rep ("      (本轮没有写被排队 —— J11b 无从检查。" +
         "这不是问题,但也说明【排队那条通路本轮没被走到】。)") "DarkGray"
}

# ================================================================
#  结论
# ================================================================
Say ""
$verdict = 0
if ($appExited) {
    Rep ("★ 结论:被测程序中途退出了(exit={0})—— 这次没验成。" -f $appExitCode)
    $verdict = 3
} elseif ($fail.Count -gt 0) {
    Rep ("★ 结论:【红】—— {0} 条判据没过:" -f $fail.Count)
    foreach ($f in $fail) { Rep ("  ✗ " + $f) }
    $verdict = 1
} else {
    Rep "★ 结论:【绿】—— 每一个写出去的值都是期望的那个值,联动写通路【通了】。"
    Rep ("  联动 {0} 次 / 写 {1} 次,期望值 {2},实际出现过的值 {3}。" -f
         $actionCount, $writeCount,
         (($proj.actions | ForEach-Object { [string]$_.write_value } | Select-Object -Unique) -join ','),
         ((@($actionVals + $sendVals + $okVals) | Select-Object -Unique) -join ','))
    if ($guardLines.Count -gt 0) {
        Rep ("  ★ 守卫命中 {0} 次 —— 轮询回包撞上'待写'的变量时,读回确实没覆盖它。" -f $guardLines.Count)
    } else {
        Rep "  (守卫命中 0 次 —— 这一轮没撞上竞态,本次不构成对 G-16 修复的活体验证。)"
    }
    Rep "  ★ 这一轮证明的是:【发出去】的值对。审计条目本身只进界面表格、从不落文件,"
    Rep "    审计那一半由单元测试(tst_ActionWrite 的 auditRecords_theValueActuallySent)钉住。"
    $verdict = 0
}

$verdictPath = Join-Path $OutDir 'verdict.txt'
($report -join "`r`n") | Out-File -FilePath $verdictPath -Encoding UTF8
Say ""
Say ("判定文件: {0}" -f $verdictPath) 'Cyan'
Say ("退出码  : {0}" -f $verdict) 'Cyan'
exit $verdict
