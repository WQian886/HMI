# build.ps1 -- PowerShell 5.1 兼容版
#
# 用法(在 Developer PowerShell for VS 2022 或任何 shell 里都可以):
#   powershell -ExecutionPolicy Bypass -File .\build.ps1           # build
#   powershell -ExecutionPolicy Bypass -File .\build.ps1 -Run       # build + 启动
#   powershell -ExecutionPolicy Bypass -File .\build.ps1 -Clean     # 清缓存 + 重 configure
#
# 这个脚本会自动:
#   1. 调 vcvars64.bat 把 MSVC 环境拉进来(cl.exe/link.exe 进 PATH)
#   2. 设 QTDIR 让 CMake 找 Qt6
#   3. 调 cmake --preset Qt-Debug (Ninja generator)
#   4. 调 ninja 编译

param(
    [switch]$Run,    # build 完后启动 hmi.exe
    [switch]$Clean   # 先删 out/ 强制重 configure
)

# ====== 路径(改成你机器上的实际路径) ======
$CMake     = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$NinjaExe  = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$Vcvars    = 'D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$QtDir     = 'D:\Qt\6.8.3\msvc2022_64'

$RepoRoot  = $PSScriptRoot
$HmiDir    = Join-Path $RepoRoot 'hmi'
$BuildDir  = Join-Path $HmiDir 'out\build\debug'

# ====== 1. 文件存在检查 ======
Write-Host '[1/6] Checking toolchain ...' -ForegroundColor Cyan
$missing = @()
foreach ($p in @($CMake, $NinjaExe, $Vcvars)) {
    if (-not (Test-Path $p)) { $missing += $p }
}
if ($missing.Count -gt 0) {
    Write-Host '  [ERROR] Missing files:' -ForegroundColor Red
    $missing | ForEach-Object { Write-Host ('    ' + $_) -ForegroundColor Red }
    exit 1
}
Write-Host ('    cmake   = ' + $CMake) -ForegroundColor DarkGray
Write-Host ('    ninja   = ' + $NinjaExe) -ForegroundColor DarkGray
Write-Host ('    vcvars  = ' + $Vcvars) -ForegroundColor DarkGray

# ====== 2. 拉 MSVC 环境 ======
Write-Host '[2/6] Loading MSVC env via vcvars64 ...' -ForegroundColor Cyan
# cmd 用 & (无条件顺序) 不用 && (依赖 vcvars 成功),避免 PowerShell 解析 && 报错
$setOutput = cmd.exe /c ('"' + $Vcvars + '" >NUL & set')
foreach ($line in ($setOutput -split "`r?`n")) {
    if ($line -match '^([^=]+)=(.*)$') {
        $name = $matches[1]
        $val  = $matches[2]
        if ($name -in @('PATH','INCLUDE','LIB','LIBPATH','VCToolsInstallDir','VCINSTALLDIR','WindowsSdkDir','WindowsSdkVerBinPath','UCRTVersion')) {
            [System.Environment]::SetEnvironmentVariable($name, $val, 'Process')
        }
    }
}
# ninja 路径加到 PATH 最前面
$env:PATH = (Split-Path $NinjaExe) + ';' + $env:PATH

$cl = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $cl) {
    Write-Host '  [ERROR] vcvars64 ran but cl.exe not in PATH. Check vcvars path.' -ForegroundColor Red
    exit 1
}
Write-Host ('    cl.exe  = ' + $cl.Source) -ForegroundColor DarkGray

# ====== 3. 设 QTDIR ======
$env:QTDIR = $QtDir
Write-Host ('    QTDIR   = ' + $QtDir) -ForegroundColor DarkGray

# ====== 4. 清理(可选) ======
if ($Clean) {
    $outDir = Join-Path $HmiDir 'out'
    if (Test-Path $outDir) {
        Write-Host '[3/6] Cleaning out/ ...' -ForegroundColor Yellow
        Remove-Item -Recurse -Force $outDir
    }
} else {
    Write-Host '[3/6] Skip clean (use -Clean to force reconfigure)' -ForegroundColor DarkGray
}

# ====== 5. Configure ======
Set-Location $HmiDir
$cache = Join-Path $BuildDir 'CMakeCache.txt'
if (-not (Test-Path $cache)) {
    Write-Host '[4/6] CMake Configure (Qt-Debug, Ninja) ...' -ForegroundColor Cyan
    & $CMake --preset Qt-Debug
    if ($LASTEXITCODE -ne 0) {
        Write-Host ('  [ERROR] cmake configure failed (exit ' + $LASTEXITCODE + ')') -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host '[4/6] CMake cache exists, skip configure' -ForegroundColor DarkGray
}

# ====== 6. Build ======
Write-Host '[5/6] Ninja Build ...' -ForegroundColor Cyan
& $CMake --build $BuildDir --parallel
if ($LASTEXITCODE -ne 0) {
    Write-Host ('  [ERROR] build failed (exit ' + $LASTEXITCODE + ')') -ForegroundColor Red
    exit 1
}

# ====== 结果 ======
$exe = Join-Path $BuildDir 'hmi.exe'
if (-not (Test-Path $exe)) {
    Write-Host ('  [ERROR] build done but hmi.exe not found at ' + $exe) -ForegroundColor Red
    exit 1
}
$size = [math]::Round((Get-Item $exe).Length / 1MB, 2)
Write-Host ''
Write-Host ('[6/6] [OK] Build OK: ' + $exe + ' (' + $size + ' MB)') -ForegroundColor Green

if ($Run) {
    Write-Host ''
    Write-Host '>>> Starting hmi.exe (Ctrl+C to quit) ...' -ForegroundColor Cyan
    & $exe
} else {
    Write-Host ''
    Write-Host 'Tip: re-run with -Run to launch, e.g.: .\build.ps1 -Run' -ForegroundColor DarkGray
}
