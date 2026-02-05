#pragma once

#include "CoreMinimal.h"
#include "RenderUtils.h"
#include "GlobalShader.h"
#include "RHIGPUReadback.h"
#include "RenderGraphUtils.h"
#include "RenderGraphBuilder.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Types/MkGpuScatteringTypes.h"        // EMkGrassScaling, FMkGrassVariety
#include "Types/MkGpuScatteringBuilderTypes.h" // FMkGpuScatteringBuilderOutput, FMkCachedLandscapeFoliage

class UMkGpuScatteringBuilder;
class UMkGpuScatteringReadbackManager;

class ALandscapeProxy;
class ULandscapeComponent;
class UHierarchicalInstancedStaticMeshComponent;


struct FMkGpuScatteringCS_Param
{
	//
	bool bHaveValidData;
	float GrassDensity;
	FVector DrawScale;
	FVector DrawLoc;
	FMatrix LandscapeToWorld;

	FIntPoint SectionBase;
	FIntPoint LandscapeSectionOffset;
	int32 ComponentSizeQuads;
	FVector2f Origin;
	FVector2f Extent;
	FVector ComponentOrigin;

	int32 SqrtMaxInstances;

	//
	UMkGpuScatteringBuilder* Builder = nullptr;
	UTexture* HeightmapTexture = nullptr;
	UTexture* WeightmapTexture = nullptr;

	int32 WeightmapChannelIdx = -1;

	FString SpawnLayerName;
	FString BlockingLayerName;
	const FMkGrassVariety* GrassVariety = nullptr;
	//




	EMkGrassScaling Scaling;
	FFloatInterval ScaleX;
	FFloatInterval ScaleY;
	FFloatInterval ScaleZ;
	bool RandomRotation;
	bool RandomScale;
	bool AlignToSurface;

	FRandomStream RandomStream;
	FMatrix XForm;
	FBox MeshBox;
	int32 DesiredInstancesPerLeaf;

	double BuildTime;
	int32 TotalInstances;
	uint32 HaltonBaseIndex;

	bool UseLandscapeLightmap;
	FVector2D LightmapBaseBias;
	FVector2D LightmapBaseScale;
	FVector2D ShadowmapBaseBias;
	FVector2D ShadowmapBaseScale;
	FVector2D LightMapComponentBias;
	FVector2D LightMapComponentScale;

	FMkGpuScatteringCachedBuffers* CachedBuffers = nullptr;
	FMkGpuScatteringBuilderOutput BuilderOutput;

	TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISMC = nullptr;
	TWeakObjectPtr<UMkGpuScatteringReadbackManager> ReadbackManager = nullptr;

	FMkGpuScatteringCS_Param(UMkGpuScatteringBuilder* InBuilder
		, FString InSpawnLayerName
		, FString InBlockingLayerName
		, ALandscapeProxy* Landscape
		, struct FMkCachedLandscapeFoliage::FGrassComp GrassComp
		, const FMkGrassVariety* GrassVariety
		, uint32 InHaltonBaseIndex
		, int32 CachedMaxInstancesPerComponent
		, UMkGpuScatteringReadbackManager* InReadbackManager
	);

	void InitLandscapeLightmap(TWeakObjectPtr<ULandscapeComponent> Component);
};


class FMkGPUScattering_CS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMkGPUScattering_CS);
	SHADER_USE_PARAMETER_STRUCT(FMkGPUScattering_CS, FGlobalShader);

	static inline bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("USE_WEIGHTMAP"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FScatteringInput>, Input)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FProgressInfo>, RWProgressInfo)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FLocationNormalScaleZ>, RWResultBuffer)
		SHADER_PARAMETER(unsigned int, WeightmapChannelIdx)


		SHADER_PARAMETER(unsigned int, bUseVoronoiNoise)
		SHADER_PARAMETER(FVector4f, VoronoiSetting)
		SHADER_PARAMETER(FVector2f, SlopeMinMax)
		SHADER_PARAMETER(FVector2f, HeightMinMax)

		SHADER_PARAMETER(float, HeightFalloffRange)
		SHADER_PARAMETER(float, PlacementJitter)
		SHADER_PARAMETER(int, InstancingRandomSeed)
		SHADER_PARAMETER(unsigned int, UseGrid)
		SHADER_PARAMETER(unsigned int, AlignToSurface)

		SHADER_PARAMETER_TEXTURE(Texture2D, HeightmapTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HeightmapTextureSampler)

		SHADER_PARAMETER_TEXTURE(Texture2D, WeightmapTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, WeightmapTextureSampler)
	END_SHADER_PARAMETER_STRUCT()

	static inline void SetUniqueParameters(FMkGPUScattering_CS::FParameters* PassParameters, const FMkGpuScatteringCS_Param& Param);
};




class FMkGPUScatteringNoWeightmap_CS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMkGPUScatteringNoWeightmap_CS);
	SHADER_USE_PARAMETER_STRUCT(FMkGPUScatteringNoWeightmap_CS, FGlobalShader);

	static inline bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("USE_WEIGHTMAP"), 0);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FScatteringInput>, Input)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FProgressInfo>, RWProgressInfo)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FLocationNormalScaleZ>, RWResultBuffer)
		SHADER_PARAMETER(unsigned int, WeightmapChannelIdx)

		SHADER_PARAMETER(unsigned int, bUseVoronoiNoise)
		SHADER_PARAMETER(FVector4f, VoronoiSetting)
		SHADER_PARAMETER(FVector2f, SlopeMinMax)
		SHADER_PARAMETER(FVector2f, HeightMinMax)

		SHADER_PARAMETER(float, HeightFalloffRange)
		SHADER_PARAMETER(float, PlacementJitter)
		SHADER_PARAMETER(int, InstancingRandomSeed)
		SHADER_PARAMETER(unsigned int, UseGrid)
		SHADER_PARAMETER(unsigned int, AlignToSurface)

		SHADER_PARAMETER_TEXTURE(Texture2D, HeightmapTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HeightmapTextureSampler)
	END_SHADER_PARAMETER_STRUCT()

	static inline void SetUniqueParameters(FMkGPUScatteringNoWeightmap_CS::FParameters* PassParameters, const FMkGpuScatteringCS_Param& Param) {}
};


void AddPass_MkScattering(FRDGBuilder& GraphBuilder, const FMkGpuScatteringCS_Param& Param);

class FMkAsyncBuilderInterface
{
public:
	// Dispatches this shader. Can be called from any thread
	static void Dispatch(FMkGpuScatteringCS_Param Param)
	{
		if (IsInRenderingThread())
		{
			DispatchRenderThread(GetImmediateCommandList_ForRenderCommand(), Param);
		}
		else
		{
			DispatchGameThread(Param);
		}
	}

private:
	// Executes this shader on the render thread
	static void DispatchRenderThread(FRHICommandListImmediate& RHICmdList, FMkGpuScatteringCS_Param Param);

	// Executes this shader on the render thread from the game thread via EnqueueRenderThreadCommand
	static void DispatchGameThread(FMkGpuScatteringCS_Param Param);
};
