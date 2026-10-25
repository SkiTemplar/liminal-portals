#include "Misc/AutomationTest.h"
#include "Debug/LiminalGpuProfile.h"

// Tests de la parte pura del modo perfil de Liminal.Bench (D-36).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalGpuProfileNameTest, "LiminalPortals.Bench.Profile.BreadcrumbName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLiminalGpuProfileNameTest::RunTest(const FString& Parameters)
{
	using LiminalGpuProfile::NormalizeBreadcrumbName;
	TestEqual(TEXT("Raíz del fotograma"), NormalizeBreadcrumbName(TEXT("Frame 481")), FString(TEXT("Frame")));
	TestEqual(TEXT("La barra separa niveles"), NormalizeBreadcrumbName(TEXT("RenderGraphExecute - /ViewFamilies")),
		FString(TEXT("RenderGraphExecute - |ViewFamilies")));
	TestEqual(TEXT("Nombre normal"), NormalizeBreadcrumbName(TEXT("LumenScreenProbeGather")), FString(TEXT("LumenScreenProbeGather")));

	// Negativos: solo "Frame <número>" se agrupa.
	TestEqual(TEXT("Frame sin número"), NormalizeBreadcrumbName(TEXT("Frame ")), FString(TEXT("Frame ")));
	TestEqual(TEXT("Frame con texto"), NormalizeBreadcrumbName(TEXT("Frame A1")), FString(TEXT("Frame A1")));
	TestEqual(TEXT("Otro prefijo con número"), NormalizeBreadcrumbName(TEXT("Scene Frame: 12")), FString(TEXT("Scene Frame: 12")));
	TestEqual(TEXT("Minúsculas"), NormalizeBreadcrumbName(TEXT("frame 3")), FString(TEXT("frame 3")));
	return true;
}
