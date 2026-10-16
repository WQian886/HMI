@echo off
REM Local test build. ASCII-only comments on purpose:
REM   .bat files are read as ANSI by cmd.exe, so Chinese comments here
REM   get mangled and cmd tries to execute them as commands.
REM   Why this file exists: running cmake directly fails with
REM   LNK1104 "cannot open kernel32.lib" - not a code problem, the MSVC
REM   library dirs are just not on INCLUDE/LIB. Typing vcvars64 by hand
REM   every build is wasted time.
REM Usage:  build_tests_local.cmd <ninja-target-name>
setlocal
call "D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=D:\Qt\Tools\CMake_64\bin;D:\Qt\Tools\Ninja;D:\Qt\6.8.3\msvc2022_64\bin;%PATH%
cd /d E:\qianwie\hmi\hmi\tests
if not exist build_ninja\CMakeCache.txt (
  cmake -S . -B build_ninja -G Ninja -DCMAKE_BUILD_TYPE=Debug || exit /b 1
)
cmake --build build_ninja --target %1 || exit /b 1
