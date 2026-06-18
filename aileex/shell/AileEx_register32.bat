@echo off
rem Register the AileEx Explorer context-menu for 32-bit apps (per-user).
%SystemRoot%\SysWOW64\regsvr32.exe /s "%~dp0AileExShell32.dll"
if errorlevel 1 (
  echo Failed to register AileExShell32.dll
) else (
  echo AileEx 32-bit shell menu registered for the current user.
  echo Restart Explorer or sign out/in if the menu does not appear.
)
pause
