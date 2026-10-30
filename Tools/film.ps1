# Rueda los planos de Liminal.Film fuera de pantalla y los monta en vídeo.
#
# Lanza /Game/Maps/L_Hub sin ventana (-RenderOffscreen) en un escritorio de Windows aparte, sin
# sonido, con paso fijo de 1/60 s y prioridad BelowNormal; Liminal.Film guarda un fotograma de cada -CaptureEvery en
# Saved/Film/<Plano>/frame_NNNN.png y el índice Saved/Film/film.json. Falla si el juego sale con un
# código distinto de 0 o si el índice no está completo (cruces y desviación de cada plano). Después,
# salvo con -NoEncode, monta cada plano en Saved/Film/<Plano>.mp4 (H.264, yuv420p) con ffmpeg.
#
# Uso: powershell -File Tools/film.ps1 [-Filter <texto>] [-CaptureEvery 2] [-ResX 1920] [-ResY 1080]
#          [-MaxFPS 20] [-ExecCmds '...'] [-NoEncode]
param(
    [string]$Filter = '*',
    [int]$CaptureEvery = 2,
    [int]$ResX = 1920,
    [int]$ResY = 1080,
    # Tope de fotogramas reales por segundo: con paso fijo no cambia el resultado y deja GPU libre.
    [int]$MaxFPS = 20,
    [string]$ExecCmds = '',
    [int]$TimeoutMinutes = 60,
    [switch]$NoEncode
)
. "$PSScriptRoot\common.ps1"

$filmDir = Join-Path $ProjectRoot 'Saved\Film'
$index = Join-Path $filmDir 'film.json'
$logPath = Join-Path $ProjectRoot 'Saved\Logs\Film.log'
# Si el juego no llega a lanzar Liminal.Film, no se da por bueno el índice de una ejecución anterior.
if (Test-Path $index) { Remove-Item $index -Force }

$commands = @('r.ScreenPercentage 100', 'DisableAllScreenMessages', "t.MaxFPS $MaxFPS")
if ($ExecCmds) { $commands += $ExecCmds }
$commands += "Liminal.Film $Filter $CaptureEvery"
$exec = $commands -join ','
$cmdArgs = @("`"$UProject`"", '/Game/Maps/L_Hub', '-game', '-RenderOffscreen', "-ResX=$ResX", "-ResY=$ResY", '-ForceRes',
    '-UseFixedTimeStep', '-FPS=60', '-NoSound', '-nosplash', '-unattended', "-ExecCmds=`"$exec`"",
    "-ABSLOG=`"$logPath`"")
# Escritorio de Windows aparte para el juego: las ventanas y diálogos que cree (aunque sea sin querer,
# con -RenderOffscreen) no llegan a la pantalla ni pueden quitar el foco a lo que se esté usando.
Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
public static class LiminalHiddenDesktop {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct StartupInfo {
        public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct ProcessInformation { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
    delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateDesktop(string name, IntPtr device, IntPtr devMode, int flags, uint access, IntPtr attributes);
    [DllImport("user32.dll", SetLastError = true)] static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("user32.dll")] static extern bool EnumDesktopWindows(IntPtr desktop, EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int max);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool CreateProcess(string application, StringBuilder commandLine, IntPtr processAttributes, IntPtr threadAttributes,
        bool inheritHandles, uint flags, IntPtr environment, string directory, ref StartupInfo startupInfo, out ProcessInformation info);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    const uint GenericAll = 0x10000000;
    const uint BelowNormalPriorityClass = 0x00004000;

    public static IntPtr Create(string name) {
        IntPtr desktop = CreateDesktop(name, IntPtr.Zero, IntPtr.Zero, 0, GenericAll, IntPtr.Zero);
        if (desktop == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        return desktop;
    }
    public static void Close(IntPtr desktop) { CloseDesktop(desktop); }
    /** Lanza el proceso en el escritorio con prioridad BelowNormal desde su creación; devuelve el PID. */
    public static int Start(string name, string application, string arguments, string directory) {
        StartupInfo startup = new StartupInfo();
        startup.cb = Marshal.SizeOf(typeof(StartupInfo));
        startup.lpDesktop = "WinSta0\\" + name;
        StringBuilder commandLine = new StringBuilder("\"" + application + "\" " + arguments);
        ProcessInformation info;
        if (!CreateProcess(application, commandLine, IntPtr.Zero, IntPtr.Zero, false, BelowNormalPriorityClass,
                IntPtr.Zero, directory, ref startup, out info))
            throw new Win32Exception(Marshal.GetLastWin32Error());
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
        return info.dwProcessId;
    }
    /** Títulos de las ventanas visibles del escritorio (diagnóstico). */
    public static string[] VisibleTitles(IntPtr desktop) {
        List<string> titles = new List<string>();
        EnumDesktopWindows(desktop, delegate (IntPtr hWnd, IntPtr lParam) {
            if (IsWindowVisible(hWnd)) {
                StringBuilder text = new StringBuilder(256);
                GetWindowText(hWnd, text, text.Capacity);
                titles.Add(text.ToString());
            }
            return true;
        }, IntPtr.Zero);
        return titles.ToArray();
    }
}
"@

$desktopName = "LiminalFilm_$PID"
$desktop = [LiminalHiddenDesktop]::Create($desktopName)
Write-Host "== Film ($exec) en el escritorio $desktopName"
$sw = [Diagnostics.Stopwatch]::StartNew()
try {
    $gamePid = [LiminalHiddenDesktop]::Start($desktopName, $Editor, ($cmdArgs -join ' '), $ProjectRoot)
    $proc = Get-Process -Id $gamePid
    # Sin leer Handle antes de que salga, ExitCode queda vacío en Windows PowerShell.
    $null = $proc.Handle
    $seen = @{}
    $deadline = (Get-Date).AddMinutes($TimeoutMinutes)
    while (-not $proc.WaitForExit(2000)) {
        foreach ($title in [LiminalHiddenDesktop]::VisibleTitles($desktop)) {
            if (-not $seen.ContainsKey($title)) { $seen[$title] = $true; Write-Host "   ventana en el escritorio aparte: '$title'" }
        }
        if ((Get-Date) -gt $deadline) {
            # Solo el proceso lanzado aquí (por PID), nunca por nombre de imagen.
            Stop-Process -Id $gamePid -Force
            throw "Film no terminó en $TimeoutMinutes minutos (log: $logPath)"
        }
    }
    $proc.WaitForExit()
}
finally {
    [LiminalHiddenDesktop]::Close($desktop)
}
Write-Host ("   {0:N0} s, código {1}" -f $sw.Elapsed.TotalSeconds, $proc.ExitCode)
if ($proc.ExitCode -ne 0) { throw "Film salió con código $($proc.ExitCode) (log: $logPath)" }
if (-not (Test-Path $index)) { throw "Liminal.Film no escribió $index (log: $logPath)" }

$report = Get-Content $index -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($shot in $report.shots) {
    $mark = if ($shot.complete) { 'OK  ' } else { 'FAIL' }
    Write-Host ("{0} {1,-14} {2,-10} {3,4} fotogramas, cruces {4} de {5}, desviación máxima {6:N4} cm" -f $mark,
        $shot.name, $shot.label, $shot.savedFrames, $shot.crossings, $shot.expectedCrossings, $shot.maxDeviationCm)
}
if (-not $report.complete) { throw "El rodaje no está completo: $($report.error) (ver $index)" }
if ($NoEncode) { exit 0 }

$ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
if (-not $ffmpeg) { throw 'No se encuentra ffmpeg en el PATH (o usa -NoEncode)' }
$rate = [double]$report.sampleRate / [int]$report.captureEvery
foreach ($shot in $report.shots) {
    $frames = Join-Path $filmDir "$($shot.name)\frame_%04d.png"
    $video = Join-Path $filmDir "$($shot.name).mp4"
    $code = Invoke-Native $ffmpeg.Source @('-y', '-loglevel', 'error', '-threads', '4', '-framerate', "$rate", '-i', $frames,
        '-c:v', 'libx264', '-preset', 'slow', '-crf', '16', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', $video)
    if ($code -ne 0) { throw "ffmpeg falló con $($shot.name) (código $code)" }
    Write-Host "   $video"
}
