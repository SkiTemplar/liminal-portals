#pragma once

#include "CoreMinimal.h"
#include "World/LiminalArtCatalog.h"
#include "World/LiminalWorldLayout.h"

/**
 * Atrezo del hub con el kit low-poly (docs/ARTE.md): marcos de puerta, moqueta modular, techo de placas
 * con los paneles fluorescentes alineados con las RectLight, rodapiés, rejillas, enchufes, interruptor,
 * cartel de salida y una silla. Funciones puras: transforms locales del hub (origen en el centro del
 * suelo) por pieza; ALiminalHub crea un ISM por pieza.
 *
 * Pivotes de las mallas (Tools/Art/Blender/README.md): marco en el origen del portal; piezas de pared
 * con el plano de la pared en X = 0 y la cara vista hacia +X; piezas de techo con el plano del techo en
 * Z = 0 y la cara vista hacia −Z; moqueta y silla en el centro de la base.
 */
namespace LiminalHubDressing
{
	/** Lado de la losa de moqueta y de la placa de techo, largo del rodapié, en cm. */
	inline constexpr double CarpetTileSize = 100.0;
	inline constexpr double CeilingTileSize = 60.0;
	inline constexpr double BaseboardLength = 100.0;
	/** Medio ancho del marco con tapajuntas (SM_Hub_DoorFrame_01: 186 cm): el rodapié termina ahí. */
	inline constexpr double DoorCasingHalfWidth = 93.0;
	/** Alto del marco con tapajuntas (252 cm): el cartel de salida va encima. */
	inline constexpr double DoorCasingHeight = 252.0;

	struct FHubDressing
	{
		TArray<FTransform> Pieces[LiminalArtCatalog::NumHubPieces];

		TArray<FTransform>& Get(LiminalArtCatalog::EHubPiece Piece) { return Pieces[static_cast<int32>(Piece)]; }
		const TArray<FTransform>& Get(LiminalArtCatalog::EHubPiece Piece) const { return Pieces[static_cast<int32>(Piece)]; }
		LIMINALPORTALS_API int32 Num() const;
	};

	/**
	 * Tramos libres de [Start, End] tras quitar los intervalos [c − HalfWidth, c + HalfWidth] de cada
	 * centro (recortados a la pared y fusionados si se solapan). Devuelve (inicio, fin) por tramo.
	 */
	LIMINALPORTALS_API TArray<FVector2D> FreeWallIntervals(double Start, double End, TConstArrayView<double> Centers, double HalfWidth);

	/** @return false (con OutDressing vacío) si el layout no es válido. */
	LIMINALPORTALS_API bool BuildHubDressing(const LiminalWorldLayout::FHubLayout& Layout, FHubDressing& OutDressing);
}
