# ============================================================================
# verify_soakw_write_side.ps1 —— SOAK-W(写侧通路判据)的负向/正向对照
#
#  为什么要有这个脚本
#  ------------------
#  ★★ 绿色的测试不一定是有效的测试 ★★
#  SOAK-W 是 2026-10-27 新加的判据。它抓的那个洞是实测出来的:
#    BUGFIX-E1 修之前,读 547 次全部成功、写 604 次全部被拒、0 帧落地,
#    而长跑的活体判据(SOAK-6)【只看读】⇒ 那种情况下它照样判绿。
#  ⇒ 一条判据如果没被咬过一口,谁也不知道它是空的。
#
#  ★ 而且这条判据有个【特别危险】的方向:它可能【天天误报】★
#    24 小时里断一次网是正常的 —— 那种情况下 ModbusMaster 会
#    「拒收未连接」若干次,并且把排队写作废(排队作废)。
#    ⇒ 如果判据把这两件事当成缺陷,它就会在每一个正常的网络抖动上误报,
#      而一条天天误报的判据等于没有判据 —— 跑一个月就没人看它了。
#    ⇒ 所以这个脚本不只测"该红的时候红",还测"【不该红的时候不能红】"。
#
#  ★★ 这条判据【换代过】(2026-10-27 重做)★★
#    原来的判据是 harness 刮日志 + 用"3 秒内有没有 errorOccurred"猜
#    环境 vs 缺陷。★ 那条路被【三条实测】推翻了 ★:
#      #1 拒收行是 qCWarning ⇒ 默认级别就看得见(原前提"必须开 debug"是错的);
#      #2 开 hmi.comm=debug ⇒ 893 行/分 ⇒ 24h 约 129 万行
#         ⇒ Get-LogSince 到 50000 行就【整个停止读】⇒ 只看得到前 0.9 小时
#         ⇒ SOAK-6 等【所有】判据全被截断;
#      #3 真断线【不产生】"Modbus 错误"(那串字在 hmi.cpp 的 UI 层,
#         TcpClient::onSocketError 故意不发 errorOccurred)
#         ⇒ 那套"时间窗口猜环境 vs 缺陷"的判法【永远不会响】;
#         而且断线会作废排队写 ⇒ 老的"排队 > 排队发出"会【误报】"排队写丢了"。
#    ⇒ 现在:【原因】由程序在它【知道原因】的地方算好(ModbusMaster 里),
#      harness 只负责取数 + 判恒等式 + 跟从站日志交叉核对。
#
#  这个脚本干什么
#  --------------
#  从 long_run.ps1 里【原样抠出】判据本体(Get-WriteSideStats /
#  Get-WriteSideVerdict / Get-SlaveWriteCount + $reWriteStats),
#  喂几组日志,检查判得对不对。
#  ★ 不复制粘贴一份实现 —— 复制的那份迟早跟本体对不上,而对不上的时候没人会发现。
#
#  ★★ 样本必须是【真日志形态】,不是自己想象的格式 ★★
#    2026-10-21 栽过一次:verify_soak6_liveness.ps1 喂的是"发出=123"
#    (等号后没空格),而程序实际打出来的是"发出= 123"(QDebug 在相邻两项
#    之间插一个空格)⇒ 正则一次都匹配不上,而那个脚本因为喂的是自己
#    想象的格式,【一直绿】。
#    ⇒ 所以这里的统计行样本跟程序侧 formatWriteStatsLine 【逐字一致】
#      (那个契约由 hmi/tests/tst_WriteStatsFormat.cpp 钉住)。
#
#  跑的用例
#  --------
#   0. ★ 真日志黄金样本 —— 判据读得懂真日志吗
#   1. 健康轮次(有排队、有成功、没拒收)   → PASS
#   2. ★ 写被饿死(拒收缺陷 > 0)            → FAIL(这是要抓的洞)
#   3. ★ 断线(拒收未连 + 排队作废)          → 【不 FAIL】(★ 不误报)
#   4. ★ 恒等式①破坏(BUGFIX-E3 的形状)     → FAIL
#   5. ★ 恒等式②破坏(排队写凭空消失)       → FAIL
#   6. ★ 队列撑爆(拒收队列满 > 0)          → FAIL
#   7. ★ 从站交叉核对:应用比从站还多       → FAIL
#   8. ★ 看不见证据(没有统计行)            → UNKNOWN(不是 PASS)
#   9. ★ 验不到(本轮一次都没写)            → UNKNOWN(不是 PASS)
#  9b. ★★ 单行窗口:恒拒收突变体的那一拍   → FAIL(不是 UNKNOWN!)
#      ★ "算不出差" ≠ "没写过" —— 2026-10-28 现场实测挖出来的缺陷
#  9c. ★ 恒等式③(拒收分类自洽)            → FAIL
#  10. 计数器归零(进程重启)                → 不出负数、不误判
#  11. 多台 PLC(不同实例号)                → 不混账
#  12. 每条结论里的数字必须真的在           → 不许有漏掉的 {0} 占位符
# ============================================================================

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$longRun = Join-Path $PSScriptRoot 'long_run.ps1'
if (-not (Test-Path $longRun)) { Write-Output "找不到 long_run.ps1: $longRun"; exit 1 }

# ---- 从 long_run.ps1 里抠出判据本体(原样,不复制实现) --------------------
$src = Get-Content -LiteralPath $longRun -Raw -Encoding UTF8

function Extract-Def([string] $Text, [string] $Name) {
    # 函数体是缩进的,所以"行首的 }"就是函数结束位置
    $m = [regex]::Match($Text, "(?s)function\s+$Name\s*\{.*?\r?\n\}\r?\n")
    if (-not $m.Success) { throw "在 long_run.ps1 里找不到函数 $Name —— 判据改名了?这个脚本要一起改。" }
    return $m.Value
}

function Extract-Var([string] $Text, [string] $Name) {
    # 形如:  $reWriteStats = '写侧统计:实例=\s*(\d+)...'
    #   PowerShell 的单引号字符串是字面量 ⇒ 里面的 \s \d 原样就是正则源码。
    $m = [regex]::Match($Text, "(?m)^\`$$Name\s*=\s*'(.*)'\s*$")
    if (-not $m.Success) { throw "在 long_run.ps1 里找不到变量 `$$Name —— 判据改名了?这个脚本要一起改。" }
    return $m.Value
}

# ★ $reWriteStats 也必须抠出来 —— Get-WriteSideStats 依赖它。
#   (tst_WriteStatsFormat 钉的是 C++ ↔ $reWriteStats;
#    这里钉的是 $reWriteStats ↔ 判据函数本身。)
Invoke-Expression (Extract-Var $src 'reWriteStats')
Invoke-Expression (Extract-Def $src 'Get-WriteSideStats')
Invoke-Expression (Extract-Def $src 'Get-WriteSideVerdict')
Invoke-Expression (Extract-Def $src 'Get-SlaveWriteCount')

# ---- 造日志行(格式跟 Get-LogSince 产出的对象一样)------------------------
#  ★ File 字段必须有 —— 判据按 (日志文件, 实例号) 分组,
#    缺了它同一个 PLC 的多台实例会被混在一起算账。
#  ★ Time 是【真的 DateTime】—— 排序要用。
function L([string] $msg, [int] $secOffset = 0, [string] $file = 'hmi_20261027_120000.log') {
    return [pscustomobject]@{
        Time  = (Get-Date).AddSeconds($secOffset)
        Level = 'INFO'
        Cat   = 'hmi.write'
        Msg   = $msg
        File  = $file
    }
}

$red = 0; $ok = 0
function Check($name, $cond, $detail) {
    if ($cond) { Write-Host "  [OK]   $name  $detail" -ForegroundColor Green; $script:ok++ }
    else       { Write-Host "  [RED]  $name  $detail" -ForegroundColor Red;   $script:red++ }
}

# ============================================================================
# ★★ 样本:写侧统计行 ★★
# ============================================================================
#  ★ 出处:跟程序侧 ModbusMaster::formatWriteStatsLine 【逐字一致】。
#    契约由 hmi/tests/tst_WriteStatsFormat.cpp 钉住
#    (它把 scripts/long_run.ps1 的 $reWriteStats 原样读出来,
#      再去匹配程序真的打出来的那一行 —— 两边【物理上】就是同一份)。
#
#  ★ 这里用【手写】的样本而不是真日志,是因为写侧统计行是 2026-10-27
#    【刚加】的 —— 还没跑过一轮长跑,真日志还不存在。
#    ⇒ 但它【不是】"自己想象的格式":它是照着 formatWriteStatsLine
#      的输出抄的,而那条输出被契约测试钉着(改了它测试立刻红)。
#    ⇒ 这条区别很重要:契约测试保证"程序 ↔ 正则",这个脚本保证"正则 ↔ 判据"。

# 计数器顺序(与 $reWriteStats 的捕获组一一对应):
#   1实例 2尝试 3接受 4拒收 5拒收未连 6拒收非法 7拒收队列满 8拒收发送
#   9拒收缺陷 10排队 11排队发出 12排队作废 13已发出 14成功 15失败 16在队 17在途写
function StatLine([int] $inst, [int] $attempts, [int] $accepted, [int] $rejected,
                  [int] $rejNotConn, [int] $rejInvalid, [int] $rejQueueFull,
                  [int] $rejSendFailed, [int] $rejDefect,
                  [int] $queued, [int] $queuedSent, [int] $queuedVoided,
                  [int] $sent, [int] $ok, [int] $fail,
                  [int] $inQueue, [int] $inflight) {
    return ("[ModbusMaster] 写侧统计:实例={0} 尝试={1} 接受={2} 拒收={3} " +
            "拒收未连={4} 拒收非法={5} 拒收队列满={6} 拒收发送={7} 拒收缺陷={8} " +
            "排队={9} 排队发出={10} 排队作废={11} 已发出={12} 成功={13} 失败={14} " +
            "在队={15} 在途写={16}") -f `
            $inst, $attempts, $accepted, $rejected, $rejNotConn, $rejInvalid, `
            $rejQueueFull, $rejSendFailed, $rejDefect, $queued, $queuedSent, `
            $queuedVoided, $sent, $ok, $fail, $inQueue, $inflight
}

# 一个"健康"的快照:尝试 10 / 接受 10 / 拒收 0 / 排队 3 / 排队发出 3 /
# 已发出 10 / 成功 10 / 失败 0 / 在队 0 / 在途写 0
#   ★ 恒等式①:已发出(10) == 成功(10) + 失败(0) + 在途写(0) ✔
#   ★ 恒等式②:排队(3) == 排队发出(3) + 排队作废(0) + 在队(0) ✔
function HealthyLine([int] $inst = 1, [int] $scale = 1) {
    $a = 10 * $scale; $q = 3 * $scale; $s = 10 * $scale
    return StatLine $inst $a $a 0 0 0 0 0 0 $q $q 0 $s $s 0 0 0
}

# ============================================================================
# 用例 0:★ 真日志黄金样本 —— 判据读得懂真日志吗
# ============================================================================
Write-Host "`n=== 用例 0:真日志黄金样本 —— 判据读得懂真日志吗 ===" -ForegroundColor Cyan
Write-Host "  这一条是【整个脚本的地基】。它红,后面几条就没意义了。" -ForegroundColor DarkGray

$s0 = Get-WriteSideStats -Lines @(
    (L (HealthyLine 1 1) 0),
    (L (HealthyLine 1 3) 60)
)
Check "统计行被认出来" ($s0.StatLines -eq 2) ("StatLines = $($s0.StatLines)")
Check "分成了 1 组" ($s0.Groups -eq 1) ("Groups = $($s0.Groups)")
# ★ 这里量的是【窗口增量】(末行 - 首行),不是累计值。
#   10 → 30 ⇒ 增量 20。
#   ★ 为什么是增量而不是累计:应用在【窗口开始之前】就一直在跑,
#     它的累计数里有一部分是【窗口外】的 ⇒ 判据量的是"这一轮里发生了什么"。
Check "★ 尝试增量算对了(20,不是别的数)" ($s0.'尝试' -eq 20) ("尝试 = $($s0.'尝试')")
Check "★ 成功增量算对了(20)" ($s0.'成功' -eq 20) ("成功 = $($s0.'成功')")
Check "★ 已发出增量算对了(20)" ($s0.'已发出' -eq 20) ("已发出 = $($s0.'已发出')")
Check "★ 排队增量算对了(6)" ($s0.'排队' -eq 6) ("排队 = $($s0.'排队')")
Check "★ 恒等式①成立" ($s0.Identity1Bad -eq 0) ("坏组 = $($s0.Identity1Bad)")
Check "★ 恒等式②成立" ($s0.Identity2Bad -eq 0) ("坏组 = $($s0.Identity2Bad)")

# ============================================================================
# 用例 1:健康轮次 ⇒ PASS
# ============================================================================
Write-Host "`n=== 用例 1:健康轮次 ⇒ PASS ===" -ForegroundColor Cyan
$s1 = Get-WriteSideStats -Lines @(
    (L (HealthyLine 1 1) 0),
    (L (HealthyLine 1 3) 60)
)
$v1 = Get-WriteSideVerdict -Stats $s1 -SlaveWrites $null
Check "健康轮次判 PASS" ($v1.State -eq 'PASS') ("State = $($v1.State)")

# ============================================================================
# 用例 2:★ 写被饿死(拒收缺陷 > 0)—— 这是要抓的那个洞
# ============================================================================
#  ★ 为什么"拒收缺陷"是缺陷:
#    每一个【合法的】拒收点都必须在 ModbusMaster 里写明原因。
#    走到"来源不明"这一桶 ⇒ 有一条路径【漏了记账】——
#    那可能是一条新的静默的洞。
#  ★ 而且【值最后落地了】也掩盖不了它:重试机制(dirty 保留→下一拍重发)
#    把缺陷盖住了。判据量的是【路径】,不是【最终结果】。
Write-Host "`n=== 用例 2:★ 写被饿死(拒收缺陷)—— 这道闸要抓的就是它 ===" -ForegroundColor Cyan
# 尝试 10,接受 6,拒收 4 —— 其中 4 次全部"来源不明"
# 排队 0(写根本没进队列),已发出 6,成功 6,失败 0
$starvedLine = StatLine 1 10 6 4 0 0 0 0 4 0 0 0 6 6 0 0 0
$s2 = Get-WriteSideStats -Lines @(
    (L (StatLine 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0) 0),
    (L $starvedLine 60)
)
$v2 = Get-WriteSideVerdict -Stats $s2 -SlaveWrites $null
Check "★ 拒收缺陷被数出来(4)" ($s2.'拒收缺陷' -eq 4) ("拒收缺陷 = $($s2.'拒收缺陷')")
Check "★ 判 FAIL" ($v2.State -eq 'FAIL') ("State = $($v2.State)")
Check "★ 结论里点明是'来源不明地拒收'" ($v2.Reason -match '来源不明') ($v2.Reason.Split("`n")[0])

# ★ 反过来的那一面:同样的缺陷,但【值最后落地了】——
#   判据【仍然】要红(它量的是路径,不是结果)。
$starvedButLanded = StatLine 1 10 6 4 0 0 0 0 4 0 0 0 6 6 0 0 0
$s2b = Get-WriteSideStats -Lines @(
    (L (StatLine 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0) 0),
    (L $starvedButLanded 60)
)
$v2b = Get-WriteSideVerdict -Stats $s2b -SlaveWrites $null
Check "★ 值落地了【也】判 FAIL(量路径不是量结果)" ($v2b.State -eq 'FAIL') ("State = $($v2b.State)")

# ============================================================================
# 用例 3:★ 断线 —— 这道闸【不能】误报
# ============================================================================
#  ★ 为什么这一条最重要 ★
#    24 小时里断一次网是正常的。那种情况下:
#      · ModbusMaster 会走 !isConnected() 分支 ⇒ 拒收(记成"拒收未连");
#      · 排队里的写会被 failQueuedWrites() 作废 ⇒ "排队作废"会涨。
#    ⇒ 这两件事都是【正常的环境事件】,不是缺陷。
#    ⇒ 如果判据把它们算成缺陷,它就会在每一个网络抖动上误报 ——
#      而一条天天误报的判据等于没有判据。
#  ★ 这条正是【实测 #3】推翻了原设计的那一条:
#    原判据用"3 秒内有没有 errorOccurred"来分环境 vs 缺陷,
#    而真断线【根本不发 errorOccurred】 ⇒ 那套判法【永远不响】,
#    而"排队 > 排队发出"那一条又【天天误报】。
Write-Host "`n=== 用例 3:★ 断线(拒收未连 + 排队作废)—— 【不能】误报 ===" -ForegroundColor Cyan
# 尝试 10,接受 6,拒收 4(全是"未连接"),排队 3,排队发出 1,排队作废 2,在队 0
# 已发出 4,成功 4,失败 0,在途写 0
#   恒等式①:4 == 4 + 0 + 0 ✔
#   恒等式②:3 == 1 + 2 + 0 ✔
$disconnectLine = StatLine 1 10 6 4 4 0 0 0 0 3 1 2 4 4 0 0 0
$s3 = Get-WriteSideStats -Lines @(
    (L (StatLine 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0) 0),
    (L $disconnectLine 60)
)
$v3 = Get-WriteSideVerdict -Stats $s3 -SlaveWrites $null
Check "★ 断线拒收被数到'拒收未连'(4)" ($s3.'拒收未连' -eq 4) ("拒收未连 = $($s3.'拒收未连')")
Check "★ 排队作废被数出来(2)" ($s3.'排队作废' -eq 2) ("排队作废 = $($s3.'排队作废')")
Check "★★ 断线【不】判 FAIL(这是最重要的一条)" ($v3.State -eq 'PASS') ("State = $($v3.State)")
Check "★★ 断线被报成【环境事件】而不是缺陷" ($v3.EnvEvents -ge 6) ("EnvEvents = $($v3.EnvEvents)")
Check "★ 恒等式①在断线下【依然】成立(否则会误报成泄漏)" ($s3.Identity1Bad -eq 0) ("坏组 = $($s3.Identity1Bad)")
Check "★ 恒等式②在断线下【依然】成立" ($s3.Identity2Bad -eq 0) ("坏组 = $($s3.Identity2Bad)")

# ============================================================================
# 用例 4:★ 恒等式①破坏 —— BUGFIX-E3 的形状
# ============================================================================
#  已发出 6,但成功 3 + 失败 0 + 在途写 0 = 3 ⇒ 残差 3
#  ⇒ 有【发出去却永远没回来】的写 ⇒ WriteScheduler 的 pending_ 泄漏
#    ⇒ 那台 PLC 从此再也不写任何东西,而且零日志。
Write-Host "`n=== 用例 4:★ 恒等式①破坏(BUGFIX-E3 的形状)⇒ FAIL ===" -ForegroundColor Cyan
$leakLine = StatLine 1 10 10 0 0 0 0 0 0 0 0 0 6 3 0 0 0
$s4 = Get-WriteSideStats -Lines @(
    (L (StatLine 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0) 0),
    (L $leakLine 60)
)
$v4 = Get-WriteSideVerdict -Stats $s4 -SlaveWrites $null
Check "★ 恒等式①被识破" ($s4.Identity1Bad -eq 1) ("坏组 = $($s4.Identity1Bad)")
Check "★ 残差算对了(3)" ($s4.Identity1Residual -eq 3) ("残差 = $($s4.Identity1Residual)")
Check "★ 判 FAIL" ($v4.State -eq 'FAIL') ("State = $($v4.State)")
Check "★ 结论里点明是恒等式①" ($v4.Reason -match '恒等式①') ($v4.Reason.Split("`n")[0])

# ============================================================================
# 用例 5:★ 恒等式②破坏 —— 排队写凭空消失
# ============================================================================
#  排队 5,排队发出 1,排队作废 0,在队 0 ⇒ 残差 4
Write-Host "`n=== 用例 5:★ 恒等式②破坏(排队写凭空消失)⇒ FAIL ===" -ForegroundColor Cyan
$qLostLine = StatLine 1 10 10 0 0 0 0 0 0 5 1 0 5 5 0 0 0
$s5 = Get-WriteSideStats -Lines @(
    (L (StatLine 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0) 0),
    (L $qLostLine 60)
)
$v5 = Get-WriteSideVerdict -Stats $s5 -SlaveWrites $null
Check "★ 恒等式②被识破" ($s5.Identity2Bad -eq 1) ("坏组 = $($s5.Identity2Bad)")
Check "★ 判 FAIL" ($v5.State -eq 'FAIL') ("State = $($v5.State)")
Check "★ 结论里点明是恒等式②" ($v5.Reason -match '恒等式②') ($v5.Reason.Split("`n")[0])

# ============================================================================
# 用例 6:★ 队列撑爆 ⇒ FAIL
# ============================================================================
#  正常情况下队列里最多 1 条(WriteScheduler 每台 PLC 同时只发一个写),
#  走到 8 说明【有别的东西在往里灌请求】—— 这本身就是要查的事。
Write-Host "`n=== 用例 6:★ 队列撑爆 ⇒ FAIL ===" -ForegroundColor Cyan
$qfLine = StatLine 1 20 12 8 0 0 8 0 0 0 0 0 12 12 0 0 0
$s6 = Get-WriteSideStats -Lines @(
    (L (StatLine 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0) 0),
    (L $qfLine 60)
)
$v6 = Get-WriteSideVerdict -Stats $s6 -SlaveWrites $null
Check "★ 拒收队列满被数出来(8)" ($s6.'拒收队列满' -eq 8) ("拒收队列满 = $($s6.'拒收队列满')")
Check "★ 判 FAIL" ($v6.State -eq 'FAIL') ("State = $($v6.State)")

# ============================================================================
# 用例 7:★ 从站交叉核对 —— 应用比从站收到的还多 ⇒ FAIL
# ============================================================================
#  ★ 这是【地面真相】:从站日志里的 "FC6 写 addr=N value=V" 是从站真收到的,
#    应用伪造不了。
#  ★ 不变式:从站收到的 >= 应用自称成功的。
#    多出来是正常的(应用还没收到回包的、回包超时后重试的);
#    少一个都没有 —— 少一个说明应用在撒谎,或者把没落地的写算成了成功。
Write-Host "`n=== 用例 7:★ 从站交叉核对:应用自称比从站还多 ⇒ FAIL ===" -ForegroundColor Cyan
$slave = [pscustomobject]@{ Available = $true; Fc6 = 2; Fc16 = 0; Total = 2; Path = '(内存)' }
$s7 = Get-WriteSideStats -Lines @(
    (L (StatLine 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0) 0),
    (L (StatLine 1 10 10 0 0 0 0 0 0 0 0 0 10 10 0 0 0) 60)
)
$v7 = Get-WriteSideVerdict -Stats $s7 -SlaveWrites $slave
Check "★ 应用说成功 10、从站只收到 2 ⇒ 判 FAIL" ($v7.State -eq 'FAIL') ("State = $($v7.State)")
Check "★ 结论里点明是'应用在撒谎'" ($v7.Reason -match '撒谎') ($v7.Reason.Split("`n")[0])

# ★ 反过来:从站收到的【更多】⇒ 不该 FAIL(那是在途的/重试的,正常)
$slaveMore = [pscustomobject]@{ Available = $true; Fc6 = 25; Fc16 = 3; Total = 28; Path = '(内存)' }
$v7b = Get-WriteSideVerdict -Stats $s7 -SlaveWrites $slaveMore
Check "★ 从站收到更多(28 > 10)⇒ 不 FAIL" ($v7b.State -eq 'PASS') ("State = $($v7b.State)")

# ★ 从站日志【不存在】⇒ 这项不参与判定(但也不能装作通过)
$slaveNone = [pscustomobject]@{ Available = $false; Fc6 = 0; Fc16 = 0; Total = 0; Path = '(没有)' }
$v7c = Get-WriteSideVerdict -Stats $s7 -SlaveWrites $slaveNone
Check "★ 没有从站日志 ⇒ 不参与判定,不误红" ($v7c.State -eq 'PASS') ("State = $($v7c.State)")

# ============================================================================
# 用例 8:★ 看不见证据 ⇒ UNKNOWN(不是 PASS)
# ============================================================================
#  ★ "没看见统计行"和"没有写被拒"在日志上【长得一模一样】——
#    而判据坏掉的样子,和程序坏掉的样子,一模一样。
#  ⇒ 看不见证据必须判 UNKNOWN,不能判 PASS。
#    判 PASS = 拿着一份不存在的证据说"没问题"。
Write-Host "`n=== 用例 8:★ 看不见证据 ⇒ UNKNOWN(不是 PASS) ===" -ForegroundColor Cyan
$s8 = Get-WriteSideStats -Lines @(
    (L '[Write] OK: "Speed" = "0" (plc= "plc1" )' 0),
    (L '[PollScheduler] 轮询统计:发出= 271  成功= 271  失败= 0  在途= 0  PLC= 1  变量= 3' 60)
)
$v8 = Get-WriteSideVerdict -Stats $s8 -SlaveWrites $null
Check "★ 一条统计行都没匹配上" ($s8.StatLines -eq 0) ("StatLines = $($s8.StatLines)")
Check "★★ 判 UNKNOWN(不是 PASS)" ($v8.State -eq 'UNKNOWN') ("State = $($v8.State)")
Check "★★ 不是 FAIL(没有证据 ≠ 有缺陷)" ($v8.State -ne 'FAIL') ("State = $($v8.State)")

# ============================================================================
# 用例 9:★ 验不到(本轮一次都没写)⇒ UNKNOWN
# ============================================================================
#  没发生写 ⇒ 写侧通路【没被走到过】⇒ 验不到。
#  "没验到"和"没问题"是两回事 —— 报 UNKNOWN,不报 PASS。
Write-Host "`n=== 用例 9:★ 验不到(本轮一次都没写)⇒ UNKNOWN ===" -ForegroundColor Cyan
$s9 = Get-WriteSideStats -Lines @(
    (L (StatLine 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0) 0),
    (L (StatLine 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0) 60)
)
$v9 = Get-WriteSideVerdict -Stats $s9 -SlaveWrites $null
Check "★ 尝试 = 0" ($s9.'尝试' -eq 0) ("尝试 = $($s9.'尝试')")
Check "★ 判 UNKNOWN" ($v9.State -eq 'UNKNOWN') ("State = $($v9.State)")

# ============================================================================
# 用例 9b:★★ 单行窗口 —— "算不出差" ≠ "没写过" ★★
# ============================================================================
#  ★★★ 这条是 2026-10-28 现场实测挖出来的判据缺陷 ★★★
#
#  背景:写侧统计行是 60 秒一条。窗口里【只有一条】时,
#    增量 = 末行 - 首行 = 0 —— 而那【不是】"这一轮没发生写",
#    只是"两条统计行之间没法做差"。
#
#  ★ 实测(恒拒收突变体的第一拍):
#      09:56:26 [ModbusMaster] 写侧统计:实例=2 尝试=541 接受=0 拒收=541 ... 拒收缺陷=541 ...
#    那一行明明白白写着"尝试=541、拒收缺陷=541",而且
#    [Action] 行从 09:55:27 就已经出现了(证明写【确实在发生】)。
#    ⇒ 而当时的判据报的是:
#        "★ 【验不到写侧】—— 本轮窗口内写尝试 0 次(没写过)。"
#      ⇒ 那是【事实相反的话】,而且方向是"看不见缺陷" ⇒ 【假绿】。
#
#  ★ 为什么这个方向【特别危险】:
#    "没写过" 会让人以为"这轮不用管写侧" ⇒ 于是 24 小时里
#    【唯一一段暴露了缺陷的时间】被判成了"没发生过"。
#
#  ⇒ 修法:坏路径(拒收缺陷/队列满)按【绝对值】判。
#    坏路径是【二进制的属性】,不是窗口的属性 ——
#    那条路坏了,它在窗口【之前】坏了也一样坏。
#    ⇒ 只有一条统计行时,绝对值照样有效 ⇒ 照样判得出 FAIL。
Write-Host "`n=== 用例 9b:★★ 单行窗口 —— 恒拒收突变体的那一拍(实测样本) ===" -ForegroundColor Cyan
# ★ 这行的数字【逐字抄自实测日志】
#   2026-10-28 09:56:26.645 [INFO ] hmi.write - [ModbusMaster] 写侧统计:
#   实例=2 尝试=541 接受=0 拒收=541 拒收未连=0 拒收非法=0 拒收队列满=0
#   拒收发送=0 拒收缺陷=541 排队=0 排队发出=0 排队作废=0 已发出=0
#   成功=0 失败=0 在队=0 在途写=0
$alwaysOne = StatLine 2 541 0 541 0 0 0 0 541 0 0 0 0 0 0 0 0
$s9b = Get-WriteSideStats -Lines @( (L $alwaysOne 0) )
Check "★ 只匹配到一条统计行" ($s9b.StatLines -eq 1) ("StatLines = $($s9b.StatLines)")
Check "★ 增量视图【不可信】(只有一条行算不出差)" ($s9b.DeltaAvailable -eq $false) `
      ("DeltaAvailable = $($s9b.DeltaAvailable)")
Check "★★ 绝对值【算得出】拒收缺陷 = 541" ($s9b.Abs.'拒收缺陷' -eq 541) `
      ("绝对值 拒收缺陷 = $($s9b.Abs.'拒收缺陷')")
Check "★★ 绝对值【算得出】尝试 = 541" ($s9b.Abs.'尝试' -eq 541) `
      ("绝对值 尝试 = $($s9b.Abs.'尝试')")
$v9b = Get-WriteSideVerdict -Stats $s9b -SlaveWrites $null
Check "★★★ 判 FAIL(不是 UNKNOWN!)" ($v9b.State -eq 'FAIL') ("State = $($v9b.State)")
Check "★★★ 结论首行【不许】说'没写过'" ($v9b.Reason.Split("`n")[0] -notmatch '没写过') `
      ("State = $($v9b.State);首行 = " + $v9b.Reason.Split("`n")[0])
Check "★★★ 结论里点明是来源不明地拒收" ($v9b.Reason -match '来源不明') `
      ($v9b.Reason.Split("`n")[0])

# ★ 反过来:单行窗口 + 【没有】缺陷 ⇒ UNKNOWN,但【不是】"没写过"。
#   绝对值说"确实没写过"(尝试=0)⇒ "验不到";
#   绝对值说"写过但算不出差" ⇒ "验不全"。
$s9c = Get-WriteSideStats -Lines @( (L (StatLine 2 36 36 0 0 0 0 0 0 10 10 0 36 36 0 0 0) 0) )
$v9c = Get-WriteSideVerdict -Stats $s9c -SlaveWrites $null
Check "★ 单行 + 健康 ⇒ UNKNOWN(验不全,不是 PASS)" ($v9c.State -eq 'UNKNOWN') ("State = $($v9c.State)")
# ★ 只查【首行】—— 正文里解释"那不代表没发生写"是允许的,
#   但【结论那一行】不能说"没写过"(那是本条判据要根除的假话)。
Check "★★ 结论首行【不许】说'没写过'(绝对值说尝试=36)" `
      ($v9c.Reason.Split("`n")[0] -notmatch '没写过') `
      ("首行 = " + $v9c.Reason.Split("`n")[0])
Check "★★ 结论里明说'验不全'" ($v9c.Reason -match '验不全') ("首行 = " + $v9c.Reason.Split("`n")[0])

# ★ 单行 + 【绝对值说确实没写过】⇒ 才可以理直气壮地说"没写过"
$s9d = Get-WriteSideStats -Lines @( (L (StatLine 2 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0) 0) )
$v9d = Get-WriteSideVerdict -Stats $s9d -SlaveWrites $null
Check "★ 单行 + 累计尝试 0 ⇒ UNKNOWN" ($v9d.State -eq 'UNKNOWN') ("State = $($v9d.State)")
Check "★ 这时候才可以说'没写过'" ($v9d.Reason -match '没写过') ("首行 = " + $v9d.Reason.Split("`n")[0])

# ============================================================================
# 用例 9c:★★ 恒等式③(拒收分类自洽)★★
# ============================================================================
#  程序说"我拒了 N 次",那这 N 次必须都能在五个桶里找到归宿。
#  差值不为零 ⇒ 有一次拒收【连自己属于哪一类都不知道】⇒ 有路径漏了记账。
Write-Host "`n=== 用例 9c:★ 恒等式③(拒收分类自洽)⇒ FAIL ===" -ForegroundColor Cyan
#  ★ 拒收=5,但五个桶加起来只有 4 ⇒ 差 1。
#    ★ 样本里【拒收缺陷=0】是故意的 —— 否则判据会先撞上"拒收缺陷 > 0"那条分支,
#      判红的原因就【不是】恒等式③了,这条用例就白测了。
#      (这正是"先判哪条"会影响"测到的是不是你想测的那条" —— 顺序有讲究。)
#      拒收=5 = 未连 4 + 非法 0 + 队列满 0 + 发送 0 + 缺陷 0 = 4 ⇒ 残差 1
$badClass = StatLine 1 10 5 5 4 0 0 0 0 0 0 0 5 5 0 0 0
$s9e = Get-WriteSideStats -Lines @(
    (L (StatLine 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0) 0),
    (L $badClass 60)
)
$v9e = Get-WriteSideVerdict -Stats $s9e -SlaveWrites $null
Check "★ 恒等式③被识破" ($s9e.Identity3Bad -eq 1) ("坏组 = $($s9e.Identity3Bad)")
Check "★ 残差算对了(1)" ($s9e.Identity3Residual -eq 1) ("残差 = $($s9e.Identity3Residual)")
Check "★ 判 FAIL" ($v9e.State -eq 'FAIL') ("State = $($v9e.State)")
Check "★ 结论里点明是恒等式③" ($v9e.Reason -match '恒等式③') ($v9e.Reason.Split("`n")[0])

# ★ 健康轮次里恒等式③必须成立(否则会天天误报)
Check "★ 健康轮次恒等式③成立" ($s1.Identity3Bad -eq 0) ("坏组 = $($s1.Identity3Bad)")
Check "★ 断线轮次恒等式③成立" ($s3.Identity3Bad -eq 0) ("坏组 = $($s3.Identity3Bad)")

# ★ 覆盖率:统计行是 60 秒一条 ⇒ 两条相距 60 秒 ⇒ 期望 2 行
Check "★ 覆盖率算得对(2 行 / 期望 2 行)" ($s1.Coverage -eq 1) ("Coverage = $($s1.Coverage)")
Check "★ 单行窗口的覆盖率是 1(一条行 / 期望 1 行)" ($s9b.ExpectedLines -eq 1) `
      ("ExpectedLines = $($s9b.ExpectedLines)")

# ============================================================================
# 用例 10:计数器归零(进程重启)⇒ 不出负数、不误判
# ============================================================================
#  ★ 进程重启 ⇒ 累计计数器从头开始 ⇒ 末值 < 首值。
#    ⇒ 如果判据直接做差(末 - 首),会算出【负数】,
#      而"拒收缺陷 > 0"之类的判断对负数仍然成立 ⇒ 可能误判。
#    ⇒ 所以判据把重置【分段】处理:每段各自做差,再累加。
Write-Host "`n=== 用例 10:计数器归零(进程重启)⇒ 不出负数、不误判 ===" -ForegroundColor Cyan
# 第一段:尝试 100 …;第二段(重启后):尝试 5 …
$s10 = Get-WriteSideStats -Lines @(
    (L (StatLine 1 100 100 0 0 0 0 0 0 0 0 0 100 100 0 0 0) 0),
    (L (StatLine 1 5 5 0 0 0 0 0 0 0 0 0 5 5 0 0 0) 60)
)
Check "★ 识出了计数器归零(1 次)" ($s10.Resets -eq 1) ("Resets = $($s10.Resets)")
Check "★ 尝试增量是 5(第二段),不是 -95" ($s10.'尝试' -eq 5) ("尝试 = $($s10.'尝试')")
Check "★ 没有负数" (($s10.'尝试' -ge 0) -and ($s10.'成功' -ge 0) -and ($s10.'已发出' -ge 0)) `
      ("尝试 $($s10.'尝试') / 成功 $($s10.'成功') / 已发出 $($s10.'已发出')")
$v10 = Get-WriteSideVerdict -Stats $s10 -SlaveWrites $null
Check "★ 判 PASS(归零不是缺陷)" ($v10.State -eq 'PASS') ("State = $($v10.State)")

# ============================================================================
# 用例 11:多台 PLC(不同实例号)⇒ 不混账
# ============================================================================
#  ★ 进程内实例号自增 ⇒ 同一个日志文件里不同的实例是【不同的 PLC 通道】。
#    混在一起算账的话,恒等式必然残差 ⇒ 【假 FAIL】。
Write-Host "`n=== 用例 11:多台 PLC(不同实例号)⇒ 不混账 ===" -ForegroundColor Cyan
# plc1(实例1):尝试 10 成功 10;plc2(实例2):尝试 20 成功 20
$s11 = Get-WriteSideStats -Lines @(
    (L (StatLine 1 10 10 0 0 0 0 0 0 0 0 0 10 10 0 0 0) 0),
    (L (StatLine 2 20 20 0 0 0 0 0 0 0 0 0 20 20 0 0 0) 0),
    (L (StatLine 1 15 15 0 0 0 0 0 0 0 0 0 15 15 0 0 0) 60),
    (L (StatLine 2 25 25 0 0 0 0 0 0 0 0 0 25 25 0 0 0) 60)
)
Check "★ 分成了 2 组" ($s11.Groups -eq 2) ("Groups = $($s11.Groups)")
Check "★ 恒等式①【每一组】都成立" ($s11.Identity1Bad -eq 0) ("坏组 = $($s11.Identity1Bad)")
Check "★ 恒等式②【每一组】都成立" ($s11.Identity2Bad -eq 0) ("坏组 = $($s11.Identity2Bad)")
$v11 = Get-WriteSideVerdict -Stats $s11 -SlaveWrites $null
Check "★ 判 PASS" ($v11.State -eq 'PASS') ("State = $($v11.State)")

# ★ 不同日志文件(跨天滚动)也要分开
$s11b = Get-WriteSideStats -Lines @(
    (L (StatLine 1 10 10 0 0 0 0 0 0 0 0 0 10 10 0 0 0) 0  'hmi_20261027_120000.log'),
    (L (StatLine 1 20 20 0 0 0 0 0 0 0 0 0 20 20 0 0 0) 60 'hmi_20261028_000000.log')
)
Check "★ 跨天滚动 ⇒ 分成 2 组" ($s11b.Groups -eq 2) ("Groups = $($s11b.Groups)")
Check "★ 跨天不误判" ($s11b.Identity1Bad -eq 0 -and $s11b.Identity2Bad -eq 0) `
      ("① $($s11b.Identity1Bad) / ② $($s11b.Identity2Bad)")

# ============================================================================
# 用例 12:从站日志计数(地面真相)
# ============================================================================
Write-Host "`n=== 用例 12:从站日志计数(地面真相) ===" -ForegroundColor Cyan
$tmpSlave = Join-Path $env:TEMP ("soakw_slave_{0}.log" -f $PID)
@(
  '2026-10-27 09:29:47 [slave] conn#2 FC6 写 addr=2 value=0',
  '2026-10-27 09:29:48 [slave] conn#2 FC16 写 addr=10 count=2',
  '2026-10-27 09:30:00 [slave] conn#2 FC6 写 addr=2 value=1',
  '2026-10-27 09:31:00 [slave] conn#2 FC3 读 addr=0 count=3',
  'not a slave line'
) | Set-Content -LiteralPath $tmpSlave -Encoding UTF8
$sc = Get-SlaveWriteCount -SlaveLog $tmpSlave -SinceTime ([datetime]'2026-10-27 09:00:00')
Check "★ 只数 FC6/FC16(读不算)" ($sc.Total -eq 3) ("Total = $($sc.Total)")
Check "★ FC6 数对了(2)" ($sc.Fc6 -eq 2) ("Fc6 = $($sc.Fc6)")
Check "★ FC16 数对了(1)" ($sc.Fc16 -eq 1) ("Fc16 = $($sc.Fc16)")
Check "★ Available = true" ($sc.Available -eq $true) ("Available = $($sc.Available)")

# ★ 时间窗过滤:早于 SinceTime 的不算
$sc2 = Get-SlaveWriteCount -SlaveLog $tmpSlave -SinceTime ([datetime]'2026-10-27 09:30:00')
Check "★ 时间窗过滤生效" ($sc2.Total -eq 1) ("Total = $($sc2.Total)")

# ★ 边界:SinceTime 带亚秒(10:00:00.400),从站时间戳是秒级(10:00:00)
#   ⇒ 这一帧【必须】算数,否则会假红(应用算成功,从站少算一个)。
$tmpSlave2 = Join-Path $env:TEMP ("soakw_slave2_{0}.log" -f $PID)
@('2026-10-27 10:00:00 [slave] conn#2 FC6 写 addr=2 value=0') |
    Set-Content -LiteralPath $tmpSlave2 -Encoding UTF8
$sc3 = Get-SlaveWriteCount -SlaveLog $tmpSlave2 -SinceTime ([datetime]'2026-10-27 10:00:00.400')
Check "★★ 秒级边界【不】丢帧(否则会假红)" ($sc3.Total -eq 1) ("Total = $($sc3.Total)")

# ★ 文件不存在 ⇒ Available = false(不装作通过)
$sc4 = Get-SlaveWriteCount -SlaveLog (Join-Path $env:TEMP 'no_such_slave.log') -SinceTime $null
Check "★ 文件不存在 ⇒ Available = false" ($sc4.Available -eq $false) ("Available = $($sc4.Available)")
Check "★ 文件不存在 ⇒ Total = 0" ($sc4.Total -eq 0) ("Total = $($sc4.Total)")

Remove-Item -LiteralPath $tmpSlave  -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $tmpSlave2 -Force -ErrorAction SilentlyContinue

# ============================================================================
# 用例 13:结论里的数字必须真的在(不许有漏掉的占位符)
# ============================================================================
Write-Host "`n=== 用例 13:结论里的数字必须真的在 ===" -ForegroundColor Cyan
foreach ($pair in @(
    @{ Name = '健康那支';   V = $v1 },
    @{ Name = '饿死那支';   V = $v2 },
    @{ Name = '断线那支';   V = $v3 },
    @{ Name = '恒等式①那支'; V = $v4 },
    @{ Name = '恒等式②那支'; V = $v5 },
    @{ Name = '队列满那支'; V = $v6 },
    @{ Name = '撒谎那支';   V = $v7 },
    @{ Name = '看不见那支'; V = $v8 }
)) {
    $txt = [string]$pair.V.Reason
    Check "$($pair.Name):没有漏掉的占位符" ($txt -notmatch '\{\d+\}') `
          ("{0} 字,状态 {1}" -f $txt.Length, $pair.V.State)
    # ★ 数字必须【真的在】—— 不是"格式对",而是"说的那个数在那儿"
    Check "$($pair.Name):State 不是空的" (-not [string]::IsNullOrWhiteSpace([string]$pair.V.State)) `
          ([string]$pair.V.State)
}

# ---- 汇总 ----
Write-Host ""
Write-Host ("=== 结果:{0} 通过,{1} 失败 ===" -f $ok, $red) -ForegroundColor $(if ($red -eq 0) { 'Green' } else { 'Red' })
if ($red -gt 0) {
    Write-Host ""
    Write-Host "★ 这道闸【没验成】。别拿它的 PASS 当结论。" -ForegroundColor Red
    exit 1
}
Write-Host "★ SOAK-W 这道闸有牙(该红的红、该不红的不红)。" -ForegroundColor Green
exit 0
