#include "MkGpuScatteringLibSubsystem.h"
#include "Library/MkGpuScatteringLibrary.h"
#include "MkGpuScatteringGlobal.h"

#include "LandscapeProxy.h"
#include "LandscapeInfoMap.h"
#include "LandscapeComponent.h"
#include "LandscapeHeightfieldCollisionComponent.h"

#include "DrawDebugHelpers.h"


MK_OPTIMIZATION_OFF

DECLARE_STATS_GROUP(TEXT("MkGpuScatteringLibSubsystem"), STATGROUP_MkGpuScatteringLibSubsystem, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("MkGpuScatteringLibSubsystem FindLandscape"), STAT_FindLandscape, STATGROUP_MkGpuScatteringLibSubsystem);
DECLARE_CYCLE_STAT(TEXT("MkGpuScatteringLibSubsystem ReadHeightmapAtLocation"), STAT_ReadHeightmapAtLocation, STATGROUP_MkGpuScatteringLibSubsystem);

//
void UMkGpuScatteringLibSubsystem::ReadHeightmap_CS(const FHitResult& InHitResult, TFunction<void(float OutputValue, FVector OutputVector)> AsyncCallback)
{
	ULandscapeHeightfieldCollisionComponent* HeightFieldCollisionComp = Cast<ULandscapeHeightfieldCollisionComponent>(InHitResult.GetComponent());
	if (!HeightFieldCollisionComp)
	{
		return;
	}

	TObjectPtr<ULandscapeComponent> LandscapeComp = HeightFieldCollisionComp->GetRenderComponent();
	UTexture* Heightmap = LandscapeComp->GetHeightmap();
	check(Heightmap);

	FMkHeightmapCS_DispatchParam Param;
	FVector LocalLocation = LandscapeComp->GetComponentTransform().InverseTransformPosition(InHitResult.ImpactPoint);

	FVector2f UVCoords = FVector2f(LocalLocation.X, LocalLocation.Y) / (float)(LandscapeComp->ComponentSizeQuads + 1.0f);
	Param.UVCoords = UVCoords;
	Param.HeightmapTexture = Heightmap;

	float LandscapePosZ = LandscapeComp->GetComponentLocation().Z;
	float LandscapeScaleZ = LandscapeComp->GetComponentScale().Z;
	FMkReadHeightmapInterface::Dispatch(Param, [LandscapePosZ, LandscapeScaleZ, AsyncCallback](float LocalHeight, FVector HeightmapNormal) {

		float WorldHeight = LocalHeight * LandscapeScaleZ + LandscapePosZ;

		AsyncCallback(WorldHeight, HeightmapNormal);

	});
}


void UMkGpuScatteringLibSubsystem::ReadHeightmapBP_CS(const FVector& InLocation)
{
	ReadHeightmapAtLocation_CS(InLocation, [](float WorldHeight, FVector HeightmapNormal) {

		UE_LOG(LogTemp, Warning, TEXT("WorldHeight %f, OutHeightmapNormal %s"), WorldHeight, *HeightmapNormal.ToString());

	});
}

void UMkGpuScatteringLibSubsystem::ReadHeightmapAtLocation_CS(const FVector& InLocation, TFunction<void(float OutputValue, FVector OutputVector)> AsyncCallback)
{
	SCOPE_CYCLE_COUNTER(STAT_ReadHeightmapAtLocation);

	FindLandscapeComponentAtLocaiton(InLocation, [&AsyncCallback](ULandscapeComponent* LandscapeComp, const FVector& Location) {

		//UE_LOG(LogTemp, Log, TEXT("!!!ReadHeightmapAtLocation_CS Location %s"), *Location.ToString());
		UTexture* Heightmap = LandscapeComp->GetHeightmap();
		check(Heightmap);

		FMkHeightmapCS_DispatchParam Param;
		FVector LocalLocation = LandscapeComp->GetComponentTransform().InverseTransformPosition(Location);

		FVector2f UVCoords = FVector2f(LocalLocation.X, LocalLocation.Y) / (float)(LandscapeComp->ComponentSizeQuads + 1.0f);
		Param.UVCoords = UVCoords;
		Param.HeightmapTexture = Heightmap;

		float LandscapePosZ = LandscapeComp->GetComponentLocation().Z;
		float LandscapeScaleZ = LandscapeComp->GetComponentScale().Z;
		FMkReadHeightmapInterface::Dispatch(Param, [LandscapePosZ, LandscapeScaleZ, AsyncCallback](float LocalHeight, FVector HeightmapNormal) {

			if (AsyncCallback)
			{
				float WorldHeight = LocalHeight * LandscapeScaleZ + LandscapePosZ;
				AsyncCallback(WorldHeight, HeightmapNormal);
			}

		});
	});
}

void UMkGpuScatteringLibSubsystem::FindLandscapeComponentAtLocaiton(const FVector& InLocation, TFunctionRef<void(ULandscapeComponent*, const FVector&)> Fn)
{
	SCOPE_CYCLE_COUNTER(STAT_FindLandscape);

	ULandscapeInfoMap* LandscapeInfoMap = ULandscapeInfoMap::FindLandscapeInfoMap(GetWorld());
	if (!LandscapeInfoMap)
	{
		return;
	}

	FBox CheckLocationBox = FBox::BuildAABB(InLocation, FVector::OneVector);
	for (const auto& Pair : LandscapeInfoMap->Map)
	{
		ULandscapeInfo* LandscapeInfo = Pair.Value;
		if (!LandscapeInfo)
		{
			continue;
		}

		LandscapeInfo->ForEachLandscapeProxy([&Fn, CheckLocationBox](ALandscapeProxy* Proxy) {

			for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
			{
				if (!Component)
				{
					continue;
				}

				if (Component->Bounds.GetBox().IsInsideXY(CheckLocationBox) == true)
				{
					Fn(Component, CheckLocationBox.GetCenter());

					return false;
				}
			}

			return true;

		});
	}
}
MK_OPTIMIZATION_ON
