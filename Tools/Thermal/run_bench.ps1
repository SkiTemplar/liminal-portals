# Benchmark térmico por fases: reposo, CPU, GPU (Liminal.Bench) y combinado.
# Registra contadores de Windows cada 2 s, marca las fases y hace capturas de pantalla cada 30 s.
param([int]$IdleSeconds = 120, [int]$CpuSeconds = 300, [int]$CoolSeconds = 120, [int]$GpuRuns = 4, [int]$ComboRuns = 2)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out = Join-Path $root 'Saved\Thermal'
$shots = Join-Path $out 'shots'
New-Item -ItemType Directory -Force -Path $shots | Out-Null
$phases = Join-Path $out 'phases.csv'
'phase,start,end' | Set-Content $phases -Encoding UTF8

$counterJob = Start-Job -ArgumentList (Join-Path $out 'counters.csv') -ScriptBlock {
    param($file)
    # Los nombres de los contadores están localizados (Windows en español).
    $c = @('\Información sobre la zona térmica(*)\Temperatura de alta precisión',
           '\Información del procesador(_Total)\% de rendimiento del procesador',
           '\Información del procesador(_Total)\% de utilidad del procesador',
           '\Información del procesador(_Total)\% de límite de rendimiento')
    'timestamp,tz_k,perf_pct,util_pct,limit_pct' | Set-Content $file -Encoding UTF8
    while ($true) {
        try {
            $s = (Get-Counter $c -ErrorAction Stop).CounterSamples
            $v = $s | ForEach-Object { [string]::Format([Globalization.CultureInfo]::InvariantCulture, '{0:F2}', $_.CookedValue) }
            "$((Get-Date).ToString('o')),$($v -join ',')" | Add-Content $file -Encoding UTF8
        }
        catch { "$((Get-Date).ToString('o')),ERR $($_.Exception.Message)" | Add-Content $file -Encoding UTF8 }
        Start-Sleep -Seconds 2
    }
}
$shotJob = Start-Job -ArgumentList $shots -ScriptBlock {
    param($dir)
    Add-Type -AssemblyName System.Windows.Forms, System.Drawing
    while ($true) {
        $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
        $bmp.Save((Join-Path $dir ("{0}.png" -f (Get-Date -Format 'HHmmss'))), [System.Drawing.Imaging.ImageFormat]::Png)
        $g.Dispose(); $bmp.Dispose()
        Start-Sleep -Seconds 30
    }
}

function Invoke-Phase([string]$Name, [scriptblock]$Body) {
    $start = (Get-Date).ToString('o')
    Write-Output "== $Name ($start)"
    & $Body
    "$Name,$start,$((Get-Date).ToString('o'))" | Add-Content $phases -Encoding UTF8
}
$stress = Join-Path $PSScriptRoot 'cpu_stress.py'
try {
    Invoke-Phase 'idle' { Start-Sleep -Seconds $IdleSeconds }
    Invoke-Phase 'cpu' { & uv run python $stress $CpuSeconds | Out-Host }
    Invoke-Phase 'cool1' { Start-Sleep -Seconds $CoolSeconds }
    if ($GpuRuns -gt 0) { Invoke-Phase 'gpu' { & powershell -NoProfile -File (Join-Path $root 'Tools\verify.ps1') -Bench -BenchRuns $GpuRuns | Out-Host } }
    Invoke-Phase 'combo' {
        $p = Start-Process -FilePath 'uv' -ArgumentList @('run', 'python', "`"$stress`"", '9999', '16') -PassThru -WindowStyle Hidden
        try { & powershell -NoProfile -File (Join-Path $root 'Tools\verify.ps1') -Bench -BenchRuns $ComboRuns | Out-Host }
        finally { & taskkill /PID $p.Id /T /F | Out-Null }
    }
    Invoke-Phase 'cool2' { Start-Sleep -Seconds $CoolSeconds }
}
finally {
    Stop-Job $counterJob, $shotJob; Remove-Job $counterJob, $shotJob -Force
}
Write-Output 'Benchmark térmico terminado.'
