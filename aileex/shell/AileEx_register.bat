@echo off
rem Register the AileEx Explorer context-menu (per-user, no elevation needed).
regsvr32 /s "%~dp0AileExShell.dll"
if errorlevel 1 (
  echo Failed to register AileExShell.dll
) else (
  echo AileEx shell menu registered for the current user.
  echo Restart Explorer or sign out/in if the menu does not appear.
)
pause
