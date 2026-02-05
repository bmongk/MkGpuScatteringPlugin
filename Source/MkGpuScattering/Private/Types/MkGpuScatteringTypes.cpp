#include "Types/MkGpuScatteringTypes.h"
#include "MkGpuScatteringSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MkGpuScatteringTypes)


const TCHAR* GMkGrassQualityLevelCVarName = TEXT("mk.grass.DensityQualityLevel");
const TCHAR* GMkGrassQualityLevelScalabilitySection = TEXT("ViewDistanceQuality");

int32 GMkGrassQualityLevel = -1;
static FAutoConsoleVariableRef CVarGMkGrassDensityQualityLevelCVar(
	GMkGrassQualityLevelCVarName,
	GMkGrassQualityLevel,
	TEXT("The quality level for grass (low, medium, high, epic). \n"),
	ECVF_Scalability);

FMkGrassVariety::FMkGrassVariety()
	: GrassMesh(nullptr)
	, GrassDensityQuality(400.0f)
	, bUseGrid(true)
	, PlacementJitter(1.0f)
	, StartCullDistanceQuality(10000)
	, EndCullDistanceQuality(10000)
	, MinLOD(-1)
	, Scaling(EMkGrassScaling::Uniform)
	, ScaleX(1.0f, 1.0f)
	, ScaleY(1.0f, 1.0f)
	, ScaleZ(1.0f, 1.0f)
	, Slope(0.0f, 90.0f)
	, Height(-1000000.0, 1000000.0)
	, HeightFalloffRange(1000.0f)
	, ZOffset(0.0f, 0.0f)
	, bUseVoronoiNoise(false)
	, VoronoiValidRange(0.0f, 1.0f)
	, VoronoiScale(10.0f)
	, VoronoiGroupSize(8192.0f)
	, AlignToSurface(true)
	, AlignMaxAngle(0.0f)
	, RandomRotation(true)
	, RotationAxis(0.0f, 1.0f, 0.0f)
	, bCheckCloseLandscape(false)
	, bUseLandscapeLightmap(false)
	, bReceivesDecals(true)
	, bAffectDistanceFieldLighting(false)
	, bCastDynamicShadow(true)
	, bCastContactShadow(true)
	, bKeepInstanceBufferCPUCopy(false)
	/*, SpawnLayerName(TEXT("None"))
	, BlockingLayerName(TEXT("None"))*/
	, bEvaluateWorldPositionOffset(true)
	, InstanceWorldPositionOffsetDisableDistance(0)
	, ShadowCacheInvalidationBehavior(EShadowCacheInvalidationBehavior::Auto)
{

	// UE5.4 에서 warning 발생
	// warning C4996: 'FPerQualityLevelProperty<FPerQualityLevelFloat,float,EName::FloatProperty>::Init': If no cvar is associated with the property, all quality levels will be keept when cooking. Call SetQualtiyLevelCVarForCooking to strip unsupported quality levels when cooking Please update your code to the new API before upgrading to the next release, otherwise your project will no longer compile.
	//GrassDensityQuality.Init(GMkGrassQualityLevelCVarName, GMkGrassQualityLevelScalabilitySection);
	//StartCullDistanceQuality.Init(GMkGrassQualityLevelCVarName, GMkGrassQualityLevelScalabilitySection);
	//EndCullDistanceQuality.Init(GMkGrassQualityLevelCVarName, GMkGrassQualityLevelScalabilitySection);
}

bool FMkGrassVariety::IsGrassQualityLevelEnable() const
{
	return (GEngine && GEngine->UseGrassVarityPerQualityLevels);
}

int32 FMkGrassVariety::GetStartCullDistance() const
{
	if (IsGrassQualityLevelEnable())
	{
		return StartCullDistanceQuality.GetValue(GMkGrassQualityLevel);
	}
	else
	{
		return StartCullDistanceQuality.GetValue(-1);
	}
}

int32 FMkGrassVariety::GetEndCullDistance() const
{
	if (IsGrassQualityLevelEnable())
	{
		return EndCullDistanceQuality.GetValue(GMkGrassQualityLevel);
	}
	else
	{
		return EndCullDistanceQuality.GetValue(-1);
	}
}

float FMkGrassVariety::GetDensity() const
{
	if (IsGrassQualityLevelEnable())
	{
		return GrassDensityQuality.GetValue(GMkGrassQualityLevel);
	}
	else
	{
		return GrassDensityQuality.GetValue(-1);
	}
}

#if WITH_EDITOR
void UMkGpuScatteringTypes::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	/*if (bGenerate)
	{

		UMkGpuScatteringSubsystem* GpuScatteringSubsystem = GetWorld()->GetSubsystem<UMkGpuScatteringSubsystem>();
		if (GpuScatteringSubsystem)
		{
			GpuScatteringSubsystem->FlushCache();
		}

		bGenerate = false;
	}*/

}
#endif
