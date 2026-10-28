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
#   -Bench      Etapa E5: lanza /Game/Maps/L_Hub a -BenchResX x -BenchResY (1920x1080 por defecto; si la
#               pantalla principal no lo admite, el mayor 16:9 que quepa) en ventana, con r.VSync 0 y
#               t.MaxFPS 0, y ejecuta Liminal.Bench dos veces (Saved/Bench_run1.json y Bench_run2.json; el
#               último queda en Saved/Bench.json). Falla si algún caso no tiene las capturas esperadas;
#               avisa si el coste por portal difiere un 25 % o más entre ejecuciones. Resumen en
#               Saved/BenchResult.json.
#   -Profile    Con -Bench (D-36): Liminal.Bench en modo perfil. Además de los tiempos, el desglose de GPU por pase
#               de cada caso, separando la vista principal de cada captura, y el recorrido del hub (capturas por
#               fotograma, superficies dibujadas y área en pantalla de los portales). Saved/BenchProfile.json (y
#               BenchProfile_run<N>.json); Tools/Verify/summarize_profile.py lo resume en BenchProfileSummary.json.
#   -BenchCross Solo los cruces del modo perfil, sin casos, recorrido ni perfilador (unos 2 min): Liminal.Bench … cross
#               a la resolución de -Bench. Por puerta y pasada, el pico al cruzar y el del giro de 180° hacia la puerta
#               de vuelta, con el estado de render que tenía esa puerta (D-31). Saved/BenchCross.json.
#   -BenchRuns  Ejecuciones de -Bench (2 por defecto; con 1 no se comprueba la estabilidad).
#   -ExecCmds  Órdenes de consola extra antes de la verificación (p. ej. 'r.AntiAliasingMethod 1').
#
# Uso: powershell -File Tools/verify.ps1 [-Calibrate [-ClipPlaneCheck]] [-SelfTest] [-Tour] [-Bench [-Profile]] [-BenchCross] [-ExecCmds '...']
param(
    [switch]$Calibrate,
    [switch]$ClipPlaneCheck,
    [switch]$SelfTest,
    [switch]$SelfTestNegative,
    [switch]$Tour,
    [switch]$Bench,
    [switch]$Profile,
    [switch]$BenchCross,
    [ValidateRange(1, 10)] [int]$BenchRuns = 2,
    [string]$ExecCmds = '',
    [int]$SelfTestCycles = 20,
    [int]$TimeoutMinutes = 45,
    [int]$ResX = 1280,
    [int]$ResY = 720,
    [int]$BenchResX = 1920,
    [int]$BenchResY = 1080,
    [int]$BenchFrames = 240,
    [int]$BenchWarmup = 120,
    [double]$BenchMaxRelativeDiff = 0.25
)
. "$PSScriptRoot\common.ps1"

$CalibrationDir = Join-Path $ProjectRoot 'Saved\Calibration'
$CalibrationMap = '/Game/Maps/L_Calibration'
$HubMap = '/Game/Maps/L_Hub'
$NumDoors = 4

# Lanza el juego con RHI en $Map, ejecuta $Commands y espera a que salga solo (con tiempo máximo).
function Invoke-GameRun([string]$Map, [string[]]$Commands, [string]$LogName, [string[]]$ExtraArgs = @(),
        [int]$Width = $ResX, [int]$Height = $ResY) {
    $logPath = Join-Path $ProjectRoot "Saved\Logs\$LogName.log"
    $exec = (@($Commands) | Where-Object { $_ }) -join ','
    $cmdArgs = @("`"$UProject`"", $Map, '-game', '-windowed', "-ResX=$Width", "-ResY=$Height", '-ForceRes',
        '-NoSound', '-nosplash', '-unattended', "-ExecCmds=`"$exec`"", '-log', "-ABSLOG=`"$logPath`"") + $ExtraArgs
    Write-Host "== $LogName ($exec)"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $proc = Start-Process -FilePath $Editor -ArgumentList $cmdArgs -PassThru
    # Sin leer Handle antes de que salga, ExitCode queda vacío en Windows PowerShell.
    $null = $proc.Handle
    if (-not $proc.WaitForExit($TimeoutMinutes * 60 * 1000)) {
        # Solo el proceso lanzado aquí (por PID), nunca por nombre de imagen.
        Stop-Process -Id $proc.Id -Force
        throw "$LogName no terminó en $TimeoutMinutes minutos (log: $logPath)"
    }
    $proc.WaitForExit()
    Write-Host ("   {0:N0} s, código {1}" -f $sw.Elapsed.TotalSeconds, $proc.ExitCode)
    # Un juego que se cae o no puede lanzar la orden (Liminal.* sale con 2) no da por buenos informes viejos.
    if ($proc.ExitCode -ne 0) { throw "$LogName salió con código $($proc.ExitCode) (log: $logPath)" }
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
    # Si el juego no llega a lanzar Liminal.Tour, no se comprueba el recorrido de una ejecución anterior.
    if (Test-Path $tourDir) { Remove-Item $tourDir -Recurse -Force }
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

# Resolución de la medida: la pedida si cabe en la pantalla principal (píxeles físicos); si no, el mayor 16:9.
function Get-BenchResolution {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -Namespace LiminalBench -Name Dpi -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();'
    [void][LiminalBench.Dpi]::SetProcessDPIAware()
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    if ($bounds.Width -ge $BenchResX -and $bounds.Height -ge $BenchResY) { return @($BenchResX, $BenchResY, $bounds) }
    $width = [Math]::Min($bounds.Width, [Math]::Floor($bounds.Height * 16 / 9))
    $width -= $width % 16
    return @([int]$width, [int]($width * 9 / 16), $bounds)
}

function Get-RelativeDifference([double]$A, [double]$B) {
    $scale = [Math]::Max([Math]::Abs($A), [Math]::Abs($B))
    if ($scale -le 1e-9) { return 0.0 }
    return [Math]::Abs($A - $B) / $scale
}

# Modo perfil: guarda el informe de la ejecución y lo resume (vista principal frente a capturas, recorrido).
function Invoke-ProfileSummary([int]$Run, [string]$ProfileReport, [string]$LogPath) {
    if (-not (Test-Path $ProfileReport)) { throw "Liminal.Bench profile no escribió $ProfileReport (log: $LogPath)" }
    $runReport = Join-Path $ProjectRoot "Saved\BenchProfile_run$Run.json"
    Copy-Item $ProfileReport $runReport -Force
    $summarize = Join-Path $ProjectRoot 'Tools\Verify\summarize_profile.py'
    $summary = Join-Path $ProjectRoot "Saved\BenchProfileSummary_run$Run.json"
    $env:PYTHONIOENCODING = 'utf-8'
    $code = Invoke-Native 'uv' @('run', '--no-project', 'python', $summarize, '--profile', $runReport, '--out', $summary) '.'
    if ($code -ne 0) { throw "No se pudo resumir $runReport (código $code)" }
    Copy-Item $summary (Join-Path $ProjectRoot 'Saved\BenchProfileSummary.json') -Force
}

function Invoke-BenchRun([int]$Run,[int]$Width, [int]$Height, [string[]]$ExtraCommands) {
    $report = Join-Path $ProjectRoot 'Saved\Bench.json'
    $profileReport = Join-Path $ProjectRoot 'Saved\BenchProfile.json'
    foreach ($stale in $report, $profileReport) { if (Test-Path $stale) { Remove-Item $stale -Force } }
    $benchCommand = "Liminal.Bench $BenchFrames $BenchWarmup" + $(if ($Profile) { ' profile' } else { '' })
    $commands = @($ExtraCommands) + @('r.VSync 0', 't.MaxFPS 0', $benchCommand)
    $logPath = Invoke-GameRun $HubMap $commands "Bench_run$Run" @() $Width $Height
    if (-not (Test-Path $report)) { throw "Liminal.Bench no escribió $report (log: $logPath)" }
    Copy-Item $report (Join-Path $ProjectRoot "Saved\Bench_run$Run.json") -Force
    if ($Profile) { Invoke-ProfileSummary $Run $profileReport $logPath }

    $data = Get-Content $report -Raw -Encoding UTF8 | ConvertFrom-Json
    Write-Host ("   {0}, {1}, viewport {2}x{3}, render target {4}x{5}, r.VSync {6}, t.MaxFPS {7}" -f $data.gpu, $data.rhi,
        $data.viewport[0], $data.viewport[1], $data.renderTarget[0], $data.renderTarget[1], $data.cvars.'r.VSync', $data.cvars.'t.MaxFPS')
    foreach ($case in $data.cases) {
        $mark = if ($case.capturesOk) { 'OK  ' } else { 'FAIL' }
        Write-Host ("{0} {1,-18} capturas {2}-{3} (esperadas {4}); GPU media {5:N3} p50 {6:N3} p95 {7:N3} ms; RT {8:N3} ms; GT {9:N3} ms; RHI {10:N3} ms; {11:N1} fps" -f $mark,
            $case.name, $case.minCaptures, $case.maxCaptures, $case.expectedCaptures, $case.gpuMs.mean, $case.gpuMs.p50,
            $case.gpuMs.p95, $case.renderThreadMs.mean, $case.gameThreadMs.mean, $case.rhiThreadMs.mean, (1000.0 / [Math]::Max($case.frameMs.mean, 1e-6)))
    }
    $failed = @($data.cases | Where-Object { -not $_.capturesOk }).Count
    if (-not $data.complete -or @($data.cases).Count -ne 5 -or $failed -gt 0) {
        throw "Liminal.Bench (ejecución $Run) incompleto: $($data.error) (casos $(@($data.cases).Count), con capturas incorrectas $failed; log: $logPath)"
    }
    if ($data.cvars.'r.VSync' -ne '0' -or $data.cvars.'t.MaxFPS' -ne '0') {
        throw "Liminal.Bench (ejecución $Run) no se midió con r.VSync 0 y t.MaxFPS 0"
    }
    Write-Host ("   por portal: GPU {0:N3} ms (4 frente a apagados), pendiente {1:N3} ms; RT {2:N3} ms" -f $data.perPortal.gpuMsPaired,
        $data.perPortal.gpuMsSlope, $data.perPortal.renderThreadMsPaired)
    return $data
}

function Invoke-Bench([string[]]$ExtraCommands) {
    $resolution = Get-BenchResolution
    $width = $resolution[0]; $height = $resolution[1]; $bounds = $resolution[2]
    if ($width -ne $BenchResX -or $height -ne $BenchResY) {
        Write-Host "La pantalla principal ($($bounds.Width)x$($bounds.Height)) no admite ${BenchResX}x${BenchResY} en ventana: se mide a ${width}x${height}"
    }
    $runs = @()
    foreach ($run in 1..$BenchRuns) { $runs += Invoke-BenchRun $run $width $height $ExtraCommands }
    if ($BenchRuns -lt 2) {
        Write-Host ("Bench: coste por portal {0:N3} ms; pendiente {1:N3} ms (una ejecución: sin comprobación de estabilidad)" -f `
            $runs[0].perPortal.gpuMsPaired, $runs[0].perPortal.gpuMsSlope)
        return
    }

    $first = $runs[0].perPortal; $second = $runs[1].perPortal
    $diffPaired = Get-RelativeDifference $first.gpuMsPaired $second.gpuMsPaired
    $diffSlope = Get-RelativeDifference $first.gpuMsSlope $second.gpuMsSlope
    $stable = ($diffPaired -lt $BenchMaxRelativeDiff) -and ($diffSlope -lt $BenchMaxRelativeDiff)
    $summary = [ordered]@{
        gpu = $runs[1].gpu
        requestedResolution = @($BenchResX, $BenchResY)
        launchedResolution = @($width, $height)
        measuredViewport = @($runs[0].viewport, $runs[1].viewport)
        screen = @($bounds.Width, $bounds.Height)
        gpuMsPerPortalPaired = @($first.gpuMsPaired, $second.gpuMsPaired)
        gpuMsPerPortalSlope = @($first.gpuMsSlope, $second.gpuMsSlope)
        relativeDifferencePaired = $diffPaired
        relativeDifferenceSlope = $diffSlope
        maxRelativeDifference = $BenchMaxRelativeDiff
        stable = $stable
    }
    $summary | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $ProjectRoot 'Saved\BenchResult.json') -Encoding UTF8
    $verdict = if ($stable) { 'estable' } else { 'INESTABLE (diferencia >= {0:P0})' -f $BenchMaxRelativeDiff }
    Write-Host ("Bench: coste por portal {0:N3} / {1:N3} ms (diferencia {2:P1}); pendiente {3:N3} / {4:N3} ms (diferencia {5:P1}): {6}" -f `
        $first.gpuMsPaired, $second.gpuMsPaired, $diffPaired, $first.gpuMsSlope, $second.gpuMsSlope, $diffSlope, $verdict)
}

function Invoke-BenchCross([string[]]$ExtraCommands) {
    $resolution = Get-BenchResolution
    $report = Join-Path $ProjectRoot 'Saved\BenchCross.json'
    if (Test-Path $report) { Remove-Item $report -Force }
    $commands = @($ExtraCommands) + @('r.VSync 0', 't.MaxFPS 0', "Liminal.Bench $BenchFrames $BenchWarmup cross")
    $logPath = Invoke-GameRun $HubMap $commands 'BenchCross' @() $resolution[0] $resolution[1]
    if (-not (Test-Path $report)) { throw "Liminal.Bench cross no escribió $report (log: $logPath)" }
    $data = Get-Content $report -Raw -Encoding UTF8 | ConvertFrom-Json
    Write-Host ("   {0}, viewport {1}x{2}" -f $data.gpu, $data.viewport[0], $data.viewport[1])
    foreach ($door in $data.crossings.doors) {
        $turn = $door.turn
        Write-Host ("{0,-22} pasada {1}: cruce {2:N1} ms (mediana antes {3:N1}, x{4:N1}); giro {5:N1} ms en el fotograma {6} (mediana {7:N1}, x{8:N1}, RT {9:N1} ms); vuelta {10}: estado al cruzar {11}, antes de girar {12}, recuperado en {13}" -f `
            $door.portal, $door.pass, $door.frameMsMaxFromCross, $door.frameMsMedianBefore, $door.spikeRatio, $turn.frameMsMax,
            $turn.frameMsMaxIndex, $turn.frameMsMedian, $turn.spikeRatio, $turn.renderThreadMsMax, $turn.returnPortal,
            $turn.returnHadStateAtCross, $turn.returnHadStateBeforeTurn, $turn.returnStateFrame)
    }
    if (-not $data.complete) { throw "Liminal.Bench cross incompleto: $($data.error) (log: $logPath)" }
}

if ($ClipPlaneCheck -and -not $Calibrate) { throw '-ClipPlaneCheck solo vale con -Calibrate.' }
if ($SelfTestNegative -and -not $SelfTest) { throw '-SelfTestNegative solo vale con -SelfTest.' }
if ($Profile -and -not $Bench) { throw '-Profile solo vale con -Bench.' }
if (-not ($Calibrate -or $SelfTest -or $Tour -or $Bench -or $BenchCross)) {
    Write-Output 'Nada que verificar: usa -Calibrate, -SelfTest, -Tour, -Bench o -BenchCross.'
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
if ($Bench) { Invoke-Bench $extraCommands }
if ($BenchCross) { Invoke-BenchCross $extraCommands }
