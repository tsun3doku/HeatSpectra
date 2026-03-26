@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
msbuild HeatSpectra\HeatSpectra.vcxproj /t:Build /p:Configuration=Debug /p:Platform=x64
