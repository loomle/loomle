// Copyright 2026 Loomle contributors.

#include "LglGraphProtocol.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "../LglDiagnostics.h"
#include "../LglObjectModel.h"

namespace Loomle::Lgl
{
TArray<TSharedPtr<FJsonValue>> ReadGraphPatchOps(const FLglObjectRequest& Request)
{
    const TArray<TSharedPtr<FJsonValue>>* Ops = nullptr;
    if (!Request.Object.IsValid() || !Request.Object->TryGetArrayField(TEXT("ops"), Ops) || Ops == nullptr)
    {
        return {};
    }
    return *Ops;
}

FString ReadGraphPatchOpKind(const TSharedPtr<FJsonValue>& OpValue)
{
    const TSharedPtr<FJsonObject>* OpObject = nullptr;
    if (!OpValue.IsValid() || !OpValue->TryGetObject(OpObject) || OpObject == nullptr || !(*OpObject).IsValid())
    {
        return FString();
    }

    FString Kind;
    (*OpObject)->TryGetStringField(TEXT("kind"), Kind);
    return Kind;
}

TSharedPtr<FJsonValue> MakeGraphPatchDiagnostic(
    const FString& Severity,
    const FString& Code,
    const FString& Message,
    int32 Index,
    const FString& Operation)
{
    TSharedPtr<FJsonObject> Diagnostic = FLglDiagnostics::Make(Severity, Code, Message);
    Diagnostic->SetStringField(TEXT("domain"), TEXT("graph"));
    Diagnostic->SetStringField(TEXT("operation"), Operation);
    TArray<TSharedPtr<FJsonValue>> Path;
    Path.Add(MakeShared<FJsonValueString>(TEXT("ops")));
    Path.Add(MakeShared<FJsonValueNumber>(Index));
    Diagnostic->SetArrayField(TEXT("path"), Path);
    return MakeShared<FJsonValueObject>(Diagnostic);
}

bool ReadGraphPinRef(
    const TSharedPtr<FJsonObject>& Object,
    FLglGraphPinRef& OutPin,
    FString& OutErrorCode,
    FString& OutErrorMessage)
{
    OutPin = {};
    if (!Object.IsValid())
    {
        OutErrorCode = TEXT("language.invalid_object_shape");
        OutErrorMessage = TEXT("Pin reference must be an object.");
        return false;
    }

    if (!Object->TryGetStringField(TEXT("node"), OutPin.Node) || OutPin.Node.IsEmpty())
    {
        OutErrorCode = TEXT("language.invalid_object_shape");
        OutErrorMessage = TEXT("Pin reference is missing required string field node.");
        return false;
    }

    if (!Object->TryGetStringField(TEXT("pin"), OutPin.Pin) || OutPin.Pin.IsEmpty())
    {
        OutErrorCode = TEXT("language.invalid_object_shape");
        OutErrorMessage = TEXT("Pin reference is missing required string field pin.");
        return false;
    }

    return true;
}

bool ReadGraphEdge(
    const TSharedPtr<FJsonObject>& EdgeObject,
    FLglGraphEdge& OutEdge,
    FString& OutErrorCode,
    FString& OutErrorMessage)
{
    OutEdge = {};
    if (!EdgeObject.IsValid())
    {
        OutErrorCode = TEXT("language.invalid_object_shape");
        OutErrorMessage = TEXT("Graph edge must be an object.");
        return false;
    }

    const TSharedPtr<FJsonObject>* From = nullptr;
    if (!EdgeObject->TryGetObjectField(TEXT("from"), From)
        || From == nullptr
        || !(*From).IsValid()
        || !ReadGraphPinRef(*From, OutEdge.From, OutErrorCode, OutErrorMessage))
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* To = nullptr;
    if (!EdgeObject->TryGetObjectField(TEXT("to"), To)
        || To == nullptr
        || !(*To).IsValid()
        || !ReadGraphPinRef(*To, OutEdge.To, OutErrorCode, OutErrorMessage))
    {
        return false;
    }

    return true;
}

TSharedPtr<FJsonObject> MakeGraphEdgeObject(const FLglGraphEdge& Edge)
{
    TSharedPtr<FJsonObject> From = MakeShared<FJsonObject>();
    From->SetStringField(TEXT("node"), Edge.From.Node);
    From->SetStringField(TEXT("pin"), Edge.From.Pin);

    TSharedPtr<FJsonObject> To = MakeShared<FJsonObject>();
    To->SetStringField(TEXT("node"), Edge.To.Node);
    To->SetStringField(TEXT("pin"), Edge.To.Pin);

    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetObjectField(TEXT("from"), From);
    Object->SetObjectField(TEXT("to"), To);
    return Object;
}

TSharedPtr<FJsonObject> BuildGraphResolvedRefs(
    const FString& AssetPath,
    const FString& GraphName,
    const FString& GraphId)
{
    TSharedPtr<FJsonObject> ResolvedRefs = MakeShared<FJsonObject>();

    TSharedPtr<FJsonObject> Asset = MakeShared<FJsonObject>();
    Asset->SetStringField(TEXT("path"), AssetPath);
    ResolvedRefs->SetObjectField(TEXT("asset"), Asset);

    TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();
    Graph->SetStringField(TEXT("name"), GraphName);
    if (!GraphId.IsEmpty())
    {
        Graph->SetStringField(TEXT("id"), GraphId);
    }
    ResolvedRefs->SetObjectField(TEXT("graph"), Graph);

    return ResolvedRefs;
}

void SetGraphResolvedNode(
    const TSharedPtr<FJsonObject>& ResolvedRefs,
    const FString& BindingName,
    const FString& NodeIdValue)
{
    if (!ResolvedRefs.IsValid() || BindingName.IsEmpty() || NodeIdValue.IsEmpty())
    {
        return;
    }

    const TSharedPtr<FJsonObject>* Nodes = nullptr;
    TSharedPtr<FJsonObject> NodesObject;
    if (ResolvedRefs->TryGetObjectField(TEXT("nodes"), Nodes) && Nodes != nullptr && (*Nodes).IsValid())
    {
        NodesObject = *Nodes;
    }
    else
    {
        NodesObject = MakeShared<FJsonObject>();
        ResolvedRefs->SetObjectField(TEXT("nodes"), NodesObject);
    }

    TSharedPtr<FJsonObject> NodeRef = MakeShared<FJsonObject>();
    NodeRef->SetStringField(TEXT("id"), NodeIdValue);
    NodesObject->SetObjectField(BindingName, NodeRef);
}
}
