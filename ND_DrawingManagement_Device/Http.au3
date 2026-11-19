; 下线机设备端 - HTTP 模块（WinHttp，XP SP3 可用）
;
; 重要：WinHttp 在服务器不可达时会抛 COM 异常。若不注册 AutoIt.Error 处理器，
; AutoIt 会弹原生错误框「The requested action with this object has failed」并终止脚本。
; 处理器只记日志、不访问异常对象属性，避免递归触发新的 COM 错误。
Func _ComErrorHandler($oComErr)
    _LogLine("COM 调用失败：服务器或系统组件暂不可用")
EndFunc

; POST JSON 请求
Func _HttpPost($sUrl, $sBody, ByRef $sResponse, $iTimeoutMs = 12000)
    $sResponse = ""
    Local $oHttp = ObjCreate("WinHttp.WinHttpRequest.5.1")
    If @error Or Not IsObj($oHttp) Then Return SetError(1, 0, False)

    $oHttp.SetTimeouts(5000, 5000, $iTimeoutMs, $iTimeoutMs)
    If @error Then Return SetError(2, 0, False)

    $oHttp.Open("POST", $sUrl, False)
    If @error Then Return SetError(3, 0, False)

    $oHttp.SetRequestHeader("Content-Type", "application/json; charset=utf-8")
    If @error Then Return SetError(3, 0, False)
    $oHttp.SetRequestHeader("Accept", "application/json; charset=utf-8")
    If @error Then Return SetError(3, 0, False)

    $oHttp.Send($sBody)
    If @error Then Return SetError(4, @error, False)

    $sResponse = $oHttp.ResponseText
    If @error Then Return SetError(5, @error, False)
    Return True
EndFunc

; GET 文本（JSON 接口）
Func _HttpGet($sUrl, ByRef $sResponse, $iTimeoutMs = 8000)
    $sResponse = ""
    Local $oHttp = ObjCreate("WinHttp.WinHttpRequest.5.1")
    If @error Or Not IsObj($oHttp) Then Return SetError(1, 0, False)
    $oHttp.SetTimeouts(5000, 5000, $iTimeoutMs, $iTimeoutMs)
    If @error Then Return SetError(2, 0, False)
    $oHttp.Open("GET", $sUrl, False)
    If @error Then Return SetError(3, 0, False)
    $oHttp.SetRequestHeader("Accept", "application/json; charset=utf-8")
    If @error Then Return SetError(3, 0, False)
    $oHttp.Send()
    If @error Then Return SetError(4, @error, False)
    $sResponse = $oHttp.ResponseText
    If @error Then Return SetError(5, @error, False)
    Return True
EndFunc

; GET 二进制文件（下载 exe/APK），保存到 $sSavePath
; $iProgress 是 ByRef，下载完成后会被置为 100
Func _HttpGetBinary($sUrl, $sSavePath, ByRef $iProgress, $iTimeoutMs = 60000)
    $iProgress = 0
    Local $oHttp = ObjCreate("WinHttp.WinHttpRequest.5.1")
    If @error Or Not IsObj($oHttp) Then Return SetError(1, 0, False)
    $oHttp.SetTimeouts(5000, 5000, $iTimeoutMs, $iTimeoutMs)
    If @error Then Return SetError(2, 0, False)
    $oHttp.Open("GET", $sUrl, False)
    If @error Then Return SetError(3, 0, False)
    $oHttp.Send()
    If @error Then Return SetError(4, @error, False)
    ; 写文件（二进制）
    Local $bData = $oHttp.ResponseBody
    If @error Then Return SetError(5, @error, False)
    Local $oStream = ObjCreate("ADODB.Stream")
    If @error Or Not IsObj($oStream) Then
        ; 降级：用 AutoIt FileWrite + DllCall 写二进制
        Local $hFile = FileOpen($sSavePath, 18)  ; 18 = FO_BINARY + FO_CREATE
        If $hFile = -1 Then Return SetError(6, 0, False)
        FileWrite($hFile, BinaryToString($bData, 4))
        FileClose($hFile)
    Else
        $oStream.Type = 1  ; adTypeBinary
        If @error Then Return SetError(7, @error, False)
        $oStream.Open()
        If @error Then Return SetError(7, @error, False)
        $oStream.Write($bData)
        If @error Then Return SetError(7, @error, False)
        $oStream.SaveToFile($sSavePath, 2)  ; adSaveCreateOverWrite
        If @error Then Return SetError(7, @error, False)
        $oStream.Close()
    EndIf
    $iProgress = 100
    Return True
EndFunc

Func _Url($sApi)
    Local $s = $gServer
    While StringRight($s, 1) = "/"
        $s = StringTrimRight($s, 1)
    WEnd
    Return $s & $sApi
EndFunc
