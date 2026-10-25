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
