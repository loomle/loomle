// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Runtime/Pcg/TypedPcgExecutionCoordinator.h"
#include "Runtime/Pcg/TypedPcgWorldRegistry.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PCGComponent.h"
#include "PCGVolume.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
using namespace Loomle::Runtime;

struct FTypedPcgFixture
{
    ~FTypedPcgFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    bool Build(FString& OutError)
    {
        OutError.Reset();
        const FString Token = FGuid::NewGuid().ToString(
            EGuidFormats::Digits);
        PackageName = FString::Printf(
            TEXT("/Game/LoomleTests/PcgWorldRegistry/%s/L_Registry"),
            *Token);
        ObjectPath = PackageName + TEXT(".L_Registry");
        Filename = FPackageName::LongPackageNameToFilename(
            PackageName,
            FPackageName::GetMapPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(Filename),
            true);

        UPackage* Package = CreatePackage(*PackageName);
        const UWorld::InitializationValues InitValues =
            UWorld::InitializationValues()
                .RequiresHitProxies(false)
                .ShouldSimulatePhysics(false)
                .EnableTraceCollision(false)
                .CreateNavigation(false)
                .CreateAISystem(false)
                .AllowAudioPlayback(false)
                .CreatePhysicsScene(false);
        World = UWorld::CreateWorld(
            EWorldType::Editor,
            false,
            FName(TEXT("L_Registry")),
            Package,
            false,
            ERHIFeatureLevel::Num,
            &InitValues);
        if (World == nullptr
            || World->PersistentLevel == nullptr
            || World->GetPathName() != ObjectPath)
        {
            OutError = TEXT(
                "UE failed to create the typed-PCG World fixture.");
            return false;
        }
        World->SetFlags(RF_Public | RF_Standalone | RF_Transactional);

        FActorSpawnParameters Params;
        Params.Name = FName(TEXT("PCG_Volume"));
        Params.OverrideLevel = World->PersistentLevel;
        Params.ObjectFlags = RF_Transactional;
        Volume = World->SpawnActor<APCGVolume>(
            APCGVolume::StaticClass(),
            FTransform::Identity,
            Params);
        if (Volume == nullptr)
        {
            OutError = TEXT(
                "UE failed to spawn the typed-PCG fixture volume.");
            return false;
        }
        Component = Volume->PCGComponent.Get();
        if (Component == nullptr)
        {
            OutError = TEXT(
                "The typed-PCG fixture volume carries no PCG Component.");
            return false;
        }
        ActorId = Volume->GetActorGuid().ToString(
            EGuidFormats::DigitsWithHyphensLower);
        ComponentId = Component->GetFName().ToString();
        if (ActorId.IsEmpty()
            || ActorId == TEXT("00000000-0000-0000-0000-000000000000")
            || ComponentId.IsEmpty())
        {
            OutError = TEXT(
                "The typed-PCG fixture lacks persistent identity.");
            return false;
        }

        World->GetOutermost()->SetDirtyFlag(true);
        World->GetOutermost()->FullyLoad();
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.Error = GLog;
        if (!UPackage::SavePackage(
                World->GetOutermost(),
                World,
                *Filename,
                SaveArgs))
        {
            OutError = TEXT(
                "UE failed to save the typed-PCG World fixture.");
            return false;
        }
        World->GetOutermost()->SetDirtyFlag(false);
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles({Filename});
        if (GEditor == nullptr)
        {
            OutError = TEXT(
                "Typed-PCG tests require an Editor runtime.");
            return false;
        }
        OriginalEditorWorld = GEditor->GetEditorWorldContext().World();
        GEditor->GetEditorWorldContext().SetCurrentWorld(World);
        return true;
    }

    FString SourceTargetText() const
    {
        TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
        Target->SetStringField(TEXT("kind"), TEXT("target"));
        Target->SetStringField(TEXT("domain"), TEXT("pcg_component"));
        Target->SetStringField(TEXT("asset"), ObjectPath);
        Target->SetStringField(TEXT("actorId"), ActorId);
        Target->SetStringField(TEXT("source"), TEXT("native"));
        Target->SetStringField(TEXT("id"), ComponentId);
        Target->SetStringField(
            TEXT("type"),
            TEXT("/Script/PCG.PCGComponent"));
        FString Text;
        FJsonSerializer::Serialize(
            Target,
            TJsonWriterFactory<>::Create(&Text));
        return Text;
    }

    bool Cleanup(FString& OutError)
    {
        OutError.Reset();
        if (GEditor != nullptr
            && GEditor->GetEditorWorldContext().World() == World)
        {
            GEditor->GetEditorWorldContext().SetCurrentWorld(
                OriginalEditorWorld);
        }
        if (World != nullptr)
        {
            FAssetRegistryModule::AssetDeleted(World);
            UPackage* Package = World->GetOutermost();
            World->DestroyWorld(false);
            Package->SetDirtyFlag(false);
            Package->ClearFlags(RF_Public | RF_Standalone);
            World = nullptr;
            Component = nullptr;
            Volume = nullptr;
        }
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        if (!PackageName.IsEmpty())
        {
            if (UPackage* Package = FindPackage(
                    nullptr,
                    *PackageName))
            {
                Package->SetDirtyFlag(false);
                Package->ClearFlags(RF_Public | RF_Standalone);
            }
            CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        }
        if (!Filename.IsEmpty())
        {
            IFileManager::Get().Delete(*Filename, false, true, true);
            IFileManager::Get().DeleteDirectory(
                *FPaths::GetPath(Filename),
                false,
                true);
        }
        return true;
    }

    UWorld* World = nullptr;
    APCGVolume* Volume = nullptr;
    UPCGComponent* Component = nullptr;
    UWorld* OriginalEditorWorld = nullptr;
    FString PackageName;
    FString ObjectPath;
    FString Filename;
    FString ActorId;
    FString ComponentId;
};

TSharedRef<FJsonObject> Selector(
    const FString& WorldKind,
    const FString& PlayMode = FString(),
    const int32 PieInstance = -1)
{
    TSharedRef<FJsonObject> Selector = MakeShared<FJsonObject>();
    Selector->SetStringField(TEXT("worldKind"), WorldKind);
    if (!PlayMode.IsEmpty())
    {
        Selector->SetStringField(TEXT("playMode"), PlayMode);
        Selector->SetNumberField(
            TEXT("pieInstance"),
            static_cast<double>(PieInstance));
    }
    return Selector;
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTypedPcgWorldSelectorTest,
    "Loomle.Runtime.Pcg.World.SelectorNormalization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTypedPcgWorldSelectorTest::RunTest(const FString& Parameters)
{
    FTypedPcgWorldSelector EditorSelector;
    FString Error;
    TestTrue(
        TEXT("An editor selector parses"),
        EditorSelector.Parse(Selector(TEXT("editor")), Error));
    TestTrue(
        TEXT("The editor selector is valid and canonical"),
        EditorSelector.IsValid()
            && EditorSelector.NormalizeText() == TEXT("editor"));

    FTypedPcgWorldSelector PieSelector;
    TestTrue(
        TEXT("A pie play selector parses"),
        PieSelector.Parse(
            Selector(TEXT("pie"), TEXT("play"), 0),
            Error));
    TestTrue(
        TEXT("The pie play selector is canonical"),
        PieSelector.IsValid()
            && PieSelector.NormalizeText() == TEXT("pie:play:0"));

    FTypedPcgWorldSelector SimulateSelector;
    TestTrue(
        TEXT("A pie simulate selector parses"),
        SimulateSelector.Parse(
            Selector(TEXT("pie"), TEXT("simulate"), 1),
            Error));
    TestTrue(
        TEXT("The pie simulate selector is canonical"),
        SimulateSelector.IsValid()
            && SimulateSelector.NormalizeText() == TEXT("pie:simulate:1"));

    FTypedPcgWorldSelector BadKind;
    TestFalse(
        TEXT("An unknown worldKind is rejected"),
        BadKind.Parse(Selector(TEXT("preview")), Error));

    FTypedPcgWorldSelector BadMode;
    TestFalse(
        TEXT("An unknown pie playMode is rejected"),
        BadMode.Parse(Selector(TEXT("pie"), TEXT("steam")), Error));

    FTypedPcgWorldSelector EditorWithPieFields;
    TestFalse(
        TEXT("An editor selector rejects pie fields"),
        EditorWithPieFields.Parse(
            Selector(TEXT("editor"), TEXT("play"), 0),
            Error));

    FTypedPcgWorldSelector NegativeInstance;
    TestFalse(
        TEXT("A negative pieInstance is rejected"),
        NegativeInstance.Parse(
            Selector(TEXT("pie"), TEXT("play"), -2),
            Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTypedPcgWorldTicketTest,
    "Loomle.Runtime.Pcg.World.TicketLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTypedPcgWorldTicketTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr
        || GEditor->IsPlaySessionInProgress())
    {
        AddError(TEXT(
            "Typed-PCG World ticket coverage requires an idle Editor outside "
            "PIE."));
        return false;
    }
    FTypedPcgFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Typed-PCG World fixture builds"), Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    FTypedPcgWorldRegistry Registry;
    Registry.Startup();
    FTypedPcgWorldSelector EditorSelector;
    TestTrue(
        TEXT("The editor selector parses"),
        EditorSelector.Parse(Selector(TEXT("editor")), Error));

    UWorld* ResolvedWorld = nullptr;
    TestTrue(
        TEXT("The editor selector resolves the fixture World"),
        FTypedPcgWorldRegistry::ResolveWorld(
            EditorSelector,
            ResolvedWorld,
            Error)
            && ResolvedWorld == Fixture.World);

    const FString SourceText = Fixture.SourceTargetText();
    FTypedPcgTicketPtr Ticket = Registry.Prepare(
        EditorSelector,
        SourceText,
        Error);
    TestTrue(
        TEXT("Prepare issues a source-bound World ticket"),
        Ticket.IsValid()
            && Ticket->Id.StartsWith(TEXT("pcg_"))
            && Ticket->SelectorText == TEXT("editor")
            && Ticket->Epoch > 0
            && Ticket->ComponentPath == Fixture.Component->GetPathName());
    if (!Ticket.IsValid())
    {
        Registry.Shutdown();
        if (!Fixture.Cleanup(Error))
        {
            AddError(Error);
        }
        return false;
    }

    UPCGComponent* Component = nullptr;
    TestTrue(
        TEXT("Validate re-proves the live Component incarnation"),
        Registry.Validate(
            EditorSelector,
            Ticket,
            SourceText,
            Component,
            Error)
            && Component == Fixture.Component);

    TestFalse(
        TEXT("A mismatched selector fails validation"),
        [&]()
        {
            FTypedPcgWorldSelector PieSelector;
            PieSelector.Parse(
                Selector(TEXT("pie"), TEXT("play"), 0),
                Error);
            UPCGComponent* Ignored = nullptr;
            return Registry.Validate(
                PieSelector,
                Ticket,
                SourceText,
                Ignored,
                Error);
        }());

    TestFalse(
        TEXT("A mismatched source Target fails validation"),
        [&]()
        {
            UPCGComponent* Ignored = nullptr;
            return Registry.Validate(
                EditorSelector,
                Ticket,
                TEXT("{\"kind\":\"target\",\"domain\":\"pcg_component\"}"),
                Ignored,
                Error);
        }());

    Registry.InvalidateAll();
    TestFalse(
        TEXT("Epoch invalidation makes the ticket stale"),
        [&]()
        {
            UPCGComponent* Ignored = nullptr;
            return Registry.Validate(
                EditorSelector,
                Ticket,
                SourceText,
                Ignored,
                Error);
        }());

    Registry.Shutdown();
    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTypedPcgExecutionAdmissionTest,
    "Loomle.Runtime.Pcg.Execution.AdmissionGenerate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTypedPcgExecutionAdmissionTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr
        || GEditor->IsPlaySessionInProgress())
    {
        AddError(TEXT(
            "Typed-PCG execution coverage requires an idle Editor outside "
            "PIE."));
        return false;
    }
    FTypedPcgFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Typed-PCG execution fixture builds"), Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    FTypedPcgExecutionCoordinator Coordinator;
    Coordinator.Startup();
    FTypedPcgWorldSelector EditorSelector;
    TestTrue(
        TEXT("The execution editor selector parses"),
        EditorSelector.Parse(Selector(TEXT("editor")), Error));
    const FString SourceText = Fixture.SourceTargetText();

    // The fixture Component is not bound to a graph, so native admission
    // refuses a task; the coordinator must complete honestly as failed with
    // the kernel lifecycle and typed result.
    FTypedPcgTicketPtr Ticket = Coordinator.Prepare(
        EditorSelector,
        SourceText,
        Error);
    TestTrue(
        TEXT("Execution prepare issues a source-bound ticket"),
        Ticket.IsValid());

    FLoomleAsyncKernel::FRecordPtr Record = nullptr;
    TSharedPtr<FJsonObject> StartError;
    {
        FTypedPcgExecutionCoordinator::FStartOptions Options;
        Options.Selector = EditorSelector;
        Options.SourceTarget = SourceText;
        Options.Ticket = Ticket;
        Record = Coordinator.Start(Options, StartError);
    }
    TestTrue(
        TEXT("The generate admission returns a kernel record"),
        Record.IsValid() && StartError == nullptr);
    if (!Record.IsValid())
    {
        Coordinator.Shutdown();
        if (!Fixture.Cleanup(Error))
        {
            AddError(Error);
        }
        return false;
    }
    TestTrue(
        TEXT("The no-task generate reaches terminal"),
        Record->State
            == FLoomleAsyncKernel::EState::Terminal);

    TSharedPtr<FJsonObject> PollError;
    const TSharedPtr<FJsonObject> Polled = Coordinator.Poll(
        Record->Id,
        PollError);
    FString Status;
    TestTrue(
        TEXT("The typed result reports failed with the exact diagnostic"),
        Polled.IsValid()
            && PollError == nullptr
            && Polled->TryGetStringField(TEXT("status"), Status)
            && Status == TEXT("failed")
            && Polled->HasField(TEXT("error"))
            && Polled->HasField(TEXT("result")));
    const TSharedPtr<FJsonObject>* Result = nullptr;
    FString Operation;
    TestTrue(
        TEXT("The typed result carries the generate operation and component"),
        Polled.IsValid()
            && Polled->TryGetObjectField(TEXT("result"), Result)
            && Result != nullptr
            && (*Result).IsValid()
            && (*Result)->TryGetStringField(TEXT("operation"), Operation)
            && Operation == TEXT("generate"));

    // A second admission is immediately available after the terminal slot
    // frees.
    TSharedPtr<FJsonObject> SecondError;
    {
        FTypedPcgExecutionCoordinator::FStartOptions Options;
        Options.Selector = EditorSelector;
        Options.SourceTarget = SourceText;
        Options.Ticket = Ticket;
        const FLoomleAsyncKernel::FRecordPtr Second = Coordinator.Start(
            Options,
            SecondError);
        TestTrue(
            TEXT("A later admission is not busy after terminal"),
            Second.IsValid() && SecondError == nullptr);
    }

    Coordinator.Shutdown();
    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

#endif
