# Genera, comprueba y fotografía los kits propios (Hub, ReturnWall, Ice, Crops).
#   powershell -File Tools/Art/Blender/build_kits.ps1 [-Kits hub,ice] [-SkipSheets]
# Sale con código distinto de 0 si fallan los tests, la exportación o el comprobador.
param(
    [string]$Kits = "all",
    [switch]$SkipSheets
)
$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Blender = if ($env:BLENDER_EXE) { $env:BLENDER_EXE } else { "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" }
if (-not (Test-Path $Blender)) { throw "No encuentro Blender en $Blender (variable BLENDER_EXE)" }
$env:PYTHONIOENCODING = "utf-8"

function Invoke-Step([string]$Name, [scriptblock]$Body) {
    Write-Host "== $Name"
    & $Body
    if ($LASTEXITCODE -ne 0) { throw "$Name falló (código $LASTEXITCODE)" }
}

Invoke-Step "tests" { uv run --no-project --with pytest pytest -q -p no:cacheprovider "$Here/tests" }
Invoke-Step "exportación" { & $Blender -b --factory-startup --python-exit-code 1 --python "$Here/export_kits.py" -- --kits $Kits }
Invoke-Step "comprobador" { uv run --no-project python "$Here/check_kits.py" --kits $Kits }
if (-not $SkipSheets) {
    Invoke-Step "hojas de contacto" { & $Blender -b --factory-startup --python-exit-code 1 --python "$Here/contact_sheet.py" -- --kits $Kits }
}
