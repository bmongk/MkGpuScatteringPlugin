// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MkGpuScatteringSubsystem.generated.h"


struct FMkGrassVariety;

class AMkGpuScatteringVolume;
class UMkGpuScatteringBuilder;
class UMkGpuScatteringReadbackManager;

UCLASS()
class MKGPUSCATTERING_API UMkGpuScatteringSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ UTickableWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual TStatId GetStatId() const override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableInEditor() const override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~ end of UTickableWorldSubsystem


	UFUNCTION(BlueprintCallable) void FlushCache();

	UPROPERTY(EditAnywhere, BlueprintReadWrite) float PoissonRandomSeed = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector PoissonGridOrigin;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float PoissonValidDistance = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float PoissonCellSize = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PoissonDiskRowCol = 10;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PoissonDiskTestIndex = 0;
	UFUNCTION(BlueprintCallable) void PoissonDiskSamplingTest();


	UFUNCTION() void AddVolume(AMkGpuScatteringVolume* Volume);
	UFUNCTION() void RemoveVolume(AMkGpuScatteringVolume* Volume);

	UFUNCTION() void CollectVolumes();

protected:
	UFUNCTION(BlueprintCallable) bool CollectInstanceBuilder(const TArray<FVector>& Cameras, TArray<UMkGpuScatteringBuilder*>& OutInstanceBuilder);

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FMkGrassVariety> GrassVarietiesDEV;

private:
	UPROPERTY(Transient) TArray<TWeakObjectPtr<AMkGpuScatteringVolume>> Volumes;


	UPROPERTY(Transient) TArray<FVector> OldCameras;
	UPROPERTY(Transient) TArray<TObjectPtr<UMkGpuScatteringBuilder>> CurrentBuilders;
	UPROPERTY(Transient) TObjectPtr<UMkGpuScatteringReadbackManager> ReadbackManager = nullptr;

};
