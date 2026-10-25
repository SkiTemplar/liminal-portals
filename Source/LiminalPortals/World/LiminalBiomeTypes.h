#pragma once

#include "CoreMinimal.h"
#include "LiminalBiomeTypes.generated.h"

/** Tipo de bioma: decide el campo de alturas, la paleta del terreno y la dispersión (E4). */
UENUM(BlueprintType)
enum class ELiminalBiomeType : uint8
{
	Jungle,
	Desert,
	Antarctic,
	Farmland,
};

/**
 * Categoría de instancia dispersada. Cada una tiene una o varias mallas low-poly (variantes) de
 * /Game/Art, listadas en LiminalArtCatalog; la dispersión elige la variante de cada instancia de forma
 * determinista y ALiminalBiome crea un HISM por categoría y variante.
 */
UENUM(BlueprintType)
enum class ELiminalScatterCategory : uint8
{
	JungleTree,
	JungleBush,
	JungleFern,
	JungleFlower,
	JungleLog,
	DesertMesa,
	DesertArch,
	DesertRock,
	DesertPebble,
	Cactus,
	DryShrub,
	IceHorizon,
	PressureRidge,
	IceBlock,
	IceShard,
	SnowMound,
	Outcrop,
	Wheat,
	Corn,
	CropRow,
	FenceSection,
	FencePost,
	HayBale,
	Scarecrow,
	Barn,
	FarmTree,
	FarmBush,
	FarmRock,
	Num UMETA(Hidden),
};

namespace LiminalBiomeTypes
{
	inline constexpr int32 NumScatterCategories = static_cast<int32>(ELiminalScatterCategory::Num);
}
