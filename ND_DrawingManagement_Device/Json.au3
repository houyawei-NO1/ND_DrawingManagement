; 下线机设备端 - 轻量 JSON 读取/转义
; 只覆盖本项目接口返回结构：对象数组 + 字符串/数字/布尔字段。

Func _JsonEscape($s)
    If $s == "" Then Return ""
    $s = StringReplace($s, "\", "\\")
    $s = StringReplace($s, '"', '\"')
    $s = StringReplace($s, @CRLF, "\n")
    $s = StringReplace($s, @LF, "\n")
    $s = StringReplace($s, @CR, "\n")
    $s = StringReplace($s, @TAB, "\t")
    Return $s
EndFunc

Func _JsonUnescape($s)
    $s = StringReplace($s, '\"', '"')
    $s = StringReplace($s, '\\', '\')
    $s = StringReplace($s, '\n', @LF)
    $s = StringReplace($s, '\r', @CR)
    $s = StringReplace($s, '\t', @TAB)
    $s = StringReplace($s, '\/', '/')
    Return $s
EndFunc

Func _JsonSkipSpace($sJson, $iPos)
    Local $n = StringLen($sJson)
    While $iPos <= $n And StringMid($sJson, $iPos, 1) = " "
        $iPos += 1
    WEnd
    Return $iPos
EndFunc

Func _JsonValuePos($sJson, $sKey)
    Local $iKey = StringInStr($sJson, '"' & $sKey & '"')
    If $iKey = 0 Then Return 0
    Local $iColon = StringInStr($sJson, ":", 0, 1, $iKey)
    If $iColon = 0 Then Return 0
    Return _JsonSkipSpace($sJson, $iColon + 1)
EndFunc

Func _JsonGetString($sJson, $sKey, $sDefault = "")
    Local $i = _JsonValuePos($sJson, $sKey)
    If $i = 0 Or $i > StringLen($sJson) Then Return $sDefault
    If StringMid($sJson, $i, 1) <> '"' Then Return $sDefault
    $i += 1
    Local $sOut = ""
    Local $bEsc = False
    Local $ch, $n = StringLen($sJson)
    While $i <= $n
        $ch = StringMid($sJson, $i, 1)
        If $bEsc Then
            Select
                Case $ch = "n"
                    $sOut &= @LF
                Case $ch = "r"
                    $sOut &= @CR
                Case $ch = "t"
                    $sOut &= @TAB
                Case Else
                    $sOut &= $ch
            EndSelect
            $bEsc = False
        ElseIf $ch = "\" Then
            $bEsc = True
        ElseIf $ch = '"' Then
            ExitLoop
        Else
            $sOut &= $ch
        EndIf
        $i += 1
    WEnd
    Return $sOut
EndFunc

Func _JsonGetInt($sJson, $sKey, $iDefault = 0)
    Local $i = _JsonValuePos($sJson, $sKey)
    If $i = 0 Or $i > StringLen($sJson) Then Return $iDefault
    Local $a = StringRegExp(StringMid($sJson, $i), "^\s*(-?\d+)", 1)
    If @error Or UBound($a) = 0 Then Return $iDefault
    Return Int($a[0])
EndFunc

Func _JsonGetBool($sJson, $sKey, $bDefault = False)
    Local $i = _JsonValuePos($sJson, $sKey)
    If $i = 0 Or $i > StringLen($sJson) Then Return $bDefault
    Local $sPart = StringMid($sJson, $i, 5)
    If StringLeft($sPart, 4) = "true" Or StringLeft($sPart, 1) = "1" Then Return True
    If StringLeft($sPart, 5) = "false" Or StringLeft($sPart, 1) = "0" Then Return False
    Return $bDefault
EndFunc

; 提取某个数组字段中的所有 JSON 对象字符串，例如 "items":[{...},{...}]
Func _JsonExtractObjects($sJson, $sKey)
    Local $aOut[1]
    Local $iKey = StringInStr($sJson, '"' & $sKey & '"')
    If $iKey = 0 Then Return $aOut
    Local $iOpen = StringInStr($sJson, "[", 0, 1, $iKey)
    If $iOpen = 0 Then Return $aOut

    Local $n = StringLen($sJson)
    Local $depth = 0, $inStr = False, $esc = False
    Local $start = 0, $ch, $count = 0
    For $i = $iOpen + 1 To $n
        $ch = StringMid($sJson, $i, 1)
        If $inStr Then
            If $esc Then
                $esc = False
            ElseIf $ch = "\" Then
                $esc = True
            ElseIf $ch = '"' Then
                $inStr = False
            EndIf
            ContinueLoop
        EndIf
        If $ch = '"' Then
            $inStr = True
        ElseIf $ch = "{" Then
            If $depth = 0 Then $start = $i
            $depth += 1
        ElseIf $ch = "}" Then
            If $depth > 0 Then
                $depth -= 1
                If $depth = 0 Then
                    ReDim $aOut[$count + 1]
                    $aOut[$count] = StringMid($sJson, $start, $i - $start + 1)
                    $count += 1
                EndIf
            EndIf
        ElseIf $ch = "]" And $depth = 0 Then
            ExitLoop
        EndIf
    Next
    Return $aOut
EndFunc
