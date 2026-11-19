; 下线机设备端 - 配置模块
; 默认设置密码 0000，可在设置界面修改。

; 配置与日志统一放 D 盘独立目录：WinXP 程序目录可能只读，且更新时会被整包替换
Global $gAppDataDir = "D:\ND_DrawingManagement_Device"
If DriveStatus("D:\") <> "READY" Then $gAppDataDir = @ScriptDir & "\ND_DrawingManagement_Device"
If Not DirCreate($gAppDataDir) Then $gAppDataDir = @ScriptDir & "\ND_DrawingManagement_Device"
Global $gConfigPath = $gAppDataDir & "\config.ini"
Global $gLogDir = $gAppDataDir & "\logs"

Global $gServer = ""
Global $gMachineNo = 1
Global $gMachineName = ""
Global $gDeviceKey = ""
Global $gPrinterName = ""
Global $gCopies = 1
Global $gOperator = ""
Global $gPassword = "0000"
Global $gPaperW = 40
Global $gPaperH = 100
Global $gGap = 2
Global $gLabelFont = "TSS24.BF2"
Global $gRefreshOnStart = 1

Func _MachineName($iNo)
    Switch $iNo
        Case 2
            Return "二号机"
        Case 3
            Return "三号机"
        Case 4
            Return "四号机"
        Case 5
            Return "五号机"
        Case Else
            Return "一号机"
    EndSwitch
EndFunc

Func _ConfigLoad()
    ; 老版本配置在程序目录，升级到 D 盘目录时自动迁移一次，避免现场参数丢失
    Local $sLegacyConfig = @ScriptDir & "\config.ini"
    If Not FileExists($gConfigPath) And FileExists($sLegacyConfig) Then
        FileCopy($sLegacyConfig, $gConfigPath, 1)
    EndIf
    If Not FileExists($gConfigPath) Then _ConfigCreateDefault()

    $gServer = IniRead($gConfigPath, "device", "server", "http://192.168.1.16:8080")
    $gMachineNo = Int(IniRead($gConfigPath, "device", "machineNo", "1"))
    $gDeviceKey = IniRead($gConfigPath, "device", "deviceKey", "ND-M1")
    $gPrinterName = IniRead($gConfigPath, "printer", "name", "DL-720C")
    $gCopies = Int(IniRead($gConfigPath, "printer", "copies", "1"))
    $gPaperW = Int(IniRead($gConfigPath, "printer", "paperWidth", "40"))
    $gPaperH = Int(IniRead($gConfigPath, "printer", "paperHeight", "100"))
    $gGap = Int(IniRead($gConfigPath, "printer", "gap", "2"))
    $gLabelFont = IniRead($gConfigPath, "printer", "font", "TSS24.BF2")
    $gOperator = IniRead($gConfigPath, "operator", "name", $gMachineName)
    $gPassword = IniRead($gConfigPath, "security", "password", "0000")
    $gRefreshOnStart = Int(IniRead($gConfigPath, "ui", "refreshOnStart", "1"))

    If $gMachineNo < 1 Or $gMachineNo > 5 Then $gMachineNo = 1
    $gMachineName = _MachineName($gMachineNo)
    If $gCopies < 1 Then $gCopies = 1
    If $gPaperW <= 0 Then $gPaperW = 40
    If $gPaperH <= 0 Then $gPaperH = 100
    If $gGap < 0 Then $gGap = 2
    If $gOperator = "" Then $gOperator = $gMachineName
    If $gPassword = "" Then $gPassword = "0000"

    If Not FileExists($gLogDir) Then DirCreate($gLogDir)
EndFunc

Func _ConfigCreateDefault()
    IniWrite($gConfigPath, "device", "server", "http://192.168.1.16:8080")
    IniWrite($gConfigPath, "device", "machineNo", "1")
    IniWrite($gConfigPath, "device", "machineName", "")
    IniWrite($gConfigPath, "device", "deviceKey", "ND-M1")
    IniWrite($gConfigPath, "printer", "name", "DL-720C")
    IniWrite($gConfigPath, "printer", "copies", "1")
    IniWrite($gConfigPath, "printer", "paperWidth", "40")
    IniWrite($gConfigPath, "printer", "paperHeight", "100")
    IniWrite($gConfigPath, "printer", "gap", "2")
    IniWrite($gConfigPath, "printer", "font", "TSS24.BF2")
    IniWrite($gConfigPath, "operator", "name", "")
    IniWrite($gConfigPath, "security", "password", "0000")
    IniWrite($gConfigPath, "ui", "refreshOnStart", "1")
EndFunc

Func _ConfigSave($sServer, $iMachineNo, $sMachineName, $sDeviceKey, $sPrinter, $iCopies, $sOperator, $sPassword, $iPaperW, $iPaperH, $iGap, $sFont)
    IniWrite($gConfigPath, "device", "server", $sServer)
    IniWrite($gConfigPath, "device", "machineNo", String($iMachineNo))
    IniWrite($gConfigPath, "device", "machineName", $sMachineName)
    IniWrite($gConfigPath, "device", "deviceKey", $sDeviceKey)
    IniWrite($gConfigPath, "printer", "name", $sPrinter)
    IniWrite($gConfigPath, "printer", "copies", String($iCopies))
    IniWrite($gConfigPath, "printer", "paperWidth", String($iPaperW))
    IniWrite($gConfigPath, "printer", "paperHeight", String($iPaperH))
    IniWrite($gConfigPath, "printer", "gap", String($iGap))
    IniWrite($gConfigPath, "printer", "font", $sFont)
    IniWrite($gConfigPath, "operator", "name", $sOperator)
    IniWrite($gConfigPath, "security", "password", $sPassword)
    _ConfigLoad()
EndFunc

Func _LogLine($sText)
    If Not FileExists($gLogDir) Then DirCreate($gLogDir)
    Local $sFile = $gLogDir & "\client-" & @YEAR & @MON & @MDAY & ".log"
    Local $h = FileOpen($sFile, 1)
    If $h = -1 Then Return
    FileWriteLine($h, @YEAR & "-" & @MON & "-" & @MDAY & " " & @HOUR & ":" & @MIN & ":" & @SEC & " " & $sText)
    FileClose($h)
EndFunc
