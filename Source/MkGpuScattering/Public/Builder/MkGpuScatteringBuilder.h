// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RHIGPUReadback.h"
#include "LandscapeGrassType.h"
#include "Types/MkGpuScatteringBuilderTypes.h" // FMkCachedLandscapeFoliage
#include "Shaders/MkGpuScatteringShaders.h"
#include "MkGpuScatteringBuilder.generated.h"

class FRDGBuilder;
class ALandscapeProxy;
class FRDGPooledBuffer;
class ULandscapeComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UMkGpuScatteringTypes;

struct FMkGpuScatteringTransformBuilder;








class UMkGpuScatteringReadbackManager;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class MKGPUSCATTERING_API UMkGpuScatteringBuilder : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UMkGpuScatteringBuilder();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	UFUNCTION() void SetScatteringTypes(const TArray<UMkGpuScatteringTypes*>& InScatteringTypes);
	UFUNCTION() void SetLandscapeProxy(ALandscapeProxy* InLandscapeProxy) { LandscapeProxy = InLandscapeProxy; }
	UFUNCTION() const ALandscapeProxy* GetLandscapeProxy() { return LandscapeProxy; }
	UFUNCTION() UHierarchicalInstancedStaticMeshComponent* CreateHISMC(AActor* Owner, const FMkGrassVariety& GrassVariety, int32 InstancingRandomSeed);


	UFUNCTION() void FlushCache();

	UFUNCTION() void Build(const TArray<FVector>& Cameras, int32& InOutNumCompsCreated, UMkGpuScatteringReadbackManager* ReadbackManager);
	UFUNCTION() void WaitAndApplyResults();

	UFUNCTION() void UpdateTick(const TArray<FVector>& Cameras, float DeltaTime, int32& InOutNumComponentsCreated, UMkGpuScatteringReadbackManager* ReadbackManager);

	FORCEINLINE int32 GetGrassUpdateInterval() const
	{
		return GrassUpdateInterval;
	}

	FORCEINLINE bool ShouldTickGrass() const
	{
		const int32 UpdateInterval = GetGrassUpdateInterval();
		if (UpdateInterval > 1)
		{
			if ((GFrameNumber + FrameOffsetForTickInterval) % uint32(UpdateInterval))
			{
				return false;
			}
		}

		return true;
	}

	void OnDelegateCompueteFinish(const FMkGpuScatteringBuilderOutput& Output);

public:
	/** Frame offset for tick interval*/
	uint32 FrameOffsetForTickInterval;

protected:
	static int32 GrassUpdateInterval;

private:
	UPROPERTY(Transient) bool bPendingFlushCache = false;
	UPROPERTY(Transient) TArray<TObjectPtr<UMkGpuScatteringTypes>> ScatteringTypes;
	UPROPERTY(transient, duplicatetransient) TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> FoliageComponents;

	ALandscapeProxy* LandscapeProxy = nullptr;
	FMkAsyncBuilderInterface* AsyncBuilderInterface = nullptr;

	FMkCachedLandscapeFoliage FoliageCache;
	TArray<FMkGpuScatteringTransformBuilder*> TransformBuilders;
};
