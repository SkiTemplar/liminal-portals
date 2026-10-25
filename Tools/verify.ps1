# Verificaciones en ejecución con RHI (no son Automation Tests: necesitan render real).
#
#   -Calibrate  Etapa E2: lanza el juego en /Game/Maps/L_Calibration dos veces (Liminal.Portal.LagFrames
#               0 y 1) con Liminal.Calibrate, y compara las pasadas con Tools/Verify/compare_calibration.py.
#               Escribe Saved/Calibration/result.json y falla si no se cumple la aceptación.
#   -ClipPlaneCheck  Con -Calibrate, añade una ejecución informativa con el clip plane invertido.
#   -ExecCmds   Órdenes de consola extra antes de calibrar (p. ej. 'r.AntiAliasingMethod 1').
#
# Uso: powershell -File Tools/verify.ps1 -Calibrate [-ClipPlaneCheck] [-ExecCmds 'r.AntiAliasingMethod 2']
param(
    [switch]$Calibrate,
    [switch]$ClipPlaneCheck,
    [string]$ExecCmds = '',
    [int]$TimeoutMinutes = 45,
    [int]$ResX = 1280,
    [int]$ResY = 720
)
. "$PSScriptRoot\common.ps1"

$CalibrationDir = Join-Path $ProjectRoot 'Saved\Calibration'
$CalibrationMap = '/Game/Maps/L_Calibration'

# Lanza el juego con RHI, ejecuta Liminal.Calibrate <Tag> y espera a que salga solo.
function Invoke-CalibrationRun([string]$Tag, [string[]]$Commands) {
    $runDir = Join-Path $CalibrationDir $Tag
    if (Test-Path $runDir) { Remove-Item $runDir -Recurse -Force }
    $logPath = Join-Path $ProjectRoot "Saved\Logs\Calibrate_$Tag.log"

    $allCommands = @($Commands) + @("Liminal.Calibrate $Tag") | Where-Object { $_ }
    $exec = ($allCommands -join ',')
    $cmdArgs = @("`"$UProject`"", $CalibrationMap, '-game', '-windowed', "-ResX=$ResX", "-ResY=$ResY", '-ForceRes',
        '-NoSound', '-nosplash', '-unattended', "-ExecCmds=`"$exec`"", '-log', "-ABSLOG=`"$logPath`"")
    Write-Host "== Calibración $Tag ($exec)"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $proc = Start-Process -FilePath $Editor -ArgumentList $cmdArgs -PassThru
    if (-not $proc.WaitForExit($TimeoutMinutes * 60 * 1000)) {
        # Solo el proceso lanzado aquí (por PID), nunca por nombre de imagen.
        Stop-Process -Id $proc.Id -Force
        throw "La calibración $Tag no terminó en $TimeoutMinutes minutos (log: $logPath)"
    }
    Write-Host ("   {0:N0} s, código {1}" -f $sw.Elapsed.TotalSeconds, $proc.ExitCode)

    $report = Join-Path $runDir 'calibration.json'
    if (-not (Test-Path $report)) { throw "La calibración $Tag no escribió $report (log: $logPath)" }
    $data = Get-Content $report -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not $data.complete) { throw "Calibración $Tag incompleta: $($data.error) (log: $logPath)" }
    Write-Host "   $($data.savedScreenshots) capturas, LagFrames $($data.lagFrames), AA $($data.antiAliasingMethod)"
    return $runDir
}

if (-not $Calibrate) {
    Write-Output 'Nada que verificar: usa -Calibrate.'
    exit 0
}

New-Item -ItemType Directory -Force $CalibrationDir | Out-Null
$extraCommands = @()
if ($ExecCmds) { $extraCommands = @($ExecCmds -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }

$reference = Invoke-CalibrationRun 'lag0' ($extraCommands + @('Liminal.Portal.LagFrames 0'))
$control = Invoke-CalibrationRun 'lag1' ($extraCommands + @('Liminal.Portal.LagFrames 1'))
$extraRuns = @()
if ($ClipPlaneCheck) {
    $extraRuns += Invoke-CalibrationRun 'flipclip' ($extraCommands + @('Liminal.Portal.LagFrames 0', 'Liminal.Portal.FlipClipPlane 1'))
}

$compare = Join-Path $ProjectRoot 'Tools\Verify\compare_calibration.py'
$result = Join-Path $CalibrationDir 'result.json'
$pyArgs = @('run', '--no-project', '--with', 'numpy', '--with', 'pillow', 'python', $compare,
    '--reference', $reference, '--control', $control, '--out', $result)
if ($extraRuns.Count -gt 0) { $pyArgs += @('--extra') + $extraRuns }
$env:PYTHONIOENCODING = 'utf-8'
$code = Invoke-Native 'uv' $pyArgs '.'
if ($code -ne 0) { throw "La calibración no cumple la aceptación (código $code): ver $result" }
