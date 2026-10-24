# CLAUDE.md — LiminalPortals

## Qué es

Proyecto de Unreal Engine 5.6 con C++20 (módulo único `LiminalPortals`). Banco de pruebas de rendering: hub liminal con puertas fijas que son portales sin corte a biomas procedurales (selva, desierto, Antártida, cultivos). Mundos desplazados en el mismo mapa y SceneCapture2D con clip plane.

El diseño, la arquitectura, los riesgos y las fases con criterios de aceptación están en `README.md`: leerlo antes de empezar y mantenerlo al día cuando cambie una decisión.

## Stack

- Unreal Engine 5.6 (`C:/Program Files/Epic Games/UE_5.6`; variable `UE_ROOT` para cambiarlo).
- Módulo `LiminalPortals` en `Source/LiminalPortals/`; categoría de log `LogLiminalPortals`.

## Cómo compilar / ejecutar

```powershell
powershell -File Tools/build.ps1                  # editor (Development)
powershell -File Tools/build.ps1 -Target Game     # juego
```

Con el editor abierto, `Build.bat` falla por Live Coding: cerrarlo antes de compilar.

## Tests

```powershell
powershell -File Tools/test.ps1 [-Filter <prefijo>]
```

Automation Tests en `Source/LiminalPortals/Tests/*.cpp`; el script falla si algún test no pasa
o si no se ejecuta ninguno. Regla: la lógica que mueve números va en funciones puras con su
test, sin depender del mundo ni de actores.
