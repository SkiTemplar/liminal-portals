# Compila el editor (por defecto) o el juego y muestra solo errores, avisos y el resultado.
param(
    [ValidateSet('Editor', 'Game')] [string]$Target = 'Editor',
    [ValidateSet('Development', 'DebugGame', 'Shipping')] [string]$Configuration = 'Development'
)
. "$PSScriptRoot\common.ps1"

$targetName = if ($Target -eq 'Editor') { 'LiminalPortalsEditor' } else { 'LiminalPortals' }
# -DisableAdaptiveUnity: el adaptive unity saca del blob los .cpp modificados según git, así que un
# árbol con cambios sin commitear compila aunque el unity completo (árbol limpio) choque por
# símbolos repetidos en namespaces anónimos. Se compila siempre el unity completo.
$cmdArgs = @($targetName, 'Win64', $Configuration, "-Project=$UProject", '-WaitMutex', '-NoHotReloadFromIDE', '-DisableAdaptiveUnity')
$code = Invoke-Native $BuildBat $cmdArgs '(error|warning C\d+|Result:)'
if ($code -ne 0) { throw "Build $targetName falló con código $code" }
