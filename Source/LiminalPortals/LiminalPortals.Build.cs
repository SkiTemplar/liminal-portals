using UnrealBuildTool;

public class LiminalPortals : ModuleRules
{
	public LiminalPortals(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		// Sin carpetas Public/Private: las rutas de include parten de la raíz del módulo (Portal/, Player/).
		PublicIncludePaths.Add(ModuleDirectory);

		// Las cabeceras públicas exponen tipos de Engine y de Enhanced Input (ACharacter, UInputAction).
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "DeveloperSettings"
		});

		// ProceduralMeshComponent lo usa el terreno de los biomas (E4); se declara ya para fijar el plugin.
		// Json e ImageCore: informe y capturas PNG de Liminal.Calibrate (Debug/).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ProceduralMeshComponent", "Json", "ImageCore"
		});
	}
}
