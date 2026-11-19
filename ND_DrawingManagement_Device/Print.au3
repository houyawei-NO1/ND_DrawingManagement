; 下线机设备端 - DL-720C 标签打印
; 默认按 40x100mm、TSPL 指令生成标签；字体/间距可在配置里调整。
; 修复记录：
;   v2 - DOCINFOW 补全 dwMode/dwJobId，StartDocPrinterW cbDocInfo 用真实结构大小
;      - OpenPrinter 失败自动回退默认打印机
;      - _PrintDebug 输出详细日志便于排查

Func _TsplEscape($s)
    $s = StringReplace($s, "\", "\\")
    $s = StringReplace($s, '"', '\"')
    Return $s
EndFunc

Func _TsplText($iX, $iY, $sText, $sFont = "")
    If $sFont = "" Then $sFont = $gLabelFont
    Return 'TEXT ' & $iX & ',' & $iY & ',"' & $sFont & '",0,1,1,"' & _TsplEscape($sText) & '"' & @LF
EndFunc

; 从字体名推断点阵尺寸（TSS24.BF2 → 24）
Func _FontSizeOf($sFont)
    If StringInStr($sFont, "12") > 0 Then Return 12
    If StringInStr($sFont, "16") > 0 Then Return 16
    If StringInStr($sFont, "32") > 0 Then Return 32
    Return 24
EndFunc

; 估算文字像素宽度：汉字 = 全高，ASCII = 半高
Func _TextWidthPx($s, $iSize)
    Local $w = 0, $iCh
    For $i = 1 To StringLen($s)
        $iCh = AscW(StringMid($s, $i, 1))
        If $iCh < 0 Then $iCh += 65536
        If $iCh > 127 Then
            $w += $iSize
        Else
            $w += Round($iSize / 2)
        EndIf
    Next
    Return $w
EndFunc

; 按宽度截断（放不下时丢弃尾部字符）
Func _TruncToWidth($s, $iSize, $iMax)
    If _TextWidthPx($s, $iSize) <= $iMax Then Return $s
    Local $r = ""
    For $i = 1 To StringLen($s)
        If _TextWidthPx($r & StringMid($s, $i, 1), $iSize) > $iMax Then ExitLoop
        $r &= StringMid($s, $i, 1)
    Next
    Return $r
EndFunc

; 单元格文字：自动选字体（24→16→12）并居中
Func _TsplCell($x1, $x2, $yTop, $iRowH, $sText)
    Local $avail = $x2 - $x1 - 8
    Local $aFonts[3] = [$gLabelFont, "TSS16.BF2", "TSS12.BF2"]
    Local $sFont = $gLabelFont, $iSize = _FontSizeOf($gLabelFont)
    For $i = 0 To 2
        $sFont = $aFonts[$i]
        $iSize = _FontSizeOf($sFont)
        If _TextWidthPx($sText, $iSize) <= $avail Then ExitLoop
    Next
    Local $txt = _TruncToWidth($sText, $iSize, $avail)
    Local $x = $x1 + Int(($x2 - $x1 - _TextWidthPx($txt, $iSize)) / 2)
    If $x < $x1 + 2 Then $x = $x1 + 2
    Local $y = $yTop + Int(($iRowH - $iSize) / 2)
    Return _TsplText($x, $y, $txt, $sFont)
EndFunc

; 单元格两行文字（表格里的"左压前作业"等长标签）
Func _TsplCellTwoLine($x1, $x2, $yTop, $s1, $s2)
    Local $s = ""
    Local $x1p = $x1 + Int(($x2 - $x1 - _TextWidthPx($s1, 24)) / 2)
    Local $x2p = $x1 + Int(($x2 - $x1 - _TextWidthPx($s2, 24)) / 2)
    If $x1p < $x1 + 2 Then $x1p = $x1 + 2
    If $x2p < $x1 + 2 Then $x2p = $x1 + 2
    $s &= _TsplText($x1p, $yTop + 2, $s1, $gLabelFont)
    $s &= _TsplText($x2p, $yTop + 26, $s2, $gLabelFont)
    Return $s
EndFunc

; 标签版式：带边框的表格（40mm 宽 = 320 dots @203dpi）
; ┌─────────────────────────┐
; │ 型号： ND-HY-FT-F008…    │
; │ 序号 │ 68 │ 机台号 │ 0   │
; │ 材质 │ …  │ 数量   │ …   │
; │ 截面积│ …  │ 颜色   │ …   │
; │ 线号 │ …  │ 长度   │ …   │
; │ 左剥皮│ …  │ 右剥皮 │ …   │
; │ 左端子 │ …（跨列）        │  行6
; │ 右端子 │ …（跨列）        │  行7
; ├─────────────────────────┤
; │        （空行）           │  行8：368..432，中心 400 = 纸张正中折叠线
; ├─────────────────────────┤
; │ 左压前 │ …（跨列）        │  行9
; │ 右压前 │ …（跨列）        │  行10
; │ 批次号：│ …（跨列）        │  行11
; ├─────────────────────────┤
; │                          │
; │      （底部大空框）        │  588..792
; └─────────────────────────┘
Func _BuildLabel($aTask, $iCurrent, $iTotal, $iCopies = -1)
    If $iCopies < 1 Then $iCopies = $gCopies
    Local $s = ""
    $s &= 'SIZE ' & $gPaperW & ' mm,' & $gPaperH & ' mm' & @LF
    $s &= 'GAP ' & $gGap & ' mm,0 mm' & @LF
    $s &= 'DIRECTION 1' & @LF
    $s &= 'CLS' & @LF

    ; 列边界：L(左) A(标签列1末) B(值列1末) C(标签列2末) R(右)
    Local $L = 8, $A = 88, $B = 176, $C = 256, $R = 312

    ; ---- 行位置表 ----
    ; 关键：纸张 100mm × 8 dots/mm = 800 dots，折叠线在正中 y=400
    ; 空行（行8）中心必须正好落在 400，折叠后空白部分才能绕线贴合
    Local $aTop[12], $aH[12]
    Local $yy = 8
    Local $i
    For $i = 0 To 7                      ; 行0..7：上半区，行高 45 → 8..368
        $aTop[$i] = $yy
        $aH[$i] = 45
        $yy += 45
    Next
    $aTop[8] = $yy                       ; 368
    $aH[8] = 64                          ; 368..432 → 中心 400（纸张正中）
    $yy += 64
    For $i = 9 To 11                     ; 行9..11：下半区，行高 52 → 432..588
        $aTop[$i] = $yy
        $aH[$i] = 52
        $yy += 52
    Next
    Local $BOT = $yy                     ; 588（表格底边）
    Local $BIGBOT = 792                  ; 底部大空框下边（100mm = 800 dots）

    ; 外框（一直画到底部大空框）
    $s &= 'BOX ' & $L & ',' & $aTop[0] & ',' & $R & ',' & $BIGBOT & ',2' & @LF

    ; 横线：每行上边 + 表格底边
    For $i = 1 To 11
        $s &= 'BAR ' & $L & ',' & $aTop[$i] & ',' & ($R - $L) & ',2' & @LF
    Next
    $s &= 'BAR ' & $L & ',' & $BOT & ',' & ($R - $L) & ',2' & @LF

    ; 竖线：第 1 列分隔（空行处不画）；第 2/3 列分隔只在"双列行"（1~5 行）有
    For $i = 0 To 11
        If $i = 8 Then ContinueLoop
        $s &= 'BAR ' & $A & ',' & $aTop[$i] & ',2,' & $aH[$i] & @LF
        If $i >= 1 And $i <= 5 Then
            $s &= 'BAR ' & $B & ',' & $aTop[$i] & ',2,' & $aH[$i] & @LF
            $s &= 'BAR ' & $C & ',' & $aTop[$i] & ',2,' & $aH[$i] & @LF
        EndIf
    Next

    ; ---- 行0：型号 ----
    $s &= _TsplCell($L, $A, $aTop[0], $aH[0], '型号：')
    $s &= _TsplCell($A, $R, $aTop[0], $aH[0], $aTask[$T_BATCH])

    ; ---- 行1：序号 / 机台号 ----
    $s &= _TsplCell($L, $A, $aTop[1], $aH[1], '序号')
    $s &= _TsplCell($A, $B, $aTop[1], $aH[1], $aTask[$T_SEQ])
    $s &= _TsplCell($B, $C, $aTop[1], $aH[1], '机台号')
    $s &= _TsplCell($C, $R, $aTop[1], $aH[1], String($gMachineNo))

    ; ---- 行2：材质 / 数量 ----
    $s &= _TsplCell($L, $A, $aTop[2], $aH[2], '材质')
    $s &= _TsplCell($A, $B, $aTop[2], $aH[2], $aTask[$T_MATERIAL])
    $s &= _TsplCell($B, $C, $aTop[2], $aH[2], '数量')
    $s &= _TsplCell($C, $R, $aTop[2], $aH[2], $iCurrent & '/' & $iTotal)

    ; ---- 行3：截面积 / 颜色 ----
    $s &= _TsplCell($L, $A, $aTop[3], $aH[3], '截面积')
    $s &= _TsplCell($A, $B, $aTop[3], $aH[3], $aTask[$T_SECTION])
    $s &= _TsplCell($B, $C, $aTop[3], $aH[3], '颜色')
    $s &= _TsplCell($C, $R, $aTop[3], $aH[3], $aTask[$T_COLOR])

    ; ---- 行4：线号 / 长度 ----
    $s &= _TsplCell($L, $A, $aTop[4], $aH[4], '线号')
    $s &= _TsplCell($A, $B, $aTop[4], $aH[4], $aTask[$T_CIRCUIT])
    $s &= _TsplCell($B, $C, $aTop[4], $aH[4], '长度')
    $s &= _TsplCell($C, $R, $aTop[4], $aH[4], $aTask[$T_LENGTH])

    ; ---- 行5：左剥皮 / 右剥皮 ----
    $s &= _TsplCell($L, $A, $aTop[5], $aH[5], '左剥皮')
    $s &= _TsplCell($A, $B, $aTop[5], $aH[5], $aTask[$T_STRIP_L])
    $s &= _TsplCell($B, $C, $aTop[5], $aH[5], '右剥皮')
    $s &= _TsplCell($C, $R, $aTop[5], $aH[5], $aTask[$T_STRIP_R])

    ; ---- 行6：左端子（跨列）----
    $s &= _TsplCell($L, $A, $aTop[6], $aH[6], '左端子')
    $s &= _TsplCell($A, $R, $aTop[6], $aH[6], $aTask[$T_TERM_L])

    ; ---- 行7：右端子（跨列）----
    $s &= _TsplCell($L, $A, $aTop[7], $aH[7], '右端子')
    $s &= _TsplCell($A, $R, $aTop[7], $aH[7], $aTask[$T_TERM_R])

    ; ---- 行8：空行（纸张正中折叠区，无竖线、无文字）----

    ; ---- 行9：左压前作业（标签两行，值跨列）----
    $s &= _TsplCellTwoLine($L, $A, $aTop[9], '左压前', '作业')
    $s &= _TsplCell($A, $R, $aTop[9], $aH[9], $aTask[$T_WORK_L])

    ; ---- 行10：右压前作业 ----
    $s &= _TsplCellTwoLine($L, $A, $aTop[10], '右压前', '作业')
    $s &= _TsplCell($A, $R, $aTop[10], $aH[10], $aTask[$T_WORK_R])

    ; ---- 行11：批次号（跨列）----
    $s &= _TsplCell($L, $A, $aTop[11], $aH[11], '批次号：')
    $s &= _TsplCell($A, $R, $aTop[11], $aH[11], $aTask[$T_BATCHNO])

    $s &= 'PRINT ' & $iCopies & ',1' & @LF
    Return $s
EndFunc

; 列出系统所有打印机 —— 用 WMI Win32_Printer，最简单可靠
; 返回一维数组，$ret[0]=数量，$ret[1..N]=打印机名称
Func _ListPrinters()
    Local $aResult[1] = [0]
    Local $objSWbemServices = ObjGet("winmgmts:\root\cimv2")
    If Not IsObj($objSWbemServices) Then Return $aResult
    Local $colPrinters = $objSWbemServices.ExecQuery("SELECT Name FROM Win32_Printer")
    If Not IsObj($colPrinters) Then Return $aResult
    Local $n = $colPrinters.Count
    If $n = 0 Then Return $aResult
    ReDim $aResult[$n + 1]
    $aResult[0] = $n
    Local $i = 1
    For $obj In $colPrinters
        $aResult[$i] = $obj.Name
        $i += 1
    Next
    Return $aResult
EndFunc

; 辅助：用 DllStruct 正确调用 OpenPrinterW（AutoIt 的 ptr* ByRef 不工作！）
; 把 DllStruct 的内存地址传给 API，再从 DllStruct 读回句柄
Func _OpenPrinterByName($sName)
    Local $tH = DllStructCreate("ptr")
    Local $a = DllCall("winspool.drv", "int", "OpenPrinterW", _
        "wstr", $sName, _
        "ptr",  DllStructGetPtr($tH), _
        "ptr",  0)
    If @error Or $a[0] = 0 Then Return 0
    Return DllStructGetData($tH, 1)
EndFunc

Func _OpenPrinterDefault()
    Local $tH = DllStructCreate("ptr")
    Local $a = DllCall("winspool.drv", "int", "OpenPrinterW", _
        "ptr",  0, _
        "ptr",  DllStructGetPtr($tH), _
        "ptr",  0)
    If @error Or $a[0] = 0 Then Return 0
    Return DllStructGetData($tH, 1)
EndFunc

; 核心：用 Win32 RAW 打印 API 直接把 TSPL 指令送给打印机
; $sPrinter = "" / "*" / "<默认打印机>*" → 都用系统默认打印机
Func _RawPrint($sPrinter, $sData)
    Local $sName = $sPrinter
    If $sName = "<默认打印机>*" Then $sName = ""
    Local $hPrinter = 0
    If $sName <> "" And $sName <> "*" Then
        $hPrinter = _OpenPrinterByName($sName)
    EndIf
    If $hPrinter = 0 Then
        $hPrinter = _OpenPrinterDefault()
        If $hPrinter = 0 Then
            Return _PrintViaFallback($sData, $sName)
        EndIf
        $sName = "<默认打印机>"
    EndIf
    _LogLine("OpenPrinterW 成功: hPrinter=" & $hPrinter & "  目标=" & $sName)

    ; ====== StartDocPrinterW ======
    ; 关键：第 2 个参数是 Level（1 或 2），不是结构字节数！
    ; Level=1 → DOC_INFO_1 { pDocName, pOutputFile, pDatatype } 共 3 个指针
    Local $tDocName = DllStructCreate("wchar[128]")
    DllStructSetData($tDocName, 1, "CutDevice Label")
    Local $tDataType = DllStructCreate("wchar[32]")
    DllStructSetData($tDataType, 1, "RAW")

    ; --- 尝试 A: Level=1 + datatype "RAW" ---
    Local $tDI1 = DllStructCreate("ptr;ptr;ptr")
    DllStructSetData($tDI1, 1, DllStructGetPtr($tDocName))
    DllStructSetData($tDI1, 2, 0)
    DllStructSetData($tDI1, 3, DllStructGetPtr($tDataType))
    DllCall("kernel32.dll", "int", "SetLastError", "dword", 0)
    Local $aStart = DllCall("winspool.drv", "dword", "StartDocPrinterW", _
        "ptr", $hPrinter, "dword", 1, "ptr", DllStructGetPtr($tDI1))
    Local $errStart = DllCall("kernel32.dll", "dword", "GetLastError")
    _LogLine("StartDocPrinterW[Level1,RAW] 返回=" & $aStart[0] & " err=" & $errStart[0])

    ; --- 尝试 B: Level=1 + datatype NULL（让驱动自选）---
    If @error Or $aStart[0] = 0 Then
        DllStructSetData($tDI1, 3, 0)
        DllCall("kernel32.dll", "int", "SetLastError", "dword", 0)
        $aStart = DllCall("winspool.drv", "dword", "StartDocPrinterW", _
            "ptr", $hPrinter, "dword", 1, "ptr", DllStructGetPtr($tDI1))
        $errStart = DllCall("kernel32.dll", "dword", "GetLastError")
        _LogLine("StartDocPrinterW[Level1,NULL] 返回=" & $aStart[0] & " err=" & $errStart[0])
    EndIf

    ; --- 尝试 C: Level=2 → DOC_INFO_2 { ..., dwMode, dwJobId } ---
    If @error Or $aStart[0] = 0 Then
        Local $tDI2 = DllStructCreate("ptr;ptr;ptr;dword;dword")
        DllStructSetData($tDI2, 1, DllStructGetPtr($tDocName))
        DllStructSetData($tDI2, 2, 0)
        DllStructSetData($tDI2, 3, DllStructGetPtr($tDataType))
        DllStructSetData($tDI2, 4, 1)
        DllStructSetData($tDI2, 5, 0)
        DllCall("kernel32.dll", "int", "SetLastError", "dword", 0)
        $aStart = DllCall("winspool.drv", "dword", "StartDocPrinterW", _
            "ptr", $hPrinter, "dword", 2, "ptr", DllStructGetPtr($tDI2))
        $errStart = DllCall("kernel32.dll", "dword", "GetLastError")
        _LogLine("StartDocPrinterW[Level2,RAW] 返回=" & $aStart[0] & " err=" & $errStart[0])
    EndIf

    If @error Or $aStart[0] = 0 Then
        _LogLine("StartDocPrinterW 全部失败 → .NET Fallback")
        DllCall("winspool.drv", "int", "ClosePrinter", "ptr", $hPrinter)
        Return _PrintViaFallback($sData, $sName)
    EndIf
    Local $iDocId = $aStart[0]

    ; ====== WritePrinter ======
    DllCall("winspool.drv", "int", "StartPagePrinter", "ptr", $hPrinter)
    Local $bin = StringToBinary($sData, 0)
    Local $len = BinaryLen($bin)
    Local $buf = DllStructCreate("byte[" & $len & "]")
    DllStructSetData($buf, 1, $bin)
    Local $tWritten = DllStructCreate("dword")
    Local $aW = DllCall("winspool.drv", "int", "WritePrinter", _
        "ptr", $hPrinter, "ptr", DllStructGetPtr($buf), "dword", $len, "ptr", DllStructGetPtr($tWritten))
    Local $written = DllStructGetData($tWritten, 1)
    _LogLine("WritePrinter: 请求" & $len & " 字节，实际写入" & $written)

    DllCall("winspool.drv", "int", "EndPagePrinter", "ptr", $hPrinter)
    DllCall("winspool.drv", "int", "EndDocPrinter", "ptr", $hPrinter)
    DllCall("winspool.drv", "int", "ClosePrinter", "ptr", $hPrinter)

    If $written = 0 Then
        _LogLine("WritePrinter 写入 0 字节 → Fallback")
        Return _PrintViaFallback($sData, $sName)
    EndIf
    _LogLine("打印成功！目标=" & $sName & "  写入=" & $written & " 字节")
    Return True
EndFunc

; ========== 终极 fallback：用 .NET (C# P/Invoke) 直接发 RAW 数据 ==========
; 生成 .ps1 脚本文件再执行，彻底规避 AutoIt 里的引号嵌套问题
; 支持未共享的本地 USB 打印机（不需要 \\localhost\ 共享）
Func _PrintViaFallback($sData, $sPrinterName)
    Local $tmp = @TempDir & "\cut_label_" & @AutoItPID & ".prn"
    Local $h = FileOpen($tmp, 18)   ; FO_BINARY + FO_CREATE
    If $h = -1 Then Return SetError(4, 0, False)
    FileWrite($h, $sData)
    FileClose($h)
    _LogLine("Fallback(.NET): 写入 " & $tmp & " 大小=" & FileGetSize($tmp))

    ; 确定目标打印机名
    Local $sPrn = $sPrinterName
    If $sPrn = "" Or $sPrn = "*" Or $sPrn = "<默认打印机>*" Or $sPrn = "<默认打印机>" Then
        $sPrn = _GetDefaultPrinterName()
    EndIf
    If $sPrn = "" Then
        _LogLine("Fallback(.NET): 无法确定打印机名，放弃")
        Return False
    EndIf
    _LogLine("Fallback(.NET): 目标打印机=" & $sPrn)

    ; 生成 ps1（用单引号字符串，内部双引号无需转义）
    Local $s = ""
    $s &= 'param([string]$PrinterName, [string]$FilePath)' & @CRLF
    $s &= 'Add-Type -TypeDefinition @"' & @CRLF
    $s &= 'using System;' & @CRLF
    $s &= 'using System.IO;' & @CRLF
    $s &= 'using System.Runtime.InteropServices;' & @CRLF
    $s &= 'public class RP {' & @CRLF
    $s &= ' [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]' & @CRLF
    $s &= ' public struct DI { public string n; public string o; public string d; }' & @CRLF
    $s &= ' [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]' & @CRLF
    $s &= ' public static extern bool OpenPrinter(string s, out IntPtr h, IntPtr p);' & @CRLF
    $s &= ' [DllImport("winspool.drv", SetLastError=true)]' & @CRLF
    $s &= ' public static extern bool ClosePrinter(IntPtr h);' & @CRLF
    $s &= ' [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]' & @CRLF
    $s &= ' public static extern bool StartDocPrinter(IntPtr h, int l, ref DI di);' & @CRLF
    $s &= ' [DllImport("winspool.drv", SetLastError=true)]' & @CRLF
    $s &= ' public static extern bool EndDocPrinter(IntPtr h);' & @CRLF
    $s &= ' [DllImport("winspool.drv", SetLastError=true)]' & @CRLF
    $s &= ' public static extern bool StartPagePrinter(IntPtr h);' & @CRLF
    $s &= ' [DllImport("winspool.drv", SetLastError=true)]' & @CRLF
    $s &= ' public static extern bool EndPagePrinter(IntPtr h);' & @CRLF
    $s &= ' [DllImport("winspool.drv", SetLastError=true)]' & @CRLF
    $s &= ' public static extern bool WritePrinter(IntPtr h, IntPtr b, int c, out int w);' & @CRLF
    $s &= ' public static string Send(string printer, string file) {' & @CRLF
    $s &= '  IntPtr h;' & @CRLF
    $s &= '  if (!OpenPrinter(printer, out h, IntPtr.Zero)) return "OPEN_FAIL " + Marshal.GetLastWin32Error();' & @CRLF
    $s &= '  DI di = new DI(); di.n = "CutDevice Label"; di.d = "RAW";' & @CRLF
    $s &= '  if (!StartDocPrinter(h, 1, ref di)) { int e = Marshal.GetLastWin32Error(); ClosePrinter(h); return "STARTDOC_FAIL " + e; }' & @CRLF
    $s &= '  StartPagePrinter(h);' & @CRLF
    $s &= '  byte[] b = File.ReadAllBytes(file);' & @CRLF
    $s &= '  IntPtr p = Marshal.AllocCoTaskMem(b.Length);' & @CRLF
    $s &= '  Marshal.Copy(b, 0, p, b.Length);' & @CRLF
    $s &= '  int w;' & @CRLF
    $s &= '  bool ok = WritePrinter(h, p, b.Length, out w);' & @CRLF
    $s &= '  Marshal.FreeCoTaskMem(p);' & @CRLF
    $s &= '  EndPagePrinter(h); EndDocPrinter(h); ClosePrinter(h);' & @CRLF
    $s &= '  return ok ? ("OK " + w) : ("WRITE_FAIL " + Marshal.GetLastWin32Error());' & @CRLF
    $s &= ' }' & @CRLF
    $s &= '}' & @CRLF
    $s &= '"@' & @CRLF
    $s &= 'Write-Output ([RP]::Send($PrinterName, $FilePath))' & @CRLF

    Local $ps1 = @TempDir & "\cut_rawprint.ps1"
    Local $out = @TempDir & "\cut_rawprint_out.txt"
    Local $hPs = FileOpen($ps1, 2 + 128)   ; 写 + UTF8
    If $hPs = -1 Then
        _LogLine("Fallback(.NET): 无法创建 ps1 脚本")
        Return False
    EndIf
    FileWrite($hPs, $s)
    FileClose($hPs)

    FileDelete($out)
    RunWait(@ComSpec & ' /c powershell -NoProfile -ExecutionPolicy Bypass -File "' & $ps1 & '" -PrinterName "' & $sPrn & '" -FilePath "' & $tmp & '" > "' & $out & '" 2>&1', @TempDir, @SW_HIDE)

    Local $sRes = ""
    If FileExists($out) Then
        Local $hf = FileOpen($out, 0)
        If $hf <> -1 Then
            $sRes = FileRead($hf)
            FileClose($hf)
        EndIf
    EndIf
    _LogLine("Fallback(.NET) 输出: " & StringStripWS($sRes, 3))

    Local $bOk = StringInStr($sRes, "OK ") > 0
    If $bOk Then
        _LogLine("Fallback(.NET) 打印成功")
        FileDelete($tmp)
    Else
        _LogLine("Fallback(.NET) 打印失败，保留 prn 供手动处理: " & $tmp)
    EndIf
    Return $bOk
EndFunc

; 终极回退：生成 .prn 文件后用 ShellExecute "print" 动词
; 前提：Windows 已安装打印机驱动且关联正确
Func _FallbackPrint($sData)
    Local $tmp = @TempDir & "\cut_label_" & @AutoItPID & ".prn"
    Local $h = FileOpen($tmp, 18)  ; 18 = FO_BINARY + FO_CREATE
    If $h = -1 Then Return SetError(4, 0, False)
    FileWrite($h, $sData)
    FileClose($h)
    _LogLine("Fallback: 写入临时文件 " & $tmp & "（" & StringLen($sData) & " 字符）")
    Local $hInst = ShellExecute($tmp, "", "", "print", @SW_HIDE)
    If $hInst <= 32 Then
        _LogLine("ShellExecute print 失败，error=" & $hInst)
        Return SetError(4, 0, False)
    EndIf
    _LogLine("Fallback: ShellExecute print 已触发")
    ; 打印后删掉临时文件（等几秒让打印后台处理完）
    AdLibRegister("_CleanupPrn", 10000)
    Return True
EndFunc

Func _CleanupPrn()
    Local $tmp = @TempDir & "\cut_label_" & @AutoItPID & ".prn"
    If FileExists($tmp) Then FileDelete($tmp)
    AdLibUnRegister("_CleanupPrn")
EndFunc

; 对外入口：打印一条任务标签
; 按数量打印：数量=2 → 依次打印 "1/2"、"2/2" 两张，每张 1 份
Func _PrintTaskLabel($aTask, $iCurrent, $iTotal, ByRef $sErr)
    $sErr = ""
    Local $iCount = $iTotal
    If $iCount < 1 Then $iCount = 1

    _LogLine("=== 开始打印 任务ID=" & $aTask[$T_ID] & " 序号=" & $aTask[$T_SEQ] & " 共" & $iCount & "张 ===")
    _LogLine("配置打印机: '" & $gPrinterName & "'  实际尝试: 配置名 → 默认打印机")

    Local $i
    For $i = 1 To $iCount
        Local $sData = _BuildLabel($aTask, $i, $iCount, 1)
        If Not _RawPrint($gPrinterName, $sData) Then
            $sErr = "标签打印失败（第 " & $i & "/" & $iCount & " 张）。请检查：1) 打印机是否联机 2) 设置里的打印机名称是否与系统一致"
            _LogLine("打印彻底失败（" & $i & "/" & $iCount & "）")
            Return False
        EndIf
        _LogLine("第 " & $i & "/" & $iCount & " 张打印完成")

        Local $sLabelNo = $i & "/" & $iCount
        Local $sResp
        If Not _ApiLogPrint($aTask[$T_ID], $gMachineNo, $gOperator, $sLabelNo, 1, "success", $sResp) Then
            $sErr = "标签已打印，但打印记录未上传：" & $sResp
            _LogLine($sErr)
        EndIf
    Next

    _LogLine("=== 打印完成（共 " & $iCount & " 张）===")
    Return True
EndFunc
