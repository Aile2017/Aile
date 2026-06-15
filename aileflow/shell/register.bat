@echo off
rem Register the AileFlow Explorer context-menu (per-user, no elevation needed).
regsvr32 /s "%~dp0AileFlowShell.dll"
if errorlevel 1 (
  echo Failed to register AileFlowShell.dll
) else (
  echo AileFlow shell menu registered for the current user.
  echo Restart Explorer or sign out/in if the menu does not appear.
)
pause
