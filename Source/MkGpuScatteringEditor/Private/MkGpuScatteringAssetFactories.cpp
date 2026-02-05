// Copyright Epic Games, Inc. All Rights Reserved.

#include "MkGpuScatteringAssetFactories.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MkGpuScatteringAssetFactories)


#ifndef MKGPUSCATTERING_DEFINE_TYPE_FACTORY_BODY
#define MKGPUSCATTERING_DEFINE_TYPE_FACTORY_BODY(type_name)\
	U##type_name##_Factory::U##type_name##_Factory(const class FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)\
	{\
		SupportedClass = U##type_name::StaticClass();\
		bEditAfterNew = true;\
		bCreateNew = true;\
	}\
	UObject* U##type_name##_Factory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)\
	{\
		check(Class->IsChildOf(U##type_name::StaticClass()));\
		U##type_name* NewInstance = NewObject<U##type_name>(InParent, Class, Name, Flags | RF_Transactional, Context);\
		return NewInstance;\
	}
#endif

#ifndef MKGPUSCATTERING_DEFINE_ASSET_TYPE_ACTION_BODY
#define MKGPUSCATTERING_DEFINE_ASSET_TYPE_ACTION_BODY(type_name)\
	FText FAssetTypeActions_##type_name::GetName() const { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_"## #type_name, #type_name); }\
	uint32 FAssetTypeActions_##type_name::GetCategories() { return FMKGPUSCATTERINGEditorModule::GetAssetsCategory(); }\
	FColor FAssetTypeActions_##type_name::GetTypeColor() const { return FMKGPUSCATTERINGEditorModule::GetAssetTypeColorDefault(); }\
	FText FAssetTypeActions_##type_name::GetAssetDescription(const FAssetData& AssetData) const { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_"###type_name##"_Desc", #type_name); }\
	UClass* FAssetTypeActions_##type_name::GetSupportedClass() const { return U##type_name::StaticClass(); }
#endif


#ifndef MKGPUSCATTERING_DEFINE_ASSETTYPE_FACTORY
#define MKGPUSCATTERING_DEFINE_ASSETTYPE_FACTORY(type_name)\
	MKGPUSCATTERING_DEFINE_TYPE_FACTORY_BODY(type_name)\
	MKGPUSCATTERING_DEFINE_ASSET_TYPE_ACTION_BODY(type_name)
#endif



/*
*
* define FactoryBody and AssetTypeActionBody
*
*/
MKGPUSCATTERING_DEFINE_TYPE_FACTORY_BODY(MkGpuScatteringTypes)
FText FAssetTypeActions_MkGpuScatteringTypes::GetName() const {
	return FText::AsLocalizable_Advanced(L"AssetTypeActions", L"AssetTypeActions_""MkGpuScatteringTypes", L"MkGpuScatteringTypes");
} uint32 FAssetTypeActions_MkGpuScatteringTypes::GetCategories() {
	return FMkGpuScatteringEditorModule::GetAssetsCategory();
} FColor FAssetTypeActions_MkGpuScatteringTypes::GetTypeColor() const {
	return FMkGpuScatteringEditorModule::GetAssetTypeColorDefault();
} FText FAssetTypeActions_MkGpuScatteringTypes::GetAssetDescription(const FAssetData& AssetData) const {
	return FText::AsLocalizable_Advanced(L"AssetTypeActions", L"AssetTypeActions_""MkGpuScatteringTypes""_Desc", L"MkGpuScatteringTypes");
} UClass* FAssetTypeActions_MkGpuScatteringTypes::GetSupportedClass() const {
	return UMkGpuScatteringTypes::StaticClass();
}
