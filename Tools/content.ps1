# Regenera el contenido creado por script (Tools/Content/build_content.py): materiales del portal,
# M_Basic, el mapa de calibración y L_Hub (hub, biomas y portales). Lanza UnrealEditor-Cmd -run=pythonscript con tiempo máximo y
# comprueba Saved/Content/build_content.json. Cerrar el editor antes (bloquea los paquetes).
# Uso: powershell -File Tools/content.ps1 [-Script Tools/Content/build_content.py] [-TimeoutMinutes 30]
param(
    [string]$Script = 'Tools/Content/build_content.py',
    [int]$TimeoutMinutes = 30
)
. "$PSScriptRoot\common.ps1"

$scriptPath = (Resolve-Path (Join-Path $ProjectRoot $Script)).Path
$resultPath = Join-Path $ProjectRoot 'Saved\Content\build_content.json'
$logPath = Join-Path $ProjectRoot 'Saved\Logs\Content.log'
New-Item -ItemType Directory -Force (Split-Path $logPath) | Out-Null
if (Test-Path $resultPath) { Remove-Item $resultPath -Force }

# Sin -nullrhi: con él el commandlet de Python puede colgarse (ver ~/.ultron/papercuts.md).
$cmdArgs = @("`"$UProject`"", '-run=pythonscript', "-script=`"$scriptPath`"", '-unattended', '-nosplash',
    '-nopause', '-NoSound', "-ABSLOG=`"$logPath`"")
$proc = Start-Process -FilePath $EditorCmd -ArgumentList $cmdArgs -PassThru -WindowStyle Hidden
if (-not $proc.WaitForExit($TimeoutMinutes * 60 * 1000)) {
    # Solo el proceso lanzado aquí (por PID), nunca por nombre de imagen.
    Stop-Process -Id $proc.Id -Force
    throw "build_content no terminó en $TimeoutMinutes minutos (log: $logPath)"
}

if (Test-Path $logPath) {
    Get-Content $logPath -Encoding UTF8 | Where-Object { $_ -match 'LiminalContent|LogPython: Error|Traceback' } | ForEach-Object { Write-Host $_ }
}
if (-not (Test-Path $resultPath)) { throw "No se generó $resultPath (código $($proc.ExitCode); log: $logPath)" }
$result = Get-Content $resultPath -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $result.ok) { throw "build_content falló: $($result.error)" }
Write-Output "Contenido listo: $($result.assets.Count) assets; L_Calibration con $($result.calibrationActors) actores; L_Hub con $($result.hubActors) actores y $($result.hubPortals) portales"
if ($result.hubPortals -ne 8) { throw "L_Hub debe tener 8 portales y tiene $($result.hubPortals)" }
