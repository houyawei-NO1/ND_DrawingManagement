// 卸载（以及覆盖安装/更新）前强制结束正在运行的程序与主程序拉起的 OCR 子进程，
// 否则 exe、dll、模型文件被占用，Qt IFW 删除文件时卸载会失败。
function stopRunningProgram()
{
    if (systemInfo.productType !== "windows")
        return;

    var target = installer.value("TargetDir");
    if (target) {
        // 先按安装目录里的完整路径结束，尽量不误杀其它目录的同名程序
        installer.killProcess(target + "/ND_DrawingManagement_PC.exe");
        installer.killProcess(target + "/ocr/PaddleOCR-json/PaddleOCR-json.exe");
    }

    // 兜底：仍有残留就按进程名强制结束（/F 强制，/T 连带子进程）
    var names = ["ND_DrawingManagement_PC.exe", "PaddleOCR-json.exe"];
    for (var i = 0; i < names.length; ++i) {
        if (installer.isProcessRunning(names[i]))
            installer.execute("taskkill.exe", ["/F", "/T", "/IM", names[i]]);
    }
}

function Component()
{
    // 卸载开始时（删除文件之前）、安装/更新开始时都先关掉程序
    installer.uninstallationStarted.connect(this, Component.prototype.onUninstallationStarted);
    installer.installationStarted.connect(this, Component.prototype.onInstallationStarted);
}

Component.prototype.onUninstallationStarted = function()
{
    stopRunningProgram();
};

Component.prototype.onInstallationStarted = function()
{
    stopRunningProgram();
};

Component.prototype.createOperations = function()
{
    // 先执行默认的文件安装操作
    component.createOperations();

    if (systemInfo.productType === "windows") {
        var exe = "@TargetDir@/ND_DrawingManagement_PC.exe";

        // 开始菜单：南都图纸管理（卸载时自动清理）
        component.addOperation("CreateShortcut", exe, "@StartMenuDir@/南都图纸管理.lnk",
            "workingDirectory=@TargetDir@",
            "iconPath=@TargetDir@/ND_DrawingManagement_PC.exe", "iconId=0",
            "description=南都图纸管理系统");

        // 桌面：南都图纸管理（卸载时自动清理）
        component.addOperation("CreateShortcut", exe, "@DesktopDir@/南都图纸管理.lnk",
            "workingDirectory=@TargetDir@",
            "iconPath=@TargetDir@/ND_DrawingManagement_PC.exe", "iconId=0",
            "description=南都图纸管理系统");
    }
};
