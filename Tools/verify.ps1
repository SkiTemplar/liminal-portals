# Verificaciones en ejecución con RHI (no son Automation Tests: necesitan render real).
#
#   -Calibrate  Etapa E2: lanza el juego en /Game/Maps/L_Calibration dos veces (Liminal.Portal.LagFrames
#               0 y 1) con Liminal.Calibrate, y compara las pasadas con Tools/Verify/compare_calibration.py.
#               Escribe Saved/Calibration/result.json y falla si no se cumple la aceptación.
#   -ClipPlaneCheck  Con -Calibrate, añade una ejecución informativa con el clip plane invertido.
#   -SelfTest   Etapa E3: lanza /Game/Maps/L_Hub con paso fijo (-UseFixedTimeStep -FPS=60) y Liminal.SelfTest
#               (20 ciclos hub -> bioma -> hub por puerta). Lee Saved/SelfTest.json y falla si no pasa.
#   -SelfTestNegative  Con -SelfTest, añade el control negativo (velocidad al 97 % tras cada paso:
#               la prueba tiene que fallar en las cuatro puertas). Deja Saved/SelfTest.json del negativo.
#   -Tour       Etapas E3 y E4: lanza /Game/Maps/L_Hub con Liminal.Tour (13 capturas: hub y, por puerta,
#               desde el hub, desde el bioma y desde dentro), las comprueba con Tools/Verify/check_tour.py
#               (luminancia, desviación y hueco distinto del marco; Saved/Tour/result.json) y compone
#               Saved/Tour/lamina.png con Tools/Verify/make_lamina.py.
#   -ExecCmds   Órdenes de consola extra antes de la verificación (p. ej. 'r.AntiAliasingMethod 1').
#
# Uso: powershell -File Tools/verify.ps1 [-Calibrate [-ClipPlaneCheck]] [-SelfTest] [-Tour] [-ExecCmds '...']
param(
    [switch]$Calibrate,
    [switch]$ClipPlaneCheck,
    [switch]$SelfTest,
    [switch]$SelfTestNegative,
    [switch]$Tour,
    [string]$ExecCmds = '',
    [int]$SelfTestCycles = 20,
    [int]$TimeoutMinutes = 45,
    [int]$ResX = 1280,
    [int]$ResY = 720
)
. "$PSScriptRoot\common.ps1"

$CalibrationDir = Join-Path $ProjectRoot 'Saved\Calibration'
$CalibrationMap = '/Game/Maps/L_Calibration'
$HubMap = '/Game/Maps/L_Hub'
$NumDoors = 4

# Lanza el juego con RHI en $Map, ejecuta $Commands y espera a que salga solo (con tiempo máximo).
function Invoke-GameRun([string]$Map, [string[]]$Commands, [string]$LogName, [string[]]$ExtraArgs = @()) {
    $logPath = Join-Path $ProjectRoot "Saved\Logs\$LogName.log"
    $exec = (@($Commands) | Where-Object { $_ }) -join ','
    $cmdArgs = @("`"$UProject`"", $Map, '-game', '-windowed', "-ResX=$ResX", "-ResY=$ResY", '-ForceRes',
        '-NoSound', '-nosplash', '-unattended', "-ExecCmds=`"$exec`"", '-log', "-ABSLOG=`"$logPath`"") + $ExtraArgs
    Write-Host "== $LogName ($exec)"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $proc = Start-Process -FilePath $Editor -ArgumentList $cmdArgs -PassThru
    if (-not $proc.WaitForExit($TimeoutMinutes * 60 * 1000)) {
        # Solo el proceso lanzado aquí (por PID), nunca por nombre de imagen.
        Stop-Process -Id $proc.Id -Force
        throw "$LogName no terminó en $TimeoutMinutes minutos (log: $logPath)"
    }
    Write-Host ("   {0:N0} s, código {1}" -f $sw.Elapsed.TotalSeconds, $proc.ExitCode)
    return $logPath
}

function Invoke-CalibrationRun([string]$Tag, [string[]]$Commands) {
    $runDir = Join-Path $CalibrationDir $Tag
    if (Test-Path $runDir) { Remove-Item $runDir -Recurse -Force }
    $logPath = Invoke-GameRun $CalibrationMap (@($Commands) + @("Liminal.Calibrate $Tag")) "Calibrate_$Tag"

    $report = Join-Path $runDir 'calibration.json'
    if (-not (Test-Path $report)) { throw "La calibración $Tag no escribió $report (log: $logPath)" }
    $data = Get-Content $report -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not $data.complete) { throw "Calibración $Tag incompleta: $($data.error) (log: $logPath)" }
    Write-Host "   $($data.savedScreenshots) capturas, LagFrames $($data.lagFrames), AA $($data.antiAliasingMethod)"
    return $runDir
}

function Invoke-Calibration([string[]]$ExtraCommands) {
    New-Item -ItemType Directory -Force $CalibrationDir | Out-Null
    $reference = Invoke-CalibrationRun 'lag0' ($ExtraCommands + @('Liminal.Portal.LagFrames 0'))
    $control = Invoke-CalibrationRun 'lag1' ($ExtraCommands + @('Liminal.Portal.LagFrames 1'))
    $extraRuns = @()
    if ($ClipPlaneCheck) {
        $extraRuns += Invoke-CalibrationRun 'flipclip' ($ExtraCommands + @('Liminal.Portal.LagFrames 0', 'Liminal.Portal.FlipClipPlane 1'))
    }

    $compare = Join-Path $ProjectRoot 'Tools\Verify\compare_calibration.py'
    $result = Join-Path $CalibrationDir 'result.json'
    $pyArgs = @('run', '--no-project', '--with', 'numpy', '--with', 'pillow', 'python', $compare,
        '--reference', $reference, '--control', $control, '--out', $result)
    if ($extraRuns.Count -gt 0) { $pyArgs += @('--extra') + $extraRuns }
    $env:PYTHONIOENCODING = 'utf-8'
    $code = Invoke-Native 'uv' $pyArgs '.'
    if ($code -ne 0) { throw "La calibración no cumple la aceptación (código $code): ver $result" }
}

# Ejecuta Liminal.SelfTest con $Cycles ciclos por puerta y devuelve el informe; muestra el resumen.
function Invoke-SelfTestRun([int]$Cycles, [string[]]$Commands, [string]$LogName) {
    $report = Join-Path $ProjectRoot 'Saved\SelfTest.json'
    if (Test-Path $report) { Remove-Item $report -Force }
    # Paso fijo de 1/60 s: el movimiento no depende de lo rápido que renderice la máquina.
    $logPath = Invoke-GameRun $HubMap ($Commands + @("Liminal.SelfTest $Cycles")) $LogName @('-UseFixedTimeStep', '-FPS=60')
    if (-not (Test-Path $report)) { throw "Liminal.SelfTest no escribió $report (log: $logPath)" }

    $data = Get-Content $report -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($door in $data.doors) {
        $mark = if ($door.passed) { 'OK  ' } else { 'FAIL' }
        Write-Host "$mark puerta $($door.index) ($($door.biome)): hub->bioma $($door.hubToBiome), bioma->hub $($door.biomeToHub), fallos $($door.failedCrossings)"
        foreach ($e in @($door.errors) | Select-Object -First 2) { Write-Host "     $e" }
    }
    Write-Host ("   cruces {0}; error de velocidad máx. {1:N4} %; dirección máx. {2:N4}°; posición máx. {3:N3} cm; velocidad mín. tras el cruce {4:P2}" -f `
        $data.crossings, $data.maxSpeedErrorPct, $data.maxDirectionErrorDeg, $data.maxPositionErrorCm, $data.minNextFrameSpeedRatio)
    Write-Host "   cámara detrás de un portal: $($data.cameraInsidePortalFrames) fotogramas; dentro de geometría: $($data.cameraInGeometryFrames) fotogramas $($data.firstCameraError)"
    return $data
}

function Invoke-SelfTest([string[]]$ExtraCommands) {
    $data = Invoke-SelfTestRun $SelfTestCycles $ExtraCommands 'SelfTest'
    $expected = $NumDoors * $SelfTestCycles
    $complete = $data.hubToBiome -eq $expected -and $data.biomeToHub -eq $expected -and @($data.doors).Count -eq $NumDoors
    if (-not $data.passed -or -not $complete) {
        throw "Liminal.SelfTest no pasa: $($data.error) (esperados $expected cruces por sentido; ver Saved/SelfTest.json)"
    }
    Write-Host "SelfTest OK: $($data.hubToBiome) cruces hub->bioma y $($data.biomeToHub) bioma->hub en $($data.frames) fotogramas"
}

# Control negativo: con la velocidad escalada al 97 % tras cada paso, la prueba tiene que fallar por velocidad.
function Invoke-SelfTestNegative([string[]]$ExtraCommands) {
    $data = Invoke-SelfTestRun 2 ($ExtraCommands + @('Liminal.Portal.DebugVelocityScale 0.97')) 'SelfTest_Negative'
    $failed = @($data.doors | Where-Object { $_.failedCrossings -gt 0 }).Count
    if ($data.passed -or $failed -ne $NumDoors -or $data.maxSpeedErrorPct -lt 2.5) {
        throw "El control negativo no falla como debe (passed $($data.passed), puertas con fallos $failed, error máx. $($data.maxSpeedErrorPct) %)"
    }
    Write-Host ("Control negativo OK: falla en las {0} puertas (error de velocidad máx. {1:N2} %)" -f $failed, $data.maxSpeedErrorPct)
}

function Invoke-Tour([string[]]$ExtraCommands) {
    $tourDir = Join-Path $ProjectRoot 'Saved\Tour'
    $logPath = Invoke-GameRun $HubMap ($ExtraCommands + @('Liminal.Tour')) 'Tour'
    $index = Join-Path $tourDir 'tour.json'
    if (-not (Test-Path $index)) { throw "Liminal.Tour no escribió $index (log: $logPath)" }

    $check = Join-Path $ProjectRoot 'Tools\Verify\check_tour.py'
    $result = Join-Path $tourDir 'result.json'
    $env:PYTHONIOENCODING = 'utf-8'
    $code = Invoke-Native 'uv' @('run', '--no-project', '--with', 'numpy', '--with', 'pillow', 'python', $check,
        '--tour', $tourDir, '--out', $result) '.'
    if ($code -ne 0) { throw "Las capturas del Tour no cumplen (código $code): ver $result" }
    $shots = @((Get-Content $index -Raw -Encoding UTF8 | ConvertFrom-Json).shots).Count
    if ($shots -ne 1 + 3 * $NumDoors) { throw "El Tour tiene $shots capturas y se esperaban $(1 + 3 * $NumDoors)" }

    $lamina = Join-Path $tourDir 'lamina.png'
    $make = Join-Path $ProjectRoot 'Tools\Verify\make_lamina.py'
    $code = Invoke-Native 'uv' @('run', '--no-project', '--with', 'pillow', 'python', $make, '--tour', $tourDir, '--out', $lamina) '.'
    if ($code -ne 0 -or -not (Test-Path $lamina)) { throw "No se pudo generar $lamina (código $code)" }
    Write-Host "Lámina: $lamina"
}

if (-not ($Calibrate -or $SelfTest -or $Tour)) {
    Write-Output 'Nada que verificar: usa -Calibrate, -SelfTest o -Tour.'
    exit 0
}

$extraCommands = @()
if ($ExecCmds) { $extraCommands = @($ExecCmds -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }

if ($Calibrate) { Invoke-Calibration $extraCommands }
if ($SelfTest) {
    if ($SelfTestNegative) { Invoke-SelfTestNegative $extraCommands }
    Invoke-SelfTest $extraCommands
}
if ($Tour) { Invoke-Tour $extraCommands }
