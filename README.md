# LiminalPortals

Banco de pruebas de rendering en Unreal Engine 5.6 (C++): portales que no cortan la imagen al
cruzarlos. No es un juego. Un *hub* liminal con varias puertas fijas; cada puerta muestra en
tiempo real un bioma generado proceduralmente y se puede atravesar sin corte visible.

## Objetivo

- Hub: sala o pasillo liminal (iluminación plana, paredes limpias, silencio) con una puerta por bioma.
- Cada puerta es una ventana al bioma: se ve en directo, con paralaje correcto, desde cualquier ángulo.
- Atravesar la puerta no produce corte ni salto de imagen. Cada bioma tiene una puerta de vuelta al hub.
- Biomas iniciales, generados con PCG y semilla fija:
  1. Bosque / selva: vegetación densa, sotobosque, niebla baja.
  2. Desierto: dunas, rocas erosionadas, sol duro.
  3. Antártida: hielo, nieve, cielo cubierto, luz difusa.
  4. Campos de cultivo: parcelas, cercas, caminos, horizonte abierto.
- Los portales los coloca el diseño (son actores fijos en el nivel). El jugador no crea portales.

## Arquitectura

### Mundos desplazados

Cada bioma es un *Level Instance* (o sublevel) colocado en el mismo mapa con un desplazamiento
grande y fijo respecto al hub (p. ej. `k × 20 km` en X). Con Large World Coordinates el
desplazamiento no tiene problemas de precisión. La física, el audio y la geometría quedan
separados porque ocupan sitios distintos.

### Portal (`ALiminalPortal`)

- Malla plana del marco y la superficie, y una referencia al portal emparejado (`LinkedPortal`).
- `USceneCaptureComponent2D` y un `UTextureRenderTarget2D` a la resolución del viewport.
- Cada fotograma, la cámara de captura se coloca en la transformación de la cámara del jugador
  llevada a través del par de portales: `T_capture = T_linked · Yaw180 · T_portal⁻¹ · T_camera` (el giro de 180° hace que entrar por la cara frontal de A salga por la frontal de B; ver `LiminalPortalMath`). Usa el
  mismo FOV que la cámara del jugador.
- Activar el clip plane de la captura en el plano del portal de destino; si no, se ve lo que hay
  entre la cámara de captura y el portal. Requiere *Support Global Clip Plane for Planar
  Reflections* en Project Settings.
- El material de la superficie toma muestras del render target en coordenadas de pantalla
  (`ScreenPosition`) para que la imagen encaje con la del viewport.
- La captura se hace con `CaptureScene()` manual, después de que se actualice la cámara (tick
  tardío o al final de `PlayerCameraManager`), para evitar el fotograma de retraso.
- Solo se capturan los portales visibles (frustum y orientación). Más adelante, bajar la
  resolución de los portales lejanos.

### Cruce

- El cruce se detecta cuando el signo de `dot(CameraPos − PortalPos, PortalNormal)` cambia con el
  punto dentro de los límites del portal. Se mide sobre la cámara, no sobre la cápsula.
- Al cruzar se aplica `T_linked · Yaw180 · T_portal⁻¹` a la posición, rotación y velocidad del pawn.
  Así se mantiene el movimiento sin saltos.
- Mientras la cámara está a menos de la distancia del plano cercano, se da grosor a la superficie
  del portal (o se renderiza como caja). Así se evita el parpadeo del plano cercano.

### Lógica pura con tests

La matemática va en funciones puras sin `UWorld`, con su Automation Test:

- transformación a través del par de portales (posición, rotación, velocidad),
- detección de cruce de plano con límites,
- cálculo del clip plane del destino.

### Ajustes del proyecto

- `r.DefaultFeature.MotionBlur=False`: la captura no tiene motion blur, pero se aplicaría a la
  superficie del portal.
- Exposición fija (Manual) o la misma en el viewport y en las capturas.
- En cada captura: GI y reflejos con Lumen, `SceneCaptureResolutionScale = 1` y *Always Persist
  Rendering State* si se captura con `CaptureScene()`. UE 5.6 desactiva Lumen en las capturas salvo
  que la captura lo sobrescriba en sus *Post Process Settings*; `ALiminalPortal` copia el método del
  proyecto en cada captura (ver `docs/DECISIONES.md`, D-10).
- Revisar el antialiasing: TSR en la captura no tiene histórico propio y puede verse distinta
  del viewport.

## Riesgos abiertos (experimentos del banco de pruebas)

1. **Iluminación distinta por bioma**: el sol y la `SkyAtmosphere` son casi globales. Opciones a medir:
   a) un sol compartido, con niebla, post-process y materiales por bioma;
   b) una luz direccional por bioma separada con *lighting channels* (comprobar con Lumen);
   c) renderizar los biomas desde un `UWorld` separado (lo más fiel y lo más caro de implementar).
2. **Lumen en las capturas**: coste y diferencias de tono en el borde del portal.
3. **Coste por portal**: cada portal visible es un render de escena adicional.
4. **Recursión** (un portal visto a través de otro): limitarla a 1 nivel o cortarla.

## Fases y criterios de aceptación

| Fase | Entregable | Aceptación |
|---|---|---|
| 1 | Un portal fijo hub → caja de prueba desplazada | La imagen del portal coincide con la del viewport (sin desfase al girar rápido); tests de transformación en verde |
| 2 | Cruce en los dos sentidos | Cruzar 20 veces seguidas sin parpadeo ni salto; la velocidad se conserva (test) |
| 3 | Hub liminal con 4 puertas y biomas en *blockout* | 4 portales funcionando; `stat gpu` con el coste por portal anotado en este README |
| 4 | Biomas con PCG y assets | Cada bioma generado desde una semilla y reproducible; lámina de capturas de los 4 |
| 5 | Pulido y medidas | Experimentos de iluminación 1a/1b documentados; objetivo provisional ≤ 4 ms por portal a 1080p |

## Assets

Por decidir en la fase 4: Fab (Megascans), CC0 (Poly Haven, Quaternius, Kenney) o kits propios
en Blender con el agente `technical-artist`. Estilo: escena liminal, limpia y algo irreal, no
fotorrealismo de catálogo.

## Fuera de alcance

Portales colocados por el jugador, multijugador, objetos que atraviesan el portal a medias
(clonado de mallas) y recursión de más de un nivel.

## Referencias

- Titanfall 2, *Effect and Cause*: dos niveles en el mismo mapa y teletransporte entre ellos.
- Plugin *Streaming Portal Levels* (documentación con ajustes de Lumen y de la captura para UE 5.2+).
- Hilo «Lumen and SceneCapture2D in UE 5.3.2» en los foros de Epic.

## Compilar y probar

```powershell
powershell -File Tools/build.ps1                  # editor (Development)
powershell -File Tools/build.ps1 -Target Game     # juego
powershell -File Tools/test.ps1                   # Automation Tests sin interfaz
powershell -File Tools/content.ps1                # materiales y mapas por script (Tools/Content)
powershell -File Tools/verify.ps1 -Calibrate      # calibración del portal con RHI (fase 1)
powershell -File Tools/verify.ps1 -SelfTest [-SelfTestNegative]   # cruces hub <-> bioma (fases 2 y 3)
powershell -File Tools/verify.ps1 -Tour           # capturas del hub y de cada puerta en Saved/Tour/
powershell -File Tools/verify.ps1 -Bench          # coste por portal a 1920x1080, dos ejecuciones (fase 5)
```

### Calibración del portal (fase 1)

`/Game/Maps/L_Calibration` tiene un portal A y su pareja B en el mismo sitio mirando a lados
opuestos, así que el paso es la identidad y lo que se ve por el portal debe coincidir con lo que se
vería sin él. `Liminal.Calibrate` gira la cámara 10° por fotograma delante del portal en dos pasadas
idénticas (superficie visible y oculta) y guarda las capturas y la máscara del portal;
`Tools/verify.ps1 -Calibrate` lo lanza con `Liminal.Portal.LagFrames 0` y `1` y compara dentro de la
máscara (`Tools/Verify/compare_calibration.py`, resultado en `Saved/Calibration/result.json`).

| Medida (1280×720, TSR, Lumen) | Diferencia media en la máscara |
|---|---|
| `LagFrames 0` (orden de fotograma correcto) | 0,75-0,81 % |
| `LagFrames 1` (control: pose del fotograma anterior) | 16,6 % (ratio > 20) |
| Clip plane invertido (`-ClipPlaneCheck`) | 47,4 % |

Aceptación: < 2 % sin retraso y > 3 veces ese valor con retraso. FXAA y TAA dan lo mismo que TSR;
el residuo es de Lumen (con Lumen apagado, 0,26 %).

### Hub, cruce y biomas en blockout (fases 2 y 3)

`/Game/Maps/L_Hub` (mapa por defecto) lo genera `Tools/content.ps1`: `ALiminalHub` en el origen (pasillo
cerrado de 16 × 6 × 3,2 m, moqueta, paneles fluorescentes con `RectLight` y cuatro puertas de
140 × 230 cm al tresbolillo), cuatro `ALiminalBiome` en k × 20 km (selva, desierto, Antártida y
cultivos: suelo de color y pared exenta con la puerta de vuelta) y ocho portales enlazados por parejas.
La geometría sale de funciones puras (`World/LiminalWorldLayout`) y se regenera en `OnConstruction`;
un cambio de layout en C++ exige volver a generar el mapa (ver `docs/DECISIONES.md`, D-16 a D-18).

`Liminal.SelfTest [Ciclos]` cruza cada puerta ida y vuelta con entrada simulada (Enhanced Input
inyectado) y paso fijo de 1/60 s, y escribe `Saved/SelfTest.json`; `Liminal.Tour` guarda trece
capturas en `Saved/Tour/` (hub y, por puerta, desde el hub, desde el bioma y desde dentro) y
`Tools/Verify/check_tour.py` comprueba que no son negras, quemadas ni uniformes y que el hueco del
portal muestra otra cosa que su marco; `make_lamina.py` compone `Saved/Tour/lamina.png`.

| Medida (1280×720, paso fijo 1/60 s) | Resultado |
|---|---|
| Cruces por sentido (4 puertas × 20) | 80 hub → bioma y 80 bioma → hub, todos correctos |
| Error máximo del módulo de la velocidad | 1,4e-14 % (tolerancia 1 %) |
| Error máximo de dirección / posición | 1,2e-6° / 2e-9 cm |
| Fotogramas con la cámara detrás de un portal o dentro de geometría | 0 de 9141 |
| Control negativo (velocidad al 97 % tras el paso) | falla en las 4 puertas (3 %) |
| Luminancia media de las capturas del Tour (0-255) | 110-185; recuadro central 171-232 (E3) |

### Biomas procedurales (fase 4)

Cada `ALiminalBiome` genera desde su semilla (`FRandomStream` → `LiminalTerrainMath::DeriveSeeds`) un
terreno de 400 × 400 m a 2 m (`UProceduralMeshComponent` con colisión y color por vértice) y la
dispersión con formas básicas del motor (un HISM por categoría, `MI_Scatter_*`), más niebla local en la
selva y la Antártida. Son componentes transitorios: se regeneran en `OnConstruction` y en `BeginPlay`, no
se guardan en el mapa (D-23). La puerta de vuelta y 30 m de camino de salida quedan llanos a ras del
portal y sin dispersión. Detalle de parámetros en `docs/DECISIONES.md`, D-23 a D-27.

| Bioma (semilla) | Relieve | Dispersión | Instancias |
|---|---|---|---|
| Selva (1001) | irregular, −5,5/+6,5 m | árboles (tronco + 1-2 copas), arbustos, sotobosque; niebla baja | 11 201 |
| Desierto (1002) | dunas orientadas, hasta 8,1 m | rocas erosionadas (esferas y cubos aplastados), guijarros, matojos | 1539 |
| Antártida (1003) | ondulación ±2,9 m y sastrugi | bloques y placas de hielo, montículos de nieve; neblina | 889 |
| Cultivos (1004) | llano ±0,9 m con caballones | parcelas de color alterno, caminos, cercas, pacas, árboles aislados | 3318 |

| Medida (1280×720) | Resultado |
|---|---|
| Generación de los cuatro biomas en `BeginPlay` | 0,36 s |
| Calibración tras E4 (`LagFrames 0` / `1`) | 0,785 % / 16,6 % |
| SelfTest tras E4 | 80 + 80 cruces correctos, 0 fotogramas con la cámara detrás de un portal |
| Hueco frente al marco en las vistas de puerta (distancia RGB media) | 68-205 (umbral 20) |
| Pared de vuelta vista desde el bioma (luminancia) | 212-220 (umbral 150, D-24) |

### Medidas (fase 5 parcial)

`Liminal.Bench` coloca la cámara en cinco poses fijas de `L_Hub` (0, 1, 2 y 4 portales visibles y la vista
del caso de 4 con `Liminal.Portals.Enable 0`), comprueba en cada fotograma medido que el subsistema hace las
capturas esperadas, calienta 120 fotogramas y mide 240 por caso: tiempo de GPU del fotograma
(`FRHIGPUFrameTimeHistory`, la serie de `RHIGetGPUFrameCycles`), render thread y game thread. Escribe
`Saved/Bench.json`; `Tools/verify.ps1 -Bench` lo lanza dos veces y compara el coste por portal (D-29).

Equipo: NVIDIA GeForce RTX 4060 Laptop GPU, D3D12 (SM6), ventana de 1920 × 1080 (pantalla de 2560 × 1440),
`r.VSync 0`, `t.MaxFPS 0`, TSR, Lumen GI y reflejos. Render target de las capturas: 1920 × 1080; la vista
principal se renderiza al 73 % y TSR la escala.

| Caso (ejecución 2) | Capturas por fotograma | GPU media / p50 / p95 (ms) | Render thread (ms) | Game thread (ms) |
|---|---|---|---|---|
| 0 portales | 0 | 10,84 / 10,57 / 12,23 | 11,29 | 1,44 |
| 1 portal | 1 | 16,28 / 16,28 / 18,11 | 17,34 | 1,42 |
| 2 portales | 2 | 24,38 / 24,35 / 26,61 | 25,91 | 1,47 |
| 4 portales | 4 | 28,61 / 28,63 / 30,93 | 31,44 | 1,53 |
| Vista de 4 con portales apagados | 0 | 10,89 / 10,60 / 12,44 | 11,54 | 1,34 |

| Coste por portal a 1080p | Ejecución 1 | Ejecución 2 |
|---|---|---|
| GPU, (4 portales − apagados) / 4 | 4,37 ms | 4,43 ms (diferencia 1,4 %) |
| GPU, pendiente sobre 0, 1, 2 y 4 | 4,42 ms | 4,49 ms (diferencia 1,7 %) |
| Game thread, (4 − apagados) / 4 | 0,06 ms | 0,05 ms |

El coste depende de lo que muestra cada portal: frente a la vista de 0 portales, el caso de 1 sale a 5,2-5,4
ms y el de 2 (selva y Antártida) a 6,7-6,8 ms por portal, pero esas vistas también cambian lo que se ve del
hub; el emparejado con los portales apagados es la cifra de referencia. El render thread coincide con el
tiempo de fotograma porque la GPU es el cuello de botella (incluye la espera). Supera el objetivo provisional
de 4 ms: con `Liminal.Portal.CaptureScale 0.73` (render target a la resolución interna de la vista principal)
baja a 3,15-3,22 ms con la calibración en 0,81 % a 1080p, y los reflejos en pantalla no ahorran nada medible;
ninguna palanca queda activada hasta revisar la imagen (D-30).

Con el editor abierto, la compilación por línea de comandos falla por Live Coding: cerrarlo antes.
La primera vez, clic derecho en `LiminalPortals.uproject` → *Generate Visual Studio project files*.
