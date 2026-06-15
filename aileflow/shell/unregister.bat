@echo off
rem Remove the AileFlow Explorer context-menu (per-user).
regsvr32 /s /u "%~dp0AileFlowShell.dll"
echo AileFlow shell menu unregistered.
pause
