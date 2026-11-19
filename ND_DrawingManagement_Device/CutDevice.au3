; 编译图标统一使用 Qt 工程里的 logo.ico（相对本脚本目录）
#Icon "..\ND_DrawingManagement_PC\logo.ico"

#include <GUIConstantsEx.au3>
#include <WindowsConstants.au3>
#include <EditConstants.au3>
#include <ComboConstants.au3>
#include <StaticConstants.au3>
#include <GuiListView.au3>
#include <GuiImageList.au3>
#include <TrayConstants.au3>

#include "Config.au3"
#include "Json.au3"
#include "Http.au3"
#include "Api.au3"
#include "Print.au3"

Opt("GUIOnEventMode", 0)
Opt("TrayOnEventMode", 1)
Opt("TrayMenuMode", 3)

; 接管 COM 异常：WinHttp 连不上服务器时不再弹 AutoIt 原生错误框，改由 _ShowServerError 提示
Global $gComErrHook
$gComErrHook = ObjEvent("AutoIt.Error", "_ComErrorHandler")

Global $gTasks[1][$T_COLS]
Global $gCurrent = 0
; 版本号唯一基准：编译脚本 build_device.ps1 从这里读取并生成 x86 / x64 两个安装包
Global $gAppVerBase = "20261119-b36"
Global $gAppArch = "x86"
If @AutoItX64 = 1 Then $gAppArch = "x64"
Global $gAppVer = $gAppVerBase & "-" & $gAppArch
Global Const $CLR_BTN_GREEN = 0x2E8B57
Global Const $CLR_BTN_RED = 0xC0392B
Global Const $CLR_BTN_NEUTRAL = 0x3A4258
Global Const $FLOAT_W = 430
Global Const $FLOAT_H = 560
Global $gMainW = 1000
Global $gMainH = 650
Global $gCurTab = 0                   ; 当前主界面 Tab：0 全部 / 1 未完成 / 2 已完成

Global $hFloat, $hMain, $hSet
Global $lblFloatTitle, $lblTaskNo, $lblSeq, $lblBatchBig, $lblBatch, $lblBatchNo, $lblOperator
Global $lblCircuit, $lblSection, $lblLength, $lblStripL, $lblStripR, $lblWorkL, $lblWorkR
Global $lblQty, $lblStatus
Global $btnPrev, $btnNext, $btnStart, $btnConfirm, $btnMain, $btnTray
; 单个 ListView + 自绘标签按钮，避免原生 Tab 的白色客户区
Global $lvTasks = 0
Global $btnTabAll, $btnTabPending, $btnTabDone
Global $btnRefresh, $btnSetCurrent, $btnMainPrint, $btnMainStart, $btnMainConfirm, $btnBackFloat, $btnSettings
Global $btnCheckUpdate, $lblVersion
Global $btnFilterAll, $btnFilterPending, $btnFilterDoing, $btnFilterDone
Global $gStatusFilter = -1   ; -1=全部，1=待生产，2=进行中，3=已完成（设备端无待派工/已作废）
Global $lblPrinterInfo, $btnPrinterTest, $btnEnumPrinters
Global $inpServer, $cmbMachine, $inpDeviceKey, $cmbPrinter
Global $inpOperator, $inpPaperW, $inpPaperH, $inpGap, $inpFont, $inpNewPassword
Global $btnSetSave, $btnSetCancel
Global $lblDetail
Global $gMapAll[1], $gMapPending[1], $gMapDone[1]
Global $gLastSelLv = 0, $gLastSelIdx = -1  ; 轮询缓存：上次选中的 ListView + 行号

_ConfigLoad()
_BuildUi()

; 每 50ms 主动查一次 ListView 选中状态（AdLib 在 GUIGetMsg 阻塞等待期间也会触发）
AdLibRegister("_PollSelection", 50)

; 启动时也尝试连服务器：连不上就弹友好提示，而不是让 WinHttp 抛 COM 原生错误框
If $gRefreshOnStart Then _RefreshTasks(True)
_UpdateFloat()

TrayCreateItem("显示浮窗")
TrayItemSetOnEvent(-1, "_TrayShowFloat")
TrayCreateItem("主界面")
TrayItemSetOnEvent(-1, "_TrayShowMain")
TrayCreateItem("")
TrayCreateItem("退出")
TrayItemSetOnEvent(-1, "_TrayExit")
TraySetOnEvent($TRAY_EVENT_PRIMARYDOUBLE, "_TrayShowFloat")
TraySetToolTip("下线机设备端 - " & $gMachineName)

While 1
    Local $aMsg = GUIGetMsg(1)
    Local $id = $aMsg[0]
    Local $win = $aMsg[1]

    If $id = $GUI_EVENT_CLOSE Then
        If $win = $hFloat Then
            GUISetState(@SW_HIDE, $hFloat)
        ElseIf $win = $hMain Then
            GUISetState(@SW_HIDE, $hMain)
            GUISetState(@SW_SHOW, $hFloat)
        ElseIf $win = $hSet Then
            GUISetState(@SW_HIDE, $hSet)
        EndIf
        ContinueLoop
    EndIf

    If $id = $GUI_EVENT_PRIMARYDOWN And $win = $hFloat Then _FloatDrag()

    If $win = $hFloat Then
        Select
            Case $id = $btnPrev
                _PrevTask()
            Case $id = $btnNext
                _NextTask()
            Case $id = $btnStart
                _DoStart()
            Case $id = $btnConfirm
                _DoConfirm()
            Case $id = $btnMain
                GUISetState(@SW_HIDE, $hFloat)
                GUISetState(@SW_SHOW, $hMain)
            Case $id = $btnTray
                GUISetState(@SW_HIDE, $hFloat)
        EndSelect
    ElseIf $win = $hMain Then
        Select
            Case $id = $btnTabAll
                _SwitchTab(0)
            Case $id = $btnTabPending
                _SwitchTab(1)
            Case $id = $btnTabDone
                _SwitchTab(2)
            Case $id = $btnFilterAll
                _ApplyStatusFilter(-1)
            Case $id = $btnFilterPending
                _ApplyStatusFilter(1)
            Case $id = $btnFilterDoing
                _ApplyStatusFilter(2)
            Case $id = $btnFilterDone
                _ApplyStatusFilter(3)
            Case $id = $btnRefresh
                _RefreshTasks(True)
            Case $id = $btnSetCurrent
                _MainSetCurrent()
            Case $id = $btnMainPrint
                _DoPrint()
            Case $id = $btnMainStart
                _DoStart()
            Case $id = $btnMainConfirm
                _DoConfirm()
            Case $id = $btnBackFloat
                GUISetState(@SW_HIDE, $hMain)
                GUISetState(@SW_SHOW, $hFloat)
            Case $id = $btnSettings
                _OpenSettings()
            Case $id = $btnCheckUpdate
                _DoCheckUpdate()
        EndSelect
    ElseIf $win = $hSet Then
        Select
            Case $id = $btnSetSave
                _SaveSettings()
            Case $id = $btnSetCancel
                GUISetState(@SW_HIDE, $hSet)
            Case $id = $btnEnumPrinters
                _EnumShowPrinters()
            Case $id = $btnPrinterTest
                _TestPrint()
        EndSelect
    EndIf
WEnd

Func _StyleBtn($id, $color)
    Local $h = GUICtrlGetHandle($id)
    If $h Then DllCall("uxtheme.dll", "int", "SetWindowTheme", "hwnd", $h, "wstr", "", "wstr", "")
    GUICtrlSetBkColor($id, $color)
    GUICtrlSetColor($id, 0xFFFFFF)
    GUICtrlSetFont($id, 11, 700)
EndFunc

Func _ColorRef($r, $g, $b)
    Return $b * 65536 + $g * 256 + $r
EndFunc

Func _FloatLabel($text, $y, $h, $size = 16, $color = 0xC8D2E4)
    Local $id = GUICtrlCreateLabel($text, 16, $y, 398, $h)
    GUICtrlSetFont($id, $size, 400)
    GUICtrlSetColor($id, $color)
    Return $id
EndFunc

Func _BuildUi()
    $gMainW = @DesktopWidth - 40
    If $gMainW < 1000 Then $gMainW = 1000
    $gMainH = @DesktopHeight - 80
    If $gMainH < 620 Then $gMainH = 620

    $hFloat = GUICreate("下线任务", $FLOAT_W, $FLOAT_H, -1, -1, $WS_POPUP, BitOR($WS_EX_TOPMOST, $WS_EX_TOOLWINDOW))
    GUISetBkColor(0x22293C, $hFloat)
    WinMove($hFloat, "", @DesktopWidth - $FLOAT_W - 8, @DesktopHeight - $FLOAT_H - 42, $FLOAT_W, $FLOAT_H)
    $lblFloatTitle = GUICtrlCreateLabel("当前机器  " & $gMachineName, 16, 10, 280, 32)
    GUICtrlSetFont($lblFloatTitle, 18, 700)
    GUICtrlSetColor($lblFloatTitle, 0xDCEAFF)
    $btnMain = GUICtrlCreateButton("主界面", 340, 10, 76, 34)
    _StyleBtn($btnMain, $CLR_BTN_NEUTRAL)

    $lblTaskNo = _FloatLabel("", 48, 28, 18, 0xFFFFFF)
    GUICtrlSetFont($lblTaskNo, 18, 700)
    $lblSeq = _FloatLabel("", 78, 28, 18, 0xFFFFFF)
    GUICtrlSetFont($lblSeq, 18, 700)
    $lblBatchBig = _FloatLabel("", 108, 32, 18, 0xFFFFFF)
    GUICtrlSetFont($lblBatchBig, 18, 700)

    $lblBatch = _FloatLabel("", 142, 22)       ; 型号
    $lblBatchNo = _FloatLabel("", 164, 22)     ; 批次号
    $lblOperator = _FloatLabel("", 186, 22)
    $lblCircuit = _FloatLabel("", 208, 22)
    $lblSection = _FloatLabel("", 230, 22)
    $lblLength = _FloatLabel("", 252, 22)
    $lblStripL = _FloatLabel("", 274, 22)
    $lblStripR = _FloatLabel("", 296, 22)
    $lblWorkL = _FloatLabel("", 318, 34, 14)
    $lblWorkR = _FloatLabel("", 352, 34, 14)

    $lblQty = GUICtrlCreateLabel("", 16, 394, 190, 32)
    GUICtrlSetFont($lblQty, 16, 700)
    GUICtrlSetColor($lblQty, 0x43CF7C)
    $lblStatus = GUICtrlCreateLabel("", 214, 394, 200, 32)
    GUICtrlSetFont($lblStatus, 16, 700)
    GUICtrlSetColor($lblStatus, 0x9FB0C9)

    $btnPrev = GUICtrlCreateButton("上一条", 16, 445, 80, 46)
    $btnNext = GUICtrlCreateButton("下一条", 104, 445, 80, 46)
    $btnStart = GUICtrlCreateButton("开始/打印", 192, 445, 110, 46)
    $btnConfirm = GUICtrlCreateButton("已完成", 310, 445, 104, 46)
    $btnTray = GUICtrlCreateButton("最小化", 16, 500, 398, 42)
    _StyleBtn($btnPrev, $CLR_BTN_NEUTRAL)
    _StyleBtn($btnNext, $CLR_BTN_NEUTRAL)
    _StyleBtn($btnStart, $CLR_BTN_GREEN)
    _StyleBtn($btnConfirm, $CLR_BTN_RED)
    _StyleBtn($btnTray, $CLR_BTN_NEUTRAL)
    GUISetState(@SW_SHOW, $hFloat)

    $hMain = GUICreate("设备任务 - " & $gMachineName, $gMainW, $gMainH, 20, 20)
    GUISetBkColor(0x2B3046, $hMain)
    ; 自绘标签按钮 + 单个 ListView：彻底避开原生 Tab 的白色客户区
    $btnTabAll = GUICtrlCreateButton("全部任务", 18, 10, 106, 28)
    $btnTabPending = GUICtrlCreateButton("未完成任务", 130, 10, 124, 28)
    $btnTabDone = GUICtrlCreateButton("已完成任务", 260, 10, 124, 28)
    _RefreshTabButtons()
    $lvTasks = _CreateTaskList(18, 72, $gMainW - 36, $gMainH - 342)

    ; 状态筛选：四个按钮，风格与主按钮一致；设备端任务已分配，不显示待派工/已作废
    GUICtrlCreateLabel("状态：", $gMainW - 392, 46, 52, 22)
    GUICtrlSetColor(-1, 0xC8D2E4)
    $btnFilterAll = GUICtrlCreateButton("全部", $gMainW - 336, 40, 62, 28)
    $btnFilterPending = GUICtrlCreateButton("待生产", $gMainW - 268, 40, 62, 28)
    $btnFilterDoing = GUICtrlCreateButton("进行中", $gMainW - 200, 40, 62, 28)
    $btnFilterDone = GUICtrlCreateButton("已完成", $gMainW - 132, 40, 62, 28)
    _RefreshFilterButtons()

    $lblDetail = GUICtrlCreateLabel("", 18, $gMainH - 250, $gMainW - 36, 132)
    GUICtrlSetFont($lblDetail, 12, 400)
    GUICtrlSetColor($lblDetail, 0xDCE6F5)
    GUICtrlSetBkColor($lblDetail, 0x1F2536)

    ; 版本号 + 检查更新统一放到右下角，排在「设备设置」右边，不再占用上方信息区
    Local $by = $gMainH - 52
    Local $rightW = 470
    Local $rx = $gMainW - $rightW - 16
    $btnRefresh = GUICtrlCreateButton("刷新", 16, $by, 70, 36)
    $btnSetCurrent = GUICtrlCreateButton("设为当前任务", 90, $by, 110, 36)
    $btnMainPrint = GUICtrlCreateButton("打印标签", 204, $by, 80, 36)
    $btnMainStart = GUICtrlCreateButton("开始/打印", 288, $by, 96, 36)
    $btnMainConfirm = GUICtrlCreateButton("已完成", 388, $by, 72, 36)
    $btnBackFloat = GUICtrlCreateButton("返回浮窗", $rx, $by, 88, 36)
    $btnSettings = GUICtrlCreateButton("设备设置", $rx + 94, $by, 88, 36)
    $lblVersion = GUICtrlCreateLabel("v" & $gAppVer, $rx + 184, $by + 4, 190, 28, $SS_RIGHT)
    GUICtrlSetFont($lblVersion, 12, 700)
    GUICtrlSetColor($lblVersion, 0xC8D2E4)
    $btnCheckUpdate = GUICtrlCreateButton("检查更新", $rx + 374, $by, 96, 36)
    _StyleBtn($btnRefresh, $CLR_BTN_NEUTRAL)
    _StyleBtn($btnSetCurrent, $CLR_BTN_NEUTRAL)
    _StyleBtn($btnMainPrint, $CLR_BTN_NEUTRAL)
    _StyleBtn($btnMainStart, $CLR_BTN_GREEN)
    _StyleBtn($btnMainConfirm, $CLR_BTN_RED)
    _StyleBtn($btnBackFloat, $CLR_BTN_NEUTRAL)
    _StyleBtn($btnSettings, $CLR_BTN_NEUTRAL)
    _StyleBtn($btnCheckUpdate, $CLR_BTN_NEUTRAL)
    GUISetState(@SW_HIDE, $hMain)

    _BuildSettingsUi()
EndFunc

; 状态筛选按钮：-1=全部，1=待生产，2=进行中，3=已完成
Func _ApplyStatusFilter($iStatus)
    $gStatusFilter = $iStatus
    _RefreshFilterButtons()
    _FillLists()
EndFunc

Func _RefreshFilterButtons()
    If $gStatusFilter = -1 Then
        _StyleBtn($btnFilterAll, $CLR_BTN_GREEN)
    Else
        _StyleBtn($btnFilterAll, $CLR_BTN_NEUTRAL)
    EndIf
    If $gStatusFilter = 1 Then
        _StyleBtn($btnFilterPending, $CLR_BTN_GREEN)
    Else
        _StyleBtn($btnFilterPending, $CLR_BTN_NEUTRAL)
    EndIf
    If $gStatusFilter = 2 Then
        _StyleBtn($btnFilterDoing, $CLR_BTN_GREEN)
    Else
        _StyleBtn($btnFilterDoing, $CLR_BTN_NEUTRAL)
    EndIf
    If $gStatusFilter = 3 Then
        _StyleBtn($btnFilterDone, $CLR_BTN_GREEN)
    Else
        _StyleBtn($btnFilterDone, $CLR_BTN_NEUTRAL)
    EndIf
EndFunc

; 自绘标签按钮：0=全部 / 1=未完成 / 2=已完成
Func _SwitchTab($iTab)
    $gCurTab = $iTab
    _RefreshTabButtons()
    _FillLists()
EndFunc

Func _RefreshTabButtons()
    If $gCurTab = 0 Then
        _StyleBtn($btnTabAll, $CLR_BTN_GREEN)
    Else
        _StyleBtn($btnTabAll, $CLR_BTN_NEUTRAL)
    EndIf
    If $gCurTab = 1 Then
        _StyleBtn($btnTabPending, $CLR_BTN_GREEN)
    Else
        _StyleBtn($btnTabPending, $CLR_BTN_NEUTRAL)
    EndIf
    If $gCurTab = 2 Then
        _StyleBtn($btnTabDone, $CLR_BTN_GREEN)
    Else
        _StyleBtn($btnTabDone, $CLR_BTN_NEUTRAL)
    EndIf
EndFunc

; 创建原生 ListView；原生表头是白色系统风格，改为自绘深色标题 Label + 隐藏原生表头
Func _CreateTaskList($iX, $iY, $iW, $iH)
    Local $headers[16] = ["序号", "任务号", "型号", "批次号", "原表序号", "回路", "材质", "截面", _
        "颜色", "长度", "左压前", "右压前", "数量", "完成", "状态", "操作员"]
    ; 基准权重：任务号收窄、型号加宽；左端子/右端子移出表格，只在下方详情显示
    Local $w[16] = [30, 110, 140, 60, 40, 60, 70, 40, 40, 60, 150, 150, 30, 30, 70, 80]
    Local $sum = 0
    For $c = 0 To 15
        $sum += $w[$c]
    Next
    Local $avail = $iW - 20
    Local $scale = $avail / $sum
    Local $fixed[16]
    For $c = 0 To 15
        $fixed[$c] = Int($w[$c] * $scale)
        If $fixed[$c] < 22 Then $fixed[$c] = 22
    Next

    Local $headerY = $iY
    Local $listY = $iY + 26
    Local $id = GUICtrlCreateListView(_ArrayJoin($headers, "|"), _
        $iX, $listY, $iW, $iH - 26, BitOR($LVS_REPORT, $LVS_SINGLESEL, $WS_VSCROLL, 0x4000))
    For $c = 0 To 15
        _GUICtrlListView_SetColumn($id, $c, $headers[$c], $fixed[$c], 2)
    Next
    ; 自绘表头：深底浅字，避免原生表头白色背景
    Local $x = $iX
    For $c = 0 To 15
        Local $hHead = GUICtrlCreateLabel($headers[$c], $x, $headerY, $fixed[$c], 24, $SS_CENTER)
        GUICtrlSetFont($hHead, 10, 700, 0, "Microsoft YaHei")
        GUICtrlSetColor($hHead, 0xDCE6F5)
        GUICtrlSetBkColor($hHead, 0x252C40)
        $x += $fixed[$c]
    Next

    GUICtrlSetFont($id, 11, 700, 0, "Microsoft YaHei")
    _GUICtrlListView_SetExtendedListViewStyle($id, BitOR(0x20, 0x1, 0x10000, 0x400))
    _GUICtrlListView_SetBkColor($id, _ColorRef(31, 37, 54))
    _GUICtrlListView_SetTextBkColor($id, _ColorRef(31, 37, 54))
    _GUICtrlListView_SetTextColor($id, _ColorRef(220, 230, 245))
    Local $img = _GUIImageList_Create(1, 36, 5, 1)
    _GUICtrlListView_SetImageList($id, $img, 1)
    Return $id
EndFunc

Func _ArrayJoin($a, $sep)
    Local $s = ""
    For $i = 0 To UBound($a) - 1
        If $i > 0 Then $s &= $sep
        $s &= $a[$i]
    Next
    Return $s
EndFunc

Func _BuildSettingsUi()
    $hSet = GUICreate("设备设置", 540, 580, -1, -1, BitOR($WS_SYSMENU, $WS_CAPTION))
    GUISetBkColor(0x2B3046, $hSet)
    GUICtrlCreateLabel("服务器地址：", 20, 20, 100, 24)
    GUICtrlSetColor(-1, 0xFFFFFF)
    $inpServer = GUICtrlCreateInput($gServer, 130, 16, 380, 28)
    GUICtrlCreateLabel("机台号：", 20, 60, 100, 24)
    GUICtrlSetColor(-1, 0xFFFFFF)
    $cmbMachine = GUICtrlCreateCombo("", 130, 56, 150, 28, $CBS_DROPDOWNLIST)
    GUICtrlSetData($cmbMachine, "一号机|二号机|三号机|四号机|五号机", $gMachineName)
    GUICtrlCreateLabel("设备密钥：", 20, 100, 100, 24)
    GUICtrlSetColor(-1, 0xFFFFFF)
    $inpDeviceKey = GUICtrlCreateCombo("", 130, 96, 380, 28, $CBS_DROPDOWNLIST)
    GUICtrlSetData($inpDeviceKey, "ND-M1|ND-M2|ND-M3|ND-M4|ND-M5", $gDeviceKey)
    GUICtrlCreateLabel("打印机：", 20, 140, 100, 24)
    GUICtrlSetColor(-1, 0xFFFFFF)
    $cmbPrinter = GUICtrlCreateCombo("", 130, 136, 250, 200)
    ; 预填充系统打印机列表（WMI 枚举）
    Local $aP = _ListPrinters()
    Local $sList = "<默认打印机>*"
    For $i = 1 To $aP[0]
        $sList &= "|" & $aP[$i]
    Next
    If $gPrinterName <> "" Then
        GUICtrlSetData($cmbPrinter, $sList, $gPrinterName)
    Else
        GUICtrlSetData($cmbPrinter, $sList, "<默认打印机>*")
    EndIf
    $btnEnumPrinters = GUICtrlCreateButton("刷新列表", 390, 134, 110, 28)
    GUICtrlSetFont($btnEnumPrinters, 9, 400)
    GUICtrlCreateLabel("操作员：", 20, 180, 100, 24)
    GUICtrlSetColor(-1, 0xFFFFFF)
    $inpOperator = GUICtrlCreateInput($gOperator, 130, 176, 380, 28)

    ; ===== 打印机信息区：打印机下拉已列出全部，这里只保留配置和默认打印机 =====
    GUICtrlCreateLabel("— 打印机信息 —", 20, 214, 120, 20)
    GUICtrlSetColor(-1, 0x8892B0)
    $lblPrinterInfo = GUICtrlCreateLabel("配置打印机：" & $gPrinterName & @CRLF & "系统默认：<未检测>", 20, 238, 480, 50)
    GUICtrlSetColor($lblPrinterInfo, 0xFFFFFF)
    GUICtrlSetBkColor($lblPrinterInfo, 0x1F2536)
    $btnPrinterTest = GUICtrlCreateButton("测试打印（标签）", 20, 300, 150, 30)
    GUICtrlSetFont($btnPrinterTest, 9, 400)

    GUICtrlCreateLabel("标签宽(mm)：", 20, 350, 100, 24)
    GUICtrlSetColor(-1, 0xFFFFFF)
    $inpPaperW = GUICtrlCreateInput(String($gPaperW), 130, 346, 80, 28)
    GUICtrlCreateLabel("标签高(mm)：", 230, 350, 100, 24)
    GUICtrlSetColor(-1, 0xFFFFFF)
    $inpPaperH = GUICtrlCreateInput(String($gPaperH), 340, 346, 80, 28)
    GUICtrlCreateLabel("间距(mm)：", 20, 390, 100, 24)
    GUICtrlSetColor(-1, 0xFFFFFF)
    $inpGap = GUICtrlCreateInput(String($gGap), 130, 386, 80, 28)
    GUICtrlCreateLabel("字体：", 230, 390, 60, 24)
    GUICtrlSetColor(-1, 0xFFFFFF)
    $inpFont = GUICtrlCreateInput($gLabelFont, 300, 386, 210, 28)
    GUICtrlCreateLabel("新密码：", 20, 430, 100, 24)
    GUICtrlSetColor(-1, 0xFFFFFF)
    $inpNewPassword = GUICtrlCreateInput("", 130, 426, 200, 28, $ES_PASSWORD)
    GUICtrlCreateLabel("默认 0000，留空表示不修改", 345, 430, 180, 24)
    GUICtrlSetColor(-1, 0xFFFFFF)

    $btnSetSave = GUICtrlCreateButton("保存", 330, 500, 90, 36)
    $btnSetCancel = GUICtrlCreateButton("取消", 430, 500, 90, 36)
    _StyleBtn($btnSetSave, $CLR_BTN_GREEN)
    _StyleBtn($btnSetCancel, $CLR_BTN_NEUTRAL)
    _StyleBtn($btnEnumPrinters, $CLR_BTN_NEUTRAL)
    _StyleBtn($btnPrinterTest, $CLR_BTN_GREEN)
    GUISetState(@SW_HIDE, $hSet)
EndFunc

Func _AskPassword()
    Local $h = GUICreate("设置密码", 340, 130, -1, -1, BitOR($WS_SYSMENU, $WS_CAPTION))
    GUISetBkColor(0x2B3046, $h)
    GUICtrlCreateLabel("请输入设置密码：", 20, 24, 130, 24)
    GUICtrlSetColor(-1, 0xFFFFFF)
    Local $inp = GUICtrlCreateInput("", 160, 20, 150, 28, $ES_PASSWORD)
    Local $ok = GUICtrlCreateButton("确定", 100, 80, 80, 30)
    Local $cancel = GUICtrlCreateButton("取消", 190, 80, 80, 30)
    _StyleBtn($ok, $CLR_BTN_GREEN)
    _StyleBtn($cancel, $CLR_BTN_NEUTRAL)
    GUISetState(@SW_SHOW, $h)
    While 1
        Local $m = GUIGetMsg()
        If $m = $ok Then
            Local $v = GUICtrlRead($inp)
            GUIDelete($h)
            Return $v
        EndIf
        If $m = $cancel Or $m = $GUI_EVENT_CLOSE Then
            GUIDelete($h)
            Return ""
        EndIf
    WEnd
EndFunc

; 检查更新 + 下载 + 覆盖安装
Func _DoCheckUpdate()
    Local $sArchText = "32 位"
    If $gAppArch = "x64" Then $sArchText = "64 位"

    ; 1. 按当前程序位数查询服务器上的下线机版本
    Local $sInfo
    If Not _HttpGet(_Url("/api/update/device/info?arch=" & $gAppArch), $sInfo, 5000) Then
        _ShowServerError("检查更新")
        Return
    EndIf

    Local $bAvail = _JsonGetBool($sInfo, "available", False)
    Local $sVer   = _JsonGetString($sInfo, "version", "")
    Local $iSize  = _JsonGetInt($sInfo, "size", 0)
    Local $sServerArch = _JsonGetString($sInfo, "arch", "")
    If $sServerArch <> "" And $sServerArch <> $gAppArch Then
        MsgBox(16, "检查更新失败", "当前程序是" & $sArchText & "，服务器返回的是 " & $sServerArch & " 安装包。" & @CRLF & _
            "请在 PC 端设置页分别上传 32 位和 64 位下线机 exe。")
        Return
    EndIf
    If Not $bAvail Or $sVer = "" Then
        MsgBox(64, "检查更新", "服务器没有可用的" & $sArchText & "下线机安装包。")
        Return
    EndIf

    ; 2. 提示用户：显示版本信息，询问是否下载
    Local $sMsg = "服务器最新版本（" & $sArchText & "）：" & $sVer & @CRLF
    If $iSize > 0 Then $sMsg &= "文件大小：" & Round($iSize / 1048576, 2) & " MB" & @CRLF
    $sMsg &= @CRLF & "当前版本：v" & $gAppVer & @CRLF & @CRLF
    If $sVer = $gAppVer Then
        $sMsg &= "服务器版本与当前一致，仍可下载覆盖安装（相当于重装）。" & @CRLF & @CRLF
    Else
        $sMsg &= "是否下载并覆盖安装最新版本？" & @CRLF & @CRLF
    EndIf
    $sMsg &= "下载完成后会自动退出并重启。"

    Local $res = MsgBox(4, "检查更新", $sMsg)
    If $res <> 6 Then Return  ; 不是 Yes 就取消

    ; 3. 下载到临时文件（32/64 位分开，避免相互覆盖）
    Local $sExeDir  = @ScriptDir
    Local $sExeName = StringRegExpReplace(@AutoItExe, "^.*\\", "")
    If $sExeName = "" Then $sExeName = "下线机设备.exe"
    Local $sMyPid   = @AutoItPID   ; 用 PID 等待自身退出，避免中文进程名在 XP cmd 里匹配失败
    Local $sNewExe  = @TempDir & "\CutDevice_" & $gAppArch & "_new.exe"
    Local $sCmdPath = @TempDir & "\CutDevice_" & $gAppArch & "_update.cmd"
    If FileExists($sNewExe) Then FileDelete($sNewExe)

    ToolTip("正在下载更新...", 0, 0)
    Local $iProg = 0
    Local $bOk = _HttpGetBinary(_Url("/api/update/device/package?arch=" & $gAppArch), $sNewExe, $iProg, 120000)
    ToolTip("")

    If Not $bOk Or Not FileExists($sNewExe) Then
        FileDelete($sNewExe)
        MsgBox(16, "下载失败", "无法下载安装包。请检查网络或服务器配置。")
        Return
    EndIf

    ; 4. 生成批处理：等待当前进程退出 → move 替换 → 重启 → 自删
    ;    Windows XP 没有 timeout.exe，用 ping 延时；%VAR% 不使用延迟展开，兼容 XP cmd
    ; 不切 chcp：AutoIt FileWrite 写的是系统 ANSI 编码，切到 65001 会让中文路径乱码
    Local $sBatch = '@echo off' & @CRLF & _
        'echo.' & @CRLF & _
        'echo ========================================' & @CRLF & _
        'echo   南都下线机 正在更新...' & @CRLF & _
        'echo ========================================' & @CRLF & _
        'echo.' & @CRLF & _
        ':wait_loop' & @CRLF & _
        'tasklist /fi "PID eq ' & $sMyPid & '" 2>nul | find "' & $sMyPid & '" >nul' & @CRLF & _
        'if errorlevel 1 goto do_update' & @CRLF & _
        'ping 127.0.0.1 -n 2 >nul' & @CRLF & _
        'goto wait_loop' & @CRLF & _
        ':do_update' & @CRLF & _
        'set "TARGET=' & $sExeDir & '\' & $sExeName & '"' & @CRLF & _
        'set "NEWFILE=' & $sNewExe & '"' & @CRLF & _
        'move /Y "%NEWFILE%" "%TARGET%" >nul' & @CRLF & _
        'if errorlevel 1 (' & @CRLF & _
        '    echo 覆盖失败！请手动替换 ' & $sExeName & @CRLF & _
        '    pause' & @CRLF & _
        '    del "%~f0"' & @CRLF & _
        '    exit /b 1' & @CRLF & _
        ')' & @CRLF & _
        'echo.' & @CRLF & _
        'echo 更新完成，正在重启...' & @CRLF & _
        'start "" "%TARGET%"' & @CRLF & _
        'del "%~f0"' & @CRLF & _
        'exit /b 0' & @CRLF

    FileWrite($sCmdPath, $sBatch)
    FileSetAttrib($sCmdPath, "-R")

    ; 5. 运行批处理，退出当前程序
    Local $sDoneMsg = "下载完成，即将退出程序并自动重启..." & @CRLF & @CRLF & _
        "如果没有自动重启，请手动运行：" & @CRLF & $sCmdPath
    MsgBox(64, "准备重启", $sDoneMsg)
    ShellExecute($sCmdPath, "", "", "open", @SW_HIDE)
    Exit

EndFunc

; 刷新打印机简要信息；系统已安装打印机列表在「打印机」下拉里已经能看到
Func _EnumShowPrinters()
    Local $sInfo = "配置打印机：" & $gPrinterName
    Local $sDefault = _GetDefaultPrinterName()
    If $sDefault <> "" Then $sInfo &= @CRLF & "系统默认打印机：" & $sDefault
    GUICtrlSetData($lblPrinterInfo, $sInfo)
EndFunc

; 获取系统默认打印机名称 —— 读注册表，比 GetDefaultPrinterW DllCall 更稳
; HKCU\Software\Microsoft\Windows NT\CurrentVersion\Windows 下 Device 值
; 格式: "Microsoft Print to PDF,winspool,FILE:" → 取第一个逗号前的
Func _GetDefaultPrinterName()
    Local $sDevice = RegRead("HKCU\Software\Microsoft\Windows NT\CurrentVersion\Windows", "Device")
    If @error Or $sDevice = "" Then Return ""
    Local $iComma = StringInStr($sDevice, ",")
    If $iComma > 0 Then Return StringLeft($sDevice, $iComma - 1)
    Return $sDevice
EndFunc

; 测试打印 —— 造一条假任务打印一条标签
Func _TestPrint()
    Local $sSelected = GUICtrlRead($cmbPrinter)
    If $sSelected = "" Then
        Local $res = MsgBox(4, "测试打印", "当前打印机字段为空，将使用系统默认打印机测试。" & @CRLF & @CRLF & "继续？")
        If $res <> 6 Then Return
    EndIf
    Local $aFake[$T_COLS]
    $aFake[$T_ID] = 0
    $aFake[$T_BATCH]     = "测试型号 TEST-001"
    $aFake[$T_SEQ]       = "999"
    $aFake[$T_CIRCUIT]   = "T-001"
    $aFake[$T_MATERIAL]  = "测试材质"
    $aFake[$T_SECTION]   = "0.5mm2"
    $aFake[$T_COLOR]     = "红"
    $aFake[$T_LENGTH]    = "1000"
    $aFake[$T_STRIP_L]   = "15"
    $aFake[$T_STRIP_R]   = "25"
    $aFake[$T_TERM_L]    = "DJ611-4.8A"
    $aFake[$T_TERM_R]    = "DJ611-4.8B"
    $aFake[$T_WORK_L]    = "左压前作业测试内容"
    $aFake[$T_WORK_R]    = "右压前作业测试内容"
    $aFake[$T_BATCHNO]   = "B20261118-TEST"
    $aFake[$T_QTY]       = 1
    $aFake[$T_DONE]      = 0
    $aFake[$T_STATUS]    = 0
    $aFake[$T_OPERATOR]  = $gOperator
    Local $sErr = ""
    If _PrintTaskLabel($aFake, 1, 1, $sErr) Then
        MsgBox(64, "测试打印", "测试标签已发送到打印机！" & @CRLF & @CRLF & "请确认打印机是否出纸。")
    Else
        MsgBox(16, "测试打印失败", $sErr & @CRLF & @CRLF & "请查看 D:\ND_DrawingManagement_Device\logs 目录下的日志文件。")
    EndIf
EndFunc

Func _OpenSettings()
    If _AskPassword() <> $gPassword Then
        MsgBox(16, "密码错误", "设置密码不正确。")
        Return
    EndIf
    GUICtrlSetData($inpServer, $gServer)
    GUICtrlSetData($cmbMachine, $gMachineName)
    GUICtrlSetData($inpDeviceKey, $gDeviceKey)
    GUICtrlSetData($cmbPrinter, $gPrinterName)
    GUICtrlSetData($inpOperator, $gOperator)
    GUICtrlSetData($inpPaperW, String($gPaperW))
    GUICtrlSetData($inpPaperH, String($gPaperH))
    GUICtrlSetData($inpGap, String($gGap))
    GUICtrlSetData($inpFont, $gLabelFont)
    GUICtrlSetData($inpNewPassword, "")
    GUISetState(@SW_SHOW, $hSet)
    ; 不在此处自动枚举 —— DllCall 可能因空指针崩溃，改成用户点按钮时才执行
EndFunc

Func _SaveSettings()
    Local $name = GUICtrlRead($cmbMachine)
    Local $no = 1
    Switch $name
        Case "二号机"
            $no = 2
        Case "三号机"
            $no = 3
        Case "四号机"
            $no = 4
        Case "五号机"
            $no = 5
    EndSwitch
    Local $pwd = GUICtrlRead($inpNewPassword)
    If $pwd = "" Then $pwd = $gPassword

    ; 份数固定 1：每根线只打印一张标签，设置页不再暴露该测试用字段
    _ConfigSave(GUICtrlRead($inpServer), $no, _MachineName($no), GUICtrlRead($inpDeviceKey), _
        GUICtrlRead($cmbPrinter), 1, GUICtrlRead($inpOperator), $pwd, _
        Int(GUICtrlRead($inpPaperW)), Int(GUICtrlRead($inpPaperH)), Int(GUICtrlRead($inpGap)), _
        GUICtrlRead($inpFont))
    $gToken = ""
    GUICtrlSetData($lblFloatTitle, "当前机器  " & $gMachineName)
    TraySetToolTip("下线机设备端 - " & $gMachineName)
    GUISetState(@SW_HIDE, $hSet)
    MsgBox(64, "已保存", "设备设置已保存。")
EndFunc

Func _RefreshTasks($bShowMsg)
    Local $a
    If Not _ApiTasks($a) Then
        If $bShowMsg Then
            If $gLastError <> "" And $gLastError <> "无法连接服务器" Then
                MsgBox(16, "读取任务失败", $gLastError & @CRLF & @CRLF & "请检查服务器设置后重试。")
            Else
                _ShowServerError("读取任务")
            EndIf
        EndIf
        Return False
    EndIf
    $gTasks = $a
    If $gCurrent >= UBound($gTasks) Then $gCurrent = 0
    _FillLists()
    _UpdateFloat()
    Return True
EndFunc

Func _FillLists()
    Switch $gCurTab
        Case 0
            _FillOneList($lvTasks, "all", $gMapAll)
        Case 1
            _FillOneList($lvTasks, "pending", $gMapPending)
        Case 2
            _FillOneList($lvTasks, "done", $gMapDone)
    EndSwitch
    _ShowTaskDetail($gCurrent)
EndFunc

; 往原生 ListView 里填行
Func _FillOneList($iLv, $sFilter, ByRef $map)
    _GUICtrlListView_DeleteAllItems($iLv)
    ReDim $map[1]
    If UBound($gTasks) <= 1 Then Return
    Local $cnt = 0
    For $i = 0 To UBound($gTasks) - 1
        Local $st = Int($gTasks[$i][$T_STATUS])
        If $sFilter = "pending" And ($st = 3 Or $st = 4) Then ContinueLoop
        If $sFilter = "done" And $st <> 3 Then ContinueLoop
        If $gStatusFilter >= 0 And $st <> $gStatusFilter Then ContinueLoop
        Local $oper = $gTasks[$i][$T_OPERATOR]
        If $oper = "" Then $oper = $gOperator
        ; 第 1 列（序号）作为主项
        Local $rowIdx = _GUICtrlListView_AddItem($iLv, String($cnt + 1), -1)
        ; 其余 15 列：型号 + 批次号，左/右端子只在详情区显示
        _GUICtrlListView_AddSubItem($iLv, $rowIdx, $gTasks[$i][$T_TASKNO], 1)
        _GUICtrlListView_AddSubItem($iLv, $rowIdx, $gTasks[$i][$T_BATCH], 2)
        _GUICtrlListView_AddSubItem($iLv, $rowIdx, $gTasks[$i][$T_BATCHNO], 3)
        _GUICtrlListView_AddSubItem($iLv, $rowIdx, String($gTasks[$i][$T_SEQ]), 4)
        _GUICtrlListView_AddSubItem($iLv, $rowIdx, $gTasks[$i][$T_CIRCUIT], 5)
        _GUICtrlListView_AddSubItem($iLv, $rowIdx, $gTasks[$i][$T_MATERIAL], 6)
        _GUICtrlListView_AddSubItem($iLv, $rowIdx, $gTasks[$i][$T_SECTION], 7)
        _GUICtrlListView_AddSubItem($iLv, $rowIdx, $gTasks[$i][$T_COLOR], 8)
        _GUICtrlListView_AddSubItem($iLv, $rowIdx, $gTasks[$i][$T_LENGTH], 9)
        _GUICtrlListView_AddSubItem($iLv, $rowIdx, $gTasks[$i][$T_WORK_L], 10)
        _GUICtrlListView_AddSubItem($iLv, $rowIdx, $gTasks[$i][$T_WORK_R], 11)
        _GUICtrlListView_AddSubItem($iLv, $rowIdx, String($gTasks[$i][$T_QTY]), 12)
        _GUICtrlListView_AddSubItem($iLv, $rowIdx, String($gTasks[$i][$T_DONE]), 13)
        _GUICtrlListView_AddSubItem($iLv, $rowIdx, _StatusText($st), 14)
        _GUICtrlListView_AddSubItem($iLv, $rowIdx, $oper, 15)
        ; 映射表：map[cnt] = gTasks 的原始索引 i
        ReDim $map[$cnt + 1]
        $map[$cnt] = $i
        $cnt += 1
    Next
EndFunc

Func _ShowTaskDetail($iTask)
    If UBound($gTasks) <= 1 Or $iTask < 0 Or $iTask >= UBound($gTasks) Then
        GUICtrlSetData($lblDetail, "")
        Return
    EndIf
    Local $r = $iTask
    Local $oper = $gTasks[$r][$T_OPERATOR]
    If $oper = "" Then $oper = $gOperator
    Local $s = _
        "任务号:" & $gTasks[$r][$T_TASKNO] & "    原表序号:" & $gTasks[$r][$T_SEQ] & "    型号:" & $gTasks[$r][$T_BATCH] & _
        "    批次号:" & $gTasks[$r][$T_BATCHNO] & @CRLF & _
        "线号:" & $gTasks[$r][$T_CIRCUIT] & "    材质:" & $gTasks[$r][$T_MATERIAL] & "    截面:" & $gTasks[$r][$T_SECTION] & _
        "    颜色:" & $gTasks[$r][$T_COLOR] & "    长度:" & $gTasks[$r][$T_LENGTH] & @CRLF & _
        "左端子:" & $gTasks[$r][$T_TERM_L] & "    右端子:" & $gTasks[$r][$T_TERM_R] & @CRLF & _
        "左剥皮:" & $gTasks[$r][$T_STRIP_L] & "    右剥皮:" & $gTasks[$r][$T_STRIP_R] & @CRLF & _
        "左压前:" & $gTasks[$r][$T_WORK_L] & @CRLF & _
        "右压前:" & $gTasks[$r][$T_WORK_R] & @CRLF & _
        "数量:" & $gTasks[$r][$T_QTY] & "    完成:" & $gTasks[$r][$T_DONE] & _
        "    状态:" & _StatusText($gTasks[$r][$T_STATUS]) & "    操作员:" & $oper
    GUICtrlSetData($lblDetail, $s)
EndFunc

; 从原生 ListView 读当前选中行，用映射表转成 $gTasks 的索引
Func _SelectFromList($iLv, ByRef $map)
    Local $aSel = _GUICtrlListView_GetSelectedIndices($iLv, True)
    If @error Or Not IsArray($aSel) Then Return
    If $aSel[0] = 0 Then Return
    Local $rowIdx = $aSel[1]
    If $rowIdx < 0 Or $rowIdx >= UBound($map) Then Return
    $gCurrent = $map[$rowIdx]
    _ShowTaskDetail($gCurrent)
EndFunc

; 主动轮询：查当前 Tab 页的 ListView 选中状态，变化时刷新详情
; 比 GUIRegisterMsg 可靠 100 倍——Tab 嵌套控件的 WM_NOTIFY 经常收不到
Func _PollSelection()
    Local $iCurTab = $gCurTab
    Local $map = $gMapAll
    Switch $iCurTab
        Case 0
            $map = $gMapAll
        Case 1
            $map = $gMapPending
        Case 2
            $map = $gMapDone
    EndSwitch
    Local $aSel = _GUICtrlListView_GetSelectedIndices($lvTasks, True)
    If Not IsArray($aSel) Or $aSel[0] = 0 Then Return
    Local $rowIdx = $aSel[1]
    If $rowIdx < 0 Or $rowIdx >= UBound($map) Then Return
    ; 对比缓存：选中没变就跳过（避免每帧重绘）
    If $iCurTab = $gLastSelLv And $rowIdx = $gLastSelIdx Then Return
    $gLastSelLv = $iCurTab
    $gLastSelIdx = $rowIdx
    $gCurrent = $map[$rowIdx]
    _ShowTaskDetail($gCurrent)
EndFunc

Func _StatusText($i)
    $i = Int($i)
    Switch $i
        Case 1
            Return "待生产"
        Case 2
            Return "进行中"
        Case 3
            Return "已完成"
        Case 4
            Return "已作废"
        Case Else
            Return "待派工"
    EndSwitch
EndFunc

Func _CurrentTask()
    Local $a[$T_COLS]
    If UBound($gTasks) <= 1 Or $gCurrent < 0 Or $gCurrent >= UBound($gTasks) Then Return $a
    For $c = 0 To $T_COLS - 1
        $a[$c] = $gTasks[$gCurrent][$c]
    Next
    Return $a
EndFunc

Func _UpdateFloat()
    If UBound($gTasks) <= 1 Then
        GUICtrlSetData($lblTaskNo, "暂无任务，请点主界面刷新")
        GUICtrlSetData($lblSeq, "")
        GUICtrlSetData($lblBatchBig, "")
        GUICtrlSetData($lblBatch, "")
        GUICtrlSetData($lblBatchNo, "")
        GUICtrlSetData($lblOperator, "")
        GUICtrlSetData($lblCircuit, "")
        GUICtrlSetData($lblSection, "")
        GUICtrlSetData($lblLength, "")
        GUICtrlSetData($lblStripL, "")
        GUICtrlSetData($lblStripR, "")
        GUICtrlSetData($lblWorkL, "")
        GUICtrlSetData($lblWorkR, "")
        GUICtrlSetData($lblQty, "")
        GUICtrlSetData($lblStatus, "")
        Return
    EndIf
    Local $a = _CurrentTask()
    Local $oper = $a[$T_OPERATOR]
    If $oper = "" Then $oper = $gOperator
    GUICtrlSetData($lblTaskNo, "任务 " & $a[$T_TASKNO])
    GUICtrlSetData($lblSeq, "原表序号 " & $a[$T_SEQ])
    GUICtrlSetData($lblBatchBig, $a[$T_BATCH])
    GUICtrlSetData($lblBatch, "型号:" & $a[$T_BATCH])
    GUICtrlSetData($lblBatchNo, "批次号:" & $a[$T_BATCHNO])
    GUICtrlSetData($lblOperator, "操作员:" & $oper)
    GUICtrlSetData($lblCircuit, "线号:" & $a[$T_CIRCUIT] & "    材质:" & $a[$T_MATERIAL])
    GUICtrlSetData($lblSection, "截面:" & $a[$T_SECTION] & "    颜色:" & $a[$T_COLOR])
    GUICtrlSetData($lblLength, "长度:" & $a[$T_LENGTH])
    GUICtrlSetData($lblStripL, "左剥皮:" & $a[$T_STRIP_L] & "    左端子:" & $a[$T_TERM_L])
    GUICtrlSetData($lblStripR, "右剥皮:" & $a[$T_STRIP_R] & "    右端子:" & $a[$T_TERM_R])
    GUICtrlSetData($lblWorkL, "左压前:" & $a[$T_WORK_L])
    GUICtrlSetData($lblWorkR, "右压前:" & $a[$T_WORK_R])
    GUICtrlSetData($lblQty, "数量:" & $a[$T_QTY])
    GUICtrlSetData($lblStatus, "状态:" & _StatusText($a[$T_STATUS]))
EndFunc

Func _PrevTask()
    If UBound($gTasks) <= 1 Then Return
    $gCurrent -= 1
    If $gCurrent < 0 Then $gCurrent = UBound($gTasks) - 1
    _UpdateFloat()
EndFunc

Func _NextTask()
    If UBound($gTasks) <= 1 Then Return
    $gCurrent += 1
    If $gCurrent >= UBound($gTasks) Then $gCurrent = 0
    _UpdateFloat()
EndFunc

Func _MoveNextPending()
    If UBound($gTasks) <= 1 Then Return
    Local $n = UBound($gTasks)
    For $k = 1 To $n
        Local $i = $gCurrent + $k
        If $i >= $n Then $i -= $n
        If $gTasks[$i][$T_STATUS] <> 3 And $gTasks[$i][$T_STATUS] <> 4 Then
            $gCurrent = $i
            ExitLoop
        EndIf
    Next
EndFunc

Func _ShowSubmitError($sMsg)
    MsgBox(16, "提交失败", "无法连接服务器，本次操作未保存。" & @CRLF & @CRLF & $sMsg & @CRLF & @CRLF & "请检查局域网后重试。")
EndFunc

Func _DoStart()
    If UBound($gTasks) <= 1 Then Return
    Local $a = _CurrentTask()
    Local $sResp = ""
    If Not _ApiStart($a[$T_ID], $gMachineNo, $sResp) Then
        _ShowSubmitError($sResp)
        Return
    EndIf
    $gTasks[$gCurrent][$T_STATUS] = 2
    Local $sErr = ""
    If Not _PrintTaskLabel($a, 1, $a[$T_QTY], $sErr) Then MsgBox(48, "打印失败", $sErr)
    _FillLists()
    _UpdateFloat()
EndFunc

; 点「已完成」= 整条任务一次完成（不按数量分批），完成后自动跳到下一条
Func _DoConfirm()
    If UBound($gTasks) <= 1 Then Return
    Local $a = _CurrentTask()
    Local $iRest = $a[$T_QTY] - $a[$T_DONE]
    If $iRest < 1 Then $iRest = 1
    Local $sResp = ""
    If Not _ApiConfirm($a[$T_ID], $gMachineNo, $gOperator, $sResp, $iRest) Then
        _ShowSubmitError($sResp)
        Return
    EndIf
    $gTasks[$gCurrent][$T_DONE] = _JsonGetInt($sResp, "doneQty", $a[$T_QTY])
    $gTasks[$gCurrent][$T_STATUS] = _JsonGetInt($sResp, "status", 3)
    _MoveNextPending()
    _FillLists()
    _UpdateFloat()
EndFunc

Func _DoPrint()
    If UBound($gTasks) <= 1 Then Return
    Local $a = _CurrentTask()
    Local $sErr = ""
    If Not _PrintTaskLabel($a, 1, $a[$T_QTY], $sErr) Then MsgBox(48, "打印失败", $sErr)
EndFunc

Func _MainSetCurrent()
    _UpdateFloat()
    GUISetState(@SW_HIDE, $hMain)
    GUISetState(@SW_SHOW, $hFloat)
EndFunc

Func _FloatDrag()
    Local $a = GUIGetCursorInfo($hFloat)
    If @error Or $a[4] <> $lblFloatTitle Then Return
    DllCall("user32.dll", "int", "ReleaseCapture")
    DllCall("user32.dll", "int", "SendMessage", "hwnd", $hFloat, "uint", 0x0112, "wparam", 0xF012, "lparam", 0)
EndFunc

Func _TrayShowFloat()
    GUISetState(@SW_SHOW, $hFloat)
    WinActivate($hFloat)
EndFunc

Func _TrayHideFloat()
    GUISetState(@SW_HIDE, $hFloat)
EndFunc

Func _TrayShowMain()
    _RefreshTasks(False)
    GUISetState(@SW_SHOW, $hMain)
    WinActivate($hMain)
EndFunc

Func _TrayExit()
    Exit
EndFunc
