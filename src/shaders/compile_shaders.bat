@echo off
setlocal
set error=0

if %PROCESSOR_ARCHITECTURE%.==ARM64. (set FXCARCH=arm64) else (if %PROCESSOR_ARCHITECTURE%.==AMD64. (set FXCARCH=x64) else (set FXCARCH=x86))

set FXCOPTS=/nologo /WX /Ges /Zi /Zpc /Qstrip_reflect /Qstrip_debug


set PCFXC="%WindowsSdkVerBinPath%%FXCARCH%\fxc.exe"
if exist %PCFXC% goto continue
set PCFXC="%WindowsSdkBinPath%%WindowsSDKVersion%\%FXCARCH%\fxc.exe"
if exist %PCFXC% goto continue
set PCFXC="%WindowsSdkDir%bin\%WindowsSDKVersion%\%FXCARCH%\fxc.exe"
if exist %PCFXC% goto continue
set PCFXC="C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\fxc.exe"
if exist %PCFXC% goto continue

set PCFXC=fxc.exe

:continue
echo %PCFXC%
set fxc=%PCFXC% "compress.hlsl" %FXCOPTS% /Tcs_5_0 /E CSMain "/Fhcompress_quality.inc" "/Fdcompress_quality.pdb" /D QUALITY=1 /Vn Compress_Quality
echo.
echo %fxc%
%fxc% || set error=1

set fxc=%PCFXC% "compress.hlsl" %FXCOPTS% /Tcs_5_0 /E CSMain "/Fhcompress_speed.inc" "/Fdcompress_speed.pdb" /D QUALITY=0 /Vn Compress_Speed
echo.
echo %fxc%
%fxc% || set error=1

exit /b