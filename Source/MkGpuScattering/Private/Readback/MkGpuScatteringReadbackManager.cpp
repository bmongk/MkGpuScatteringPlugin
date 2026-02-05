#include "Readback/MkGpuScatteringReadbackManager.h"
#include "Types/MkGpuScatteringTypes.h"
#include "Builder/MkGpuScatteringBuilder.h"
#include "MkGpuScatteringGlobal.h"

#include "RHIGPUReadback.h"
#include "RenderGraphUtils.h"
#include "RenderGraphBuilder.h"

#include "HAL/LowLevelMemTracker.h"

//#include UE_INLINE_GENERATED_CPP_BY_NAME(MkGpuScatteringReadbackManager) // compile error


LLM_DEFINE_TAG(MkGpuScatteringReadbackManager);
LLM_DEFINE_TAG(MkGpuScatteringReadbackManager_Clear);
LLM_DEFINE_TAG(MkGpuScatteringReadbackManager_AddEnque);
LLM_DEFINE_TAG(MkGpuScatteringReadbackManager_AddReadback);
LLM_DEFINE_TAG(MkGpuScatteringReadbackManager_RemoveReadback);


MK_OPTIMIZATION_OFF

bool bShowMkReadbackLog = false;
FAutoConsoleVariableRef ShowMkReadbackDebugLogVar(TEXT("MkGpuScattering.ShowReadbackLog"), bShowMkReadbackLog, TEXT(""), ECVF_Default);

int32 MkMaxReadbackCountPerFrame = 10;
FAutoConsoleVariableRef MkMaxReadbackCountPerFrameVar(TEXT("MkGpuScattering.MaxReadbackPerFrame"), MkMaxReadbackCountPerFrame, TEXT(""), ECVF_Default);

int32 MkReadbackDelayFrameCount = 2;
FAutoConsoleVariableRef MkReadbackDelayFrameCountVar(TEXT("MkGpuScattering.ReadbackDelayFrameCount"), MkReadbackDelayFrameCount, TEXT(""), ECVF_Default);

using namespace MkGpuScatteringBuilderTypes;

//~ FMkReadback
//void FMkReadback::AddReadback(TRefCountPtr<FRDGPooledBuffer> Buffer, FRHIGPUBufferReadback* ReadbackPtr, TFunction<void(FMkReadback& InReadback)> ReadbackFunc)
void FMkReadback::AddReadback(UMkGpuScatteringBuilder* InBuilder, const FMkGpuScatteringBuilderOutput& InBuilderOutput, TRefCountPtr<FRDGPooledBuffer> Buffer, FRHIGPUBufferReadback* ReadbackPtr)
{
	Builder = InBuilder;
	BuilderOutput = InBuilderOutput;

	Buffers.Add(Buffer);
	ReadbackPtrs.Add(ReadbackPtr);
}
//~ end of FMkReadback

void UMkGpuScatteringReadbackManager::ClearAll()
{
	LLM_SCOPE_BYTAG(MkGpuScatteringReadbackManager_Clear);
	for(FMkReadback Readback : ReadbackList)
	{
		Readback.Clear();
	}
	ReadbackList.Empty();
}

//~ UMkGpuScatteringReadbackManager
void UMkGpuScatteringReadbackManager::Readback(FRHICommandListImmediate& RHICmdList)
{
	if (ReadbackList.IsEmpty())
	{
		return;
	}

	LLM_SCOPE_BYTAG(MkGpuScatteringReadbackManager);

	int32 MaxLoop = ReadbackList.Num() > MkMaxReadbackCountPerFrame ? FMath::Max(1, ReadbackList.Num() / MkMaxReadbackCountPerFrame) : ReadbackList.Num();

	for (int32 Index = 0; Index < MaxLoop; ++Index)
	{
		FMkReadback& Readback = ReadbackList[Index];
		int32 ReadbackIndex = Readback.Index;

		if (Readback.LastUsedFrameNumberRenderThread + MkReadbackDelayFrameCount > GFrameNumberRenderThread)
		{
			continue;
		}


		//
		if (!Readback.ReadbackPtrs.IsValidIndex(ReadbackIndex))
		{
			continue;
		}


		FRHIGPUBufferReadback* ReadbackPtr = Readback.ReadbackPtrs[ReadbackIndex];
		TRefCountPtr<FRDGPooledBuffer> ReadbackBuffer = Readback.Buffers[ReadbackIndex];
		if (ReadbackIndex == 0)
		{
			if (ReadbackPtr->IsReady())
			{
				void* Buffer = (void*)ReadbackPtr->Lock(1);
				FProgressInfo ProgressInfo;
				FPlatformMemory::Memcpy(&ProgressInfo, Buffer, sizeof(FProgressInfo) * 1);
				ReadbackPtr->Unlock();

				if (ProgressInfo.Count >= ProgressInfo.MaxInstances)
				{
					Readback.NextBufferSize = ProgressInfo.MaxInstances;
					Readback.IncrementIndex();
				}

				ReadbackBuffer.SafeRelease();
				delete(ReadbackPtr);
				continue;
			}
		}
		else
		{
			if (ReadbackPtr->IsReady())
			{
				int32 CurrentBufferSize = Readback.NextBufferSize;

				void* Buffer = (void*)ReadbackPtr->Lock(1);
				TArray<FLocationNormalScaleZ> ResultBuffer;
				ResultBuffer.SetNumZeroed(CurrentBufferSize);
				FPlatformMemory::Memcpy(ResultBuffer.GetData(), Buffer, sizeof(FLocationNormalScaleZ) * CurrentBufferSize);
				ReadbackPtr->Unlock();

#if !UE_BUILD_SHIPPING
				if (bShowMkReadbackLog)
				{
					TMap<float, int32> DebugCount;
					for (auto& buff : ResultBuffer)
					{
						if (buff.ComputedNormal.Z > 1)
						{
							DebugCount.FindOrAdd(buff.ComputedNormal.Z)++;
						}
					}

					for (auto& KV : DebugCount)
					{
						UE_LOG(LogTemp, Log, TEXT("ErrorCode %f,  Count %d"), KV.Key, KV.Value);
					}
				}
#endif

				int32 BeforeCount = ResultBuffer.Num();
				ResultBuffer.RemoveAll([](FLocationNormalScaleZ& Data) { return Data.ComputedNormal.Z > 1; });
				int32 AfterCount = ResultBuffer.Num();
				//UE_LOG(LogTemp, Log, TEXT("Readback before %d -> After %d"), BeforeCount, AfterCount);

				ReadbackBuffer.SafeRelease();
				delete(ReadbackPtr);

				Readback.MarkComplete();

				if (Readback.Builder)
				{
					Readback.BuilderOutput.ResultBuffer = MoveTemp(ResultBuffer);
					Readback.Builder->OnDelegateCompueteFinish(Readback.BuilderOutput);
				}
				{
					LLM_SCOPE_BYTAG(MkGpuScatteringReadbackManager_RemoveReadback);
					if (!ReadbackList.IsValidIndex(Index))
					{
						break;
					}
					ReadbackList.RemoveAtSwap(Index--);
					MaxLoop = ReadbackList.Num() > 10 ? FMath::Max(1, ReadbackList.Num() / 10) : ReadbackList.Num();
				}
				continue;
			}
		}

		{
			LLM_SCOPE_BYTAG(MkGpuScatteringReadbackManager_AddEnque);
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGBufferRef Buffer = GraphBuilder.RegisterExternalBuffer(ReadbackBuffer);
			AddEnqueueCopyPass(GraphBuilder, ReadbackPtr, Buffer, 0);
			Readback.Touch();
			GraphBuilder.Execute();
		}
	}

}

void UMkGpuScatteringReadbackManager::AddReadback(const FMkReadback& InReadback)
{
	LLM_SCOPE_BYTAG(MkGpuScatteringReadbackManager_AddReadback);
	ReadbackList.Add(InReadback);
}

//TArray<FMkReadback>& UMkGpuScatteringReadbackManager::GetReadbackList()
//{
//	return ReadbackList;
//}
//~ end of UMkGpuScatteringReadbackManager

MK_OPTIMIZATION_ON
