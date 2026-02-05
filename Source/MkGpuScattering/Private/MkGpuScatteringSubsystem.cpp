#include "MkGpuScatteringSubsystem.h"
#include "MkGpuScatteringVolume.h"
#include "Types/MkGpuScatteringTypes.h"
#include "Builder/MkGpuScatteringBuilder.h"
#include "Readback/MkGpuScatteringReadbackManager.h"
#include "Shaders/MkGpuScatteringShaders.h"
#include "MkGpuScatteringGlobal.h"

#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeInfoMap.h"
#include "LandscapeComponent.h"
#include "LandscapeStreamingProxy.h"
#include "DrawDebugHelpers.h"
#include "HAL/LowLevelMemTracker.h"

#include "EngineUtils.h"

LLM_DEFINE_TAG(MkGpuScatteringSubsystem_Tick);
LLM_DEFINE_TAG(MkGpuScatteringSubsystem_FlushCache);
LLM_DEFINE_TAG(MkGpuScatteringSubsystem_RenderThread);


#include UE_INLINE_GENERATED_CPP_BY_NAME(MkGpuScatteringSubsystem)

MK_OPTIMIZATION_OFF
bool bEnableMkGpuScattering = true;
FAutoConsoleVariableRef EnableMkGpuScatteringVar(
	TEXT("MkGpuScattering.Enable"),
	bEnableMkGpuScattering,
	TEXT(""));

bool bEnableMkGpuScatteringEditorTick = true;
FAutoConsoleVariableRef EnableMkGpuScatteringEditorTickVar(
	TEXT("MkGpuScattering.EnableEditorTick"),
	bEnableMkGpuScatteringEditorTick,
	TEXT(""));

TStatId UMkGpuScatteringSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMkGpuScatteringSubsystem, STATGROUP_Tickables);
}

void UMkGpuScatteringSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!ReadbackManager)
	{
		ReadbackManager = NewObject<UMkGpuScatteringReadbackManager>();
		ReadbackManager->AddToRoot();
	}
}

void UMkGpuScatteringSubsystem::Deinitialize()
{
	FlushCache();

	if (ReadbackManager)
	{
		ReadbackManager->ClearAll();
		ReadbackManager->RemoveFromRoot();
		ReadbackManager->ConditionalBeginDestroy();
		UE_LOG(LogTemp, Log, TEXT("[UMkGpuScatteringSubsystem::Deinitialize] ReadbackManager->RemoveFromRoot()"));
	}

	Volumes.Empty();
	Super::Deinitialize();
}

bool UMkGpuScatteringSubsystem::CollectInstanceBuilder(const TArray<FVector>& Cameras, TArray<UMkGpuScatteringBuilder*>& OutInstanceBuilders)
{
	UWorld* World = GetTickableGameObjectWorld();
	if (!World)
	{
		return false;
	}

	ULandscapeInfoMap* LandscapeInfoMap = ULandscapeInfoMap::FindLandscapeInfoMap(World);
	if (!LandscapeInfoMap)
	{
		return false;
	}

	OutInstanceBuilders.Reset();

	struct FSortedBuilderElement
	{
		FSortedBuilderElement(UMkGpuScatteringBuilder* InBuilder, float InMinDistance)
			: Builder(InBuilder)
			, MinDistance(InMinDistance)
		{

		}

		UMkGpuScatteringBuilder* Builder;
		float MinDistance;
	};
	TArray<FSortedBuilderElement> SortedBuilders;

	for (const auto& Pair : LandscapeInfoMap->Map)
	{
		ULandscapeInfo* LandscapeInfo = Pair.Value;
		if (!LandscapeInfo)
		{
			continue;
		}

		if (LandscapeInfo->StreamingProxies.Num() > 0)
		{
			for (TWeakObjectPtr<ALandscapeStreamingProxy> StreamingProxyPtr : LandscapeInfo->StreamingProxies)
			{
				if (ALandscapeProxy* LandscapeProxy = StreamingProxyPtr.Get())
				{
					UMkGpuScatteringBuilder* Builder = LandscapeProxy->GetComponentByClass<UMkGpuScatteringBuilder>();
					if (!Builder)
					{
						Builder = NewObject<UMkGpuScatteringBuilder>(LandscapeProxy, TEXT("MkGpuScatteringBuilder"), RF_Transient);
						Builder->SetLandscapeProxy(LandscapeProxy);

						//Builder->RegisterComponent();
					}

					if (Builder->ShouldTickGrass())
					{
						float MinSqrDistance = Cameras.Num() ? MAX_flt : 0.0f;
						for (const FVector& CameraPos : Cameras)
						{
							MinSqrDistance = FMath::Min<float>(MinSqrDistance, static_cast<float>(FVector::Distance(CameraPos, LandscapeProxy->GetActorLocation())));
						}

						SortedBuilders.Add(FSortedBuilderElement(Builder, MinSqrDistance));
					}

					for (TWeakObjectPtr<AMkGpuScatteringVolume> Volume : Volumes)
					{
						FBox Bounds = LandscapeProxy->GetComponentsBoundingBox(false);
						if (!Bounds.IntersectXY(Volume->GetBounds().GetBox()))
						{
							continue;
						}

						Builder->SetScatteringTypes(Volume->GetScatteringTypes());
						break;
					}
				}
			}
			continue;
		}

#if false // TODO: fix this
		if (ALandscape* Landscape = LandscapeInfo->LandscapeActor.Get())
		{
			UMkGpuScatteringBuilder* Builder = Landscape->GetComponentByClass<UMkGpuScatteringBuilder>();
			if (!Builder)
			{
				Builder = NewObject<UMkGpuScatteringBuilder>(Landscape);
				Builder->SetLandscapeProxy(Landscape);
			}

			if (Builder->ShouldTickGrass())
			{
				OutInstanceBuilder.Add(Builder);
			}

			for (TWeakObjectPtr<AMkGpuScatteringVolume> Volume : Volumes)
			{
				FBox Bounds = Landscape->GetComponentsBoundingBox(false);
				if (!Bounds.IntersectXY(Volume->GetBounds().GetBox()))
				{
					continue;
				}

				Builder->SetScatteringTypes(Volume->ScatteringTypes);
				break;
			}
			continue;
		}
#endif
	}

	Algo::Sort(SortedBuilders, [](const FSortedBuilderElement& A, const FSortedBuilderElement& B) { return A.MinDistance < B.MinDistance; });

	//UE_LOG(LogTemp, Warning, TEXT("!-------------------"));
	for (const FSortedBuilderElement& Element : SortedBuilders)
	{
		OutInstanceBuilders.Add(Element.Builder);
		//UE_LOG(LogTemp, Warning, TEXT("MinDistance %f"), Element.MinDistance);
	}
	//UE_LOG(LogTemp, Warning, TEXT("-------------------!"));

	return true;
}

void CollectLandscapeComponents(UWorld* World, TArray<TObjectPtr<ULandscapeComponent>>& OutLandscapeComponents)
{
	ULandscapeInfoMap* LandscapeInfoMap = ULandscapeInfoMap::FindLandscapeInfoMap(World);
	if (!LandscapeInfoMap)
	{
		return;
	}

	OutLandscapeComponents.Reset();
	for (const auto& Pair : LandscapeInfoMap->Map)
	{
		ULandscapeInfo* LandscapeInfo = Pair.Value;
		if (!LandscapeInfo)
		{
			continue;
		}

		//
		if (LandscapeInfo->StreamingProxies.Num() > 0)
		{
			for (TWeakObjectPtr<ALandscapeStreamingProxy> StreamingProxyPtr : LandscapeInfo->StreamingProxies)
			{
				if (ALandscapeProxy* LandscapeProxy = StreamingProxyPtr.Get())
				{
					UMkGpuScatteringBuilder* Builder = LandscapeProxy->GetComponentByClass<UMkGpuScatteringBuilder>();
					if (!Builder)
					{
						Builder = NewObject<UMkGpuScatteringBuilder>(LandscapeProxy, TEXT("Builder"));
						Builder->RegisterComponent();
#if WITH_EDITOR

						LandscapeProxy->AddInstanceComponent(Builder);

#endif
					}

					if (Builder->ShouldTickGrass())
					{
						OutLandscapeComponents.Append(LandscapeProxy->LandscapeComponents);
					}
				}
			}

			continue;
		}

		if (ALandscape* Landscape = LandscapeInfo->LandscapeActor.Get())
		{
			UMkGpuScatteringBuilder* Builder = Landscape->GetComponentByClass<UMkGpuScatteringBuilder>();
			if (!Builder)
			{
				Builder = NewObject<UMkGpuScatteringBuilder>(Landscape, TEXT("Builder"));
				Builder->RegisterComponent();
#if WITH_EDITOR

				Landscape->AddInstanceComponent(Builder);

#endif
			}

			if (Builder->ShouldTickGrass())
			{
				OutLandscapeComponents.Append(Landscape->LandscapeComponents);
			}
			continue;
		}
		//
	}
}

void UMkGpuScatteringSubsystem::FlushCache()
{
	LLM_SCOPE_BYTAG(MkGpuScatteringSubsystem_FlushCache);

	if (!Volumes.Num())
	{
		CollectVolumes();
	}
	else
	{
		for (TWeakObjectPtr<AMkGpuScatteringVolume> Volume : Volumes)
		{
			if (!Volume.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("[UMkGpuScatteringSubsystem::FlushCache] Volume is invalid"))
					continue;
			}

			Volume->FlushCache();
		}
	}

	for (ALandscapeProxy* Landscape : TObjectRange<ALandscapeProxy>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::Garbage))
	{
		UMkGpuScatteringBuilder* Builder = Landscape->GetComponentByClass<UMkGpuScatteringBuilder>();
		if (Builder)
		{
			Builder->FlushCache();
		}

		Landscape->FlushGrassComponents();
	}

	CurrentBuilders.Empty();

	if (!bEnableMkGpuScattering)
	{
		return;
	}

	/*if (ReadbackManager)
	{
		ReadbackManager->ClearAll();
	}*/
}


static void FlushMkGpuScattering(const TArray<FString>& Args)
{
	UWorld* World = GWorld->GetWorld();
	if (!World)
	{
		return;
	}
	if (UMkGpuScatteringSubsystem* Subsystem = World->GetSubsystem<UMkGpuScatteringSubsystem>())
	{
		Subsystem->FlushCache();
	}
}

static FAutoConsoleCommand FlushMkGpuScatteringCacheCmd(
	TEXT("MkGpuScattering.FlushCache"),
	TEXT("Flush the grass cache, debugging."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&FlushMkGpuScattering)
);

bool UMkGpuScatteringSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return true;
}

bool UMkGpuScatteringSubsystem::IsTickableInEditor() const
{
	return bEnableMkGpuScatteringEditorTick;
}


void UMkGpuScatteringSubsystem::Tick(float DeltaTime)
{
	LLM_SCOPE_BYTAG(MkGpuScatteringSubsystem_Tick);

	if (!bEnableMkGpuScattering)
	{
		OldCameras.Empty();
		return;
	}

#if WITH_EDITOR
	CollectVolumes();
#endif

	UWorld* World = GetTickableGameObjectWorld();
	TArray<FVector>* Cameras = nullptr;

	int32 Num = IStreamingManager::Get().GetNumViews();
	if (Num)
	{
		OldCameras.Reset(Num);
		for (int32 Index = 0; Index < Num; Index++)
		{
			auto& ViewInfo = IStreamingManager::Get().GetViewInformation(Index);
			OldCameras.Add(ViewInfo.ViewOrigin);
		}
		Cameras = &OldCameras;
	}
	if (!Cameras)
	{
		return;
	}

	TArray<UMkGpuScatteringBuilder*> CollectedBuilders;
	if (CollectInstanceBuilder(*Cameras, CollectedBuilders))
	{
		CurrentBuilders = CollectedBuilders;
	}

	int32 InOutNumComponentsCreated = 0;
	for (UMkGpuScatteringBuilder* Builder : CurrentBuilders)
	{
		Builder->UpdateTick(*Cameras, DeltaTime, InOutNumComponentsCreated, ReadbackManager);
	}

	ENQUEUE_RENDER_COMMAND(MkReadbackManagerUpdate)([ReadbackManager = ReadbackManager](FRHICommandListImmediate& RHICmdList)
		{
			LLM_SCOPE_BYTAG(MkGpuScatteringSubsystem_RenderThread);
			if (ReadbackManager)
			{
				ReadbackManager->Readback(RHICmdList);
			}
		});
}

void UMkGpuScatteringSubsystem::AddVolume(AMkGpuScatteringVolume* Volume)
{
	Volumes.Add(Volume);
}

void UMkGpuScatteringSubsystem::RemoveVolume(AMkGpuScatteringVolume* Volume)
{
	Volumes.Remove(Volume);
}

void UMkGpuScatteringSubsystem::CollectVolumes()
{
	Volumes.Reset(0);

	for (AMkGpuScatteringVolume* Volume : TActorRange<AMkGpuScatteringVolume>(GetTickableGameObjectWorld()))
	{
		Volumes.Add(Volume);
	}
}

void UMkGpuScatteringSubsystem::PoissonDiskSamplingTest()
{
	auto StartTime = FPlatformTime::Seconds();

	// Generate Poisson disk sampling pdf
	// https://www.cs.ubc.ca/~rbridson/docs/bridson-siggraph07-poissondisk.pdf
	UE_LOG(LogTemp, Log, TEXT("PoissonDiskSamplingTest"));
	int32 RowCol = PoissonDiskRowCol;

	struct FCell
	{
		int32 Index = -1;
		FVector2D Position = FVector2D::ZeroVector;
	};

	int32 NumCells = RowCol * RowCol;
	TArray<FCell> Grid;
	Grid.Reserve(NumCells);

	for (int32 i = 0; i < NumCells; ++i)
	{
		Grid.Emplace(FCell());
	}

	TArray<FCell> ActiveList;
	FRandomStream RandomStream(PoissonRandomSeed);

	for (int32 i = 0; i < NumCells; i++)
	{
		int32 ClampedIndex = FMath::FloorToInt32(RandomStream.FRandRange(0, NumCells - 1));

		int32 X = ClampedIndex % RowCol;
		int32 Y = ClampedIndex / RowCol;


		FVector2D NewPosition = FVector2D(RandomStream.FRandRange(0.0f, PoissonCellSize), RandomStream.FRandRange(0.0f, PoissonCellSize));
		NewPosition = NewPosition + FVector2D(X, Y) * PoissonCellSize;

		bool bOccupied = false;
		for (int32 dX = -1; dX <= 1; dX++)
		{
			for (int32 dY = -1; dY <= 1; dY++)
			{
				int32 X2 = X + dX;
				int32 Y2 = Y + dY;

				int32 Index = Y2 * RowCol + X2;
				if (X2 < 0 || X2 >= RowCol || Y2 < 0 || Y2 >= RowCol || ClampedIndex == Index)
				{
					continue;
				}

				if (Grid[Index].Index > -1)
				{
					if (FVector2D::Distance(Grid[Index].Position, NewPosition) < PoissonCellSize)
					{
						bOccupied = true;
						break;
					}
				}


			}
			if (bOccupied)
			{
				break;
			}

		}

		if (!bOccupied && Grid[ClampedIndex].Index == -1)
		{
			FCell NewCell;
			NewCell.Index = ClampedIndex;
			NewCell.Position = NewPosition;
			ActiveList.Add(NewCell);
			Grid[ClampedIndex] = NewCell;
		}
	}


	float BuildTime = FPlatformTime::Seconds() - StartTime;
	UE_LOG(LogTemp, Log, TEXT("BuildTime %f"), BuildTime);

	// Print Grid num, ActiveList num
	UE_LOG(LogTemp, Log, TEXT("Grid Num %d, ActiveList Num %d"), Grid.Num(), ActiveList.Num());


	for (int32 i = 0; i < NumCells; ++i)
	{
		int32 X = i % RowCol;
		int32 Y = i / RowCol;
		FVector DrawPosition = PoissonGridOrigin;
		DrawPosition.X += X * PoissonCellSize;
		DrawPosition.Y += Y * PoissonCellSize;
		//DrawDebugPoint(GetTickableGameObjectWorld(), DrawPosition, 10.0f, FColor::Red, false, 10.0f);
	}

	for(const FCell& Cell : ActiveList)
	{
		int32 i = Cell.Index;
		int32 X = i % RowCol;
		int32 Y = i / RowCol;
		FVector DrawPosition = PoissonGridOrigin;
		DrawPosition.X += Cell.Position.X;
		DrawPosition.Y += Cell.Position.Y;
		DrawDebugPoint(GetTickableGameObjectWorld(), DrawPosition, 10.0f, FColor::Blue, false, 10.0f);
	}
}
MK_OPTIMIZATION_ON
