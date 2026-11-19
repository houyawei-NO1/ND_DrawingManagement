; 下线机设备端 - 服务器接口封装

Global $gToken = ""
Global $gLastError = ""

Global Const $T_COLS = 22
Global Const $T_ID = 0
Global Const $T_SEQ = 1
Global Const $T_BATCH = 2
Global Const $T_CIRCUIT = 3
Global Const $T_MATERIAL = 4
Global Const $T_SECTION = 5
Global Const $T_COLOR = 6
Global Const $T_LENGTH = 7
Global Const $T_STRIP_L = 8
Global Const $T_STRIP_R = 9
Global Const $T_TERM_L = 10
Global Const $T_TERM_R = 11
Global Const $T_WORK_L = 12
Global Const $T_WORK_R = 13
Global Const $T_QTY = 14
Global Const $T_DONE = 15
Global Const $T_STATUS = 16
Global Const $T_MACHINE = 17
Global Const $T_CREATED = 18
Global Const $T_OPERATOR = 19
Global Const $T_BATCHNO = 20
Global Const $T_TASKNO = 21

Func _ResponseOk($sResp)
    Return _JsonGetBool($sResp, "ok", False)
EndFunc

Func _ResponseError($sResp)
    Local $s = _JsonGetString($sResp, "msg", "")
    If $s = "" Then $s = _JsonGetString($sResp, "error", "")
    If $s = "" Then $s = "服务器返回失败"
    Return $s
EndFunc

; 服务器通讯失败统一提示：替代 AutoIt 原生 COM 错误框
Func _ShowServerError($sScene)
    Local $sMsg = "无法与服务器通讯（" & $sScene & "）。" & @CRLF & @CRLF & _
        "服务器地址：" & $gServer & @CRLF & _
        "请检查：①服务器主机是否已启动  ②网线/WiFi 是否正常  ③服务器 IP 是否正确。" & @CRLF & @CRLF & _
        "本次操作未生效，修复后请重试。"
    MsgBox(16, "服务器连接失败", $sMsg)
EndFunc

Func _ApiAuth()
    Local $sBody = '{"machineNo":' & $gMachineNo & ',"deviceKey":"' & _JsonEscape($gDeviceKey) & '"}'
    Local $sResp
    If Not _HttpPost(_Url("/api/device/auth"), $sBody, $sResp) Then
        $gLastError = "无法连接服务器"
        Return SetError(1, 0, False)
    EndIf
    If Not _ResponseOk($sResp) Then
        $gLastError = _ResponseError($sResp)
        Return SetError(2, 0, $gLastError)
    EndIf
    Local $sToken = _JsonGetString($sResp, "token", "")
    If $sToken = "" Then
        $gLastError = "服务器未返回 token"
        Return SetError(3, 0, $gLastError)
    EndIf
    $gToken = $sToken
    Return True
EndFunc

Func _EnsureToken()
    If $gToken <> "" Then Return True
    Return _ApiAuth()
EndFunc

Func _TaskFromJson($sObj)
    Local $a[$T_COLS]
    $a[$T_ID] = _JsonGetInt($sObj, "id", 0)
    $a[$T_SEQ] = _JsonGetInt($sObj, "seq", 0)
    $a[$T_BATCH] = _JsonGetString($sObj, "batchName", "")
    $a[$T_CIRCUIT] = _JsonGetString($sObj, "circuit", "")
    $a[$T_MATERIAL] = _JsonGetString($sObj, "material", "")
    $a[$T_SECTION] = _JsonGetString($sObj, "section", "")
    $a[$T_COLOR] = _JsonGetString($sObj, "color", "")
    $a[$T_LENGTH] = _JsonGetString($sObj, "wireLength", "")
    $a[$T_STRIP_L] = _JsonGetString($sObj, "stripLeft", "")
    $a[$T_STRIP_R] = _JsonGetString($sObj, "stripRight", "")
    $a[$T_TERM_L] = _JsonGetString($sObj, "terminalLeft", "")
    $a[$T_TERM_R] = _JsonGetString($sObj, "terminalRight", "")
    $a[$T_WORK_L] = _JsonGetString($sObj, "workLeft", "")
    $a[$T_WORK_R] = _JsonGetString($sObj, "workRight", "")
    $a[$T_QTY] = _JsonGetInt($sObj, "quantity", 1)
    $a[$T_DONE] = _JsonGetInt($sObj, "doneQty", 0)
    $a[$T_STATUS] = _JsonGetInt($sObj, "status", 1)
    $a[$T_MACHINE] = _JsonGetInt($sObj, "machineId", 0)
    $a[$T_CREATED] = _JsonGetString($sObj, "batchCreatedAt", "")
    $a[$T_OPERATOR] = _JsonGetString($sObj, "operator", "")
    $a[$T_BATCHNO] = _JsonGetString($sObj, "batchNo", "")
    If $a[$T_BATCHNO] = "" Then $a[$T_BATCHNO] = $a[$T_BATCH]
    $a[$T_TASKNO] = _JsonGetString($sObj, "taskNo", "")
    If $a[$T_TASKNO] = "" Then $a[$T_TASKNO] = String($a[$T_ID])
    Return $a
EndFunc

Func _ApiTasks(ByRef $aTasks)
    Local $aEmpty[1][$T_COLS]
    $aTasks = $aEmpty
    If Not _EnsureToken() Then Return SetError(1, 0, False)

    Local $sBody = '{"action":"cut_query","what":"tasks","machineId":' & $gMachineNo & '}'
    Local $sResp
    If Not _HttpPost(_Url("/api/data"), $sBody, $sResp) Then
        $gLastError = "无法连接服务器"
        Return SetError(2, 0, False)
    EndIf
    If Not _ResponseOk($sResp) Then
        $gLastError = _ResponseError($sResp)
        $gToken = ""
        Return SetError(3, 0, $gLastError)
    EndIf

    Local $aObjs = _JsonExtractObjects($sResp, "items")
    Local $n = UBound($aObjs)
    If $n > 0 Then
        ReDim $aTasks[$n][$T_COLS]
        For $i = 0 To $n - 1
            Local $aRow = _TaskFromJson($aObjs[$i])
            For $c = 0 To $T_COLS - 1
                $aTasks[$i][$c] = $aRow[$c]
            Next
        Next
    EndIf
    Return True
EndFunc

Func _ApiWrite($sOp, $sExtra, ByRef $sResp)
    If Not _EnsureToken() Then Return SetError(1, 0, False)
    Local $sBody = '{"token":"' & _JsonEscape($gToken) & '","action":"cut_write","op":"' & $sOp & '"' & $sExtra & '}'
    If Not _HttpPost(_Url("/api/write"), $sBody, $sResp) Then
        $sResp = "无法连接服务器"
        $gLastError = $sResp
        Return SetError(2, 0, False)
    EndIf
    If Not _ResponseOk($sResp) Then
        Local $sMsg = _ResponseError($sResp)
        $gLastError = $sMsg
        $sResp = $sMsg
        If StringInStr($sMsg, "登录已失效") > 0 Or StringInStr($sMsg, "token") > 0 Then $gToken = ""
        Return SetError(3, 0, $sMsg)
    EndIf
    Return True
EndFunc

Func _ApiStart($iTaskId, $iMachineId, ByRef $sResp)
    Local $sExtra = ',"taskId":' & $iTaskId & ',"machineId":' & $iMachineId
    Return _ApiWrite("start_task", $sExtra, $sResp)
EndFunc

; 确认完成：默认一次报产 1 条；设备端点「已完成」时传整条任务剩余数量，一次完成
Func _ApiConfirm($iTaskId, $iMachineId, $sOperator, ByRef $sResp, $iQty = 1)
    If $iQty < 1 Then $iQty = 1
    Local $sExtra = ',"taskId":' & $iTaskId & ',"machineId":' & $iMachineId & _
        ',"operator":"' & _JsonEscape($sOperator) & '","qty":' & $iQty
    Return _ApiWrite("confirm_task", $sExtra, $sResp)
EndFunc

Func _ApiLogPrint($iTaskId, $iMachineId, $sOperator, $sLabelNo, $iCopies, $sResult, ByRef $sResp)
    Local $sExtra = ',"taskId":' & $iTaskId & ',"machineId":' & $iMachineId & _
        ',"operator":"' & _JsonEscape($sOperator) & '"' & _
        ',"labelNo":"' & _JsonEscape($sLabelNo) & '"' & _
        ',"copies":' & $iCopies & ',"result":"' & _JsonEscape($sResult) & '"'
    Return _ApiWrite("log_print", $sExtra, $sResp)
EndFunc
