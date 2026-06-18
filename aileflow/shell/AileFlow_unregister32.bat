@echo off
rem Remove the AileFlow Explorer context-menu for 32-bit apps (per-user).
%SystemRoot%\SysWOW64\regsvr32.exe /s /u "%~dp0AileFlowShell32.dll"
echo AileFlow 32-bit shell menu unregistered.
pause
