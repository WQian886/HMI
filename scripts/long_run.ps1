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

    (3) 2026-10-21:活体判据【第四版】—— 改用程序自己打的【轮询统计行】。
        上面第 (2) 条的"按类别看"有两个方向都会错:
          · 假绿:本地心跳也被算成日志在长(2026-10-20 那次就是这么骗过去的);
          · 假红:健康的轮询【根本不产生业务类日志】——
            协议层的收发全是 qCDebug,默认 INFO 级别下一行都不落。
            于是"真跑着业务的 24 小时"会被判成"只在自转" → FAIL。
        现在的判据读的是 PollScheduler 每 60 秒打的一行:
            [PollScheduler] 轮询统计:发出=123 成功=120 失败=3 在途=0 PLC=1
        判【计数器在不在涨】,不判【有没有日志】:
            成功数在涨          → PASS(业务在跑)
            发出在涨、成功不涨   → 从站没回包 → 没验成(exit 4)
            发出不涨 + 在途不为 0 → 应用卡在在途态 → 验出问题(exit 1)
            一条统计行都没有     → 跑的是旧二进制 → UNKNOWN(exit 4)
        顺带:同一批调查还查出并修掉了应用的根因 ——
        ModbusMaster 在协议级失败(异常响应/超时)时【不发 readFinished(false)】,
        而 PollScheduler 只在 readFinished 里清 inFlight_ ⇒ 一次失败之后,
        那台 PLC 的变量就永久卡在在途态,再也不会被轮询到,而且日志里一声不吭。
        详见 PollScheduler::onStatsTick 与 ModbusMaster::finalizeErrorReply 的注释。
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
    [int]    $SettleSec     = 90,     # 启动后静默多久再取"活体快照"(等启动日志写完)

    # ---- 2026-10-21:启动自检窗口 ---------------------------------------
    #  程序每 60 秒打一行轮询统计(PollScheduler::onStatsTick),
    #  所以第一行最早出现在启动后 60 秒左右 —— 窗口小于这个数就永远等不到。
    #  给 120 秒:一行统计行之外,还留了点余量给工程加载和连接建立。
    [int]    $SelfCheckSec  = 120
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

# ★ 2026-10-21:轮询统计行的解析式 —— 【全脚本唯一出处】
#   程序那边的格式定死在 PollScheduler::onStatsTick:
#     "[PollScheduler] 轮询统计:发出=123 成功=120 失败=3 在途=0 PLC=1"
#   用它的地方有两处:开跑前的【启动自检】和结尾的【SOAK-6 判据】。
#   两处各写一份的话,程序那边一改格式,这边就静默失配 ——
#   而且失配的表现是"判不出"(不是"判错"),更难发现:
#   启动自检会一直等不到统计行(最后当成没资格跑),
#   SOAK-6 会判成 UNKNOWN(看起来像"跑的是旧二进制")。
#
#   ★★ 2026-10-21:这个正则曾经【一次都匹配不上】,而且两边都没发现 ★★
#   程序实际打出来的是:
#     轮询统计:发出= 271  成功= 271  失败= 0  在途= 0  PLC= 1  变量= 3
#                ↑ 等号后面多一个空格
#   原因在程序那边:QDebug 在【相邻两项之间插一个空格】,
#   于是 `<< "发出=" << count` 出来的是 "发出= 271",不是注释里写的 "发出=271"。
#   后果实测到了:启动自检等满 120 秒一条都没匹配上,判成"没资格跑",
#   而那 120 秒里程序一直在正常轮询(成功 271 次、失败 0 次)。
#   —— 判据坏掉的样子,和程序坏掉的样子,一模一样。
#   ★ 而 verify_soak6_liveness.ps1 一直绿 —— 因为它喂给判据的是
#     【它自己想象的格式】("发出=123",没空格)。一个拿自己当参照的测试,
#     证明不了它和现实对得上。
#
#   ⇒ 现在等号后面【允许有空白】(\s*)。两种格式都认:
#     · 新二进制(2026-10-21 起):发出=271 成功=271   —— 无空格
#     · 旧二进制(2026-10-21 之前):发出= 271  成功= 271 —— QDebug 插的空格
#   这样判据对【两个版本都有效】,而字段名/顺序改了照样会失配(那才是要命的)。
#   程序那边也修了:现在整行拼成一个字符串再输出,输出与它自己注释里的格式一致
#   (PollScheduler::formatStatsLine,tst_PollStatsFormat 钉着)。
$rePollStats = '轮询统计:发出=\s*(\d+)\s+成功=\s*(\d+)\s+失败=\s*(\d+)\s+在途=\s*(\d+)\s+PLC=\s*(\d+)'

# ================================================================
# ★★ 2026-10-27(SOAK-W 重做):写侧统计行的正则 ★★
# ================================================================
#  ★ 为什么这条跟 $rePollStats 是同一个级别的"单一出处" ★
#    读侧那条钉的是"业务还在跑";写侧这条钉的是"写有没有真的出去"。
#    两边都是【两方契约】:程序打出来,harness 用正则取数。
#    ⇒ 一边改了另一边没改,必须【立刻】有人喊 ——
#      tst_WriteStatsFormat 把这一对钉住(它把本行原样读出来,
#      再去匹配程序【真的打出来的那一行】)。
#
#  ★ 为什么字段名是中文、而且这么多 ★
#    因为它要同时回答两个【不同】的问题:
#      · "写有没有真的落地"      → 已发出 / 成功 / 失败
#      · "没落地的话,是环境还是缺陷" → 拒收的五个分类
#    合成一个"拒收 N 次"是不够的:断线拒收和缺陷拒收在日志上长得一模一样。
#
#  ★★ 为什么【必须】由程序来分类,而不是 harness 猜 ★★
#    原来的设计是 harness 刮日志 + 用"3 秒内有没有 errorOccurred"去猜。
#    ★ 那条路已经被【实测】推翻了(2026-10-27):
#      · 真断线【不产生】"Modbus 错误" —— 那串字在 hmi/hmi.cpp:400/416(UI 层),
#        而 TcpClient::onSocketError 故意【不发】 errorOccurred(免得界面闪红框);
#      · 断线会 failQueuedWrites() 把排队写作废 ⇒ 老的"排队 > 排队发出"
#        会【误报】"排队写丢了"。
#    ⇒ 只有【知道原因的那个地方】能分清环境与缺陷 —— 原因就在 ModbusMaster 里。
#      所以那条统计行把【原因】直接算好,harness 只负责取数。
#
#  ★ 捕获组顺序(= 字段顺序,改一处必须改两处)★
#    1 实例 2 尝试 3 接受 4 拒收 5 拒收未连 6 拒收非法 7 拒收队列满
#    8 拒收发送 9 拒收缺陷 10 排队 11 排队发出 12 排队作废
#    13 已发出 14 成功 15 失败 16 在队 17 在途写
$reWriteStats = '写侧统计:实例=\s*(\d+)\s+尝试=\s*(\d+)\s+接受=\s*(\d+)\s+拒收=\s*(\d+)\s+拒收未连=\s*(\d+)\s+拒收非法=\s*(\d+)\s+拒收队列满=\s*(\d+)\s+拒收发送=\s*(\d+)\s+拒收缺陷=\s*(\d+)\s+排队=\s*(\d+)\s+排队发出=\s*(\d+)\s+排队作废=\s*(\d+)\s+已发出=\s*(\d+)\s+成功=\s*(\d+)\s+失败=\s*(\d+)\s+在队=\s*(\d+)\s+在途写=\s*(\d+)'

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
    # ★ 2026-10-21:【永远】返回一个对象 —— 没有日志文件时返回"空统计",不返回 $null。
    #   为什么:调用方直接写 $baselineLog.Count / $baselineLog.MaxFile.Length,
    #   而 Set-StrictMode -Version 2.0 下对 $null 取 .Count 会抛
    #   PropertyNotFoundStrict,脚本【当场崩】。
    #   以前踩不到,是因为大家跑长跑时日志目录里已经有上一轮留下的文件;
    #   一旦给长跑实例一份【干净的私有数据目录】(这正是"不动正在跑的那一轮"的前提),
    #   基线那一刻目录就是空的 —— 这条隐藏的坑立刻现形。
    #   契约要稳:有文件没文件,返回的东西形状必须一样。
    $empty = [pscustomobject]@{
        Count   = 0
        Bytes   = 0
        MaxFile = $null
        Newest  = $null
    }
    if (-not (Test-Path $logDir)) { return $empty }
    $files = @(Get-ChildItem $logDir -Filter '*.log' -File -ErrorAction SilentlyContinue)
    if ($files.Count -eq 0) { return $empty }
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

# ★★★ 2026-10-27:上限从 50000 提到 200000,并加【截断检测】★★★
#   【原来那个 50000 是个雷,而且踩过一次】
#     到顶就【整个停止读】——不是"读到最近的",是"后面的一律不看"。
#     ⇒ 触顶之后,日志里【新发生的事】一行都读不到。
#     ⇒ 所有【靠日志行判定】的判据(SOAK-6、SOAK-W)都会读到一个【残缺的窗口】,
#       而【报告上看不出任何异常】—— 它只是安静地判了个 UNKNOWN 或者更糟:判个 PASS。
#   【实测依据(为什么 200000 够用)】
#     默认日志级别下实测:1440 行/24 小时(hmi_20261025_000045.log:1440 行 / 1439 分钟);
#     加上写侧统计行(60 秒一条)≈ 1440 行/24 小时 ⇒ 合计 ≈ 2880 行。
#     最悲观估计(按 [Action] 18.4 次/分算 24 小时)也才 ≈ 31000 行。
#     ⇒ 200000 有 6 倍余量;真到顶了,那就是日志级别被打开了(见下面 hmi.json 那段)。
function Get-LogSince($sinceTime, [int] $limit = 200000) {
    if ($null -eq $sinceTime) { return @() }
    $out   = New-Object System.Collections.Generic.List[object]
    # ★ 按文件名排序:日志文件名带时间戳(hmi_yyyyMMdd_HHmmss.log),
    #   字典序 ≈ 时间序 ⇒ 截断时截掉的是【最早】的那部分,不是随机的。
    #   不排序的话,文件顺序由文件系统决定 ⇒ 截断可能正好把【最新的】那个文件吃掉。
    $files = @(Get-ChildItem $logDir -Filter '*.log' -File -ErrorAction SilentlyContinue | Sort-Object Name)
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
                            # ★ File 字段:写侧判据要按【日志文件 + 实例】分组,
                            #   用来把不同进程的统计行分开(进程重启 ⇒ 计数器归零)。
                            File = $f.Name
                        })
                    }
                }
            }
            # ★ 截断检测:因为文件【没读完】而停了 ⇒ 后面还有内容没被看到。
            #   这个标志必须留着 —— 不然就是"用半截数据下结论",本项目的核心教训。
            if (-not $sr.EndOfStream -and $out.Count -ge $limit) {
                $script:logTruncated = $true
                Say ("★ 日志超过 $limit 行,已停止读取(" + $f.Name + ")—— 后面还有内容没被看到。") 'Red'
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

# ---- 占位符漏检(2026-10-21 新增)------------------------------------------
#  ★ 为什么要有这个
#  ------------------
#  2026-10-21 冒烟跑完,verdict.txt 里 SOAK-6 那一行是这么写的:
#
#      ★ 成功读 +{0} 次 —— 业务真的在跑,判定资格成立。
#
#  {0} 没被替换 —— 因为写那一行的人少写了一个 -f。
#  它没有报错、没有红、没有中断,判据照常判了 PASS。
#  而这行字的全部作用就是把"业务真的在跑"这件事【量化】给读报告的人看。
#  数字没了,这句话就退化成一句自我声明 —— 恰好是它本该避免的东西。
#
#  ★ 这和"轮询统计行格式对不上"是同一类病,同一个教训:
#      判据坏掉的样子,和被测物坏掉的样子,一模一样。
#      所以【坏了要响】,不能只在报告里留个记号等人发现。
#
#  这里扫的是 {0} {1} 这种【格式占位符的形状】,不是任意花括号 ——
#  免得路径里有个 { 也被当成漏检。
function Get-UnformattedPlaceholders {
    param([string] $Text)
    if ([string]::IsNullOrEmpty($Text)) { return @() }
    $m = [regex]::Matches($Text, '\{\d+\}')
    if ($m.Count -eq 0) { return @() }
    return @($m | ForEach-Object { $_.Value } | Select-Object -Unique)
}

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
        # ★ 落盘前扫一遍:报告里还有没替换的占位符 ⇒ 这份报告的【数字】是缺的。
        #   缺数字的报告【不能被当成验收结论】—— 判定的意义就是那些数字。
        #   写明 + 让退出码变成 4(没验成),而不是在文件里悄悄留一句。
        $leaked = @(Get-UnformattedPlaceholders -Text ($report -join "`n"))
        if ($leaked.Count -gt 0) {
            $script:reportHasPlaceholder = $true
            $script:reportLeaked = ($leaked -join ', ')
            $sb.Add("")
            $sb.Add("★★ 本报告里有【未替换的占位符】: " + ($leaked -join ', '))
            $sb.Add("★★ 上面这些位置本该是数字,现在缺了 —— 这份报告的数字不可信,")
            $sb.Add("★★ 【不能当验收结论用】。(harness 的问题,不是被测程序的问题)")
        }
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
# ★ 报告里有没有漏掉的占位符。默认 false,落盘时(Write-RunState 'DONE')改判。
#   显式初始化 —— 这个脚本开了 Set-StrictMode 2.0,没初始化的变量读一下就炸。
$script:reportHasPlaceholder = $false
$script:reportLeaked = ''
# ★ 2026-10-27:日志被截断了吗?(Get-LogSince 撞上限时置位)
#   截断了就意味着"后面还有内容没被看到" ⇒ 任何靠日志行的判据都只看到了半截
#   ⇒ 整体降级为 exit 4(没验成),不许拿半截数据下结论。
$script:logTruncated = $false

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
    # ★ 2026-10-21:-Project → --project(现在这个开关是真能打开工程的)
    #   【为什么长跑必须用它】默认工程指向 127.0.0.1:502 —— 那是 mbslave 占的端口,
    #   而 mbslave 现在对每个 FC3 都回 ExCode=0x03(读必失败)。
    #   不换工程,这一轮验到的只有"重试正常",验不到"读得到数据"。
    if ($Project) {
        if (-not (Test-Path $Project)) {
            Say ("-Project 指定的工程文件不存在: {0}" -f $Project) 'Red'; exit 1
        }
        $Project = (Resolve-Path $Project).Path
        $launchArgs += @('--project', $Project)
        Say ("工程文件: {0}" -f $Project) 'Cyan'
    }

    # ★ 2026-10-21:把实例键交给被测进程。
    #   机制:环境变量 HMI_INSTANCE_KEY —— 由 SingleInstance::keyFromEnvironment() 读取。
    #   【为什么不是命令行选项】
    #     单实例检测发生在 main() 的最前面,那时 QApplication 还不存在,
    #     命令行解析器也就还没建好 —— 键【拿不到】。
    #     (把解析器整体前移就得改公共启动路径,风险比收益大。)
    #     环境变量没有这个次序问题:任何时候都能读。
    #   【显式设置,不留隐式状态】
    #     不给 -InstanceKey 时也显式置空 —— 让被测进程用默认键,
    #     和报告里写的对得上。
    #     (环境里要是残留着一个 HMI_INSTANCE_KEY,而报告说"用默认键",
    #      那报告和现场就对不上了 —— 对不上就查不了。)
    if ($InstanceKey) {
        $env:HMI_INSTANCE_KEY = $InstanceKey
        Say ("单实例键          : {0}(经环境变量 HMI_INSTANCE_KEY 传给被测进程)" -f $InstanceKey) 'Cyan'
    } else {
        $env:HMI_INSTANCE_KEY = ''
        Say "单实例键          : 默认(hmi-single-instance-v1)" 'DarkGray'
    }

    $owned = Start-Process -FilePath $exePath -ArgumentList $launchArgs -PassThru
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

    # ---- 启动自检:程序到底有没有在跟 PLC 说话 --------------------------
    #  ★ 为什么要有这道门(2026-10-21)
    #  -----------------------------------
    #  上一轮长跑(950.9 分钟)的教训:程序从启动后【第一秒】起就没跟 PLC 说过话
    #  (第一批读请求失败 → 变量卡在在途态 → 那台 PLC 再也收不到任何读请求),
    #  而"日志还在长"(本地心跳每 60 秒一行)把 harness 骗了过去 ——
    #  结果是 16 小时采到空气,连判据都没写出来。
    #  那种失败要到【最后】判 SOAK-6 才暴露,代价是一整夜。
    #  这里在开跑后 $SelfCheckSec 秒内就问一次:"你到底发出请求没有?"
    #  没有 → 这一轮没资格跑,当场停 —— 而不是"跑满 24 小时再说不合格"。
    #
    #  【判据来源】和 SOAK-6 同一个正则($rePollStats)、同一个来源
    #    (程序自己打的轮询统计行,PollScheduler::onStatsTick,每 60 秒一条)。
    #    发出 > 0 → 在通信(哪怕全失败,那也是从站的问题,不是应用的)
    #    发出 = 0,或根本没有统计行 → 没资格跑
    Say ("启动自检:等程序打出第一行轮询统计(最多 {0} 秒)..." -f $SelfCheckSec) 'Cyan'
    $selfCheckOk    = $false
    $selfCheckNote  = ''
    $selfCheckUntil = (Get-Date).AddSeconds($SelfCheckSec)
    while ((Get-Date) -lt $selfCheckUntil) {
        Start-Sleep -Seconds 10
        $null = $owned.Refresh()
        if ($owned.HasExited) {
            $code = try { $owned.ExitCode } catch { $null }
            $selfCheckNote = "被测进程在启动自检期间退出了(exit=" +
                             $(if ($null -eq $code -or "$code" -eq '') { "取不到" } else { $code }) + ")"
            break
        }
        $selfLines = @(Get-LogSince $baselineTime 5000)
        $selfStat  = @($selfLines | Where-Object { $_.Msg -match $rePollStats })
        if ($selfStat.Count -gt 0) {
            $m     = [regex]::Match($selfStat[-1].Msg, $rePollStats)
            $sent  = [int]$m.Groups[1].Value
            $ok    = [int]$m.Groups[2].Value
            $fail  = [int]$m.Groups[3].Value
            $infl  = [int]$m.Groups[4].Value
            if ($sent -gt 0) {
                $selfCheckOk = $true
                Say ("  ✓ 发出={0} 成功={1} 失败={2} 在途={3} —— 程序确实在跟 PLC 通信,这一轮有资格跑。" -f `
                     $sent, $ok, $fail, $infl) 'Green'
                break
            }
            $selfCheckNote = ("程序打出了统计行,但发出=0(成功=0 失败=0 在途={0}) —— 一个请求都没发出去" -f $infl)
            break
        }
    }
    if (-not $selfCheckOk) {
        if ($selfCheckNote -eq '') {
            $selfCheckNote = "等了 $SelfCheckSec 秒,一条轮询统计行都没有"
        }
        Say "★ 启动自检没过 —— 这一轮没资格跑,当场停:" 'Red'
        Say ("  " + $selfCheckNote) 'Red'
        Say "  查:① -Exe 指的是不是【修好的那个二进制】(2026-10-21 之前的那版没有轮询统计行);" 'Red'
        Say "      ② 从站在不在(现在 502 端口上那个 mbslave 对每个 FC3 都回 ExCode=0x03);" 'Red'
        Say "      ③ 工程文件的 host/port 对不对(长跑要指向自建从站 127.0.0.1:1502);" 'Red'
        Say "      ④ ★ 日志里那一行的【格式】还对不对得上 \$rePollStats —— 2026-10-21 踩过这个:" 'Red'
        Say "         程序打的是 '发出= 271'(QDebug 插的空格),正则要的是 '发出=271',于是永远匹配不上。" 'Red'
        Say "         症状和'程序没在跑'一模一样:【判据坏了,不是程序坏了】。" 'Red'
        # 把我们启动的进程收掉 —— 留一个不会干活的进程在机器上,是最难查的现场
        try { if (-not $owned.HasExited) { Stop-Process -Id $owned.Id -Force } } catch { }
        Write-RunState 'PREFLIGHT_FAILED' ("启动自检没过:" + $selfCheckNote)
        exit 4
    }
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
if ($settledLog.Count -eq 0) {
    # ★ 2026-10-21:一个日志文件都没有,是【异常信号】,不是"正常但没日志"。
    #   启动 90 秒了还一个文件没落 ⇒ 日志压根没建起来。
    #   以前这条分支说的是"没有日志目录" —— 而现在 Get-LogStats 永远返回对象,
    #   "目录不存在"和"目录里没有文件"都归到这儿,措辞得跟着改对。
    Say "静默期快照:一个日志文件都没有(启动 90 秒了还没落日志)—— 拿不到活体证据" 'Yellow'
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

# ============================================================
# ★★ 活体判据【第二版】(2026-10-21)★★
# ============================================================
#  第一版判据:业务类日志行数 > 0。
#  它【两个方向都会错】,而且错得都很贵:
#
#    · 假绿:本地心跳(HistoryRecorder/TrendWidget)也写日志。
#      2026-10-20 那次 950.9 分钟就是被这个骗过去的 ——
#      进程自己在给自己记心跳,看着像"在干活"。
#
#    · 假红:健康的轮询【根本不产生业务类日志】!
#      协议层的收发全是 qCDebug,默认 INFO 级别下一行都不落。
#      于是"真跑着业务的 24 小时"会被判成"只在自转" → FAIL。
#      —— 这是第一版判据最要命的地方:它会否掉唯一有意义的跑法。
#      (2026-10-21 查完根因才发现:协议层失败确实一行日志都不打。)
#
#  第二版判据:读程序自己打的【轮询统计行】
#      "[PollScheduler] 轮询统计:发出=123 成功=120 失败=3 在途=0 PLC=1"
#    (PollScheduler 每 60 秒一条,INFO 级,见 PollScheduler::onStatsTick)
#  判的是【计数器在不在涨】,不是【有没有日志】:
#      · 成功数在涨          → 业务真的在跑(唯一能判 PASS 的情形)
#      · 发出在涨、成功不涨   → 发出去了没人回 ⇒ 从站不响应 / 在途态卡死
#      · 发出不涨            → 一个请求都没发出去 ⇒ 没连上 / 没有到期变量
#      · 一条统计行都没有     → 跑的是旧二进制(没这行),判不出来 → UNKNOWN
#
#  为什么"计数器"骗不了人:它记的是真实发生过的收发次数,
#  而"这一行日志本身还在打"又证明程序还活着。
#  日志行数则两头都能骗 —— 多了是心跳,少了是协议层静默。
#
#  ★★ 为什么把判据抽成【函数】★★
#  判据是这次长跑唯一能信的东西,它必须能被单独拿去喂数据、验它会不会红。
#  写在脚本主干里就没法做对照实验(主干一跑就是 24 小时)。
#  scripts/verify_soak6_liveness.ps1 从【本文件里原样抠出】这两个函数来跑,
#  不复制粘贴一份 —— 复制的那份迟早跟本体对不上,而对不上的时候没人会发现。

function Get-PollStats {
    <#
      从日志行里抠出轮询统计行,算出"这一轮里涨了多少"。
      返回的对象字段:
        N          统计行条数
        SentDelta  发出计数增量
        OkDelta    成功计数增量   ← 判"业务在不在跑"就看它
        FailDelta  失败计数增量
        Inflight   最后一条的"在途变量"数
        SentTotal  最后一条的累计发出
        OkTotal    最后一条的累计成功
    #>
    param([array] $Lines)
    # ★ 2026-10-21:正则改成用脚本级的 $rePollStats(全脚本唯一出处)。
    #   原来这里是函数内的一份副本 —— 启动自检也用同一套格式,
    #   两份副本迟早有一份跟程序那边对不上,而对不上的表现是"判不出"。
    $re = $rePollStats
    $statLines = @($Lines | Where-Object { $_.Msg -match $re })
    $r = [pscustomobject]@{
        N = $statLines.Count; SentDelta = 0; OkDelta = 0; FailDelta = 0
        Inflight = 0; SentTotal = 0; OkTotal = 0
    }
    if ($r.N -eq 0) { return $r }
    $mFirst = [regex]::Match($statLines[0].Msg,  $re)
    $mLast  = [regex]::Match($statLines[-1].Msg, $re)
    if (-not ($mFirst.Success -and $mLast.Success)) { return $r }
    $r.SentTotal = [int]$mLast.Groups[1].Value
    $r.OkTotal   = [int]$mLast.Groups[2].Value
    $r.SentDelta = $r.SentTotal - [int]$mFirst.Groups[1].Value
    $r.OkDelta   = $r.OkTotal   - [int]$mFirst.Groups[2].Value
    $r.FailDelta = [int]$mLast.Groups[3].Value - [int]$mFirst.Groups[3].Value
    $r.Inflight  = [int]$mLast.Groups[4].Value
    return $r
}

function Get-LivenessVerdict {
    <#
      活体判据本体。返回:
        State     'PASS' / 'FAIL' / 'UNKNOWN'
        AppDefect $true = 这一轮查出的是【应用自己的故障】(退出码 1),
                  $false = 没验成 / 环境问题(退出码 4)
        Reason    判据说明(直接进报告)
    #>
    param($Stats, [bool] $IsFrozen)
    $s = $Stats
    if ($IsFrozen) {
        return [pscustomobject]@{ State='FAIL'; AppDefect=$false
            Reason='冻住:一条日志都没长,事件循环停了(可能是模态框)。这一轮什么都不用看了。' }
    }
    if ($s.N -eq 0) {
        return [pscustomobject]@{ State='UNKNOWN'; AppDefect=$false
            Reason=("★ 一条【轮询统计行】都没有 —— 判不出活体,不下结论。`n" +
                    "          三种可能:(a) 跑的是 2026-10-21 之前编译的旧二进制(那版没这行);`n" +
                    "                    (b) 日志级别把 hmi.poll 类别滤掉了(默认 INFO,不该发生);`n" +
                    "                    (c) 程序改了这一行的格式,正则没跟上 —— ★ 这一种最难查,`n" +
                    "                        因为症状和'程序没在跑'一模一样。`n" +
                    "                        查:日志里那一行长什么样,与 $rePollStats 对不对得上。`n" +
                    "          ★ 旧判据在这里会判成'业务类日志 0 条 → 只在自转' —— 那是假红:`n" +
                    "            健康的轮询本来就不产生业务类日志(协议层全是 Debug 级)。") }
    }
    if ($s.OkDelta -gt 0) {
        $extra = ''
        if ($s.FailDelta -gt 0) {
            $extra = ("`n        (期间失败 {0} 次 —— 2026-10-21 修的 readFinished(false) 让它能自动重试,没有卡死在途态)" -f $s.FailDelta)
        }
        # ★ 2026-10-21:这里原本【少写了一个 -f】。
        #   少写的后果不是"报错了",是"报了一句看起来像结论、其实没有数字的话":
        #   verdict.txt 里落下来的是 "★ 成功读 +{0} 次 —— 业务真的在跑,判定资格成立。"
        #   —— 判 PASS,数字却是空的。读报告的人扫一眼"判定资格成立"就翻过去了。
        #   (和"轮询统计行格式对不上"是同一类病:坏了不响。)
        #   ⇒ 下面这行必须带 -f;另外整个报告在落盘前会扫一遍有没有漏掉的占位符。
        return [pscustomobject]@{ State='PASS'; AppDefect=$false
            Reason=(("★ 成功读 +{0} 次 —— 业务真的在跑,判定资格成立。`n" +
                     "          SOAK-2/3 的数字这一轮有效。") -f $s.OkDelta) + $extra }
    }
    if ($s.SentDelta -gt 0) {
        # ★★ 2026-10-21:这两支原来都栽在同一个 PowerShell 陷阱上 ——
        #   【-f 的优先级比 + 高】。
        #   "a{0}" + "b{1}" -f $x  实际是  "a{0}" + ("b{1}" -f $x)
        #   ⇒ 只有【最后一个】字面量被格式化,前面几段里的 {0}/{1} 原样留着!
        #   于是"从站不回包"这一支落进报告的是:
        #       ★ 发出 +{0} 次但成功 0 次:…在途变量 = {1} —— …
        #   —— 两个数字全缺。而这一支恰恰是【真出问题时才走的那一支】。
        #   (判据的状态还是对的,FAIL / 不算应用故障;坏掉的是给人看的那句话。)
        #   ⇒ 修法:先【把整个拼接括起来】,再 -f。顺序反了就全错。
        return [pscustomobject]@{ State='FAIL'; AppDefect=$false
            Reason=(("★ 发出 +{0} 次但成功 0 次:请求发出去了,没人回。`n" +
                     "          在途变量 = {1} —— 这正是 2026-10-21 查出的根因的现场形态:`n" +
                     "          一次协议级失败之后,变量卡在在途态,再也发不出去。`n" +
                     "          查:从站是不是不回包(服务没起?地址对不上?),或看日志里的 [ModbusMaster] 通讯错误。") -f `
                    $s.SentDelta, $s.Inflight) }
    }
    if ($s.Inflight -gt 0) {
        return [pscustomobject]@{ State='FAIL'; AppDefect=$true
            Reason=(("★ 发出数一条没涨,而在途变量 = {0} —— 卡在在途态等一个永远不来的回包。`n" +
                     "          这就是 2026-10-21 查出的根因:一次失败之后,inFlight_ 永不清。`n" +
                     "          ★ 这是【应用侧的故障】(不是从站没回包),退出码按'验出问题'给。`n" +
                     "          查:日志里的 [ModbusMaster] 通讯错误(异常响应/超时)。") -f $s.Inflight) }
    }
    return [pscustomobject]@{ State='FAIL'; AppDefect=$false
        Reason=("★ 发出数一条没涨,在途变量 0 —— 压根没有请求要发。`n" +
                "          查:PLC 连上了没?变量表是不是空的?是不是所有变量都 alwaysPoll=false?") }
}

$stats        = Get-PollStats -Lines $sinceLines
$statsN       = $stats.N
$pollSentDelta = $stats.SentDelta
$pollOkDelta   = $stats.OkDelta
$pollFailDelta = $stats.FailDelta
$inflightLast  = $stats.Inflight
$okTotalLast   = $stats.OkTotal
$sentTotalLast = $stats.SentTotal

# ================================================================
# ★★ 写侧判据(2026-10-27 新增)★★
# ================================================================
#  ★ 为什么长跑需要这一条(而以前没有)★
#    活体判据【只看读】:成功读计数在涨 ⇒ 判 PASS。
#    ⇒ 而 BUGFIX-E1 那个洞(写被读饿死)【读完全正常】——
#      修之前实测:读 547 次全部成功,写 604 次全部被拒,0 帧落地。
#    ⇒ ★ 所以【长跑的活体判据对"写被饿死"是瞎的】★
#      —— 24 小时一个字节都写不出去,活体判据照样绿。
#    ⇒ 这跟 G-15/G-16 同一个形状:判据量错了东西。
#
#  ★ 为什么"写落地 0 条"也不够(重试机制会盖住)★
#    2026-10-27 现场级负向对照实测:把排队拆回"直接拒收"之后,
#    那一轮【拒收 12 次、发写 51 次、FC6 写帧 39 条、值全对】,
#    短跑判据 J1~J10 【全绿】!
#    ⇒ 因为"拒收 → dirty 保留 → 下一拍重发 → 这次发出去了"。
#    ⇒ ★ 所以判据必须盯【拒收次数】,而不是只盯"最后有没有落地"。★
#
#  ★ 这条判据【不参与】活体 PASS/FAIL 的判定(那是读的事)★
#    —— 它是【独立的一道闸】:写侧出问题,活体该绿还是绿,
#       但这轮【不算验收通过】。
#    ⇒ 为什么不并进活体判据:活体判据被 verify_soak6_liveness.ps1
#      原样抠出去单独喂数据(见上面 Get-LivenessVerdict 的注释),
#      动它会连带那个对照脚本一起坏。
#    ⇒ 而且它们回答的是【不同的问题】:
#      活体:程序还活着、还在跟 PLC 通信吗?
#      写侧:写有没有【真的、按该走的那条路】出去?
function Get-WriteSideStats {
    <#
      从日志行里抠出【写侧统计行】,算窗口增量,并检查两条【会计恒等式】。

      ★★ 为什么判据整个搬到了程序里(这是 2026-10-27 的重做)★★
        原来这一块是 harness 刮日志 + 用"3 秒内有没有 errorOccurred"猜环境 vs 缺陷。
        ★ 那条路已经被【实测】推翻了:
          · 真断线【不产生】"Modbus 错误" —— 那串字在 hmi/hmi.cpp:400/416(UI 层),
            而 TcpClient::onSocketError 故意【不发】 errorOccurred(免得界面闪红框);
          · 断线会 failQueuedWrites() 把排队写作废 ⇒ 老的"排队 > 排队发出"
            会【误报】"排队写丢了" —— 一条天天误报的判据,跑一个月就没人信了。
        ⇒ 现在:【原因】由程序在它【知道原因】的地方算好(ModbusMaster 里),
          harness 只负责取数 + 判恒等式。

      返回对象:
        StatLines / Groups / Resets   统计行条数 / 分组数 / 计数器归零次数
        尝试 接受 拒收 拒收未连 拒收非法 拒收队列满 拒收发送 拒收缺陷
        排队 排队发出 排队作废 已发出 成功 失败      ← 都是【窗口增量】
        Identity1Bad / Identity1Residual            恒等式①残差(组数 / 绝对值和)
        Identity2Bad / Identity2Residual            恒等式②残差
    #>
    param([array] $Lines)

    # ★ 捕获组顺序(= 字段顺序)。程序侧见 ModbusMaster::formatWriteStatsLine。
    #   2..15 = 【累计计数器】(只增)⇒ 首末行做差 = 窗口增量;
    #   16/17 = 【仪表】(瞬时值)⇒ 只取最后一次的值,不做差;
    #   1     = 实例号(进程内自增)。
    $names = @('实例','尝试','接受','拒收','拒收未连','拒收非法','拒收队列满','拒收发送','拒收缺陷',
               '排队','排队发出','排队作废','已发出','成功','失败','在队','在途写')

    $r = [pscustomobject]@{
        StatLines = 0; Groups = 0; Resets = 0
        # ★ DeltaAvailable:窗口增量【可不可信】。
        #   增量 = 末行 - 首行 ⇒ 一组里【只有一条】统计行时增量【恒等于 0】,
        #   而那【不是】"没发生过写",只是"算不出差"。
        #   ⇒ 只要有一组不足两条,整个增量视图就【不可信】。
        DeltaAvailable = $false
        SingleLineGroups = 0
        # 窗口跨度 / 统计行【覆盖率】—— 用来发现"统计定时器中途停了"
        WindowSpanSec = 0; ExpectedLines = 0; Coverage = 0
        Identity1Bad = 0; Identity1Residual = 0
        Identity2Bad = 0; Identity2Residual = 0
        Identity3Bad = 0; Identity3Residual = 0
    }
    foreach ($n in $names) {
        if ($n -eq '实例' -or $n -eq '在队' -or $n -eq '在途写') { continue }
        Add-Member -InputObject $r -MemberType NoteProperty -Name $n -Value ([int64]0)
    }
    # ★ 绝对值视图 —— 跟增量视图【平级】的一份,不是备胎。
    #   为什么要它,见下面"绝对值视图"那一段的注释。
    #   ★ 提前建好(不是用到了才建)—— 判据函数在所有分支上都会读它,
    #     而 StrictMode 2.0 下读一个不存在的属性会直接抛错。
    $abs = [pscustomobject]@{}
    foreach ($n in $names) {
        if ($n -eq '实例' -or $n -eq '在队' -or $n -eq '在途写') { continue }
        Add-Member -InputObject $abs -MemberType NoteProperty -Name $n -Value ([int64]0)
    }
    Add-Member -InputObject $r -MemberType NoteProperty -Name 'Abs' -Value $abs
    if (-not $Lines) { return $r }

    $re = $reWriteStats
    $parsed = New-Object System.Collections.Generic.List[object]
    foreach ($l in $Lines) {
        if ($l.Msg -notmatch $re) { continue }
        $m = [regex]::Match($l.Msg, $re)
        if (-not $m.Success) { continue }
        # ★ 数组长度要 18,不是 17 ——
        #   正则的捕获组是【1..17】(1 基),而 .NET 数组是【0 基】⇒ 下标 17 也要能放。
        #   写成 17 的话下标 17 越界 ⇒ "Index was outside the bounds of the array"。
        #   ★ 这条是【verify_soakw_write_side.ps1 跑出来的】——
        #     真跑一次才发现的,光看代码看不出来(17 个字段 ↔ 数组长度 17,一眼像是对的)。
        $v = New-Object 'System.Int64[]' 18
        for ($i = 1; $i -le 17; $i++) { $v[$i] = [int64]$m.Groups[$i].Value }
        $parsed.Add([pscustomobject]@{ File = $l.File; Time = $l.Time; V = $v })
    }
    $r.StatLines = $parsed.Count
    if ($parsed.Count -eq 0) { return $r }

    # ★ 按 (日志文件, 实例号) 分组。
    #   进程内实例号自增 ⇒ 同一个日志文件里不同的实例是【不同的 PLC 通道】,
    #   计数器各算各的,不能混。
    #   日志文件分开 ⇒ 跨天滚动、进程重启天然分开。
    #   ★ 用哈希表分组,不用 Group-Object -Property { ... } ——
    #     后者在 PS 5.1 里对脚本块属性的求值时机不好把握,而这里算错一次
    #     就是"两台 PLC 的计数混在一起"⇒ 恒等式必然残差 ⇒ 假 FAIL。
    $groups = @{}
    foreach ($it in $parsed) {
        $key = "$($it.File)#$($it.V[1])"
        if (-not $groups.ContainsKey($key)) {
            $groups[$key] = New-Object System.Collections.Generic.List[object]
        }
        $groups[$key].Add($it)
    }
    $r.Groups = $groups.Count

    # ★ 只有【一条】统计行的组:增量算不出来(末-首=0,但那不是"没发生过")。
    #   这些组的绝对值照样有效 ⇒ 坏路径照样能判。
    $singleLineGroups = 0

    foreach ($key in @($groups.Keys)) {
        $items = @($groups[$key] | Sort-Object Time)
        if ($items.Count -lt 2) { $singleLineGroups++ }
        # ★★ 绝对值视图:每段取【该段最后一行】的原始值,各段【相加】 ★★
        #   —— 因为"归零"把一个【单调计数器】切成了几段,每段各自从 0 开始。
        #   段内最后一行 = 该段的累计终值 ⇒ 各段相加 = 【进程一生】的累计值。
        #
        #   ★ 为什么需要这一份(增量视图不够用)
        #     增量视图算的是"窗口内发生了什么",它【必须有两条以上的统计行】才能算。
        #     而窗口里【只有一条】时,增量恒为 0 —— 那不是"没发生过",是"算不出差"。
        #     ⇒ 2026-10-28 实测踩到了:恒拒收突变体的第一拍(只有一条统计行),
        #       那一行明明白白写着"尝试=541 拒收缺陷=541",
        #       判据却报"本轮窗口内写尝试 0 次(没写过)"——
        #       【事实相反的话】,而且方向是"看不见缺陷" ⇒ 假绿。
        #     ⇒ 坏路径(拒收缺陷/拒收队列满)是【二进制的属性】,不是窗口的属性:
        #       那条路径坏了,它在窗口【之前】坏了也一样坏。
        #       ⇒ 判它必须看【绝对值】,而不是看窗口内的差。
        $segAbs = New-Object 'System.Int64[]' 18
        # ★ 长度 18 —— 同上:捕获组下标最大是 17。
        $segFirst = New-Object 'System.Int64[]' 18
        $prev     = New-Object 'System.Int64[]' 18
        $acc      = New-Object 'System.Int64[]' 18
        $havePrev = $false
        foreach ($it in $items) {
            if ($havePrev) {
                # ★ 段边界:【尝试】掉了 ⇒ 累计计数器归零 ⇒ 进程重启过。
                #   用"尝试"当探针,是因为它【每次写尝试都 +1】,是所有计数器里
                #   变化最密的 —— 归零时它最不可能"碰巧"没掉。
                #   (用别的计数器就有可能:旧的拒收数是 0,新的也是 0 ⇒ 看不出来。)
                if ($it.V[2] -lt $prev[2]) {
                    $r.Resets++
                    # 旧段结算:观测到的两行之间的增量
                    for ($i = 2; $i -le 15; $i++) {
                        $acc[$i] += ($prev[$i] - $segFirst[$i])
                        # ★ 旧段的【绝对终值】= 旧段最后一行 $prev 的原始值。
                        #   (计数器在进程启动时从 0 开始 ⇒ 段内最后一行就是该段的累计终值。)
                        $segAbs[$i] += $prev[$i]
                    }
                    # ★★ 新段的基线 = 0,不是新段第一行的值 ★★
                    #   为什么:重启发生在【两行之间】⇒ 重启点在观测窗口【之内】
                    #   ⇒ 新进程【整个生命期】都在窗口里
                    #   ⇒ 它那些计数【全部】是窗口内的量,不能减掉。
                    #   ★ 写成 $it.V[$i] 的话,新进程做的 5 次尝试会被减成 0 ——
                    #     而那 5 次里可能有【拒收缺陷】⇒ 缺陷被漏掉 ⇒ 假绿。
                    #     ⇒ 宁可多算(⇒ 可能假红,能看见),不可少算(⇒ 假绿,看不见)。
                    for ($i = 2; $i -le 15; $i++) { $segFirst[$i] = 0 }
                }
            } else {
                for ($i = 2; $i -le 15; $i++) { $segFirst[$i] = $it.V[$i] }
                $havePrev = $true
            }
            for ($i = 1; $i -le 17; $i++) { $prev[$i] = $it.V[$i] }
        }
        for ($i = 2; $i -le 15; $i++) { $acc[$i] += ($prev[$i] - $segFirst[$i]) }
        # ★ 最后一段的绝对终值 = 本组最后一行 $prev。
        for ($i = 2; $i -le 15; $i++) { $segAbs[$i] += $prev[$i] }

        # ★ $names 是 0 基,$v 是 1 基 ⇒ 名字要用 $names[$i-1]。
        #   写成 $names[$i] 会整体错位一格:【尝试】的增量会被记到【接受】头上,
        #   【失败】会被记到【在队】头上 —— 而恒等式是分开算的,于是
        #   判据会【看着绿、其实在量错东西】。这种错不会自己暴露。
        for ($i = 2; $i -le 15; $i++) {
            $nm = $names[$i - 1]
            $r.$nm = [int64]$r.$nm + $acc[$i]
            # ★ 绝对值视图:【各组的绝对终值相加】。
            #   不同组 = 不同 PLC 通道 ⇒ 各自的坏路径【各算各的】,
            #   但对"这条通路坏没坏"这个问题,【任一组坏了就是坏了】⇒ 相加是对的。
            $r.Abs.$nm = [int64]$r.Abs.$nm + $segAbs[$i]
        }

        # ★★ 恒等式检查:用【本组最后一行】的原始值(快照),不是增量 ★★
        #   ① 已发出 == 成功 + 失败 + 在途写
        #      ⇒ 有【发出去却永远没回来】的写 ⇒ 泄漏(BUGFIX-E3 那种)⇒ FAIL
        #   ② 排队 == 排队发出 + 排队作废 + 在队
        #      ⇒ 有【排队写凭空消失】 ⇒ FAIL
        #   ★ 为什么用快照而不是增量:
        #     "在途写"是瞬时值,不是累计值 ⇒ 拿它做差没有意义。
        #     而在【任何一个统计时刻】,sent == ok + fail + inflight 都必须成立 ——
        #     这是一个【恒等式】,不是一段时间里的收支平衡。
        #   ★ 为什么"排队作废"必须单列:
        #     断线时 ModbusMaster 会 failQueuedWrites() 把排队写作废 ——
        #     那是【正常的环境事件】,不是"排队写丢了"。
        #     原来的判据把这两件事混成一件 ⇒ 必然误报。
        $last = $items[-1]
        $sent = $last.V[13]; $ok = $last.V[14]; $fail = $last.V[15]; $infl = $last.V[17]
        $i1 = $sent - ($ok + $fail + $infl)
        if ($i1 -ne 0) {
            $r.Identity1Bad++
            $r.Identity1Residual += [math]::Abs($i1)
        }
        $queued = $last.V[10]; $qsent = $last.V[11]; $qvoid = $last.V[12]; $inq = $last.V[16]
        $i2 = $queued - ($qsent + $qvoid + $inq)
        if ($i2 -ne 0) {
            $r.Identity2Bad++
            $r.Identity2Residual += [math]::Abs($i2)
        }
        # ★★★ 恒等式③:拒收 == 拒收未连 + 拒收非法 + 拒收队列满 + 拒收发送 + 拒收缺陷
        #   为什么加这一条:上面两个恒等式查的是【发出去的写有没有回来】,
        #   查不到【被拒收的写有没有被正确分类】。
        #   而"拒收缺陷"这个桶的存在本身(有拒收没进任何已知桶)就是一条缺陷路径。
        #   ⇒ 这条恒等式是【自洽性检查】:程序说"我拒了 N 次",
        #     那这 N 次【必须】都能在五个桶里找到归宿。
        #     差值不为零 ⇒ 有一次拒收【连自己属于哪一类都不知道】⇒ 缺陷。
        #   ★ 用绝对值判(快照),跟①②一样 —— 它是"此刻的账",不是"一段时间的收支"。
        $rej = $last.V[4]
        $rejSum = $last.V[5] + $last.V[6] + $last.V[7] + $last.V[8] + $last.V[9]
        $i3 = $rej - $rejSum
        if ($i3 -ne 0) {
            $r.Identity3Bad++
            $r.Identity3Residual += [math]::Abs($i3)
        }
    }

    # ★ 增量可不可信:只要有【一组】不足两条统计行,增量视图就【不能用来下结论】。
    #   (那一组的增量恒为 0 —— 是"算不出差",不是"没发生"。)
    $r.DeltaAvailable = ($singleLineGroups -eq 0)
    $r.SingleLineGroups = $singleLineGroups

    # ★ 窗口跨度 & 覆盖率 —— 用来发现"统计定时器中途停了"。
    #   统计行是 60 秒一条 ⇒ 窗口里【本该】有多少行是可以算出来的。
    #   覆盖率远低于 1 ⇒ 中间有洞 ⇒ 那一段的写侧是【看不见】的。
    if ($parsed.Count -gt 0) {
        $ts = @($parsed | Sort-Object Time)
        $r.WindowSpanSec = [math]::Round(($ts[-1].Time - $ts[0].Time).TotalSeconds, 1)
        $r.ExpectedLines = [int][math]::Floor($r.WindowSpanSec / 60) + 1
        $r.Coverage = if ($r.ExpectedLines -gt 0) {
            [math]::Round($r.StatLines / $r.ExpectedLines, 3)
        } else { 1 }
    }
    return $r
}

function Get-SlaveWriteCount {
    <#
      数从站日志里【真的收到】的写帧 —— 这是【地面真相】,应用伪造不了。
      从站日志格式(test_slave.py:157-162):
        "2026-10-27 09:29:47 [slave] conn#2 FC6 写 addr=2 value=0"
      ★ 只数 FC6 / FC16:
        FC6  行打印 value(= 从站真收到的值);FC16 行【不打印 value】(只数条数)。
      ★ 找不到从站日志 ⇒ Available=$false —— 【不装作通过】,报告里说"未执行"。
    #>
    param([string] $SlaveLog, $SinceTime)
    $res = [pscustomobject]@{ Available = $false; Fc6 = 0; Fc16 = 0; Total = 0; Path = $SlaveLog }
    if (-not $SlaveLog -or -not (Test-Path $SlaveLog)) { return $res }
    $res.Available = $true
    $reSlave = '^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\s+\[slave\].*?(FC6|FC16)\s+写'
    foreach ($ln in [System.IO.File]::ReadLines($SlaveLog)) {
        if ($ln -notmatch $reSlave) { continue }
        if ($null -ne $SinceTime) {
            $ts = [datetime]::ParseExact($matches[1], 'yyyy-MM-dd HH:mm:ss', $null)
            # ★★ 边界修正:从站时间戳【秒级】,而 $SinceTime 是【亚秒级】 ★★
            #   设 $SinceTime = 10:00:00.400,而从站把一帧 10:00:00 的写记成
            #   "10:00:00"(秒级截断)⇒ 直接比会 $ts < $SinceTime ⇒ 这一帧被丢掉。
            #   而这一帧【可能就是应用算作成功的那一帧】⇒ 从站数 < 应用成功数
            #   ⇒ 判据判 FAIL ⇒ 【假红】。
            #   ⇒ 这里【多留 1 秒】。方向是安全的:不变式是"从站收到的 >= 应用成功的",
            #     多算几帧【不会】让判据误红,少算才会。
            if ($ts.AddSeconds(1) -lt $SinceTime) { continue }
        }
        if ($matches[2] -eq 'FC6') { $res.Fc6++ } else { $res.Fc16++ }
    }
    $res.Total = $res.Fc6 + $res.Fc16
    return $res
}

function Get-WriteSideVerdict {
    <#
      ★ 写侧这一道闸的【判决逻辑】 ★
      单独抠成一个函数,是为了让 verify_soakw_write_side.ps1 能
      【原样抠出来喂假数据】—— 判决逻辑跟跑长跑是同一份代码,
      不是"测试里另写一遍"。

      判据表(2026-10-27 重做;2026-10-28 加"绝对值/增量"分工):
        窗口内一条统计行都没有     ⇒ UNKNOWN(看不见证据 ≠ 没问题)
        拒收缺陷 > 0【绝对值】       ⇒ FAIL  ★ 新增的拒收路径忘了记账 / 突变体
        拒收队列满 > 0【绝对值】     ⇒ FAIL  ★ 队列撑爆 = 有别的东西在灌请求
        恒等式①残差 ≠ 0             ⇒ FAIL  ★ 有发出去没回来的写(泄漏)
        恒等式②残差 ≠ 0             ⇒ FAIL  ★ 有排队写凭空消失
        恒等式③残差 ≠ 0             ⇒ FAIL  ★ 有拒收【连自己属于哪类都不知道】
        应用成功数 > 从站收到的写帧   ⇒ FAIL  ★ 应用在撒谎(用【增量】,窗口对齐)
        窗口里只有一条统计行         ⇒ UNKNOWN(验不全 —— 【不是】"没写过")
        窗口内【尝试】没涨(没写)   ⇒ UNKNOWN(本轮没发生写 ⇒ 验不到写侧通路)
        拒收未连/非法/发送 > 0      ⇒ 环境事件(单独报出来,不判失败)
        排队作废 > 0                ⇒ 环境事件(断线作废,正常)

      ★★★ 为什么坏路径按【绝对值】判,流量按【增量】判 ★★★
        坏路径(拒收缺陷/队列满)是【二进制的属性】:那条路坏了,
        它在窗口【之前】坏了也一样坏 ⇒ 该看【累计值】。
        流量(尝试/成功/已发出)量的是"这一轮里发生了什么" ⇒ 该看【增量】。
        ★ 而【增量】在窗口只有一条统计行时【算不出来】(末-首=0)。
          2026-10-28 实测踩到:恒拒收突变体第一拍只有一条行,
          明明写着"尝试=541 拒收缺陷=541",判据却报"写尝试 0 次(没写过)"
          —— 【事实相反的话】,而且方向是"看不见缺陷" ⇒ 假绿。
          ⇒ 现在分开:坏路径看绝对值(单行也判得出),流量看增量,
            算不出差时明说"验不全",【不再】说"没写过"。
    #>
    param($Stats, $SlaveWrites)

    $writeState = 'PASS'
    $writeReason = ''
    $writeEnvEvents = 0
    $writeEnvDetail = @()

    # ★★★ 绝对值视图(判【坏路径】用)★★★ ★
    #   为什么要绝对值:坏路径是【二进制的属性】,不是窗口的属性。
    #   那条路径坏了,它在窗口【之前】坏了也一样坏。
    #   ⇒ 而绝对值【在只有一条统计行时也成立】——
    #     这正是 2026-10-28 实测踩到的坑:
    #     恒拒收突变体的第一拍只有一条统计行(写着 尝试=541 拒收缺陷=541),
    #     而判据只算增量 ⇒ 增量 0 ⇒ 报"写尝试 0 次(没写过)" ⇒ 【事实相反的话】,
    #     而且方向是"看不见缺陷" ⇒ 假绿。
    #   ⇒ 现在:坏路径看绝对值,流量看增量;两者分开,谁也不冒充谁。
    $abs = $Stats.Abs
    $absDefect   = $abs.'拒收缺陷'
    $absQueueFull = $abs.'拒收队列满'
    # ★ 窗口增量【流量】:只有在增量可信时才用。
    #   ★ 交叉核对【必须】用增量,不能用绝对值 ——
    #     从站计数是【按窗口过滤】的(只数 settleTime 之后的帧),
    #     而绝对值成功数覆盖【进程一生】(包含静默期里写过的)。
    #     拿"一生"去比"窗口" ⇒ 静默期写过的那些【必然】对不上
    #     ⇒ 【必然假红】。2026-10-28 改绝对值时踩到,当场发现。
    #   ⇒ 交叉核对只在【增量可信】时做 —— 而单行窗口本来就要判 UNKNOWN,
    #     少做一次核对不会放过任何东西。
    $dOk = $Stats.'成功'

    if ($Stats.StatLines -eq 0) {
        # ★ 判 UNKNOWN,不是 PASS。
        #   "没看见统计行"和"没有写被拒"在日志上【长得一模一样】——
        #   而判据坏掉的样子,和程序坏掉的样子,一模一样。
        $writeState = 'UNKNOWN'
        $writeReason = ("★ 【看不见证据】—— 本轮窗口内一条写侧统计行都没有。`n" +
            "          ★ 写侧统计行是 INFO 级、默认就落地(ModbusMaster::onWriteStatsTick,60 秒一条)。`n" +
            "          ★ 没有它,可能是:跑的是【旧二进制】(修之前没有这行)/ 日志被截断 / 程序没建 ModbusMaster。`n" +
            "          ⇒ 『写被拒收 0 次』在这里【只说明没看见】,不说明没发生。这道闸本轮【不作数】。")
    } elseif ($absDefect -gt 0) {
        # ★★★ 坏路径:按【绝对值】判,而不是窗口增量 ★★★
        $writeState = 'FAIL'
        $writeReason = ("★ 写被【来源不明地】拒收 {0} 次 —— 有一条拒收路径【没记账】。`n" +
            "          ★ 每一个合法的拒收点都必须在 ModbusMaster 里写明原因;`n" +
            "            走到『缺陷』这一桶,说明有一条路径【漏了】—— 那可能是一条新的静默的洞。`n" +
            "          ★ 【值最后落地了】也掩盖不了它:重试机制(dirty 保留→下一拍重发)`n" +
            "            把缺陷盖住了,判据量的是【最终结果】,而 BUGFIX-E1 修的是【路径】。`n" +
            "          ★ 这个数是【绝对值】不是窗口增量 —— 坏路径是二进制的属性,`n" +
            "            它在窗口【之前】坏了也一样坏,所以不受窗口长度影响。`n" +
            "          累计:尝试 {1} / 接受 {2} / 拒收 {3} / 成功 {4} / 失败 {5} / 排队 {6}。") -f `
            $absDefect, $abs.'尝试', $abs.'接受', $abs.'拒收', `
            $abs.'成功', $abs.'失败', $abs.'排队'
    } elseif ($absQueueFull -gt 0) {
        $writeState = 'FAIL'
        $writeReason = ("★ 写队列被撑爆 {0} 次 —— 队列满了。`n" +
            "          ★ 正常情况下队列里最多 1 条(WriteScheduler 每台 PLC 同时只发一个写),`n" +
            "            走到 8 说明【有别的东西在往里灌请求】—— 这本身就是要查的事。`n" +
            "          累计:尝试 {1} / 接受 {2} / 拒收 {3} / 成功 {4}。") -f `
            $absQueueFull, $abs.'尝试', $abs.'接受', $abs.'拒收', $abs.'成功'
    } elseif ($Stats.Identity1Bad -gt 0) {
        $writeState = 'FAIL'
        $writeReason = ("★ 恒等式①不成立:{0} 组残留 {1}(已发出 != 成功 + 失败 + 在途写)。`n" +
            "          ★ 有【发出去却永远没回来】的写 ⇒ WriteScheduler 的 pending_ 泄漏`n" +
            "            ⇒ 那台 PLC 从此【再也不写任何东西】,而且零日志。`n" +
            "          ★ 形状跟 BUGFIX-E3 一模一样(异常响应路径不发 writeFinished)。") -f `
            $Stats.Identity1Bad, $Stats.Identity1Residual
    } elseif ($Stats.Identity2Bad -gt 0) {
        $writeState = 'FAIL'
        $writeReason = ("★ 恒等式②不成立:{0} 组残留 {1}(排队 != 排队发出 + 排队作废 + 在队)。`n" +
            "          ★ 有【排队写凭空消失】 ⇒ 调用方永远等一个不来的 writeFinished。") -f `
            $Stats.Identity2Bad, $Stats.Identity2Residual
    } elseif ($Stats.Identity3Bad -gt 0) {
        $writeState = 'FAIL'
        $writeReason = ("★ 恒等式③不成立:{0} 组残留 {1}(拒收 != 未连+非法+队列满+发送+缺陷)。`n" +
            "          ★ 程序说『我拒了 N 次』,那这 N 次必须都能在五个桶里找到归宿。`n" +
            "          ★ 差值不为零 ⇒ 有一次拒收【连自己属于哪一类都不知道】⇒ 有拒收路径漏了记账。") -f `
            $Stats.Identity3Bad, $Stats.Identity3Residual
    } elseif ($Stats.DeltaAvailable -and $null -ne $SlaveWrites -and $SlaveWrites.Available -and ($dOk -gt $SlaveWrites.Total)) {
        # ★ 交叉核对用【窗口增量】的成功数 —— 跟从站计数的窗口一致。
        #   ★ 只在增量可信时做:单行窗口的增量是 0,拿 0 去比不会漏,
        #     而下面那个分支本来就会判 UNKNOWN。
        $writeState = 'FAIL'
        $writeReason = ("★ 应用自称写成功 {0} 次,但从站只收到 {1} 个写帧(FC6 {2} / FC16 {3})。`n" +
            "          ★ 应用在撒谎,或者它把没落地的写算成了成功 —— 这道闸就是为这件事准备的。") -f `
            $dOk, $SlaveWrites.Total, $SlaveWrites.Fc6, $SlaveWrites.Fc16
    } elseif (-not $Stats.DeltaAvailable) {
        # ★★★ "算不出差" ≠ "没写过" ★★★
        #   窗口里【只有一条】统计行时,增量 = 末-首 = 0,
        #   而那【不是】"这一轮没发生写",只是"两条统计行之间没法做差"。
        #   ⇒ 以前这里直接报"写尝试 0 次(没写过)"—— 那是【事实相反的话】。
        #     2026-10-28 实测:恒拒收突变体第一拍明明写着"尝试=541",
        #     判据却报"0 次(没写过)" ⇒ 而"没写过"会让人以为"这轮不用管写侧"。
        #   ⇒ 现在分三种说:
        #     (a) 绝对值说"确实没写过"    ⇒ UNKNOWN(验不到)
        #     (b) 绝对值说"写过,但算不出差" ⇒ UNKNOWN(验不全),但把数报出来
        #     (c) 两条都有                ⇒ 正常走增量
        $absAttempts = $abs.'尝试'
        if ($absAttempts -le 0) {
            $writeState = 'UNKNOWN'
            $writeReason = ("★ 【验不到写侧】—— 这一轮【确实没写过】(累计尝试 0 次)。`n" +
                "          ★ 没发生写,就【没验到】写侧通路 —— 不等于写侧是好的。`n" +
                "          ⇒ 这道闸本轮【不作数】。")
        } else {
            $writeState = 'UNKNOWN'
            $writeReason = ("★ 【验不全写侧】—— 窗口里【只有一条】写侧统计行,增量算不出来。`n" +
                "          ★ 累计:尝试 {0} / 接受 {1} / 拒收 {2} / 成功 {3} / 失败 {4}。`n" +
                "          ★ 这些写【确实发生过】,但『这一轮里发生了什么』算不出来`n" +
                "            (增量 = 末行 - 首行,只有一条行时恒等于 0 —— 那【不代表】没发生写)。`n" +
                "          ★ 好消息:坏路径(拒收缺陷/队列满)按【绝对值】判,不受这条限制 ——`n" +
                "            上面没判红就说明【没有】来源不明的拒收。`n" +
                "          ⇒ 这道闸本轮【不作数】(验不全),但不是【看不见】。") -f `
                $absAttempts, $abs.'接受', $abs.'拒收', $abs.'成功', $abs.'失败'
        }
    } elseif ($Stats.'尝试' -le 0) {
        # ★ 这一轮压根没有写 ⇒ 写侧通路【没被走到过】⇒ 验不到。
        #   "没验到"和"没问题"是两回事 —— 报 UNKNOWN,不报 PASS。
        $writeState = 'UNKNOWN'
        $writeReason = ("★ 【验不到写侧】—— 本轮窗口内写尝试 {0} 次(没写过)。`n" +
            "          ★ 没发生写,就【没验到】写侧通路 —— 不等于写侧是好的。`n" +
            "          ⇒ 这道闸本轮【不作数】。") -f $Stats.'尝试'
    } else {
        $writeReason = ("★ 写侧通路正常:尝试 {0} / 接受 {1} / 拒收 {2}(其中缺陷 {3}、队列满 {4}) / " +
            "排队 {5}(排队发出 {6}、作废 {7}) / 已发出 {8} / 成功 {9} / 失败 {10};`n" +
            "          恒等式①②③残差 0。") -f `
            $Stats.'尝试', $Stats.'接受', $Stats.'拒收', $Stats.'拒收缺陷', `
            $Stats.'拒收队列满', $Stats.'排队', $Stats.'排队发出', $Stats.'排队作废', `
            $Stats.'已发出', $Stats.'成功', $Stats.'失败'
    }

    # ★ 环境事件:【不算缺陷】,但要【说出来】。
    #   24 小时里断一次网是正常的 —— 把它算成缺陷,这条判据就变成"天天误报",
    #   而天天误报的判据等于没有判据。
    #   ★ 用【绝对值】,跟坏路径一个道理:断线是【运行期的事件】,
    #     不是【窗口内的流量】—— 只有一条统计行时,增量算不出来,
    #     但那次断线【确实发生过】,不能因为算不出差就不说。
    if ($abs.'拒收未连' -gt 0) {
        $writeEnvEvents += $abs.'拒收未连'
        $writeEnvDetail += ("断线/未连接拒收 {0} 次" -f $abs.'拒收未连')
    }
    if ($abs.'拒收非法' -gt 0) {
        $writeEnvEvents += $abs.'拒收非法'
        $writeEnvDetail += ("参数/配置非法拒收 {0} 次" -f $abs.'拒收非法')
    }
    if ($abs.'拒收发送' -gt 0) {
        $writeEnvEvents += $abs.'拒收发送'
        $writeEnvDetail += ("发送失败拒收 {0} 次" -f $abs.'拒收发送')
    }
    if ($abs.'排队作废' -gt 0) {
        $writeEnvEvents += $abs.'排队作废'
        $writeEnvDetail += ("排队写作废 {0} 次(断线作废,正常事件)" -f $abs.'排队作废')
    }

    return [pscustomobject]@{
        State      = $writeState
        Reason     = $writeReason
        EnvEvents  = $writeEnvEvents
        EnvDetail  = $writeEnvDetail
    }
}

$writeStats = Get-WriteSideStats -Lines $sinceLines
# ★ 从站地面真相交叉核对 ★
#   应用说"写成功 N 次",从站说"我收到 M 个写帧"。
#   ★ 不变式:M >= N。
#     应用每一次"成功"都意味着从站【处理过】那一次写(它先写日志再回包),
#     而从站日志里的写帧还可能【多出】那些应用还没收到回包的、在途的、
#     或者回包超时后重试的 ⇒ 多出来是正常的,少一个都没有。
#   ★ 所以只有"M < N"(应用自称比从站收到的还多)才算 FAIL ——
#     那说明应用在撒谎,或者它把没落地的写算成了成功。
$slaveLogPath = Join-Path $OutDir 'slave.log'
$slaveWrites  = Get-SlaveWriteCount -SlaveLog $slaveLogPath -SinceTime $settleTime

$writeVerdict = Get-WriteSideVerdict -Stats $writeStats -SlaveWrites $slaveWrites
$writeState    = $writeVerdict.State
$writeReason   = $writeVerdict.Reason
$writeEnvEvents = $writeVerdict.EnvEvents
$writeEnvDetail = $writeVerdict.EnvDetail

# ★ 业务在跑的【唯一】判据:成功读计数在涨
$bizGrew = ($pollOkDelta -gt 0)

# 三种"没在干活",分开报 —— 它们指向的排查方向不一样,混成一句"idle"就废了
$isFrozen    = ((-not $logGrew) -and ($cpuPct -lt $MinCpuPercent))  # 不记日志且不吃 CPU:冻住了
$isBusyQuiet = ((-not $logGrew) -and ($cpuPct -ge $MinCpuPercent))  # 不记日志但在忙
$isDormant   = ($logGrew -and (-not $bizGrew))                       # 在长,但没有业务在跑
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
    #   判据换到"成功读计数"之后,这一支的含义也变了:
    #   不再是"业务类日志一条都没有"(那是旧口径,而且会假红),
    #   而是"【成功读计数一条没涨】"—— 业务确实没在跑。
    $memTxt += ("`n        ★ 只在自转:日志在长(净增 {0} KB),但【成功读计数一条没涨】。" -f [math]::Round($logGrewBytes/1KB,1))
    $memTxt += ("`n          轮询统计:发出 +{0},成功 +{1},在途变量 {2};平均 CPU {3:N2}%。" -f `
        $pollSentDelta, $pollOkDelta, $inflightLast, $cpuPct)
    $memTxt += "`n          进程活着,但它没在做业务 —— PLC 大概率没连上,轮询是停的。"
    $memTxt += "`n          这种状态内存当然不涨,所以本轮【不构成对程序稳定性的验收】,判 UNKNOWN。"
    $memTxt += "`n          ★ 2026-10-20 那 950.9 分钟就是在这一支上骗过去的:旧判据只看日志总量,"
    $memTxt += "`n            心跳把'总量在长'撑住了,于是给了 PASS。本次实测已复现。"
    $memTxt += "`n          查:PLC 地址是不是回环(127.0.0.1)?启动有没有带 --soak-user/--soak-pass?"
    $memTxt += "`n              日志里有没有 [ModbusMaster] 通讯错误?在途变量是不是不为 0?"
} elseif ($isBusyQuiet) {
    $memTxt += ("`n        进程在忙但不记业务日志(平均 CPU {0:N2}%) —— 本轮【无法判定活体】,不下结论。" -f $cpuPct)
} elseif ($cpuPct -lt $MinCpuPercent) {
    # ★ 这条分支要写出来,不然读报告的人会以为脚本判错了:
    #   CPU 明明低于 5% 阈值,怎么不判成没在干活?
    $memTxt += ("`n        CPU {0:N2}%(低于阈值 {1}%),但静默期之后【成功读 +{2} 次】 —— 判定资格成立。" -f `
        $cpuPct, $MinCpuPercent, $pollOkDelta)
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
# ★ 这一轮是不是【应用侧卡死】(而不是"从站没回包")—— 两者退出码不同,见文件末尾。
#   应用侧卡死的形态:发出数【不涨】而在途变量不为 0
#   —— 变量卡在在途态等一个永远不来的回包(2026-10-21 查出的根因)。
#   从站没回包的形态:发出数【在涨】、成功数不涨 —— 应用在正常重试,是环境问题。
$live        = Get-LivenessVerdict -Stats $stats -IsFrozen $isFrozen
$liveState   = $live.State
$soak6AppDefect = $live.AppDefect
$liveTxt += ("`n        轮询统计行 {0} 条;发出 +{1},成功 +{2},失败 +{3};在途变量 {4};累计 发出={5} 成功={6}" -f `
    $statsN, $pollSentDelta, $pollOkDelta, $pollFailDelta, $inflightLast, $sentTotalLast, $okTotalLast)
$liveTxt += "`n        " + $live.Reason
if ($sinceCatTxt -ne '(无)') {
    $liveTxt += ("`n        业务类按类别:" + $sinceCatTxt)
}
if ($noiseLines.Count -gt 0) {
    $noiseByCat = @($noiseLines | Group-Object Msg | Sort-Object Count -Descending |
                    Select-Object -First 3 @{n='m';e={$_.Name}}, @{n='n';e={$_.Count}})
    $liveTxt += "`n        噪声 Top3:" + (($noiseByCat | ForEach-Object { "$($_.m) x$($_.n)" }) -join ' | ')
}
Verdict 'SOAK-6' '业务活体' $liveState $liveTxt

# --- 3b. 写侧通路(SOAK-W)---
#  ★ 为什么它是【独立的一道闸】,不并进 SOAK-6 ★
#    SOAK-6 问的是"程序还活着、还在跟 PLC 通信吗"(读的事)。
#    SOAK-W 问的是"写有没有【真的、按该走的那条路】出去"(写的事)。
#    两个都绿才算这一轮通过 —— 写侧不通而读侧正常,那正是 BUGFIX-E1 修之前的形状。
#  ★ 退出码:SOAK-W 判 FAIL ⇒ exit 1(验出问题),跟 SOAK-2/3 一样。
#    判 UNKNOWN ⇒ exit 4(没验成),跟 SOAK-6 一个待遇 ——
#    "看不见证据"不能算通过,也不能算失败。
$writeTxt = $writeReason
# ★ 环境事件:【单独报出来】,不算缺陷。
#   24 小时里断一次网是正常的。把它算成缺陷 ⇒ 这条判据天天误报 ⇒ 没人信它。
#   但也不能默默吞掉 —— 那说明这一轮有环境问题,写侧的数字要在【这个前提下】读。
if ($writeEnvEvents -gt 0) {
    $writeTxt += ("`n        (环境事件,不算缺陷){0} —— 断线/参数非法/发送失败这类是【正常的环境抖动】," +
                  "不是应用的洞。但它说明这一轮有环境问题,上面的写侧数字要在【这个前提下】读。") -f `
                  (($writeEnvDetail) -join '、')
}
# ★ 排队通路有没有【真的被走到过】—— 这一条决定这道闸验的是什么。
if ($writeStats.'排队' -gt 0) {
    $writeTxt += ("`n        排队 {0} 次 —— 写被在途的读挡住过 {0} 次(排队发出 {1} / 作废 {2})。" +
                  "★ 这证明 BUGFIX-E1 那条排队通路在 24 小时里【真的被走到过】,不是空跑。") -f `
                  $writeStats.'排队', $writeStats.'排队发出', $writeStats.'排队作废'
} elseif ($writeState -eq 'PASS') {
    # ★ 2026-10-28:这段提示原来写的是"给从站加 --reply-delay-ms 120" ——
    #   ★ 那是被实测【推翻】的说法,留着会把下一个人引到错的方向。
    #     真实机制(见 docs/现状.md §六之六 9.7):
    #       写【排队】的唯一条件 = "发写那一刻,在途槽被读占着"。
    #       【下一读什么时候发】由 poll 决定;【写什么时候发】由报警 min_duration_ms 决定。
    #       ⇒ 杠杆是这两者的【先后】,不是从站回包延迟的大小。
    #       实测:poll=200 时,D=120 / D=250 连跑 6 轮约 360 次写,排队【全是 0】;
    #             poll=200 + m=190 ⇒ 排队 7~11/56~58;m=250 ⇒ 31/57。
    #   ⇒ 正确提示是"把 min_duration_ms 抬到逼近 poll",而不是"加延迟"。
    $writeTxt += ("`n        ★ 排队 0 次 —— 这一轮【没撞上】车(写从没被在途的读挡住)。" +
                  "⇒ 这一轮验的是『没撞车时写是好的』,【不是】『撞车时写也能落地』。" +
                  "⇒ 想验后者:把工程副本里报警的 min_duration_ms 抬到【逼近 poll(200ms)】" +
                  "(scripts\run_soak.ps1 -AlarmMinDurationMs 190,默认就是它)。" +
                  "★ 【不是】加从站回包延迟 —— 实测那不是杠杆(D=120/250 排队照样 0)。")
}
# ★ 从站地面真相交叉核对:应用说成功几次,从站说收到几个写帧。
if ($slaveWrites.Available) {
    $writeTxt += ("`n        从站交叉核对:应用成功 {0} 次,从站真收到 {1} 个写帧(FC6 {2} / FC16 {3})—— " +
                  "不变式『从站收到 >= 应用成功』成立。") -f `
                  $writeStats.'成功', $slaveWrites.Total, $slaveWrites.Fc6, $slaveWrites.Fc16
} else {
    # ★ 不装作通过:没找到从站日志 ⇒ 这一项【没执行】,不是"通过"。
    $writeTxt += ("`n        从站交叉核对【未执行】—— 本轮没找到从站日志({0})。" +
                  "★ 没执行 ≠ 通过。") -f $slaveLogPath
}
# ★ 日志被截断 ⇒ 窗口是半截的 ⇒ 任何基于它的结论都不作数。
if ($script:logTruncated) {
    $writeTxt += ("`n        ★★ 日志被截断(撞到 Get-LogSince 的上限)—— 窗口只覆盖了一部分," +
                  "上面的数字【只说明看得到的那一段】。")
}
# ★★ 2026-10-28:排队 0 时,光说"没撞上"不够 ——
#   要说清楚【撞车这条路这轮到底走不走得通】。
#   机制:写【排队】的唯一条件 = "发写那一刻,在途槽被读占着"。
#         【下一读什么时候发】由 poll 决定;【写什么时候发】由报警 min_duration_ms 决定。
#   ⇒ 判据用 r = m / poll 这个比值,分【三档】——
#     ★ 档位是【实测出来的】,不是推算(docs/现状.md §六之六 9.7/9.8):
#         r ≈ 0.48 (m=95, poll=200)  ⇒ 排队 0/360     ⇒ 撞车这条路【基本走不通】
#         r ≈ 0.95 (m=190,poll=200)  ⇒ 排队 12~19%   ⇒ 【走得通,但不是常态】
#         r ≈ 1.25 (m=250,poll=200)  ⇒ 排队 54%      ⇒ 【大概率撞上】
#     ⇒ ★ 关键:【m < poll 不等于撞不上】—— m=190 < 200 却照样撞 12~19%。
#       写滞后上一读回包与下一读发出滞后都有几十 ms 的抖动,两段区间会交叠。
#       所以这里【不能】用"m<poll ⇒ 走不通"的二分法(那会把 m=190 误判成走不通,
#       而 m=190 实测是撞得上的)。按比值分档才与实测一致。
#   ⇒ 读【这一轮实际用的那份工程】(副本),把 m 和 poll 摆出来。
$collideNote = ''
try {
    if ($Project -and (Test-Path $Project)) {
        $pj = Get-Content -LiteralPath $Project -Raw -Encoding UTF8 | ConvertFrom-Json
        $ms   = @($pj.alarms | ForEach-Object { [int]$_.min_duration_ms } | Sort-Object -Unique)
        $polls = @($pj.variables | ForEach-Object { [int]$_.poll_interval_ms } | Sort-Object -Unique)
        $commPoll = 0
        try { $commPoll = [int]$pj.communications.modbus.poll_interval_ms } catch { }
        if ($polls.Count -eq 0 -and $commPoll -gt 0) { $polls = @($commPoll) }
        $minM = if ($ms.Count -gt 0) { ($ms | Measure-Object -Minimum).Minimum } else { 0 }
        $minP = if ($polls.Count -eq 0 -and $commPoll -gt 0) { $commPoll }
                 else { ($polls | Measure-Object -Minimum).Minimum }
        $collideNote = ("工程实测节奏:报警 min_duration_ms = {0} ms,轮询 poll = {1} ms(r = {2:N2})。" -f `
                        ($ms -join '/'), ($polls -join '/'), ($minM / [double]$minP))
        if ($minM -gt 0 -and $minP -gt 0) {
            $r = $minM / [double]$minP
            # ★ 后半段的措辞【只在真的排队=0 时才成立】——
            #   排队>0 时说"本轮排队 0 有两种可能"就是胡说。
            $zeroTail = ($writeStats.'排队' -le 0)
            if ($r -lt 0.6) {
                $collideNote += ("`n        ★ r < 0.6 ⇒ 【撞车这条路基本走不通】(实测 r≈0.48 ⇒ 排队 0/360)。")
                if ($zeroTail) {
                    $collideNote += ("`n        ⇒ 本轮『排队 0』不是侥幸没撞上,是这条路压根没被走到 ⇒ " +
                                     "这一轮验的仍是『没撞车时写是好的』。")
                }
            } elseif ($r -le 1.0) {
                $collideNote += ("`n        ★ 0.6 <= r <= 1.0 ⇒ 【撞车这条路走得通,但不是常态】(实测 r≈0.95 ⇒ 排队 12~19%)。")
                if ($zeroTail) {
                    $collideNote += ("`n        ⇒ 本轮『排队 0』有两种可能:① 采样量不够(联动次数太少,期望撞上次数 < 1);" +
                                     "② 撞上了但没留痕。若本轮联动次数 >> 预期撞上次数,那就是 ②,得查。")
                }
            } else {
                $collideNote += ("`n        ★ r > 1.0 ⇒ 【大概率撞上】(实测 r≈1.25 ⇒ 排队 54%)。")
                if ($zeroTail) {
                    $collideNote += ("`n        ⇒ 本轮『排队 0』不正常 —— 写从来没有被在途的读挡住过,得查。")
                }
            }
        }
    }
} catch {
    $collideNote = "★ 读工程文件失败,没能核对本轮 m 与 poll 的关系(读不到就说明读不到,不做猜测)。"
}
if ($collideNote) { $writeTxt += ("`n        " + $collideNote) }
Verdict 'SOAK-W' '写侧通路' $writeState $writeTxt

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

# ★★ 2026-10-27:日志被截断 —— 单独一条横幅 ★★
#   这条【必须在最显眼的位置】:它影响的不是某一个判据,而是【所有】靠日志行的判据。
#   而截断的表现是"安静"—— 报告上看不出任何异常,只是结论少了半截。
#   ⇒ 退出码按 4 给(没验成),不许拿半截数据当结论。
if ($script:logTruncated) {
    Say "" ; Say "════════════════════════════════════════════════════" 'Red'
    Say "★★ 日志被截断了(Get-LogSince 撞到了行数上限)。" 'Red'
    Say "  后面还有内容【没被看到】—— 上面所有判据只覆盖了看得到的那一段。" 'Red'
    Say "  退出码按 4 给(没验成):拿半截数据下结论,就是本项目最核心的那个教训。" 'Red'
    Say "  查:是不是日志级别被打开了(hmi.json 里 default_level=debug?)。" 'Red'
    Say "      —— 2026-10-27 实测:开 hmi.comm=debug 会到 893 行/分钟,24 小时 129 万行。" 'Red'
    Say "════════════════════════════════════════════════════" 'Red'
}
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
# ★ 2026-10-21:报告里有没替换的占位符 —— 单独一条横幅,比 notQualified 更响。
#   因为它【不影响任何一个判据的结论】,所以上面的横幅不会替它说话;
#   而它影响的恰恰是"这些结论的数字对不对",那是最该被看见的一处。
if ($script:reportHasPlaceholder) {
    Say "" ; Say "════════════════════════════════════════════════════" 'Red'
    Say ("★ 报告里有【未替换的占位符】: {0}" -f $script:reportLeaked) 'Red'
    Say "  上面这些位置本该是数字,现在缺了 —— 判据的结论本身还在,但支撑它的数字没了。" 'Red'
    Say "  这份报告【不能当验收结论用】。退出码按 4 给(没验成)。" 'Red'
    Say "  这是 harness 自己的问题(多半是某个字符串少写了 -f),不是被测程序的故障。" 'Red'
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

# ★ 2026-10-27:SOAK-W(写侧通路)【不算】进 $failCount。
#   它有自己的退出码待遇,理由跟 SOAK-6 一样:
#     · 判 FAIL  ⇒ exit 1(验出问题)—— 写被拒收/泄漏是【应用缺陷】,不是环境。
#     · 判 UNKNOWN ⇒ exit 4(没验成)—— 看不见证据 ≠ 没问题。
#   ⇒ 混进 $failCount 的话,"没验成"会被当成"验出问题",两条结论共用一个码。
if ($writeState -eq 'FAIL') { exit 1 }
if ($failCount -gt 0) { exit 1 }
# ★ 2026-10-21:SOAK-6 判出【应用侧卡死】时走 exit 1(验出问题),不是 exit 4。
#   区别在于:发出数不涨 + 在途变量不为 0 ⇒ 变量卡在在途态,应用自己把自己锁死了;
#   而"发出在涨、成功不涨"只是从站没回包,应用在正常重试 —— 那是环境问题,归 exit 4。
if ($soak6AppDefect) { exit 1 }
# ★★ 2026-10-27:日志被截断 ⇒ 整个窗口是【半截】的 ⇒ exit 4(没验成)★★
#   这条比 exit 3(有告警)严重,但比 exit 1(验出故障)轻:
#   截断意味着"后面还有内容没被看到" ⇒ 所有靠日志行的判据都只看了半截
#   ⇒ 拿半截数据下结论,就是本项目最核心的那个教训。
#   ⇒ 放在 exit 1 之后、exit 3 之前。
if ($script:logTruncated) { exit 4 }
# ★ 2026-10-21:报告里有没替换的占位符 ⇒ 数字缺失 ⇒ 没法算"验过了" ⇒ exit 4。
#   放在这儿(而不是最后)的理由:exit 1(验出应用故障)比它严重,先给出去;
#   但它比 exit 3(有告警)严重 —— 缺数字的报告连"有告警"都算不上,那是没结论。
if ($script:reportHasPlaceholder) { exit 4 }
if ($warnCount -gt 0) { exit 3 }
# SOAK-6 FAIL = 没验成(进程没在跑业务),和 UNKNOWN 归为一类
if ($liveState -eq 'FAIL' -or $unkCount -gt 0) { exit 4 }
# ★ 2026-10-27:SOAK-W 判 UNKNOWN(窗口内没有写侧统计行 / 本轮没发生写 ⇒ 看不见证据)⇒ exit 4。
#   放在最后:它比 exit 3(有告警)还轻 —— 一轮有告警但写侧没验成,仍然不是"验过了"。
if ($writeState -eq 'UNKNOWN') { exit 4 }
exit 0

# ★ 2026-10-21:SOAK-6「业务活体」判 FAIL 走 exit 4,不进 exit 1。
#   它的含义是"这一轮没有资格下结论",不是"验出了故障"。
#   混进 failCount 的话,调用方会把"没验成"当成"验出问题",
#   两条完全不同的结论共用一个退出码,门禁就没法做了。
#   (上面的 $failCount 统计只认 SOAK-1/2/3,SOAK-6 在这里单独处理。)
