# Kits propios generados en Blender

Kits deterministas (semilla fija por kit) que dan identidad al proyecto; la vegetación genérica
sale de `Tools/Art/Packs`. Dirección y paletas en `docs/ARTE.md`.

```powershell
powershell -File Tools/Art/Blender/build_kits.ps1            # tests, exportación, comprobador y hojas
powershell -File Tools/Art/Blender/build_kits.ps1 -Kits hub  # un kit (hub, returnwall, ice, crops)
```

| Fichero | Qué hace |
|---|---|
| `lp_geom.py`, `lp_shapes.py` | Geometría pura (sin bpy) en cm: malla inmutable, primitivas, barridos, envolvente convexa. |
| `lp_checks.py` | Medidas del código (puerta, pared, panel), presupuestos y comprobaciones puras. |
| `kit_hub.py`, `kit_returnwall.py`, `kit_ice.py`, `kit_crops.py` | Un kit por fichero; `build()` devuelve los assets con su especificación. |
| `lp_blender.py` | Blender: escena, atributo `Col`, `M_LowPoly`, flat shading, exportación FBX. |
| `export_kits.py` | Blender: `Art/Export/<Kit>/SM_<Kit>_<Cosa>_<NN>.fbx`. Solo borra FBX propios obsoletos. |
| `fbx_reader.py`, `check_kits.py` | Python puro: lee el FBX crudo y escribe `Art/Export/kits_report.json`. |
| `contact_sheet.py` | Blender: reimporta los FBX y genera `Art/Export/sheets/kit_<kit>.png`. |

## Convenciones

- Pivote en el centro de la base (z mínima 0), +X delante, Z arriba, salvo:
  - marcos y paredes exentas: origen del portal (centro inferior del hueco en la cara visible,
    pared en X ∈ [−20, 0]); el hueco libre de 140 × 230 cm se comprueba triángulo a triángulo;
  - piezas de pared (rodapié, rejilla, enchufe, interruptor, cartel): plano de la pared en X = 0;
  - piezas de techo (placa, panel): plano del techo en Z = 0, cara vista hacia −Z, sin girar;
  - tramo de cerca: pie del poste inicial, 300 cm hacia +Y.
- **Emisivo**: alfa del atributo `Col` = 0 en las caras emisivas y 1 en el resto. En `M_LowPoly`:
  `Emissive = Col.rgb × Emissive × (1 − Col.a)`. Lo usan el difusor de `SM_Hub_LightPanel_01` y la
  cara verde y el pictograma de `SM_Hub_ExitSign_01` (verde `#2F9E5B`, fuera de paleta a propósito).

## Import en Unreal

Igual que los packs: FBX en metros (`UnitScaleFactor = 100`, lo mide `check_kits.py`), importar
con **`import_uniform_scale = 100`**, normales importadas, sin materiales del FBX (asignar
`M_LowPoly`), sin colisión del FBX. Comprobar el bounding box tras importar
(`SM_Hub_DoorFrame_01`: 27 × 186 × 252 cm). El importador de Unreal pasa a mano izquierda
invirtiendo Y: en Blender la flecha del cartel apunta a +Y; verificar tras importar hacia dónde queda.
