@echo off
chcp 65001 >nul
setlocal
REM ===================== 需要你确认的路径 =====================
REM Qt Installer Framework 的 bin 目录（含 binarycreator.exe）
set "QTIFW_BIN=D:\Qt\Tools\QtInstallerFramework\4.11\bin"
REM Qt 6 mingw 的 windeployqt
set "WIND_DEPLOY=D:\Qt\6.8.3\mingw_64\bin\windeployqt.exe"

REM ===================== 项目相关路径（一般不用改） =====================
set "SRC_BIN=E:\HouYawei\01.QTProject\ND_DrawingManagement\build\Qt_6_8_3_mingw_64-Release\bin"
set "OCR_SRC=E:\HouYawei\01.QTProject\ND_DrawingManagement\ND_DrawingManagement_PC\ocr"
set "HEIF_SRC=E:\HouYawei\01.QTProject\ND_DrawingManagement\ND_DrawingManagement_PC\third_party\libheif"
set "INSTALLER_DIR=%~dp0"
set "DATA=%INSTALLER_DIR%packages\com.nd.ndmpc\data"
set "SETUP=%INSTALLER_DIR%ND_DrawingManagement_PC_Setup.exe"
set "LOG=%INSTALLER_DIR%build.log"

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
