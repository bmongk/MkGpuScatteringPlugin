#pragma once

#include "CoreMinimal.h"


//~ Read heightmap
struct FMkHeightmapCS_DispatchParam
{
public:
	FVector2f UVCoords = FVector2f::ZeroVector;
	UTexture* HeightmapTexture = nullptr;
};

class FMkReadHeightmapInterface
{

public:
	// Dispatches this shader. Can be called from any thread
	static void Dispatch(FMkHeightmapCS_DispatchParam Params, TFunction<void(float OutputValue, FVector OutputVector)> AsyncCallback)
	{
		if (IsInRenderingThread())
		{
			DispatchRenderThread(GetImmediateCommandList_ForRenderCommand(), Params, AsyncCallback);
		}
		else
		{
			DispatchGameThread(Params, AsyncCallback);
		}
	}

private:
	// Executes this shader on the render thread
	static void DispatchRenderThread(FRHICommandListImmediate& RHICmdList, FMkHeightmapCS_DispatchParam Params, TFunction<void(float OutputValue, FVector OutputVector)> AsyncCallback);

	// Executes this shader on the render thread from the game thread via EnqueueRenderThreadCommand
	static void DispatchGameThread(FMkHeightmapCS_DispatchParam Params, TFunction<void(float OutputValue, FVector OutputVector)> AsyncCallback)
	{
		ENQUEUE_RENDER_COMMAND(SceneDrawCompletion)(
			[Params, AsyncCallback](FRHICommandListImmediate& RHICmdList)
			{
				DispatchRenderThread(RHICmdList, Params, AsyncCallback);
			});
	}
};
//~! Read heightmap
