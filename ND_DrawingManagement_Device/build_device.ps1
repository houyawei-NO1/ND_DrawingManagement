param(
    [string]$AutoItDir = 'D:\Program Files (x86)\AutoIt3',
    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

$deviceDir   = $PSScriptRoot
$projectRoot = Split-Path -Parent $deviceDir
$source      = Join-Path $deviceDir 'CutDevice.au3'
$icon        = Join-Path $projectRoot 'ND_DrawingManagement_PC\logo.ico'
$au3check    = Join-Path $AutoItDir 'Au3Check.exe'
$aut2exeX86  = Join-Path $AutoItDir 'Aut2Exe\Aut2exe.exe'
$aut2exeX64  = Join-Path $AutoItDir 'Aut2Exe\Aut2exe_x64.exe'

foreach ($tool in @($source, $icon, $au3check, $aut2exeX86, $aut2exeX64)) {
    if (-not (Test-Path -LiteralPath $tool)) {
        throw "Missing file or tool: $tool"
    }
}

$verMatch = Select-String -Path $source -Pattern '^\s*Global\s+\$gAppVerBase\s*=\s*"([^"]+)"'
if (-not $verMatch) {
    throw 'Cannot read $gAppVerBase from CutDevice.au3'
}
$baseVer = $verMatch.Matches[0].Groups[1].Value
if ($baseVer -notmatch '^\d{8}-b\d+$') {
    throw "Version must be yyyyMMdd-bXX, got: $baseVer"
}

if (-not $SkipCheck) {
    & $au3check -q $source
    if ($LASTEXITCODE -ne 0) {
        throw "Au3Check failed with exit code $LASTEXITCODE"
    }
}

Write-Host "Base version: $baseVer"
Write-Host "Icon: $icon"

# 中文包名与 PC 端上传校验共用同一条规则：<yyyyMMdd-bXX>-x86/x64.exe 结尾
foreach ($arch in @('x86', 'x64')) {
    $out = Join-Path $deviceDir ("下线机设备{0}-{1}.exe" -f $baseVer, $arch)
    if (Test-Path -LiteralPath $out) {
        Remove-Item -LiteralPath $out -Force
    }

    $compiler = if ($arch -eq 'x64') { $aut2exeX64 } else { $aut2exeX86 }
    $argLine = '/in "{0}" /out "{1}" /icon "{2}" /nopack' -f $source, $out, $icon
    $proc = Start-Process -FilePath $compiler -ArgumentList $argLine -Wait -PassThru
    Start-Sleep -Milliseconds 500
    if ($proc.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $out)) {
        throw "Compile $arch failed (exit $($proc.ExitCode), output missing)"
    }

    $fi = Get-Item -LiteralPath $out
    Write-Host ("Built {0}  {1:N0} bytes" -f $fi.Name, $fi.Length)
}

Write-Host 'Done. x86 for WinXP machines, x64 for Win10 machine.'
