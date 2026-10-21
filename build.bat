@echo off
rem Thin wrapper around scripts\build.ps1. All arguments are forwarded verbatim.
rem   build.bat                          desktop Release (defaults)
rem   build.bat desktop Release
rem   build.bat desktop Debug -SkipTests
rem   build.bat android Release
rem   build.bat core Release
rem   build.bat verify-ios
rem   build.bat verify-render
rem   build.bat clean
setlocal

set "SHELL=powershell"
where pwsh >nul 2>nul && set "SHELL=pwsh"

"%SHELL%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" %*
exit /b %ERRORLEVEL%
