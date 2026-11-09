@echo off
chcp 65001 >nul
setlocal
REM NOTE: Keep all REM comments ASCII-only. With chcp 65001, cmd may split
REM UTF-8 Chinese REM lines and execute the tail as a command. Echo text below
REM stays Chinese because cmd prints it correctly.
REM ===================== Paths to verify =====================
REM Qt Installer Framework bin directory (binarycreator.exe)
set "QTIFW_BIN=D:\Qt\Tools\QtInstallerFramework\4.11\bin"
REM Qt 6 mingw windeployqt
set "WIND_DEPLOY=D:\Qt\6.8.3\mingw_64\bin\windeployqt.exe"

REM ===================== Project paths =====================
set "SRC_BIN=E:\HouYawei\01.QTProject\ND_DrawingManagement\build\Qt_6_8_3_mingw_64-Release\bin"
set "OCR_SRC=E:\HouYawei\01.QTProject\ND_DrawingManagement\ND_DrawingManagement_PC\ocr"
set "HEIF_SRC=E:\HouYawei\01.QTProject\ND_DrawingManagement\ND_DrawingManagement_PC\third_party\libheif"
set "INSTALLER_DIR=%~dp0"
set "DATA=%INSTALLER_DIR%packages\com.nd.ndmpc\data"
set "LOG=%INSTALLER_DIR%build.log"

REM ===== Version = build time of the main exe =====
REM Setup filename, IFW version and in-app version all use the same build date,
REM so a build and its setup package can recognise each other.
set "VERFILE=%TEMP%\nd_build_version.txt"
powershell -NoProfile -Command "(Get-Item '%SRC_BIN%\ND_DrawingManagement_PC.exe').LastWriteTime.ToString('yyyy-MM-dd')" > "%VERFILE%"
set /p VER=<"%VERFILE%"
powershell -NoProfile -Command "(Get-Item '%SRC_BIN%\ND_DrawingManagement_PC.exe').LastWriteTime.ToString('yyyyMMddHHmmss')" > "%VERFILE%"
set /p STAMP=<"%VERFILE%"
del /q "%VERFILE%" >nul 2>&1
if "%VER%"=="" (
    echo [错误] 取不到主程序构建时间，请先编译 ND_DrawingManagement_PC.exe。
    pause >nul
    exit /b 1
)
set "SETUP=%INSTALLER_DIR%ND_DrawingManagement_PC_Setup_%VER%.exe"
echo 版本号：%VER%（构建戳 %STAMP%）

if not exist "%QTIFW_BIN%\binarycreator.exe" (
    echo [错误] 找不到 binarycreator.exe，请修改脚本顶部的 QTIFW_BIN。
    pause >nul
    exit /b 1
)
if not exist "%HEIF_SRC%\heif.dll" (
    echo [错误] 找不到 HEIC 解码运行库：%HEIF_SRC%\heif.dll
    pause >nul
    exit /b 1
)
if not exist "%DATA%" mkdir "%DATA%"

REM Write the build date into data: the installed app reads version.txt first,
REM so the version does not change to the installation time.
REM Build stamp distinguishes multiple builds on the same day.
> "%DATA%\version.txt" echo version=%VER%
>> "%DATA%\version.txt" echo stamp=%STAMP%

REM IFW version increases with the build date (1.yyyy.m.d).
REM Running a new setup overwrites the old version; no uninstall is needed.
powershell -NoProfile -Command "$d='%VER%'; $q=$d.Split('-'); $v='1.'+$q[0]+'.'+[int]$q[1]+'.'+[int]$q[2]; $enc=[System.Text.UTF8Encoding]::new($false); $c='%INSTALLER_DIR%config\config.xml'; $t=[IO.File]::ReadAllText($c); $t=[regex]::Replace($t,'<Version>[^<]*</Version>','<Version>'+$v+'</Version>'); [IO.File]::WriteAllText($c,$t,$enc); $p='%INSTALLER_DIR%packages\com.nd.ndmpc\meta\package.xml'; $t=[IO.File]::ReadAllText($p); $t=[regex]::Replace($t,'<Version>[^<]*</Version>','<Version>'+$v+'</Version>'); $t=[regex]::Replace($t,'<ReleaseDate>[^<]*</ReleaseDate>','<ReleaseDate>'+$d+'</ReleaseDate>'); [IO.File]::WriteAllText($p,$t,$enc)"

echo [1/4] 收集 Qt 运行库...
xcopy /Y /Q "%SRC_BIN%\ND_DrawingManagement_PC.exe" "%DATA%" >nul
"%WIND_DEPLOY%" --release --no-translations --compiler-runtime "%DATA%\ND_DrawingManagement_PC.exe"
echo       完成

echo [2/4] 复制 HEIC 解码运行库与许可证...
xcopy /Y /Q "%HEIF_SRC%\*.dll" "%DATA%" >nul
xcopy /Y /Q "%HEIF_SRC%\*.txt" "%DATA%" >nul
echo       完成

echo [3/4] 复制离线 OCR 引擎...
xcopy /E /I /Y /Q "%OCR_SRC%" "%DATA%\ocr" >nul
echo       完成

echo [4/4] 生成安装包...
echo       正在压缩安装内容（含 OCR 模型，约几百 MB），可能需要几分钟，请耐心等待，不要关闭窗口。
"%QTIFW_BIN%\binarycreator.exe" -c "%INSTALLER_DIR%config\config.xml" -p "%INSTALLER_DIR%packages" "%SETUP%" > "%LOG%" 2>&1
if errorlevel 1 (
    echo.
    echo [失败] 生成安装包出错，详情见：%LOG%
    pause >nul
    exit /b 1
)
echo.
echo 完成：%SETUP%
echo.
echo 按任意键退出...
pause >nul
endlocal
