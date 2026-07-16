// Copyright 2026 Loomle contributors.

#include "SalWidgetInterface.h"

#include "../SalDiagnostics.h"
#include "../SalObjectBuilder.h"
#include "../SalRuntime.h"
#include "Algo/Reverse.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetRegistryHelpers.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/BlueprintExtension.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/UserWidgetBlueprint.h"
#include "Blueprint/WidgetNavigation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/NamedSlotInterface.h"
#include "Components/Image.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/Texture.h"
#include "EditorClassUtils.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Interfaces/IPluginManager.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Crc.h"
#include "Misc/PackageName.h"
#include "Misc/PathViews.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "ObjectEditorUtils.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Templates/WidgetTemplateBlueprintClass.h"
#include "Templates/WidgetTemplateClass.h"
#include "Templates/WidgetTemplateImageClass.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UIComponentWidgetBlueprintExtension.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"
#include "WidgetBlueprintExtension.h"
#include "WidgetBlueprintEditorUtils.h"
#include "Extensions/UIComponentUserWidgetExtension.h"
#include "WidgetEditingProjectSettings.h"
#include "WidgetTemplate.h"

namespace Loomle::Sal
{
namespace
{
constexpr const TCHAR* InterfaceName = TEXT("widget");
constexpr const TCHAR* ClassPalettePrefix = TEXT("widget.class:");
constexpr const TCHAR* BlueprintPalettePrefix = TEXT("widget.blueprint:");
constexpr const TCHAR* ImagePalettePrefix = TEXT("widget.image:");

FWidgetBlueprintEditor* FindOpenWidgetBlueprintEditor(UWidgetBlueprint* Blueprint);

FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

UWidgetBlueprint* WidgetBlueprint(const FSalResolvedTarget& Target)
{
    return Target.Kind == ESalTargetKind::Blueprint ? Cast<UWidgetBlueprint>(Target.Blueprint) : nullptr;
}

TSharedPtr<FJsonObject> QueryError(
    const FString& Code,
    const FString& Message,
    const FString& Operation = FString(),
    const FString& Ref = FString(),
    const TArray<FString>& Supported = {})
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(Code, Message).Interface(InterfaceName);
    if (!Operation.IsEmpty())
    {
        Diagnostic.Operation(Operation);
    }
    if (!Ref.IsEmpty())
    {
        Diagnostic.Ref(Ref);
    }
    if (!Supported.IsEmpty())
    {
        Diagnostic.Supported(Supported);
    }
    return FSalDiagnostics::Result(Diagnostic.Build());
}

TSharedPtr<FJsonObject> MutationError(
    const FSalPatch& Patch,
    const FSalResolvedTarget& Target,
    const FString& Code,
    const FString& Message,
    const FString& Operation,
    const FString& Ref = FString())
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(Code, Message)
        .Interface(InterfaceName)
        .Operation(Operation);
    if (!Ref.IsEmpty())
    {
        Diagnostic.Ref(Ref);
    }
    return MakeMutationResult(
        nullptr,
        {Diagnostic.Build()},
        Patch.bDryRun,
        false,
        false,
        Target.AssetPath,
        TEXT("patch"));
}

FString WidgetQuerySignature(const FSalQuery& Query, UWidgetBlueprint* Blueprint)
{
    FString Signature = Blueprint != nullptr
        ? Blueprint->GetPathName() + TEXT("|") + GuidText(Blueprint->GetBlueprintGuid())
        : FString();
    FString Operation;
    const TSharedRef<TJsonWriter<>> OperationWriter = TJsonWriterFactory<>::Create(&Operation);
    FJsonSerializer::Serialize(Query.Operation.ToSharedRef(), OperationWriter);
    Signature += TEXT("|") + Operation;
    if (Query.Where.IsValid())
    {
        FString Where;
        const TSharedRef<TJsonWriter<>> WhereWriter = TJsonWriterFactory<>::Create(&Where);
        FJsonSerializer::Serialize(Query.Where.ToSharedRef(), WhereWriter);
        Signature += TEXT("|") + Where;
    }
    Signature += TEXT("|") + FString::Join(Query.With, TEXT(","));
    for (const TSharedPtr<FJsonObject>& Entry : Query.OrderBy)
    {
        FString Key;
        FString Direction;
        Entry->TryGetStringField(TEXT("key"), Key);
        Entry->TryGetStringField(TEXT("direction"), Direction);
        Signature += FString::Printf(TEXT("|%s:%s"), *Key, *Direction);
    }
    return FString::Printf(TEXT("%08x"), FCrc::StrCrc32(*Signature));
}

bool DecodeWidgetPage(
    const FSalQuery& Query,
    UWidgetBlueprint* Blueprint,
    const int32 DefaultLimit,
    const int32 MaxLimit,
    FSalPage& OutPage)
{
    OutPage.Limit = FMath::Clamp(Query.PageLimit > 0 ? Query.PageLimit : DefaultLimit, 1, MaxLimit);
    OutPage.Offset = 0;
    if (Query.PageAfter.IsEmpty())
    {
        return true;
    }
    TArray<FString> Parts;
    Query.PageAfter.ParseIntoArray(Parts, TEXT(":"), false);
    if (Parts.Num() != 3
        || Parts[0] != TEXT("widget")
        || Parts[1] != WidgetQuerySignature(Query, Blueprint)
        || !ParseNonNegativeInt32(Parts[2], OutPage.Offset))
    {
        return false;
    }
    return true;
}

TSharedPtr<FJsonObject> MakeWidgetPageResult(
    const TSharedPtr<FJsonObject>& Result,
    const FSalQuery& Query,
    UWidgetBlueprint* Blueprint,
    const int32 NextOffset,
    const bool bHasNext)
{
    if (Result.IsValid() && bHasNext)
    {
        TSharedPtr<FJsonObject> Page = MakeShared<FJsonObject>();
        Page->SetStringField(
            TEXT("next"),
            FString::Printf(TEXT("widget:%s:%d"), *WidgetQuerySignature(Query, Blueprint), NextOffset));
        Result->SetObjectField(TEXT("page"), Page);
    }
    return Result;
}

bool ReadKind(const TSharedPtr<FJsonObject>& Object, FString& OutKind)
{
    OutKind.Reset();
    return Object.IsValid() && Object->TryGetStringField(TEXT("kind"), OutKind) && !OutKind.IsEmpty();
}

bool ReadRef(const TSharedPtr<FJsonObject>& Ref, FString& OutKind, FString& OutIdentity)
{
    if (!ReadKind(Ref, OutKind))
    {
        return false;
    }
    if (OutKind == TEXT("local"))
    {
        return Ref->TryGetStringField(TEXT("name"), OutIdentity) && !OutIdentity.IsEmpty();
    }
    return Ref->TryGetStringField(TEXT("id"), OutIdentity) && !OutIdentity.IsEmpty();
}

bool ReadMember(
    const TSharedPtr<FJsonObject>& Member,
    TSharedPtr<FJsonObject>& OutOwner,
    TArray<FString>& OutPath)
{
    FString Kind;
    const TSharedPtr<FJsonObject>* Owner = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
    if (!ReadKind(Member, Kind)
        || Kind != TEXT("member")
        || !Member->TryGetObjectField(TEXT("object"), Owner)
        || Owner == nullptr
        || !(*Owner).IsValid()
        || !Member->TryGetArrayField(TEXT("path"), Path)
        || Path == nullptr
        || Path->IsEmpty())
    {
        return false;
    }
    OutOwner = *Owner;
    OutPath.Reset();
    for (const TSharedPtr<FJsonValue>& SegmentValue : *Path)
    {
        FString Segment;
        if (!SegmentValue.IsValid() || !SegmentValue->TryGetString(Segment) || Segment.IsEmpty())
        {
            return false;
        }
        OutPath.Add(Segment);
    }
    return true;
}

bool HasExactPatchTargetId(const FSalResolvedTarget& Target)
{
    return Target.Kind == ESalTargetKind::Blueprint
        && Target.Blueprint != nullptr
        && !Target.Id.IsEmpty()
        && Target.Id.Equals(GuidText(Target.Blueprint->GetBlueprintGuid()), ESearchCase::IgnoreCase);
}

TArray<UWidget*> SourceWidgets(UWidgetBlueprint* Blueprint)
{
    return Blueprint != nullptr ? Blueprint->GetAllSourceWidgets() : TArray<UWidget*>();
}

bool ValidateWidgetIds(UWidgetBlueprint* Blueprint, FString& OutError)
{
    TSet<FGuid> Guids;
    for (UWidget* Widget : SourceWidgets(Blueprint))
    {
        if (Widget == nullptr)
        {
            continue;
        }
        const FGuid* Guid = Blueprint->WidgetVariableNameToGuidMap.Find(Widget->GetFName());
        if (Guid == nullptr || !Guid->IsValid())
        {
            OutError = FString::Printf(TEXT("Source Widget %s has no persistent Widget GUID."), *Widget->GetName());
            return false;
        }
        if (Guids.Contains(*Guid))
        {
            OutError = FString::Printf(TEXT("Source Widget GUID is duplicated: %s."), *GuidText(*Guid));
            return false;
        }
        Guids.Add(*Guid);
    }
    return true;
}

FString WidgetId(UWidgetBlueprint* Blueprint, const UWidget* Widget)
{
    if (Blueprint == nullptr || Widget == nullptr)
    {
        return FString();
    }
    if (const FGuid* Guid = Blueprint->WidgetVariableNameToGuidMap.Find(Widget->GetFName()))
    {
        return GuidText(*Guid);
    }
    return FString();
}

UWidget* FindWidgetById(UWidgetBlueprint* Blueprint, const FString& Id)
{
    UWidget* Match = nullptr;
    for (UWidget* Widget : SourceWidgets(Blueprint))
    {
        if (Widget != nullptr && WidgetId(Blueprint, Widget).Equals(Id, ESearchCase::IgnoreCase))
        {
            if (Match != nullptr)
            {
                return nullptr;
            }
            Match = Widget;
        }
    }
    return Match;
}

UWidget* FindWidgetByName(UWidgetBlueprint* Blueprint, const FString& Name)
{
    UWidget* Match = nullptr;
    for (UWidget* Widget : SourceWidgets(Blueprint))
    {
        if (Widget != nullptr && Widget->GetName() == Name)
        {
            if (Match != nullptr)
            {
                return nullptr;
            }
            Match = Widget;
        }
    }
    return Match;
}

TSharedPtr<FJsonValue> PropertyValue(const FProperty* Property, const void* Container)
{
    if (Property == nullptr || Container == nullptr)
    {
        return Value::Null();
    }
    if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
    {
        return Value::Bool(Bool->GetPropertyValue_InContainer(Container));
    }
    if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
    {
        const void* Address = Numeric->ContainerPtrToValuePtr<void>(Container);
        if (Numeric->IsFloatingPoint())
        {
            return Value::Number(Numeric->GetFloatingPointPropertyValue(Address));
        }
        if (Numeric->IsInteger() && Numeric->GetSize() < 8)
        {
            return Value::Number(static_cast<double>(Numeric->GetSignedIntPropertyValue(Address)));
        }
    }
    if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
    {
        const void* Address = Enum->ContainerPtrToValuePtr<void>(Container);
        const int64 Raw = Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Address);
        return Value::Name(Enum->GetEnum()->GetNameStringByValue(Raw));
    }
    if (const FByteProperty* Byte = CastField<FByteProperty>(Property); Byte != nullptr && Byte->Enum != nullptr)
    {
        return Value::Name(Byte->Enum->GetNameStringByValue(Byte->GetPropertyValue_InContainer(Container)));
    }
    if (const FNameProperty* Name = CastField<FNameProperty>(Property))
    {
        return Value::String(Name->GetPropertyValue_InContainer(Container).ToString());
    }
    if (const FStrProperty* String = CastField<FStrProperty>(Property))
    {
        return Value::String(String->GetPropertyValue_InContainer(Container));
    }
    if (const FTextProperty* Text = CastField<FTextProperty>(Property))
    {
        return Value::String(Text->GetPropertyValue_InContainer(Container).ToString());
    }
    return NativeValue(ExportPropertyValue(Property, Container));
}

FString InlineNativePropertyValue(const FProperty* Property, const void* Container)
{
    FString Text = ExportPropertyValue(Property, Container);
    Text.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    Text.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    return Text;
}

bool IsNativeWidgetField(const FProperty* Property)
{
    if (Property == nullptr
        || !Property->HasAnyPropertyFlags(CPF_Edit)
        || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)
        || Property->GetName() == TEXT("Slot")
        || Property->GetName() == TEXT("Navigation")
        || Property->IsA<FDelegateProperty>()
        || Property->IsA<FMulticastDelegateProperty>())
    {
        return false;
    }
    return true;
}

bool CanWriteNativeWidgetField(const FProperty* Property, const UObject* Container)
{
    return IsNativeWidgetField(Property)
        && Container != nullptr
        && !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_BlueprintReadOnly | CPF_DisableEditOnTemplate)
        && Container->CanEditChange(Property);
}

bool CanResetNativeWidgetField(const FProperty* Property, const UObject* Container)
{
    return CanWriteNativeWidgetField(Property, Container)
        && !Property->HasMetaData(TEXT("NoResetToDefault"))
        && Container->GetClass()->GetDefaultObject() != nullptr;
}

void AddNonDefaultFields(const TSharedPtr<FJsonObject>& Args, UObject* Object)
{
    if (!Args.IsValid() || Object == nullptr)
    {
        return;
    }
    UObject* Defaults = Object->GetClass()->GetDefaultObject();
    for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
    {
        FProperty* Property = *It;
        if (!IsNativeWidgetField(Property))
        {
            continue;
        }
        const void* Current = Property->ContainerPtrToValuePtr<void>(Object);
        const void* Default = Defaults != nullptr ? Property->ContainerPtrToValuePtr<void>(Defaults) : nullptr;
        if (Default != nullptr && Property->Identical(Current, Default, PPF_None))
        {
            continue;
        }
        Args->SetField(Property->GetName(), PropertyValue(Property, Object));
    }
}

TSharedPtr<FJsonObject> SlotValue(UPanelSlot* Slot, const bool bComplete)
{
    if (Slot == nullptr)
    {
        return nullptr;
    }
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("type"), Slot->GetClass()->GetPathName());
    if (bComplete)
    {
        AddNonDefaultFields(Object, Slot);
    }
    else
    {
        AddNonDefaultFields(Object, Slot);
    }
    Object->RemoveField(TEXT("Parent"));
    Object->RemoveField(TEXT("Content"));
    return Object;
}

TSharedPtr<FJsonObject> NamedSlotsValue(UWidgetBlueprint* Blueprint, UObject* Host)
{
    INamedSlotInterface* Named = Cast<INamedSlotInterface>(Host);
    if (Named == nullptr)
    {
        return nullptr;
    }
    TArray<FName> SlotNames;
    Named->GetSlotNames(SlotNames);
    if (SlotNames.IsEmpty())
    {
        return nullptr;
    }
    TSharedPtr<FJsonObject> Slots = MakeShared<FJsonObject>();
    for (const FName SlotName : SlotNames)
    {
        UWidget* Content = Named->GetContentForSlot(SlotName);
        const FString Id = WidgetId(Blueprint, Content);
        Slots->SetField(
            SlotName.ToString(),
            Content != nullptr && !Id.IsEmpty() ? Value::Stable(TEXT("widget"), Id) : Value::Null());
    }
    return Slots;
}

enum class EWidgetDetail : uint8
{
    Compact,
    Skeleton,
    Complete
};

TSharedPtr<FJsonValue> WidgetValue(UWidgetBlueprint* Blueprint, UWidget* Widget, const EWidgetDetail Detail)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("id"), WidgetId(Blueprint, Widget));
    Args->SetStringField(TEXT("type"), Widget->GetClass()->GetPathName());
    const FString Label = Widget->GetDisplayLabel();
    if (!Label.IsEmpty() && Label != Widget->GetName())
    {
        Args->SetStringField(TEXT("DisplayLabel"), Label);
    }
    if (Widget->bIsVariable)
    {
        Args->SetBoolField(TEXT("bIsVariable"), true);
    }
    if (Detail == EWidgetDetail::Complete)
    {
        AddNonDefaultFields(Args, Widget);
    }
    if (Detail != EWidgetDetail::Compact)
    {
        if (TSharedPtr<FJsonObject> Slot = SlotValue(Widget->Slot, Detail == EWidgetDetail::Complete))
        {
            Args->SetObjectField(TEXT("Slot"), Slot);
        }
        if (TSharedPtr<FJsonObject> NamedSlots = NamedSlotsValue(Blueprint, Widget))
        {
            Args->SetObjectField(TEXT("NamedSlots"), NamedSlots);
        }
    }
    return Value::Call(TEXT("widget"), Args);
}

TSharedPtr<FJsonValue> BlueprintValue(UWidgetBlueprint* Blueprint, const bool bNamedSlots)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("id"), GuidText(Blueprint->GetBlueprintGuid()));
    if (bNamedSlots)
    {
        if (TSharedPtr<FJsonObject> NamedSlots = NamedSlotsValue(Blueprint, Blueprint->WidgetTree))
        {
            Args->SetObjectField(TEXT("NamedSlots"), NamedSlots);
        }
    }
    return Value::Call(TEXT("blueprint"), Args);
}

TArray<UWidget*> DirectChildren(UWidget* Widget)
{
    TArray<UWidget*> Children;
    if (INamedSlotInterface* Named = Cast<INamedSlotInterface>(Widget))
    {
        TArray<FName> Names;
        Named->GetSlotNames(Names);
        for (const FName Name : Names)
        {
            if (UWidget* Child = Named->GetContentForSlot(Name))
            {
                Children.AddUnique(Child);
            }
        }
    }
    if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
    {
        for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
        {
            if (UWidget* Child = Panel->GetChildAt(Index))
            {
                Children.AddUnique(Child);
            }
        }
    }
    return Children;
}

void VisitReachable(UWidget* Widget, TArray<UWidget*>& Order, TSet<UWidget*>& Seen)
{
    if (Widget == nullptr || Seen.Contains(Widget))
    {
        return;
    }
    Seen.Add(Widget);
    Order.Add(Widget);
    for (UWidget* Child : DirectChildren(Widget))
    {
        VisitReachable(Child, Order, Seen);
    }
}

TArray<UWidget*> ReachableWidgets(UWidgetBlueprint* Blueprint)
{
    TArray<UWidget*> Order;
    TSet<UWidget*> Seen;
    if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr)
    {
        return Order;
    }
    VisitReachable(Blueprint->WidgetTree->RootWidget, Order, Seen);
    TArray<FName> Names;
    Blueprint->WidgetTree->GetSlotNames(Names);
    for (const FName Name : Names)
    {
        VisitReachable(Blueprint->WidgetTree->GetContentForSlot(Name), Order, Seen);
    }
    return Order;
}

void EmitSubtree(
    FSalObjectBuilder& Builder,
    UWidgetBlueprint* Blueprint,
    UWidget* Widget,
    const FString& OwnerAlias,
    const TArray<FString>& Path,
    const int32 Depth,
    const int32 MaxDepth,
    TSet<UWidget*>& Seen)
{
    if (Widget == nullptr || Seen.Contains(Widget))
    {
        return;
    }
    Seen.Add(Widget);
    if (Path.IsEmpty())
    {
        Builder.AddLocalBinding(OwnerAlias, WidgetValue(Blueprint, Widget, EWidgetDetail::Skeleton));
    }
    else
    {
        Builder.AddMemberBinding(OwnerAlias, Path, WidgetValue(Blueprint, Widget, EWidgetDetail::Skeleton));
    }
    const TArray<UWidget*> Children = DirectChildren(Widget);
    if (Depth >= MaxDepth)
    {
        if (!Children.IsEmpty())
        {
            Builder.AddComment(FString::Printf(TEXT("truncated: widget@%s has %d child widget(s)"), *WidgetId(Blueprint, Widget), Children.Num()));
        }
        return;
    }
    for (UWidget* Child : Children)
    {
        TArray<FString> ChildPath = Path;
        ChildPath.Add(Child->GetName());
        EmitSubtree(Builder, Blueprint, Child, OwnerAlias, ChildPath, Depth + 1, MaxDepth, Seen);
    }
}

TSharedPtr<FJsonObject> TreeResult(UWidgetBlueprint* Blueprint, UWidget* Start, const int32 Depth)
{
    FSalObjectBuilder Builder;
    if (Start == nullptr)
    {
        Builder.AddComment(TEXT("empty WidgetTree"));
        return Builder.BuildResult();
    }
    TSet<UWidget*> Seen;
    const FString Alias = Builder.UniqueAlias(Start->GetName());
    EmitSubtree(Builder, Blueprint, Start, Alias, {}, 0, Depth, Seen);
    return Builder.BuildResult();
}

TSharedPtr<FJsonObject> FullTreeResult(UWidgetBlueprint* Blueprint, const int32 Depth)
{
    FSalObjectBuilder Builder;
    TSet<UWidget*> Seen;
    TArray<FName> NamedSlotNames;
    Blueprint->WidgetTree->GetSlotNames(NamedSlotNames);
    FString BlueprintAlias;
    if (!NamedSlotNames.IsEmpty())
    {
        BlueprintAlias = Builder.UniqueAlias(Blueprint->GetName());
        Builder.AddLocalBinding(BlueprintAlias, BlueprintValue(Blueprint, true));
    }
    if (UWidget* Root = Blueprint->WidgetTree->RootWidget)
    {
        const FString RootAlias = Builder.UniqueAlias(Root->GetName());
        EmitSubtree(Builder, Blueprint, Root, RootAlias, {}, 0, Depth, Seen);
    }
    for (const FName SlotName : NamedSlotNames)
    {
        UWidget* Content = Blueprint->WidgetTree->GetContentForSlot(SlotName);
        if (Content == nullptr || Seen.Contains(Content))
        {
            continue;
        }
        EmitSubtree(Builder, Blueprint, Content, BlueprintAlias, {Content->GetName()}, 0, Depth, Seen);
    }
    if (Blueprint->WidgetTree->RootWidget == nullptr && NamedSlotNames.IsEmpty())
    {
        Builder.AddComment(TEXT("empty WidgetTree"));
    }
    return Builder.BuildResult();
}

void AddEventOwnerLocator(
    TArray<FString>& Lines,
    UWidgetBlueprint* Blueprint,
    UEdGraph* Graph,
    const UEdGraphNode* Node)
{
    Lines.Add(TEXT("      eventOwner = blueprint("));
    Lines.Add(FString::Printf(TEXT("        asset: \"%s\","), *Blueprint->GetPathName()));
    Lines.Add(FString::Printf(TEXT("        id: \"%s\""), *GuidText(Blueprint->GetBlueprintGuid())));
    Lines.Add(TEXT("      )"));
    Lines.Add(TEXT("      eventGraph = graph("));
    Lines.Add(TEXT("        asset: eventOwner,"));
    Lines.Add(FString::Printf(TEXT("        id: \"%s\""), *GuidText(Graph->GraphGuid)));
    Lines.Add(TEXT("      )"));
    Lines.Add(TEXT("      query eventGraph"));
    if (Node != nullptr)
    {
        Lines.Add(FString::Printf(TEXT("      node@%s"), *GuidText(Node->NodeGuid)));
    }
}

void AddGraphEventSchema(
    TArray<FString>& Lines,
    UWidgetBlueprint* Blueprint,
    UWidget* Widget,
    FMulticastDelegateProperty* Delegate)
{
    if (Delegate == nullptr
        || Delegate->HasAnyPropertyFlags(CPF_Deprecated)
        || !UEdGraphSchema_K2::CanUserKismetAccessVariable(
            Delegate,
            Widget->GetClass(),
            UEdGraphSchema_K2::MustBeDelegate))
    {
        return;
    }

    Lines.Add(FString::Printf(
        TEXT("  %s: %s; graph event"),
        *Delegate->GetName(),
        *Delegate->GetCPPType(nullptr, 0)));

    FObjectProperty* GeneratedProperty = Blueprint->SkeletonGeneratedClass != nullptr
        ? FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass, Widget->GetFName())
        : nullptr;
    if (GeneratedProperty == nullptr || !Widget->IsA(GeneratedProperty->PropertyClass))
    {
        Lines.Add(TEXT("    availability: unavailable"));
        Lines.Add(TEXT("    reason: Widget has no generated FObjectProperty"));
        Lines.Add(TEXT("    requires: bIsVariable = true"));
        return;
    }

    const UK2Node_ComponentBoundEvent* Existing = FKismetEditorUtilities::FindBoundEventForComponent(
        Blueprint,
        Delegate->GetFName(),
        Widget->GetFName());
    if (Existing != nullptr && Existing->GetGraph() != nullptr && Existing->NodeGuid.IsValid())
    {
        Lines.Add(TEXT("    availability: existing"));
        Lines.Add(TEXT("    inspect with:"));
        AddEventOwnerLocator(Lines, Blueprint, Existing->GetGraph(), Existing);
        return;
    }

    TArray<UEdGraph*> EventGraphs;
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (Graph != nullptr && Graph->GraphGuid.IsValid())
        {
            EventGraphs.Add(Graph);
        }
    }
    EventGraphs.Sort([](const UEdGraph& Left, const UEdGraph& Right)
    {
        return Left.GetPathName() < Right.GetPathName();
    });
    if (EventGraphs.IsEmpty())
    {
        Lines.Add(TEXT("    availability: unavailable"));
        Lines.Add(TEXT("    reason: WidgetBlueprint has no compatible Ubergraph"));
        return;
    }

    Lines.Add(TEXT("    availability: available"));
    for (UEdGraph* Graph : EventGraphs)
    {
        Lines.Add(TEXT("    discover with:"));
        AddEventOwnerLocator(Lines, Blueprint, Graph, nullptr);
        Lines.Add(FString::Printf(TEXT("      palette entries \"%s\""), *Delegate->GetName()));
        Lines.Add(FString::Printf(TEXT("      where widget = widget@%s"), *WidgetId(Blueprint, Widget)));
    }
}

FString WidgetSchema(UWidgetBlueprint* Blueprint, UWidget* Widget)
{
    TArray<FString> Lines = {TEXT("schema"), TEXT(""), TEXT("fields:")};
    for (TFieldIterator<FProperty> It(Widget->GetClass()); It; ++It)
    {
        FProperty* Property = *It;
        if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)
            || Property->GetName() == TEXT("Slot")
            || Property->GetName() == TEXT("Navigation"))
        {
            continue;
        }
        if (FMulticastDelegateProperty* Delegate = CastField<FMulticastDelegateProperty>(Property))
        {
            AddGraphEventSchema(Lines, Blueprint, Widget, Delegate);
            continue;
        }
        if (!IsNativeWidgetField(Property))
        {
            continue;
        }
        const bool bWritable = CanWriteNativeWidgetField(Property, Widget);
        const bool bResettable = CanResetNativeWidgetField(Property, Widget);
        const UObject* Defaults = Widget->GetClass()->GetDefaultObject();
        Lines.Add(bResettable && Defaults != nullptr
            ? FString::Printf(
                TEXT("  %s: %s; readable, writable, resettable; reset: %s"),
                *Property->GetName(),
                *Property->GetCPPType(),
                *InlineNativePropertyValue(Property, Defaults))
            : bWritable
            ? FString::Printf(
                TEXT("  %s: %s; readable, writable"),
                *Property->GetName(),
                *Property->GetCPPType())
            : FString::Printf(
                TEXT("  %s: %s; readable"),
                *Property->GetName(),
                *Property->GetCPPType()));
    }
    Lines.Add(TEXT("  bIsVariable: bool; readable, writable, resettable; reset: false; structural"));
    if (Widget->Slot != nullptr)
    {
        Lines.Add(TEXT("Slot fields:"));
        for (TFieldIterator<FProperty> It(Widget->Slot->GetClass()); It; ++It)
        {
            FProperty* Property = *It;
            if (IsNativeWidgetField(Property)
                && Property->GetName() != TEXT("Parent")
                && Property->GetName() != TEXT("Content"))
            {
                const bool bWritable = CanWriteNativeWidgetField(Property, Widget->Slot);
                const bool bResettable = CanResetNativeWidgetField(Property, Widget->Slot);
                const UObject* Defaults = Widget->Slot->GetClass()->GetDefaultObject();
                Lines.Add(bResettable && Defaults != nullptr
                    ? FString::Printf(
                        TEXT("  Slot.%s: %s; readable, writable, resettable; reset: %s"),
                        *Property->GetName(),
                        *Property->GetCPPType(),
                        *InlineNativePropertyValue(Property, Defaults))
                    : bWritable
                    ? FString::Printf(
                        TEXT("  Slot.%s: %s; readable, writable"),
                        *Property->GetName(),
                        *Property->GetCPPType())
                    : FString::Printf(
                        TEXT("  Slot.%s: %s; readable"),
                        *Property->GetName(),
                        *Property->GetCPPType()));
            }
        }
    }
    if (Cast<INamedSlotInterface>(Widget) != nullptr)
    {
        Lines.Add(TEXT("NamedSlots: readable relationship; edit through add, move, wrap, or replace"));
    }
    Lines.Add(TEXT("operations:"));
    Lines.Add(TEXT("  remove, Rename"));
    const bool bRoot = Widget == Blueprint->WidgetTree->RootWidget;
    const TScriptInterface<INamedSlotInterface> NamedHost =
        FWidgetBlueprintEditorUtils::FindNamedSlotHostForContent(Widget, Blueprint->WidgetTree);
    const bool bNamedContent = NamedHost.GetObject() != nullptr
        || Blueprint->WidgetTree->NamedSlotBindings.FindKey(Widget) != nullptr;
    const bool bStructurallyPlaced = bRoot || Widget->GetParent() != nullptr || bNamedContent;
    if (bStructurallyPlaced)
    {
        Lines.Add(TEXT("  wrap, replace"));
    }
    if (bStructurallyPlaced && !bRoot)
    {
        Lines.Add(TEXT("  move"));
    }
    if (Widget->GetParent() != nullptr
        && Widget->GetParent()->CanHaveMultipleChildren()
        && Widget->GetParent()->CanAddMoreChildren())
    {
        Lines.Add(TEXT("  Duplicate"));
    }
    return FString::Join(Lines, TEXT("\n"));
}

void EmitExactWidget(FSalObjectBuilder& Builder, UWidgetBlueprint* Blueprint, UWidget* Widget, const bool bSchema)
{
    TArray<UWidget*> TargetToRoot = {Widget};
    TSet<UWidget*> Seen;
    Seen.Add(Widget);
    UWidget* Current = Widget;
    while (Current != nullptr)
    {
        UWidget* Owner = Current->GetParent();
        if (Owner == nullptr)
        {
            Owner = FWidgetBlueprintEditorUtils::FindNamedSlotHostWidgetForContent(Current, Blueprint->WidgetTree);
        }
        if (Owner == nullptr || Seen.Contains(Owner))
        {
            break;
        }
        Seen.Add(Owner);
        TargetToRoot.Add(Owner);
        Current = Owner;
    }
    Algo::Reverse(TargetToRoot);

    UWidget* Top = TargetToRoot[0];
    const bool bTreeNamed = Top != Blueprint->WidgetTree->RootWidget
        && Blueprint->WidgetTree->NamedSlotBindings.FindKey(Top) != nullptr;
    FString OwnerAlias;
    TArray<FString> Path;
    int32 StartIndex = 0;
    if (bTreeNamed)
    {
        OwnerAlias = Builder.UniqueAlias(Blueprint->GetName());
        Builder.AddLocalBinding(OwnerAlias, BlueprintValue(Blueprint, true));
    }
    else
    {
        OwnerAlias = Builder.UniqueAlias(Top->GetName());
        Builder.AddLocalBinding(
            OwnerAlias,
            WidgetValue(Blueprint, Top, TargetToRoot.Num() == 1 ? EWidgetDetail::Complete : EWidgetDetail::Skeleton));
        StartIndex = 1;
    }
    for (int32 Index = StartIndex; Index < TargetToRoot.Num(); ++Index)
    {
        UWidget* ChainWidget = TargetToRoot[Index];
        Path.Add(ChainWidget->GetName());
        Builder.AddMemberBinding(
            OwnerAlias,
            Path,
            WidgetValue(
                Blueprint,
                ChainWidget,
                Index == TargetToRoot.Num() - 1 ? EWidgetDetail::Complete : EWidgetDetail::Skeleton));
    }
    if (Top != Blueprint->WidgetTree->RootWidget && !bTreeNamed)
    {
        Builder.AddComment(TEXT("detached source Widget subtree"));
    }
    if (bSchema)
    {
        Builder.AddComment(WidgetSchema(Blueprint, Widget));
    }
}

bool BoolExpr(const TSharedPtr<FJsonValue>& Value, bool& Out)
{
    if (Value.IsValid() && Value->TryGetBool(Out))
    {
        return true;
    }
    const FString Text = ExprString(Value);
    if (Text == TEXT("true") || Text == TEXT("false"))
    {
        Out = Text == TEXT("true");
        return true;
    }
    return false;
}

bool ValidateWidgetCondition(const TSharedPtr<FJsonObject>& Condition, FString& OutError)
{
    if (!Condition.IsValid())
    {
        return true;
    }
    FString Kind;
    Condition->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("not"))
    {
        const TSharedPtr<FJsonObject>* Inner = nullptr;
        return Condition->TryGetObjectField(TEXT("condition"), Inner)
            && Inner != nullptr
            && ValidateWidgetCondition(*Inner, OutError);
    }
    if (Kind == TEXT("and") || Kind == TEXT("or"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
        if (!Condition->TryGetArrayField(TEXT("conditions"), Items) || Items == nullptr)
        {
            OutError = TEXT("Widget condition has no nested conditions.");
            return false;
        }
        for (const TSharedPtr<FJsonValue>& Value : *Items)
        {
            const TSharedPtr<FJsonObject>* Inner = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Inner) || Inner == nullptr || !ValidateWidgetCondition(*Inner, OutError))
            {
                return false;
            }
        }
        return true;
    }
    if (!(Kind == TEXT("eq") || Kind == TEXT("ne")))
    {
        OutError = TEXT("Widget collection fields support only = and !=.");
        return false;
    }
    const FString Field = ConditionField(Condition);
    if (!(Field == TEXT("name") || Field == TEXT("id") || Field == TEXT("type") || Field == TEXT("DisplayLabel")
        || Field == TEXT("bIsVariable") || Field == TEXT("reachable")))
    {
        OutError = FString::Printf(TEXT("Widget collection field is unsupported: %s."), *Field);
        return false;
    }
    if (Field == TEXT("bIsVariable") || Field == TEXT("reachable"))
    {
        bool bValue = false;
        if (!BoolExpr(Condition->TryGetField(TEXT("value")), bValue))
        {
            OutError = FString::Printf(TEXT("Widget field %s requires a Boolean value."), *Field);
            return false;
        }
    }
    return true;
}

FString WidgetField(UWidgetBlueprint* Blueprint, UWidget* Widget, const FString& Field, const TSet<UWidget*>& Reachable)
{
    if (Field == TEXT("name")) return Widget->GetName();
    if (Field == TEXT("id")) return WidgetId(Blueprint, Widget);
    if (Field == TEXT("type")) return Widget->GetClass()->GetPathName();
    if (Field == TEXT("DisplayLabel")) return Widget->GetDisplayLabel();
    if (Field == TEXT("bIsVariable")) return Widget->bIsVariable ? TEXT("true") : TEXT("false");
    if (Field == TEXT("reachable")) return Reachable.Contains(Widget) ? TEXT("true") : TEXT("false");
    return FString();
}

bool MatchesWidgetCondition(
    UWidgetBlueprint* Blueprint,
    UWidget* Widget,
    const TSharedPtr<FJsonObject>& Condition,
    const TSet<UWidget*>& Reachable)
{
    if (!Condition.IsValid())
    {
        return true;
    }
    FString Kind;
    Condition->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("not"))
    {
        const TSharedPtr<FJsonObject>* Inner = nullptr;
        return Condition->TryGetObjectField(TEXT("condition"), Inner)
            && Inner != nullptr
            && !MatchesWidgetCondition(Blueprint, Widget, *Inner, Reachable);
    }
    if (Kind == TEXT("and") || Kind == TEXT("or"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
        Condition->TryGetArrayField(TEXT("conditions"), Items);
        const bool bAnd = Kind == TEXT("and");
        for (const TSharedPtr<FJsonValue>& Value : *Items)
        {
            const TSharedPtr<FJsonObject>* Inner = nullptr;
            const bool bMatch = Value->TryGetObject(Inner)
                && Inner != nullptr
                && MatchesWidgetCondition(Blueprint, Widget, *Inner, Reachable);
            if (bAnd && !bMatch) return false;
            if (!bAnd && bMatch) return true;
        }
        return bAnd;
    }
    const FString Field = ConditionField(Condition);
    const FString Right = ExprString(Condition->TryGetField(TEXT("value")));
    const bool bEqual = WidgetField(Blueprint, Widget, Field, Reachable).Equals(Right, ESearchCase::CaseSensitive);
    return Kind == TEXT("ne") ? !bEqual : bEqual;
}

double WidgetSearchScore(UWidget* Widget, const FString& Text)
{
    if (Text.IsEmpty()) return 0.0;
    const FString Name = Widget->GetName();
    const FString Label = Widget->GetDisplayLabel();
    const FString MetadataLabel = Widget->GetLabelTextWithMetadata().ToString();
    const FString Type = Widget->GetClass()->GetPathName();
    if (Name.Equals(Text, ESearchCase::IgnoreCase)
        || Label.Equals(Text, ESearchCase::IgnoreCase)
        || MetadataLabel.Equals(Text, ESearchCase::IgnoreCase)) return 100.0;
    if (Name.StartsWith(Text, ESearchCase::IgnoreCase)
        || Label.StartsWith(Text, ESearchCase::IgnoreCase)
        || MetadataLabel.StartsWith(Text, ESearchCase::IgnoreCase)) return 90.0;
    if (Name.Contains(Text, ESearchCase::IgnoreCase)
        || Label.Contains(Text, ESearchCase::IgnoreCase)
        || MetadataLabel.Contains(Text, ESearchCase::IgnoreCase)) return 80.0;
    if (Type.Contains(Text, ESearchCase::IgnoreCase)
        || Widget->GetClass()->GetName().Contains(Text, ESearchCase::IgnoreCase)
        || Widget->GetClass()->GetDisplayNameText().ToString().Contains(Text, ESearchCase::IgnoreCase)) return 60.0;
    return -1.0;
}

struct FWidgetItem
{
    UWidget* Widget = nullptr;
    double Score = 0.0;
    int32 TreeIndex = MAX_int32;
    bool bReachable = false;
};

struct FPaletteEntry
{
    enum class EKind : uint8
    {
        NativeClass,
        WidgetBlueprint,
        ImageAsset
    };

    FString Id;
    FString Label;
    FString Type;
    EKind Kind = EKind::NativeClass;
    FAssetData Asset;
    UClass* Class = nullptr;
};

bool PassesPaletteSettings(
    UWidgetBlueprint* Blueprint,
    const FString& Path,
    const FString& Category,
    UClass* Class)
{
    if (Blueprint == nullptr)
    {
        return false;
    }
    const UWidgetEditingProjectSettings* Settings = Blueprint->GetRelevantSettings();
    if (Settings == nullptr)
    {
        return true;
    }
    if ((!Settings->bShowWidgetsFromEngineContent && Path.StartsWith(TEXT("/Engine")))
        || (!Settings->bShowWidgetsFromDeveloperContent && Path.StartsWith(TEXT("/Game/Developers")))
        || Settings->CategoriesToHide.Contains(Category))
    {
        return false;
    }
    if (!Blueprint->AllowEditorWidget() && Class != nullptr && IsEditorOnlyObject(Class))
    {
        return false;
    }
    if (Settings->bUseEditorConfigPaletteFiltering)
    {
        // The editor's category permission is combined with its global Class Viewer
        // filter.  The native helper below owns that branch whenever the target is
        // open.  Headless enumeration follows the remaining path-permission branch.
        const bool bAllowedPath = Settings->GetAllowedPaletteWidgets().PassesFilter(Path);
        if (FPackageName::IsScriptPackage(Path))
        {
            return bAllowedPath;
        }
        const FStringView MountPoint = FPathViews::GetMountPointNameFromPath(Path);
        const TSet<FString>& BuiltInPlugins = IPluginManager::Get().GetBuiltInPluginNames();
        const bool bUnderKnownMount = MountPoint.Equals(TEXT("Engine"), ESearchCase::IgnoreCase)
            || MountPoint.Equals(TEXT("Game"), ESearchCase::IgnoreCase)
            || BuiltInPlugins.ContainsByHash(GetTypeHash(MountPoint), MountPoint);
        return !bUnderKnownMount || bAllowedPath;
    }
    for (const FSoftClassPath& Hidden : Settings->WidgetClassesToHide)
    {
        if (Path.StartsWith(Hidden.ToString()))
        {
            return false;
        }
    }
    return true;
}

bool UsableWidgetClass(UWidgetBlueprint* Blueprint, UClass* Class)
{
    if (Blueprint == nullptr
        || Class == nullptr
        || !Class->IsChildOf(UWidget::StaticClass())
        || Class == Blueprint->GeneratedClass
        || Class->HasAnyClassFlags(
            CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_HideDropDown | CLASS_Hidden)
        || (Class->HasAnyFlags(RF_Transient) && Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
        || Class->GetName().StartsWith(TEXT("SKEL_"))
        || Class->GetName().StartsWith(TEXT("REINST_")))
    {
        return false;
    }
    bool bExperimental = false;
    bool bEarlyAccess = false;
    FString DevelopmentClass;
    FObjectEditorUtils::GetClassDevelopmentStatus(Class, bExperimental, bEarlyAccess, DevelopmentClass);
    if (bExperimental || bEarlyAccess)
    {
        return false;
    }
    const FString Category = FWidgetBlueprintEditorUtils::GetPaletteCategory(Class).ToString();
    if (const UWidgetEditingProjectSettings* Settings = Blueprint->GetRelevantSettings();
        Settings != nullptr && Settings->CategoriesToHide.Contains(Category))
    {
        return false;
    }
    if (FWidgetBlueprintEditor* Editor = FindOpenWidgetBlueprintEditor(Blueprint))
    {
        return FWidgetBlueprintEditorUtils::IsUsableWidgetClass(
            Class,
            StaticCastSharedRef<FWidgetBlueprintEditor>(Editor->AsShared()));
    }
    return PassesPaletteSettings(Blueprint, Class->GetPathName(), Category, Class);
}

bool UsableWidgetAsset(
    UWidgetBlueprint* Blueprint,
    const FAssetData& Asset,
    UClass* NativeParent,
    const EClassFlags Flags,
    const FString& Category)
{
    if (Blueprint == nullptr
        || NativeParent == nullptr
        || !NativeParent->IsChildOf(UWidget::StaticClass())
        || (Flags & (CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_HideDropDown | CLASS_Hidden)) != 0)
    {
        return false;
    }
    if (const UWidgetEditingProjectSettings* Settings = Blueprint->GetRelevantSettings();
        Settings != nullptr && Settings->CategoriesToHide.Contains(Category))
    {
        return false;
    }
    if (FWidgetBlueprintEditor* Editor = FindOpenWidgetBlueprintEditor(Blueprint))
    {
        const TValueOrError<FWidgetBlueprintEditorUtils::FUsableWidgetClassResult, void> Usable =
            FWidgetBlueprintEditorUtils::IsUsableWidgetClass(
                Asset,
                StaticCastSharedRef<FWidgetBlueprintEditor>(Editor->AsShared()));
        return Usable.HasValue()
            && (Usable.GetValue().AssetClassFlags & (CLASS_Hidden | CLASS_HideDropDown)) == 0;
    }
    return PassesPaletteSettings(Blueprint, Asset.GetObjectPathString(), Category, NativeParent);
}

UClass* NativeParentForWidgetAsset(const FAssetData& Asset)
{
    FString NativeParentPath;
    if (!Asset.GetTagValue(FBlueprintTags::NativeParentClassPath, NativeParentPath))
    {
        return nullptr;
    }
    NativeParentPath = FPackageName::ExportTextPathToObjectPath(NativeParentPath);
    return UClass::TryFindTypeSlow<UClass>(NativeParentPath);
}

TArray<FPaletteEntry> WidgetPalette(UWidgetBlueprint* Blueprint)
{
    TArray<FPaletteEntry> Entries;
    TArray<UClass*> WidgetClasses;
    GetDerivedClasses(UWidget::StaticClass(), WidgetClasses, true);
    TSet<FName> ProcessedPackages;
    for (UClass* Class : WidgetClasses)
    {
        if (Class != nullptr)
        {
            ProcessedPackages.Add(Class->GetPackage()->GetFName());
        }
        if (!UsableWidgetClass(Blueprint, Class))
        {
            continue;
        }
        FPaletteEntry& Entry = Entries.AddDefaulted_GetRef();
        Entry.Id = ClassPalettePrefix + Class->GetPathName();
        Entry.Label = Class->GetDisplayNameText().ToString();
        Entry.Type = Class->GetPathName();
        Entry.Class = Class;
        Entry.Asset = FAssetData(Class);
    }
    FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    if (!Module.Get().IsSearchAllAssets())
    {
        Module.Get().SearchAllAssets(true);
    }
    Module.Get().WaitForCompletion();
    TArray<FAssetData> WidgetAssets;
    UAssetRegistryHelpers::GetDerivedClassAssetData(
        {UWidget::StaticClass()->GetClassPathName()},
        WidgetAssets);
    for (const FAssetData& Asset : WidgetAssets)
    {
        if (!Asset.IsValid()
            || ProcessedPackages.Contains(Asset.PackageName)
            || Asset.PackageName == Blueprint->GetOutermost()->GetFName())
        {
            continue;
        }
        UClass* NativeParent = NativeParentForWidgetAsset(Asset);
        const EClassFlags Flags = static_cast<EClassFlags>(Asset.GetTagValueRef<uint32>(FBlueprintTags::ClassFlags));
        const FString Category = FWidgetBlueprintEditorUtils::GetPaletteCategory(
            Asset,
            TSubclassOf<UWidget>(NativeParent)).ToString();
        if (!UsableWidgetAsset(Blueprint, Asset, NativeParent, Flags, Category))
        {
            continue;
        }
        FPaletteEntry& Entry = Entries.AddDefaulted_GetRef();
        Entry.Id = BlueprintPalettePrefix + Asset.GetObjectPathString();
        Entry.Label = Asset.AssetName.ToString();
        Entry.Type = Asset.GetObjectPathString() + TEXT("_C");
        Entry.Kind = FPaletteEntry::EKind::WidgetBlueprint;
        Entry.Asset = Asset;
        ProcessedPackages.Add(Asset.PackageName);
    }

    TArray<FAssetData> AllAssets;
    Module.Get().GetAllAssets(AllAssets, false);
    for (const FAssetData& Asset : AllAssets)
    {
        UClass* AssetClass = FindObject<UClass>(Asset.AssetClassPath);
        if (!Asset.IsValid()
            || AssetClass == nullptr
            || !FWidgetTemplateImageClass::Supports(AssetClass)
            || !PassesPaletteSettings(Blueprint, Asset.GetObjectPathString(), TEXT("Common"), UImage::StaticClass()))
        {
            continue;
        }
        FPaletteEntry& Entry = Entries.AddDefaulted_GetRef();
        Entry.Id = ImagePalettePrefix + Asset.GetObjectPathString();
        Entry.Label = Asset.AssetName.ToString();
        Entry.Type = UImage::StaticClass()->GetPathName();
        Entry.Kind = FPaletteEntry::EKind::ImageAsset;
        Entry.Asset = Asset;
    }
    Entries.Sort([](const FPaletteEntry& Left, const FPaletteEntry& Right)
    {
        const int32 Label = Left.Label.Compare(Right.Label, ESearchCase::IgnoreCase);
        return Label != 0 ? Label < 0 : Left.Id < Right.Id;
    });
    return Entries;
}

TSharedPtr<FWidgetTemplate> ResolveTemplate(UWidgetBlueprint* Blueprint, const FString& Id, FString& OutError);

FString PaletteSchema(UWidgetBlueprint* Blueprint, const FPaletteEntry& Entry)
{
    FString Error;
    TSharedPtr<FWidgetTemplate> Template = ResolveTemplate(Blueprint, Entry.Id, Error);
    UWidgetTree* ScratchTree = NewObject<UWidgetTree>(GetTransientPackage());
    UWidget* Preview = Template.IsValid() && ScratchTree != nullptr ? Template->Create(ScratchTree) : nullptr;
    if (Preview == nullptr)
    {
        return FString::Printf(TEXT("schema\n\nunavailable: %s"), Error.IsEmpty() ? TEXT("template could not create a preview Widget") : *Error);
    }
    const bool bCircular = Cast<UUserWidget>(Preview) != nullptr
        && !Blueprint->IsWidgetFreeFromCircularReferences(CastChecked<UUserWidget>(Preview));
    TArray<FString> Lines = {
        TEXT("schema"),
        TEXT(""),
        TEXT("constructor:"),
        TEXT("  widget(palette: string)"),
        TEXT(""),
        TEXT("result type:"),
        TEXT("  ") + Preview->GetClass()->GetPathName(),
        TEXT(""),
        TEXT("context:"),
        bCircular ? TEXT("  unavailable: circular WidgetBlueprint reference") : TEXT("  available in this WidgetBlueprint"),
        TEXT(""),
        TEXT("editable fields:")};
    for (TFieldIterator<FProperty> It(Preview->GetClass()); It; ++It)
    {
        FProperty* Property = *It;
        if (CanWriteNativeWidgetField(Property, Preview))
        {
            Lines.Add(CanResetNativeWidgetField(Property, Preview)
                ? FString::Printf(
                    TEXT("  %s: %s; initial: %s; writable, resettable"),
                    *Property->GetName(),
                    *Property->GetCPPType(),
                    *InlineNativePropertyValue(Property, Preview))
                : FString::Printf(
                    TEXT("  %s: %s; initial: %s; writable"),
                    *Property->GetName(),
                    *Property->GetCPPType(),
                    *InlineNativePropertyValue(Property, Preview)));
        }
    }
    Lines.Add(TEXT(""));
    Lines.Add(TEXT("materialization operations:"));
    Lines.Add(Cast<UPanelWidget>(Preview) != nullptr
        ? TEXT("  add, wrap, replace")
        : TEXT("  add, replace"));
    Lines.Add(TEXT("object operations after materialization:"));
    Lines.Add(TEXT("  set, reset, move, remove, wrap, replace, Rename"));
    Lines.Add(TEXT("  Duplicate when placed under a multi-child Panel"));
    return FString::Join(Lines, TEXT("\n"));
}

void EmitPalette(
    FSalObjectBuilder& Builder,
    UWidgetBlueprint* Blueprint,
    const FPaletteEntry& Entry,
    const bool bSchema)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("palette"), Entry.Id);
    Builder.AddLocalBinding(Builder.UniqueAlias(Entry.Label), Value::Call(TEXT("widget"), Args));
    if (bSchema)
    {
        Builder.AddComment(PaletteSchema(Blueprint, Entry));
    }
}

TSharedPtr<FWidgetTemplate> ResolveTemplate(UWidgetBlueprint* Blueprint, const FString& Id, FString& OutError)
{
    const TArray<FPaletteEntry> Entries = WidgetPalette(Blueprint);
    const FPaletteEntry* Entry = Entries.FindByPredicate([&Id](const FPaletteEntry& Candidate)
    {
        return Candidate.Id == Id;
    });
    if (Entry == nullptr)
    {
        OutError = TEXT("Widget Palette entry is unavailable in the current WidgetBlueprint context.");
        return nullptr;
    }
    if (Entry->Kind == FPaletteEntry::EKind::ImageAsset)
    {
        if (Entry->Asset.GetAsset() == nullptr)
        {
            OutError = TEXT("Image Palette resource could not be loaded.");
            return nullptr;
        }
        return MakeShared<FWidgetTemplateImageClass>(Entry->Asset);
    }
    if (Entry->Kind == FPaletteEntry::EKind::WidgetBlueprint)
    {
        return MakeShared<FWidgetTemplateBlueprintClass>(Entry->Asset);
    }
    if (Entry->Class != nullptr && Entry->Class->IsChildOf(UUserWidget::StaticClass()))
    {
        return MakeShared<FWidgetTemplateBlueprintClass>(Entry->Asset, Entry->Class);
    }
    if (Entry->Class != nullptr)
    {
        return MakeShared<FWidgetTemplateClass>(Entry->Class);
    }
    return TSharedPtr<FWidgetTemplate>();
}
}

TSharedPtr<FJsonObject> FSalWidgetInterface::Query(const FSalQuery& Query, const FSalResolvedTarget& Target)
{
    UWidgetBlueprint* Blueprint = WidgetBlueprint(Target);
    if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr)
    {
        return QueryError(TEXT("capability.interface_unavailable"), TEXT("Widget interface requires a resolved UWidgetBlueprint with a WidgetTree."));
    }
    FString IdError;
    if (!ValidateWidgetIds(Blueprint, IdError))
    {
        return QueryError(TEXT("validation.invalid_widget_identity"), IdError);
    }
    FString Operation;
    if (!ReadKind(Query.Operation, Operation))
    {
        return QueryError(TEXT("capability.unsupported_query_operation"), TEXT("Widget Query has no supported primary operation."));
    }
    for (const FString& Detail : Query.With)
    {
        if (Detail != TEXT("schema"))
        {
            return QueryError(TEXT("capability.detail_unavailable"), FString::Printf(TEXT("Widget does not support with %s."), *Detail), Operation, Detail);
        }
    }
    const bool bSchema = HasDetail(Query, TEXT("schema"));

    if (Operation == TEXT("summary"))
    {
        if (bSchema || Query.Where.IsValid() || !Query.OrderBy.IsEmpty() || Query.PageLimit > 0 || !Query.PageAfter.IsEmpty())
        {
            return QueryError(TEXT("capability.clause_unavailable"), TEXT("Widget summary accepts no clauses."), Operation);
        }
        const TArray<UWidget*> All = SourceWidgets(Blueprint);
        const TArray<UWidget*> Reachable = ReachableWidgets(Blueprint);
        FSalObjectBuilder Builder;
        TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetStringField(TEXT("id"), GuidText(Blueprint->GetBlueprintGuid()));
        Args->SetField(
            TEXT("type"),
            Value::Name(StaticEnum<EBlueprintType>()->GetNameStringByValue(static_cast<int64>(Blueprint->BlueprintType))));
        Args->SetField(
            TEXT("Status"),
            Value::Name(StaticEnum<EBlueprintStatus>()->GetNameStringByValue(static_cast<int64>(Blueprint->Status))));
        if (Blueprint->ParentClass != nullptr)
        {
            Args->SetStringField(TEXT("ParentClass"), Blueprint->ParentClass->GetPathName());
        }
        if (Blueprint->WidgetTree->RootWidget != nullptr)
        {
            Args->SetField(TEXT("Root"), Value::Stable(TEXT("widget"), WidgetId(Blueprint, Blueprint->WidgetTree->RootWidget)));
        }
        Builder.AddLocalBinding(Builder.UniqueAlias(Blueprint->GetName()), Value::Call(TEXT("blueprint"), Args));
        int32 VariableCount = 0;
        int32 DispatcherCount = 0;
        for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
        {
            Variable.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate
                ? ++DispatcherCount
                : ++VariableCount;
        }
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        const int32 ComponentCount = Blueprint->SimpleConstructionScript != nullptr
            ? Blueprint->SimpleConstructionScript->GetAllNodes().Num()
            : 0;
        Builder.AddComment(FString::Printf(TEXT("variables: %d"), VariableCount));
        Builder.AddComment(FString::Printf(TEXT("dispatchers: %d"), DispatcherCount));
        Builder.AddComment(FString::Printf(TEXT("graphs: %d"), Graphs.Num()));
        Builder.AddComment(FString::Printf(TEXT("components: %d"), ComponentCount));
        Builder.AddComment(FString::Printf(TEXT("widgets: %d"), All.Num()));
        Builder.AddComment(FString::Printf(TEXT("reachable widgets: %d"), Reachable.Num()));
        Builder.AddComment(FString::Printf(TEXT("detached widgets: %d"), All.Num() - Reachable.Num()));
        return Builder.BuildResult();
    }

    if (Operation == TEXT("tree"))
    {
        if (bSchema || Query.Where.IsValid() || !Query.OrderBy.IsEmpty() || Query.PageLimit > 0 || !Query.PageAfter.IsEmpty())
        {
            return QueryError(TEXT("capability.clause_unavailable"), TEXT("Widget tree accepts only optional root and depth."), Operation);
        }
        UWidget* Start = Blueprint->WidgetTree->RootWidget;
        bool bExplicitRoot = false;
        const TSharedPtr<FJsonObject>* Root = nullptr;
        if (Query.Operation->TryGetObjectField(TEXT("root"), Root) && Root != nullptr)
        {
            bExplicitRoot = true;
            FString Kind;
            FString Id;
            if (!ReadRef(*Root, Kind, Id) || Kind != TEXT("widget") || (Start = FindWidgetById(Blueprint, Id)) == nullptr)
            {
                return QueryError(TEXT("resolution.widget_not_found"), TEXT("Widget tree root was not found."), Operation, Id);
            }
        }
        double RequestedDepth = 20.0;
        Query.Operation->TryGetNumberField(TEXT("depth"), RequestedDepth);
        const int32 Depth = FMath::Max(1, static_cast<int32>(RequestedDepth));
        return bExplicitRoot ? TreeResult(Blueprint, Start, Depth) : FullTreeResult(Blueprint, Depth);
    }

    if (Operation == TEXT("widgets"))
    {
        if (bSchema)
        {
            return QueryError(TEXT("capability.detail_unavailable"), TEXT("Widget collection is compact; query an exact Widget with schema."), Operation);
        }
        FString FilterError;
        if (!ValidateWidgetCondition(Query.Where, FilterError))
        {
            return QueryError(TEXT("capability.clause_unavailable"), FilterError, Operation);
        }
        for (const TSharedPtr<FJsonObject>& Entry : Query.OrderBy)
        {
            FString Key;
            Entry->TryGetStringField(TEXT("key"), Key);
            if (!(Key == TEXT("name") || Key == TEXT("type") || Key == TEXT("id")))
            {
                return QueryError(TEXT("capability.clause_unavailable"), FString::Printf(TEXT("Widget order key is unsupported: %s."), *Key), Operation);
            }
        }
        FString Search;
        Query.Operation->TryGetStringField(TEXT("text"), Search);
        const TArray<UWidget*> ReachableOrder = ReachableWidgets(Blueprint);
        TSet<UWidget*> Reachable;
        Reachable.Append(ReachableOrder);
        TMap<UWidget*, int32> TreeIndex;
        for (int32 Index = 0; Index < ReachableOrder.Num(); ++Index) TreeIndex.Add(ReachableOrder[Index], Index);
        TArray<FWidgetItem> Items;
        for (UWidget* Widget : SourceWidgets(Blueprint))
        {
            const double Score = WidgetSearchScore(Widget, Search);
            if (Score < 0.0 || !MatchesWidgetCondition(Blueprint, Widget, Query.Where, Reachable)) continue;
            Items.Add({Widget, Score, TreeIndex.FindRef(Widget), Reachable.Contains(Widget)});
            if (!Reachable.Contains(Widget)) Items.Last().TreeIndex = MAX_int32;
        }
        Items.Sort([&](const FWidgetItem& Left, const FWidgetItem& Right)
        {
            for (const TSharedPtr<FJsonObject>& Entry : Query.OrderBy)
            {
                FString Key;
                FString Direction;
                Entry->TryGetStringField(TEXT("key"), Key);
                Entry->TryGetStringField(TEXT("direction"), Direction);
                const FString A = WidgetField(Blueprint, Left.Widget, Key, Reachable);
                const FString B = WidgetField(Blueprint, Right.Widget, Key, Reachable);
                const int32 Result = A.Compare(B, ESearchCase::IgnoreCase);
                if (Result != 0) return Direction == TEXT("desc") ? Result > 0 : Result < 0;
            }
            if (Query.OrderBy.IsEmpty() && !Search.IsEmpty() && Left.Score != Right.Score) return Left.Score > Right.Score;
            if (Query.OrderBy.IsEmpty() && Left.TreeIndex != Right.TreeIndex) return Left.TreeIndex < Right.TreeIndex;
            return Left.Widget->GetName() < Right.Widget->GetName();
        });
        FSalPage Page;
        if (!DecodeWidgetPage(Query, Blueprint, 50, 200, Page))
        {
            return QueryError(
                TEXT("validation.invalid_cursor"),
                TEXT("Widget cursor does not belong to this target and Query. Re-run the first page."),
                Operation,
                Query.PageAfter);
        }
        const int32 End = FMath::Min(Page.Offset + Page.Limit, Items.Num());
        FSalObjectBuilder Builder;
        for (int32 Index = FMath::Min(Page.Offset, Items.Num()); Index < End; ++Index)
        {
            const FWidgetItem& Item = Items[Index];
            Builder.AddLocalBinding(Builder.UniqueAlias(Item.Widget->GetName()), WidgetValue(Blueprint, Item.Widget, EWidgetDetail::Compact));
            if (!Item.bReachable) Builder.AddComment(TEXT("detached source Widget"));
        }
        if (Items.IsEmpty()) Builder.AddComment(TEXT("no matches"));
        return MakeWidgetPageResult(Builder.BuildResult(), Query, Blueprint, End, End < Items.Num());
    }

    if (Operation == TEXT("widget"))
    {
        if (Query.Where.IsValid() || !Query.OrderBy.IsEmpty() || Query.PageLimit > 0 || !Query.PageAfter.IsEmpty())
        {
            return QueryError(TEXT("capability.clause_unavailable"), TEXT("Exact Widget Query accepts only with schema."), Operation);
        }
        UWidget* Widget = nullptr;
        FString Ref;
        if (Query.Operation->HasField(TEXT("name")))
        {
            Query.Operation->TryGetStringField(TEXT("name"), Ref);
            Widget = FindWidgetByName(Blueprint, Ref);
        }
        else
        {
            Query.Operation->TryGetStringField(TEXT("id"), Ref);
            Widget = FindWidgetById(Blueprint, Ref);
        }
        if (Widget == nullptr)
        {
            return QueryError(TEXT("resolution.widget_not_found"), TEXT("Exact Widget was not found or its identity is ambiguous."), Operation, Ref);
        }
        FSalObjectBuilder Builder;
        EmitExactWidget(Builder, Blueprint, Widget, bSchema);
        return Builder.BuildResult();
    }

    if (Operation == TEXT("palette_entries"))
    {
        if (bSchema || Query.Where.IsValid() || !Query.OrderBy.IsEmpty())
        {
            return QueryError(TEXT("capability.clause_unavailable"), TEXT("Widget Palette search accepts only text and pagination."), Operation);
        }
        FString Search;
        Query.Operation->TryGetStringField(TEXT("text"), Search);
        TArray<FPaletteEntry> Entries = WidgetPalette(Blueprint);
        if (!Search.IsEmpty())
        {
            Entries = Entries.FilterByPredicate([&Search](const FPaletteEntry& Entry)
            {
                return Entry.Label.Contains(Search, ESearchCase::IgnoreCase)
                    || Entry.Type.Contains(Search, ESearchCase::IgnoreCase);
            });
        }
        FSalPage Page;
        if (!DecodeWidgetPage(Query, Blueprint, 50, 200, Page))
        {
            return QueryError(
                TEXT("validation.invalid_cursor"),
                TEXT("Widget Palette cursor does not belong to this target and Query. Re-run the first page."),
                Operation,
                Query.PageAfter);
        }
        const int32 End = FMath::Min(Page.Offset + Page.Limit, Entries.Num());
        FSalObjectBuilder Builder;
        for (int32 Index = FMath::Min(Page.Offset, Entries.Num()); Index < End; ++Index) EmitPalette(Builder, Blueprint, Entries[Index], false);
        if (Entries.IsEmpty()) Builder.AddComment(TEXT("no matches"));
        return MakeWidgetPageResult(Builder.BuildResult(), Query, Blueprint, End, End < Entries.Num());
    }

    if (Operation == TEXT("palette"))
    {
        if (Query.Where.IsValid() || !Query.OrderBy.IsEmpty() || Query.PageLimit > 0 || !Query.PageAfter.IsEmpty())
        {
            return QueryError(TEXT("capability.clause_unavailable"), TEXT("Exact Widget Palette Query accepts only with schema."), Operation);
        }
        FString Id;
        Query.Operation->TryGetStringField(TEXT("id"), Id);
        for (const FPaletteEntry& Entry : WidgetPalette(Blueprint))
        {
            if (Entry.Id == Id)
            {
                FSalObjectBuilder Builder;
                EmitPalette(Builder, Blueprint, Entry, bSchema);
                return Builder.BuildResult();
            }
        }
        return QueryError(TEXT("resolution.palette_entry_not_found"), TEXT("Widget Palette entry is no longer available."), Operation, Id);
    }

    return QueryError(
        TEXT("capability.unsupported_query_operation"),
        FString::Printf(TEXT("Widget does not implement %s."), *Operation),
        Operation,
        FString(),
        {TEXT("summary"), TEXT("tree"), TEXT("widgets"), TEXT("widget"), TEXT("palette_entries"), TEXT("palette")});
}

// Patch implementation follows the Query implementation so its dry run can execute the
// same native WidgetTree edit path against a transient duplicate before touching the asset.

namespace
{
struct FWidgetPlanIdentity
{
    FString Ref;
    FString Name;
    FString Origin;
};

struct FWidgetPatchEffect
{
    TSharedPtr<FJsonObject> Detail;
    FString Summary;
};

struct FWidgetPatchContext
{
    UWidgetBlueprint* Blueprint = nullptr;
    FString TargetAlias;
    TMap<FString, FString> Declarations;
    TSet<FString> ConsumedDeclarations;
    TMap<FString, UWidget*> Locals;
    TArray<FString> LocalOrder;
    TSet<UWidget*> Touched;
    TArray<UWidget*> TouchedOrder;
    TMap<UWidget*, FWidgetPlanIdentity> PlanIdentities;
    TArray<FWidgetPatchEffect> Effects;
    FString ErrorCode = TEXT("validation.widget_patch_invalid");
};

void SeedPlanIdentities(FWidgetPatchContext& Context)
{
    for (UWidget* Widget : SourceWidgets(Context.Blueprint))
    {
        if (Widget == nullptr)
        {
            continue;
        }
        const FString Id = WidgetId(Context.Blueprint, Widget);
        Context.PlanIdentities.Add(
            Widget,
            {Id.IsEmpty() ? FString() : TEXT("widget@") + Id, Widget->GetName(), TEXT("live")});
    }
}

void RegisterCreatedPlanIdentities(
    FWidgetPatchContext& Context,
    UWidget* Widget,
    const FString& Ref,
    TSet<UWidget*>& Seen)
{
    if (Widget == nullptr || Seen.Contains(Widget))
    {
        return;
    }
    Seen.Add(Widget);
    Context.PlanIdentities.Add(Widget, {Ref, Widget->GetName(), TEXT("local")});
    for (UWidget* Child : DirectChildren(Widget))
    {
        RegisterCreatedPlanIdentities(Context, Child, Ref + TEXT(".") + Child->GetName(), Seen);
    }
}

void RegisterCreatedPlanIdentities(
    FWidgetPatchContext& Context,
    UWidget* Primary,
    const FString& Alias,
    const TArray<UWidget*>& Created)
{
    TSet<UWidget*> Seen;
    RegisterCreatedPlanIdentities(Context, Primary, Alias, Seen);
    for (UWidget* Widget : Created)
    {
        if (Widget != nullptr && !Seen.Contains(Widget))
        {
            RegisterCreatedPlanIdentities(Context, Widget, Alias + TEXT(".") + Widget->GetName(), Seen);
        }
    }
}

TSharedPtr<FJsonObject> PlanIdentity(const FWidgetPatchContext& Context, UWidget* Widget)
{
    TSharedPtr<FJsonObject> Identity = MakeShared<FJsonObject>();
    if (const FWidgetPlanIdentity* Known = Context.PlanIdentities.Find(Widget))
    {
        if (!Known->Ref.IsEmpty()) Identity->SetStringField(TEXT("ref"), Known->Ref);
        Identity->SetStringField(TEXT("name"), Known->Name);
        Identity->SetStringField(TEXT("origin"), Known->Origin);
    }
    else if (Widget != nullptr)
    {
        // An unknown object is never assigned its duplicate's generated GUID.  Name is
        // the only safe identity until the object is registered under a local alias.
        Identity->SetStringField(TEXT("name"), Widget->GetName());
        Identity->SetStringField(TEXT("origin"), TEXT("local"));
    }
    return Identity;
}

FString PlanRef(const FWidgetPatchContext& Context, UWidget* Widget)
{
    if (const FWidgetPlanIdentity* Known = Context.PlanIdentities.Find(Widget))
    {
        return !Known->Ref.IsEmpty() ? Known->Ref : Known->Name;
    }
    return Widget != nullptr ? Widget->GetName() : TEXT("unknown");
}

TSharedPtr<FJsonObject> PlanPlacement(const FWidgetPatchContext& Context, UWidget* Widget)
{
    TSharedPtr<FJsonObject> Placement = MakeShared<FJsonObject>();
    if (Widget == nullptr)
    {
        Placement->SetStringField(TEXT("relationship"), TEXT("unavailable"));
        return Placement;
    }
    if (Widget == Context.Blueprint->WidgetTree->RootWidget)
    {
        Placement->SetStringField(TEXT("relationship"), TEXT("root"));
        Placement->SetStringField(TEXT("path"), Context.TargetAlias + TEXT(".Root"));
        return Placement;
    }
    if (UPanelWidget* Parent = Widget->GetParent())
    {
        const int32 Index = Parent->GetChildIndex(Widget);
        Placement->SetStringField(TEXT("relationship"), TEXT("panel_child"));
        Placement->SetObjectField(TEXT("parent"), PlanIdentity(Context, Parent));
        Placement->SetNumberField(TEXT("index"), Index);
        Placement->SetStringField(
            TEXT("path"),
            FString::Printf(TEXT("%s.children[%d]"), *PlanRef(Context, Parent), Index));
        if (TSharedPtr<FJsonObject> Slot = SlotValue(Widget->Slot, true))
        {
            Placement->SetObjectField(TEXT("slot"), Slot);
        }
        return Placement;
    }

    TScriptInterface<INamedSlotInterface> Host =
        FWidgetBlueprintEditorUtils::FindNamedSlotHostForContent(Widget, Context.Blueprint->WidgetTree);
    if (Host)
    {
        TArray<FName> Names;
        Host->GetSlotNames(Names);
        for (const FName Name : Names)
        {
            if (Host->GetContentForSlot(Name) == Widget)
            {
                Placement->SetStringField(TEXT("relationship"), TEXT("named_slot"));
                if (UWidget* HostWidget = Cast<UWidget>(Host.GetObject()))
                {
                    Placement->SetObjectField(TEXT("host"), PlanIdentity(Context, HostWidget));
                    Placement->SetStringField(
                        TEXT("path"),
                        PlanRef(Context, HostWidget) + TEXT(".NamedSlots.") + Name.ToString());
                }
                else
                {
                    Placement->SetStringField(TEXT("path"), Context.TargetAlias + TEXT(".NamedSlots.") + Name.ToString());
                }
                Placement->SetStringField(TEXT("slotName"), Name.ToString());
                return Placement;
            }
        }
    }
    if (const FName* Name = Context.Blueprint->WidgetTree->NamedSlotBindings.FindKey(Widget))
    {
        Placement->SetStringField(TEXT("relationship"), TEXT("named_slot"));
        Placement->SetStringField(TEXT("slotName"), Name->ToString());
        Placement->SetStringField(TEXT("path"), Context.TargetAlias + TEXT(".NamedSlots.") + Name->ToString());
        return Placement;
    }
    Placement->SetStringField(TEXT("relationship"), TEXT("detached"));
    return Placement;
}

TSharedPtr<FJsonObject> PlanWidgetState(const FWidgetPatchContext& Context, UWidget* Widget)
{
    TSharedPtr<FJsonObject> State = MakeShared<FJsonObject>();
    State->SetObjectField(TEXT("identity"), PlanIdentity(Context, Widget));
    if (Widget != nullptr)
    {
        State->SetStringField(TEXT("type"), Widget->GetClass()->GetPathName());
        State->SetObjectField(TEXT("placement"), PlanPlacement(Context, Widget));
    }
    return State;
}

void AddEffect(
    FWidgetPatchContext& Context,
    const TSharedPtr<FJsonObject>& Detail,
    const FString& Summary)
{
    Context.Effects.Add({Detail, Summary});
}

TSharedPtr<FJsonObject> StructuralEffect(
    const FString& Kind,
    const FWidgetPatchContext& Context,
    UWidget* Target,
    const TSharedPtr<FJsonObject>& Before,
    const TSharedPtr<FJsonObject>& After)
{
    TSharedPtr<FJsonObject> Effect = MakeShared<FJsonObject>();
    Effect->SetStringField(TEXT("kind"), Kind);
    Effect->SetObjectField(TEXT("target"), PlanIdentity(Context, Target));
    if (Before.IsValid()) Effect->SetObjectField(TEXT("before"), Before);
    if (After.IsValid()) Effect->SetObjectField(TEXT("after"), After);
    return Effect;
}

void Touch(FWidgetPatchContext& Context, UWidget* Widget)
{
    if (Widget != nullptr && !Context.Touched.Contains(Widget))
    {
        Context.Touched.Add(Widget);
        Context.TouchedOrder.Add(Widget);
    }
}

void SetLocal(FWidgetPatchContext& Context, const FString& Alias, UWidget* Widget)
{
    if (!Context.Locals.Contains(Alias))
    {
        Context.LocalOrder.Add(Alias);
    }
    Context.Locals.Add(Alias, Widget);
}

UWidget* ResolvePatchWidget(FWidgetPatchContext& Context, const TSharedPtr<FJsonObject>& Ref, FString& OutError)
{
    FString Kind;
    FString Identity;
    if (!ReadRef(Ref, Kind, Identity))
    {
        OutError = TEXT("Expected a local or widget@id reference.");
        return nullptr;
    }
    if (Kind == TEXT("local"))
    {
        if (UWidget** Local = Context.Locals.Find(Identity)) return *Local;
        Context.ErrorCode = TEXT("resolution.binding_not_found");
        OutError = FString::Printf(TEXT("Local Widget %s has not been materialized."), *Identity);
        return nullptr;
    }
    if (Kind != TEXT("widget"))
    {
        OutError = FString::Printf(TEXT("Expected widget reference, got %s."), *Kind);
        return nullptr;
    }
    UWidget* Widget = FindWidgetById(Context.Blueprint, Identity);
    if (Widget == nullptr)
    {
        Context.ErrorCode = TEXT("resolution.widget_not_found");
        OutError = FString::Printf(TEXT("Widget was not found: %s."), *Identity);
    }
    return Widget;
}

bool RegisterCreatedWidgets(
    FWidgetPatchContext& Context,
    const TArray<UWidget*>& Before,
    UWidget* Primary,
    const FString& Alias,
    TArray<UWidget*>& OutCreated,
    FString& OutError)
{
    UWidgetBlueprint* Blueprint = Context.Blueprint;
    TSet<UWidget*> Existing;
    Existing.Append(Before);
    TArray<UWidget*> Created;
    for (UWidget* Widget : SourceWidgets(Blueprint))
    {
        if (Widget != nullptr && !Existing.Contains(Widget)) Created.Add(Widget);
    }
    if (Primary == nullptr || !Created.Contains(Primary))
    {
        OutError = TEXT("Widget template did not create a primary source Widget.");
        return false;
    }
    FKismetNameValidator NameValidator(Blueprint, Primary->GetFName());
    const EValidatorResult NameResult = NameValidator.IsValid(FName(*Alias));
    if (Primary->GetName() != Alias && NameResult != EValidatorResult::Ok)
    {
        OutError = INameValidatorInterface::GetErrorString(Alias, NameResult);
        return false;
    }
    if (Primary->GetName() != Alias
        && !Primary->Rename(*Alias, Blueprint->WidgetTree, REN_Test | REN_DontCreateRedirectors | REN_ForceNoResetLoaders))
    {
        OutError = FString::Printf(TEXT("Widget object name is unavailable: %s."), *Alias);
        return false;
    }
    if (Primary->GetName() != Alias && !Primary->Rename(*Alias, Blueprint->WidgetTree, REN_DontCreateRedirectors | REN_ForceNoResetLoaders))
    {
        OutError = FString::Printf(TEXT("UE could not assign Widget name %s."), *Alias);
        return false;
    }
    for (UWidget* Widget : Created)
    {
        if (!Blueprint->WidgetVariableNameToGuidMap.Contains(Widget->GetFName())) Blueprint->OnVariableAdded(Widget->GetFName());
        Touch(Context, Widget);
    }
    RegisterCreatedPlanIdentities(Context, Primary, Alias, Created);
    OutCreated = Created;
    return true;
}

UWidget* Materialize(FWidgetPatchContext& Context, const FString& Alias, FString& OutError)
{
    if (Context.ConsumedDeclarations.Contains(Alias) || Context.Locals.Contains(Alias))
    {
        OutError = FString::Printf(TEXT("Widget binding %s has already been materialized."), *Alias);
        return nullptr;
    }
    const FString* PaletteId = Context.Declarations.Find(Alias);
    if (PaletteId == nullptr)
    {
        Context.ErrorCode = TEXT("resolution.binding_not_found");
        OutError = FString::Printf(TEXT("Widget binding %s was not declared with widget(palette: ...)."), *Alias);
        return nullptr;
    }
    TSharedPtr<FWidgetTemplate> Template = ResolveTemplate(Context.Blueprint, *PaletteId, OutError);
    if (!Template.IsValid())
    {
        Context.ErrorCode = TEXT("resolution.palette_entry_not_found");
        return nullptr;
    }
    const TArray<UWidget*> Before = SourceWidgets(Context.Blueprint);
    UWidget* Created = Template->Create(Context.Blueprint->WidgetTree);
    if (Created == nullptr)
    {
        OutError = TEXT("FWidgetTemplate::Create returned no primary Widget.");
        return nullptr;
    }
    if (UUserWidget* UserWidget = Cast<UUserWidget>(Created); UserWidget != nullptr && !Context.Blueprint->IsWidgetFreeFromCircularReferences(UserWidget))
    {
        OutError = TEXT("Widget template would create a circular User Widget reference.");
        return nullptr;
    }
    TArray<UWidget*> CreatedObjects;
    if (!RegisterCreatedWidgets(Context, Before, Created, Alias, CreatedObjects, OutError)) return nullptr;
    Context.ConsumedDeclarations.Add(Alias);
    SetLocal(Context, Alias, Created);
    TSharedPtr<FJsonObject> Effect = MakeShared<FJsonObject>();
    Effect->SetStringField(TEXT("kind"), TEXT("create"));
    Effect->SetObjectField(TEXT("target"), PlanIdentity(Context, Created));
    TArray<TSharedPtr<FJsonValue>> Objects;
    for (UWidget* Object : CreatedObjects)
    {
        Objects.Add(MakeShared<FJsonValueObject>(PlanWidgetState(Context, Object)));
    }
    TSharedPtr<FJsonObject> After = MakeShared<FJsonObject>();
    After->SetArrayField(TEXT("objects"), Objects);
    Effect->SetObjectField(TEXT("after"), After);
    AddEffect(Context, Effect, FString::Printf(TEXT("created %s"), *Alias));
    return Created;
}

bool IsDescendant(UWidget* Parent, UWidget* Candidate)
{
    if (Parent == nullptr || Candidate == nullptr) return false;
    TArray<UWidget*> Descendants;
    UWidgetTree::GetChildWidgets(Parent, Descendants);
    return Descendants.Contains(Candidate);
}

bool DetachWidget(UWidgetBlueprint* Blueprint, UWidget* Widget, FString& OutError)
{
    Widget->Modify();
    if (Widget->GetParent() != nullptr)
    {
        UPanelWidget* Parent = Widget->GetParent();
        Parent->Modify();
        return Parent->RemoveChild(Widget);
    }
    if (TScriptInterface<INamedSlotInterface> Host = FWidgetBlueprintEditorUtils::FindNamedSlotHostForContent(Widget, Blueprint->WidgetTree))
    {
        if (UObject* HostObject = Host.GetObject()) HostObject->Modify();
        return FWidgetBlueprintEditorUtils::RemoveNamedSlotHostContent(Widget, Host);
    }
    if (const FName* Slot = Blueprint->WidgetTree->NamedSlotBindings.FindKey(Widget))
    {
        Blueprint->WidgetTree->Modify();
        Blueprint->WidgetTree->SetContentForSlot(*Slot, nullptr);
        return true;
    }
    OutError = TEXT("Widget is detached or is the WidgetTree root.");
    return false;
}

bool PlaceInNamedSlot(
    FWidgetPatchContext& Context,
    UWidget* Widget,
    const TSharedPtr<FJsonObject>& Owner,
    const TArray<FString>& Path,
    FString& OutError)
{
    if (Path.Num() != 2 || Path[0] != TEXT("NamedSlots"))
    {
        OutError = TEXT("Named Slot destination must end in NamedSlots.<name>.");
        return false;
    }
    FString Kind;
    FString Identity;
    if (!ReadRef(Owner, Kind, Identity))
    {
        return false;
    }
    INamedSlotInterface* Host = nullptr;
    UWidget* HostWidget = nullptr;
    if (Kind == TEXT("local") && Identity == Context.TargetAlias)
    {
        Host = Context.Blueprint->WidgetTree;
    }
    else
    {
        HostWidget = ResolvePatchWidget(Context, Owner, OutError);
        Host = Cast<INamedSlotInterface>(HostWidget);
    }
    if (Host == nullptr)
    {
        OutError = TEXT("Named Slot host does not implement INamedSlotInterface.");
        return false;
    }
    if (HostWidget != nullptr && (HostWidget == Widget || IsDescendant(Widget, HostWidget)))
    {
        OutError = TEXT("Named Slot placement would create an ownership cycle.");
        return false;
    }
    TArray<FName> Names;
    Host->GetSlotNames(Names);
    const FName SlotName(*Path[1]);
    if (!Names.Contains(SlotName))
    {
        OutError = FString::Printf(TEXT("Named Slot does not exist: %s."), *Path[1]);
        return false;
    }
    if (Host->GetContentForSlot(SlotName) != nullptr)
    {
        OutError = FString::Printf(TEXT("Named Slot is not empty: %s."), *Path[1]);
        return false;
    }
    Widget->Modify();
    if (HostWidget != nullptr) HostWidget->Modify();
    else Context.Blueprint->WidgetTree->Modify();
    Host->SetContentForSlot(SlotName, Widget);
    return true;
}

bool PlaceWidget(
    FWidgetPatchContext& Context,
    UWidget* Widget,
    const TSharedPtr<FJsonObject>& To,
    const TSharedPtr<FJsonObject>& Before,
    const TSharedPtr<FJsonObject>& After,
    const bool bMoving,
    FString& OutError)
{
    if (!To.IsValid() && !Before.IsValid() && !After.IsValid())
    {
        if (bMoving || Context.Blueprint->WidgetTree->RootWidget != nullptr)
        {
            OutError = TEXT("Bare add requires an empty WidgetTree root.");
            return false;
        }
        Context.Blueprint->WidgetTree->Modify();
        Widget->Modify();
        Context.Blueprint->WidgetTree->RootWidget = Widget;
        return true;
    }

    TMap<FName, FString> OldSlotProperties;
    if (bMoving && Widget->Slot != nullptr) FWidgetBlueprintEditorUtils::ExportPropertiesToText(Widget->Slot, OldSlotProperties);

    UPanelWidget* OriginalParent = Widget->GetParent();
    if (bMoving && (Before.IsValid() || After.IsValid()))
    {
        UWidget* Anchor = ResolvePatchWidget(Context, Before.IsValid() ? Before : After, OutError);
        UPanelWidget* AnchorParent = Anchor != nullptr ? Anchor->GetParent() : nullptr;
        if (AnchorParent != nullptr && AnchorParent == OriginalParent)
        {
            if (Anchor == Widget)
            {
                OutError = TEXT("Widget cannot be moved relative to itself.");
                return false;
            }
            int32 Index = AnchorParent->GetChildIndex(Anchor) + (After.IsValid() ? 1 : 0);
            const int32 CurrentIndex = AnchorParent->GetChildIndex(Widget);
            if (CurrentIndex < Index) --Index;
            AnchorParent->Modify();
            Widget->Modify();
            AnchorParent->ShiftChild(Index, Widget);
            return true;
        }
    }
    if (bMoving && To.IsValid())
    {
        FString ToKind;
        To->TryGetStringField(TEXT("kind"), ToKind);
        if (ToKind != TEXT("member"))
        {
            UWidget* Destination = ResolvePatchWidget(Context, To, OutError);
            if (Destination == OriginalParent && OriginalParent != nullptr)
            {
                OriginalParent->Modify();
                Widget->Modify();
                OriginalParent->ShiftChild(OriginalParent->GetChildrenCount(), Widget);
                return true;
            }
        }
    }
    if (bMoving && !DetachWidget(Context.Blueprint, Widget, OutError)) return false;

    if (To.IsValid())
    {
        FString ToKind;
        To->TryGetStringField(TEXT("kind"), ToKind);
        if (ToKind == TEXT("member"))
        {
            TSharedPtr<FJsonObject> Owner;
            TArray<FString> Path;
            if (!ReadMember(To, Owner, Path) || !PlaceInNamedSlot(Context, Widget, Owner, Path, OutError)) return false;
            return true;
        }
        UWidget* ParentWidget = ResolvePatchWidget(Context, To, OutError);
        UPanelWidget* Panel = Cast<UPanelWidget>(ParentWidget);
        if (Panel == nullptr || !Panel->CanAddMoreChildren())
        {
            OutError = TEXT("Widget destination is not a Panel with available capacity.");
            return false;
        }
        if (Panel == Widget || IsDescendant(Widget, Panel))
        {
            OutError = TEXT("Widget move would create an ownership cycle.");
            return false;
        }
        Panel->Modify();
        Widget->Modify();
        UPanelSlot* Slot = Panel->AddChild(Widget);
        if (Slot == nullptr) return false;
        FWidgetBlueprintEditorUtils::ImportPropertiesFromText(Slot, OldSlotProperties);
        return true;
    }

    UWidget* Anchor = ResolvePatchWidget(Context, Before.IsValid() ? Before : After, OutError);
    UPanelWidget* Parent = Anchor != nullptr ? Anchor->GetParent() : nullptr;
    if (Parent == nullptr || (!Parent->CanAddMoreChildren() && Parent != Widget->GetParent()))
    {
        OutError = TEXT("before/after anchor has no compatible Panel parent.");
        return false;
    }
    if (Parent == Widget || IsDescendant(Widget, Parent))
    {
        OutError = TEXT("Widget move would create an ownership cycle.");
        return false;
    }
    int32 Index = Parent->GetChildIndex(Anchor) + (After.IsValid() ? 1 : 0);
    Parent->Modify();
    Widget->Modify();
    UPanelSlot* Slot = Parent->InsertChildAt(Index, Widget);
    if (Slot == nullptr) return false;
    FWidgetBlueprintEditorUtils::ImportPropertiesFromText(Slot, OldSlotProperties);
    return true;
}

bool ValidateNativePropertyImport(
    FProperty* Property,
    UObject* Container,
    const FString& Text,
    FString& OutError)
{
    if (Property == nullptr || Container == nullptr)
    {
        OutError = TEXT("Property or value container is unavailable.");
        return false;
    }
    void* Temporary = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
    Property->InitializeValue(Temporary);
    Property->CopyCompleteValue(Temporary, Property->ContainerPtrToValuePtr<void>(Container));
    const TCHAR* End = Property->ImportText_Direct(*Text, Temporary, Container, PPF_None, GLog);
    bool bValid = End != nullptr;
    if (End != nullptr)
    {
        while (*End != TEXT('\0') && FChar::IsWhitespace(*End))
        {
            ++End;
        }
        bValid = *End == TEXT('\0');
    }
    Property->DestroyValue(Temporary);
    FMemory::Free(Temporary);
    if (!bValid)
    {
        OutError = FString::Printf(TEXT("UE could not import a complete value for %s."), *Property->GetName());
    }
    return bValid;
}

bool ApplyField(FWidgetPatchContext& Context, const TSharedPtr<FJsonObject>& Statement, const bool bReset, FString& OutError)
{
    const TSharedPtr<FJsonObject>* Target = nullptr;
    TSharedPtr<FJsonObject> Owner;
    TArray<FString> Path;
    if (!Statement->TryGetObjectField(TEXT("target"), Target) || Target == nullptr || !ReadMember(*Target, Owner, Path))
    {
        OutError = TEXT("set/reset requires a Widget member target.");
        return false;
    }
    UWidget* Widget = ResolvePatchWidget(Context, Owner, OutError);
    if (Widget == nullptr) return false;
    UObject* Container = Widget;
    if (Path.Num() == 2 && Path[0] == TEXT("Slot"))
    {
        Container = Widget->Slot;
        Path.RemoveAt(0);
    }
    if (Container == nullptr || Path.Num() != 1 || Path[0] == TEXT("NamedSlots"))
    {
        OutError = TEXT("Widget field path is unavailable or structural.");
        return false;
    }
    if (Container == Widget && Path[0] == TEXT("bIsVariable"))
    {
        const bool bBefore = Widget->bIsVariable;
        bool bValue = false;
        if (!bReset && !BoolExpr(Statement->TryGetField(TEXT("value")), bValue))
        {
            OutError = TEXT("bIsVariable requires a Boolean value.");
            return false;
        }
        Widget->Modify();
        Widget->bIsVariable = bReset ? false : bValue;
        if (FWidgetBlueprintEditor* Editor = FindOpenWidgetBlueprintEditor(Context.Blueprint))
        {
            if (UWidget* Preview = Editor->GetReferenceFromTemplate(Widget).GetPreview(); Preview != nullptr && Preview != Widget)
            {
                Preview->Modify();
                Preview->bIsVariable = Widget->bIsVariable;
            }
        }
        Touch(Context, Widget);
        TSharedPtr<FJsonObject> Effect = MakeShared<FJsonObject>();
        Effect->SetStringField(TEXT("kind"), bReset ? TEXT("reset") : TEXT("set"));
        Effect->SetObjectField(TEXT("target"), PlanIdentity(Context, Widget));
        Effect->SetStringField(TEXT("field"), TEXT("bIsVariable"));
        Effect->SetBoolField(TEXT("before"), bBefore);
        Effect->SetBoolField(TEXT("after"), Widget->bIsVariable);
        AddEffect(
            Context,
            Effect,
            FString::Printf(TEXT("%s %s.bIsVariable"), bReset ? TEXT("reset") : TEXT("set"), *Widget->GetName()));
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Context.Blueprint);
        return true;
    }
    FProperty* Property = FindFProperty<FProperty>(Container->GetClass(), *Path[0]);
    if ((!bReset && !CanWriteNativeWidgetField(Property, Container))
        || (bReset && !CanResetNativeWidgetField(Property, Container)))
    {
        OutError = bReset
            ? FString::Printf(TEXT("Widget field is not resettable: %s."), *Path[0])
            : FString::Printf(TEXT("Widget field is not writable: %s."), *Path[0]);
        return false;
    }
    const FString ImportText = !bReset ? ExprString(Statement->TryGetField(TEXT("value"))) : FString();
    if (!bReset && !ValidateNativePropertyImport(Property, Container, ImportText, OutError))
    {
        return false;
    }
    const TSharedPtr<FJsonValue> BeforeValue = PropertyValue(Property, Container);
    Container->Modify();
    Container->PreEditChange(Property);
    void* Destination = Property->ContainerPtrToValuePtr<void>(Container);
    if (bReset)
    {
        UObject* Defaults = Container->GetClass()->GetDefaultObject();
        const void* Source = Defaults != nullptr ? Property->ContainerPtrToValuePtr<void>(Defaults) : nullptr;
        if (Source == nullptr) return false;
        Property->CopyCompleteValue(Destination, Source);
    }
    else
    {
        if (!ImportPropertyValue(Property, Container, ImportText, OutError)) return false;
    }
    FPropertyChangedEvent ChangeEvent(Property, bReset ? EPropertyChangeType::ResetToDefault : EPropertyChangeType::ValueSet);
    Container->PostEditChangeProperty(ChangeEvent);
    Touch(Context, Widget);
    TSharedPtr<FJsonObject> Effect = MakeShared<FJsonObject>();
    Effect->SetStringField(TEXT("kind"), bReset ? TEXT("reset") : TEXT("set"));
    Effect->SetObjectField(TEXT("target"), PlanIdentity(Context, Widget));
    Effect->SetStringField(
        TEXT("field"),
        Container == Widget ? Path[0] : TEXT("Slot.") + Path[0]);
    Effect->SetField(TEXT("before"), BeforeValue);
    Effect->SetField(TEXT("after"), PropertyValue(Property, Container));
    AddEffect(
        Context,
        Effect,
        FString::Printf(TEXT("%s %s.%s"), bReset ? TEXT("reset") : TEXT("set"), *Widget->GetName(), *Path[0]));
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Context.Blueprint);
    return true;
}

bool ApplyAdd(FWidgetPatchContext& Context, const TSharedPtr<FJsonObject>& Statement, FString& OutError)
{
    const TSharedPtr<FJsonObject>* Target = nullptr;
    FString Kind;
    FString Alias;
    if (!Statement->TryGetObjectField(TEXT("target"), Target) || Target == nullptr || !ReadRef(*Target, Kind, Alias) || Kind != TEXT("local"))
    {
        OutError = TEXT("Widget add target must be a declared local binding.");
        return false;
    }
    UWidget* Widget = Materialize(Context, Alias, OutError);
    if (Widget == nullptr) return false;
    const TSharedPtr<FJsonObject>* To = nullptr;
    const TSharedPtr<FJsonObject>* Before = nullptr;
    const TSharedPtr<FJsonObject>* After = nullptr;
    Statement->TryGetObjectField(TEXT("to"), To);
    Statement->TryGetObjectField(TEXT("before"), Before);
    Statement->TryGetObjectField(TEXT("after"), After);
    const TSharedPtr<FJsonObject> BeforePlacement = PlanPlacement(Context, Widget);
    if (!PlaceWidget(Context, Widget, To != nullptr ? *To : nullptr, Before != nullptr ? *Before : nullptr, After != nullptr ? *After : nullptr, false, OutError)) return false;
    AddEffect(
        Context,
        StructuralEffect(TEXT("place"), Context, Widget, BeforePlacement, PlanPlacement(Context, Widget)),
        FString::Printf(TEXT("added %s"), *Alias));
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Context.Blueprint);
    return true;
}

bool ApplyMove(FWidgetPatchContext& Context, const TSharedPtr<FJsonObject>& Statement, FString& OutError)
{
    const TSharedPtr<FJsonObject>* Target = nullptr;
    if (!Statement->TryGetObjectField(TEXT("target"), Target) || Target == nullptr) return false;
    UWidget* Widget = ResolvePatchWidget(Context, *Target, OutError);
    if (Widget == nullptr || Widget == Context.Blueprint->WidgetTree->RootWidget)
    {
        OutError = TEXT("Widget root cannot be moved.");
        return false;
    }
    const TSharedPtr<FJsonObject>* To = nullptr;
    const TSharedPtr<FJsonObject>* Before = nullptr;
    const TSharedPtr<FJsonObject>* After = nullptr;
    Statement->TryGetObjectField(TEXT("to"), To);
    Statement->TryGetObjectField(TEXT("before"), Before);
    Statement->TryGetObjectField(TEXT("after"), After);
    const TSharedPtr<FJsonObject> BeforePlacement = PlanPlacement(Context, Widget);
    if (!PlaceWidget(Context, Widget, To != nullptr ? *To : nullptr, Before != nullptr ? *Before : nullptr, After != nullptr ? *After : nullptr, true, OutError)) return false;
    Touch(Context, Widget);
    AddEffect(
        Context,
        StructuralEffect(TEXT("move"), Context, Widget, BeforePlacement, PlanPlacement(Context, Widget)),
        FString::Printf(TEXT("moved %s"), *Widget->GetName()));
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Context.Blueprint);
    return true;
}

void CollectPlanSubtree(UWidget* Widget, TArray<UWidget*>& Out, TSet<UWidget*>& Seen)
{
    if (Widget == nullptr || Seen.Contains(Widget))
    {
        return;
    }
    Seen.Add(Widget);
    Out.Add(Widget);
    for (UWidget* Child : DirectChildren(Widget))
    {
        CollectPlanSubtree(Child, Out, Seen);
    }
}

TArray<UWidget*> PlanSubtree(UWidget* Widget)
{
    TArray<UWidget*> Result;
    TSet<UWidget*> Seen;
    CollectPlanSubtree(Widget, Result, Seen);
    return Result;
}

TSharedPtr<FJsonObject> RemoveCascadePlan(
    const FWidgetPatchContext& Context,
    UWidget* Target,
    const TArray<UWidget*>& Subtree)
{
    TSharedPtr<FJsonObject> Cascade = MakeShared<FJsonObject>();

    TArray<TSharedPtr<FJsonValue>> GuidRecords;
    for (UWidget* Widget : Subtree)
    {
        GuidRecords.Add(MakeShared<FJsonValueObject>(PlanIdentity(Context, Widget)));
    }
    Cascade->SetArrayField(TEXT("widgetGuidRecords"), GuidRecords);

    TArray<TSharedPtr<FJsonValue>> DelegateBindings;
    for (const FDelegateEditorBinding& Binding : Context.Blueprint->Bindings)
    {
        if (Binding.ObjectName != Target->GetName())
        {
            continue;
        }
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetObjectField(TEXT("widget"), PlanIdentity(Context, Target));
        Entry->SetStringField(TEXT("property"), Binding.PropertyName.ToString());
        if (!Binding.FunctionName.IsNone()) Entry->SetStringField(TEXT("function"), Binding.FunctionName.ToString());
        if (!Binding.SourceProperty.IsNone()) Entry->SetStringField(TEXT("sourceProperty"), Binding.SourceProperty.ToString());
        DelegateBindings.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Cascade->SetArrayField(TEXT("delegateBindings"), DelegateBindings);

    TArray<TSharedPtr<FJsonValue>> VariableNodes;
    TArray<UEdGraph*> Graphs;
    Context.Blueprint->GetAllGraphs(Graphs);
    UClass* SelfClass = Context.Blueprint->GeneratedClass;
    for (UWidget* Widget : Subtree)
    {
        for (UEdGraph* Graph : Graphs)
        {
            TArray<UK2Node_Variable*> Nodes;
            if (Graph != nullptr) Graph->GetNodesOfClass(Nodes);
            for (UK2Node_Variable* Node : Nodes)
            {
                if (Node == nullptr
                    || Node->GetVarName() != Widget->GetFName()
                    || Node->VariableReference.GetMemberParentClass(SelfClass) != SelfClass)
                {
                    continue;
                }
                TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
                Entry->SetObjectField(TEXT("widget"), PlanIdentity(Context, Widget));
                Entry->SetStringField(TEXT("graphName"), Graph->GetName());
                if (Graph->GraphGuid.IsValid()) Entry->SetStringField(TEXT("graphRef"), TEXT("graph@") + GuidText(Graph->GraphGuid));
                if (Node->NodeGuid.IsValid()) Entry->SetStringField(TEXT("nodeRef"), TEXT("node@") + GuidText(Node->NodeGuid));
                else Entry->SetStringField(TEXT("nodeName"), Node->GetName());
                VariableNodes.Add(MakeShared<FJsonValueObject>(Entry));
            }
        }
    }
    Cascade->SetArrayField(TEXT("graphVariableNodes"), VariableNodes);

    const UUserWidget* WidgetCDO = Context.Blueprint->GeneratedClass != nullptr
        ? Context.Blueprint->GeneratedClass->GetDefaultObject<UUserWidget>()
        : nullptr;
    if (WidgetCDO != nullptr && WidgetCDO->GetDesiredFocusWidgetName() == Target->GetFName())
    {
        Cascade->SetObjectField(TEXT("desiredFocus"), PlanIdentity(Context, Target));
    }
    return Cascade;
}

bool ApplyRemove(FWidgetPatchContext& Context, const TSharedPtr<FJsonObject>& Statement, FString& OutError)
{
    const TSharedPtr<FJsonObject>* Target = nullptr;
    if (!Statement->TryGetObjectField(TEXT("target"), Target) || Target == nullptr) return false;
    UWidget* Widget = ResolvePatchWidget(Context, *Target, OutError);
    if (Widget == nullptr) return false;
    const FString Name = Widget->GetName();
    const TArray<UWidget*> Subtree = PlanSubtree(Widget);
    TArray<TSharedPtr<FJsonValue>> BeforeSubtree;
    for (UWidget* Item : Subtree)
    {
        BeforeSubtree.Add(MakeShared<FJsonValueObject>(PlanWidgetState(Context, Item)));
    }
    const TSharedPtr<FJsonObject> Cascade = RemoveCascadePlan(Context, Widget, Subtree);
    FWidgetBlueprintEditorUtils::DeleteWidgets(
        Context.Blueprint,
        {Widget},
        FWidgetBlueprintEditorUtils::EDeleteWidgetWarningType::DeleteSilently);
    const TArray<UWidget*> Remaining = SourceWidgets(Context.Blueprint);
    for (UWidget* Removed : Subtree)
    {
        if (Remaining.Contains(Removed))
        {
            OutError = FString::Printf(TEXT("UE did not remove Widget subtree member %s."), *Removed->GetName());
            return false;
        }
    }
    TSharedPtr<FJsonObject> BeforeState = MakeShared<FJsonObject>();
    BeforeState->SetArrayField(TEXT("subtree"), BeforeSubtree);
    TSharedPtr<FJsonObject> AfterState = MakeShared<FJsonObject>();
    AfterState->SetBoolField(TEXT("deleted"), true);
    TSharedPtr<FJsonObject> Effect = StructuralEffect(TEXT("delete"), Context, Widget, BeforeState, AfterState);
    Effect->SetObjectField(TEXT("cascade"), Cascade);
    AddEffect(Context, Effect, FString::Printf(TEXT("removed %s"), *Name));
    return true;
}

bool ApplyWrap(FWidgetPatchContext& Context, const TSharedPtr<FJsonObject>& Statement, FString& OutError)
{
    const TSharedPtr<FJsonObject>* With = nullptr;
    FString Kind;
    FString Alias;
    if (!Statement->TryGetObjectField(TEXT("with"), With) || With == nullptr || !ReadRef(*With, Kind, Alias) || Kind != TEXT("local"))
    {
        OutError = TEXT("Widget wrap requires a declared local wrapper.");
        return false;
    }
    UPanelWidget* Wrapper = Cast<UPanelWidget>(Materialize(Context, Alias, OutError));
    if (Wrapper == nullptr)
    {
        OutError = TEXT("Widget wrapper template must create a UPanelWidget.");
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* TargetValues = nullptr;
    if (!Statement->TryGetArrayField(TEXT("targets"), TargetValues) || TargetValues == nullptr || TargetValues->IsEmpty()) return false;
    TArray<UWidget*> Targets;
    for (const TSharedPtr<FJsonValue>& Value : *TargetValues)
    {
        const TSharedPtr<FJsonObject>* Ref = nullptr;
        UWidget* Widget = Value->TryGetObject(Ref) && Ref != nullptr ? ResolvePatchWidget(Context, *Ref, OutError) : nullptr;
        if (Widget == nullptr || Targets.Contains(Widget))
        {
            OutError = TEXT("Widget wrap targets must be unique existing Widgets.");
            return false;
        }
        Targets.Add(Widget);
    }
    if (!Wrapper->CanHaveMultipleChildren() && Targets.Num() > 1)
    {
        OutError = TEXT("Wrapper cannot contain every target.");
        return false;
    }
    TArray<TSharedPtr<FJsonValue>> BeforeTargets;
    for (UWidget* Widget : Targets)
    {
        BeforeTargets.Add(MakeShared<FJsonValueObject>(PlanWidgetState(Context, Widget)));
    }
    const TSharedPtr<FJsonObject> BeforeExternalPlacement = PlanPlacement(Context, Targets[0]);
    UWidget* First = Targets[0];
    UPanelWidget* Parent = First->GetParent();
    if (Targets.Num() > 1)
    {
        for (UWidget* Widget : Targets)
        {
            if (Widget->GetParent() != Parent || Parent == nullptr || IsDescendant(Widget, First) || IsDescendant(First, Widget))
            {
                OutError = TEXT("Multiple wrap targets must be direct siblings in one Panel.");
                return false;
            }
        }
    }
    if (Parent != nullptr)
    {
        Parent->Modify();
        First->Modify();
        Wrapper->Modify();
        if (!Parent->ReplaceChild(First, Wrapper)) return false;
        for (int32 Index = 1; Index < Targets.Num(); ++Index)
        {
            Targets[Index]->Modify();
            Parent->RemoveChild(Targets[Index]);
        }
    }
    else if (First == Context.Blueprint->WidgetTree->RootWidget)
    {
        if (Targets.Num() != 1) return false;
        Context.Blueprint->WidgetTree->Modify();
        First->Modify();
        Wrapper->Modify();
        Context.Blueprint->WidgetTree->RootWidget = Wrapper;
    }
    else if (TScriptInterface<INamedSlotInterface> Host = FWidgetBlueprintEditorUtils::FindNamedSlotHostForContent(First, Context.Blueprint->WidgetTree))
    {
        if (UObject* HostObject = Host.GetObject()) HostObject->Modify();
        First->Modify();
        Wrapper->Modify();
        if (Targets.Num() != 1 || !FWidgetBlueprintEditorUtils::ReplaceNamedSlotHostContent(First, Host, Wrapper)) return false;
    }
    else if (const FName* Slot = Context.Blueprint->WidgetTree->NamedSlotBindings.FindKey(First))
    {
        if (Targets.Num() != 1) return false;
        Context.Blueprint->WidgetTree->Modify();
        First->Modify();
        Wrapper->Modify();
        Context.Blueprint->WidgetTree->SetContentForSlot(*Slot, Wrapper);
    }
    else
    {
        OutError = TEXT("Detached Widget cannot be wrapped.");
        return false;
    }
    for (UWidget* Widget : Targets)
    {
        Wrapper->Modify();
        Widget->Modify();
        if (!Wrapper->CanAddMoreChildren() || Wrapper->AddChild(Widget) == nullptr)
        {
            OutError = TEXT("Wrapper rejected a target Widget.");
            return false;
        }
        Touch(Context, Widget);
    }
    Touch(Context, Wrapper);
    TArray<TSharedPtr<FJsonValue>> AfterTargets;
    for (UWidget* Widget : Targets)
    {
        AfterTargets.Add(MakeShared<FJsonValueObject>(PlanWidgetState(Context, Widget)));
    }
    TSharedPtr<FJsonObject> BeforeState = MakeShared<FJsonObject>();
    BeforeState->SetArrayField(TEXT("targets"), BeforeTargets);
    TSharedPtr<FJsonObject> AfterState = MakeShared<FJsonObject>();
    AfterState->SetObjectField(TEXT("wrapper"), PlanWidgetState(Context, Wrapper));
    AfterState->SetArrayField(TEXT("targets"), AfterTargets);
    TSharedPtr<FJsonObject> SlotTransfer = MakeShared<FJsonObject>();
    SlotTransfer->SetObjectField(TEXT("from"), PlanIdentity(Context, First));
    SlotTransfer->SetObjectField(TEXT("to"), PlanIdentity(Context, Wrapper));
    SlotTransfer->SetObjectField(TEXT("before"), BeforeExternalPlacement);
    SlotTransfer->SetObjectField(TEXT("after"), PlanPlacement(Context, Wrapper));
    AfterState->SetObjectField(TEXT("externalPlacementTransfer"), SlotTransfer);
    AddEffect(
        Context,
        StructuralEffect(TEXT("wrap"), Context, Wrapper, BeforeState, AfterState),
        FString::Printf(TEXT("wrapped %d Widget(s) with %s"), Targets.Num(), *Alias));
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Context.Blueprint);
    return true;
}

bool PromoteExistingReplacement(FWidgetPatchContext& Context, UWidget* Target, UWidget* Replacement, FString& OutError)
{
    const UPanelWidget* TargetPanel = Cast<UPanelWidget>(Target);
    const bool bOnlyPanelChild = TargetPanel != nullptr
        && TargetPanel->GetChildrenCount() == 1
        && TargetPanel->GetChildAt(0) == Replacement;
    bool bDirectNamedSlotContent = false;
    if (INamedSlotInterface* NamedSlots = Cast<INamedSlotInterface>(Target))
    {
        TArray<FName> SlotNames;
        NamedSlots->GetSlotNames(SlotNames);
        for (const FName SlotName : SlotNames)
        {
            if (NamedSlots->GetContentForSlot(SlotName) == Replacement)
            {
                bDirectNamedSlotContent = true;
                break;
            }
        }
    }
    const bool bValidContent = bOnlyPanelChild || bDirectNamedSlotContent;
    if (!bValidContent)
    {
        OutError = TEXT("Existing replacement must be the target's only direct child or direct Named Slot content.");
        return false;
    }
    if (!DetachWidget(Context.Blueprint, Replacement, OutError))
    {
        return false;
    }
    UPanelWidget* Parent = Target->GetParent();
    Target->Modify();
    Replacement->Modify();
    if (Parent != nullptr)
    {
        Parent->Modify();
        if (!Parent->ReplaceChild(Target, Replacement)) return false;
    }
    else if (Target == Context.Blueprint->WidgetTree->RootWidget)
    {
        Context.Blueprint->WidgetTree->Modify();
        Context.Blueprint->WidgetTree->RootWidget = Replacement;
    }
    else if (TScriptInterface<INamedSlotInterface> Host = FWidgetBlueprintEditorUtils::FindNamedSlotHostForContent(Target, Context.Blueprint->WidgetTree))
    {
        if (UObject* HostObject = Host.GetObject()) HostObject->Modify();
        if (!FWidgetBlueprintEditorUtils::ReplaceNamedSlotHostContent(Target, Host, Replacement)) return false;
    }
    else if (const FName* Slot = Context.Blueprint->WidgetTree->NamedSlotBindings.FindKey(Target))
    {
        Context.Blueprint->WidgetTree->Modify();
        Context.Blueprint->WidgetTree->SetContentForSlot(*Slot, Replacement);
    }
    else return false;
    FWidgetBlueprintEditorUtils::DeleteWidgets(Context.Blueprint, {Target}, FWidgetBlueprintEditorUtils::EDeleteWidgetWarningType::DeleteSilently);
    Touch(Context, Replacement);
    return true;
}

bool ApplyReplace(FWidgetPatchContext& Context, const TSharedPtr<FJsonObject>& Statement, FString& OutError)
{
    const TSharedPtr<FJsonObject>* TargetRef = nullptr;
    const TSharedPtr<FJsonObject>* WithRef = nullptr;
    if (!Statement->TryGetObjectField(TEXT("target"), TargetRef) || TargetRef == nullptr
        || !Statement->TryGetObjectField(TEXT("with"), WithRef) || WithRef == nullptr) return false;
    UWidget* Target = ResolvePatchWidget(Context, *TargetRef, OutError);
    if (Target == nullptr) return false;
    const TArray<UWidget*> OriginalSubtree = PlanSubtree(Target);
    TArray<TSharedPtr<FJsonValue>> BeforeSubtree;
    for (UWidget* Item : OriginalSubtree)
    {
        BeforeSubtree.Add(MakeShared<FJsonValueObject>(PlanWidgetState(Context, Item)));
    }
    const TSharedPtr<FJsonObject> TargetIdentity = PlanIdentity(Context, Target);
    const TSharedPtr<FJsonObject> TargetPlacement = PlanPlacement(Context, Target);
    FString WithKind;
    FString WithIdentity;
    if (!ReadRef(*WithRef, WithKind, WithIdentity)) return false;
    if (WithKind != TEXT("local"))
    {
        UWidget* Existing = ResolvePatchWidget(Context, *WithRef, OutError);
        const FString ExistingName = Existing != nullptr ? Existing->GetName() : FString();
        const FString TargetName = Target->GetName();
        const TSharedPtr<FJsonObject> ExistingBefore = Existing != nullptr ? PlanWidgetState(Context, Existing) : nullptr;
        TSet<UWidget*> Preserved;
        if (Existing != nullptr) Preserved.Append(PlanSubtree(Existing));
        TArray<UWidget*> DiscardWidgets;
        for (UWidget* Item : OriginalSubtree)
        {
            if (!Preserved.Contains(Item)) DiscardWidgets.Add(Item);
        }
        const TSharedPtr<FJsonObject> Cascade = Existing != nullptr
            ? RemoveCascadePlan(Context, Target, DiscardWidgets)
            : nullptr;
        if (Existing == nullptr || !PromoteExistingReplacement(Context, Target, Existing, OutError)) return false;
        TArray<TSharedPtr<FJsonValue>> Discarded;
        for (UWidget* Item : DiscardWidgets)
        {
            Discarded.Add(MakeShared<FJsonValueObject>(PlanIdentity(Context, Item)));
        }
        TSharedPtr<FJsonObject> BeforeState = MakeShared<FJsonObject>();
        BeforeState->SetArrayField(TEXT("subtree"), BeforeSubtree);
        if (ExistingBefore.IsValid()) BeforeState->SetObjectField(TEXT("promoted"), ExistingBefore);
        TSharedPtr<FJsonObject> AfterState = MakeShared<FJsonObject>();
        AfterState->SetObjectField(TEXT("replacement"), PlanWidgetState(Context, Existing));
        AfterState->SetArrayField(TEXT("discarded"), Discarded);
        TSharedPtr<FJsonObject> PlacementTransfer = MakeShared<FJsonObject>();
        PlacementTransfer->SetObjectField(TEXT("from"), TargetIdentity);
        PlacementTransfer->SetObjectField(TEXT("to"), PlanIdentity(Context, Existing));
        PlacementTransfer->SetObjectField(TEXT("before"), TargetPlacement);
        PlacementTransfer->SetObjectField(TEXT("after"), PlanPlacement(Context, Existing));
        AfterState->SetObjectField(TEXT("externalPlacementTransfer"), PlacementTransfer);
        TSharedPtr<FJsonObject> Effect = StructuralEffect(TEXT("promote"), Context, Existing, BeforeState, AfterState);
        if (Cascade.IsValid()) Effect->SetObjectField(TEXT("cascade"), Cascade);
        AddEffect(
            Context,
            Effect,
            FString::Printf(TEXT("promoted %s over %s"), *ExistingName, *TargetName));
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Context.Blueprint);
        return true;
    }
    UWidget* Replacement = Materialize(Context, WithIdentity, OutError);
    if (Replacement == nullptr) return false;
    UPanelWidget* OldPanel = Cast<UPanelWidget>(Target);
    UPanelWidget* NewPanel = Cast<UPanelWidget>(Replacement);
    if (OldPanel != nullptr && OldPanel->GetChildrenCount() > 0 && NewPanel == nullptr)
    {
        OutError = TEXT("Replacement must be a Panel because the target has children.");
        return false;
    }
    TArray<TPair<FName, UWidget*>> NamedContent;
    if (INamedSlotInterface* OldNamed = Cast<INamedSlotInterface>(Target))
    {
        TArray<FName> OldNames;
        OldNamed->GetSlotNames(OldNames);
        INamedSlotInterface* NewNamed = Cast<INamedSlotInterface>(Replacement);
        TArray<FName> NewNames;
        if (NewNamed != nullptr) NewNamed->GetSlotNames(NewNames);
        for (const FName Name : OldNames)
        {
            if (UWidget* Content = OldNamed->GetContentForSlot(Name))
            {
                if (NewNamed == nullptr || !NewNames.Contains(Name) || NewNamed->GetContentForSlot(Name) != nullptr)
                {
                    OutError = FString::Printf(TEXT("Replacement cannot preserve Named Slot %s."), *Name.ToString());
                    return false;
                }
                NamedContent.Emplace(Name, Content);
            }
        }
    }
    TMap<FName, FString> Properties;
    FWidgetBlueprintEditorUtils::ExportPropertiesToText(Target, Properties);
    Target->Modify();
    Replacement->Modify();
    FWidgetBlueprintEditorUtils::ImportPropertiesFromText(Replacement, Properties);
    if (UPanelWidget* Parent = Target->GetParent())
    {
        Parent->Modify();
        if (!Parent->ReplaceChild(Target, Replacement)) return false;
    }
    else if (Target == Context.Blueprint->WidgetTree->RootWidget)
    {
        Context.Blueprint->WidgetTree->Modify();
        Context.Blueprint->WidgetTree->RootWidget = Replacement;
    }
    else if (TScriptInterface<INamedSlotInterface> Host = FWidgetBlueprintEditorUtils::FindNamedSlotHostForContent(Target, Context.Blueprint->WidgetTree))
    {
        if (UObject* HostObject = Host.GetObject()) HostObject->Modify();
        if (!FWidgetBlueprintEditorUtils::ReplaceNamedSlotHostContent(Target, Host, Replacement)) return false;
    }
    else if (const FName* Slot = Context.Blueprint->WidgetTree->NamedSlotBindings.FindKey(Target))
    {
        Context.Blueprint->WidgetTree->Modify();
        Context.Blueprint->WidgetTree->SetContentForSlot(*Slot, Replacement);
    }
    else
    {
        OutError = TEXT("Detached Widget cannot be replaced.");
        return false;
    }
    if (OldPanel != nullptr && NewPanel != nullptr)
    {
        while (OldPanel->GetChildrenCount() > 0)
        {
            UWidget* Child = OldPanel->GetChildAt(0);
            OldPanel->Modify();
            NewPanel->Modify();
            Child->Modify();
            OldPanel->RemoveChild(Child);
            if (!NewPanel->CanAddMoreChildren() || NewPanel->AddChild(Child) == nullptr)
            {
                OutError = TEXT("Replacement Panel cannot accept every existing child.");
                return false;
            }
        }
    }
    if (!NamedContent.IsEmpty())
    {
        INamedSlotInterface* OldNamed = Cast<INamedSlotInterface>(Target);
        INamedSlotInterface* NewNamed = Cast<INamedSlotInterface>(Replacement);
        for (const TPair<FName, UWidget*>& Pair : NamedContent)
        {
            Target->Modify();
            Replacement->Modify();
            Pair.Value->Modify();
            OldNamed->SetContentForSlot(Pair.Key, nullptr);
            NewNamed->SetContentForSlot(Pair.Key, Pair.Value);
        }
    }
    const FName OldName = Target->GetFName();
    const FName CreatedName = Replacement->GetFName();
    Context.Blueprint->OnVariableRemoved(CreatedName);
    if (!Target->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders))
    {
        OutError = TEXT("UE could not move the replaced Widget out of the WidgetTree.");
        return false;
    }
    if (!Replacement->Rename(*OldName.ToString(), Context.Blueprint->WidgetTree, REN_DontCreateRedirectors | REN_ForceNoResetLoaders))
    {
        OutError = TEXT("UE could not preserve the replaced Widget name.");
        return false;
    }
    FBlueprintEditorUtils::ReplaceVariableReferences(Context.Blueprint, OldName, OldName);
    SetLocal(Context, WithIdentity, Replacement);
    Touch(Context, Replacement);
    TArray<TSharedPtr<FJsonValue>> PreservedChildren;
    for (UWidget* Item : OriginalSubtree)
    {
        if (Item != Target) PreservedChildren.Add(MakeShared<FJsonValueObject>(PlanWidgetState(Context, Item)));
    }
    TSharedPtr<FJsonObject> BeforeState = MakeShared<FJsonObject>();
    BeforeState->SetArrayField(TEXT("subtree"), BeforeSubtree);
    TSharedPtr<FJsonObject> AfterState = MakeShared<FJsonObject>();
    AfterState->SetObjectField(TEXT("replacement"), PlanWidgetState(Context, Replacement));
    AfterState->SetArrayField(TEXT("preservedChildren"), PreservedChildren);
    AfterState->SetObjectField(TEXT("discarded"), TargetIdentity);
    AfterState->SetObjectField(TEXT("stableIdentity"), TargetIdentity);
    TSharedPtr<FJsonObject> PlacementTransfer = MakeShared<FJsonObject>();
    PlacementTransfer->SetObjectField(TEXT("from"), TargetIdentity);
    PlacementTransfer->SetObjectField(TEXT("to"), PlanIdentity(Context, Replacement));
    PlacementTransfer->SetObjectField(TEXT("before"), TargetPlacement);
    PlacementTransfer->SetObjectField(TEXT("after"), PlanPlacement(Context, Replacement));
    AfterState->SetObjectField(TEXT("externalPlacementTransfer"), PlacementTransfer);
    AddEffect(
        Context,
        StructuralEffect(TEXT("replace"), Context, Replacement, BeforeState, AfterState),
        FString::Printf(TEXT("replaced %s with %s"), *OldName.ToString(), *WithIdentity));
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Context.Blueprint);
    return true;
}

FString SanitizeWidgetObjectName(const FString& DisplayName)
{
    return SlugStringForValidName(DisplayName);
}

FWidgetBlueprintEditor* FindOpenWidgetBlueprintEditor(UWidgetBlueprint* Blueprint)
{
    if (Blueprint == nullptr || Blueprint->GetOutermost() == GetTransientPackage() || GEditor == nullptr)
    {
        return nullptr;
    }
    UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    return Editors != nullptr
        ? static_cast<FWidgetBlueprintEditor*>(Editors->FindEditorForAsset(Blueprint, false))
        : nullptr;
}

void RenameGeneratedWidgetGetterPins(UWidgetBlueprint* Blueprint, UWidget* Widget, const FName NewName)
{
#if UE_HAS_WIDGET_GENERATED_BY_CLASS
    if (Blueprint == nullptr || Widget == nullptr || !Widget->bIsVariable)
    {
        return;
    }
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (const UEdGraph* Graph : Graphs)
    {
        TArray<UK2Node_Variable*> Nodes;
        Graph->GetNodesOfClass(Nodes);
        for (UK2Node_Variable* Node : Nodes)
        {
            UClass* SelfClass = Blueprint->GeneratedClass;
            if (Node == nullptr
                || Node->VariableReference.GetMemberParentClass(SelfClass) != SelfClass
                || Node->GetVarName() != NewName)
            {
                continue;
            }
            UEdGraphPin* ValuePin = Node->GetValuePin();
            if (ValuePin == nullptr)
            {
                continue;
            }
            ValuePin->Modify();
            Node->Modify();
            UEdGraphPin* NewPin = Node->CreatePin(
                ValuePin->Direction,
                ValuePin->PinType.PinCategory,
                ValuePin->PinType.PinSubCategory,
                Widget->WidgetGeneratedByClass.Get(),
                NewName);
            if (NewPin != nullptr)
            {
                ValuePin->bOrphanedPin = true;
            }
        }
    }
#endif
}

bool ApplyRename(FWidgetPatchContext& Context, UWidget* Widget, const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString DisplayName;
    if (!Args.IsValid() || !Args->TryGetStringField(TEXT("displayName"), DisplayName) || DisplayName.TrimStartAndEnd().IsEmpty())
    {
        OutError = TEXT("Rename requires displayName.");
        return false;
    }
    const FName OldName = Widget->GetFName();
    const TSharedPtr<FJsonObject> StableIdentity = PlanIdentity(Context, Widget);
    const FString OldDisplayLabel = Widget->GetDisplayLabel();
    FString NewName = SanitizeWidgetObjectName(DisplayName);
    if (NewName.IsEmpty())
    {
        NewName = OldName.ToString();
    }
    const FName NewFName(*NewName);
    FKismetNameValidator NameValidator(Context.Blueprint, OldName);
    const FObjectPropertyBase* ExistingProperty = Context.Blueprint->ParentClass != nullptr
        ? CastField<FObjectPropertyBase>(Context.Blueprint->ParentClass->FindPropertyByName(NewFName))
        : nullptr;
    const bool bBindWidget = ExistingProperty != nullptr
        && FWidgetBlueprintEditorUtils::IsBindWidgetProperty(ExistingProperty)
        && Widget->IsA(ExistingProperty->PropertyClass);
    const EValidatorResult NameResult = NameValidator.IsValid(NewFName);
    if ((NameResult != EValidatorResult::Ok && !bBindWidget)
        || (NewFName != OldName
            && !Widget->Rename(*NewName, Context.Blueprint->WidgetTree, REN_Test | REN_DontCreateRedirectors | REN_ForceNoResetLoaders)))
    {
        OutError = NameResult != EValidatorResult::Ok && !bBindWidget
            ? INameValidatorInterface::GetErrorString(NewName, NameResult)
            : TEXT("Widget object name is unavailable.");
        return false;
    }

    Widget->Modify();
    Context.Blueprint->Modify();

    FWidgetBlueprintEditor* OpenEditor = FindOpenWidgetBlueprintEditor(Context.Blueprint);
    UWidget* PreviewWidget = OpenEditor != nullptr
        ? OpenEditor->GetReferenceFromTemplate(Widget).GetPreview()
        : nullptr;
    if (PreviewWidget != nullptr)
    {
        PreviewWidget->SetFlags(RF_Transactional);
        PreviewWidget->Modify();
        PreviewWidget->SetDisplayLabel(DisplayName);
        if (NewFName != OldName && !PreviewWidget->Rename(*NewName))
        {
            OutError = TEXT("UE could not rename the preview Widget.");
            return false;
        }
    }

    Context.Blueprint->OnVariableRenamed(OldName, NewFName);
    if (PreviewWidget == nullptr || PreviewWidget != Widget)
    {
        Widget->SetDisplayLabel(DisplayName);
        if (NewFName != OldName
            && !Widget->Rename(*NewName, Context.Blueprint->WidgetTree, REN_DontCreateRedirectors | REN_ForceNoResetLoaders))
        {
            OutError = TEXT("UE could not rename the Widget.");
            return false;
        }
    }

    RenameGeneratedWidgetGetterPins(Context.Blueprint, Widget, NewFName);
    FWidgetBlueprintEditorUtils::ReplaceDesiredFocus(Context.Blueprint, OldName, NewFName);

    const FString OldNameString = OldName.ToString();
    for (FDelegateEditorBinding& Binding : Context.Blueprint->Bindings)
    {
        if (Binding.ObjectName == OldNameString)
        {
            Binding.ObjectName = NewName;
        }
    }
    for (UWidgetAnimation* Animation : Context.Blueprint->Animations)
    {
        if (Animation == nullptr)
        {
            continue;
        }
        for (FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
        {
            if (Binding.WidgetName != OldName)
            {
                continue;
            }
            Animation->Modify();
            Binding.WidgetName = NewFName;
            if (Animation->MovieScene != nullptr)
            {
                Animation->MovieScene->Modify();
                if (Binding.SlotWidgetName == NAME_None)
                {
                    if (FMovieScenePossessable* Possessable = Animation->MovieScene->FindPossessable(Binding.AnimationGuid))
                    {
                        Possessable->SetName(NewName);
                    }
                }
                else
                {
                    break;
                }
            }
        }
    }
    Context.Blueprint->WidgetTree->ForEachWidget([OldName, NewFName](UWidget* SourceWidget)
    {
        if (SourceWidget != nullptr && SourceWidget->Navigation != nullptr)
        {
            SourceWidget->Navigation->SetFlags(RF_Transactional);
            SourceWidget->Navigation->Modify();
            SourceWidget->Navigation->TryToRenameBinding(OldName, NewFName);
        }
    });

    if (UUIComponentWidgetBlueprintExtension* Extension =
            UWidgetBlueprintExtension::GetExtension<UUIComponentWidgetBlueprintExtension>(Context.Blueprint))
    {
        Extension->RenameWidget(OldName, NewFName);
    }
    if (OpenEditor != nullptr)
    {
        if (UUserWidget* Preview = OpenEditor->GetPreview())
        {
            if (UUIComponentUserWidgetExtension* Extension = Preview->GetExtension<UUIComponentUserWidgetExtension>())
            {
                Extension->RenameWidget(OldName, NewFName);
            }
        }
    }

    FBlueprintEditorUtils::ValidateBlueprintChildVariables(Context.Blueprint, NewFName);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Context.Blueprint);
    FBlueprintEditorUtils::ReplaceVariableReferences(Context.Blueprint, OldName, NewFName);
    Touch(Context, Widget);
    TSharedPtr<FJsonObject> Effect = MakeShared<FJsonObject>();
    Effect->SetStringField(TEXT("kind"), TEXT("rename"));
    Effect->SetObjectField(TEXT("target"), StableIdentity);
    TSharedPtr<FJsonObject> BeforeState = MakeShared<FJsonObject>();
    BeforeState->SetStringField(TEXT("name"), OldName.ToString());
    BeforeState->SetStringField(TEXT("displayLabel"), OldDisplayLabel);
    TSharedPtr<FJsonObject> AfterState = MakeShared<FJsonObject>();
    AfterState->SetStringField(TEXT("name"), NewName);
    AfterState->SetStringField(TEXT("displayLabel"), Widget->GetDisplayLabel());
    Effect->SetObjectField(TEXT("before"), BeforeState);
    Effect->SetObjectField(TEXT("after"), AfterState);
    TArray<TSharedPtr<FJsonValue>> Cascades;
    for (const TCHAR* Cascade : {
        TEXT("widgetGuidRecord"),
        TEXT("graphVariableReferences"),
        TEXT("delegateBindings"),
        TEXT("desiredFocus"),
        TEXT("animationBindings"),
        TEXT("navigationBindings"),
        TEXT("uiComponentExtensions")})
    {
        Cascades.Add(Value::String(Cascade));
    }
    Effect->SetArrayField(TEXT("referenceCascades"), Cascades);
    AddEffect(Context, Effect, FString::Printf(TEXT("renamed %s to %s"), *OldName.ToString(), *NewName));
    return true;
}

bool ApplyDuplicate(
    FWidgetPatchContext& Context,
    UWidget* Widget,
    const TArray<TSharedPtr<FJsonValue>>& Outputs,
    FString& OutError)
{
    const TSharedPtr<FJsonObject> SourceState = PlanWidgetState(Context, Widget);
    UPanelWidget* Parent = Widget->GetParent();
    if (Parent == nullptr || !Parent->CanHaveMultipleChildren() || !Parent->CanAddMoreChildren() || Outputs.Num() != 1)
    {
        OutError = TEXT("Duplicate requires one output and a source with a multi-child Panel parent.");
        return false;
    }
    FString OutputAlias;
    const TSharedPtr<FJsonObject>* Output = nullptr;
    if (!Outputs[0]->TryGetObject(Output) || Output == nullptr || !(*Output)->TryGetStringField(TEXT("alias"), OutputAlias) || OutputAlias.IsEmpty())
    {
        OutError = TEXT("Duplicate output alias is invalid.");
        return false;
    }
    FString Exported;
    TMap<FName, FString> SourceSlotProperties;
    if (Widget->Slot != nullptr)
    {
        FWidgetBlueprintEditorUtils::ExportPropertiesToText(Widget->Slot, SourceSlotProperties);
    }
    FWidgetBlueprintEditorUtils::ExportWidgetsToText({Widget}, Exported);
    TSet<UWidget*> Imported;
    TMap<FName, UWidgetSlotPair*> SlotData;
    FWidgetBlueprintEditorUtils::ImportWidgetsFromText(Context.Blueprint, Exported, Imported, SlotData);
    UWidget* Root = nullptr;
    for (UWidget* Candidate : Imported)
    {
        Context.Blueprint->OnVariableAdded(Candidate->GetFName());
        Touch(Context, Candidate);
        if (Candidate->GetParent() == nullptr)
        {
            bool bNamedChild = false;
            for (UWidget* Host : Imported)
            {
                if (INamedSlotInterface* Named = Cast<INamedSlotInterface>(Host); Named != nullptr && Named->ContainsContent(Candidate)) bNamedChild = true;
            }
            if (!bNamedChild)
            {
                if (Root != nullptr)
                {
                    OutError = TEXT("Duplicate serialization produced multiple roots.");
                    return false;
                }
                Root = Candidate;
            }
        }
    }
    if (Root == nullptr)
    {
        OutError = TEXT("Duplicate serialization produced no root Widget.");
        return false;
    }
    const int32 Index = Parent->GetChildIndex(Widget) + 1;
    Parent->Modify();
    Widget->Modify();
    Root->Modify();
    UPanelSlot* NewSlot = Parent->InsertChildAt(Index, Root);
    if (NewSlot == nullptr) return false;
    FWidgetBlueprintEditorUtils::ImportPropertiesFromText(NewSlot, SourceSlotProperties);
    SetLocal(Context, OutputAlias, Root);
    TArray<UWidget*> ImportedWidgets;
    for (UWidget* ImportedWidget : Imported) ImportedWidgets.Add(ImportedWidget);
    RegisterCreatedPlanIdentities(Context, Root, OutputAlias, ImportedWidgets);
    Touch(Context, Root);
    TSharedPtr<FJsonObject> BeforeState = MakeShared<FJsonObject>();
    BeforeState->SetObjectField(TEXT("source"), SourceState);
    TSharedPtr<FJsonObject> AfterState = MakeShared<FJsonObject>();
    AfterState->SetObjectField(TEXT("copy"), PlanWidgetState(Context, Root));
    AddEffect(
        Context,
        StructuralEffect(TEXT("duplicate"), Context, Root, BeforeState, AfterState),
        FString::Printf(TEXT("duplicated %s as %s"), *Widget->GetName(), *OutputAlias));
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Context.Blueprint);
    return true;
}

bool ApplyInvoke(FWidgetPatchContext& Context, const TSharedPtr<FJsonObject>& Statement, FString& OutError)
{
    const TSharedPtr<FJsonObject>* Target = nullptr;
    const TSharedPtr<FJsonObject>* Args = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
    FString Operation;
    if (!Statement->TryGetObjectField(TEXT("target"), Target) || Target == nullptr
        || !Statement->TryGetStringField(TEXT("operation"), Operation)
        || !Statement->TryGetObjectField(TEXT("args"), Args) || Args == nullptr
        || !Statement->TryGetArrayField(TEXT("outputs"), Outputs) || Outputs == nullptr) return false;
    UWidget* Widget = ResolvePatchWidget(Context, *Target, OutError);
    if (Widget == nullptr) return false;
    if (Operation == TEXT("Rename")) return ApplyRename(Context, Widget, *Args, OutError);
    if (Operation == TEXT("Duplicate")) return ApplyDuplicate(Context, Widget, *Outputs, OutError);
    OutError = FString::Printf(TEXT("Widget operation is unavailable: %s."), *Operation);
    return false;
}

bool ParseDeclaration(
    const TSharedPtr<FJsonObject>& Statement,
    FWidgetPatchContext& Context,
    FString& OutError)
{
    const TSharedPtr<FJsonObject>* Target = nullptr;
    const TSharedPtr<FJsonObject>* Call = nullptr;
    const TSharedPtr<FJsonObject>* Args = nullptr;
    FString TargetKind;
    FString Alias;
    FString ValueKind;
    FString Callee;
    FString Palette;
    if (!Statement.IsValid()
        || !Statement->TryGetObjectField(TEXT("target"), Target) || Target == nullptr
        || !ReadRef(*Target, TargetKind, Alias) || TargetKind != TEXT("local")
        || !Statement->TryGetObjectField(TEXT("value"), Call) || Call == nullptr
        || !(*Call)->TryGetStringField(TEXT("kind"), ValueKind) || ValueKind != TEXT("call")
        || !(*Call)->TryGetStringField(TEXT("callee"), Callee) || Callee != TEXT("widget")
        || !(*Call)->TryGetObjectField(TEXT("args"), Args) || Args == nullptr
        || !(*Args)->TryGetStringField(TEXT("palette"), Palette) || Palette.IsEmpty())
    {
        OutError = TEXT("Widget Patch bindings must be local widget(palette: ...) declarations.");
        return false;
    }
    if (Alias == Context.TargetAlias || Context.Declarations.Contains(Alias) || Context.Locals.Contains(Alias))
    {
        OutError = FString::Printf(TEXT("Widget binding alias is duplicated: %s."), *Alias);
        return false;
    }
    Context.Declarations.Add(Alias, Palette);
    return true;
}

bool ApplyWidgetPatch(const FSalPatch& Patch, FWidgetPatchContext& Context, FString& OutError)
{
    Context.Blueprint->Modify();
    Context.Blueprint->WidgetTree->Modify();
    for (const TSharedPtr<FJsonValue>& Value : Patch.Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        Value->TryGetObject(Statement);
        FString Kind;
        (*Statement)->TryGetStringField(TEXT("kind"), Kind);
        if (Kind.IsEmpty())
        {
            if (!ParseDeclaration(*Statement, Context, OutError)) return false;
            continue;
        }
        bool bOk = false;
        if (Kind == TEXT("add")) bOk = ApplyAdd(Context, *Statement, OutError);
        else if (Kind == TEXT("remove")) bOk = ApplyRemove(Context, *Statement, OutError);
        else if (Kind == TEXT("move")) bOk = ApplyMove(Context, *Statement, OutError);
        else if (Kind == TEXT("set")) bOk = ApplyField(Context, *Statement, false, OutError);
        else if (Kind == TEXT("reset")) bOk = ApplyField(Context, *Statement, true, OutError);
        else if (Kind == TEXT("wrap")) bOk = ApplyWrap(Context, *Statement, OutError);
        else if (Kind == TEXT("replace")) bOk = ApplyReplace(Context, *Statement, OutError);
        else if (Kind == TEXT("invoke")) bOk = ApplyInvoke(Context, *Statement, OutError);
        else
        {
            OutError = FString::Printf(TEXT("Widget Patch operation is unsupported: %s."), *Kind);
            return false;
        }
        if (!bOk)
        {
            if (OutError.IsEmpty()) OutError = FString::Printf(TEXT("Widget operation failed: %s."), *Kind);
            return false;
        }
    }
    for (const TPair<FString, FString>& Declaration : Context.Declarations)
    {
        if (!Context.ConsumedDeclarations.Contains(Declaration.Key))
        {
            Context.ErrorCode = TEXT("validation.unused_binding");
            OutError = FString::Printf(
                TEXT("Widget creation binding %s must be consumed exactly once by add, wrap, or replace."),
                *Declaration.Key);
            return false;
        }
    }
    return true;
}

enum class EWidgetPatchRefOwner : uint8
{
    Widget,
    TargetBlueprint,
    Other,
    Unknown
};

EWidgetPatchRefOwner ClassifyWidgetPatchRef(
    const TSharedPtr<FJsonObject>& Ref,
    const FString& TargetAlias,
    const TSet<FString>& WidgetAliases)
{
    FString Kind;
    FString Identity;
    if (ReadRef(Ref, Kind, Identity))
    {
        if (Kind == TEXT("widget"))
        {
            return EWidgetPatchRefOwner::Widget;
        }
        if (Kind == TEXT("local"))
        {
            if (Identity == TargetAlias)
            {
                return EWidgetPatchRefOwner::TargetBlueprint;
            }
            return WidgetAliases.Contains(Identity)
                ? EWidgetPatchRefOwner::Widget
                : EWidgetPatchRefOwner::Unknown;
        }
        return EWidgetPatchRefOwner::Other;
    }
    TSharedPtr<FJsonObject> Owner;
    TArray<FString> Path;
    return ReadMember(Ref, Owner, Path)
        ? ClassifyWidgetPatchRef(Owner, TargetAlias, WidgetAliases)
        : EWidgetPatchRefOwner::Unknown;
}

bool IsExplicitOtherWidgetPatchRef(
    const TSharedPtr<FJsonObject>& Ref,
    const FString& TargetAlias,
    const TSet<FString>& WidgetAliases)
{
    const EWidgetPatchRefOwner Owner = ClassifyWidgetPatchRef(Ref, TargetAlias, WidgetAliases);
    return Owner == EWidgetPatchRefOwner::Other || Owner == EWidgetPatchRefOwner::TargetBlueprint;
}

bool IsExplicitOtherWidgetDestination(
    const TSharedPtr<FJsonObject>& Ref,
    const FString& TargetAlias,
    const TSet<FString>& WidgetAliases)
{
    TSharedPtr<FJsonObject> Owner;
    TArray<FString> Path;
    if (ReadMember(Ref, Owner, Path))
    {
        const EWidgetPatchRefOwner OwnerKind = ClassifyWidgetPatchRef(Owner, TargetAlias, WidgetAliases);
        if ((OwnerKind == EWidgetPatchRefOwner::Widget || OwnerKind == EWidgetPatchRefOwner::TargetBlueprint)
            && Path.Num() == 2
            && Path[0] == TEXT("NamedSlots"))
        {
            return false;
        }
        return OwnerKind == EWidgetPatchRefOwner::Other || OwnerKind == EWidgetPatchRefOwner::TargetBlueprint;
    }
    return IsExplicitOtherWidgetPatchRef(Ref, TargetAlias, WidgetAliases);
}

bool PatchMixesNonWidgetInterface(const FSalPatch& Patch)
{
    TSet<FString> WidgetAliases;
    for (const TSharedPtr<FJsonValue>& Value : Patch.Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(Statement) || Statement == nullptr)
        {
            continue;
        }
        FString Kind;
        (*Statement)->TryGetStringField(TEXT("kind"), Kind);
        if (Kind.IsEmpty())
        {
            const TSharedPtr<FJsonObject>* Target = nullptr;
            const TSharedPtr<FJsonObject>* Call = nullptr;
            const TSharedPtr<FJsonObject>* Args = nullptr;
            FString RefKind;
            FString Alias;
            FString ValueKind;
            FString Callee;
            FString Palette;
            const bool bWidgetBinding = (*Statement)->TryGetObjectField(TEXT("target"), Target)
                && Target != nullptr
                && ReadRef(*Target, RefKind, Alias)
                && RefKind == TEXT("local")
                && (*Statement)->TryGetObjectField(TEXT("value"), Call)
                && Call != nullptr
                && (*Call)->TryGetStringField(TEXT("kind"), ValueKind)
                && ValueKind == TEXT("call")
                && (*Call)->TryGetStringField(TEXT("callee"), Callee)
                && Callee == TEXT("widget")
                && (*Call)->TryGetObjectField(TEXT("args"), Args)
                && Args != nullptr
                && (*Args)->TryGetStringField(TEXT("palette"), Palette)
                && !Palette.IsEmpty();
            if (!bWidgetBinding)
            {
                return true;
            }
            WidgetAliases.Add(Alias);
            continue;
        }
        if (Kind == TEXT("compile") || Kind == TEXT("save"))
        {
            continue;
        }

        auto HasOtherField = [&](const TCHAR* Field, const bool bDestination = false)
        {
            const TSharedPtr<FJsonObject>* Ref = nullptr;
            if (!(*Statement)->TryGetObjectField(Field, Ref) || Ref == nullptr)
            {
                return false;
            }
            return bDestination
                ? IsExplicitOtherWidgetDestination(*Ref, Patch.Alias, WidgetAliases)
                : IsExplicitOtherWidgetPatchRef(*Ref, Patch.Alias, WidgetAliases);
        };

        if (Kind == TEXT("add"))
        {
            if (HasOtherField(TEXT("target"))
                || HasOtherField(TEXT("to"), true)
                || HasOtherField(TEXT("before"))
                || HasOtherField(TEXT("after"))) return true;
        }
        else if (Kind == TEXT("move"))
        {
            if (HasOtherField(TEXT("target"))
                || HasOtherField(TEXT("to"), true)
                || HasOtherField(TEXT("before"))
                || HasOtherField(TEXT("after"))) return true;
        }
        else if (Kind == TEXT("set") || Kind == TEXT("reset") || Kind == TEXT("remove"))
        {
            if (HasOtherField(TEXT("target"))) return true;
        }
        else if (Kind == TEXT("replace"))
        {
            if (HasOtherField(TEXT("target")) || HasOtherField(TEXT("with"))) return true;
        }
        else if (Kind == TEXT("wrap"))
        {
            if (HasOtherField(TEXT("with"))) return true;
            const TArray<TSharedPtr<FJsonValue>>* Targets = nullptr;
            if ((*Statement)->TryGetArrayField(TEXT("targets"), Targets) && Targets != nullptr)
            {
                for (const TSharedPtr<FJsonValue>& TargetValue : *Targets)
                {
                    const TSharedPtr<FJsonObject>* Ref = nullptr;
                    if (TargetValue.IsValid()
                        && TargetValue->TryGetObject(Ref)
                        && Ref != nullptr
                        && IsExplicitOtherWidgetPatchRef(*Ref, Patch.Alias, WidgetAliases)) return true;
                }
            }
        }
        else if (Kind == TEXT("invoke"))
        {
            if (HasOtherField(TEXT("target"))) return true;
            const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
            if ((*Statement)->TryGetArrayField(TEXT("outputs"), Outputs) && Outputs != nullptr)
            {
                for (const TSharedPtr<FJsonValue>& OutputValue : *Outputs)
                {
                    const TSharedPtr<FJsonObject>* Output = nullptr;
                    FString Alias;
                    if (OutputValue.IsValid()
                        && OutputValue->TryGetObject(Output)
                        && Output != nullptr
                        && (*Output)->TryGetStringField(TEXT("alias"), Alias)
                        && !Alias.IsEmpty())
                    {
                        WidgetAliases.Add(Alias);
                    }
                }
            }
        }
        else
        {
            return true;
        }
    }
    return false;
}

TSharedPtr<FJsonObject> PatchObject(UWidgetBlueprint* Blueprint, const FWidgetPatchContext* Context = nullptr)
{
    FSalObjectBuilder Builder;
    if (Context != nullptr)
    {
        TSet<UWidget*> Emitted;
        TSet<UWidget*> ExplicitLocals;
        for (const FString& Alias : Context->LocalOrder)
        {
            if (UWidget* const* Widget = Context->Locals.Find(Alias); Widget != nullptr && *Widget != nullptr)
            {
                ExplicitLocals.Add(*Widget);
            }
        }
        TFunction<void(UWidget*, const FString&, const TArray<FString>&)> EmitCreatedSubtree;
        EmitCreatedSubtree = [&](UWidget* Widget, const FString& OwnerAlias, const TArray<FString>& Path)
        {
            if (Widget == nullptr || Emitted.Contains(Widget))
            {
                return;
            }
            if (!Path.IsEmpty() && ExplicitLocals.Contains(Widget))
            {
                return;
            }
            if (Path.IsEmpty())
            {
                Builder.AddLocalBinding(OwnerAlias, WidgetValue(Blueprint, Widget, EWidgetDetail::Complete));
            }
            else
            {
                Builder.AddMemberBinding(OwnerAlias, Path, WidgetValue(Blueprint, Widget, EWidgetDetail::Complete));
            }
            Emitted.Add(Widget);
            for (UWidget* Child : DirectChildren(Widget))
            {
                TArray<FString> ChildPath = Path;
                ChildPath.Add(Child->GetName());
                EmitCreatedSubtree(Child, OwnerAlias, ChildPath);
            }
        };
        for (const FString& Alias : Context->LocalOrder)
        {
            UWidget* const* Widget = Context->Locals.Find(Alias);
            if (Widget != nullptr && *Widget != nullptr && (*Widget)->GetOuter() != GetTransientPackage())
            {
                EmitCreatedSubtree(*Widget, Alias, {});
            }
        }
        for (UWidget* Widget : Context->TouchedOrder)
        {
            if (Widget != nullptr && Widget->GetOuter() != GetTransientPackage() && !Emitted.Contains(Widget))
            {
                EmitExactWidget(Builder, Blueprint, Widget, false);
                Emitted.Add(Widget);
            }
        }
        for (const FWidgetPatchEffect& Effect : Context->Effects) Builder.AddComment(Effect.Summary);
    }
    if (Builder.BuildObject()->GetArrayField(TEXT("statements")).IsEmpty())
    {
        Builder.AddLocalBinding(Builder.UniqueAlias(Blueprint->GetName()), BlueprintValue(Blueprint, false));
    }
    return Builder.BuildObject();
}

TSharedPtr<FJsonObject> PatchPlan(const FWidgetPatchContext& Context)
{
    TSharedPtr<FJsonObject> Plan = MakeShared<FJsonObject>();
    Plan->SetStringField(TEXT("operation"), TEXT("widget_patch"));
    TArray<TSharedPtr<FJsonValue>> Effects;
    Effects.Reserve(Context.Effects.Num());
    for (int32 Index = 0; Index < Context.Effects.Num(); ++Index)
    {
        TSharedPtr<FJsonObject> Ordered = MakeShared<FJsonObject>();
        Ordered->SetNumberField(TEXT("index"), Index);
        if (Context.Effects[Index].Detail.IsValid())
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Context.Effects[Index].Detail->Values)
            {
                Ordered->SetField(Field.Key, Field.Value);
            }
        }
        Ordered->SetStringField(TEXT("summary"), Context.Effects[Index].Summary);
        Effects.Add(MakeShared<FJsonValueObject>(Ordered));
    }
    Plan->SetArrayField(TEXT("effects"), Effects);
    return Plan;
}

UBlueprintGeneratedClass* DuplicateWidgetSandboxClass(
    UBlueprintGeneratedClass* Source,
    UWidgetBlueprint* Blueprint,
    const TCHAR* Suffix,
    FString& OutError)
{
    if (Source == nullptr)
    {
        return nullptr;
    }
    const FName Name = MakeUniqueObjectName(
        GetTransientPackage(),
        Source->GetClass(),
        FName(*(Source->GetName() + Suffix)));
    UBlueprintGeneratedClass* Copy = DuplicateObject<UBlueprintGeneratedClass>(Source, GetTransientPackage(), Name);
    if (Copy == nullptr || !Copy->IsIn(GetTransientPackage()))
    {
        OutError = FString::Printf(TEXT("UE could not isolate %s for Widget Patch preflight."), *Source->GetName());
        return nullptr;
    }
    Copy->ClassGeneratedBy = Blueprint;
    Copy->SetFlags(RF_Transient | RF_Transactional);
    UObject* SourceCDO = Source->GetDefaultObject(false);
    UObject* CopyCDO = Copy->GetDefaultObject();
    if (CopyCDO == nullptr
        || CopyCDO == SourceCDO
        || !CopyCDO->IsIn(GetTransientPackage()))
    {
        OutError = FString::Printf(TEXT("UE did not isolate the default object for %s."), *Source->GetName());
        return nullptr;
    }
    return Copy;
}

UWidgetBlueprint* DuplicateForPreflight(UWidgetBlueprint* Blueprint, FString& OutError)
{
    OutError.Reset();
    const FName Name = MakeUniqueObjectName(
        GetTransientPackage(),
        Blueprint->GetClass(),
        FName(*(Blueprint->GetName() + TEXT("_SALDryRun"))));
    UWidgetBlueprint* Copy = DuplicateObject<UWidgetBlueprint>(Blueprint, GetTransientPackage(), Name);
    if (Copy == nullptr || !Copy->IsIn(GetTransientPackage()))
    {
        OutError = TEXT("UE could not duplicate the WidgetBlueprint into transient preflight state.");
        return nullptr;
    }
    Copy->SetFlags(RF_Transient | RF_Transactional);

    if (Copy->WidgetTree == Blueprint->WidgetTree)
    {
        Copy->WidgetTree = DuplicateObject<UWidgetTree>(Blueprint->WidgetTree, Copy);
    }
    if (Copy->WidgetTree == nullptr
        || Copy->WidgetTree == Blueprint->WidgetTree
        || !Copy->WidgetTree->IsIn(Copy))
    {
        OutError = TEXT("UE did not isolate the WidgetTree for Widget Patch preflight.");
        return nullptr;
    }

    TSet<UWidget*> LiveWidgets;
    LiveWidgets.Append(SourceWidgets(Blueprint));
    for (UWidget* Widget : SourceWidgets(Copy))
    {
        if (Widget == nullptr || LiveWidgets.Contains(Widget) || !Widget->IsIn(Copy))
        {
            OutError = TEXT("UE did not isolate every source Widget for Widget Patch preflight.");
            return nullptr;
        }
        if ((Widget->Slot != nullptr && !Widget->Slot->IsIn(Copy))
            || (Widget->Navigation != nullptr && !Widget->Navigation->IsIn(Copy)))
        {
            OutError = FString::Printf(TEXT("UE did not isolate Slot or Navigation state for Widget %s."), *Widget->GetName());
            return nullptr;
        }
    }

    UBlueprintGeneratedClass* Generated = DuplicateWidgetSandboxClass(
        Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass.Get()),
        Copy,
        TEXT("_SALGenerated"),
        OutError);
    if (Blueprint->GeneratedClass != nullptr && Generated == nullptr)
    {
        return nullptr;
    }
    UBlueprintGeneratedClass* Skeleton = DuplicateWidgetSandboxClass(
        Cast<UBlueprintGeneratedClass>(Blueprint->SkeletonGeneratedClass.Get()),
        Copy,
        TEXT("_SALSkeleton"),
        OutError);
    if (Blueprint->SkeletonGeneratedClass != nullptr && Skeleton == nullptr)
    {
        return nullptr;
    }
    Copy->GeneratedClass = Generated;
    Copy->SkeletonGeneratedClass = Skeleton != nullptr ? Skeleton : Generated;

    TArray<UEdGraph*> LiveGraphs;
    Blueprint->GetAllGraphs(LiveGraphs);
    TSet<UEdGraph*> LiveGraphSet;
    LiveGraphSet.Append(LiveGraphs);
    TArray<UEdGraph*> SandboxGraphs;
    Copy->GetAllGraphs(SandboxGraphs);
    for (UEdGraph* Graph : SandboxGraphs)
    {
        if (Graph == nullptr || LiveGraphSet.Contains(Graph) || !Graph->IsIn(Copy))
        {
            OutError = TEXT("UE did not isolate every WidgetBlueprint Graph for Widget Patch preflight.");
            return nullptr;
        }
    }

    TArray<TObjectPtr<UWidgetAnimation>> Animations;
    Animations.Reserve(Blueprint->Animations.Num());
    for (int32 Index = 0; Index < Blueprint->Animations.Num(); ++Index)
    {
        UWidgetAnimation* SourceAnimation = Blueprint->Animations[Index];
        if (SourceAnimation == nullptr)
        {
            Animations.Add(nullptr);
            continue;
        }
        UWidgetAnimation* Animation = Copy->Animations.IsValidIndex(Index) ? Copy->Animations[Index].Get() : nullptr;
        if (Animation == SourceAnimation || Animation == nullptr || !Animation->IsIn(Copy))
        {
            const FName AnimationName = MakeUniqueObjectName(Copy, SourceAnimation->GetClass(), SourceAnimation->GetFName());
            Animation = DuplicateObject<UWidgetAnimation>(SourceAnimation, Copy, AnimationName);
        }
        if (Animation == nullptr || Animation == SourceAnimation || !Animation->IsIn(Copy))
        {
            OutError = FString::Printf(TEXT("UE did not isolate Widget Animation %s."), *SourceAnimation->GetName());
            return nullptr;
        }
        if (SourceAnimation->MovieScene != nullptr
            && (Animation->MovieScene == SourceAnimation->MovieScene
                || Animation->MovieScene == nullptr
                || !Animation->MovieScene->IsIn(Copy)))
        {
            const FName MovieSceneName = MakeUniqueObjectName(Animation, SourceAnimation->MovieScene->GetClass(), SourceAnimation->MovieScene->GetFName());
            Animation->MovieScene = DuplicateObject<UMovieScene>(SourceAnimation->MovieScene, Animation, MovieSceneName);
        }
        if (SourceAnimation->MovieScene != nullptr
            && (Animation->MovieScene == nullptr
                || Animation->MovieScene == SourceAnimation->MovieScene
                || !Animation->MovieScene->IsIn(Copy)))
        {
            OutError = FString::Printf(TEXT("UE did not isolate MovieScene state for Widget Animation %s."), *SourceAnimation->GetName());
            return nullptr;
        }
        Animations.Add(Animation);
    }
    Copy->Animations = MoveTemp(Animations);

    const TArrayView<const TObjectPtr<UBlueprintExtension>> LiveExtensions = Blueprint->GetExtensions();
    const TArrayView<const TObjectPtr<UBlueprintExtension>> SandboxExtensions = Copy->GetExtensions();
    if (LiveExtensions.Num() != SandboxExtensions.Num())
    {
        OutError = TEXT("UE did not preserve WidgetBlueprint Extensions in transient preflight state.");
        return nullptr;
    }
    for (int32 Index = 0; Index < SandboxExtensions.Num(); ++Index)
    {
        UBlueprintExtension* Extension = SandboxExtensions[Index];
        if (Extension == nullptr
            || Extension == LiveExtensions[Index]
            || !Extension->IsIn(Copy))
        {
            OutError = TEXT("UE did not isolate every WidgetBlueprint Extension for Widget Patch preflight.");
            return nullptr;
        }
    }
    return Copy;
}
}

TSharedPtr<FJsonObject> FSalWidgetInterface::Patch(const FSalPatch& Patch, const FSalResolvedTarget& Target)
{
    UWidgetBlueprint* Blueprint = WidgetBlueprint(Target);
    if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr || !HasExactPatchTargetId(Target))
    {
        return MutationError(Patch, Target, TEXT("validation.exact_widget_blueprint_required"), TEXT("Widget Patch requires blueprint(asset: ..., id: ...) resolving a UWidgetBlueprint."), TEXT("patch"));
    }

    for (const TSharedPtr<FJsonValue>& Value : Patch.Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        FString Kind;
        if (Value->TryGetObject(Statement) && Statement != nullptr) (*Statement)->TryGetStringField(TEXT("kind"), Kind);
        if (Kind == TEXT("save") || Kind == TEXT("compile"))
        {
            return MutationError(Patch, Target, TEXT("validation.finalization_must_be_independent"), TEXT("Widget edits cannot be mixed with compile or save."), Kind);
        }
    }
    if (PatchMixesNonWidgetInterface(Patch))
    {
        return MutationError(
            Patch,
            Target,
            TEXT("capability.mixed_patch_interfaces_unavailable"),
            TEXT("One authored Patch must be owned atomically by one interface planner. Split Blueprint-owned and Widget-owned statements into separate ordered requests."),
            TEXT("patch"));
    }

    FString Error;
    UWidgetBlueprint* PreflightBlueprint = DuplicateForPreflight(Blueprint, Error);
    if (PreflightBlueprint == nullptr || PreflightBlueprint->WidgetTree == nullptr)
    {
        return MutationError(
            Patch,
            Target,
            TEXT("validation.preflight_failed"),
            Error.IsEmpty() ? TEXT("UE could not create a fully isolated transient WidgetBlueprint for atomic preflight.") : Error,
            TEXT("patch"));
    }
    FWidgetPatchContext Preflight;
    Preflight.Blueprint = PreflightBlueprint;
    Preflight.TargetAlias = Patch.Alias;
    SeedPlanIdentities(Preflight);
    bool bPreflightSucceeded = false;
    bool bPreflightTransactionAvailable = false;
    {
        FScopedTransaction Transaction(NSLOCTEXT("Loomle", "SalWidgetPatchPreflight", "SAL Widget Patch Preflight"));
        bPreflightTransactionAvailable = Transaction.IsOutstanding();
        if (bPreflightTransactionAvailable)
        {
            bPreflightSucceeded = ApplyWidgetPatch(Patch, Preflight, Error);
            // Preflight edits only a transient duplicate.  Canceling prevents native
            // helpers with nested FScopedTransaction instances from leaving an undo
            // record while retaining the duplicate for result/plan inspection.
            Transaction.Cancel();
        }
    }
    if (!bPreflightTransactionAvailable)
    {
        return MutationError(
            Patch,
            Target,
            TEXT("capability.transaction_unavailable"),
            TEXT("Widget Patch preflight requires an available top-level UE editor transaction."),
            TEXT("patch"));
    }
    if (!bPreflightSucceeded)
    {
        return MutationError(Patch, Target, Preflight.ErrorCode, Error, TEXT("patch"));
    }
    const TSharedPtr<FJsonObject> Plan = PatchPlan(Preflight);
    if (Patch.bDryRun)
    {
        return MakeMutationResult(PatchObject(Blueprint), {}, true, true, false, Target.AssetPath, TEXT("patch"), Plan);
    }

    FWidgetPatchContext Applied;
    Applied.Blueprint = Blueprint;
    Applied.TargetAlias = Patch.Alias;
    SeedPlanIdentities(Applied);
    UPackage* Package = Blueprint->GetOutermost();
    const bool bWasDirty = Package != nullptr && Package->IsDirty();
    bool bApplySucceeded = false;
    bool bTransactionAvailable = false;
    {
        FScopedTransaction Transaction(NSLOCTEXT("Loomle", "SalWidgetPatch", "SAL Widget Patch"));
        bTransactionAvailable = Transaction.IsOutstanding();
        if (bTransactionAvailable)
        {
            bApplySucceeded = ApplyWidgetPatch(Patch, Applied, Error);
        }
    }
    if (!bTransactionAvailable)
    {
        return MutationError(
            Patch,
            Target,
            TEXT("capability.transaction_unavailable"),
            TEXT("Widget Patch requires an available top-level UE editor transaction."),
            TEXT("patch"));
    }
    if (!bApplySucceeded)
    {
        const bool bRolledBack = GEditor != nullptr && GEditor->UndoTransaction(false);
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(bRolledBack ? bWasDirty : true);
        }
        if (!bRolledBack)
        {
            return MakeMutationResult(
                nullptr,
                {FSalDiagnostics::Error(
                    TEXT("validation.atomic_rollback_failed"),
                    Error + TEXT(" UE could not roll back the failed Widget transaction."))
                    .Interface(InterfaceName)
                    .Operation(TEXT("patch"))
                    .Build()},
                false,
                false,
                true,
                Target.AssetPath,
                TEXT("patch"));
        }
        return MutationError(
            Patch,
            Target,
            TEXT("validation.atomic_apply_failed"),
            Error + TEXT(" The Widget transaction was rolled back."),
            TEXT("patch"));
    }
    return MakeMutationResult(PatchObject(Blueprint, &Applied), {}, false, true, true, Target.AssetPath, TEXT("patch"), Plan);
}
}
