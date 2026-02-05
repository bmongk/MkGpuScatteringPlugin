#include "MkGpuScatteringVolume.h"
#include "MkGpuScatteringSubsystem.h"
#include "Types/MkGpuScatteringTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MkGpuScatteringVolume)



void AMkGpuScatteringVolume::BeginPlay()
{
	Super::BeginPlay();

	if (UMkGpuScatteringSubsystem* GpuScatteringSubsystem = GetWorld()->GetSubsystem<UMkGpuScatteringSubsystem>())
	{
		GpuScatteringSubsystem->AddVolume(this);
	}
}

void AMkGpuScatteringVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UMkGpuScatteringSubsystem* GpuScatteringSubsystem = GetWorld()->GetSubsystem<UMkGpuScatteringSubsystem>())
	{
		GpuScatteringSubsystem->RemoveVolume(this);
	}
	Super::EndPlay(EndPlayReason);
}

void AMkGpuScatteringVolume::FlushCache()
{
	//
}
