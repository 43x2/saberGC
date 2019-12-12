@echo off
setlocal
pushd %~dp0

premake5.exe vs2019
if not %ERRORLEVEL% == 0 (
	pause
)

popd
endlocal
