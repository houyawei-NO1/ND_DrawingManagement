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
    // IFW 的「安装文件夹」页会拒绝「目录已包含安装」（TargetDirectoryInUse，
    // 只有一个 No 按钮，无法自动应答），该页一显示就没法覆盖安装。
    // 所以按目标目录是否已有安装动态决定页面可见性：
    //   · 已有安装 → 隐藏该页，跳过校验，直接覆盖旧版本（无需卸载）；
    //   · 首次安装 → 显示该页，默认值来自 config.xml 的 TargetDir（D 盘），用户可改。
    // 取不到 TargetDir 或 API 异常时一律隐藏，优先保证覆盖安装可用。
    var showTargetPage = false;
    try {
        var target = installer.value("TargetDir");
        if (target)
            showTargetPage = !installer.fileExists(target + "/components.xml");
    } catch (e) {
        showTargetPage = false;
    }
    try {
        installer.setDefaultPageVisible(QInstaller.TargetDirectory, showTargetPage);
    } catch (e) {
    }

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
