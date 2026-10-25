# Packs CC0: vegetación y rocas de los biomas

Vegetación y rocas genéricas sacadas de packs CC0 y recoloreadas a la paleta de cada bioma
(`docs/ARTE.md`). El flujo está adaptado de Explored (`Tools/Packs`), sin importarlo de allí. **No
se versionan** los zips ni los FBX (`Art/Packs/`, `Art/Export/`): solo este código, el
manifiesto, el catálogo y las paletas.

| Fichero | Qué es |
|---|---|
| `packs.json` | Manifiesto: autor, página, versión, fuente (itch.io o URL directa), licencia CC0 con su evidencia y sha256 del zip. |
| `fetch_packs.py` | Descarga a `Art/Packs/` (o `$LIMINAL_PACKS_CACHE`), verifica el sha256 y extrae comprobando rutas, enlaces y tamaños. No ejecuta nada de lo descargado. |
| `palettes.json` | Paletas sRGB por bioma, copiadas de `docs/ARTE.md` (un test comprueba que coinciden). |
| `catalog.json` | Malla → fichero del pack, bioma, categoría (presupuesto y rango de medidas), escala en metros, giro y reglas de color. |
| `packs_core.py` | Lógica pura (sin `bpy`): Oklab, reglas de color, degradado, medida, pivote y validación. |
| `normalize.py` | Blender: aplica el catálogo y exporta `Art/Export/<Bioma>/SM_<Bioma>_<Cosa>_<NN>.fbx`. |
| `check_export.py` | Blender: reimporta cada FBX, lo valida y escribe `Art/Export/packs_report.json`. |
| `contact_sheet.py` | Blender: `Art/Export/sheets/packs_<bioma>.png` (encajadas) y `packs_<bioma>_escala.png` (escala real con una figura de 1,8 m). |
| `inventory.py` | Blender: medidas, triángulos y colores de candidatos del pack, con su hoja, para escribir reglas nuevas. |

## Uso

```bash
cd Tools/Art/Packs
uv run python -I fetch_packs.py              # descarga y verifica los tres packs
uv run pytest -q --cov                       # lógica pura
uv run ruff check .
cd ../../..
B="C:/Program Files/Blender Foundation/Blender 5.2/blender.exe"
"$B" -b --factory-startup --python-exit-code 1 --python Tools/Art/Packs/normalize.py      # [-- --biome Desert] [--meshes SM_X]
"$B" -b --factory-startup --python-exit-code 1 --python Tools/Art/Packs/check_export.py   # sale con 1 si algo falla
"$B" -b --factory-startup --python-exit-code 1 --python Tools/Art/Packs/contact_sheet.py  # [-- --biome Jungle]
# Para escribir las reglas de una malla nueva:
"$B" -b --factory-startup --python Tools/Art/Packs/inventory.py -- <salida.png> quaternius_ultimate_nature/OBJ/Rock_1.obj
```

## Normalización

- Solo se importan formatos de datos (OBJ de Ultimate Nature, GLB de Kenney, glTF del MegaKit);
  nunca un `.blend` del pack, porque podría traer scripts.
- Escala uniforme a `size` (metros), pivote en el centro de la huella con z mínima = 0, +X delante
  y Z arriba. Los troncos caídos se giran para quedar a lo largo de X. El arco del desierto se
  cruza por X.
- Decimado por colapso solo si se pasa del presupuesto (árbol 3.000, planta 800, roca 600), con un
  3 % de margen. Hoy solo afecta a `SM_Farmland_Bush_02` (892 → 776).
- Color: cada cara toma la muestra de la regla más cercana en Oklab, y cada esquina el color de
  esa muestra con un degradado vertical que solo toca la luminosidad Oklab (±0,04 de abajo
  arriba). Se guarda en `Col` y sale en el FBX como sRGB. Un material (`M_LowPoly`), sin
  texturas y con flat shading.
- El follaje del Stylized Nature MegaKit usa texturas con alfa y no se puede recolorear por cara.
  De ese pack solo se usa una roca, pintada con una sola muestra.

## Import en Unreal

FBX en metros (`UnitScaleFactor = 100` en el fichero, lo mide `check_export.py`). El importador
de UE 5.6 ignora la unidad: hay que importar con **`import_uniform_scale = 100`** y comprobar el
bounding box (p. ej. `SM_Jungle_Tree_01` mide 16 m de alto, 1.600 uu). Material: `M_LowPoly`
(color de vértice `Col` × `Tint`), sin importar materiales ni texturas del FBX. Normales
importadas (flat), sin colisión del FBX: la de la vegetación la decide quien la disperse.

## Problemas abiertos

- La paleta de cultivos no tiene gris de piedra: las rocas pequeñas de cultivos salen en tierra
  (`tierra_1`, `tierra_2`).
- Las flores 02 y 03 de la selva vienen de Kenney y tienen menos facetas que las de Quaternius.
- `SM_Desert_Arch_01` es la pared con cueva de Kenney engordada ×3: un arco macizo de
  10 × 10 × 5 m.
