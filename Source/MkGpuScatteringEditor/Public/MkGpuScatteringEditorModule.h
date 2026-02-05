// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "AssetTypeCategories.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"


class FMkGpuScatteringEditorModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static EAssetTypeCategories::Type GetAssetsCategory() { return MkGpuScatteringAssetsCategory; }
	static FColor GetAssetTypeColorDefault() { return AssetTypeColorDefault; }

private:
	static EAssetTypeCategories::Type MkGpuScatteringAssetsCategory;
	static FColor AssetTypeColorDefault;

	void RegisterAssetTypeActions(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action)
	{
		AssetTools.RegisterAssetTypeActions(Action);
		CreatedAssetTypeActions.Add(Action);
	}

private:
	TArray<TSharedPtr<IAssetTypeActions>> CreatedAssetTypeActions;
	TSharedPtr<class FSlateStyleSet> StyleSet;

};
