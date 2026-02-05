#include "Shaders/MkGpuScatteringShaders.h"
#include "Types/MkGpuScatteringTypes.h"
#include "Builder/MkGpuScatteringBuilder.h"
#include "Readback/MkGpuScatteringReadbackManager.h"
#include "MkGpuScatteringGlobal.h"

#include "ShadowMap.h"
#include "LandscapeLight.h"
#include "LandscapeProxy.h"
#include "LandscapeComponent.h"
#include "LandscapeGrassType.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

#include "HAL/LowLevelMemTracker.h"

LLM_DEFINE_TAG(MkGpuScatteringShaders);
LLM_DEFINE_TAG(MkGpuScatteringDispatch);

using namespace MkGpuScatteringBuilderTypes;

IMPLEMENT_GLOBAL_SHADER(FMkGPUScattering_CS, "/MkGPUPlacementShaders/GPUScattering_CS.usf", "Scattering_CS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FMkGPUScatteringNoWeightmap_CS, "/MkGPUPlacementShaders/GPUScattering_CS.usf", "Scattering_CS", SF_Compute);

MK_OPTIMIZATION_OFF

int32 MkThreadNum_ScatteringCS = 32;
static FAutoConsoleVariableRef MkThreadNum_ScatteringCSVar(
	TEXT("MkGpuScattering.ThreadCount"),
	MkThreadNum_ScatteringCS,
	TEXT(""));

//~ MkGpuScatteringBuilderParam

// LandscapeGrass.cpp 참고


FMkGpuScatteringCS_Param::FMkGpuScatteringCS_Param(
	UMkGpuScatteringBuilder* InBuilder
	, FString InSpawnLayerName
	, FString InBlockingLayerName
	, ALandscapeProxy* Landscape
	, FMkCachedLandscapeFoliage::FGrassComp GrassComp
	, const FMkGrassVariety* InGrassVariety
	, uint32 InHaltonBaseIndex
	, int32 CachedMaxInstancesPerComponent
	, UMkGpuScatteringReadbackManager* InReadbackManager
)
{
	Builder = InBuilder;
	GrassVariety = InGrassVariety;
	HaltonBaseIndex = InHaltonBaseIndex;
	ReadbackManager = InReadbackManager;

	//~ LandscapeProxy info
	DrawScale = Landscape->GetRootComponent()->GetRelativeScale3D();
	DrawLoc = Landscape->GetActorLocation();
	LandscapeSectionOffset = Landscape->LandscapeSectionOffset;
	LandscapeToWorld = Landscape->GetRootComponent()->GetComponentTransform().ToMatrixNoScale();
	//~ end of LandscapeProxy info

	FMkCachedLandscapeFoliage::FGrassCompKey GrassCompKey = GrassComp.Key;
	int32 SqrtSubsections = GrassCompKey.SqrtSubsections;
	int32 SubX = GrassCompKey.SubsectionX;
	int32 SubY = GrassCompKey.SubsectionY;
	int32 NumVarieties = GrassCompKey.NumVarieties;
	int32 VarietyIndex = GrassCompKey.VarietyIndex;

	Scaling = GrassVariety->Scaling;
	ScaleX = GrassVariety->ScaleX;
	ScaleY = GrassVariety->ScaleY;
	ScaleZ = GrassVariety->ScaleZ;
	RandomRotation = GrassVariety->RandomRotation;
	AlignToSurface = GrassVariety->AlignToSurface;
	MeshBox = GrassVariety->GrassMesh->GetBounds().GetBox();

	const float DensityScale = GMkGpuScatteringDensityScale;
	GrassDensity = GrassVariety->GetDensity() * DensityScale;

	UseLandscapeLightmap = GrassVariety->bUseLandscapeLightmap;
	LightmapBaseBias = FVector2D::ZeroVector;
	LightmapBaseScale = FVector2D::UnitVector;
	ShadowmapBaseBias = FVector2D::ZeroVector;
	ShadowmapBaseScale = FVector2D::UnitVector;
	LightMapComponentBias = FVector2D::ZeroVector;
	LightMapComponentScale = FVector2D::UnitVector;


	HISMC = GrassComp.Foliage;
	RandomStream = FRandomStream(HISMC->InstancingRandomSeed);
	XForm = LandscapeToWorld * HISMC->GetComponentTransform().ToMatrixWithScale().Inverse();
	DesiredInstancesPerLeaf = HISMC->DesiredInstancesPerLeaf();
	BuildTime = 0;
	TotalInstances = 0;


	CachedBuffers = GrassComp.CachedBuffers;

	TWeakObjectPtr<ULandscapeComponent> Component = GrassCompKey.BasedOn;
	SectionBase = Component->GetSectionBase();
	ComponentSizeQuads = Component->ComponentSizeQuads;

	Origin.X = DrawScale.X * SectionBase.X;
	Origin.Y = DrawScale.Y * SectionBase.Y;

	Extent.X = DrawScale.X * ComponentSizeQuads;
	Extent.Y = DrawScale.Y * ComponentSizeQuads;

	ComponentOrigin.X = DrawScale.X * (SectionBase.X - LandscapeSectionOffset.X);
	ComponentOrigin.Y = DrawScale.Y * (SectionBase.Y - LandscapeSectionOffset.Y);
	ComponentOrigin.Z = 0.0f;

	SqrtMaxInstances = FMath::CeilToInt32(FMath::Sqrt(FMath::Abs(Extent.X * Extent.Y * GrassDensity / 1000.0f / 1000.0f)));

	bHaveValidData = SqrtMaxInstances != 0;

	//
	HeightmapTexture = Component->GetHeightmap();
	TArray<FWeightmapLayerAllocationInfo>& WeightmapLayerAllocations = Component->GetWeightmapLayerAllocations(true);

	int32 WeightmapIndex = -1;
	for (const FWeightmapLayerAllocationInfo& WeightLayerInfo : WeightmapLayerAllocations)
	{
		FString LayerName = WeightLayerInfo.LayerInfo.GetName();
		if (LayerName.Contains(InSpawnLayerName))
		{
			WeightmapIndex = WeightLayerInfo.WeightmapTextureIndex;
			WeightmapChannelIdx = WeightLayerInfo.WeightmapTextureChannel + 1;
		}
		//UE_LOG(LogTemp, Warning, TEXT("!!!!!!!!!!!!!! WeightLayerInfo %s, TextureIndex %d(Channel : %d)"), *LayerName, WeightLayerInfo.WeightmapTextureIndex, WeightLayerInfo.WeightmapTextureChannel);
	}

	if (WeightmapIndex > -1)
	{
		WeightmapTexture = Component->GetWeightmapTextures()[WeightmapIndex];
	}

	if (bHaveValidData && SqrtSubsections != 1)
	{
		check(SqrtMaxInstances > 2 * SqrtSubsections);
		SqrtMaxInstances /= SqrtSubsections;
		check(SqrtMaxInstances > 0);

		Extent.X /= SqrtSubsections;
		Extent.Y /= SqrtSubsections;

		Origin.X += Extent.X * SubX;
		Origin.Y += Extent.Y * SubY;
	}


	switch (Scaling)
	{
	case EMkGrassScaling::Uniform:
		RandomScale = ScaleX.Size() > 0;
		break;
	case EMkGrassScaling::Free:
		RandomScale = ScaleX.Size() > 0 || ScaleY.Size() > 0 || ScaleZ.Size() > 0;
		break;
	case EMkGrassScaling::LockXY:
		RandomScale = ScaleX.Size() > 0 || ScaleZ.Size() > 0;
		break;
	default:
		check(0);
	}

	BuilderOutput = FMkGpuScatteringBuilderOutput(Component, SqrtSubsections, CachedMaxInstancesPerComponent, SubX, SubY, NumVarieties, VarietyIndex, XForm, GrassVariety);
	BuilderOutput.RandomScale = RandomScale;

	bHaveValidData = true;

	check(DesiredInstancesPerLeaf > 0);

	if (UseLandscapeLightmap)
	{
		InitLandscapeLightmap(Component);
	}
}

void FMkGpuScatteringCS_Param::InitLandscapeLightmap(TWeakObjectPtr<ULandscapeComponent> Component)
{
	const int32 SubsectionSizeQuads = Component->SubsectionSizeQuads;
	const int32 NumSubsections = Component->NumSubsections;
	const int32 LandscapeComponentSizeQuads = Component->ComponentSizeQuads;

	const int32 StaticLightingLOD = Component->GetLandscapeProxy()->StaticLightingLOD;
	const int32 ComponentSizeVerts = LandscapeComponentSizeQuads + 1;
	const float LightMapRes = Component->StaticLightingResolution > 0.0f ? Component->StaticLightingResolution : Component->GetLandscapeProxy()->StaticLightingResolution;
	const int32 LightingLOD = Component->GetLandscapeProxy()->StaticLightingLOD;

	// Calculate mapping from landscape to lightmap space for mapping landscape grass to the landscape lightmap
	// Copied from the calculation of FLandscapeUniformShaderParameters::LandscapeLightmapScaleBias in FLandscapeComponentSceneProxy::OnTransformChanged()
	int32 PatchExpandCountX = 0;
	int32 PatchExpandCountY = 0;
	int32 DesiredSize = 1;
	const float LightMapRatio = ::GetTerrainExpandPatchCount(LightMapRes, PatchExpandCountX, PatchExpandCountY, LandscapeComponentSizeQuads, (NumSubsections * (SubsectionSizeQuads + 1)), DesiredSize, LightingLOD);
	const float LightmapLODScaleX = LightMapRatio / ((ComponentSizeVerts >> StaticLightingLOD) + 2 * PatchExpandCountX);
	const float LightmapLODScaleY = LightMapRatio / ((ComponentSizeVerts >> StaticLightingLOD) + 2 * PatchExpandCountY);
	const float LightmapBiasX = PatchExpandCountX * LightmapLODScaleX;
	const float LightmapBiasY = PatchExpandCountY * LightmapLODScaleY;
	const float LightmapScaleX = LightmapLODScaleX * (float)((ComponentSizeVerts >> StaticLightingLOD) - 1) / LandscapeComponentSizeQuads;
	const float LightmapScaleY = LightmapLODScaleY * (float)((ComponentSizeVerts >> StaticLightingLOD) - 1) / LandscapeComponentSizeQuads;

	LightMapComponentScale = FVector2D(LightmapScaleX, LightmapScaleY) / FVector2D(DrawScale);
	LightMapComponentBias = FVector2D(LightmapBiasX, LightmapBiasY);

	const FMeshMapBuildData* MeshMapBuildData = Component->GetMeshMapBuildData();

	if (MeshMapBuildData != nullptr)
	{
		if (MeshMapBuildData->LightMap.IsValid())
		{
			LightmapBaseBias = MeshMapBuildData->LightMap->GetLightMap2D()->GetCoordinateBias();
			LightmapBaseScale = MeshMapBuildData->LightMap->GetLightMap2D()->GetCoordinateScale();
		}

		if (MeshMapBuildData->ShadowMap.IsValid())
		{
			ShadowmapBaseBias = MeshMapBuildData->ShadowMap->GetShadowMap2D()->GetCoordinateBias();
			ShadowmapBaseScale = MeshMapBuildData->ShadowMap->GetShadowMap2D()->GetCoordinateScale();
		}
	}
}
//~ end of MkGpuScatteringBuilderParam

void FMkGPUScattering_CS::SetUniqueParameters(FMkGPUScattering_CS::FParameters* PassParameters, const FMkGpuScatteringCS_Param& Param)
{
	PassParameters->WeightmapChannelIdx = Param.WeightmapChannelIdx;
	PassParameters->WeightmapTexture = Param.WeightmapTexture->TextureReference.TextureReferenceRHI;
	//PassParameters->WeightmapTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();
	PassParameters->WeightmapTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
}

template<class T, typename FParameters>
void AddPass_MkScattering(FRDGBuilder& GraphBuilder, const FMkGpuScatteringCS_Param& Param)
{
	LLM_SCOPE_BYTAG(MkGpuScatteringShaders);

	typename T::FPermutationDomain PermutationVector;
	TShaderMapRef<T> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
	if (!ComputeShader.IsValid())
	{
		return;
	}

	//~ Init parameters
	FParameters* PassParameters = GraphBuilder.AllocParameters<FParameters>();

	int32 SqrtMaxInstances = Param.SqrtMaxInstances;
	int32 MaxInstances = SqrtMaxInstances * SqrtMaxInstances;

	const auto& GrassVariety = Param.GrassVariety;
	PassParameters->bUseVoronoiNoise = GrassVariety->bUseVoronoiNoise;
	PassParameters->VoronoiSetting = (GrassVariety->bUseVoronoiNoise == true)
										? FVector4f(GrassVariety->VoronoiGroupSize, GrassVariety->VoronoiScale, GrassVariety->VoronoiValidRange.Min, GrassVariety->VoronoiValidRange.Max)
										: FVector4f::Zero();
	PassParameters->SlopeMinMax = FVector2f(GrassVariety->Slope.Min, GrassVariety->Slope.Max);
	PassParameters->HeightMinMax = FVector2f(GrassVariety->Height.Min, GrassVariety->Height.Max);
	PassParameters->HeightFalloffRange = GrassVariety->HeightFalloffRange;
	PassParameters->PlacementJitter = GrassVariety->PlacementJitter;
	PassParameters->InstancingRandomSeed = Param.HISMC->InstancingRandomSeed;
	PassParameters->UseGrid = GrassVariety->bUseGrid;
	PassParameters->AlignToSurface = GrassVariety->AlignToSurface;

	PassParameters->HeightmapTexture = Param.HeightmapTexture->TextureReference.TextureReferenceRHI;
	PassParameters->HeightmapTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();
	//PassParameters->HeightmapTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();

	FRDGBufferRef ProgressInfoBuffer;
	FRDGBufferRef Result_Buffer;

	FMkGpuScatteringCachedBuffers* CachedBuffers = Param.CachedBuffers;
	if (!CachedBuffers)
	{
		return;
	}

	if (CachedBuffers->Input_Buffer.IsValid())
	{
		PassParameters->Input = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(CachedBuffers->Input_Buffer));
	}
	else
	{
		FRDGBufferRef Input_Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FScatteringInput), 1), TEXT("Input_Buffer"));
		CachedBuffers->Input_Buffer = GraphBuilder.ConvertToExternalBuffer(Input_Buffer);

		FRDGUploadData<FScatteringInput> InputData(GraphBuilder, 1);
		FScatteringInput& InputRef = InputData[0];
		InputRef.Origin = Param.Origin;
		InputRef.Extent = Param.Extent;
		InputRef.Offset = FVector2f(Param.LandscapeSectionOffset.X, Param.LandscapeSectionOffset.Y);
		InputRef.DrawScale = FVector3f(Param.DrawScale.X, Param.DrawScale.Y, Param.DrawScale.Z);
		InputRef.SectionBase = FVector2f(Param.SectionBase.X, Param.SectionBase.Y);

		InputRef.SqrtMaxInstances = Param.SqrtMaxInstances;
		InputRef.HaltonBaseIndex = Param.HaltonBaseIndex;
		InputRef.Stride = Param.ComponentSizeQuads + 1;
		GraphBuilder.QueueBufferUpload<FScatteringInput>(Input_Buffer, InputData, ERDGInitialDataFlags::NoCopy);

		PassParameters->Input = GraphBuilder.CreateSRV(Input_Buffer);
	}

	if (CachedBuffers->Result_Buffer.IsValid())
	{
		Result_Buffer = GraphBuilder.RegisterExternalBuffer(CachedBuffers->Result_Buffer);
		PassParameters->RWResultBuffer = GraphBuilder.CreateUAV(Result_Buffer);
	}
	else
	{
		Result_Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FLocationNormalScaleZ), MaxInstances), TEXT("Result_Buffer"));
		CachedBuffers->Result_Buffer = GraphBuilder.ConvertToExternalBuffer(Result_Buffer);
		PassParameters->RWResultBuffer = GraphBuilder.CreateUAV(Result_Buffer);
	}

	if (CachedBuffers->ProgressInfo_Buffer.IsValid())
	{
		ProgressInfoBuffer = GraphBuilder.RegisterExternalBuffer(CachedBuffers->ProgressInfo_Buffer);
		PassParameters->RWProgressInfo = GraphBuilder.CreateUAV(ProgressInfoBuffer);
	}
	else
	{
		ProgressInfoBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FProgressInfo), 1), TEXT("ProgressInfo_Buffer"));
		CachedBuffers->ProgressInfo_Buffer = GraphBuilder.ConvertToExternalBuffer(ProgressInfoBuffer);

		FRDGUploadData<FProgressInfo> ProgressData(GraphBuilder, 1);
		ProgressData[0].Count = 0;
		ProgressData[0].MaxInstances = MaxInstances;
		GraphBuilder.QueueBufferUpload<FProgressInfo>(ProgressInfoBuffer, ProgressData, ERDGInitialDataFlags::NoCopy);

		PassParameters->RWProgressInfo = GraphBuilder.CreateUAV(ProgressInfoBuffer);
	}

	T::SetUniqueParameters(PassParameters, Param);
	//~ end of Init parameters

	int32 NumThreadCS = MkThreadNum_ScatteringCS;
	int32 NeedGroupCount = FMath::CeilToInt((float)SqrtMaxInstances / (float)NumThreadCS);
	FIntVector GroupCount = FIntVector(NeedGroupCount, NeedGroupCount, 1);
	FComputeShaderUtils::AddPass<T>(
		GraphBuilder,
		RDG_EVENT_NAME("AddPass_MkScattering"),
		ComputeShader, PassParameters, GroupCount);
}


//~ FMkAsyncBuilderInterface
void FMkAsyncBuilderInterface::DispatchGameThread(FMkGpuScatteringCS_Param Param)
{
	ENQUEUE_RENDER_COMMAND(MkAsyncBuilder)(
		[Param](FRHICommandListImmediate& RHICmdList)
		{
			DispatchRenderThread(RHICmdList, Param);
		});
}


void FMkAsyncBuilderInterface::DispatchRenderThread(FRHICommandListImmediate& RHICmdList, FMkGpuScatteringCS_Param Param/*, TFunction<void(const FMkGpuScatteringBuilderOutput& Output)> AsyncCallback*/)
{
	LLM_SCOPE_BYTAG(MkGpuScatteringDispatch);
	if (!Param.HISMC.IsValid())
	{
		return;
	}

	FRDGBuilder GraphBuilder(RHICmdList);

	if (Param.WeightmapTexture)
	{
		AddPass_MkScattering<FMkGPUScattering_CS, FMkGPUScattering_CS::FParameters>(GraphBuilder, Param);
	}
	else
	{
		AddPass_MkScattering<FMkGPUScatteringNoWeightmap_CS, FMkGPUScatteringNoWeightmap_CS::FParameters>(GraphBuilder, Param);
	}


	FMkReadback Readback;

	FRHIGPUBufferReadback* ProgressInfoReadback = new FRHIGPUBufferReadback(TEXT("MkProgressInfo"));
	FRHIGPUBufferReadback* LocationAndNormalReadback = new FRHIGPUBufferReadback(TEXT("MkLocationAndNormalRes"));

	FMkGpuScatteringCachedBuffers* CachedBuffers = Param.CachedBuffers;
	Readback.AddReadback(Param.Builder, Param.BuilderOutput, CachedBuffers->ProgressInfo_Buffer, ProgressInfoReadback);
	Readback.AddReadback(Param.Builder, Param.BuilderOutput, CachedBuffers->Result_Buffer, LocationAndNormalReadback);
	Param.ReadbackManager->AddReadback(Readback);
	GraphBuilder.Execute();
}
//~ end of FMkAsyncBuilderInterface
MK_OPTIMIZATION_ON
