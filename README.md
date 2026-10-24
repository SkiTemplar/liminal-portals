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
  llevada a través del par de portales: `T_capture = T_linked · T_portal⁻¹ · T_camera`. Usa el
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
- Al cruzar se aplica `T_linked · T_portal⁻¹` a la posición, rotación y velocidad del pawn.
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
  Rendering State* si se captura con `CaptureScene()`.
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
```

Con el editor abierto, la compilación por línea de comandos falla por Live Coding: cerrarlo antes.
La primera vez, clic derecho en `LiminalPortals.uproject` → *Generate Visual Studio project files*.
