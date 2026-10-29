# ============================================================================
# soakw_field_check.ps1 —— 把【真实的写侧统计行】喂给【真实的 SOAK-W 判据】
#
#  为什么要有这个脚本
#  ------------------
#  verify_soakw_write_side.ps1 验的是【判据逻辑】:
#    它喂的是【手写的】统计行样本 ⇒ 证明"判据本身判得对不对"。
#  tst_WriteStatsFormat 验的是【两方契约】:
#    它证明"程序打出来的那一行"能被 $reWriteStats 匹配上。
#
#  ★ 但这两者之间还差【一环】★
#    "程序在【真实故障】下打出来的行" → "判据读到它 → 判出 FAIL"
#    这一环没人验过。
#  ⇒ 而它恰恰是【最容易出事】的一环:
#    判据坏掉的样子,和程序坏掉的样子,一模一样(都是"没报红")。
#
#  ★★ 本脚本就是补这一环的 ★★
#    它【不复制判据实现】—— 从 long_run.ps1 里原样抠出
#    Get-WriteSideStats / Get-WriteSideVerdict / Get-SlaveWriteCount,
#    然后喂【磁盘上真实存在的日志文件】。
#
#  用法:
#    powershell -File scripts\soakw_field_check.ps1 -LogDir <应用数据目录>\logs
#    powershell -File scripts\soakw_field_check.ps1 -LogDir <...>\logs -SlaveLog <...>\slave.log
#
#  退出码(跟 long_run.ps1 的梯子一致):
#    0 = PASS(写侧通路正常)
#    1 = FAIL(验出问题)
#    4 = UNKNOWN(看不见证据 —— 跟"没问题"是两回事)
#
#  ★ 只读日志,不碰任何进程、不碰 git。★
# ============================================================================

[CmdletBinding()]
param(
    # 应用日志目录(里面是 hmi_yyyyMMdd_HHmmss.log)
    [Parameter(Mandatory = $true)]
    [string] $LogDir,

    # 从站日志(可选)。给了就做地面真相交叉核对。
    [string] $SlaveLog = '',

    # 只看这个时刻之后的日志(可选,格式 'yyyy-MM-dd HH:mm:ss')
    [string] $Since = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$longRun = Join-Path $PSScriptRoot 'long_run.ps1'
if (-not (Test-Path $longRun)) { Write-Output "找不到 long_run.ps1: $longRun"; exit 4 }

$src = Get-Content -LiteralPath $longRun -Raw -Encoding UTF8

# ---- 从 long_run.ps1 里【原样抠出】判据本体 ------------------------------
#   ★ 复制粘贴一份实现是本项目反复吃亏的地方:
#     复制的那份迟早跟本体对不上,而对不上的时候【没人会发现】。
function Extract-Def([string] $Text, [string] $Name) {
    $m = [regex]::Match($Text, "(?s)function\s+$Name\s*\{.*?\r?\n\}\r?\n")
    if (-not $m.Success) { throw "在 long_run.ps1 里找不到函数 $Name —— 判据改名了?这个脚本要一起改。" }
    return $m.Value
}

function Extract-Var([string] $Text, [string] $Name) {
    $m = [regex]::Match($Text, "(?m)^\`$$Name\s*=\s*'(.*)'\s*$")
    if (-not $m.Success) { throw "在 long_run.ps1 里找不到变量 `$$Name —— 判据改名了?这个脚本要一起改。" }
    return $m.Value
}

Invoke-Expression (Extract-Var $src 'reWriteStats')
Invoke-Expression (Extract-Def $src 'Get-WriteSideStats')
Invoke-Expression (Extract-Def $src 'Get-WriteSideVerdict')
Invoke-Expression (Extract-Def $src 'Get-SlaveWriteCount')

# ---- 读日志 ---------------------------------------------------------------
#   ★ 这里只做【取数】,不做判定。
#     判定的全部逻辑都在上面抠出来的三个函数里(跟长跑用的是同一份)。
#     日志行的解析式跟 long_run.ps1 的 $reLineTs 一致(格式是 Logging.cpp 定死的)。
$reLineTs = '^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\.\d{3}\s+\[(\w+)\s*\]\s+([\w.]+)\s+-\s+(.*)$'

if (-not (Test-Path $LogDir)) {
    Write-Output "找不到日志目录: $LogDir"
    exit 4
}

$lines = New-Object System.Collections.Generic.List[object]
foreach ($f in @(Get-ChildItem $LogDir -Filter '*.log' -File -ErrorAction SilentlyContinue | Sort-Object Name)) {
    try {
        $fs = [System.IO.File]::Open($f.FullName, 'Open', 'Read', 'ReadWrite')
        $sr = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8)
        while (-not $sr.EndOfStream) {
            $l = $sr.ReadLine()
            if ($l -match $reLineTs) {
                $lines.Add([pscustomobject]@{
                    Time  = [datetime]::ParseExact($matches[1], 'yyyy-MM-dd HH:mm:ss', $null)
                    Level = $matches[2]; Cat = $matches[3]; Msg = $matches[4]
                    File  = $f.Name
                })
            }
        }
        $sr.Close(); $fs.Close()
    } catch {
        Write-Output ("读日志失败(" + $f.Name + "): " + $_.Exception.Message)
    }
}

$sinceTime = $null
if ($Since) { $sinceTime = [datetime]::ParseExact($Since, 'yyyy-MM-dd HH:mm:ss', $null) }
elseif ($lines.Count -gt 0) { $sinceTime = ($lines | Sort-Object Time | Select-Object -First 1).Time }

if ($sinceTime) {
    $lines = @($lines | Where-Object { $_.Time -ge $sinceTime })
}

Write-Output ("日志行 {0} 条,日志文件 {1} 个" -f $lines.Count,
              @(Get-ChildItem $LogDir -Filter '*.log' -File -ErrorAction SilentlyContinue).Count)

$stats = Get-WriteSideStats -Lines $lines

$slaveWrites = $null
if ($SlaveLog) { $slaveWrites = Get-SlaveWriteCount -SlaveLog $SlaveLog -SinceTime $sinceTime }

$verdict = Get-WriteSideVerdict -Stats $stats -SlaveWrites $slaveWrites

Write-Output ""
Write-Output ("SOAK-W 状态: {0}" -f $verdict.State)
Write-Output $verdict.Reason
if ($verdict.EnvEvents -gt 0) {
    Write-Output ("环境事件(不算缺陷): {0}" -f ($verdict.EnvDetail -join '、'))
}

# ---- 关键数字也打出来(便于脚本 / 人核对) ---------------------------------
Write-Output ""
Write-Output "--- 写侧计数器(窗口增量)---"
foreach ($n in @('尝试','接受','拒收','拒收未连','拒收非法','拒收队列满','拒收发送','拒收缺陷',
                 '排队','排队发出','排队作废','已发出','成功','失败')) {
    Write-Output ("  {0} = {1}" -f $n, $stats.$n)
}
# ★★ 绝对值视图 —— 它现在是【载荷】,不是附注 ★★
#   坏路径(拒收缺陷/队列满)按绝对值判 ⇒ 窗口里只有一条统计行时也判得出。
#   ⇒ 这两个数必须打出来,不然"判据为什么红了"没法从输出里核对。
Write-Output "--- 写侧计数器(绝对值/累计)---"
foreach ($n in @('尝试','接受','拒收','拒收未连','拒收非法','拒收队列满','拒收发送','拒收缺陷',
                 '排队','排队发出','排队作废','已发出','成功','失败')) {
    Write-Output ("  {0} = {1}" -f $n, $stats.Abs.$n)
}
Write-Output ("  统计行条数 = {0};分组数 = {1};计数器归零次数 = {2}" -f $stats.StatLines, $stats.Groups, $stats.Resets)
Write-Output ("  增量可不可信 = {0}(单行组数 = {1})" -f $stats.DeltaAvailable, $stats.SingleLineGroups)
Write-Output ("  窗口跨度 = {0} 秒;期望行数 = {1};覆盖率 = {2}" -f `
    $stats.WindowSpanSec, $stats.ExpectedLines, $stats.Coverage)
Write-Output ("  恒等式①坏组 = {0}(残差 {1});恒等式②坏组 = {2}(残差 {3});恒等式③坏组 = {4}(残差 {5})" -f `
    $stats.Identity1Bad, $stats.Identity1Residual, $stats.Identity2Bad, $stats.Identity2Residual, `
    $stats.Identity3Bad, $stats.Identity3Residual)

if ($slaveWrites) {
    Write-Output ""
    Write-Output "--- 从站地面真相 ---"
    Write-Output ("  可用 = {0};FC6 = {1};FC16 = {2};合计 = {3}" -f `
        $slaveWrites.Available, $slaveWrites.Fc6, $slaveWrites.Fc16, $slaveWrites.Total)
}

switch ($verdict.State) {
    'FAIL'   { exit 1 }
    'UNKNOWN'{ exit 4 }
    default  { exit 0 }
}
