function Component()
{
}

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
