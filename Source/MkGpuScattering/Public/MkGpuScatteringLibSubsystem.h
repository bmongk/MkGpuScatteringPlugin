// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "MkGpuScatteringLibSubsystem.generated.h"


class UTexture;

UCLASS()
class MKGPUSCATTERING_API UMkGpuScatteringLibSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void ReadHeightmap_CS(const FHitResult& InHitResult, TFunction<void(float OutputValue, FVector OutputVector)> AsyncCallback);

	// 최소한으로 호출 할 것
	UFUNCTION(BlueprintCallable) void ReadHeightmapBP_CS(const FVector& InLocation);
	void ReadHeightmapAtLocation_CS(const FVector& InLocation, TFunction<void(float OutputValue, FVector OutputVector)> AsyncCallback);

	void FindLandscapeComponentAtLocaiton(const FVector& InLocation, TFunctionRef<void(class ULandscapeComponent*, const FVector&)> Fn);
};
