# package.ps1 -- 阶段 I:把 hmi 打成一个别人能装的 exe
#
# 用法:
#   powershell -ExecutionPolicy Bypass -File .\installer\package.ps1
#   powershell -ExecutionPolicy Bypass -File .\installer\package.ps1 -SkipDeploy   # 只重编安装包
#
# 产物:installer\Output\hmi-setup-<AppVersion>.exe
#
# ★ 为什么要有这个脚本(而不是让每个人手工敲三条命令)
#   打包链有三步,每一步忘了前面那步就会得到一个【看起来成功、实际不能用】的包:
#     1. Release 构建    —— 忘了 = 用 Debug 的 exe 打包,慢 10 倍还带 _d 后缀依赖
#     2. windeployqt     —— 忘了 = 裸 exe,拷到别人机器上缺 Qt6Core.dll 直接起不来
#     3. ISCC            —— 忘了 = 什么都没产出
#   第 2 步最阴险:在【本机】跑起来好好的(自己机器装了 Qt),换台机器就死。
#   所以这里把三步绑死,并且第 2 步失败直接 exit,不许拿旧产物继续打。
param(
    [switch]$SkipDeploy   # 跳过 windeployqt(仅在依赖没变时用)
)

$ErrorActionPreference = 'Stop'

$CMake     = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$NinjaExe  = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$Vcvars    = 'D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$QtDir     = 'D:\Qt\6.8.3\msvc2022_64'
$DeployQt  = Join-Path $QtDir 'bin\windeployqt.exe'

$HmiDir      = Split-Path $PSScriptRoot -Parent
$InstallerDir = $PSScriptRoot
$BuildDir    = Join-Path $HmiDir 'out\build\release'
$InstallRoot = Join-Path $InstallerDir 'install_root'
$Exe         = Join-Path $BuildDir 'hmi.exe'

# ---- Inno Setup 找路径 -------------------------------------------------
# 装在 32 位程序目录(官方默认),但有人会装到 Program Files,两个都试。
$Iscc = @(
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe'
) | Where-Object { Test-Path $_ } | Select-Object -First 1

Write-Host '[1/4] Loading MSVC env ...' -ForegroundColor Cyan
$setOutput = cmd.exe /c ('"' + $Vcvars + '" >NUL & set')
foreach ($line in ($setOutput -split "`r?`n")) {
    if ($line -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
$env:PATH = (Split-Path $NinjaExe) + ';' + (Join-Path $QtDir 'bin') + ';' + $env:PATH
$env:QTDIR = $QtDir
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Write-Host '  [ERROR] cl.exe not in PATH' -ForegroundColor Red; exit 1
}

Write-Host '[2/4] Release build ...' -ForegroundColor Cyan
Push-Location $HmiDir
try {
    & $CMake --preset Qt-Release
    if ($LASTEXITCODE -ne 0) { Write-Host '  [ERROR] configure failed' -ForegroundColor Red; exit 1 }
    & $CMake --build $BuildDir
    if ($LASTEXITCODE -ne 0) { Write-Host '  [ERROR] build failed' -ForegroundColor Red; exit 1 }
} finally { Pop-Location }

if (-not (Test-Path $Exe)) {
    Write-Host "  [ERROR] build finished but $Exe is missing" -ForegroundColor Red; exit 1
}

Write-Host '[3/4] windeployqt -> install_root ...' -ForegroundColor Cyan
if ($SkipDeploy) {
    Write-Host '  (-SkipDeploy: 复用现有 install_root。改了 Qt 依赖就别用这个开关。)' -ForegroundColor Yellow
} else {
    # 每次先清空:windeployqt 只加不减,删掉的依赖会留在上一版残留里,
    # 然后被打进安装包 —— 用户装了却加载着一个你早就删掉的旧 DLL。
    if (Test-Path $InstallRoot) { Remove-Item -Recurse -Force $InstallRoot }
    New-Item -ItemType Directory -Force $InstallRoot | Out-Null
    & $DeployQt --release --no-translations --dir $InstallRoot $Exe
    if ($LASTEXITCODE -ne 0) { Write-Host '  [ERROR] windeployqt failed' -ForegroundColor Red; exit 1 }

    # ★ windeployqt 会把【依赖】收过来,但 hmi.exe 自己不一定会跟过来
    #   (实测本机 6.8.3 + msvc2022_64 就是不复制)。它不复制并不会报错,
    #   于是 install_root 里就只剩一堆 DLL —— 而 [Files] 整个目录收进去,
    #   打出来的安装包会【一个程序都没有】,安装过程还一切正常。
    #   所以这里显式补,并且后面单独断言它存在。
    Copy-Item $Exe (Join-Path $InstallRoot 'hmi.exe') -Force

    if (-not (Test-Path (Join-Path $InstallRoot 'hmi.exe'))) {
        Write-Host '  [ERROR] install_root\hmi.exe missing -- the installer would contain no program' -ForegroundColor Red
        exit 1
    }

    # 收尾自检:插件目录少一个,Qt 启动时才报"could not find the Qt platform plugin",
    # 那个错在用户的机器上出,我们在这台机器上看不见。
    foreach ($d in 'platforms','styles','sqldrivers','imageformats','tls') {
        $p = Join-Path $InstallRoot $d
        if (-not (Test-Path $p)) {
            Write-Host "  [ERROR] install_root\$d missing -- Qt will fail to start on a clean machine" -ForegroundColor Red
            exit 1
        }
    }
}

$n = (Get-ChildItem $InstallRoot -Recurse -File | Measure-Object -Property Length -Sum)
Write-Host ("  collected {0} files / {1:N1} MB" -f $n.Count, ($n.Sum / 1MB))

# 再查一遍 exe —— -SkipDeploy 跳过的是 windeployqt 那段,这个断言必须在外面,
# 否则"跳过部署 + 上次部署不完整"会直接打出一个空安装包。
if (-not (Test-Path (Join-Path $InstallRoot 'hmi.exe'))) {
    Write-Host '  [ERROR] install_root\hmi.exe missing -- run without -SkipDeploy' -ForegroundColor Red
    exit 1
}

Write-Host '[4/4] ISCC ...' -ForegroundColor Cyan
if (-not $Iscc) {
    Write-Host '  [ERROR] Inno Setup not found.' -ForegroundColor Red
    Write-Host '  前三步(Release 构建 + install_root 收集)已经完成,install_root 可以直接用。' -ForegroundColor Yellow
    Write-Host '  装 Inno Setup 6 之后重跑本脚本即可: https://jrsoftware.org/isdl.php' -ForegroundColor Yellow
    Write-Host '  PACKAGE_READY_NO_ISCC' -ForegroundColor Yellow
    exit 2
}

& $Iscc (Join-Path $InstallerDir 'hmi.iss')
if ($LASTEXITCODE -ne 0) { Write-Host '  [ERROR] ISCC failed' -ForegroundColor Red; exit 1 }

$out = Join-Path $InstallerDir 'Output'
Get-ChildItem $out -Filter '*.exe' | ForEach-Object {
    Write-Host ("  built {0} ({1:N1} MB)" -f $_.Name, ($_.Length / 1MB)) -ForegroundColor Green
}
Write-Host 'PACKAGE_OK' -ForegroundColor Green
