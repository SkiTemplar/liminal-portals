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
 * Categoría de instancia dispersada. Cada una es una forma básica del motor con su material
 * (MI_Scatter_<Categoría>, creado por build_content.py) y un componente HISM propio por bioma.
 */
UENUM(BlueprintType)
enum class ELiminalScatterCategory : uint8
{
	TreeTrunk,
	TreeCrown,
	Bush,
	Undergrowth,
	RockLarge,
	RockBlock,
	Pebble,
	DryShrub,
	IceBlock,
	IcePlate,
	SnowMound,
	FencePost,
	FenceRail,
	HayBale,
	Num UMETA(Hidden),
};

/** Forma básica del motor (/Engine/BasicShapes: 100 cm, centrada en su origen). */
enum class ELiminalBasicShape : uint8
{
	Cube,
	Sphere,
	Cylinder,
	Cone,
};

namespace LiminalBiomeTypes
{
	inline constexpr int32 NumScatterCategories = static_cast<int32>(ELiminalScatterCategory::Num);
}
