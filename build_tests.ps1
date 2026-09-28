# build_tests.ps1 -- 单元测试构建/运行(主程序 build.ps1 的测试版)
#
# 用法:
#   powershell -ExecutionPolicy Bypass -File .\build_tests.ps1              # 编译全部测试
#   powershell -ExecutionPolicy Bypass -File .\build_tests.ps1 -Test tst_ControlBaseWrite   # 只编一个
#   powershell -ExecutionPolicy Bypass -File .\build_tests.ps1 -Run         # 编译 + ctest 全跑
#   powershell -ExecutionPolicy Bypass -File .\build_tests.ps1 -Test tst_ControlBaseWrite -Run  # 只跑一个
#
# 【为什么单独一个脚本】主程序 build.ps1 建的 hmi/out/build/debug 里【没有测试】——
#   测试是独立 CMake 工程 hmi/tests/,自己一份 build_ninja/。踩过一次:
#   对着主程序目录跑 ctest 报 "No tests were found",以为测试挂了。

param(
    [string]$Test = '',   # 指定只编/跑某个测试,空 = 全部
    [switch]$Run,          # 编译完跑 ctest
    [switch]$Clean
)

$CMake    = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$NinjaExe = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$Vcvars   = 'D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$QtDir    = 'D:\Qt\6.8.3\msvc2022_64'

$TestsDir = Join-Path $PSScriptRoot 'hmi\tests'
$BuildDir = Join-Path $TestsDir 'build_ninja'

Write-Host '[1/4] Loading MSVC env ...' -ForegroundColor Cyan
$setOutput = cmd.exe /c ('"' + $Vcvars + '" >NUL & set')
foreach ($line in ($setOutput -split "`r?`n")) {
    if ($line -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
$env:PATH  = (Split-Path $NinjaExe) + ';' + (Join-Path $QtDir 'bin') + ';' + $env:PATH
$env:QTDIR = $QtDir
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Write-Host '  [ERROR] cl.exe not in PATH' -ForegroundColor Red; exit 1
}

Write-Host '[2/4] Configure ...' -ForegroundColor Cyan
if ($Clean -and (Test-Path $BuildDir)) { Remove-Item -Recurse -Force $BuildDir }
if (-not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))) {
    Set-Location $TestsDir
    & $CMake -G Ninja -S $TestsDir -B $BuildDir
    if ($LASTEXITCODE -ne 0) { Write-Host '  [ERROR] configure failed' -ForegroundColor Red; exit 1 }
}

Write-Host '[3/4] Build ...' -ForegroundColor Cyan
# ★ -Target '' 会报 unknown target,必须整个参数省掉而不是传空串
if ($Test) { & $CMake --build $BuildDir --parallel --target $Test }
else        { & $CMake --build $BuildDir --parallel }
if ($LASTEXITCODE -ne 0) { Write-Host '  [ERROR] build failed' -ForegroundColor Red; exit 1 }

if ($Run) {
    Write-Host '[4/4] ctest ...' -ForegroundColor Cyan
    $env:QT_QPA_PLATFORM = 'offscreen'
    $ctest = Join-Path (Split-Path $CMake) 'ctest.exe'
    if (Test-Path $ctest) {
        if ($Test) { & $ctest --test-dir $BuildDir -R "^$Test$" --output-on-failure }
        else        { & $ctest --test-dir $BuildDir --output-on-failure }
    } else {
        & $CMake --build $BuildDir --target test
    }
    exit $LASTEXITCODE
} else {
    Write-Host '[4/4] Build OK (add -Run to execute)' -ForegroundColor Green
}
