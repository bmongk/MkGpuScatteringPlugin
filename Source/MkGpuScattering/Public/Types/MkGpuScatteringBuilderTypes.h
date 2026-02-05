// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MkGpuScatteringTypes.h"

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"
#include "UObject/PerPlatformProperties.h"
#include "RenderGraphResources.h"  // FRDGPooledBuffer full definition


class ULandscapeComponent;
class UHierarchicalInstancedStaticMeshComponent;

struct FMkGpuScatteringBuilderOutput;

static float GMkGpuScatteringDensityScale = 1;
static FAutoConsoleVariableRef CVarMkGpuScatteringDensityScale(
	TEXT("MkGpuScattering.densityScale"),
	GMkGpuScatteringDensityScale,
	TEXT("Multiplier on all mk instance densities."),
	ECVF_Scalability);


//~ For cache
struct FMkGpuScatteringCachedBuffers
{
	// Multi-frame buffers used to store the instance data.
	mutable TRefCountPtr<FRDGPooledBuffer> Input_Buffer;
	mutable TRefCountPtr<FRDGPooledBuffer> ProgressInfo_Buffer;
	mutable TRefCountPtr<FRDGPooledBuffer> Result_Buffer;

	void SafeReleaseAll()
	{
		Input_Buffer.SafeRelease();
		ProgressInfo_Buffer.SafeRelease();
		Result_Buffer.SafeRelease();
	}
};

struct FMkCachedLandscapeFoliage
{
	struct FGrassCompKey
	{
		TWeakObjectPtr<ULandscapeComponent> BasedOn;
		int32 SqrtSubsections;
		int32 CachedMaxInstancesPerComponent;
		int32 SubsectionX;
		int32 SubsectionY;
		int32 NumVarieties;
		int32 VarietyIndex;

		FGrassCompKey()
			: SqrtSubsections(0)
			, CachedMaxInstancesPerComponent(0)
			, SubsectionX(0)
			, SubsectionY(0)
			, NumVarieties(0)
			, VarietyIndex(-1)
		{
		}
		inline bool operator==(const FGrassCompKey& Other) const
		{
			return
				SqrtSubsections == Other.SqrtSubsections &&
				CachedMaxInstancesPerComponent == Other.CachedMaxInstancesPerComponent &&
				SubsectionX == Other.SubsectionX &&
				SubsectionY == Other.SubsectionY &&
				BasedOn == Other.BasedOn &&
				NumVarieties == Other.NumVarieties &&
				VarietyIndex == Other.VarietyIndex;
		}

		friend uint32 GetTypeHash(const FGrassCompKey& Key)
		{
			return GetTypeHash(Key.BasedOn) ^ Key.SqrtSubsections ^ Key.CachedMaxInstancesPerComponent ^ (Key.SubsectionX << 16) ^ (Key.SubsectionY << 24) ^ (Key.NumVarieties << 3) ^ (Key.VarietyIndex << 13);
		}

	};

	struct FGrassComp
	{
		FGrassCompKey Key;
		FMkGpuScatteringBuilderOutput* BuilderOutput = nullptr;
		FMkGpuScatteringCachedBuffers* CachedBuffers = nullptr;

		TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> Foliage;

		TArray<FBox> ExcludedBoxes;
		uint32 LastUsedFrameNumber;
		uint32 ExclusionChangeTag;

		double LastUsedTime;
		bool Pending;
		bool PendingRemovalRebuild;

		FGrassComp()
			: ExclusionChangeTag(0)
			, Pending(true)
			, PendingRemovalRebuild(false)
		{
			Touch();
		}
		~FGrassComp()
		{
			CachedBuffers = nullptr;
			BuilderOutput = nullptr;
			Foliage = nullptr;
		}

		void Touch()
		{
			LastUsedFrameNumber = GFrameNumber;
			LastUsedTime = FPlatformTime::Seconds();
		}
	};

	struct FGrassCompKeyFuncs : BaseKeyFuncs<FGrassComp, FGrassCompKey>
	{
		static KeyInitType GetSetKey(const FGrassComp& Element)
		{
			return Element.Key;
		}

		static bool Matches(KeyInitType A, KeyInitType B)
		{
			return A == B;
		}

		static uint32 GetKeyHash(KeyInitType Key)
		{
			return GetTypeHash(Key);
		}
	};

	typedef TSet<FGrassComp, FGrassCompKeyFuncs> TGrassSet;
	TSet<FGrassComp, FGrassCompKeyFuncs> CachedGrassComps;

	void ClearCache()
	{
		for (FGrassComp& Comp : CachedGrassComps)
		{
			if (Comp.CachedBuffers)
			{
				Comp.CachedBuffers->SafeReleaseAll();
				delete(Comp.CachedBuffers);
			}
			Comp.CachedBuffers = nullptr;
			Comp.BuilderOutput = nullptr;
		}

		CachedGrassComps.Empty();
	}
};
//~!

namespace MkGpuScatteringBuilderTypes
{
	// Parameter를 늘리면 높이값 계산이 안되는거 같은 모습이 보임.
	// 모르는 제약 조건, 규칙 같은게 있는거 아닐까?
	struct FScatteringInput
	{
		FVector2f Origin;
		FVector2f Extent;
		FVector2f Offset;
		FVector2f SectionBase;

		FVector3f DrawScale;

		uint32 SqrtMaxInstances;
		uint32 HaltonBaseIndex;
		uint32 Stride;
	};

	// Todo. 정리
	struct FProgressInfo
	{
		uint32 Count = 0;
		uint32 MaxInstances = 0;
		//bool bComplete = false;
	};

	struct FLocationOnly
	{
		FVector3f Location;
	};

	struct FLocationAndNormal
	{
		FVector3f Location;
		FVector3f ComputedNormal;
	};

	struct FLocationNormalScaleZ
	{
		FVector3f Location;
		FVector3f ComputedNormal;
		float ScaleZ;
	};
};


struct FMkGpuScatteringBuilderOutput
{
	//TArray<MkGpuScatteringBuilderTypes::FLocationAndNormal> LocationAndNormals;
	TArray<MkGpuScatteringBuilderTypes::FLocationNormalScaleZ> ResultBuffer;

	TWeakObjectPtr<ULandscapeComponent> BasedOn;
	int32 SqrtSubsections;
	int32 CachedMaxInstancesPerComponent;
	int32 SubsectionX;
	int32 SubsectionY;
	int32 NumVarieties;
	int32 VarietyIndex;

	FMatrix XForm;

	const FMkGrassVariety* GrassVariety;
	bool RandomScale = false;

	FMkGpuScatteringBuilderOutput()
	{

	}

	FMkGpuScatteringBuilderOutput(
		TWeakObjectPtr<ULandscapeComponent> InBasedOn
		, int32 InSqrtSubsections
		, int32 InCachedMaxInstancesPerComponent
		, int32 InSubsectionX
		, int32 InSubsectionY
		, int32 InNumVarieties
		, int32 InVarietyIndex
		, FMatrix InXForm
		, const FMkGrassVariety* InGrassVariety
	)
		: BasedOn(InBasedOn)
		, SqrtSubsections(InSqrtSubsections)
		, CachedMaxInstancesPerComponent(InCachedMaxInstancesPerComponent)
		, SubsectionX(InSubsectionX)
		, SubsectionY(InSubsectionY)
		, NumVarieties(InNumVarieties)
		, VarietyIndex(InVarietyIndex)
		, XForm(InXForm)
		, GrassVariety(InGrassVariety)
	{}
};
