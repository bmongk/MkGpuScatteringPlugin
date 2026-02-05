// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"
#include "UObject/PerPlatformProperties.h"
#include "PerQualityLevelProperties.h"
#include "SceneTypes.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataAsset.h"
#include "MkGpuScatteringTypes.generated.h"




UENUM()
enum class EMkGrassScaling : uint8
{
	/** Grass instances will have uniform X, Y and Z scales. */
	Uniform,
	/** Grass instances will have random X, Y and Z scales. */
	Free,
	/** X and Y will be the same random scale, Z will be another */
	LockXY,
};

USTRUCT(BlueprintType)
struct FMkGrassVariety
{
	GENERATED_BODY()

	FMkGrassVariety();

	UPROPERTY(EditAnywhere, Category = Grass)
	TObjectPtr<UStaticMesh> GrassMesh;

	UPROPERTY(EditAnywhere, Category = Grass) FName CollisionProfileName = UCollisionProfile::NoCollision_ProfileName;

	UPROPERTY(EditAnywhere, Category = Grass, meta = (ToolTip = "Material Overrides."))
	TArray<TObjectPtr<class UMaterialInterface>> OverrideMaterials;

	/* Instances per 10 square meters. */
	/*UPROPERTY(EditAnywhere, Category = Grass, meta = (UIMin = 0, ClampMin = 0, UIMax = 1000, ClampMax = 1000))
	FPerPlatformFloat GrassDensity;*/

	UPROPERTY(EditAnywhere, Category = Grass, meta = (UIMin = 0, ClampMin = 0, UIMax = 1000, ClampMax = 10000000))
	FPerQualityLevelFloat GrassDensityQuality;

	/* If true, use a jittered grid sequence for placement, otherwise use a halton sequence. */
	UPROPERTY(EditAnywhere, Category = Placement)	bool bUseGrid;

	UPROPERTY(EditAnywhere, Category = Placement, meta = (EditCondition = "bUseGrid", UIMin = 0, ClampMin = 0, UIMax = 1, ClampMax = 1))	float PlacementJitter;

	/* The distance where instances will begin to fade out if using a PerInstanceFadeAmount material node. 0 disables. */
	//UPROPERTY(EditAnywhere, Category = Grass, meta = (UIMin = 0, ClampMin = 0, UIMax = 1000000, ClampMax = 1000000))	FPerPlatformInt StartCullDistance;

	UPROPERTY(EditAnywhere, Category = Grass, meta = (UIMin = 0, ClampMin = 0, UIMax = 1000000, ClampMax = 1000000))	FPerQualityLevelInt StartCullDistanceQuality;

	/**
	 * The distance where instances will have completely faded out when using a PerInstanceFadeAmount material node. 0 disables.
	 * When the entire cluster is beyond this distance, the cluster is completely culled and not rendered at all.
	 */
	//UPROPERTY(EditAnywhere, Category = Grass, meta = (UIMin = 0, ClampMin = 0, UIMax = 1000000, ClampMax = 1000000))	FPerPlatformInt EndCullDistance;

	UPROPERTY(EditAnywhere, Category = Grass, meta = (UIMin = 0, ClampMin = 0, UIMax = 1000000, ClampMax = 1000000))	FPerQualityLevelInt EndCullDistanceQuality;
	/**
	 * Specifies the smallest LOD that will be used for this component.
	 * If -1 (default), the MinLOD of the static mesh asset will be used instead.
	 */
	UPROPERTY(EditAnywhere, Category = Grass, meta = (UIMin = -1, ClampMin = -1, UIMax = 8, ClampMax = 8))
	int32 MinLOD;

	/** Specifies grass instance scaling type */
	UPROPERTY(EditAnywhere, Category = Placement)
	EMkGrassScaling Scaling;

	/** Specifies the range of scale, from minimum to maximum, to apply to a grass instance's X Scale property */
	UPROPERTY(EditAnywhere, Category = Placement)
	FFloatInterval ScaleX;

	/** Specifies the range of scale, from minimum to maximum, to apply to a grass instance's Y Scale property */
	UPROPERTY(EditAnywhere, Category = Placement, meta = (EditCondition = "Scaling == EMkGrassScaling::Free"))
	FFloatInterval ScaleY;

	/** Specifies the range of scale, from minimum to maximum, to apply to a grass instance's Z Scale property */
	UPROPERTY(EditAnywhere, Category = Placement, meta = (EditCondition = "Scaling == EMkGrassScaling::Free || Scaling == EMkGrassScaling::LockXY"))
	FFloatInterval ScaleZ;

	UPROPERTY(EditAnywhere, Category = Placement, meta = (UIMin = 0, ClampMin = 0, UIMax = 90, ClampMax = 90)) FFloatInterval Slope;
	UPROPERTY(EditAnywhere, Category = Placement, meta = (UIMin = -1000000.0, ClampMin = -1000000.0, UIMax = 1000000, ClampMax = 1000000)) FFloatInterval Height;
	UPROPERTY(EditAnywhere, Category = Placement, meta = (UIMin = 0.0, ClampMin = 0.0, UIMax = 100000, ClampMax = 100000)) float HeightFalloffRange;
	UPROPERTY(EditAnywhere, Category = Placement, meta = (UIMin = -1000.0, ClampMin = -1000.0, UIMax = 1000.0, ClampMax = 1000.0)) FFloatInterval ZOffset;

	//
	UPROPERTY(EditAnywhere, Category = Placement) bool bUseVoronoiNoise;
	UPROPERTY(EditAnywhere, Category = Placement, meta = (EditCondition = "bUseVoronoiNoise", UIMin = 0, ClampMin = 0, UIMax = 1, ClampMax = 1)) FFloatInterval VoronoiValidRange;
	UPROPERTY(EditAnywhere, Category = Placement, meta = (EditCondition = "bUseVoronoiNoise")) float VoronoiScale;
	UPROPERTY(EditAnywhere, Category = Placement, meta = (EditCondition = "bUseVoronoiNoise")) float VoronoiGroupSize;

	/** Whether the grass instances should be tilted to the normal of the landscape (true), or always vertical (false) */
	UPROPERTY(EditAnywhere, Category = Placement) bool AlignToSurface;
	UPROPERTY(EditAnywhere, Category = Placement, meta = (UIMin = 0, ClampMin = 0, UIMax = 359, ClampMax = 359, EditCondition = "AlignToSurface")) float AlignMaxAngle;

	/** Whether the grass instances should be placed at random rotation (true) or all at the same rotation (false) */
	UPROPERTY(EditAnywhere, Category = Placement) bool RandomRotation;
	UPROPERTY(EditAnywhere, Category = Placement) FVector RotationAxis;

	/* Whether to use the landscape's lightmap when rendering the grass. */
	UPROPERTY(EditAnywhere, Category = Placement) bool bCheckCloseLandscape;

	/* Whether to use the landscape's lightmap when rendering the grass. */
	UPROPERTY(EditAnywhere, Category = Placement) bool bUseLandscapeLightmap;


	/**
	 * Lighting channels that the grass will be assigned. Lights with matching channels will affect the grass.
	 * These channels only apply to opaque materials, direct lighting, and dynamic lighting and shadowing.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Grass)
	FLightingChannels LightingChannels;

	/** Whether the grass instances should receive decals. */
	UPROPERTY(EditAnywhere, Category = Grass)
	bool bReceivesDecals;

	/** Controls whether the primitive should affect dynamic distance field lighting methods. */
	UPROPERTY(EditAnywhere, Category = Grass)
	bool bAffectDistanceFieldLighting;

	/** Whether the grass should cast shadows when using non-precomputed shadowing. **/
	UPROPERTY(EditAnywhere, Category = Grass)
	bool bCastDynamicShadow;

	/** Whether the grass should cast contact shadows. **/
	UPROPERTY(EditAnywhere, Category = Grass)
	bool bCastContactShadow;

	/** Whether we should keep a cpu copy of the instance buffer. This should be set to true if you plan on using GetOverlappingXXXXCount functions of the component otherwise it won't return any data.**/
	UPROPERTY(EditAnywhere, Category = Grass)
	bool bKeepInstanceBufferCPUCopy;

	UPROPERTY(EditAnywhere, Category = Grass)
	bool bEvaluateWorldPositionOffset;

	/** Distance at which to grass instances should disable WPO for performance reasons */
	UPROPERTY(EditAnywhere, Category = Grass, meta = (EditCondition = "bEvaluateWorldPositionOffset"))
	uint32 InstanceWorldPositionOffsetDisableDistance;

	/** Control shadow invalidation behavior, in particular with respect to Virtual Shadow Maps and material effects like World Position Offset. */
	UPROPERTY(EditAnywhere, Category = Grass, AdvancedDisplay)
	EShadowCacheInvalidationBehavior ShadowCacheInvalidationBehavior;

	bool IsGrassQualityLevelEnable() const;

	int32 GetStartCullDistance() const;

	int32 GetEndCullDistance() const;

	float GetDensity() const;
};


//
UCLASS(BlueprintType, NotBlueprintable, Const)
class MKGPUSCATTERING_API UMkGpuScatteringTypes : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere) bool bEnable = true;

	UPROPERTY(EditAnywhere, Category = Grass) bool bEnableSpawnLayer = false;
	UPROPERTY(EditAnywhere, Category = Grass, meta = (EditCondition = "bEnableSpawnLayer")) FString SpawnLayerName;

	UPROPERTY(EditAnywhere, Category = Grass) bool bEnableBlockingLayer = false;
	UPROPERTY(EditAnywhere, Category = Grass, meta = (EditCondition = "bEnableBlockingLayer")) FString BlockingLayerName;
	UPROPERTY(EditAnywhere, Category = Grass) TArray<FMkGrassVariety> GrassVarieties;

protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent);
#endif
};
