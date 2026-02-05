// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MkGpuScatteringEditor : ModuleRules
{
	public MkGpuScatteringEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        if (Target.bBuildEditor)
        {
            bUseUnity = false;
            MinFilesUsingPrecompiledHeaderOverride = 1;

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "UnrealEd",
                }
                );
        }

        PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);


		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);


		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
                "AssetTools",
                "MkGpuScattering",
				"Foliage"
			}
			);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
                "MkGpuScattering"
			}
			);


		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
