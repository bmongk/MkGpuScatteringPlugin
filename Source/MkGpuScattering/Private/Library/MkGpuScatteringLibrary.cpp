#include "Library/MkGpuScatteringLibrary.h"
#include "MkGpuScatteringGlobal.h"

#include "PixelShaderUtils.h"
#include "MeshPassProcessor.inl"
#include "StaticMeshResources.h"
#include "DynamicMeshBuilder.h"
#include "RenderGraphResources.h"
#include "GlobalShader.h"
#include "UnifiedBuffer.h"
#include "CanvasTypes.h"
#include "MeshDrawShaderBindings.h"
#include "RHIGPUReadback.h"
#include "MeshPassUtils.h"
#include "MaterialShader.h"
#include "ShaderParameterMacros.h"

DECLARE_STATS_GROUP(TEXT("MkGpuScatteringComputeShader"), STAT_MkGpuScatteringComputeShader, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("MkGpuScatteringComputeShader Execute"), STAT_MkGpuScatteringComputeShader_Execute, STAT_MkGpuScatteringComputeShader);


MK_OPTIMIZATION_OFF
// This class carries our parameter declarations and acts as the bridge between cpp and HLSL.
class FMkReadHeightmap_CS : public FGlobalShader
{
public:

	DECLARE_GLOBAL_SHADER(FMkReadHeightmap_CS);
	SHADER_USE_PARAMETER_STRUCT(FMkReadHeightmap_CS, FGlobalShader);


	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		SHADER_PARAMETER(FVector2f, UVCoords)
		SHADER_PARAMETER_TEXTURE(Texture2D, HeightmapTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HeightmapTextureSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, NormalmapTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, NormalmapTextureSampler)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float4>, RWNormalAndHeight)

		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);

		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
private:
};
// This will tell the engine to create the shader and where the shader entry point is.
//                            ShaderType                            ShaderPath                     Shader function name    Type
IMPLEMENT_GLOBAL_SHADER(FMkReadHeightmap_CS, "/MkGPUPlacementShaders/ReadHeightmapComputeShader.usf", "ReadHeightmap_CS", SF_Compute);


void FMkReadHeightmapInterface::DispatchRenderThread(FRHICommandListImmediate& RHICmdList, FMkHeightmapCS_DispatchParam Params, TFunction<void(float OutputValue, FVector OutputVector)> AsyncCallback)
{
	if (!Params.HeightmapTexture)
	{
		UE_LOG(LogTemp, Warning, TEXT("FMkReadHeightmapInterface. HeightmapTexture is nullptr"));
		return;
	}

	FRDGBuilder GraphBuilder(RHICmdList);

	{
		SCOPE_CYCLE_COUNTER(STAT_MkGpuScatteringComputeShader_Execute);
		DECLARE_GPU_STAT(MkGpuScatteringComputeShader);
		RDG_EVENT_SCOPE(GraphBuilder, "MkGpuScatteringComputeShader");
		RDG_GPU_STAT_SCOPE(GraphBuilder, MkGpuScatteringComputeShader);

		//~ ReadHeightmap
		typename FMkReadHeightmap_CS::FPermutationDomain PermutationVector;
		TShaderMapRef<FMkReadHeightmap_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

		bool bIsShaderValid = ComputeShader.IsValid();

		if (bIsShaderValid)
		{
			FMkReadHeightmap_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMkReadHeightmap_CS::FParameters>();
			FRDGBufferRef NormalAndHeightBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(float) * 4, 1), TEXT("NormalAndHeight"));


			PassParameters->UVCoords = Params.UVCoords;
			PassParameters->RWNormalAndHeight = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(NormalAndHeightBuffer, PF_A32B32G32R32F));

			PassParameters->HeightmapTexture = Params.HeightmapTexture->TextureReference.TextureReferenceRHI;
			PassParameters->HeightmapTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();

			PassParameters->NormalmapTexture = Params.HeightmapTexture->TextureReference.TextureReferenceRHI;
			PassParameters->NormalmapTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();


			auto GroupCount = FComputeShaderUtils::GetGroupCount(FIntVector(1, 1, 1), FComputeShaderUtils::kGolden2DGroupSize);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ExecuteReadHeightmapTexture"),
				PassParameters,
				ERDGPassFlags::AsyncCompute,
				[&PassParameters, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
				{
					FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
				});


			FRHIGPUBufferReadback* NormalAndHeightReadback = new FRHIGPUBufferReadback(TEXT("FMkReadHeightmap_CS_NormalAndHeight"));
			AddEnqueueCopyPass(GraphBuilder, NormalAndHeightReadback, NormalAndHeightBuffer, 0u);

			auto RunnerFunc = [NormalAndHeightReadback, AsyncCallback](auto&& RunnerFunc) -> void {

				if (NormalAndHeightReadback->IsReady())
				{

					void* NormalAndHeightBuffer = (void*)NormalAndHeightReadback->Lock(1);
					FVector4f NormalAndHeight;
					FPlatformMemory::Memcpy(&NormalAndHeight, NormalAndHeightBuffer, sizeof(float) * 4 * 1);
					NormalAndHeightReadback->Unlock();

					AsyncTask(ENamedThreads::GameThread, [AsyncCallback, NormalAndHeight]() {

						AsyncCallback(NormalAndHeight.W, FVector(NormalAndHeight.X, NormalAndHeight.Y, NormalAndHeight.Z));
					});
				}
				else
				{
					AsyncTask(ENamedThreads::ActualRenderingThread, [RunnerFunc]() {
						RunnerFunc(RunnerFunc);
					});
				}
			};

			AsyncTask(ENamedThreads::ActualRenderingThread, [RunnerFunc]() {
				RunnerFunc(RunnerFunc);
			});

		}
		else
		{
#if WITH_EDITOR
			GEngine->AddOnScreenDebugMessage((uint64)42145125184, 6.f, FColor::Red, FString(TEXT("The compute shader has a problem.")));
#endif
			// We exit here as we don't want to crash the game if the shader is not found or has an error.
		}
	}
	//~! ReadHeightmap

	GraphBuilder.Execute();
}
MK_OPTIMIZATION_ON
