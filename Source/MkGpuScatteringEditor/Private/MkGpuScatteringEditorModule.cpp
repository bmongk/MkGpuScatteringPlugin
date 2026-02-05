// Copyright Epic Games, Inc. All Rights Reserved.

#include "MkGpuScatteringEditorModule.h"
#include "MkGpuScatteringAssetFactories.h"

#include "AssetToolsModule.h"
#include "Styling/SlateStyle.h"
#include "Styling/StyleColors.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"

#define LOCTEXT_NAMESPACE "FMkGpuScatteringEditorModule"


EAssetTypeCategories::Type FMkGpuScatteringEditorModule::MkGpuScatteringAssetsCategory;
FColor FMkGpuScatteringEditorModule::AssetTypeColorDefault(128, 32, 210);


/** Custom style set for MkGpuScattering */
class FMkGpuScatteringSlateStyle final : public FSlateStyleSet
{
public:
	FMkGpuScatteringSlateStyle()
		: FSlateStyleSet("FMkGpuScatteringEditor")
	{
		SetParentStyleName(FAppStyle::GetAppStyleSetName());

		// The icons are located in /Plugins/MkGpuScattering/Content/Editor/Slate/Icons
		SetContentRoot(FPaths::ProjectPluginsDir() / TEXT("MkGpuScattering/Content/Editor/Slate"));
		SetCoreContentRoot(FPaths::ProjectPluginsDir() / TEXT("Slate"));

		// MkGpuScattering Input Editor icons
		static const FVector2D SizeIcon16 = FVector2D(16.0f, 16.0f);
		static const FVector2D SizeIcon64 = FVector2D(64.0f, 64.0f);

		/*Set("MkGpuScatteringIcon_Small", new IMAGE_BRUSH("Icons/MkGpuScattering_16", Icon16));
		Set("MkGpuScatteringIcon_Large", new IMAGE_BRUSH("Icons/MkGpuScattering_64", Icon64));*/

		auto lambdaSetStyle = [&](FString TypeName)
		{
			FString ClassIconName = FString("ClassIcon.") + TypeName;
			FString IconPath = FString("Icons/") + TypeName + FString("_16");

			FString ClassThumbnailName = FString("ClassThumbnail.") + TypeName;
			FString ThumbnailPath = FString("Icons/") + TypeName + FString("_64");

			Set(*ClassIconName, new IMAGE_BRUSH(*IconPath, SizeIcon16));
			Set(*ClassThumbnailName, new IMAGE_BRUSH(*ThumbnailPath, SizeIcon16));
		};

		lambdaSetStyle("MkGpuScatteringTypes");
	}
};




void FMkGpuScatteringEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	MkGpuScatteringAssetsCategory = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("MkGpuScattering")), LOCTEXT("MkGpuScatteringAssetsCategory", "MkGpuScattering"));
	{
		RegisterAssetTypeActions(AssetTools, MakeShareable(new FAssetTypeActions_MkGpuScatteringTypes));
	};

	StyleSet = MakeShared<FMkGpuScatteringSlateStyle>();
	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FMkGpuScatteringEditorModule::ShutdownModule()
{
	if (FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools"))
	{
		for (TSharedPtr<IAssetTypeActions>& AssetAction : CreatedAssetTypeActions)
		{
			AssetToolsModule->Get().UnregisterAssetTypeActions(AssetAction.ToSharedRef());
		}
	}
	CreatedAssetTypeActions.Empty();

	// Unregister slate stylings
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
	}
}




#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMkGpuScatteringEditorModule, MkGpuScatteringEditor)

