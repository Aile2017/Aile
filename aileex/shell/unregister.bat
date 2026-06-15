@echo off
rem Remove the AileEx Explorer context-menu (per-user).
regsvr32 /s /u "%~dp0AileExShell.dll"
echo AileEx shell menu unregistered.
pause
