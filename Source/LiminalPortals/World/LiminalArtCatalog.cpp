#include "World/LiminalArtCatalog.h"

// Namespace con nombre propio (D-22): el unity build junta todos los .cpp del módulo.
namespace LiminalArtCatalogDetail
{
	using ECat = ELiminalScatterCategory;

	const TCHAR* const JungleTrees[] = { TEXT("SM_Jungle_Tree_01"), TEXT("SM_Jungle_Tree_02"), TEXT("SM_Jungle_Tree_03"),
		TEXT("SM_Jungle_Tree_04"), TEXT("SM_Jungle_Tree_05") };
	const TCHAR* const JungleBushes[] = { TEXT("SM_Jungle_Bush_01"), TEXT("SM_Jungle_Bush_02"), TEXT("SM_Jungle_Bush_03") };
	const TCHAR* const JungleFerns[] = { TEXT("SM_Jungle_Fern_01"), TEXT("SM_Jungle_Fern_02"), TEXT("SM_Jungle_Fern_03") };
	const TCHAR* const JungleFlowers[] = { TEXT("SM_Jungle_Flower_01"), TEXT("SM_Jungle_Flower_02"), TEXT("SM_Jungle_Flower_03") };
	const TCHAR* const JungleLogs[] = { TEXT("SM_Jungle_Log_01"), TEXT("SM_Jungle_Log_02"), TEXT("SM_Jungle_Roots_01") };
	const TCHAR* const DesertMesas[] = { TEXT("SM_Desert_Rock_01") };
	const TCHAR* const DesertArches[] = { TEXT("SM_Desert_Arch_01") };
	const TCHAR* const DesertRocks[] = { TEXT("SM_Desert_Rock_02"), TEXT("SM_Desert_Rock_03") };
	const TCHAR* const DesertPebbles[] = { TEXT("SM_Desert_Rock_04"), TEXT("SM_Desert_Rock_05") };
	const TCHAR* const Cacti[] = { TEXT("SM_Desert_Cactus_01"), TEXT("SM_Desert_Cactus_02"), TEXT("SM_Desert_Cactus_03") };
	const TCHAR* const DryShrubs[] = { TEXT("SM_Desert_Shrub_01"), TEXT("SM_Desert_Shrub_02") };
	const TCHAR* const IceHorizons[] = { TEXT("SM_Ice_Iceberg_01"), TEXT("SM_Ice_Iceberg_02"), TEXT("SM_Ice_Serac_01") };
	const TCHAR* const PressureRidges[] = { TEXT("SM_Ice_PressureRidge_01") };
	const TCHAR* const IceBlocks[] = { TEXT("SM_Ice_Block_01"), TEXT("SM_Ice_Block_02"), TEXT("SM_Ice_Block_03"), TEXT("SM_Ice_Block_04") };
	const TCHAR* const IceShards[] = { TEXT("SM_Ice_Shard_01"), TEXT("SM_Ice_Shard_02"), TEXT("SM_Ice_Shard_03") };
	const TCHAR* const SnowMounds[] = { TEXT("SM_Ice_SnowMound_01"), TEXT("SM_Ice_SnowMound_02"), TEXT("SM_Ice_SnowMound_03") };
	const TCHAR* const Outcrops[] = { TEXT("SM_Antarctic_Rock_01"), TEXT("SM_Antarctic_Rock_02"), TEXT("SM_Antarctic_Rock_03"),
		TEXT("SM_Antarctic_Rock_04") };
	const TCHAR* const Wheats[] = { TEXT("SM_Crops_Wheat_01"), TEXT("SM_Crops_Wheat_02") };
	const TCHAR* const Corns[] = { TEXT("SM_Crops_Corn_01") };
	const TCHAR* const CropRows[] = { TEXT("SM_Crops_CropRow_01") };
	const TCHAR* const FenceSections[] = { TEXT("SM_Crops_FenceSection_01") };
	const TCHAR* const FencePosts[] = { TEXT("SM_Crops_FencePost_01") };
	const TCHAR* const HayBales[] = { TEXT("SM_Crops_HayBaleRound_01"), TEXT("SM_Crops_HayBaleSquare_01") };
	const TCHAR* const Scarecrows[] = { TEXT("SM_Crops_Scarecrow_01") };
	const TCHAR* const Barns[] = { TEXT("SM_Crops_Barn_01") };
	const TCHAR* const FarmTrees[] = { TEXT("SM_Farmland_Tree_01"), TEXT("SM_Farmland_Tree_02"), TEXT("SM_Farmland_Tree_03") };
	const TCHAR* const FarmBushes[] = { TEXT("SM_Farmland_Bush_01"), TEXT("SM_Farmland_Bush_02") };
	const TCHAR* const FarmRocks[] = { TEXT("SM_Farmland_Rock_01"), TEXT("SM_Farmland_Rock_02") };

	const TCHAR* const HubPieces[] = {
		TEXT("SM_Hub_DoorFrame_01"),
		TEXT("SM_Hub_Carpet_01"),
		TEXT("SM_Hub_Carpet_02"),
		TEXT("SM_Hub_CeilingTile_01"),
		TEXT("SM_Hub_CeilingTile_02"),
		TEXT("SM_Hub_LightPanel_01"),
		TEXT("SM_Hub_Baseboard_01"),
		TEXT("SM_Hub_Vent_01"),
		TEXT("SM_Hub_Outlet_01"),
		TEXT("SM_Hub_Switch_01"),
		TEXT("SM_Hub_ExitSign_01"),
		TEXT("SM_Hub_Chair_01"),
	};
	static_assert(UE_ARRAY_COUNT(HubPieces) == LiminalArtCatalog::NumHubPieces, "Una malla por pieza del hub");

	template <int32 N>
	TConstArrayView<const TCHAR*> View(const TCHAR* const (&Names)[N])
	{
		return TConstArrayView<const TCHAR*>(Names, N);
	}
}

namespace LiminalArtCatalog
{
	TConstArrayView<const TCHAR*> GetVariantNames(ELiminalScatterCategory Category)
	{
		using namespace LiminalArtCatalogDetail;
		switch (Category)
		{
		case ECat::JungleTree: return View(JungleTrees);
		case ECat::JungleBush: return View(JungleBushes);
		case ECat::JungleFern: return View(JungleFerns);
		case ECat::JungleFlower: return View(JungleFlowers);
		case ECat::JungleLog: return View(JungleLogs);
		case ECat::DesertMesa: return View(DesertMesas);
		case ECat::DesertArch: return View(DesertArches);
		case ECat::DesertRock: return View(DesertRocks);
		case ECat::DesertPebble: return View(DesertPebbles);
		case ECat::Cactus: return View(Cacti);
		case ECat::DryShrub: return View(DryShrubs);
		case ECat::IceHorizon: return View(IceHorizons);
		case ECat::PressureRidge: return View(PressureRidges);
		case ECat::IceBlock: return View(IceBlocks);
		case ECat::IceShard: return View(IceShards);
		case ECat::SnowMound: return View(SnowMounds);
		case ECat::Outcrop: return View(Outcrops);
		case ECat::Wheat: return View(Wheats);
		case ECat::Corn: return View(Corns);
		case ECat::CropRow: return View(CropRows);
		case ECat::FenceSection: return View(FenceSections);
		case ECat::FencePost: return View(FencePosts);
		case ECat::HayBale: return View(HayBales);
		case ECat::Scarecrow: return View(Scarecrows);
		case ECat::Barn: return View(Barns);
		case ECat::FarmTree: return View(FarmTrees);
		case ECat::FarmBush: return View(FarmBushes);
		case ECat::FarmRock: return View(FarmRocks);
		default: return View(FarmRocks);
		}
	}

	int32 GetNumVariants(ELiminalScatterCategory Category)
	{
		return GetVariantNames(Category).Num();
	}

	const TCHAR* GetHubPieceName(EHubPiece Piece)
	{
		const int32 Index = FMath::Clamp(static_cast<int32>(Piece), 0, NumHubPieces - 1);
		return LiminalArtCatalogDetail::HubPieces[Index];
	}

	const TCHAR* GetReturnWallName(ELiminalBiomeType Type)
	{
		switch (Type)
		{
		case ELiminalBiomeType::Jungle: return TEXT("SM_ReturnWall_Moss_01");
		case ELiminalBiomeType::Desert: return TEXT("SM_ReturnWall_Sand_01");
		case ELiminalBiomeType::Antarctic: return TEXT("SM_ReturnWall_Snow_01");
		default: return TEXT("SM_ReturnWall_Mud_01");
		}
	}

	FString MakeMeshObjectPath(const FString& MeshName)
	{
		TArray<FString> Parts;
		MeshName.ParseIntoArray(Parts, TEXT("_"));
		if (Parts.Num() < 3 || Parts[0] != TEXT("SM") || Parts[1].IsEmpty())
		{
			return FString();
		}
		return FString::Printf(TEXT("/Game/Art/%s/%s.%s"), *Parts[1], *MeshName, *MeshName);
	}

	TArray<FString> GetAllMeshNames()
	{
		TArray<FString> Names;
		for (int32 Index = 0; Index < LiminalBiomeTypes::NumScatterCategories; ++Index)
		{
			for (const TCHAR* Name : GetVariantNames(static_cast<ELiminalScatterCategory>(Index)))
			{
				Names.AddUnique(Name);
			}
		}
		for (int32 Index = 0; Index < NumHubPieces; ++Index)
		{
			Names.AddUnique(GetHubPieceName(static_cast<EHubPiece>(Index)));
		}
		for (const ELiminalBiomeType Type : { ELiminalBiomeType::Jungle, ELiminalBiomeType::Desert, ELiminalBiomeType::Antarctic, ELiminalBiomeType::Farmland })
		{
			Names.AddUnique(GetReturnWallName(Type));
		}
		return Names;
	}

	bool HasCollision(ELiminalScatterCategory Category)
	{
		using ECat = ELiminalScatterCategory;
		switch (Category)
		{
		case ECat::JungleTree:
		case ECat::JungleLog:
		case ECat::DesertMesa:
		case ECat::DesertArch:
		case ECat::DesertRock:
		case ECat::IceHorizon:
		case ECat::IceBlock:
		case ECat::Outcrop:
		case ECat::Barn:
		case ECat::FarmTree:
			return true;
		default:
			return false;
		}
	}

	double GetCullDistance(ELiminalScatterCategory Category)
	{
		using ECat = ELiminalScatterCategory;
		switch (Category)
		{
		case ECat::JungleFlower: return 3000.0;
		case ECat::JungleFern: return 4500.0;
		case ECat::FarmRock: return 4000.0;
		case ECat::DesertPebble: return 5000.0;
		case ECat::Wheat: return 6000.0;
		case ECat::CropRow: return 6000.0;
		case ECat::IceShard: return 6000.0;
		case ECat::DryShrub: return 7000.0;
		case ECat::Corn: return 9000.0;
		case ECat::FarmBush: return 9000.0;
		case ECat::JungleBush: return 12000.0;
		case ECat::JungleLog: return 15000.0;
		case ECat::SnowMound: return 15000.0;
		case ECat::FenceSection: return 15000.0;
		case ECat::FencePost: return 15000.0;
		case ECat::HayBale: return 15000.0;
		default: return 0.0;
		}
	}

	bool CastsShadow(ELiminalScatterCategory Category)
	{
		using ECat = ELiminalScatterCategory;
		return Category != ECat::JungleFlower && Category != ECat::DesertPebble && Category != ECat::FarmRock;
	}

	bool AffectsDistanceFieldLighting(ELiminalScatterCategory Category)
	{
		using ECat = ELiminalScatterCategory;
		switch (Category)
		{
		case ECat::JungleFern:
		case ECat::JungleFlower:
		case ECat::DesertPebble:
		case ECat::DryShrub:
		case ECat::IceShard:
		case ECat::Wheat:
		case ECat::Corn:
		case ECat::CropRow:
		case ECat::FarmRock:
			return false;
		default:
			return true;
		}
	}
}
