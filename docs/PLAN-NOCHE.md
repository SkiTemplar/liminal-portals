# Plan de implementación nocturna (2026-10-25)

Especificación compartida por todos los agentes que trabajan esta noche. El diseño de referencia
está en `README.md`; este documento fija lo que README deja abierto y lo divide en etapas con
criterios de aceptación medibles. Rama: `feat/portales-fases-1-4` (sin push).

## Reglas para los agentes

- Un solo árbol de trabajo compartido: las etapas son secuenciales. No crear worktrees.
- Compilar con `powershell -File Tools/build.ps1`; tests con `powershell -File Tools/test.ps1`.
  Si `Build.bat` sale al instante sin compilar, lanzar `UnrealBuildTool.exe` con los mismos argumentos
  (ver `~/.ultron/papercuts.md`).
- Procesos de Unreal: cerrar solo por PID propio, nunca con `taskkill /IM`. Todo commandlet o
  ejecución con `-ExecCmds` termina en `quit`; poner siempre un tiempo máximo.
- Python del editor: `UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<ruta>`; si se
  cuelga con `-nullrhi`, lanzarlo sin `-nullrhi`. Guardar mapas con
  `unreal.EditorLoadingAndSavingUtils.save_map`. Python fuera del editor: `uv run python`.
- Lógica que mueve números: funciones puras sin `UWorld` con Automation Test (caso positivo y
  negativo). Comentarios y commits en español técnico. Conventional commits.
- Cada etapa termina con build y tests en verde y un commit propio.
- Si una decisión no prevista aquí es no trivial, tomar la opción más conservadora, apuntarla en
  `docs/DECISIONES.md` (sección «Pendientes de revisión») con alternativas y motivo, y seguir.

## Decisiones fijadas para esta noche

Todas quedan en `docs/DECISIONES.md` para que Rodrigo las revise; se pueden revertir.

1. **Convención de portal.** El eje X local del portal apunta hacia fuera de la cara visible. Entrar
   por la cara frontal de A sale por la cara frontal de B. La transformación de paso es
   `Transfer = Target · Yaw180 · Source⁻¹` (el README omite el giro de 180°; se corrige en README).
   Los portales no se escalan (la escala va en los componentes).
2. **Cruce solo frontal → trasero**, medido sobre la cámara con el segmento entre la posición del
   fotograma anterior y la actual, dentro de los límites del rectángulo.
3. **Cada portal está empotrado**: marco, superficie y una placa trasera sólida a ~60 cm detrás del
   plano. Los portales de vuelta en los biomas son una pared exenta con puerta (estética liminal).
   Por detrás no se puede atravesar.
4. **Orden de fotograma.** `ALiminalPlayerCameraManager::UpdateCamera` llama a `Super`, después a
   `ULiminalPortalSubsystem`, que (1) comprueba cruces y teletransporta (y recalcula la cámara una vez)
   y (2) coloca y dispara `CaptureScene()` en los portales visibles. `bCaptureEveryFrame = false`.
5. **Captura en HDR lineal** (`SCS_SceneColorHDR`, `RTF_RGBA16f`), superficie unlit con emisivo
   desde `ScreenPosition`, de modo que el tonemapper del viewport se aplica una sola vez.
   Consecuencia: **un único post-process global** (exposición manual fija). El aspecto de cada bioma
   sale de luces, niebla local (`ALocalFogVolume`), cielo y materiales, no de post-process; así no hay
   salto de color al cruzar.
6. **Recursión cortada**: en cada captura se ocultan todas las superficies de portal; cada portal
   tiene un plano de reserva `bVisibleInSceneCaptureOnly` (color liminal plano) que se ve a través de
   otro portal. El reserva del portal de destino también se oculta en su propia captura.
7. **Plano cercano**: la superficie es una caja fina de material two-sided; cuando la cámara está a
   menos de `NearThicknessDistance` del plano por delante, la caja se extiende hacia detrás del plano.
8. **Iluminación (riesgo 1)**: opción 1a para el blockout (sol, `SkyAtmosphere` y `SkyLight`
   compartidos; hub cerrado con `RectLight`). 1b queda como experimento.
9. **Generación procedural en C++ con semilla** (`FRandomStream`) en lugar de grafos PCG: es
   determinista, se testea con funciones puras y se puede generar desde Python sin editor gráfico.
   Terreno con `UProceduralMeshComponent` desde un campo de alturas puro; dispersión con
   `UHierarchicalInstancedStaticMeshComponent` de `/Engine/BasicShapes`. Migrar a PCG queda abierto.
10. **Sin assets externos** esta noche: formas básicas del motor y materiales creados por script.
11. **Mapa sin World Partition**: `/Game/Maps/L_Hub`, biomas desplazados `k × 20 km` en X (k = 1..4).
12. **Entrada**: Enhanced Input con `UInputAction` y `UInputMappingContext` creados en C++ en tiempo de
    ejecución (no hacen falta assets).

## Estructura de código prevista

```
Source/LiminalPortals/
  Portal/LiminalPortalMath.h/.cpp        funciones puras (namespace LiminalPortalMath)
  Portal/LiminalPortal.h/.cpp            ALiminalPortal
  Portal/LiminalPortalSubsystem.h/.cpp   ULiminalPortalSubsystem (UWorldSubsystem)
  Player/LiminalPlayerCameraManager.*    orden de fotograma
  Player/LiminalCharacter.*              primera persona, Enhanced Input en C++
  Player/LiminalPlayerController.*, LiminalGameMode.*
  World/LiminalHub.*                     generador del hub (OnConstruction)
  World/LiminalBiome.*                   generador de bioma por tipo y semilla
  World/LiminalTerrainMath.*, LiminalScatterMath.*   funciones puras
  Debug/LiminalDebugCommands.*           Liminal.Tour, Liminal.Bench, Liminal.SelfTest, Liminal.Calibrate
  LiminalPortalsSettings.*               UDeveloperSettings con rutas de materiales (DefaultGame.ini)
  Tests/*.cpp
Tools/Content/*.py                       creación de materiales y mapa
Tools/content.ps1, Tools/verify.ps1      envoltorios
```

Funciones puras mínimas (`LiminalPortalMath`): transformación de paso (punto, dirección/velocidad,
rotación, transform completo), detección de cruce con límites, plano de recorte del destino,
visibilidad aproximada del portal (delante de la cámara y dentro del cono de visión), grosor de la
superficie según la distancia al plano, tamaño del render target según el viewport.

## Etapas y criterios de aceptación

### E1 — Núcleo (fase 1, código)

- Módulos añadidos a `Build.cs` (`ProceduralMeshComponent`, `DeveloperSettings` si hace falta) y
  plugins en `.uproject` (`PythonScriptPlugin`, `EditorScriptingUtilities`, `ProceduralMeshComponent`).
- `Config/DefaultEngine.ini`: `r.AllowGlobalClipPlane=True`, `r.DefaultFeature.MotionBlur=False`,
  `r.DefaultFeature.AutoExposure=False`, Lumen GI y reflejos, mapa por defecto y GameMode.
- Todas las funciones puras con tests (≥ 2 casos por función, uno negativo); ida y vuelta A→B→A da la
  identidad; la velocidad se conserva en módulo y se gira con el portal.
- Actores y subsistema de la sección «Estructura» compilando.
- **Aceptación**: build y tests en verde; número de tests ≥ 15.

### E2 — Contenido y calibración (fase 1, ejecución)

- `Tools/Content/build_content.py` (idempotente) crea `M_PortalSurface`, `M_PortalFallback`,
  `M_Basic` (parámetros `Color`, `Roughness`, `Emissive`) y el mapa de calibración
  `/Game/Maps/L_Calibration`: sala abierta con geometría de colores variados, un portal A y su
  pareja B **en la misma posición mirando al lado contrario**, con lo que la transformación de paso es
  la identidad.
- `Liminal.Calibrate`: gira la cámara 10° por fotograma delante del portal y guarda capturas en
  `Saved/Calibration/` con la superficie visible y, en una segunda pasada con la misma secuencia,
  oculta. `Tools/verify.ps1 -Calibrate` las compara (PIL con `uv run`) dentro de la máscara del portal.
- Control negativo: `Liminal.Portal.LagFrames 1` reutiliza la pose del fotograma anterior; la
  diferencia tiene que ser claramente mayor.
- **Aceptación**: diferencia media en la máscara < 2 % (0-255) sin retraso y > 3× ese valor con
  `LagFrames 1`. Si TSR impide llegar, medir con FXAA/TAA y documentar la elección en `DECISIONES.md`.

### E3 — Hub, cruce y biomas en blockout (fases 2 y 3)

- `ALiminalHub`: pasillo o sala liminal (paredes claras, moqueta, techo con `RectLight`), cuatro
  huecos de puerta con su transform expuesto (`GetDoorTransform(int32)`).
- `ALiminalBiome`: tipo (Jungle, Desert, Antarctic, Farmland), semilla, suelo de blockout y la pared
  exenta con la puerta de vuelta (`GetReturnDoorTransform()`).
- `build_content.py` crea `/Game/Maps/L_Hub` con hub, 4 biomas a `k × 20 km`, 8 portales enlazados,
  sol, cielo, `SkyLight` y post-process global.
- `Liminal.SelfTest`: recorre el cruce hub → bioma → hub 20 veces por cada puerta moviendo el pawn con
  input simulado; comprueba en cada cruce que hubo teletransporte, que el módulo de la velocidad se
  conserva (tolerancia 1 %) y que la cámara nunca queda detrás de la placa trasera. Escribe
  `Saved/SelfTest.json`. `Tools/verify.ps1 -SelfTest` falla si algo no pasa.
- **Aceptación**: SelfTest en verde para las 4 puertas; capturas del hub y de cada puerta en
  `Saved/Tour/`.

### E4 — Biomas procedurales (fase 4)

- Campo de alturas puro por bioma (ruido fractal determinista sobre semilla): dunas en desierto,
  ondulación suave en Antártida, llano con surcos en cultivos, relieve irregular en selva.
- Dispersión pura y determinista (Poisson o rejilla con jitter, exclusión cerca de la puerta y del
  camino de salida): selva (árboles cilindro+esfera, arbustos, niebla baja con `ALocalFogVolume`),
  desierto (rocas erosionadas: esferas y cubos aplastados), Antártida (bloques de hielo, cielo
  cubierto aproximado con niebla y luz difusa), cultivos (parcelas, cercas, caminos).
- Tests: misma semilla ⇒ mismo resultado (hash), semilla distinta ⇒ distinto, ninguna instancia dentro
  del radio de exclusión, alturas acotadas.
- **Aceptación**: tests en verde; `Liminal.Tour` genera una lámina `Saved/Tour/lamina.png` con los 4
  biomas vistos a través de su puerta y desde dentro.

### E5 — Medidas (fase 5 parcial)

- `Liminal.Bench`: tiempo de GPU del fotograma con 0, 1, 2 y 4 portales visibles (posiciones fijas),
  media de ≥ 120 fotogramas tras calentar; escribe `Saved/Bench.json`. Coste por portal anotado en el
  README (1080p si la ventana lo permite; si no, la resolución real medida).

### E6 — Revisión

- Revisión independiente (corrección, ciclo de vida de UE y render, tests) con verificación
  adversarial de cada hallazgo; arreglar CRITICAL y HIGH.

### E7 — Documentación

- README al día (convención 180°, decisiones, coste medido, cómo ejecutar verify), `DECISIONES.md`
  completo y kanban actualizado.
