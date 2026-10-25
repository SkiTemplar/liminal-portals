# Arte: low-poly estilizado

Dirección fijada por Rodrigo el 2026-10-25: **low-poly estilizado**. Facetas visibles (flat shading),
colores planos con un degradado vertical suave, siluetas claras y paleta contenida por bioma. Lo
liminal sale del orden excesivo, de la limpieza y de la luz, no del detalle.

## Origen de los assets

- **Vegetación y rocas genéricas**: packs CC0 (Quaternius *Stylized Nature MegaKit*, *Ultimate
  Nature*, Kenney *Nature Kit*) descargados de forma reproducible y recoloreados a la paleta del
  bioma. Se reutiliza el flujo de Explored (`~/PERSONAL/ProyectosPersonales/Explored/Explored/Tools/Packs`):
  manifiesto con sha256, descarga desde itch.io con el botón «No thanks», recoloreado en Oklab.
- **Lo que da identidad**: kits propios generados en Blender por script, deterministas: hub, marcos
  de puerta, pared exenta, hielo y cultivos.
- **No se versionan** los zips de los packs ni los FBX intermedios (`Art/Packs/`, `Art/Export/`):
  solo los scripts, el manifiesto y los `.uasset` importados en `Content/`.

## Convenciones de malla

- Unidades: centímetros en Unreal. El importador de UE 5.6 ignora la unidad del FBX: exportar en
  metros con escala de escena 1 e importar con `import_uniform_scale = 100` (papercut de Explored), o
  exportar ya en centímetros; comprobarlo midiendo el bounding box tras importar.
- Pivote en el centro de la base (z mínima = 0), +X hacia delante, Z arriba.
- Flat shading: normales por cara (sin suavizado) salvo en copas de árbol, donde se admiten normales
  suavizadas por grupo si quedan mejor.
- Un único material por malla: color en atributo de vértice `Col` (sRGB) que lee `M_LowPoly`
  (color de vértice × parámetro `Tint`, `Roughness` y `Emissive`). Sin texturas.
- Nombres: `SM_<Kit>_<Cosa>_<NN>` (p. ej. `SM_Jungle_Tree_01`, `SM_Hub_CeilingTile_01`).
- Presupuesto de triángulos: árbol ≤ 3.000, arbusto o planta ≤ 800, roca ≤ 600, prop del hub ≤ 1.500,
  pared exenta ≤ 2.000. La dispersión instanciada no debe subir el coste de GPU por portal más de un
  15 % respecto a la medida de E5 (4,4 ms a 1080p).

## Paletas (sRGB)

| Kit | Colores |
|---|---|
| Hub | pared `#E8DDB5`, moqueta `#B39A5A`, moqueta oscura `#8F7A42`, techo `#F2EEDD`, rejilla `#CFC8B0`, marco `#6E6A5E`, fluorescente `#FFF8E0`, rodapié `#9C927A` |
| Selva | verdes `#1F4D2B` `#2E6B36` `#4E8F3A` `#8DBF4A`, tronco `#5A3E2B`, camino `#7A6A45`, flor `#E05A47` `#F2B33D` |
| Desierto | arena `#E8C48A` `#D9A866` `#C48A4E`, roca `#A0583A` `#7E4630` `#5E3626`, cactus `#6F8F4E`, matorral seco `#9C8A5A` |
| Antártida | nieve `#F4F7FA` `#DDE7EF`, hielo `#A9D6EA` `#6FB3D6` `#3C83B0`, roca `#3A3F47` `#55606B` |
| Cultivos | trigo `#E3C565` `#C9A54A`, cultivo verde `#8DB45A` `#5E8C3A`, tierra `#7A5434` `#5C3E26`, madera `#A47B4E`, granero `#A63D2F`, heno `#D8B65A` |

Las paredes exentas de vuelta usan el color de pared del hub (es un trozo del hub en mitad del
bioma), con un zócalo teñido por el bioma (arena, nieve, musgo o barro).

## Medidas que tienen que encajar con el código

- Hueco de puerta: 140 × 230 cm (`DoorSize` en `ALiminalHub` y `ALiminalBiome`), marco de 12 cm
  (`DoorFrameThickness`, `FrameThickness` en `ALiminalPortal`), placa trasera a 60 cm detrás del plano.
- Hub: pasillo de 16 × 6 × 3,2 m, paredes de 20 cm (`ALiminalHub`).
- El marco nuevo no puede invadir el hueco: el cruce y la cápsula (radio ~34 cm) dependen de él.

## Verificación

Cada kit se acepta por script (triángulos, bounding box, pivote, un material, atributo `Col`) y con
una hoja de contacto renderizada para revisión visual (`Art/Export/sheets/<kit>.png`, ignorado; la
lámina final de UE va a `Saved/Tour/lamina.png`).
