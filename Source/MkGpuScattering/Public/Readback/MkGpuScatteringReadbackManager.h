// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Types/MkGpuScatteringTypes.h"
#include "Types/MkGpuScatteringBuilderTypes.h"
#include "HAL/LowLevelMemTracker.h"
#include "MkGpuScatteringReadbackManager.generated.h"


class UMkGpuScatteringBuilder;
class FRHIGPUBufferReadback;
struct FMkGpuScatteringBuilderOutput;

struct FMkReadback
{
public:
	uint32 LastUsedFrameNumberRenderThread = 0;
	TFunction<void(TArray<MkGpuScatteringBuilderTypes::FLocationNormalScaleZ>&)> AsyncCallback;

	int32 Index = 0;
	int32 NextBufferSize = 0;

	bool bComplete = false;

	UMkGpuScatteringBuilder* Builder = nullptr;
	FMkGpuScatteringBuilderOutput BuilderOutput;
	mutable TArray<TRefCountPtr<FRDGPooledBuffer>> Buffers;

	TArray<FRHIGPUBufferReadback*> ReadbackPtrs;
	TArray<TFunction<void(FMkReadback& InReadback)>> ReadbackFuncs;

	//TArray<MkGpuScatteringBuilderTypes::FLocationNormalScaleZ> LocationAndNormals;

	FMkReadback() : LastUsedFrameNumberRenderThread(GFrameNumberRenderThread)
	{

	}
	FMkReadback(TFunction<void(TArray<MkGpuScatteringBuilderTypes::FLocationNormalScaleZ>&)> InAsyncCallback)
		: LastUsedFrameNumberRenderThread(GFrameNumberRenderThread), AsyncCallback(InAsyncCallback)
	{
	}

	void Clear()
	{
		/*for (FRHIGPUBufferReadback* Ptr : ReadbackPtrs)
		{
			delete(Ptr);
			Ptr = nullptr;
		}*/
		Buffers.Empty();
		ReadbackPtrs.Empty();
	}

	//void AddReadback(TRefCountPtr<FRDGPooledBuffer> Buffer, FRHIGPUBufferReadback* ReadbackPtr, TFunction<void(FMkReadback& InReadback)> ReadbackFunc);
	void AddReadback(UMkGpuScatteringBuilder* InBuilder, const FMkGpuScatteringBuilderOutput& InBuilderOutput, TRefCountPtr<FRDGPooledBuffer> Buffer, FRHIGPUBufferReadback* ReadbackPtr);

	void Touch()
	{
		//UE_LOG(LogTemp, Warning, TEXT("LastFrame %d -> %d"), LastUsedFrameNumber, GFrameNumberRenderThread);
		LastUsedFrameNumberRenderThread = GFrameNumberRenderThread;
	}

	void MarkComplete() { bComplete = true; }

	void IncrementIndex() { ++Index; }

	bool IsCompleted() const { return bComplete; }
};

UCLASS()
class MKGPUSCATTERING_API UMkGpuScatteringReadbackManager : public UObject
{
	GENERATED_BODY()

public:
	void Readback(FRHICommandListImmediate& RHICmdList);

	void AddReadback(const FMkReadback& InReadback);
	void ClearAll();

private:
	TArray<FMkReadback> ReadbackList;
};
