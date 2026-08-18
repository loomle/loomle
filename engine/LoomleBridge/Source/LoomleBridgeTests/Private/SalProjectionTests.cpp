// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/SalProjectionService.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

namespace
{
using namespace Loomle::Sal;

TSharedPtr<FJsonObject> ProjectionResult(const FString& Path)
{
    TSharedPtr<FJsonObject> Marker = MakeShared<FJsonObject>();
    Marker->SetStringField(
        FSalProjectionService::MarkerKey,
        Path);
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetObjectField(TEXT("result"), Marker);
    return Result;
}

bool ReadProjectionAnnex(
    const TSharedPtr<FJsonObject>& Result,
    bool& OutComplete,
    int32& OutMarked,
    int32& OutProjected)
{
    OutComplete = false;
    OutMarked = 0;
    OutProjected = 0;
    const TSharedPtr<FJsonObject>* Annex = nullptr;
    double Marked = 0.0;
    double Projected = 0.0;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("projection"), Annex)
        || Annex == nullptr
        || !(*Annex).IsValid()
        || !(*Annex)->TryGetBoolField(TEXT("complete"), OutComplete)
        || !(*Annex)->TryGetNumberField(TEXT("marked"), Marked)
        || !(*Annex)->TryGetNumberField(TEXT("projected"), Projected))
    {
        return false;
    }
    OutMarked = static_cast<int32>(Marked);
    OutProjected = static_cast<int32>(Projected);
    return true;
}

bool ReadMarkerReplacement(
    const TSharedPtr<FJsonObject>& Result,
    const TSharedPtr<FJsonObject>*& OutRecord)
{
    OutRecord = nullptr;
    const TSharedPtr<FJsonObject>* ResultObject = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("result"), ResultObject)
        || ResultObject == nullptr
        || !(*ResultObject).IsValid())
    {
        return false;
    }
    OutRecord = ResultObject;
    return true;
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalProjectionClassViewTest,
    "Loomle.Sal.Projection.ClassView",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalProjectionClassViewTest::RunTest(const FString& Parameters)
{
    TSharedPtr<FJsonObject> Result = ProjectionResult(
        TEXT("/Script/Engine.Actor"));
    TestTrue(
        TEXT("Projection completes for a native Class marker"),
        FSalProjectionService::ProjectResult(Result));
    bool bComplete = false;
    int32 Marked = 0;
    int32 Projected = 0;
    TestTrue(
        TEXT("Projection annex is reported"),
        ReadProjectionAnnex(Result, bComplete, Marked, Projected));
    TestTrue(
        TEXT("Projection annex is complete for a single projected view"),
        bComplete && Marked == 1 && Projected == 1);
    const TSharedPtr<FJsonObject>* Record = nullptr;
    TestTrue(
        TEXT("The Class marker is replaced by a projection record"),
        ReadMarkerReplacement(Result, Record));
    FString Status;
    FString Relation;
    TestTrue(
        TEXT("The Class record is projected"),
        Record != nullptr
            && (*Record)->TryGetStringField(TEXT("status"), Status)
            && Status == TEXT("projected"));
    TestTrue(
        TEXT("The Class record relation is exact"),
        Record != nullptr
            && (*Record)->TryGetStringField(TEXT("relation"), Relation)
            && Relation == TEXT("exact"));
    const TSharedPtr<FJsonObject>* View = nullptr;
    const TSharedPtr<FJsonObject>* Binding = nullptr;
    const TSharedPtr<FJsonObject>* Target = nullptr;
    FString Domain;
    FString Path;
    TestTrue(
        TEXT("The Class record carries a canonical class view"),
        Record != nullptr
            && (*Record)->TryGetObjectField(TEXT("view"), View)
            && View != nullptr
            && (*View).IsValid()
            && (*View)->TryGetObjectField(TEXT("target"), Binding)
            && Binding != nullptr
            && (*Binding).IsValid()
            && (*Binding)->TryGetObjectField(TEXT("target"), Target)
            && Target != nullptr
            && (*Target).IsValid()
            && (*Target)->TryGetStringField(TEXT("domain"), Domain)
            && Domain == TEXT("class")
            && (*Target)->TryGetStringField(TEXT("path"), Path)
            && Path == TEXT("/Script/Engine.Actor"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalProjectionStaleAndTransientTest,
    "Loomle.Sal.Projection.StaleAndTransient",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalProjectionStaleAndTransientTest::RunTest(const FString& Parameters)
{
    // A marker whose object no longer resolves reports stale.
    TSharedPtr<FJsonObject> StaleResult = ProjectionResult(
        TEXT("/Game/LoomleTests/DoesNotExist.DoesNotExist"));
    TestTrue(
        TEXT("Projection completes for a stale marker"),
        FSalProjectionService::ProjectResult(StaleResult));
    const TSharedPtr<FJsonObject>* StaleRecord = nullptr;
    TestTrue(
        TEXT("The stale marker is replaced"),
        ReadMarkerReplacement(StaleResult, StaleRecord));
    FString StaleStatus;
    TestTrue(
        TEXT("The unresolvable marker reports stale"),
        StaleRecord != nullptr
            && (*StaleRecord)->TryGetStringField(TEXT("status"), StaleStatus)
            && StaleStatus == TEXT("stale"));
    const TSharedPtr<FJsonObject>* StaleDiagnostics = nullptr;
    TestTrue(
        TEXT("The stale record reports the exact diagnostic code"),
        StaleRecord != nullptr
            && (*StaleRecord)->TryGetObjectField(
                TEXT("diagnostics"),
                StaleDiagnostics)
            && StaleDiagnostics != nullptr
            && (*StaleDiagnostics).IsValid());

    // A transient-only object reports unsupported with transient_only.
    UObject* Probe = NewObject<UObject>(
        GetTransientPackage(),
        FName(TEXT("LoomleProjectionTransientProbe")));
    if (!TestNotNull(
            TEXT("The transient projection probe is created"),
            Probe))
    {
        return false;
    }
    const FString ProbePath = Probe->GetPathName();
    TestTrue(
        TEXT("The transient probe path resolves through the marker protocol"),
        FindObject<UObject>(nullptr, *ProbePath) == Probe);
    TSharedPtr<FJsonObject> TransientResult = ProjectionResult(ProbePath);
    TestTrue(
        TEXT("Projection completes for a transient marker"),
        FSalProjectionService::ProjectResult(TransientResult));
    const TSharedPtr<FJsonObject>* TransientRecord = nullptr;
    TestTrue(
        TEXT("The transient marker is replaced"),
        ReadMarkerReplacement(TransientResult, TransientRecord));
    FString TransientStatus;
    TestTrue(
        TEXT("The transient object reports unsupported"),
        TransientRecord != nullptr
            && (*TransientRecord)->TryGetStringField(
                TEXT("status"),
                TransientStatus)
            && TransientStatus == TEXT("unsupported"));
    const TSharedPtr<FJsonObject>* TransientDiagnostics = nullptr;
    TestTrue(
        TEXT("The transient record reports the transient-only diagnostic"),
        TransientRecord != nullptr
            && (*TransientRecord)->TryGetObjectField(
                TEXT("diagnostics"),
                TransientDiagnostics)
            && TransientDiagnostics != nullptr
            && (*TransientDiagnostics).IsValid());
    return true;
}

#endif
