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
