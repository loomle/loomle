// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/SalJson.h"
#include "Sal/SalObjectBuilder.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

namespace
{
using namespace Loomle::Sal;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalObjectBuilderNativeNameTest,
    "Loomle.Sal.ObjectBuilder.NativeNameFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalObjectBuilderNativeNameTest::RunTest(
    const FString& Parameters)
{
    const TSharedPtr<FJsonValue> Safe =
        Value::NameOrString(TEXT("Ready"));
    const TSharedPtr<FJsonObject>* SafeObject = nullptr;
    FString SafeKind;
    FString SafeName;
    TestTrue(
        TEXT("A non-reserved local identifier remains a SAL Name"),
        Safe.IsValid()
            && Safe->TryGetObject(SafeObject)
            && SafeObject != nullptr
            && (*SafeObject)->TryGetStringField(
                TEXT("kind"),
                SafeKind)
            && SafeKind == TEXT("name")
            && (*SafeObject)->TryGetStringField(
                TEXT("name"),
                SafeName)
            && SafeName == TEXT("Ready"));

    for (const FString& Text : {
             FString(TEXT("target")),
             FString(TEXT("object")),
             FString(TEXT("graph")),
             FString(TEXT("state_tree")),
             FString(TEXT("not valid"))})
    {
        FString Encoded;
        TestTrue(
            *FString::Printf(
                TEXT("Native Name %s falls back losslessly to a string"),
                *Text),
            Value::NameOrString(Text)->TryGetString(Encoded)
                && Encoded == Text);
    }
    TestTrue(
        TEXT("Local identifier predicate accepts ordinary atoms"),
        FSalObjectBuilder::IsLocalIdentifier(TEXT("Ready")));
    TestFalse(
        TEXT("Local identifier predicate rejects Core and Domain words"),
        FSalObjectBuilder::IsLocalIdentifier(TEXT("graph")));

    TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
    Fields->SetField(
        TEXT("NativeName"),
        Value::NameOrString(TEXT("graph")));
    FSalObjectBuilder Builder;
    Builder.AddLocalBinding(
        TEXT("entry"),
        Value::Call(TEXT("record"), Fields));
    TSharedPtr<FJsonObject> Result = Builder.BuildResult();
    Result->SetStringField(
        TEXT("targetContext"),
        TEXT("exact_target"));
    TSharedRef<FJsonObject> TargetValue =
        MakeShared<FJsonObject>();
    TargetValue->SetStringField(TEXT("kind"), TEXT("target"));
    TargetValue->SetStringField(TEXT("domain"), TEXT("class"));
    TargetValue->SetStringField(
        TEXT("path"),
        AActor::StaticClass()->GetPathName());
    TSharedRef<FJsonObject> TargetBinding =
        MakeShared<FJsonObject>();
    TargetBinding->SetStringField(TEXT("alias"), TEXT("actor_class"));
    TargetBinding->SetObjectField(TEXT("target"), TargetValue);
    Result->SetObjectField(TEXT("target"), TargetBinding);

    TSharedPtr<FJsonObject> ValidationError;
    TestTrue(
        TEXT("Reserved native Names encoded as strings satisfy outgoing Result validation"),
        FSalJson::ValidateResult(Result, ValidationError));
    return true;
}
}

#endif
