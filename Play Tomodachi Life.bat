@echo off
rem Opens the launcher of the game built by Install Tomodachi Life.bat. The install location (default build\, or the folder
rem chosen in the installer window, remembered in build_location.txt) is read by scripts\run.py.
cd /d "%~dp0"
set "PY="
rem the private Python of THIS folder first: build\python_path.txt keeps an absolute path, which is wrong after the folder moved
if exist deps\python\python.exe set "PY=%~dp0deps\python\python.exe"
if not defined PY if exist build\python_path.txt set /p PY=<build\python_path.txt
if defined PY if not exist "%PY%" set "PY="
if not defined PY set "PY=python"
"%PY%" scripts\run.py
if errorlevel 1 pause
