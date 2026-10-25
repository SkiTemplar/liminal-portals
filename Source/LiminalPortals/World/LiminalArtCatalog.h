#pragma once

#include "CoreMinimal.h"
#include "World/LiminalBiomeTypes.h"

/**
 * Catálogo del arte low-poly (docs/ARTE.md): qué mallas de /Game/Art usa cada categoría de dispersión
 * (sus variantes), cada pieza del hub y cada pared de vuelta, y cómo se instancian (colisión, distancia
 * de desaparición, sombra). Es la tabla editable del mapeo: añadir una variante es añadir su nombre
 * aquí (la dispersión elige entre todas las de la categoría). Funciones puras, sin cargar assets.
 *
 * Las mallas las importa Tools/Content/import_art.py con el nombre SM_<Kit>_<Cosa>_<NN> en
 * /Game/Art/<Kit>/; la ruta se deduce del nombre.
 */
namespace LiminalArtCatalog
{
	/** Piezas del hub (cada una es un ISM del hub). */
	enum class EHubPiece : uint8
	{
		DoorFrame,
		Carpet,
		CarpetBorder,
		CeilingTile,
		CeilingTileStained,
		LightPanel,
		Baseboard,
		Vent,
		Outlet,
		Switch,
		ExitSign,
		Chair,
		Num,
	};

	inline constexpr int32 NumHubPieces = static_cast<int32>(EHubPiece::Num);

	/** Nombres de las mallas de la categoría, en el orden de su índice de variante (nunca vacío). */
	LIMINALPORTALS_API TConstArrayView<const TCHAR*> GetVariantNames(ELiminalScatterCategory Category);

	LIMINALPORTALS_API int32 GetNumVariants(ELiminalScatterCategory Category);

	/** Malla de la pieza del hub. */
	LIMINALPORTALS_API const TCHAR* GetHubPieceName(EHubPiece Piece);

	/** Pared exenta de la puerta de vuelta de cada bioma (zócalo de musgo, arena, nieve o barro). */
	LIMINALPORTALS_API const TCHAR* GetReturnWallName(ELiminalBiomeType Type);

	/**
	 * Ruta de objeto de una malla del catálogo: SM_<Kit>_<Resto> → /Game/Art/<Kit>/SM_….SM_….
	 * @return Cadena vacía si el nombre no sigue la convención.
	 */
	LIMINALPORTALS_API FString MakeMeshObjectPath(const FString& MeshName);

	/** Todos los nombres del catálogo, sin repetir (dispersión, hub y paredes de vuelta). */
	LIMINALPORTALS_API TArray<FString> GetAllMeshNames();

	/** Categorías que bloquean: troncos, rocas, bloques de hielo, hitos y granero (la malla trae la colisión simple). */
	LIMINALPORTALS_API bool HasCollision(ELiminalScatterCategory Category);

	/** Distancia de desaparición en cm (0 = sin límite) de lo pequeño o numeroso. */
	LIMINALPORTALS_API double GetCullDistance(ELiminalScatterCategory Category);

	/** Si proyecta sombra dinámica (lo diminuto no: flores, guijarros, piedras sueltas). */
	LIMINALPORTALS_API bool CastsShadow(ELiminalScatterCategory Category);

	/** Si entra en la iluminación por distance fields (Lumen): no lo pequeño y numeroso (sotobosque, cultivos). */
	LIMINALPORTALS_API bool AffectsDistanceFieldLighting(ELiminalScatterCategory Category);
}
