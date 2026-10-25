# LiminalPortals

Banco de pruebas de rendering en Unreal Engine 5.6 (C++): portales que no cortan la imagen al
cruzarlos. No es un juego. Un *hub* liminal con varias puertas fijas; cada puerta muestra en
tiempo real un bioma generado proceduralmente y se puede atravesar sin corte visible.

Las decisiones tomadas durante la implementación (y las doce de partida de `docs/PLAN-NOCHE.md`)
están en `docs/DECISIONES.md`, con un índice al principio que marca las que conviene revisar antes.

## Estado

Cifras de la última verificación completa (rama `feat/portales-fases-1-4`, tras la revisión E6;
ficheros en `Saved/`). El detalle de cada medida está en la sección «Medidas».

| Fase | Estado | Evidencia |
|---|---|---|
| 1. Portal fijo hub → caja desplazada | Hecha | Calibración: 0,79 % de diferencia en la máscara sin retraso frente a 16,6 % con un fotograma de retraso (ratio 20,9; aceptación < 2 % y ratio > 3); con el arte low-poly, 0,85 % (ratio 19,6). 58 Automation Tests en verde |
| 2. Cruce en los dos sentidos | Hecha | `Liminal.SelfTest`: 80 cruces hub → bioma y 80 bioma → hub correctos; error máximo del módulo de la velocidad 1,4e-14 %; 0 de 9141 fotogramas con la cámara detrás de un portal o dentro de geometría. El parpadeo se comprueba por la posición de la cámara, no por imagen |
| 3. Hub con 4 puertas y biomas en *blockout* | Hecha | 4 puertas y 8 portales enlazados en `L_Hub`; `Liminal.Tour` (13 capturas) pasa `check_tour.py`; coste por portal anotado (con `FRHIGPUFrameTimeHistory` en lugar de `stat gpu`, D-29) |
| 4. Biomas procedurales | Hecha, con desviación | Cada bioma sale de su semilla con generación en C++ (no PCG) y el arte low-poly de `/Game/Art` (packs CC0 recoloreados y kits propios, D-32 a D-35); tests de determinismo, variantes y exclusión; lámina `Saved/Tour/lamina.png` |
| 5. Pulido y medidas | Parcial | Coste por portal medido: 4,40-4,46 ms de GPU a 1080p, por encima del objetivo provisional de 4 ms. Experimentos de iluminación 1a/1b pendientes: 1a está en uso sin medida comparativa y 1b no se ha probado |

## Objetivo

- Hub: sala o pasillo liminal (iluminación plana, paredes limpias, silencio) con una puerta por bioma.
- Cada puerta es una ventana al bioma: se ve en directo, con paralaje correcto, desde cualquier ángulo.
- Atravesar la puerta no produce corte ni salto de imagen. Cada bioma tiene una puerta de vuelta al hub.
- Biomas generados por código con semilla fija (el diseño original pedía PCG; ver «Biomas procedurales»):
  1. Bosque / selva: vegetación densa, sotobosque, niebla baja.
  2. Desierto: dunas, rocas erosionadas, sol duro.
  3. Antártida: hielo, nieve, cielo cubierto, luz difusa.
  4. Campos de cultivo: parcelas, cercas, caminos, horizonte abierto.
- Los portales los coloca el diseño (son actores fijos en el nivel). El jugador no crea portales.

## Arquitectura

### Mundos desplazados

Hub y biomas son actores del mismo mapa, `/Game/Maps/L_Hub`, sin World Partition ni *Level
Instances*: `ALiminalHub` en el origen y cada `ALiminalBiome` desplazado `k × 20 km` en X (k = 1..4:
selva, desierto, Antártida y cultivos). Con Large World Coordinates el desplazamiento no tiene problemas
de precisión. La física, el audio y la geometría quedan separados porque ocupan sitios distintos.

### Convención del portal y giro de 180°

- El eje X local de `ALiminalPortal` apunta hacia fuera de la cara visible. El origen está en el centro
  inferior del hueco, a ras de suelo (D-01), de modo que el paso conserva el suelo.
- Entrar por la cara frontal de A sale por la cara frontal de B. La transformación de paso es
  `Transfer = T_B · Yaw180 · T_A⁻¹`: el giro de 180° alrededor de Z convierte «entrar de frente en A»
  en «salir de frente en B». En coordenadas locales, (x, y, z) en A pasa a (−x, −y, z) en B. El diseño
  original omitía este giro.
- Los portales no se escalan; la escala va en los componentes.
- Se cruza solo de la cara frontal a la trasera. Cada portal está empotrado: marco en túnel y placa
  trasera sólida a 60 cm del plano (D-04), así que por detrás no se puede atravesar.

### Portal (`ALiminalPortal`) y subsistema (`ULiminalPortalSubsystem`)

- Malla del marco, superficie, placa trasera, plano de reserva y referencia al portal emparejado
  (`LinkedPortal`).
- `USceneCaptureComponent2D` con `bCaptureEveryFrame = false`, `bAlwaysPersistRenderingState = true` y
  captura en HDR lineal (`SCS_SceneColorHDR`, render target `RTF_RGBA16f` del tamaño del viewport por
  `RenderTargetScale`). La superficie es unlit y emite el render target en coordenadas de pantalla
  (`ScreenPosition`), de modo que el tonemapper del viewport se aplica una sola vez.
- Orden de fotograma: `ALiminalPlayerCameraManager::UpdateCamera` llama a `Super` y después a
  `ULiminalPortalSubsystem::UpdatePortals`, que (1) detecta cruces, teletransporta el pawn y recalcula la
  cámara (D-05) y (2) coloca la cámara de cada portal visible en `Transfer · T_camera`, con el FOV del
  jugador, y llama a `CaptureScene()`. Así no hay fotograma de retraso.
- Clip plane en el plano del portal de destino, 1 cm por detrás (`r.AllowGlobalClipPlane=True`). El
  sentido se comprobó en ejecución (D-11).
- Visibilidad aproximada por esfera envolvente y cono del frustum, sin oclusión (D-08).
- Lumen en las capturas: UE 5.6 lo apaga en las `SceneCapture` salvo que la captura lo sobrescriba;
  cada captura copia el método de GI y de reflejos del proyecto y fija `LumenSurfaceCacheResolution = 1`
  (D-10).
- Recursión cortada: cada captura oculta todas las superficies de portal; a través de un portal se ve
  el plano de reserva (`bVisibleInSceneCaptureOnly`, color liminal plano) del portal que haya detrás.
- Plano cercano: la superficie es una caja fina que se extiende hacia detrás del plano (de 1 a 40 cm)
  cuando la cámara está a menos de 60 cm por delante.
- Estado de render: los portales que llevan 10 s sin capturarse y están a 100 m o más de la cámara
  liberan su `FSceneViewState` (y su escena de Lumen); la siguiente captura lo recrea (D-31).

### Cruce

- Se detecta sobre la cámara, no sobre la cápsula: el segmento entre la posición del fotograma
  anterior y la actual corta el plano de frente a atrás dentro del rectángulo del hueco. Saltos de más
  de 10 m por fotograma no cuentan (D-06).
- Al cruzar se aplica `Transfer` a la posición, la rotación de control y la velocidad del pawn; el
  módulo de la velocidad se conserva y la dirección gira con el portal. Solo se teletransporta un
  `ALiminalCharacter` (D-07). No se marca corte de cámara (D-02).

### Iluminación y post-process

- Opción 1a del riesgo 1: un sol, `SkyAtmosphere` y `SkyLight` compartidos por todo el mapa; el hub es
  una sala cerrada iluminada con `RectLight`.
- Un único post-process global con exposición manual fija. Como la captura es HDR lineal y se tonemapea
  una sola vez en el viewport, el aspecto de cada bioma sale de la niebla local (`ULocalFogVolumeComponent`),
  el terreno y los materiales, no de un post-process por bioma; así no hay salto de color al cruzar.

### Lógica pura con tests

La matemática que mueve números va en funciones puras sin `UWorld`, con su Automation Test (caso
positivo y negativo): paso entre portales (punto, dirección, rotación, transform), detección de cruce
con límites, clip plane del destino, visibilidad, grosor de la superficie, tamaño del render target,
liberación del estado de render, geometría del hub y de los biomas, terreno, dispersión, comprobaciones
del SelfTest, secuencia de la calibración y estadísticos del Bench.

### Ajustes del proyecto

- `r.DefaultFeature.MotionBlur=False` y `r.DefaultFeature.AutoExposure=False`.
- Lumen GI y reflejos, D3D12 con SM6 y `r.GenerateMeshDistanceFields=True` (D-08).
- Antialiasing TSR (D-12): las capturas `SCS_SceneColorHDR` no pasan por post-proceso y la vista
  principal aplica TSR una vez sobre la superficie.
- `GameDefaultMap` y `EditorStartupMap`: `/Game/Maps/L_Hub` (D-21).

## Estructura del código

```text
Source/LiminalPortals/
  LiminalPortals.*                       módulo y categoría de log LogLiminalPortals
  LiminalPortalsSettings.*               ULiminalPortalsSettings (DefaultGame.ini): materiales, escala y tamaño máximo del render target
  Portal/LiminalPortalMath.*             funciones puras del portal (namespace LiminalPortalMath)
  Portal/LiminalPortal.*                 ALiminalPortal: marco, superficie, reserva, placa y captura
  Portal/LiminalPortalSubsystem.*        ULiminalPortalSubsystem (UWorldSubsystem): cruces, capturas y CVars Liminal.Portal*
  Player/LiminalPlayerCameraManager.*    orden de fotograma (UpdateCamera → subsistema)
  Player/LiminalCharacter.*              primera persona con Enhanced Input creado en C++
  Player/LiminalPlayerController.*, LiminalGameMode.*
  World/LiminalWorldLayout.*             geometría pura del hub y de los biomas en blockout
  World/LiminalBlockout.*                cajas con ISM compartidas por hub y biomas
  World/LiminalHub.*                     ALiminalHub (OnConstruction)
  World/LiminalBiome.*, LiminalBiomeTypes.h   ALiminalBiome: terreno, dispersión y niebla por tipo y semilla
  World/LiminalTerrainMath.*             campo de alturas, color y malla del terreno (puras)
  World/LiminalScatterMath.*             dispersión determinista con variantes y exclusiones (pura)
  World/LiminalArtCatalog.*              catálogo del arte: mallas por categoría, piezas del hub y paredes de vuelta
  World/LiminalHubDressing.*             atrezo del hub con el kit low-poly (puro)
  Debug/LiminalDebugCommands.*           registro de las órdenes Liminal.*
  Debug/LiminalCalibration*.*            Liminal.Calibrate y su matemática pura
  Debug/LiminalSelfTest*.*               Liminal.SelfTest y sus comprobaciones puras
  Debug/LiminalTour.*                    Liminal.Tour
  Debug/LiminalBench*.*                  Liminal.Bench, estadísticos y poses fijas
  Debug/LiminalBenchWalk.*, LiminalBenchCrossing.*, LiminalGpuProfile.*   modo perfil de Liminal.Bench: recorrido, cruces y desglose de GPU por pase (D-36)
  Debug/LiminalDebugWorld.*, LiminalDebugShared.h   utilidades comunes de las ejecuciones de depuración
  Tests/*.cpp                            Automation Tests (61)
Tools/
  build.ps1, test.ps1, content.ps1, verify.ps1, common.ps1
  Content/build_content.py, content_utils.py   materiales y mapas creados por script
  Content/import_art.py                  importación del arte de Art/Export a /Game/Art
  Verify/compare_calibration.py, check_tour.py, make_lamina.py, summarize_profile.py (+ tests de Python)
```

## Controles

| Acción | Teclado y ratón | Mando |
|---|---|---|
| Moverse | W A S D | stick izquierdo |
| Mirar | ratón | stick derecho |
| Saltar | barra espaciadora | botón inferior |
| Correr (de 400 a 700 cm/s mientras se mantiene) | Mayús izquierda | pulsar el stick izquierdo |

`UInputAction` y `UInputMappingContext` se crean en C++ en tiempo de ejecución (no hay assets de entrada).

## Compilar, generar contenido y verificar

Con el editor abierto, la compilación por línea de comandos falla por Live Coding: cerrarlo antes. La
primera vez, clic derecho en `LiminalPortals.uproject` → *Generate Visual Studio project files*.

```powershell
powershell -File Tools/build.ps1                  # editor (Development), unity build completo (D-22)
powershell -File Tools/build.ps1 -Target Game     # juego
powershell -File Tools/test.ps1 [-Filter <prefijo>]   # Automation Tests sin interfaz; falla si no se ejecuta ninguno
```

### Regenerar contenido

```powershell
powershell -File Tools/content.ps1 [-Mode All|Art|Content]
```

`-Mode Art` lanza `Tools/Content/import_art.py`: importa los FBX de `Art/Export/<Kit>/` (no versionados; se
regeneran con `Tools/Art`) en `/Game/Art/<Kit>/` con `M_LowPoly`, comprueba el bounding box contra los informes
(±2 %) y solo reimporta los que cambian (`Saved/Content/import_art.json`). `-Mode Content` lanza
`UnrealEditor-Cmd -run=pythonscript` con `Tools/Content/build_content.py` (idempotente) y
comprueba `Saved/Content/build_content.json`; `All` (por defecto) hace los dos. Crea los materiales (`M_PortalSurface`, `M_PortalFallback`,
`M_Basic`, `M_Terrain` y sus instancias), `/Game/Maps/L_Calibration` y `/Game/Maps/L_Hub` (hub, cuatro
biomas, ocho portales enlazados, sol, cielo, `SkyLight` y post-process global). Hay que cerrar el editor
antes (bloquea los paquetes). Cuándo regenerar:

- cambios en el layout del hub o de la pared de vuelta (`LiminalWorldLayout`): las cajas del blockout se
  guardan en el mapa y en `-game` no se vuelve a ejecutar `OnConstruction` (D-17);
- cambios en `build_content.py` (sol, cielo, materiales, colocación).

El terreno y la dispersión no se guardan en el mapa: se regeneran desde la semilla en `BeginPlay`
(D-23), así que cambiar sus recetas en C++ no exige regenerar.

### Verificaciones con render real (`Tools/verify.ps1`)

No son Automation Tests: lanzan el juego con RHI, ejecutan una orden `Liminal.*` y comprueban el informe.
Fallan si el juego sale con un código distinto de 0 o si el informe no cumple; no dan por buenos
informes de ejecuciones anteriores.

| Modo | Qué hace | Qué mide y cuándo falla |
|---|---|---|
| `-Calibrate` | `L_Calibration` a 1280 × 720, dos ejecuciones de `Liminal.Calibrate` con `Liminal.Portal.LagFrames 0` y `1` | Diferencia media en RGB dentro de la máscara del portal entre la pasada con superficie visible y la oculta; falla si sin retraso no es < 2 % o si el control no es > 3 veces mayor. `Saved/Calibration/result.json` |
| `-Calibrate -ClipPlaneCheck` | Añade una ejecución con `Liminal.Portal.FlipClipPlane 1` | Informativa: con la normal invertida la diferencia tiene que dispararse |
| `-SelfTest` | `L_Hub` con paso fijo de 1/60 s y `Liminal.SelfTest` (20 ciclos por puerta con entrada inyectada en Enhanced Input) | Por cruce: portales esperados, salto, módulo (1 %) y dirección (0,5°) de la velocidad, posición (1 cm); por fotograma: cámara fuera del volumen de detrás de cada portal y sin solapar geometría. `Saved/SelfTest.json` |
| `-SelfTest -SelfTestNegative` | Añade el control negativo `Liminal.Portal.DebugVelocityScale 0.97` | La prueba tiene que fallar en las cuatro puertas |
| `-Tour` | `L_Hub` con `Liminal.Tour`: 13 capturas (hub y, por puerta, desde el hub, desde el bioma y desde dentro) en `Saved/Tour/` | `check_tour.py`: luminancia media entre 15 y 245, desviación > 6, hueco del portal distinto del marco (> 20) y con detalle (> 4), pared de vuelta iluminada (≥ 150) y primer plano no negro (≤ 25 % de píxeles < 20). `Saved/Tour/result.json` y `lamina.png` |
| `-Bench` | `L_Hub` a 1920 × 1080 en ventana, `r.VSync 0`, `t.MaxFPS 0`, dos ejecuciones de `Liminal.Bench` | Tiempo de GPU, render thread y game thread en cinco poses fijas (0, 1, 2, 4 portales y 4 apagados), 120 fotogramas de calentamiento y 240 medidos; falla si algún caso no tiene las capturas esperadas y avisa si el coste por portal difiere un 25 % o más. `Saved/Bench_run{1,2}.json`, `Saved/BenchResult.json` |
| `-Bench -Profile` | Lo mismo con `Liminal.Bench … profile` (D-36) y, tras los casos, un recorrido de 216 poses por el hub | Desglose de GPU por pase y por cola de cada caso, con la vista principal separada de cada captura (`Tools/Verify/summarize_profile.py` lo agrupa en Lumen, sombras, base pass, TSR…), CPU (game, render y RHI thread) y, en el recorrido, capturas por fotograma, superficies dibujadas en la vista principal y área en pantalla de cada portal capturado. `Saved/BenchProfile_run<N>.json`, `Saved/BenchProfileSummary.json` |

Parámetros comunes: `-ExecCmds '...'` (órdenes de consola previas), `-ResX`/`-ResY` (1280 × 720),
`-TimeoutMinutes` (45); de `-Bench`: `-BenchResX`/`-BenchResY`, `-BenchFrames`, `-BenchWarmup` y
`-BenchMaxRelativeDiff` y `-BenchRuns` (2; con 1 no se comprueba la estabilidad).

### CVars de experimento y diagnóstico

| CVar | Uso |
|---|---|
| `Liminal.Portals.Enable` | Apaga todas las capturas (caso de referencia del Bench) |
| `Liminal.Portal.LagFrames` | Captura con la pose de N fotogramas antes (control negativo de la calibración) |
| `Liminal.Portal.FlipClipPlane` | Invierte la normal del clip plane (comprobación de D-11) |
| `Liminal.Portal.DebugVelocityScale` | Escala la velocidad tras cada paso (control negativo del SelfTest) |
| `Liminal.Portal.CaptureScale` | Escala del render target (0 = la de `ULiminalPortalsSettings::RenderTargetScale`) |
| `Liminal.Portal.CaptureReflectionMethod` | Método de reflejos de las capturas (−1 = el del proyecto) |
| `Liminal.Portal.ReleaseIdleSeconds`, `Liminal.Portal.ReleaseDistance` | Liberación del estado de render (10 s, 100 m; 0 s la desactiva) |
| `Liminal.Portal.CaptureGIMethod` | Iluminación global de las capturas (−1 = la del proyecto, 0 = ninguna, 2 = en pantalla; D-36) |
| `Liminal.Portal.CaptureShowFlags` | Show flags de las capturas, `Nombre=0+Nombre=1` (con `+`: `-ExecCmds` parte por comas; D-36) |
| `Liminal.Scatter.CullScale` | Escala de las distancias de corte de la dispersión (1; las categorías sin corte no cambian; D-36) |

## Medidas

### Calibración del portal (fase 1)

`/Game/Maps/L_Calibration` tiene un portal A y su pareja B en el mismo sitio mirando a lados
opuestos, así que el paso es la identidad y lo que se ve por el portal debe coincidir con lo que se
vería sin él. `Liminal.Calibrate` gira la cámara 10° por fotograma delante del portal en dos pasadas
idénticas (superficie visible y oculta) y guarda las capturas y la máscara del portal;
`Tools/Verify/compare_calibration.py` compara dentro de la máscara (D-15).

| Medida (1280×720, TSR, Lumen) | Diferencia media en la máscara |
|---|---|
| `LagFrames 0` (orden de fotograma correcto) | 0,79 % tras E6 (0,75-0,81 % en E2) |
| `LagFrames 1` (control: pose del fotograma anterior) | 16,6 % (ratio 20,9) |
| Clip plane invertido (`-ClipPlaneCheck`, E2) | 47,4 % |

Aceptación: < 2 % sin retraso y > 3 veces ese valor con retraso. FXAA y TAA dan lo mismo que TSR;
el residuo es de Lumen (con Lumen apagado, 0,26 %).

### Hub y cruce (fases 2 y 3)

`ALiminalHub` es un pasillo cerrado de 16 × 6 × 3,2 m con moqueta, paneles fluorescentes con `RectLight`
y cuatro puertas de 140 × 230 cm al tresbolillo (D-16). La geometría sale de funciones puras
(`World/LiminalWorldLayout`).

| Medida (1280×720, paso fijo 1/60 s) | Resultado |
|---|---|
| Cruces por sentido (4 puertas × 20) | 80 hub → bioma y 80 bioma → hub, todos correctos |
| Error máximo del módulo de la velocidad | 1,4e-14 % (tolerancia 1 %) |
| Error máximo de dirección / posición | 1,2e-6° / 2e-9 cm |
| Fotogramas con la cámara detrás de un portal o dentro de geometría | 0 de 9141 |
| Control negativo (velocidad al 97 % tras el paso) | falla en las 4 puertas (3 %) |

### Biomas procedurales (fase 4)

Cada `ALiminalBiome` genera desde su semilla (`FRandomStream` → `LiminalTerrainMath::DeriveSeeds`) un
terreno facetado de 400 × 400 m a 2,5 m (`UProceduralMeshComponent` con colisión, normal y color por cara) y
la dispersión de las mallas low-poly de `/Game/Art` (un HISM por categoría y variante; catálogo en
`World/LiminalArtCatalog.cpp`), más niebla local en la selva y la Antártida. Son componentes transitorios: se regeneran en `OnConstruction` y en `BeginPlay`, no
se guardan en el mapa (D-23). La puerta de vuelta y 30 m de camino de salida quedan llanos a ras del
portal y sin dispersión. Detalle de parámetros en `docs/DECISIONES.md`, D-23 a D-28.

| Bioma (semilla) | Relieve | Dispersión | Instancias (triángulos) | Desde la puerta (D-34) |
|---|---|---|---|---|
| Selva (1001) | irregular, −5,5/+6,5 m | árboles altos, arbustos, helechos y flores (más densos en el borde del claro), troncos caídos; niebla baja | 8977 (5,76 M) | 2361 (2,94 M) |
| Desierto (1002) | dunas orientadas, hasta 8,1 m | tres mesetas y un arco como hitos, corros de rocas, cactus, matorral, guijarros | 1961 (0,63 M) | 807 (0,30 M) |
| Antártida (1003) | ondulación ±2,9 m y sastrugi | icebergs y seracs en el horizonte, crestas de presión, bloques y esquirlas, montículos, afloramientos oscuros; neblina | 966 (0,06 M) | 448 (0,03 M) |
| Cultivos (1004) | llano ±0,9 m con caballones | trigo, maíz y filas verdes cerca de la puerta, cercas, balas, espantapájaros, granero, árboles sueltos | 4889 (1,28 M) | 3666 (1,09 M) |

| Medida (1280×720, `Liminal.Tour` tras E6) | Resultado |
|---|---|
| Generación de los cuatro biomas en `BeginPlay` | 0,36 s (E4) |
| Luminancia media de las 13 capturas (0-255) | 82-219; recuadro central 139-226 (umbral 15-245) |
| Hueco frente al marco en las vistas de puerta (distancia RGB media) | 105-213 (umbral 20) |
| Pared de vuelta vista desde el bioma (luminancia) | 215-223 (umbral 150, D-24) |
| Primer plano oscuro en las vistas desde dentro | 0 % de píxeles < 20 (máximo 25 %, D-28) |

### Coste por portal (fase 5 parcial)

`Liminal.Bench` coloca la cámara en cinco poses fijas de `L_Hub` (0, 1, 2 y 4 portales visibles y la vista
del caso de 4 con `Liminal.Portals.Enable 0`), comprueba en cada fotograma medido que el subsistema hace las
capturas esperadas, calienta 120 fotogramas y mide 240 por caso: tiempo de GPU del fotograma
(`FRHIGPUFrameTimeHistory`, la serie de `RHIGetGPUFrameCycles`), render thread y game thread (D-29).

Equipo: NVIDIA GeForce RTX 4060 Laptop GPU, D3D12 (SM6), ventana de 1920 × 1080 (pantalla de 2560 × 1440),
`r.VSync 0`, `t.MaxFPS 0`, TSR, Lumen GI y reflejos. Render target de las capturas: 1920 × 1080; la vista
principal se renderiza al 73 % y TSR la escala.

| Caso (ejecución 2, tras E6) | Capturas por fotograma | GPU media / p50 / p95 (ms) | Render thread (ms) | Game thread (ms) |
|---|---|---|---|---|
| 0 portales | 0 | 10,89 / 10,62 / 12,50 | 11,32 | 1,42 |
| 1 portal | 1 | 16,16 / 16,18 / 17,87 | 17,30 | 1,43 |
| 2 portales | 2 | 24,52 / 24,40 / 26,58 | 26,14 | 1,48 |
| 4 portales | 4 | 28,69 / 28,79 / 30,46 | 31,59 | 1,60 |
| Vista de 4 con portales apagados | 0 | 10,86 / 10,57 / 12,54 | 11,42 | 1,33 |

| Coste por portal a 1080p | Ejecución 1 | Ejecución 2 |
|---|---|---|
| GPU, (4 portales − apagados) / 4 | 4,40 ms | 4,46 ms (diferencia 1,4 %) |
| GPU, pendiente sobre 0, 1, 2 y 4 | 4,40 ms | 4,51 ms (diferencia 2,4 %) |
| Game thread, (4 − apagados) / 4 | 0,05 ms | 0,07 ms |

En las cuatro ejecuciones de E5 el coste emparejado fue de 4,29-4,43 ms (D-30). Depende de lo que muestra
cada portal: frente a la vista de 0 portales, el caso de 1 sale a 5,1-5,3 ms y el de 2 (selva y Antártida) a
6,6-6,8 ms por portal, pero esas vistas también cambian lo que se ve del hub; el emparejado con los portales
apagados es la cifra de referencia. El render thread coincide con el tiempo de fotograma porque la GPU es el
cuello de botella (incluye la espera).

Palancas medidas en E5 y no activadas (D-30): `Liminal.Portal.CaptureScale 0.73` (render target a la
resolución interna de la vista principal, 1402 × 788) baja el coste a 3,15-3,22 ms con la calibración en
0,81 % a 1080p; los reflejos en pantalla en las capturas (`CaptureReflectionMethod 2`) no ahorran nada
medible (4,18-4,37 ms).

### Desglose por pase y palancas (D-36)

`verify.ps1 -Bench -Profile` (método en D-36). Desglose aditivo con `r.RDG.AsyncCompute 0`, ms de GPU por
fotograma; captura = media de las cuatro del caso de 4 (la de la selva, sola, llega a 7,6 ms).

| Pase | Vista principal (0 portales) | Una captura |
|---|---|---|
| Lumen: screen probe gather / iluminación de la escena / reflejos / volumen de translucidez | 1,81 / 1,49 / 0,08 / 0,25 | 1,34 / 0,18 / 0,19 / 0,37 |
| Sombras (VSM: asignación de páginas, profundidad, proyección) | 2,24 | 1,41 |
| Luces diferidas | 0,95 | 0,23 |
| Base pass + prepass | 0,33 | 0,38 |
| Nanite / instancias, campos de distancia | 0 / 0 | 0 / 0,09 |
| Niebla, cielo | 0 / 0,07 | 0,07 / 0,14 |
| TSR + post-process | 3,02 + 0,29 | — |
| Resto | 0,51 | 0,53 |
| **Total** | **11,2** (GPU 11,3) | **5,1** |

La captura no paga TSR ni post-process (3,3 ms), pero renderiza al 100 % (1920 × 1080 frente a los 1400 × 788
de la principal) y repite lo que no depende de los píxeles: con `CaptureScale` 1 / 0,73 / 0,5 el coste se ajusta
a ~55 % fijo + ~45 % proporcional a los píxeles.

| Palanca (no activada) | Coste por portal | Vista de 0 portales | Calibración (< 2 %) |
|---|---|---|---|
| Capturas sin Lumen GI (`CaptureGIMethod 0`, reflejos en pantalla) | −7 % | igual | **8,6 %, falla** |
| Todo sin Lumen (GI 0, reflejos en pantalla, DFAO) | −19 % | −23 % | 0,26 % (sin la GI de Lumen) |
| Sombras en cascada (`r.Shadow.Virtual.Enable 0`) | −19 % | −12 % | 0,82 % |
| `CaptureScale 0.73` / `0.5` | −19 % / −33 % | igual | 0,73 % / 0,93 % (la métrica no ve el emborronado, D-30) |
| Show flags baratos en las capturas (AO, sombras de contacto, niebla volumétrica, rebotes de Lumen…) | sin efecto medible | igual | 1,99 % (al límite) |
| `Liminal.Scatter.CullScale 0.5` / sin dispersión en las capturas | 0 % / −4 % (ruido) | igual | — |

Recorrido de 216 poses por el hub: 1,48 capturas por fotograma (17 % de poses sin ninguna, 15 % con 3 o 4); solo
1,15 portales están dentro del frustum y 1,14 tienen la superficie dibujada. El rectángulo de pantalla de un portal
visible ocupa de media el 8,0 % del viewport (mediana 2,3 %; 9,2 % sumando todos los de un fotograma).

Cruces (dos pasadas por las cuatro puertas, 12 s de espera antes de cada una): el primer cruce del proceso tarda
~640 ms en un fotograma; los demás, 51-61 ms frente a una mediana de 17-20 ms (×2,9-3,5), un solo fotograma, en
el render thread; la GPU no pasa de 27 ms. Es Lumen: con `r.DynamicGlobalIlluminationMethod 0` y reflejos en
pantalla no hay pico (máximo ×1,1-1,5, también en el primer cruce); con sombras en cascada empeora (76-100 ms).

CPU: a resolución normal manda la GPU (fotograma = GPU + 0,5-2,5 ms; game thread 1,3-2,5 ms). Con la GPU casi
descargada (`r.ScreenPercentage 26`, `CaptureScale 0.1`) el fotograma pasa a ser del render thread: 10,3 / 15,2 /
19,8 / 32,7 ms con 0 / 1 / 2 / 4 capturas frente a 8,4 / 10,7 / 16,5 / 21,3 ms de GPU (~5 ms de CPU por captura,
con el portátil limitado por temperatura); el RHI thread sube de 6 a 15 ms.

## Riesgos abiertos

1. **Coste por portal por encima del objetivo**: ~4,4 ms de GPU por portal a 1080p frente a los 4 ms
   provisionales; con cuatro portales en pantalla el fotograma pasa de 10,9 a 28,7 ms. La palanca
   `CaptureScale 0.73` cumple (3,15-3,22 ms), pero la calibración no sirve para juzgarla: con menos
   resolución la diferencia baja porque el reescalado promedia el ruido de Lumen, y la sala de calibración
   apenas tiene bordes. **Pendiente de validar visualmente** con una captura antes de activarla; además el
   valor 0,73 solo vale para 1080p (a 720p la vista principal va al 100 %), así que lo correcto sería seguir
   la fracción de resolución de la vista principal en cada fotograma.
2. **Iluminación distinta por bioma** (fase 5, sin hacer): el sol y la `SkyAtmosphere` son casi globales.
   En uso, la opción a) un sol compartido, con niebla y materiales por bioma; la SkyLight de `L_Hub` está al
   doble para que la selva no quede negra (D-28) y la Antártida tiene sombras duras con el sol compartido.
   Faltan por medir b) una luz direccional por bioma separada con *lighting channels* (comprobar con Lumen)
   y c) renderizar los biomas desde un `UWorld` separado (lo más fiel y lo más caro de implementar).
3. **El terreno no existe para Lumen fuera de pantalla**: `UProceduralMeshComponent` no genera mesh distance
   field ni cards de Lumen en UE 5.6 (sin rebote difuso del suelo, sin oclusión de cielo ni reflejos). Pasar
   a `UStaticMesh`, Landscape o un proxy oculto queda abierto (D-23).
4. **Memoria de Lumen en las capturas**: cada portal con estado persistente tiene su propia escena de Lumen
   con el atlas completo; D-31 libera los lejanos, pero la memoria no se ha medido (`rhi.DumpResourceMemory`)
   y al volver a ver un portal liberado su luz indirecta tarda unos fotogramas en converger.
5. **Lumen y la superficie del portal**: las trazas de pantalla de Lumen ven la imagen del destino en la
   superficie y aclaran el suelo de delante del portal (D-14); el residuo de la calibración (0,79 %) también
   es de Lumen.
6. **Recursión**: cortada a cero niveles; a través de un portal se ve el plano de reserva del portal que haya
   detrás, no su destino.
7. **Niebla**: no hay doble aplicación con el layout actual (D-25), pero no se ha medido con una variante de
   la calibración con niebla (D-13).

## Assets

Arte low-poly estilizado (`docs/ARTE.md`): vegetación y rocas de packs CC0 (Quaternius, Kenney) recoloreadas
a la paleta de cada bioma y kits propios generados en Blender (hub, marcos, paredes exentas, hielo y
cultivos), 81 mallas en `/Game/Art` con un único material de color de vértice (`M_LowPoly`). Con el arte, el
coste por portal a 1080p es de 4,32-4,56 ms (4,40-4,46 ms con las formas básicas; límite del encargo 5,1 ms).

## Fuera de alcance

Portales colocados por el jugador, multijugador, objetos que atraviesan el portal a medias
(clonado de mallas) y recursión de más de un nivel.

## Referencias

- Titanfall 2, *Effect and Cause*: dos niveles en el mismo mapa y teletransporte entre ellos.
- Plugin *Streaming Portal Levels* (documentación con ajustes de Lumen y de la captura para UE 5.2+).
- Hilo «Lumen and SceneCapture2D in UE 5.3.2» en los foros de Epic.
