// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "AssetTypeActions/AssetTypeActions_DataAsset.h"
#include "AssetTypeActions/AssetTypeActions_Blueprint.h"
#include "Styling/SlateStyle.h"
#include "Styling/StyleColors.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"

#include "MkGpuScatteringEditorModule.h"
#include "Types/MkGpuScatteringTypes.h"

#include "MkGpuScatteringAssetFactories.generated.h"



#ifndef MkGpuScattering_DECLARE_ASSET_TYPE_ACTION
#define MkGpuScattering_DECLARE_ASSET_TYPE_ACTION(type_name)\
class FAssetTypeActions_##type_name : public FAssetTypeActions_DataAsset\
{\
public:\
	virtual FText GetName() const override;\
	virtual uint32 GetCategories() override;\
	virtual FColor GetTypeColor() const override;\
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override;\
	virtual UClass* GetSupportedClass() const override;\
};
#endif



// MkGpuScatteringTypes
UCLASS()
class MKGPUSCATTERINGEDITOR_API UMkGpuScatteringTypes_Factory : public UFactory
{
	GENERATED_BODY()

public:
	UMkGpuScatteringTypes_Factory(const class FObjectInitializer& ObjectInitializer);
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};
MkGpuScattering_DECLARE_ASSET_TYPE_ACTION(MkGpuScatteringTypes)




