#pragma once

#include "CoreMinimal.h"

/**
 * Geometría pura del hub y de los biomas en blockout (etapa E3): cajas alineadas con los ejes en
 * espacio local, sin UWorld ni actores, para poder testearla aislada. Unidades en cm.
 *
 * Convenciones compartidas con ALiminalPortal (D-01): una puerta es el transform del origen del
 * portal (centro inferior del hueco, a ras de suelo) con el eje X hacia fuera de la cara visible.
 * El hueco que se deja en la pared es el exterior del marco: (ancho + 2·marco) × (alto + marco).
 */
namespace LiminalWorldLayout
{
	/** Separación en X entre biomas consecutivos (decisión 11 del plan): 20 km. */
	constexpr double BiomeSpacing = 2'000'000.0;

	/** Hueco rectangular en una pared: centro horizontal, ancho y alto medido desde el suelo. */
	struct FWallOpening
	{
		double Center = 0.0;
		double Width = 0.0;
		double Height = 0.0;
	};

	/** Hueco que necesita una puerta con su marco (exterior del marco del portal). */
	LIMINALPORTALS_API FWallOpening MakeDoorOpening(double Center, const FVector2D& DoorSize, double FrameThickness);

	/**
	 * Parte la pared [Start, End] × [0, Height] (U horizontal, Z vertical) en rectángulos que dejan
	 * libres los huecos: tramos de altura completa entre huecos y un dintel encima de cada uno.
	 * Los huecos deben caber en la pared, no solaparse y ser más bajos que ella.
	 * @return false (con OutSegments vacío) si la pared o algún hueco no son válidos.
	 */
	LIMINALPORTALS_API bool SplitWallAroundOpenings(double Start, double End, double Height,
		TConstArrayView<FWallOpening> Openings, TArray<FBox2D>& OutSegments);

	/** Puerta del hub: posición en X sobre una de las dos paredes largas. */
	struct FHubDoorSpec
	{
		double PositionX = 0.0;
		/** true: pared de +Y (la puerta mira a −Y); false: pared de −Y (mira a +Y). */
		bool bPositiveYWall = true;
	};

	/** Parámetros del hub: sala cerrada, interior centrado en el origen en XY y suelo en Z = 0. */
	struct FHubLayout
	{
		/** Interior: largo (X), ancho (Y) y alto (Z). */
		FVector InteriorSize = FVector(1600.0, 600.0, 320.0);
		double WallThickness = 20.0;
		/** Grosor del suelo y del techo. */
		double SlabThickness = 10.0;
		FVector2D DoorSize = FVector2D(140.0, 230.0);
		double DoorFrameThickness = 12.0;
		TArray<FHubDoorSpec> Doors;
		int32 NumCeilingLights = 4;
		/** Paneles fluorescentes: largo (X) y ancho (Y). */
		FVector2D LightPanelSize = FVector2D(120.0, 60.0);
		double LightPanelThickness = 2.0;
	};

	/** Cuatro puertas al tresbolillo: dos en cada pared larga, alternando lados a lo largo de X. */
	LIMINALPORTALS_API TArray<FHubDoorSpec> MakeDefaultHubDoors();

	/** Cajas del hub en espacio local. */
	struct FHubGeometry
	{
		TArray<FBox> Walls;
		FBox Floor = FBox(ForceInit);
		FBox Ceiling = FBox(ForceInit);
		TArray<FBox> LightPanels;
		/** Centro de la cara emisora de cada panel (las RectLight apuntan hacia −Z). */
		TArray<FVector> LightLocations;
		TArray<FTransform> Doors;
	};

	/**
	 * Comprueba que el hub es construible: medidas positivas, al menos una puerta, puertas más bajas
	 * que la sala, dentro de su pared (con un muro de al menos un marco hasta la esquina) y sin solaparse.
	 */
	LIMINALPORTALS_API bool IsValidHubLayout(const FHubLayout& Layout, FString* OutError = nullptr);

	/** Transform local de la puerta Index (origen del portal). @return false si el índice no existe. */
	LIMINALPORTALS_API bool GetHubDoorTransform(const FHubLayout& Layout, int32 Index, FTransform& OutTransform);

	/** @return false (con OutGeometry vacío) si el layout no es válido. */
	LIMINALPORTALS_API bool BuildHubGeometry(const FHubLayout& Layout, FHubGeometry& OutGeometry);

	/**
	 * Pared exenta de un bioma con la puerta de vuelta: la puerta está en el origen mirando a +X y la
	 * pared ocupa X ∈ [−Thickness, 0], centrada en Y, de Z = 0 a Height.
	 */
	struct FFreestandingWall
	{
		double Width = 600.0;
		double Height = 350.0;
		double Thickness = 20.0;
		FVector2D DoorSize = FVector2D(140.0, 230.0);
		double DoorFrameThickness = 12.0;
	};

	/** @return false (con OutBoxes vacío) si la puerta no cabe en la pared. */
	LIMINALPORTALS_API bool BuildFreestandingDoorWall(const FFreestandingWall& Wall, TArray<FBox>& OutBoxes);

	/** Origen del bioma número BiomeNumber (1, 2, …): BiomeNumber × BiomeSpacing en X. */
	LIMINALPORTALS_API FVector GetBiomeOrigin(int32 BiomeNumber, double Spacing = BiomeSpacing);
}
