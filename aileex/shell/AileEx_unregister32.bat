@echo off
rem Remove the AileEx Explorer context-menu for 32-bit apps (per-user).
%SystemRoot%\SysWOW64\regsvr32.exe /s /u "%~dp0AileExShell32.dll"
echo AileEx 32-bit shell menu unregistered.
pause
