#include "Builder/MkGpuScatteringBuilder.h"
#include "Types/MkGpuScatteringTypes.h"
#include "Readback/MkGpuScatteringReadbackManager.h"
#include "MkGpuScatteringGlobal.h"
#include "MkGpuScatteringVolume.h"


#include "Math/Halton.h"
#include "Async/AsyncWork.h"
#include "LandscapeLight.h"
#include "LandscapeProxy.h"
#include "LandscapeComponent.h"
#include "LandscapeGrassType.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Runtime/Foliage/Public/GrassInstancedStaticMeshComponent.h"

//
#include "RenderUtils.h"
#include "GlobalShader.h"
#include "RHIGPUReadback.h"
#include "RenderGraphUtils.h"
#include "RenderGraphBuilder.h"
#include "DataDrivenShaderPlatformInfo.h"

#include "HAL/LowLevelMemTracker.h"

#include "DrawDebugHelpers.h"

LLM_DEFINE_TAG(MkGpuScatteringBuilder_Tick);
LLM_DEFINE_TAG(MkGpuScatteringBuilder_Build);
LLM_DEFINE_TAG(MkGpuScatteringBuilder_TransformBuild);
LLM_DEFINE_TAG(MkGpuScatteringBuilder_WaitAndApply);
LLM_DEFINE_TAG(MkGpuScatteringBuilder_CreateHISMC);
LLM_DEFINE_TAG(MkGpuScatteringBuilder_SetGrassVarieties);
LLM_DEFINE_TAG(MkGpuScatteringBuilder_Build_DelegateFinish);

LLM_DEFINE_TAG(MkGpuScatteringBuilder_ApplyInstance);



#include UE_INLINE_GENERATED_CPP_BY_NAME(MkGpuScatteringBuilder)

using namespace MkGpuScatteringBuilderTypes;


MK_OPTIMIZATION_OFF

static float GMkGpuScatteringMinTimeToKeepGrass = 5.0f;
static FAutoConsoleVariableRef CVarMkMinTimeToKeepGrass(
	TEXT("MkGpuScattering.MinTimeToKeepGrass"),
	GMkGpuScatteringMinTimeToKeepGrass,
	TEXT("Minimum number of seconds before cached grass can be discarded; used to prevent thrashing."));

static float GMkGpuScatteringGuardBandMultiplier = 1.3f;
static FAutoConsoleVariableRef CVarMkGuardBandMultiplier(
	TEXT("MkGpuScattering.GuardBandMultiplier"),
	GMkGpuScatteringGuardBandMultiplier,
	TEXT("Used to control discarding in the grass system. Approximate range, 1-4. Multiplied by the cull distance to control when we add grass components."));

static float GMkGpuScatteringGuardBandDiscardMultiplier = 1.4f;
static FAutoConsoleVariableRef CVarMkGuardBandDiscardMultiplier(
	TEXT("MkGpuScattering.GuardBandDiscardMultiplier"),
	GMkGpuScatteringGuardBandDiscardMultiplier,
	TEXT("Used to control discarding in the grass system. Approximate range, 1-4. Multiplied by the cull distance to control when we discard grass components."));

static int32 GMkGpuScatteringCullSubsections = 1;
static FAutoConsoleVariableRef CVarMkCullSubsections(
	TEXT("MkGpuScattering.CullSubsections"),
	GMkGpuScatteringCullSubsections,
	TEXT("1: Cull each foliage component; 0: Cull only based on the landscape component."));

static float GMkGpuScatteringCullDistanceScale = 1;
static FAutoConsoleVariableRef CVarMkGpuScatteringCullDistanceScale(
	TEXT("MkGpuScattering.CullDistanceScale"),
	GMkGpuScatteringCullDistanceScale,
	TEXT("Multiplier on all MkGpuScattering cull distances."),
	ECVF_Scalability);



static int32 GMkMaxInstancesPerComponent = 65536;
static FAutoConsoleVariableRef CVarMkMaxInstancesPerComponent(
	TEXT("MkGpuScattering.MaxInstancesPerComponent"),
	GMkMaxInstancesPerComponent,
	TEXT("Used to control the number of grass components created. More can be more efficient, but can be hitchy as new components come into range"));


DECLARE_CYCLE_STAT(TEXT("MkGpuScattering Transform Build Time"), STAT_MkGpuScatteringTransformBuildTime, STATGROUP_Foliage);


//~

struct FMkFoliagePlacementUtil
{
	static int32 GetRandomSeedForPosition(const FVector2D& Position)
	{
		// generate a unique random seed for a given position (precision = cm)
		int64 Xcm = FMath::RoundToInt(Position.X);
		int64 Ycm = FMath::RoundToInt(Position.Y);
		// use the int32 hashing function to avoid patterns by spreading out distribution :
		return HashCombine(GetTypeHash(Xcm), GetTypeHash(Ycm));
	}
};


struct FMkGpuScatteringTransformBuilder
{
	FMkCachedLandscapeFoliage::FGrassCompKey Key;
	TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISMC;
	TArray<MkGpuScatteringBuilderTypes::FLocationNormalScaleZ> ResultBuffer;
	FMatrix XForm;
	FRandomStream RandomStream;

	EMkGrassScaling Scaling = EMkGrassScaling::Uniform;
	FFloatInterval ScaleX = FFloatInterval(1.0f, 1.0f);
	FFloatInterval ScaleY = FFloatInterval(1.0f, 1.0f);
	FFloatInterval ScaleZ = FFloatInterval(1.0f, 1.0f);

	bool RandomScale = false;
	bool RandomRotation = false;
	bool AlignToSurface = false;
	//bool RequireCPUAccess = false;
	bool bCollisionEnabled = false;
	bool bCheckCloseLandscape = false;
	bool IsDone = false;

	double BuildTime;

	// output
	TArray<FInstancedStaticMeshInstanceData> InstanceData;
	FStaticMeshInstanceData InstanceBuffer;
	TArray<FClusterNode> ClusterTree;
	int32 OutOcclusionLayerNum;

	const FMkGrassVariety* GrassVariety;

	FVector Origin;

	FMkGpuScatteringTransformBuilder(
		FMkCachedLandscapeFoliage::FGrassCompKey InKey
		, TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> OutHISMC
		, TArray<MkGpuScatteringBuilderTypes::FLocationNormalScaleZ> InResultBuffer
		, const FMatrix& InXForm
		, FRandomStream InRandomStream
		, const FMkGrassVariety* InGrassVariety
	)
		: Key(MoveTemp(InKey))
		, HISMC(OutHISMC)
		, ResultBuffer(MoveTemp(InResultBuffer))
		, XForm(InXForm)
		, RandomStream(InRandomStream)
		, InstanceBuffer(true)
		, GrassVariety(InGrassVariety)
		, Origin(HISMC->GetComponentLocation())
	{
		BuildTime = 0.0;

		//RequireCPUAccess = GrassVariety->bKeepInstanceBufferCPUCopy;
		bCollisionEnabled = GrassVariety->CollisionProfileName != TEXT("NoCollision");
		bCheckCloseLandscape = GrassVariety->bCheckCloseLandscape;

		RandomRotation = GrassVariety->RandomRotation;
		AlignToSurface = GrassVariety->AlignToSurface;

		Scaling = GrassVariety->Scaling;

		ScaleX = GrassVariety->ScaleX;
		ScaleY = GrassVariety->ScaleY;
		ScaleZ = GrassVariety->ScaleZ;

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
	}

	~FMkGpuScatteringTransformBuilder()
	{
		Clear();
	}

	void SetInstance(int32 InstanceIndex, const FMatrix& InXForm, float RandomFraction)
	{
		InstanceBuffer.SetInstance(InstanceIndex, FMatrix44f(InXForm), RandomFraction);
	}

	FVector GetDefaultScale() const
	{
		FVector Result(ScaleX.Min > 0.0f && FMath::IsNearlyZero(ScaleX.Size()) ? ScaleX.Min : 1.0f,
			ScaleY.Min > 0.0f && FMath::IsNearlyZero(ScaleY.Size()) ? ScaleY.Min : 1.0f,
			ScaleZ.Min > 0.0f && FMath::IsNearlyZero(ScaleZ.Size()) ? ScaleZ.Min : 1.0f);
		switch (Scaling)
		{
		case EMkGrassScaling::Uniform:
			Result.Y = Result.X;
			Result.Z = Result.X;
			break;
		case EMkGrassScaling::Free:
			break;
		case EMkGrassScaling::LockXY:
			Result.Y = Result.X;
			break;
		default:
			check(0);
		}
		return Result;
	}

	FVector GetRandomScale(const FRandomStream& LocalRandomStream, float Alpha) const
	{
		FVector Result(1.0f);

		float NewAlpha = Alpha;

		FFloatInterval NewRangeX = ScaleX;
		FFloatInterval NewRangeY = ScaleY;
		FFloatInterval NewRangeZ = ScaleZ;

		if (GrassVariety->bUseVoronoiNoise && Alpha > GrassVariety->VoronoiValidRange.Max)
		{
			NewAlpha = 1.0f - Alpha;

			NewRangeX.Min = FMath::Max(NewRangeX.Min * NewAlpha, 1.0f);
			NewRangeY.Min = FMath::Max(NewRangeY.Min * NewAlpha, 1.0f);
			NewRangeZ.Min = FMath::Max(NewRangeZ.Min * NewAlpha, 0.5f);
		}


		//float InterpAlpha = 0.5f * (NewAlpha + RandomStream.GetFraction());
		float InterpAlpha = (NewAlpha + LocalRandomStream.FRand());
		switch (Scaling)
		{
		case EMkGrassScaling::Uniform:
			Result.X = NewRangeX.Interpolate(InterpAlpha);
			Result.Y = Result.X;
			Result.Z = Result.X;
			break;
		case EMkGrassScaling::Free:
			Result.X = NewRangeX.Interpolate(InterpAlpha);
			Result.Y = NewRangeY.Interpolate(InterpAlpha);
			Result.Z = NewRangeZ.Interpolate(InterpAlpha);
			break;
		case EMkGrassScaling::LockXY:
			Result.X = NewRangeX.Interpolate(InterpAlpha);
			Result.Y = Result.X;
			Result.Z = NewRangeZ.Interpolate(InterpAlpha);
			break;
		default:
			check(0);
		}

		return Result;
	}

	void Clear()
	{
		ResultBuffer.Empty();

		ClusterTree.Empty();
		InstanceData.Empty();
	}

	// InstancedFoliage.h
	FMatrix AlignToNormal(const FVector& InNormal, float AlignMaxAngle)
	{
		FRotator AlignRotation = InNormal.Rotation();
		// Static meshes are authored along the vertical axis rather than the X axis, so we add 90 degrees to the static mesh's Pitch.
		AlignRotation.Pitch -= 90.f;
		// Clamp its value inside +/- one rotation
		AlignRotation.Pitch = FRotator::NormalizeAxis(AlignRotation.Pitch);

		// limit the maximum pitch angle if it's > 0.
		if (AlignMaxAngle > 0.f)
		{
			int32 MaxPitch = static_cast<int32>(AlignMaxAngle);
			if (AlignRotation.Pitch > MaxPitch)
			{
				AlignRotation.Pitch = MaxPitch;
			}
			else if (AlignRotation.Pitch < -MaxPitch)
			{
				AlignRotation.Pitch = -MaxPitch;
			}
		}
		return AlignRotation.Quaternion().ToMatrix();
	}

	//~
	void Build()
	{
		SCOPE_CYCLE_COUNTER(STAT_MkGpuScatteringTransformBuildTime);
		LLM_SCOPE_BYTAG(MkGpuScatteringBuilder_TransformBuild);

		IsDone = false;

		if (!HISMC.IsValid() || !HISMC->GetStaticMesh())
		{
			IsDone = true;
			return;
		}


		double StartTime = FPlatformTime::Seconds();

		UWorld* World = HISMC->GetWorld();
		FBoxSphereBounds MeshBounds = HISMC->GetStaticMesh()->GetBounds();

		TArray<FMatrix> InstanceTransforms;


		const FVector DefaultScale = GetDefaultScale();

		for (const FLocationNormalScaleZ& Result : ResultBuffer)
		{
			FVector LocationWithHeight = FVector(Result.Location);
			FVector2D Location2D = FVector2D(LocationWithHeight);

			FRandomStream LocalRandomStream(FMkFoliagePlacementUtil::GetRandomSeedForPosition(Location2D));
			FVector Scale = RandomScale ? GetRandomScale(LocalRandomStream, Result.ScaleZ) : DefaultScale;
			float PlacementOffsetZ = GrassVariety->ZOffset.Interpolate(LocalRandomStream.FRand());

			if (bCheckCloseLandscape)
			{
				TArray<FHitResult> HitResults;
				FCollisionQueryParams QueryParams;
				QueryParams.bTraceComplex = false;
				FCollisionObjectQueryParams ObjectParams(FCollisionObjectQueryParams::AllObjects);

				//~ plugin으로 배치된 HISMC의 충돌 감지는 안됨
				FVector TraceLocation = Origin + LocationWithHeight;
				const FCollisionShape CollisionShape = FCollisionShape::MakeSphere(1.0f);
				if (World->SweepMultiByObjectType(HitResults, TraceLocation + FVector::UpVector, TraceLocation + FVector::DownVector, FQuat::Identity, ObjectParams, CollisionShape, QueryParams))
				{
					bool bCloseLandscape = false;
					for (const FHitResult& HitResult : HitResults)
					{
						AActor* Actor = HitResult.GetActor();
						if (Actor->IsA<ALandscapeProxy>())
						{
							bCloseLandscape = true;
							break;
						}
					}

					if (!bCloseLandscape)
					{
						continue;
					}
				}
				//~!
			}

			FVector ComputedNormal = FVector(Result.ComputedNormal);
			const float Rot = RandomRotation ? RandomStream.GetFraction() * 180.0f : 0.0f;
			FVector RotVector = GrassVariety->RotationAxis * Rot;

			const FMatrix BaseXForm = FScaleRotationTranslationMatrix(Scale, FRotator(RotVector.X, RotVector.Y, RotVector.Z), FVector::ZeroVector);
			FMatrix OutXForm;
			if (AlignToSurface && !ComputedNormal.IsNearlyZero())
			{
				//~ LandscapeGrass code
				/*const FVector NewZ = ComputedNormal * FMath::Sign(ComputedNormal.Z);
				const FVector NewX = (FVector(0, -1, 0) ^ NewZ).GetSafeNormal();
				const FVector NewY = NewZ ^ NewX;
				const FMatrix Align = FMatrix(NewX, NewY, NewZ, FVector::ZeroVector);*/
				//~ LandscapeGrass code ~!

				FMatrix Align = AlignToNormal(ComputedNormal, GrassVariety->AlignMaxAngle);
				FMatrix AlignedXForm = (BaseXForm * Align);
				FVector AxisZ = AlignedXForm.GetUnitAxis(EAxis::Z);
				LocationWithHeight += AxisZ * (PlacementOffsetZ * Scale.Z);
				OutXForm = AlignedXForm.ConcatTranslation(LocationWithHeight) * XForm;
			}
			else
			{
				FVector AxisZ = BaseXForm.GetUnitAxis(EAxis::Z);
				LocationWithHeight += AxisZ * (PlacementOffsetZ * Scale.Z);
				OutXForm = BaseXForm.ConcatTranslation(LocationWithHeight) * XForm;
			}
			InstanceTransforms.Add(OutXForm);
		}

		ResultBuffer.Empty();

		int32 TotalInstances = 0;

		if (InstanceTransforms.Num())
		{
			//float RandomFraction = RandomStream.GetFraction();

			TotalInstances += InstanceTransforms.Num();

			InstanceBuffer.AllocateInstances(InstanceTransforms.Num(), 0, EResizeBufferFlags::AllowSlackOnGrow | EResizeBufferFlags::AllowSlackOnReduce, true);

			for (int32 InstanceIndex = 0; InstanceIndex < InstanceTransforms.Num(); InstanceIndex++)
			{
				const FMatrix& OutXForm = InstanceTransforms[InstanceIndex];
				SetInstance(InstanceIndex, OutXForm, RandomStream.GetFraction());

			}

			if (!HISMC.IsValid() || !HISMC->GetStaticMesh())
			{
				IsDone = true;
				return;
			}

			//TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISMC = Existing->Foliage;
			int32 DesiredInstancesPerLeaf = HISMC->DesiredInstancesPerLeaf();
			int32 NumInstances = InstanceTransforms.Num();
			TArray<int32> SortedInstances;
			TArray<int32> InstanceReorderTable;
			TArray<float> InstanceCustomDataDummy;

			FBox MeshBox(MeshBounds.GetBox());

			//~ by jhlim
			UGrassInstancedStaticMeshComponent::BuildTreeAnyThread(InstanceTransforms, InstanceCustomDataDummy, 0, MeshBox, ClusterTree, SortedInstances, InstanceReorderTable, OutOcclusionLayerNum, DesiredInstancesPerLeaf, false);
			//~! by jhlim

			InstanceData.Reset(NumInstances);
			for (const FMatrix& Transform : InstanceTransforms)
			{
				InstanceData.Emplace(Transform);
			}

			// in-place sort the instances and generate the sorted instance data
			for (int32 FirstUnfixedIndex = 0; FirstUnfixedIndex < NumInstances; FirstUnfixedIndex++)
			{
				int32 LoadFrom = SortedInstances[FirstUnfixedIndex];

				if (LoadFrom != FirstUnfixedIndex)
				{
					check(LoadFrom > FirstUnfixedIndex);
					InstanceBuffer.SwapInstance(FirstUnfixedIndex, LoadFrom);
					InstanceData.Swap(FirstUnfixedIndex, LoadFrom);

					int32 SwapGoesTo = InstanceReorderTable[FirstUnfixedIndex];
					check(SwapGoesTo > FirstUnfixedIndex);
					check(SortedInstances[SwapGoesTo] == FirstUnfixedIndex);
					SortedInstances[SwapGoesTo] = LoadFrom;
					InstanceReorderTable[LoadFrom] = SwapGoesTo;

					InstanceReorderTable[FirstUnfixedIndex] = FirstUnfixedIndex;
					SortedInstances[FirstUnfixedIndex] = FirstUnfixedIndex;
				}
			}
			BuildTime = FPlatformTime::Seconds() - StartTime;
			//UE_LOG(LogTemp, Warning, TEXT("BuildTime %f"), BuildTime);
		}

		IsDone = true;
	}
	//~ end of Build()

};





// LandscapeGrass.cpp에서 옮겨옴.
struct FMkGpuScatteringBuilderBase
{
	bool bHaveValidData;
	float GrassDensity;
	FVector DrawScale;
	FVector DrawLoc;
	FMatrix LandscapeToWorld;

	FIntPoint SectionBase;
	FIntPoint LandscapeSectionOffset;
	int32 ComponentSizeQuads;
	FVector Origin;
	FVector Extent;
	FVector ComponentOrigin;

	int32 SqrtMaxInstances;

	//
	UTexture* HeightmapTexture = nullptr;
	const FMkGrassVariety* GrassVariety;

	FMkGpuScatteringBuilderBase(ALandscapeProxy* Landscape, ULandscapeComponent* Component, const FMkGrassVariety& InGrassVariety, int32 SqrtSubsections = 1, int32 SubX = 0, int32 SubY = 0, bool bEnableDensityScaling = true)
	{
		const float DensityScale = bEnableDensityScaling ? GMkGpuScatteringDensityScale : 1.0f;
		GrassVariety = &InGrassVariety;
		GrassDensity = GrassVariety->GetDensity() * DensityScale;

		DrawScale = Landscape->GetRootComponent()->GetRelativeScale3D();
		DrawLoc = Landscape->GetActorLocation();
		LandscapeSectionOffset = Landscape->LandscapeSectionOffset;

		SectionBase = Component->GetSectionBase();
		ComponentSizeQuads = Component->ComponentSizeQuads;

		Origin.X = DrawScale.X * SectionBase.X;
		Origin.Y = DrawScale.Y * SectionBase.Y;
		Origin.Z = 0.0f;

		Extent.X = DrawScale.X * ComponentSizeQuads;
		Extent.Y = DrawScale.Y * ComponentSizeQuads;
		Extent.Z = 0.0f;

		ComponentOrigin.X = DrawScale.X * (SectionBase.X - LandscapeSectionOffset.X);
		ComponentOrigin.Y = DrawScale.Y * (SectionBase.Y - LandscapeSectionOffset.Y);
		ComponentOrigin.Z = 0.0f;

		SqrtMaxInstances = FMath::CeilToInt32(FMath::Sqrt(FMath::Abs(Extent.X * Extent.Y * GrassDensity / 1000.0f / 1000.0f)));

		bHaveValidData = SqrtMaxInstances != 0;

		LandscapeToWorld = Landscape->GetRootComponent()->GetComponentTransform().ToMatrixNoScale();

		//
		HeightmapTexture = Component->GetHeightmap();

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
	}
};



//~ UMkGpuScatteringBuilder
int32 UMkGpuScatteringBuilder::GrassUpdateInterval = 1;

UMkGpuScatteringBuilder::UMkGpuScatteringBuilder()
{
	PrimaryComponentTick.bCanEverTick = false;
}



void UMkGpuScatteringBuilder::BeginPlay()
{
	Super::BeginPlay();

}

UHierarchicalInstancedStaticMeshComponent* UMkGpuScatteringBuilder::CreateHISMC(AActor* Owner, const FMkGrassVariety& GrassVariety, int32 InstancingRandomSeed)
{
	LLM_SCOPE_BYTAG(MkGpuScatteringBuilder_CreateHISMC);


	FName CollisionProfileName = GrassVariety.CollisionProfileName;
	bool bDisableCollision = CollisionProfileName == TEXT("NoCollision");

	UHierarchicalInstancedStaticMeshComponent* HISMC = bDisableCollision ? NewObject<UGrassInstancedStaticMeshComponent>(Owner) : NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner);
	HISMC->Mobility = EComponentMobility::Static;
	HISMC->SetStaticMesh(GrassVariety.GrassMesh);
	HISMC->MinLOD = GrassVariety.MinLOD;
	HISMC->bOverrideMinLOD = (HISMC->MinLOD > 0);
	HISMC->bSelectable = false;
	HISMC->bHasPerInstanceHitProxies = false;
	HISMC->bReceivesDecals = GrassVariety.bReceivesDecals;

	HISMC->SetCollisionProfileName(CollisionProfileName);
	HISMC->bDisableCollision = bDisableCollision;

	HISMC->SetCanEverAffectNavigation(false);
	HISMC->InstancingRandomSeed = InstancingRandomSeed;// FolSeed;

	HISMC->LightingChannels = GrassVariety.LightingChannels;
	HISMC->bCastStaticShadow = false;
	HISMC->CastShadow = (GrassVariety.bCastDynamicShadow || GrassVariety.bCastContactShadow);// && !bDisableDynamicShadows;
	//HISMC->CastShadow = (GrassVariety.bCastDynamicShadow)/* && !bDisableDynamicShadows*/;
	HISMC->bAffectDistanceFieldLighting = GrassVariety.bAffectDistanceFieldLighting;
	HISMC->bCastDynamicShadow = GrassVariety.bCastDynamicShadow/* && !bDisableDynamicShadows*/;
	HISMC->bCastContactShadow = GrassVariety.bCastContactShadow/* && !bDisableDynamicShadows*/;
	HISMC->OverrideMaterials = GrassVariety.OverrideMaterials;
	HISMC->bEvaluateWorldPositionOffset = GrassVariety.bEvaluateWorldPositionOffset;
	HISMC->WorldPositionOffsetDisableDistance = GrassVariety.InstanceWorldPositionOffsetDisableDistance;
	HISMC->ShadowCacheInvalidationBehavior = GrassVariety.ShadowCacheInvalidationBehavior;

	HISMC->InstanceStartCullDistance = static_cast<int32>(GrassVariety.GetStartCullDistance()/* * GMkGpuScatteringCullDistanceScale*/);
	HISMC->InstanceEndCullDistance = static_cast<int32>(GrassVariety.GetEndCullDistance()/* * GMkGpuScatteringCullDistanceScale*/);

	HISMC->PrecachePSOs();
	HISMC->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

	FTransform DesiredTransform = Owner->GetRootComponent()->GetComponentTransform();
	DesiredTransform.RemoveScaling();
	HISMC->SetWorldTransform(DesiredTransform);

	HISMC->RegisterComponent();

	FoliageComponents.Add(HISMC);

	return HISMC;
}


void UMkGpuScatteringBuilder::SetScatteringTypes(const TArray<UMkGpuScatteringTypes*>& InScatteringTypes)
{
	LLM_SCOPE_BYTAG(MkGpuScatteringBuilder_SetGrassVarieties);

	if (!InScatteringTypes.Num())
	{
		return;
	}

	ScatteringTypes.Empty();
	ScatteringTypes.Append(InScatteringTypes);

	// Sort하면 Asset에 직접적인 영향이 미침. 개선 필요.
	/*for (TWeakObjectPtr<UMkGpuScatteringTypes> ScatteringType : InScatteringTypes)
	{
		if (ScatteringType->GrassVarieties.IsEmpty())
		{
			continue;
		}

		ScatteringTypes.Add(ScatteringType);
		ScatteringTypes.Last()->GrassVarieties.Sort([](const FMkGrassVariety& A, const FMkGrassVariety& B) { return A.GetDensity() < B.GetDensity(); });
	}

	ScatteringTypes.Sort([](const TWeakObjectPtr<UMkGpuScatteringTypes>& A, const TWeakObjectPtr<UMkGpuScatteringTypes>& B) {
		return A->GrassVarieties[0].GetDensity() < B->GrassVarieties[0].GetDensity();
	});*/
}

void UMkGpuScatteringBuilder::OnDelegateCompueteFinish(const FMkGpuScatteringBuilderOutput& Output)
{
	check(!IsInGameThread());


	LLM_SCOPE_BYTAG(MkGpuScatteringBuilder_Build_DelegateFinish);

	FMkCachedLandscapeFoliage::FGrassCompKey GrassCompKey;
	GrassCompKey.BasedOn = Output.BasedOn;
	GrassCompKey.CachedMaxInstancesPerComponent = Output.CachedMaxInstancesPerComponent;
	GrassCompKey.NumVarieties = Output.NumVarieties;
	GrassCompKey.SqrtSubsections = Output.SqrtSubsections;
	GrassCompKey.SubsectionX = Output.SubsectionX;
	GrassCompKey.SubsectionY = Output.SubsectionY;
	GrassCompKey.VarietyIndex = Output.VarietyIndex;

	FMkCachedLandscapeFoliage::FGrassComp* Existing = FoliageCache.CachedGrassComps.Find(GrassCompKey);
	if (!Existing || !Existing->Pending || !Existing->Foliage.IsValid())
	{
		return;
	}

	TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISMC = Existing->Foliage;
	FRandomStream RandomStream(HISMC->InstancingRandomSeed);

	FMkGpuScatteringTransformBuilder* TransformBuilder = new FMkGpuScatteringTransformBuilder(GrassCompKey, HISMC, Output.ResultBuffer, Output.XForm, RandomStream, Output.GrassVariety);
	TransformBuilder->Build();

	//if (TransformBuilder->RequireCPUAccess)
	if (TransformBuilder->bCollisionEnabled) // 충돌 객체의 우선순위를 높임
	{
		TransformBuilders.Insert(MoveTemp(TransformBuilder), 0);
	}
	else
	{
		TransformBuilders.Add(MoveTemp(TransformBuilder));
	}

	Existing->Pending = false;
}

// ClusterTree build 과정에서 약간의 leak이 발생하는듯(UnrealInsight에서 확인함)
void UMkGpuScatteringBuilder::Build(const TArray<FVector>& Cameras, int32& InOutNumCompsCreated, UMkGpuScatteringReadbackManager* ReadbackManager)
{
	LLM_SCOPE_BYTAG(MkGpuScatteringBuilder_Build);

	if (bPendingFlushCache || !LandscapeProxy)
	{
		return;
	}

	TArray<TObjectPtr<ULandscapeComponent>> LandscapeComponents = LandscapeProxy->LandscapeComponents;

	float GrassMaxDiscardDistance = 0.0f;
	for (const UMkGpuScatteringTypes* ScatteringType : ScatteringTypes)
	{
		/*if (!ScatteringType.IsValid())
		{
			continue;
		}*/
		if (!ScatteringType)
		{
			continue;
		}

		for (const FMkGrassVariety& Variety : ScatteringType->GrassVarieties)
		{
			if (GrassMaxDiscardDistance < Variety.GetEndCullDistance())
			{
				GrassMaxDiscardDistance = Variety.GetEndCullDistance();
			}
		}
	}

	float GrassMaxCulledDiscardDistance = GrassMaxDiscardDistance * GMkGpuScatteringCullDistanceScale * FMath::Max(GMkGpuScatteringGuardBandDiscardMultiplier, GMkGpuScatteringGuardBandMultiplier);
	float GrassMaxSquareDiscardDistance = GrassMaxCulledDiscardDistance * GrassMaxCulledDiscardDistance;


	//~ Sorting
	struct SortedLandscapeElement
	{
		SortedLandscapeElement(ULandscapeComponent* InComponent, float InMinDistance, const FBox& InBoundsBox)
			: LandscapeProxy(InComponent->GetLandscapeProxy())
			, Component(InComponent)
			, MinDistance(InMinDistance)
			, BoundsBox(InBoundsBox)
		{

		}

		ALandscapeProxy* LandscapeProxy;
		ULandscapeComponent* Component;
		float MinDistance;
		FBox BoundsBox;
	};

	/*static*/ TArray<SortedLandscapeElement> SortedLandscapeComponents;
	SortedLandscapeComponents.Reset(LandscapeComponents.Num());

	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		if (!Component)
		{
			continue;
		}

		FBoxSphereBounds WorldBounds = Component->CalcBounds(Component->GetComponentTransform());
		float MinSqrDistanceToComponent = Cameras.Num() ? MAX_flt : 0.0f;
		for (const FVector& CameraPos : Cameras)
		{
			MinSqrDistanceToComponent = FMath::Min<float>(MinSqrDistanceToComponent, static_cast<float>(WorldBounds.ComputeSquaredDistanceFromBoxToPoint(CameraPos)));
		}

		// GrassVarieties 중 가장 먼 Grass 거리를 기준으로 그려질 가능성이 없는 LandscapeComponent 필터링
		if (MinSqrDistanceToComponent > GrassMaxSquareDiscardDistance)
		{
			continue;
		}

		SortedLandscapeComponents.Emplace(Component, FMath::Sqrt(MinSqrDistanceToComponent), WorldBounds.GetBox());
	}

	Algo::Sort(SortedLandscapeComponents, [](const SortedLandscapeElement& A, const SortedLandscapeElement& B) { return A.MinDistance < B.MinDistance; });
	//~ end of Sorting


	float GuardBand = GMkGpuScatteringGuardBandMultiplier;
	float DiscardGuardBand = GMkGpuScatteringGuardBandDiscardMultiplier;
	bool bCullSubsections = GMkGpuScatteringCullSubsections > 0;
	float CullDistanceScale = GMkGpuScatteringCullDistanceScale;

	int32 GrassMaxCreatePerFrame = 1; //GGrassMaxCreatePerFrame;

	//UE_LOG(LogTemp, Warning, TEXT("[MkGpuScattering] SortedLandscapeComponents %d"), SortedLandscapeComponents.Num());
	for (const SortedLandscapeElement& SortedLandscapeComponent : SortedLandscapeComponents)
	{
		ULandscapeComponent* LandscapeComponent = SortedLandscapeComponent.Component;
		float MinDistanceToComp = SortedLandscapeComponent.MinDistance;

		uint32 HaltonBaseIndex = 1;
		int32 GrassVarietyIndex = -1;

		//ERHIFeatureLevel::Type FeatureLevel = GetWorld()->Scene->GetFeatureLevel();
		int32 MaxInstancesPerComponent = FMath::Max<int32>(1024, GMkMaxInstancesPerComponent);

		//for (const TWeakObjectPtr<UMkGpuScatteringTypes>& ScatteringType : ScatteringTypes)
		for (const UMkGpuScatteringTypes* ScatteringType : ScatteringTypes)
		{
			if (!ScatteringType || !ScatteringType->bEnable)
			{
				continue;
			}

			bool bEnableSpawnLayer = ScatteringType->bEnableSpawnLayer;
			bool bEnableBlockingLayer = ScatteringType->bEnableBlockingLayer;

			FString SpawnLayerName = (bEnableSpawnLayer) ? ScatteringType->SpawnLayerName : TEXT("All");
			FString BlockingLayerName =	(bEnableBlockingLayer) ? ScatteringType->BlockingLayerName : TEXT("None");
			for (const FMkGrassVariety& GrassVariety : ScatteringType->GrassVarieties)
			{
				++GrassVarietyIndex;

				int32 EndCullDistance = GrassVariety.GetEndCullDistance();
				if (!GrassVariety.GrassMesh || GrassVariety.GetDensity() <= 0.0f || EndCullDistance <= 0)
				{
					continue;
				}

				float MustHaveDistance = GuardBand * (float)EndCullDistance * CullDistanceScale;
				float DiscardDistance = DiscardGuardBand * (float)EndCullDistance * CullDistanceScale;


				bool bUseHalton = !GrassVariety.bUseGrid;

				if (!bUseHalton && MinDistanceToComp > DiscardDistance)
				{
					continue;
				}


				FMkGpuScatteringBuilderBase ForSubsectionMath(LandscapeProxy, LandscapeComponent, GrassVariety);

				int32 SqrtSubsections = 1;
				if (ForSubsectionMath.bHaveValidData && ForSubsectionMath.SqrtMaxInstances > 0)
				{
					SqrtSubsections = FMath::Clamp<int32>(FMath::CeilToInt(float(ForSubsectionMath.SqrtMaxInstances) / FMath::Sqrt((float)MaxInstancesPerComponent)), 1, 16);
				}

				int32 MaxInstancesSub = FMath::Square(ForSubsectionMath.SqrtMaxInstances / SqrtSubsections);
				if (bUseHalton && MinDistanceToComp > DiscardDistance)
				{
					HaltonBaseIndex += MaxInstancesSub * SqrtSubsections * SqrtSubsections;
					continue;
				}
				//UE_LOG(LogTemp, Warning, TEXT("[MkGpuScattering] SqrtSubsections %d, MaxInstancesSub %d, SqrtMaxInstances %d"), SqrtSubsections, MaxInstancesSub, ForSubsectionMath.SqrtMaxInstances);

				FBox LocalBox = LandscapeComponent->CachedLocalBox;
				FVector LocalExtentDiv = (LocalBox.Max - LocalBox.Min) * FVector(1.0f / float(SqrtSubsections), 1.0f / float(SqrtSubsections), 1.0f);
				for (int32 SubX = 0; SubX < SqrtSubsections; SubX++)
				{
					for (int32 SubY = 0; SubY < SqrtSubsections; SubY++)
					{
						float MinDistanceToSubComp = MinDistanceToComp;

						FBox WorldSubBox;

						if ((bCullSubsections && SqrtSubsections > 1))// || Component->ActiveExcludedBoxes.Num())
						{
							FVector BoxMin;
							BoxMin.X = LocalBox.Min.X + LocalExtentDiv.X * float(SubX);
							BoxMin.Y = LocalBox.Min.Y + LocalExtentDiv.Y * float(SubY);
							BoxMin.Z = LocalBox.Min.Z;

							FVector BoxMax;
							BoxMax.X = LocalBox.Min.X + LocalExtentDiv.X * float(SubX + 1);
							BoxMax.Y = LocalBox.Min.Y + LocalExtentDiv.Y * float(SubY + 1);
							BoxMax.Z = LocalBox.Max.Z;

							FBox LocalSubBox(BoxMin, BoxMax);
							WorldSubBox = LocalSubBox.TransformBy(LandscapeComponent->GetComponentTransform());

							//if (bCullSubsections && SqrtSubsections > 1)
							{
								MinDistanceToSubComp = Cameras.Num() ? MAX_flt : 0.0f;
								for (auto& Pos : Cameras)
								{
									MinDistanceToSubComp = FMath::Min<float>(MinDistanceToSubComp, static_cast<float>(ComputeSquaredDistanceFromBoxToPoint(WorldSubBox.Min, WorldSubBox.Max, Pos)));
								}
								MinDistanceToSubComp = FMath::Sqrt(MinDistanceToSubComp);
							}
						}

						if (bUseHalton)
						{
							HaltonBaseIndex += MaxInstancesSub;  // we are going to pre-increment this for all of the continues...however we need to subtract later if we actually do this sub
						}

						if (MinDistanceToSubComp > DiscardDistance)
						{
							continue;
						}


						FMkCachedLandscapeFoliage::FGrassComp NewComp;
						NewComp.Key.BasedOn = LandscapeComponent;
						NewComp.Key.SqrtSubsections = SqrtSubsections;
						NewComp.Key.CachedMaxInstancesPerComponent = MaxInstancesPerComponent;
						NewComp.Key.SubsectionX = SubX;
						NewComp.Key.SubsectionY = SubY;
						NewComp.Key.NumVarieties = ScatteringType->GrassVarieties.Num();
						NewComp.Key.VarietyIndex = GrassVarietyIndex;

						uint32 HaltonIndexForSub = 0;
						if (bUseHalton)
						{
							check(HaltonBaseIndex > (uint32)MaxInstancesSub);
							HaltonIndexForSub = HaltonBaseIndex - (uint32)MaxInstancesSub;
						}

						//UE_LOG(LogTemp, Log, TEXT("!!!!!!!! HaltonIndexForSub %d"), HaltonIndexForSub);

						FMkCachedLandscapeFoliage::FGrassComp* Existing = FoliageCache.CachedGrassComps.Find(NewComp.Key);
						if (Existing)
						{
							Existing->Touch();
							continue;
						}
						if (InOutNumCompsCreated >= GrassMaxCreatePerFrame)
						{
							continue;
						}
						InOutNumCompsCreated++;
						//UE_LOG(LogTemp, Warning, TEXT("Frame %d(%s), InOutNumCompsCreated %d"), GFrameCounter, *LandscapeProxy->GetName(), InOutNumCompsCreated);

						//UE_LOG(LogTemp, Warning, TEXT("LandscapeComponent->GetName().ToLower() %s"), *LandscapeComponent->GetName().ToLower());
						int32 FolSeed = FCrc::StrCrc32(StringCast<ANSICHAR>(*FString::Printf(TEXT("%s%d %d %d"), *LandscapeComponent->GetName().ToLower(), SubX, SubY, GrassVarietyIndex)).Get());
						if (FolSeed == 0)
						{
							FolSeed++;
						}

						// Do not record the transaction of creating temp component for visualizations
						ClearFlags(RF_Transactional);
						bool PreviousPackageDirtyFlag = GetOutermost()->IsDirty();

						UHierarchicalInstancedStaticMeshComponent* HISMC = CreateHISMC(LandscapeProxy, GrassVariety, FolSeed);
						NewComp.CachedBuffers = new FMkGpuScatteringCachedBuffers();
						NewComp.Foliage = HISMC;

#if WITH_EDITOR
						LandscapeProxy->AddInstanceComponent(HISMC);
#endif
						FMkGpuScatteringCS_Param* Param = new FMkGpuScatteringCS_Param(
							this
							, SpawnLayerName
							, BlockingLayerName
							, LandscapeProxy
							, NewComp
							, &GrassVariety
							, HaltonIndexForSub
							, MaxInstancesPerComponent
							, ReadbackManager
						);

						if (ScatteringType->bEnableSpawnLayer && !Param->WeightmapTexture)
						{
							FoliageCache.CachedGrassComps.Add(NewComp);

							SetFlags(RF_Transactional);
							GetOutermost()->SetDirtyFlag(PreviousPackageDirtyFlag);
							continue;
						}

						if (!AsyncBuilderInterface)
						{
							AsyncBuilderInterface = new FMkAsyncBuilderInterface();
						}
						AsyncBuilderInterface->Dispatch(*Param);
						delete(Param);

						FoliageCache.CachedGrassComps.Add(NewComp);

						SetFlags(RF_Transactional);
						GetOutermost()->SetDirtyFlag(PreviousPackageDirtyFlag);
					}
				}
			}
		}
	}
}


void UMkGpuScatteringBuilder::WaitAndApplyResults()
{
	if (bPendingFlushCache)
	{
		return;
	}

	//
	LLM_SCOPE_BYTAG(MkGpuScatteringBuilder_WaitAndApply);

	TSet<UHierarchicalInstancedStaticMeshComponent*> StillUsed;
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_Grass_StillUsed);

		// trim cached items based on time, pending and emptiness
		double OldestToKeepTime = FPlatformTime::Seconds() - GMkGpuScatteringMinTimeToKeepGrass;
		uint32 OldestToKeepFrame = GFrameNumber - GMkGpuScatteringMinTimeToKeepGrass * GetGrassUpdateInterval();

		for (FMkCachedLandscapeFoliage::TGrassSet::TIterator Iter(FoliageCache.CachedGrassComps); Iter; ++Iter)
		{
			/*const*/ FMkCachedLandscapeFoliage::FGrassComp& GrassItem = *Iter;
			UHierarchicalInstancedStaticMeshComponent* Used = GrassItem.Foliage.Get();
			bool bOld =	!GrassItem.Pending
				&& (!GrassItem.Key.BasedOn.Get()
					|| /*!GrassItem.Key.GrassType.Get() ||*/
					!Used || (GrassItem.LastUsedFrameNumber < OldestToKeepFrame && GrassItem.LastUsedTime < OldestToKeepTime));

			if (bOld)
			{
				delete(GrassItem.CachedBuffers);

				Iter.RemoveCurrent();
			}
			else if (Used)
			{
				if (!StillUsed.Num())
				{
					StillUsed.Reserve(FoliageCache.CachedGrassComps.Num());
				}
				if (Used)
				{
					//UE_LOG(LogTemp, Warning, TEXT("PerInstanceSMData.Num %d"), Used->PerInstanceSMData.Num());
					StillUsed.Add(Used);
				}
			}
		}
	}

	if (StillUsed.Num() < FoliageComponents.Num())
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_Grass_DelComps);

		// delete components that are no longer used
		for (int32 Index = 0; Index < FoliageComponents.Num(); Index++)
		{
			UHierarchicalInstancedStaticMeshComponent* HComponent = FoliageComponents[Index];
			if (!StillUsed.Contains(HComponent))
			{
				if (HComponent)
				{
					HComponent->ClearInstances();
					HComponent->DetachFromComponent(FDetachmentTransformRules(EDetachmentRule::KeepRelative, false));
					HComponent->DestroyComponent();
				}
				FoliageComponents.RemoveAtSwap(Index--);

				//if (!bForceSync)
				{
					break; // one per frame is fine
				}
			}
		}
	}

	if (TransformBuilders.IsEmpty())
	{
		return;
	}

	for (int32 Index = 0; Index < TransformBuilders.Num(); Index++)
	{
		if (!TransformBuilders.IsValidIndex(Index))
		{
			UE_LOG(LogTemp, Log, TEXT("Break loop. Index %d, TransformBuilders.Num() %d"), Index, TransformBuilders.Num());
			break;
		}

		FMkGpuScatteringTransformBuilder* TransformBuilder = TransformBuilders[Index];
		if (!TransformBuilder)
		{
			TransformBuilders.RemoveAtSwap(Index--);
			continue;
		}
		if (!TransformBuilder->IsDone)
		{
			continue;
		}



		int32 NumBuiltRenderInstances = TransformBuilder->InstanceBuffer.GetNumInstances();
		UHierarchicalInstancedStaticMeshComponent* HISMC = (TransformBuilder->HISMC.Get());
		if (HISMC && NumBuiltRenderInstances > 0)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_FoliageGrassEndComp_AcceptPrebuiltTree);

#if false
			//HISMC->ReleasePerInstanceRenderData();
			//
			// UE5.4 에서 InitPerInstanceRenderData, ReleasePerInstanceRenderData 는 아무일도 하지 않는 함수가 됨.
			//if (!HISMC->PerInstanceRenderData.IsValid())
			//{
			//	LLM_SCOPE_BYTAG(MkGpuScatteringBuilder_ApplyInstance);
			//	HISMC->InitPerInstanceRenderData(true, &TransformBuilder->InstanceBuffer, TransformBuilder->RequireCPUAccess);
			//}


			// UE5.4 에서 다른 인자값 타입을 사용하는 함수로 변경됨.
			//HISMC->AcceptPrebuiltTree(TransformBuilder->InstanceData, TransformBuilder->ClusterTree, TransformBuilder->OutOcclusionLayerNum, NumBuiltRenderInstances);

			// Nanite가 아닌 mesh의 collsion이 생성될 수 있도록 PerInstanceSMData 세팅.
			// Nanite는 AcceptPrebuiltTree에서 세팅됨.
			if (TransformBuilder->RequireCPUAccess && TransformBuilder->InstanceData.Num())
			{
				HISMC->PerInstanceSMData = MoveTemp(TransformBuilder->InstanceData);
			}

			if (!HISMC->IsRegistered())
			{
				HISMC->RegisterComponent();
			}
#else
			if (!HISMC->bDisableCollision)
			{
				TArray<FTransform> TMs;
				for (const FInstancedStaticMeshInstanceData& InstanceData : TransformBuilder->InstanceData)
				{
					FTransform TM(InstanceData.Transform);
					TMs.Add(TM);
				}
				HISMC->AddInstances(TMs, false, false);
			}
			else
			{
				if (UGrassInstancedStaticMeshComponent* GrassHISMC = Cast<UGrassInstancedStaticMeshComponent>(HISMC))
				{
					GrassHISMC->AcceptPrebuiltTree(TransformBuilder->ClusterTree, TransformBuilder->OutOcclusionLayerNum, NumBuiltRenderInstances, &TransformBuilder->InstanceBuffer);
				}
			}
#endif

			FMkCachedLandscapeFoliage::FGrassComp* Existing = FoliageCache.CachedGrassComps.Find(TransformBuilder->Key);
			if (Existing)
			{
				Existing->Pending = false;
				Existing->Touch();
			}
			TransformBuilder->Clear();

			delete(TransformBuilders[Index]);
			TransformBuilders.RemoveAtSwap(Index--);
			break;
		}
	}



}


void UMkGpuScatteringBuilder::FlushCache()
{
	bPendingFlushCache = true;

	for (FMkGpuScatteringTransformBuilder* TransformBuilder : TransformBuilders)
	{
		while (!TransformBuilder->IsDone)
		{
			continue;
		}
		TransformBuilder->Clear();
		delete(TransformBuilder);
	}
	TransformBuilders.Empty();
	FoliageCache.ClearCache();

	for (TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISMC : FoliageComponents)
	{
		HISMC->DestroyComponent();
	}
	FoliageComponents.Empty();

	ScatteringTypes.Empty();
	bPendingFlushCache = false;
}

void UMkGpuScatteringBuilder::UpdateTick(const TArray<FVector>& Cameras, float DeltaTime, int32& InOutNumComponentsCreated, UMkGpuScatteringReadbackManager* ReadbackManager)
{
	LLM_SCOPE_BYTAG(MkGpuScatteringBuilder_Tick);

	Build(Cameras, InOutNumComponentsCreated, ReadbackManager);
	WaitAndApplyResults();
}
//~ end of UMkGpuScatteringBuilder

MK_OPTIMIZATION_ON
