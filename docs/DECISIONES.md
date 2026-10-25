# Decisiones

Registro de decisiones de diseño. Las de la sección «Pendientes de revisión» las tomó la IA
durante la sesión nocturna del 2026-10-25 sin validación de Rodrigo: cada una lleva alternativas y
motivo para poder razonarla y revertirla.

## Pendientes de revisión

Las doce decisiones de partida están en `docs/PLAN-NOCHE.md` («Decisiones fijadas para esta noche»).
Las que surjan durante la implementación se añaden aquí con este formato:

### D-NN · Título

- **Contexto**: qué problema obligó a decidir.
- **Elegida**: opción tomada.
- **Alternativas**: otras opciones y su coste.
- **Motivo**: por qué se eligió.
- **Cómo revertir**: ficheros o ajustes afectados.

### D-01 · Origen del portal a ras de suelo y paso de suelo a suelo

- **Contexto**: el plan no fija dónde está el origen de `ALiminalPortal`.
- **Elegida**: origen en el centro inferior del hueco (a ras de suelo), eje X hacia fuera. El paso
  (`GetTransferToLinked`) usa el transform del actor, de modo que el suelo de A coincide con el de B
  aunque los huecos tengan alturas distintas. El cruce y la visibilidad usan el centro del
  rectángulo (`GetPlaneTransform`).
- **Alternativas**: origen en el centro del hueco (más simétrico, pero colocar portales en el nivel
  obliga a sumar media altura y, con alturas distintas, el jugador aparece flotando o hundido).
- **Motivo**: lo conservador para un personaje que camina es conservar el suelo.
- **Cómo revertir**: `ALiminalPortal::GetPortalTransform` y `ApplyLayout` en `Portal/LiminalPortal.cpp`.

### D-02 · Sin corte de cámara al teletransportar

- **Contexto**: tras el paso, la cámara salta decenas de kilómetros; TSR/TAA reproyecta con la
  matriz del fotograma anterior.
- **Elegida**: `ALiminalPlayerCameraManager::bCameraCutOnTeleport = false` por defecto.
- **Alternativas**: marcar corte (`SetGameCameraCutThisFrame`): descarta el histórico y da un
  fotograma sin antialiasing temporal, lo que puede verse como el salto que se quiere evitar.
- **Motivo**: la imagen antes y después del paso es casi idéntica; se mide en E2/E3 y se cambia con
  un solo booleano si aparece ghosting.
- **Cómo revertir**: `bCameraCutOnTeleport = true` en `Player/LiminalPlayerCameraManager.h`.

### D-03 · Rutas y carga de los materiales del portal

- **Contexto**: los materiales se crean en E2; E1 solo los referencia.
- **Elegida**: `/Game/Portal/Materials/M_PortalSurface` (parámetro de textura `PortalTexture`) y
  `/Game/Portal/Materials/M_PortalFallback`, en `ULiminalPortalsSettings` (DefaultGame.ini). Carga
  síncrona en `BeginPlay`, con aviso y sin material si el paquete no existe.
- **Alternativas**: carga asíncrona con `FStreamableManager` (innecesaria para dos materiales
  pequeños en un banco de pruebas).
- **Motivo**: simple, sin crash antes de E2.
- **Cómo revertir**: `Config/DefaultGame.ini` y `LiminalPortalsSettings.h`.

### D-04 · Marco en túnel hasta la placa trasera y medidas de la superficie

- **Contexto**: la decisión 3 fija una placa a ~60 cm; hay que impedir entrar por el lateral al
  hueco entre el plano y la placa.
- **Elegida**: los montantes y el dintel ocupan desde 5 cm delante del plano hasta la placa (60 cm).
  Superficie: 1 cm en reposo y 40 cm extendida cuando la cámara está a menos de 60 cm por delante
  (siempre al menos 5 cm por delante de la placa).
- **Alternativas**: marco fino y pared del hub cerrando los lados (depende del generador de E3).
- **Motivo**: el portal es autosuficiente y no depende de cómo se construya la pared.
- **Cómo revertir**: propiedades `BackPlateDistance`, `FrameThickness`, `NearThicknessDistance`,
  `SurfaceBaseDepth` y `SurfaceExtendedDepth` de `ALiminalPortal`.

### D-05 · Recalcular la cámara con `Super::UpdateCamera(0)`

- **Contexto**: tras teletransportar hay que capturar con la cámara ya en el destino.
- **Elegida**: llamar otra vez a `APlayerCameraManager::UpdateCamera` con `DeltaTime = 0`, protegida
  con un booleano para que no haya recursión.
- **Alternativas**: recolocar a mano la `FMinimalViewInfo` con el paso (rápido, pero se salta
  modificadores, sacudidas y la caché de cámara que usa el render).
- **Motivo**: la caché de cámara del motor queda coherente con el pawn movido.
- **Cómo revertir**: `ALiminalPlayerCameraManager::RecomputeAfterTeleport`.

### D-06 · Límite de 10 m para contar un cruce

- **Contexto**: si el pawn aparece o se teletransporta por otra vía, el segmento entre fotogramas
  puede atravesar un portal sin haberlo cruzado.
- **Elegida**: `DetectCrossing` ignora saltos mayores que `MaxStep = 1000 cm`.
- **Alternativas**: obligar a llamar a `ResetCameraHistory` en cada teletransporte ajeno (frágil).
- **Motivo**: 10 m por fotograma son 600 m/s a 60 fps; ningún movimiento a pie llega.
- **Cómo revertir**: argumento `MaxStep` en `Portal/LiminalPortalMath.h`.

### D-07 · Alcance del teletransporte y del historial

- **Contexto**: banco de pruebas de un jugador.
- **Elegida**: solo se teletransporta un `ALiminalCharacter`; un único historial de cámara en el
  subsistema (sin pantalla dividida). Tras un teletransporte se vacía el historial de
  `Liminal.Portal.LagFrames`.
- **Alternativas**: historial por `APlayerController`; interfaz genérica de teletransporte.
- **Motivo**: README deja fuera multijugador y objetos que atraviesan.
- **Cómo revertir**: `ULiminalPortalSubsystem::ProcessCrossings`.

### D-08 · Ajustes de motor no fijados por el plan

- **Contexto**: Lumen exige SM6 y el mapa por defecto aún no existe.
- **Elegida**: `DefaultGraphicsRHI_DX12` con `PCD3D_SM6`; `r.GenerateMeshDistanceFields=True`;
  `GameDefaultMap=/Game/Maps/L_Hub` pero **sin** `EditorStartupMap` hasta que exista (un mapa de
  arranque inexistente ensucia los commandlets). Clip plane 1 cm detrás del plano del destino.
  Visibilidad por esfera envolvente y cono del frustum, sin oclusión.
- **Alternativas**: test de frustum exacto por planos; oclusión por consultas de GPU (E5).
- **Motivo**: conservador: capturar de más antes que congelar un portal visible.
- **Cómo revertir**: `Config/DefaultEngine.ini`; `ClipPlaneOffset` en `ALiminalPortal`.

### D-09 · Pareja de calibración: portales sin empotrar y solo de salida

- **Contexto**: en `L_Calibration` A y B coinciden (paso = identidad). Con la decisión 3 la placa
  trasera de B queda 60 cm delante de A y tapa la vista; la superficie de B (de espaldas a la
  cámara, sin captura) se pinta delante de la de A; y el reserva de A (visible solo en capturas)
  queda dentro de la propia captura de A.
- **Elegida**: dos propiedades de `ALiminalPortal`: `bEmbedded` (por defecto `true`; si es `false`,
  marco en aro de ±5 cm y placa oculta y sin colisión) y `bExitOnly` (sin superficie visible, sin
  captura y sin cruce desde él). Además, cada captura oculta el reserva del origen además del
  del destino.
- **Alternativas**: separar A y B unos centímetros (el paso deja de ser la identidad y la
  comparación ya no es píxel a píxel); ocultar componentes desde el script de contenido (frágil,
  depende de nombres de subobjetos).
- **Motivo**: lo mínimo para que la pareja coincidente funcione sin tocar el caso normal. Ocultar
  el reserva del origen solo cambia algo si el origen es visible desde el destino; en ese caso se
  ve la placa trasera en lugar del color plano.
- **Cómo revertir**: `bEmbedded`, `bExitOnly` y `SetSurfaceHiddenInGame` en
  `Portal/LiminalPortal.*`; `HiddenComponents.Add(Portal->GetFallbackComponent())` en
  `ULiminalPortalSubsystem::CaptureVisiblePortals`.

### D-10 · Lumen activado explícitamente en las capturas

- **Contexto**: la primera calibración dio 8,6 % de diferencia media en la máscara: las caras en
  sombra salían casi negras en el portal. `SceneCaptureRendering.cpp` (UE 5.6) fuerza
  `DynamicGlobalIlluminationMethod` y `ReflectionMethod` a `None` en las capturas salvo que la
  propia captura los sobrescriba, y baja la caché de superficies a 0,5. Comprobado: con
  `r.Lumen.DiffuseIndirect.Allow 0` la vista principal y el portal coinciden píxel a píxel.
- **Elegida**: en cada `CaptureView`, la captura copia `r.DynamicGlobalIlluminationMethod` y
  `r.ReflectionMethod` del proyecto (override en sus `PostProcessSettings`) y fija
  `LumenSurfaceCacheResolution = 1`. Resultado: 0,75-0,81 % (antes 8,6 %).
- **Alternativas**: `bMainViewCamera` + `bInheritMainViewCameraPostProcessSettings` (exige
  `CaptureSceneDeferred` y cambia el orden de fotograma de la decisión 4); quitar Lumen del proyecto
  (0,26 %, pero contradice la decisión 8).
- **Motivo**: el portal debe verse con la misma iluminación que el viewport; es el ajuste que
  pide el propio motor.
- **Cómo revertir**: `MatchProjectLighting` en `Portal/LiminalPortal.cpp`.

### D-11 · Sentido del clip plane comprobado en ejecución

- **Contexto**: había que confirmar que `USceneCaptureComponent2D` conserva el semiespacio hacia
  el que apunta `ClipPlaneNormal` (el motor monta `FPlane(Base, Normal)` como `GlobalClippingPlane`).
- **Elegida**: sin cambios en `LiminalPortalMath::ComputeClipPlane` ni en sus tests. Evidencia:
  `Tools/verify.ps1 -Calibrate -ClipPlaneCheck` da 0,79 % con el sentido actual y 47,4 % con la
  normal invertida (`Liminal.Portal.FlipClipPlane 1`: el portal muestra el cielo y negro).
- **Alternativas**: ninguna; la CVar de diagnóstico se queda para repetir la comprobación.
- **Cómo revertir**: no aplica.

### D-12 · Antialiasing: se mantiene TSR

- **Contexto**: README avisaba de que TSR en la captura no tiene histórico propio. Las capturas
  `SCS_SceneColorHDR` no pasan por post-proceso (sin TSR ni jitter); la vista principal aplica TSR
  una vez sobre la superficie.
- **Elegida**: TSR (`r.AntiAliasingMethod 4`, el valor por defecto). Medido en `L_Calibration`
  (32 fotogramas, giros de 10°): TSR 0,75-0,81 %, FXAA 0,71 %, TAA 0,69 %; con `LagFrames 1`,
  16,4-16,6 % en los tres. La diferencia residual es de Lumen (con Lumen apagado baja a 0,26 %),
  no del antialiasing.
- **Alternativas**: FXAA o TAA (mejora inapreciable para la aceptación y peor imagen).
- **Motivo**: TSR cumple con margen (< 2 % y ratio > 20).
- **Cómo revertir**: `r.AntiAliasingMethod` en `Config/DefaultEngine.ini`.

### D-13 · Calibración sin niebla y riesgo de doble niebla en los biomas

- **Contexto**: la niebla exponencial, la `ALocalFogVolume` y la perspectiva aérea de
  `SkyAtmosphere` se aplican a los píxeles opacos según su profundidad. La superficie del portal es
  opaca: recibe niebla por el tramo cámara-portal además de la que ya trae la captura.
- **Elegida**: `L_Calibration` no tiene niebla (perspectiva aérea a partir de 100 m, fuera de la
  sala). El riesgo queda abierto para E3/E4, donde los biomas usan niebla local.
- **Alternativas para E3/E4**: niebla solo en el lado del bioma lejos de la puerta; superficie
  translúcida sin niebla (cambia el orden de render y el TSR); medirlo con una variante de la
  calibración con niebla.
- **Motivo**: medir primero el caso limpio; la niebla es un experimento aparte.
- **Cómo revertir**: no aplica (no hay código).

### D-14 · Lumen ve la superficie del portal en sus trazas de pantalla

- **Contexto**: fuera de la máscara, la pasada visible y la oculta difieren 1,8 niveles de media;
  lo más visible es el suelo en sombra justo delante del portal, más claro con la superficie
  visible. Las trazas de pantalla de Lumen leen el color de escena: la superficie emite la imagen
  del destino como si estuviera en el plano.
- **Elegida**: se acepta; no entra en la aceptación de E2 y, en un portal de verdad, que la luz
  del destino ilumine el lado de origen es razonable.
- **Alternativas**: `r.Lumen.ScreenProbeGather.ScreenTraces 0` (no resuelve la diferencia de
  dentro de la máscara: 7,8 % sin el arreglo de D-10) o excluir la superficie de las trazas.
- **Motivo**: conservador: sin cambios hasta medirlo en el hub (E3/E5).
- **Cómo revertir**: no aplica.

### D-15 · Procedimiento de calibración

- **Contexto**: el plan fija la secuencia pero no el formato de salida ni la métrica exacta.
- **Elegida**: `Liminal.Calibrate [Etiqueta]` escribe en `Saved/Calibration[/Etiqueta]/`
  `{visible,hidden}/NNN.png` y `calibration.json` (esquinas del portal en píxeles por fotograma,
  CVars vigentes y contraste con `ProjectWorldLocationToScreen`, error máximo 1e-4 px). Barrido de
  yaw en onda triangular ±20° con paso de 10° (32 fotogramas por pasada), 45 fotogramas quietos
  antes de cada pasada y entrada del jugador bloqueada. `compare_calibration.py` rasteriza la
  máscara, la erosiona 3 px y promedia |visible − oculta| en RGB dentro de ella; escribe `mask/` y
  `diff/` (×8) para revisión visual. `verify.ps1` usa las etiquetas `lag0`, `lag1` y `flipclip`.
- **Alternativas**: máscara renderizada (tercera pasada con material plano); métrica en luminancia.
- **Motivo**: la máscara geométrica no depende del render que se mide y el polígono es comprobable.
- **Cómo revertir**: `Debug/LiminalCalibration.*`, `Tools/Verify/compare_calibration.py`.

### D-16 · Hub en pasillo con cuatro puertas al tresbolillo

- **Contexto**: el plan pide «pasillo o sala liminal» con cuatro huecos del tamaño del portal.
- **Elegida**: sala cerrada de 16 × 6 × 3,2 m (interior), paredes de 20 cm, moqueta y techo de
  10 cm y cuatro paneles fluorescentes de 120 × 60 cm con una `RectLight` de 40 nits cada uno.
  Puertas de 140 × 230 cm (cápsula de 68 cm: 36 cm de holgura por lado) con marco de 12 cm, dos en
  cada pared larga y alternadas: X = −500 y +100 en la pared de +Y, −100 y +500 en la de −Y. El plano
  del portal queda en la cara interior de la pared; el túnel del marco y la placa trasera (70 cm)
  sobresalen por fuera del hub, donde no se ven. El hueco de la pared es el exterior del marco.
- **Alternativas**: puertas en los extremos (se ven todas a la vez y una tapa a otra); portal de
  120 cm (26 cm de holgura, más roces con los montantes); pared de 70 cm que contenga el túnel.
- **Motivo**: el pasillo es el arquetipo liminal y las puertas alternadas no se miran de frente.
  Con 100 nits la vista general salía quemada (luminancia media 221/255); con 40, 185.
- **Cómo revertir**: propiedades de `ALiminalHub` y `MakeDefaultHubDoors` en `World/LiminalWorldLayout.cpp`;
  volver a generar `L_Hub` con `Tools/content.ps1`.

### D-17 · Geometría de blockout con ISM regenerados en OnConstruction

- **Contexto**: hub y biomas se generan por código; hay que decidir con qué componentes.
- **Elegida**: un `UInstancedStaticMeshComponent` de cubos del motor por material (paredes,
  moqueta, techo, paneles), creados en el constructor y rellenados en `OnConstruction` desde la
  geometría pura de `LiminalWorldLayout`. Las `RectLight` son cuatro subobjetos fijos. Las
  instancias quedan guardadas en el mapa: en `-game` no se vuelve a ejecutar `OnConstruction`, así
  que **un cambio de layout en C++ exige regenerar `L_Hub`**. Sin navegación en esos ISM: en UE 5.6
  `AddInstance` en un mundo de juego invalida la caché de límites y el sistema de navegación la lee
  antes de recalcularla (`ensure` en `GetNavigationBounds`). `M_Basic` lleva
  `bUsedWithInstancedStaticMeshes` (sin él, el juego usaba el material por defecto).
- **Alternativas**: componentes creados en `OnConstruction` con `CreationMethod = UserConstructionScript`
  (más frágil de serializar); `UProceduralMeshComponent` (innecesario para cajas).
- **Motivo**: un solo camino de datos (layout puro → cajas) y cero assets propios.
- **Cómo revertir**: `World/LiminalBlockout.*`, `World/LiminalHub.*`, `World/LiminalBiome.*`.

### D-18 · Bioma: origen en la puerta de vuelta y pared exenta fina

- **Contexto**: el plan fija la pared exenta con la puerta de vuelta, no sus medidas.
- **Elegida**: el origen de `ALiminalBiome` es el de su portal de vuelta (eje X hacia el bioma) y el
  suelo, de 400 × 400 m, tiene la cara superior en Z = 0 (paso de suelo a suelo, D-01). Pared de
  6 × 3,5 m y 20 cm, en X ∈ [−20, 0]; el túnel del portal sobresale 50 cm por detrás. Colores de
  suelo por tipo en `ALiminalBiome::GetBiomeBaseColor` (el script de contenido crea `MI_Biome_*` con
  ellos). Biomas en k × 20 km con k = 1..4 en el orden selva, desierto, Antártida y cultivos, y
  semillas 1001-1004. La generación de E4 entra por `BuildProceduralContent(FRandomStream&)`.
- **Alternativas**: pared de 80 cm que esconda el túnel (deja un rebaje de 10 cm por detrás).
- **Motivo**: blockout simple; la vista por detrás de la pared no forma parte de ninguna prueba.
- **Observación**: el sol (el de la calibración) viene de −X: desde el hub se ve el bioma de cara
  al sol en la espalda, y la cara frontal de la pared exenta queda en su propia sombra. Se revisa
  con la iluminación por bioma (riesgo 1).
- **Cómo revertir**: `World/LiminalBiome.*`, `BIOMES` en `Tools/Content/build_content.py`.

### D-19 · Método de Liminal.SelfTest

- **Contexto**: el plan pide 20 cruces hub → bioma → hub por puerta con entrada simulada o
  velocidad impuesta, sin teletransporte manual.
- **Elegida**: entrada simulada con `UEnhancedInputLocalPlayerSubsystem::InjectInputForAction` en
  la acción de movimiento del personaje, cada fotograma; la dirección se fija con la rotación de
  control (la entrada de mirada real se ignora durante la prueba). Al empezar cada puerta se coloca
  al jugador 3 m delante (es preparación, no un cruce: se vacía el historial de cámara). Ciclo:
  camina hacia el centro del hueco, cruza, se aleja 1,5 m del portal de salida, da media vuelta y
  vuelve a cruzar. Paso fijo de 1/60 s (`-UseFixedTimeStep -FPS=60`) para que el movimiento no
  dependa de la velocidad de render. Comprobaciones por cruce (`LiminalSelfTestMath`, independientes
  de `MakeTransfer`: coordenadas locales (x, y, z) ↔ (−x, −y, z)): portales esperados, salto > 1 km,
  módulo de la velocidad (1 %), dirección (0,5°), posición (1 cm), cámara por delante del destino y
  velocidad del fotograma siguiente ≥ 99 %. En cada fotograma: cámara fuera del volumen de detrás
  de cada portal (hueco + marco, hasta la placa + 1 m) y sin solapar geometría (esfera de 5 cm en
  `ECC_Camera`). El subsistema emite ahora `FLiminalPortalTeleportEvent` (estado antes y después).
- **Control negativo**: `Liminal.Portal.DebugVelocityScale 0.97` (escala la velocidad tras cada
  paso) hace fallar las cuatro puertas con un 3 % de error (`verify.ps1 -SelfTest -SelfTestNegative`).
- **Resultado**: 80 cruces por sentido, error de velocidad 1,4e-14 %, dirección 1,2e-6°, posición
  2e-9 cm, cámara 1,2-4,2 cm por delante del destino tras el paso, 0 fotogramas con la cámara detrás
  de un portal o dentro de geometría (9141 fotogramas).
- **Alternativas**: imponer `Velocity` al `CharacterMovement` (se salta el camino de entrada real);
  `AddMovementInput` directo (no pasa por Enhanced Input).
- **Cómo revertir**: `Debug/LiminalSelfTest.*`, `Debug/LiminalSelfTestMath.*`.

### D-20 · Liminal.Tour y criterio de «no negra»

- **Contexto**: hay que comprobar por script que las capturas no son negras.
- **Elegida**: nueve capturas (vista general del hub y, por puerta, desde 3,5 m en el hub y desde
  4,5 m en el bioma, mirando al centro del hueco), con 90 fotogramas de espera para TSR y Lumen.
  `Tools/Verify/check_tour.py` exige luminancia media (Rec. 709 sobre sRGB) entre 15 y 245 en la
  imagen completa y en el recuadro central del 20 % (donde queda la puerta). Medido: medias de 110 a
  185 y centros de 171 a 232.
- **Alternativas**: comparar con imágenes de referencia (frágil ante cambios de iluminación).
- **Cómo revertir**: `Debug/LiminalTour.*`, `Tools/Verify/check_tour.py`.

### D-21 · L_Hub también como mapa de arranque del editor

- **Contexto**: D-08 dejó `EditorStartupMap` sin fijar hasta que existiera `L_Hub`.
- **Elegida**: `EditorStartupMap=/Game/Maps/L_Hub.L_Hub`. Los tests (`Tools/test.ps1`) siguen en
  verde y tardan lo mismo (17 s).
- **Cómo revertir**: `Config/DefaultEngine.ini`.

### D-22 · Unity build completo: sin símbolos repetidos en namespaces anónimos

- **Contexto**: tras d406fff el árbol limpio no compilaba (C2374/C2084 por `MaxTicksWithoutUpdate`,
  `ToJsonArray` y `CubeSize` repetidos en namespaces anónimos; C4459 porque los fixtures globales
  `PortalA`/`PortalB` de `LiminalPortalMathTests.cpp`, visibles por `using namespace`, ocultaban
  locales de `LiminalPortalWorldTests.cpp`). UBT junta los 26 .cpp del módulo en un solo
  `Module.LiminalPortals.cpp`; el adaptive unity saca de él los ficheros modificados según git, y por
  eso el build con cambios sin commitear salía en verde.
- **Elegida**: `MaxTicksWithoutUpdate` y `ToJsonArray` pasan a `Debug/LiminalDebugShared.h`
  (namespace `LiminalDebugShared`); `CubeSize` a `LiminalBlockout.h`; los fixtures se renombran a
  `FixturePortalA`/`FixturePortalB`. `Tools/build.ps1` pasa `-DisableAdaptiveUnity` para compilar
  siempre el unity completo, sea cual sea el estado de git. Se mantiene el unity build (más rápido).
- **Alternativas**: `bUseUnity = false` en `LiminalPortals.Build.cs` (oculta el problema a costa de
  tiempo de compilación).
- **Regla**: un símbolo de namespace anónimo debe ser único en el módulo; lo compartido va a un
  header con namespace propio.
- **Cómo revertir**: quitar `-DisableAdaptiveUnity` de `Tools/build.ps1`.

### D-23 · Terreno y dispersión como componentes transitorios regenerados desde la semilla

- **Contexto**: D-17 guarda en el mapa las instancias del blockout. El terreno de 400 × 400 m a 2 m
  (40 401 vértices) y hasta 11 200 instancias por bioma guardados en `L_Hub` (sin LFS) habrían hecho
  crecer el mapa varios MB en cada regeneración, y `ProcMeshSections` es `UPROPERTY` (se serializa).
- **Elegida**: `UProceduralMeshComponent` y un `UHierarchicalInstancedStaticMeshComponent` por categoría
  creados con `NewObject(..., RF_Transient)` en `BuildProceduralContent`, desde `OnConstruction` (editor)
  y desde `BeginPlay` si no existen (juego, PIE). La pared exenta y la niebla siguen siendo subobjetos
  guardados. Se quita el suelo de losa: el terreno es el suelo. `L_Hub` pasa de 85,5 KB a 89,6 KB.
  Generación de los cuatro biomas en `BeginPlay`: 0,36 s en total (log del Tour). Navegación desactivada
  en todos los componentes (D-17); colisión solo en terreno, troncos, rocas grandes, bloques de roca y de hielo.
- **Alternativas**: guardar los componentes en el mapa como en D-17 (sin coste en `BeginPlay`, pero el
  mapa crece y un cambio en C++ exige regenerar `L_Hub`); PCG (decisión 9 del plan).
- **Motivo**: un solo camino de datos (semilla → funciones puras → componentes), mapa pequeño y un cambio
  de las recetas no exige regenerar el mapa.
- **Cómo revertir**: `ALiminalBiome::BuildTerrain`, `BuildScatter` y `BeginPlay` en `World/LiminalBiome.cpp`.

### D-24 · Sol de L_Hub de cara a las puertas de vuelta

- **Contexto**: con el sol de E2 (`pitch −42°, yaw 35°`, la luz viaja hacia +X) la cara frontal de cada
  pared exenta quedaba en su propia sombra y la sombra caía delante de la puerta, oscureciendo el primer
  plano visto desde el hub (D-18).
- **Elegida**: en `L_Hub`, `pitch −35°, yaw 125°` (la luz viaja hacia −X y +Y): la cara frontal recibe
  luz (franja de pared alrededor del marco con luminancia 212-220 en las vistas desde el bioma) y la
  sombra cae detrás de la pared y hacia un lado. Visto desde el hub (mirando a +X) el paisaje queda con
  luz lateral, que marca el relieve de las dunas. `L_Calibration` conserva el sol de E2 para no mover su medida.
- **Alternativas**: girar las paredes de vuelta 180° (cambia la convención de los portales y el cruce);
  sol a contraluz desde +X (paisaje plano y oscuro visto desde el hub).
- **Cómo revertir**: `HUB_SUN_ROTATION` en `Tools/Content/build_content.py`.

### D-25 · Niebla local con el componente de ALocalFogVolume y parámetros del shader de UE 5.6

- **Contexto**: el plan pide `ALocalFogVolume` en la selva y la Antártida. Con densidades «por metro»
  pequeñas (0,015) no se veía niebla: `LocalFogVolumeCommon.ush` combina las opacidades radial y de altura
  multiplicándolas en el espacio de la esfera unidad (`T = 1 − (1 − T_r)(1 − T_h)`) y escala después por
  el radio en metros; con densidad radial 0 la niebla es nula. Además empieza a 20 m
  (`r.LocalFogVolume.GlobalStartDistance`).
- **Elegida**: `ULocalFogVolumeComponent` (el componente de `ALocalFogVolume`) como subobjeto de
  `ALiminalBiome`, esfera de 300 m centrada en la puerta, configurado por tipo en `ApplyAtmosphere`.
  Selva: radial 2, altura 0,04, caída 7500 (1/e cada 4 m): cobertura ~50 % a 100 m en horizontal y 1 %
  hacia el cielo a 30°. Antártida: radial 2, altura 0,03, caída 800: ~50 % a 100 m y ~60 % hacia el cielo
  (aproximación de cielo cubierto con el sol compartido). Valores calculados reproduciendo la integral del
  shader y comprobados en las capturas del Tour.
- **Alternativas**: un actor `ALocalFogVolume` por bioma colocado desde Python (los parámetros quedarían
  fuera del C++ del bioma); `ExponentialHeightFog` global (se aplicaría también al hub). La luz difusa de
  verdad en la Antártida exige su propia luz (riesgo 1, opción 1b): con el sol compartido las sombras
  siguen siendo duras.
- **Doble niebla (D-13)**: desde el hub no hay niebla en el tramo cámara-portal; desde el bioma, la del
  tramo cámara-puerta es la que hay de verdad, y el hub no tiene niebla. No hay doble aplicación.
- **Cómo revertir**: `LiminalBiomeDetail::GetBiomeFog` en `World/LiminalBiome.cpp`.

### D-26 · Terreno: color por vértice, zona llana y recetas de dispersión

- **Contexto**: el plan fija campo de alturas y dispersión puros, no sus parámetros.
- **Elegida**: ruido de valor con hash entero (lowbias32) y fBm normalizado a [−1, 1], de modo que cada
  tipo tiene un intervalo de alturas garantizado por construcción (selva −5,5/+6,5 m, desierto −0,6/+8,1 m,
  Antártida ±2,9 m, cultivos −0,9/+0,85 m). Dunas: perfil asimétrico de 55 m con orientación sacada de la
  semilla y crestas alabeadas. Zona llana: cápsula de 6 m de radio de X = −8 m a +30 m y transición
  smoothstep de 15 m; el relieve se multiplica por ese peso, así que la puerta y el camino quedan a Z = 0
  exacto. Colores por vértice guardados en sRGB y devueltos a lineal en `M_Terrain` (`VertexColor^2,2`;
  en lineal de 8 bits los verdes oscuros se bandeaban). Dispersión: rejilla con jitter del 80 % (una
  muestra por celda, separación mínima 0,2 · celda), densidad modulada por fBm y exclusión de 10 m en la
  puerta y 7 m a lo largo del camino de salida (35 m), con la huella de cada capa; un filtro final quita
  cualquier pieza derivada con el centro dentro. Instancias con las semillas 1001-1004: selva 11 201,
  desierto 1539, Antártida 889, cultivos 3318 (presupuesto 15 000).
- **Alternativas**: Poisson disk de Bridson (mejor distribución, más código y más difícil de hacer
  estable al cambiar el área); ruido Perlin del motor (`FMath::PerlinNoise2D`, sin semilla).
- **Cómo revertir**: `World/LiminalTerrainMath.*`, `World/LiminalScatterMath.*` y `build_terrain_material`
  en `Tools/Content/build_content.py`.

### D-27 · Comprobaciones del Tour y lámina

- **Contexto**: la luminancia media no detecta una imagen uniforme ni un portal que muestre lo mismo que
  su marco.
- **Elegida**: `Liminal.Tour` añade, por puerta, una vista desde dentro (pies a 6 m de la puerta, de
  espaldas a ella, pitch −4°) y guarda en `tour.json` los polígonos en píxeles del hueco, del exterior del
  marco y de una franja de 50 cm de pared. `check_tour.py` exige además desviación de luminancia > 6 en
  toda captura y, en las vistas de puerta, desviación > 4 dentro del hueco y distancia RGB media de los
  píxeles del hueco al color medio del marco > 20 (medido: 68-205). Comparar solo medias fallaba con la
  selva (14,7: paisaje oscuro y variado). Desde el bioma, la franja de pared debe tener luminancia ≥ 150
  (D-24; medido 212-220). `make_lamina.py` compone `Saved/Tour/lamina.png` (4 filas × 3 columnas).
- **Cómo revertir**: `Debug/LiminalTour.*`, `Tools/Verify/check_tour.py`, `Tools/Verify/make_lamina.py`.

### D-28 · Luz de cielo de L_Hub al doble y suelo de la selva más claro

- **Contexto**: la verificación de E4 encontró el primer plano de la selva vista desde dentro negro
  (`03_door0_Jungle_inside.png`: franja inferior y 500-720 con luminancia media 10,6 y 97 % de píxeles
  < 20) y una banda casi negra cruzando el camino visto desde el hub. No es la sombra de la pared de vuelta
  (cae detrás de ella, D-24), sino la de los árboles del lado del sol sobre el claro. Medido con el Tour:
  sin oclusión de cielo (`r.Lumen.DiffuseIndirect.Allow 0`, `r.DistanceFieldAO 0`) la franja solo sube a
  16,6, así que la oclusión de las copas explica una parte pequeña; el resto es el albedo del suelo de la
  selva (musgo 0,04-0,17 de luminancia lineal y camino 0,08, frente a 0,17-0,5 en cultivos y 0,45-0,9 en
  desierto y nieve) con la proporción sol/cielo de la `SkyAtmosphere` y el pie del tonemapper.
- **Elegida**: dentro de la opción 1a (sol y cielo compartidos), dos cambios: SkyLight de `L_Hub` con
  intensidad 2,0 (`HUB_SKY_INTENSITY` en `build_content.py`; `L_Calibration` sigue en 1,0) y suelo de la
  selva con albedo verosímil (musgo `(0,10, 0,32, 0,08)`-`(0,09, 0,18, 0,05)`, camino `(0,26, 0,17, 0,09)`
  en `LiminalTerrainMath::SampleColor`). Franja inferior de la selva: 10,6 → 23,3 solo con el albedo y
  41,7 con los dos (0 % < 20); banda del camino desde el hub ~21 → ~58. Los otros biomas apenas cambian
  (franja inferior: desierto 189 → 195, Antártida 221 → 225, cultivos 152 → 159). `check_tour.py` exige
  ahora en las vistas `inside` como mucho un 25 % de píxeles < 20 en el 30 % inferior de la imagen.
- **Alternativas**: un claro más grande alrededor de la puerta (con el sol a 35° la copa de un árbol de
  16 m proyecta sombra a ~23 m: el claro tendría que ser asimétrico y depender del sol); luz direccional
  de relleno por bioma con *lighting channels* (opción 1b, experimento de E5); `Skylight Leaking` de
  Lumen (actúa sobre la oclusión, que no es la causa principal).
- **Cómo revertir**: `HUB_SKY_INTENSITY = 1.0` y regenerar `L_Hub` (`Tools/content.ps1`); colores de la
  selva en `World/LiminalTerrainMath.cpp`; `MAX_DARK_FOREGROUND` en `Tools/Verify/check_tour.py`.
