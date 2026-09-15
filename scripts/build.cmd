@echo off
REM build.cmd — 给习惯双击 .cmd 的人用,内部还是调 build.ps1
REM 用法: build.cmd         (只 build)
REM       build.cmd -Run    (build + 启动)
REM       build.cmd -Clean  (清缓存重 configure)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\build.ps1" %*
if errorlevel 1 (
    echo.
    echo ❌ 失败,看上面的错误信息
    pause
)
