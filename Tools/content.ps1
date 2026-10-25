# Regenera el contenido creado por script. Lanza UnrealEditor-Cmd -run=pythonscript con tiempo máximo y
# comprueba el JSON de resultado de cada paso. Cerrar el editor antes (bloquea los paquetes).
#   -Mode Art      Tools/Content/import_art.py: FBX de Art/Export -> /Game/Art (M_LowPoly, bounding box
#                  comprobado contra los informes; idempotente: solo reimporta los FBX que cambian).
#   -Mode Content  Tools/Content/build_content.py: materiales del portal, M_Basic, L_Calibration y L_Hub.
#   -Mode All      (por defecto) los dos, en ese orden: L_Hub referencia las mallas de /Game/Art.
# Uso: powershell -File Tools/content.ps1 [-Mode All|Art|Content] [-TimeoutMinutes 30]
param(
    [ValidateSet('All', 'Art', 'Content')] [string]$Mode = 'All',
    [int]$TimeoutMinutes = 30
)
. "$PSScriptRoot\common.ps1"

# Ejecuta un script de Python del editor y devuelve el JSON de resultado (falla si no hay o si ok = false).
function Invoke-EditorPython([string]$Script, [string]$ResultName, [string]$LogName, [string]$Tag) {
    $scriptPath = (Resolve-Path (Join-Path $ProjectRoot $Script)).Path
    $resultPath = Join-Path $ProjectRoot "Saved\Content\$ResultName"
    $logPath = Join-Path $ProjectRoot "Saved\Logs\$LogName"
    New-Item -ItemType Directory -Force (Split-Path $logPath) | Out-Null
    if (Test-Path $resultPath) { Remove-Item $resultPath -Force }

    # Sin -nullrhi: con él el commandlet de Python puede colgarse (ver ~/.ultron/papercuts.md).
    $cmdArgs = @("`"$UProject`"", '-run=pythonscript', "-script=`"$scriptPath`"", '-unattended', '-nosplash',
        '-nopause', '-NoSound', "-ABSLOG=`"$logPath`"")
    $proc = Start-Process -FilePath $EditorCmd -ArgumentList $cmdArgs -PassThru -WindowStyle Hidden
    if (-not $proc.WaitForExit($TimeoutMinutes * 60 * 1000)) {
        # Solo el proceso lanzado aquí (por PID), nunca por nombre de imagen.
        Stop-Process -Id $proc.Id -Force
        throw "$Script no terminó en $TimeoutMinutes minutos (log: $logPath)"
    }
    if (Test-Path $logPath) {
        Get-Content $logPath -Encoding UTF8 | Where-Object { $_ -match "$Tag|LogPython: Error|Traceback" } | ForEach-Object { Write-Host $_ }
    }
    if (-not (Test-Path $resultPath)) { throw "No se generó $resultPath (código $($proc.ExitCode); log: $logPath)" }
    $result = Get-Content $resultPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not $result.ok) { throw "$Script falló: $($result.error)" }
    return $result
}

if ($Mode -in @('All', 'Art')) {
    $art = Invoke-EditorPython 'Tools/Content/import_art.py' 'import_art.json' 'ImportArt.log' 'LiminalArt'
    Write-Output "Arte listo: $($art.count) mallas, $(@($art.changed).Count) assets cambiados"
}
if ($Mode -in @('All', 'Content')) {
    $result = Invoke-EditorPython 'Tools/Content/build_content.py' 'build_content.json' 'Content.log' 'LiminalContent'
    Write-Output "Contenido listo: $($result.assets.Count) assets; L_Calibration con $($result.calibrationActors) actores; L_Hub con $($result.hubActors) actores y $($result.hubPortals) portales"
    if ($result.hubPortals -ne 8) { throw "L_Hub debe tener 8 portales y tiene $($result.hubPortals)" }
}
