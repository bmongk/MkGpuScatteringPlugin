#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "MkGpuScatteringVolume.generated.h"


class UMkGpuScatteringTypes;

UCLASS(Blueprintable)
class MKGPUSCATTERING_API AMkGpuScatteringVolume : public AVolume
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION() void FlushCache();
	FORCEINLINE const TArray<UMkGpuScatteringTypes*> GetScatteringTypes() const { return ScatteringTypes; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MkGpuScattering") TArray<TObjectPtr<UMkGpuScatteringTypes>> ScatteringTypes;
};
