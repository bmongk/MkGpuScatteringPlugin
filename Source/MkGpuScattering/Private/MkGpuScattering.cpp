// Copyright Epic Games, Inc. All Rights Reserved.

#include "MkGpuScattering.h"

#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FMkGpuScatteringModule"

void FMkGpuScatteringModule::StartupModule()
{

	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("MkGpuScattering"))->GetBaseDir(), TEXT("Shaders/GpuScattering/Private"));
	AddShaderSourceDirectoryMapping(TEXT("/MkGPUPlacementShaders"), PluginShaderDir);

}

void FMkGpuScatteringModule::ShutdownModule()
{

}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMkGpuScatteringModule, MkGpuScattering)
