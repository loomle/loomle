// Copyright 2026 Loomle contributors.

#include "SalBlueprintInterface.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "AssetRegistry/AssetData.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "BlueprintNamespaceUtilities.h"
#include "BlueprintNamespaceRegistry.h"
#include "BlueprintEditor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "EdGraphToken.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/TimelineTemplate.h"
#include "Engine/World.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "KismetCompilerModule.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "ObjectEditorUtils.h"
#include "PreviewScene.h"
#include "Sal/SalDiagnostics.h"
#include "Sal/SalObjectBuilder.h"
#include "Sal/SalRuntime.h"
#include "ScopedTransaction.h"
#include "Settings/BlueprintEditorProjectSettings.h"
#include "BlueprintEditorSettings.h"
#include "SubobjectData.h"
#include "SubobjectDataSubsystem.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace Loomle::Sal
{
namespace
{
constexpr const TCHAR* BlueprintInterfaceName = TEXT("blueprint");
constexpr const TCHAR* VariablePaletteId = TEXT("blueprint.variable");
constexpr const TCHAR* DispatcherPaletteId = TEXT("blueprint.dispatcher");
constexpr const TCHAR* FunctionGraphPaletteId = TEXT("blueprint.graph.function");
constexpr const TCHAR* MacroGraphPaletteId = TEXT("blueprint.graph.macro");
constexpr const TCHAR* EventGraphPaletteId = TEXT("blueprint.graph.event");
constexpr const TCHAR* ComponentPalettePrefix = TEXT("blueprint.component:");

FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

FString EnumName(const UEnum* Enum, const int64 Value)
{
    if (Enum == nullptr)
    {
        return LexToString(Value);
    }
    FString Name = Enum->GetNameStringByValue(Value);
    int32 Separator = INDEX_NONE;
    if (Name.FindLastChar(TEXT(':'), Separator))
    {
        Name = Name.Mid(Separator + 1);
    }
    return Name;
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
    if (const FByteProperty* Byte = CastField<FByteProperty>(Property); Byte != nullptr && Byte->Enum != nullptr)
    {
        return Value::Name(EnumName(Byte->Enum, Byte->GetPropertyValue_InContainer(Container)));
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
    if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
    {
        const void* Address = EnumProperty->ContainerPtrToValuePtr<void>(Container);
        return Value::Name(EnumName(
            EnumProperty->GetEnum(),
            EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Address)));
    }
    if (const FNameProperty* Name = CastField<FNameProperty>(Property))
    {
        const FString Text = Name->GetPropertyValue_InContainer(Container).ToString();
        return Text.IsEmpty() ? Value::Name(TEXT("None")) : Value::String(Text);
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

FString QuoteNativeString(FString Text)
{
    Text.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Text.ReplaceInline(TEXT("\""), TEXT("\\\""));
    return TEXT("\"") + Text + TEXT("\"");
}

FString NativeTextForProperty(const FProperty* Property, const TSharedPtr<FJsonValue>& JsonValue)
{
    if (Property == nullptr || !JsonValue.IsValid())
    {
        return FString();
    }
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!JsonValue->TryGetArray(Values) || Values == nullptr)
        {
            return FString();
        }
        TArray<FString> Items;
        for (const TSharedPtr<FJsonValue>& Item : *Values)
        {
            Items.Add(NativeTextForProperty(Array->Inner, Item));
        }
        return TEXT("(") + FString::Join(Items, TEXT(",")) + TEXT(")");
    }
    if (const FSetProperty* Set = CastField<FSetProperty>(Property))
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!JsonValue->TryGetArray(Values) || Values == nullptr)
        {
            return FString();
        }
        TArray<FString> Items;
        for (const TSharedPtr<FJsonValue>& Item : *Values)
        {
            Items.Add(NativeTextForProperty(Set->ElementProp, Item));
        }
        return TEXT("(") + FString::Join(Items, TEXT(",")) + TEXT(")");
    }
    if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        if (JsonValue->TryGetObject(Object) && Object != nullptr && (*Object).IsValid() && !(*Object)->HasField(TEXT("kind")))
        {
            TArray<FString> Fields;
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Object)->Values)
            {
                FProperty* Child = Struct->Struct->FindPropertyByName(FName(*Pair.Key));
                if (Child == nullptr)
                {
                    return FString();
                }
                Fields.Add(Pair.Key + TEXT("=") + NativeTextForProperty(Child, Pair.Value));
            }
            return TEXT("(") + FString::Join(Fields, TEXT(",")) + TEXT(")");
        }
    }
    const FString Text = ExprString(JsonValue);
    if (CastField<FStrProperty>(Property) != nullptr
        || CastField<FTextProperty>(Property) != nullptr
        || CastField<FNameProperty>(Property) != nullptr)
    {
        return QuoteNativeString(Text);
    }
    return Text;
}

TSharedPtr<FJsonObject> CallArgs()
{
    return MakeShared<FJsonObject>();
}

void SetArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, const TSharedPtr<FJsonValue>& InValue)
{
    if (Args.IsValid() && InValue.IsValid())
    {
        Args->SetField(Name, InValue);
    }
}

TSharedPtr<FJsonValue> Call(const FString& Callee, const TSharedPtr<FJsonObject>& Args)
{
    return Value::Call(Callee, Args);
}

TSharedPtr<FJsonObject> QueryError(
    const FString& Code,
    const FString& Message,
    const FString& Operation = FString(),
    const FString& Ref = FString(),
    const FString& Suggestion = FString())
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(Code, Message).Interface(BlueprintInterfaceName);
    if (!Operation.IsEmpty())
    {
        Diagnostic.Operation(Operation);
    }
    if (!Ref.IsEmpty())
    {
        Diagnostic.Ref(Ref);
    }
    if (!Suggestion.IsEmpty())
    {
        Diagnostic.Suggestion(Suggestion);
    }
    return FSalDiagnostics::Result(Diagnostic.Build());
}

TSharedPtr<FJsonObject> MutationError(
    const FSalPatch& Patch,
    const FSalResolvedTarget& Target,
    const FString& Code,
    const FString& Message,
    const FString& Operation,
    const FString& Ref = FString(),
    const FString& Suggestion = FString(),
    const bool bApplied = false)
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(Code, Message)
        .Interface(BlueprintInterfaceName)
        .Operation(Operation);
    if (!Ref.IsEmpty())
    {
        Diagnostic.Ref(Ref);
    }
    if (!Suggestion.IsEmpty())
    {
        Diagnostic.Suggestion(Suggestion);
    }
    return MakeMutationResult(
        nullptr,
        {Diagnostic.Build()},
        Patch.bDryRun,
        false,
        bApplied,
        Target.AssetPath,
        TEXT("patch"));
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

bool IsDispatcher(const FBPVariableDescription& Variable)
{
    return Variable.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate;
}

FBPVariableDescription* FindVariable(UBlueprint* Blueprint, const FString& Id, const bool bWantDispatcher)
{
    if (Blueprint == nullptr)
    {
        return nullptr;
    }
    FBPVariableDescription* Match = nullptr;
    for (FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        if (GuidText(Variable.VarGuid).Equals(Id, ESearchCase::IgnoreCase)
            && IsDispatcher(Variable) == bWantDispatcher)
        {
            if (Match != nullptr)
            {
                return nullptr;
            }
            Match = &Variable;
        }
    }
    return Match;
}

FBPVariableDescription* FindVariableByName(UBlueprint* Blueprint, const FString& Name, const bool bWantDispatcher)
{
    if (Blueprint == nullptr)
    {
        return nullptr;
    }
    FBPVariableDescription* Match = nullptr;
    for (FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        if (Variable.VarName.ToString() == Name && IsDispatcher(Variable) == bWantDispatcher)
        {
            if (Match != nullptr)
            {
                return nullptr;
            }
            Match = &Variable;
        }
    }
    return Match;
}

TArray<UEdGraph*> BlueprintGraphs(UBlueprint* Blueprint)
{
    TArray<UEdGraph*> Graphs;
    if (Blueprint != nullptr)
    {
        Blueprint->GetAllGraphs(Graphs);
    }
    return Graphs;
}

UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& Id)
{
    UEdGraph* Match = nullptr;
    for (UEdGraph* Graph : BlueprintGraphs(Blueprint))
    {
        if (Graph != nullptr && GuidText(Graph->GraphGuid).Equals(Id, ESearchCase::IgnoreCase))
        {
            if (Match != nullptr)
            {
                return nullptr;
            }
            Match = Graph;
        }
    }
    return Match;
}

UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& Name)
{
    UEdGraph* Match = nullptr;
    for (UEdGraph* Graph : BlueprintGraphs(Blueprint))
    {
        if (Graph != nullptr && Graph->GetName() == Name)
        {
            if (Match != nullptr)
            {
                return nullptr;
            }
            Match = Graph;
        }
    }
    return Match;
}

UEdGraph* FindDispatcherGraph(UBlueprint* Blueprint, const FString& Name)
{
    if (Blueprint == nullptr)
    {
        return nullptr;
    }
    UEdGraph* Match = nullptr;
    for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
    {
        if (Graph != nullptr && Graph->GetName() == Name)
        {
            if (Match != nullptr)
            {
                return nullptr;
            }
            Match = Graph;
        }
    }
    return Match;
}

USCS_Node* FindComponent(UBlueprint* Blueprint, const FString& Id)
{
    if (Blueprint == nullptr || Blueprint->SimpleConstructionScript == nullptr)
    {
        return nullptr;
    }
    FGuid Guid;
    if (!FGuid::Parse(Id, Guid))
    {
        return nullptr;
    }
    USCS_Node* Match = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node != nullptr && Node->VariableGuid == Guid)
        {
            if (Match != nullptr)
            {
                return nullptr;
            }
            Match = Node;
        }
    }
    return Match;
}

USCS_Node* FindComponentByName(UBlueprint* Blueprint, const FString& Name)
{
    if (Blueprint == nullptr || Blueprint->SimpleConstructionScript == nullptr)
    {
        return nullptr;
    }
    USCS_Node* Match = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node != nullptr && Node->GetVariableName().ToString() == Name)
        {
            if (Match != nullptr)
            {
                return nullptr;
            }
            Match = Node;
        }
    }
    return Match;
}

FString PinTypeText(const FEdGraphPinType& Type)
{
    FString Text;
    FEdGraphPinType::StaticStruct()->ExportText(Text, &Type, nullptr, nullptr, PPF_None, nullptr);
    return Text;
}

bool ParsePinType(const FString& Text, FEdGraphPinType& OutType)
{
    if (Text.IsEmpty())
    {
        return false;
    }
    OutType = FEdGraphPinType();
    const TCHAR* End = FEdGraphPinType::StaticStruct()->ImportText(
        *Text,
        &OutType,
        nullptr,
        PPF_None,
        GLog,
        TEXT("FEdGraphPinType"));
    if (End == nullptr)
    {
        return false;
    }
    while (*End != TEXT('\0') && FChar::IsWhitespace(*End))
    {
        ++End;
    }
    return *End == TEXT('\0');
}

FString GraphType(UEdGraph* Graph)
{
    const UEdGraphSchema* Schema = Graph != nullptr ? Graph->GetSchema() : nullptr;
    return Schema != nullptr
        ? EnumName(StaticEnum<EGraphType>(), Schema->GetGraphType(Graph))
        : TEXT("GT_Function");
}

struct FBlueprintOutputContext
{
    FString AssetAlias;
    FString BlueprintAlias;
};

void AddBlueprintNativeField(
    const TSharedPtr<FJsonObject>& Args,
    UBlueprint* Blueprint,
    const TCHAR* Name,
    const bool bAlways = false)
{
    FProperty* Property = FindFProperty<FProperty>(Blueprint->GetClass(), Name);
    if (Property == nullptr)
    {
        return;
    }
    const UObject* Defaults = Blueprint->GetClass()->GetDefaultObject();
    if (!bAlways && Defaults != nullptr && Property->Identical_InContainer(Blueprint, Defaults))
    {
        return;
    }
    SetArg(Args, Name, PropertyValue(Property, Blueprint));
}

FBlueprintOutputContext AddBlueprintObject(
    FSalObjectBuilder& Builder,
    UBlueprint* Blueprint,
    const FString& PreferredAlias,
    const bool bComplete)
{
    FBlueprintOutputContext Context;
    Context.AssetAlias = Builder.UniqueAlias(PreferredAlias + TEXT("Asset"));
    TSharedPtr<FJsonObject> AssetArgs = CallArgs();
    SetArg(AssetArgs, TEXT("path"), Value::String(Blueprint->GetPathName()));
    SetArg(AssetArgs, TEXT("type"), Value::String(Blueprint->GetClass()->GetPathName()));
    Builder.AddLocalBinding(Context.AssetAlias, Call(TEXT("asset"), AssetArgs));

    Context.BlueprintAlias = Builder.UniqueAlias(PreferredAlias.IsEmpty() ? Blueprint->GetName() : PreferredAlias);
    TSharedPtr<FJsonObject> Args = CallArgs();
    SetArg(Args, TEXT("asset"), Value::Local(Context.AssetAlias));
    SetArg(Args, TEXT("id"), Value::String(GuidText(Blueprint->GetBlueprintGuid())));
    SetArg(Args, TEXT("type"), Value::Name(EnumName(StaticEnum<EBlueprintType>(), Blueprint->BlueprintType)));
    SetArg(Args, TEXT("Status"), Value::Name(EnumName(StaticEnum<EBlueprintStatus>(), Blueprint->Status)));
    if (Blueprint->ParentClass != nullptr)
    {
        SetArg(Args, TEXT("ParentClass"), Value::String(Blueprint->ParentClass->GetPathName()));
    }
    if (bComplete)
    {
        static const TCHAR* Fields[] = {
            TEXT("bRunConstructionScriptOnDrag"),
            TEXT("bRunConstructionScriptInSequencer"),
            TEXT("BlueprintDisplayName"),
            TEXT("BlueprintDescription"),
            TEXT("BlueprintNamespace"),
            TEXT("BlueprintCategory"),
            TEXT("bGenerateConstClass"),
            TEXT("bGenerateAbstractClass"),
            TEXT("bDeprecate"),
            TEXT("ShouldCookPropertyGuidsValue"),
            TEXT("CompileMode")};
        for (const TCHAR* Field : Fields)
        {
            AddBlueprintNativeField(Args, Blueprint, Field);
        }
        if (!Blueprint->HideCategories.IsEmpty())
        {
            TArray<TSharedPtr<FJsonValue>> Values;
            for (const FString& Category : Blueprint->HideCategories)
            {
                Values.Add(Value::String(Category));
            }
            Args->SetArrayField(TEXT("HideCategories"), Values);
        }
        if (!Blueprint->ImportedNamespaces.IsEmpty())
        {
            TArray<FString> Namespaces = Blueprint->ImportedNamespaces.Array();
            Namespaces.Sort();
            TArray<TSharedPtr<FJsonValue>> Values;
            for (const FString& Namespace : Namespaces)
            {
                Values.Add(Value::String(Namespace));
            }
            Args->SetArrayField(TEXT("ImportedNamespaces"), Values);
        }

        TArray<TSharedPtr<FJsonValue>> Interfaces;
        for (const FBPInterfaceDescription& Description : Blueprint->ImplementedInterfaces)
        {
            if (Description.Interface == nullptr)
            {
                continue;
            }
            TSharedPtr<FJsonObject> Interface = MakeShared<FJsonObject>();
            Interface->SetStringField(TEXT("Interface"), Description.Interface->GetPathName());
            TArray<TSharedPtr<FJsonValue>> GraphRefs;
            for (UEdGraph* Graph : Description.Graphs)
            {
                if (Graph != nullptr)
                {
                    GraphRefs.Add(Value::Stable(TEXT("graph"), GuidText(Graph->GraphGuid)));
                }
            }
            Interface->SetArrayField(TEXT("Graphs"), GraphRefs);
            Interfaces.Add(MakeShared<FJsonValueObject>(Interface));
        }
        if (!Interfaces.IsEmpty())
        {
            Args->SetArrayField(TEXT("ImplementedInterfaces"), Interfaces);
        }
    }
    Builder.AddLocalBinding(Context.BlueprintAlias, Call(TEXT("blueprint"), Args));
    if (bComplete)
    {
        TSet<FString> DefaultImports;
        FBlueprintNamespaceUtilities::GetDefaultImportsForObject(Blueprint, DefaultImports);
        DefaultImports.Remove(FString());
        TArray<FString> SortedDefaultImports = DefaultImports.Array();
        SortedDefaultImports.Sort();
        if (!SortedDefaultImports.IsEmpty())
        {
            Builder.AddComment(TEXT("Default Namespaces\n") + FString::Join(SortedDefaultImports, TEXT("\n")));
        }
        if (Blueprint->GeneratedClass != nullptr)
        {
            Builder.AddComment(FString::Printf(TEXT("Generated Class: %s"), *Blueprint->GeneratedClass->GetPathName()));
        }
    }
    return Context;
}

TSharedPtr<FJsonValue> VariableValue(const FBPVariableDescription& Variable, const bool bDispatcher)
{
    TSharedPtr<FJsonObject> Args = CallArgs();
    SetArg(Args, TEXT("id"), Value::String(GuidText(Variable.VarGuid)));
    SetArg(Args, TEXT("type"), Value::String(PinTypeText(Variable.VarType)));
    if (!Variable.FriendlyName.IsEmpty())
    {
        SetArg(Args, TEXT("FriendlyName"), Value::String(Variable.FriendlyName));
    }
    if (!Variable.Category.IsEmpty())
    {
        SetArg(Args, TEXT("Category"), Value::String(Variable.Category.ToString()));
    }
    if (Variable.PropertyFlags != 0)
    {
        SetArg(Args, TEXT("PropertyFlags"), Value::String(LexToString(Variable.PropertyFlags)));
    }
    if (!Variable.RepNotifyFunc.IsNone())
    {
        SetArg(Args, TEXT("RepNotifyFunc"), Value::String(Variable.RepNotifyFunc.ToString()));
    }
    if (Variable.ReplicationCondition != COND_None)
    {
        SetArg(Args, TEXT("ReplicationCondition"), Value::Name(EnumName(StaticEnum<ELifetimeCondition>(), Variable.ReplicationCondition)));
    }
    if (!Variable.MetaDataArray.IsEmpty())
    {
        TArray<TSharedPtr<FJsonValue>> MetaData;
        for (const FBPVariableMetaDataEntry& Entry : Variable.MetaDataArray)
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("DataKey"), Entry.DataKey.ToString());
            Item->SetStringField(TEXT("DataValue"), Entry.DataValue);
            MetaData.Add(MakeShared<FJsonValueObject>(Item));
        }
        Args->SetArrayField(TEXT("MetaDataArray"), MetaData);
    }
    if (!Variable.DefaultValue.IsEmpty())
    {
        SetArg(Args, TEXT("DefaultValue"), Value::String(Variable.DefaultValue));
    }
    return Call(bDispatcher ? TEXT("dispatcher") : TEXT("variable"), Args);
}

TSharedPtr<FJsonValue> GraphValue(UEdGraph* Graph, const FString& BlueprintAlias)
{
    TSharedPtr<FJsonObject> Args = CallArgs();
    SetArg(Args, TEXT("asset"), Value::Local(BlueprintAlias));
    SetArg(Args, TEXT("id"), Value::String(GuidText(Graph->GraphGuid)));
    SetArg(Args, TEXT("name"), Value::String(Graph->GetName()));
    SetArg(Args, TEXT("type"), Value::Name(GraphType(Graph)));
    if (Graph->GetSchema() != nullptr)
    {
        SetArg(Args, TEXT("Schema"), Value::String(Graph->GetSchema()->GetClass()->GetPathName()));
    }
    SetArg(Args, TEXT("bEditable"), Value::Bool(Graph->bEditable));
    SetArg(Args, TEXT("bAllowDeletion"), Value::Bool(Graph->bAllowDeletion));
    SetArg(Args, TEXT("bAllowRenaming"), Value::Bool(Graph->bAllowRenaming));
    if (Graph->InterfaceGuid.IsValid())
    {
        SetArg(Args, TEXT("InterfaceGuid"), Value::String(GuidText(Graph->InterfaceGuid)));
    }
    return Call(TEXT("graph"), Args);
}

bool IsPersistentNativeField(const FProperty* Property)
{
    return Property != nullptr
        && !Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated);
}

bool CanWriteNativeTemplateField(const UObject* Object, const FProperty* Property)
{
    if (Object == nullptr
        || !IsPersistentNativeField(Property)
        || !Property->HasAnyPropertyFlags(CPF_Edit)
        || Property->HasAnyPropertyFlags(CPF_EditConst)
        || (Object->IsTemplate() && Property->HasAnyPropertyFlags(CPF_DisableEditOnTemplate))
        || (!Object->IsTemplate() && Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance)))
    {
        return false;
    }
    return Object->CanEditChange(Property);
}

bool CanResetNativeTemplateField(const UObject* Object, const FProperty* Property)
{
    return CanWriteNativeTemplateField(Object, Property)
        && !Property->HasMetaData(TEXT("NoResetToDefault"))
        && Object->GetClass()->GetDefaultObject() != nullptr;
}

TSharedPtr<FJsonValue> ComponentValue(USCS_Node* Node, const bool bComplete)
{
    TSharedPtr<FJsonObject> Args = CallArgs();
    SetArg(Args, TEXT("id"), Value::String(GuidText(Node->VariableGuid)));
    if (Node->ComponentClass != nullptr)
    {
        SetArg(Args, TEXT("type"), Value::String(Node->ComponentClass->GetPathName()));
    }
    if (!Node->CategoryName.IsEmpty())
    {
        SetArg(Args, TEXT("CategoryName"), Value::String(Node->CategoryName.ToString()));
    }
    if (!Node->AttachToName.IsNone())
    {
        SetArg(Args, TEXT("AttachToName"), Value::String(Node->AttachToName.ToString()));
    }
    if (bComplete && Node->ComponentTemplate != nullptr)
    {
        UObject* Template = Node->ComponentTemplate;
        const UObject* Defaults = Template->GetClass()->GetDefaultObject();
        for (TFieldIterator<FProperty> It(Template->GetClass()); It; ++It)
        {
            FProperty* Property = *It;
            if (!Property->HasAnyPropertyFlags(CPF_Edit)
                || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)
                || (Defaults != nullptr && Property->Identical_InContainer(Template, Defaults)))
            {
                continue;
            }
            if (!Args->HasField(Property->GetName()))
            {
                Args->SetField(Property->GetName(), PropertyValue(Property, Template));
            }
        }
    }
    return Call(TEXT("component"), Args);
}

void AddVariableBinding(
    FSalObjectBuilder& Builder,
    const FString& BlueprintAlias,
    const FBPVariableDescription& Variable,
    const bool bDispatcher)
{
    Builder.AddMemberBinding(
        BlueprintAlias,
        {Variable.VarName.ToString()},
        VariableValue(Variable, bDispatcher));
}

void AddGraphBinding(
    FSalObjectBuilder& Builder,
    const FString& BlueprintAlias,
    UEdGraph* Graph)
{
    const FString Alias = Builder.UniqueAlias(Graph->GetName());
    Builder.AddLocalBinding(Alias, GraphValue(Graph, BlueprintAlias));
}

void AddComponentBinding(
    FSalObjectBuilder& Builder,
    UBlueprint* Blueprint,
    const FString& BlueprintAlias,
    USCS_Node* Node,
    const bool bComplete)
{
    TArray<FString> Path;
    for (USCS_Node* Current = Node; Current != nullptr; )
    {
        Path.Insert(Current->GetVariableName().ToString(), 0);
        Current = Blueprint != nullptr && Blueprint->SimpleConstructionScript != nullptr
            ? Blueprint->SimpleConstructionScript->FindParentNode(Current)
            : nullptr;
    }
    Builder.AddMemberBinding(
        BlueprintAlias,
        Path,
        ComponentValue(Node, bComplete));
}

bool IsBlueprintCompileAllowedDuringPIE(const UBlueprint* Blueprint);

bool IsBlueprintFieldWritableNow(const UBlueprint* Blueprint, const FString& Field, FString* OutReason = nullptr)
{
    auto Reject = [OutReason](const TCHAR* Reason)
    {
        if (OutReason != nullptr)
        {
            *OutReason = Reason;
        }
        return false;
    };
    if (Blueprint == nullptr)
    {
        return Reject(TEXT("Blueprint is unavailable."));
    }
    if (Field == TEXT("bRunConstructionScriptOnDrag") && Blueprint->BlueprintType == BPTYPE_LevelScript)
    {
        return Reject(TEXT("Level Blueprints do not expose this option."));
    }
    if (Field == TEXT("bDeprecate"))
    {
        if (Blueprint->BlueprintType == BPTYPE_LevelScript)
        {
            return Reject(TEXT("Level Blueprints do not expose deprecation."));
        }
        if (Blueprint->ParentClass != nullptr && Blueprint->ParentClass->HasAnyClassFlags(CLASS_Deprecated))
        {
            return Reject(TEXT("The inherited Class is deprecated, so local deprecation cannot be changed."));
        }
    }
    if (Field == TEXT("CompileMode")
        && !GetDefault<UBlueprintEditorSettings>()->bAllowExplicitImpureNodeDisabling)
    {
        return Reject(TEXT("Explicit impure Node disabling is disabled in Blueprint Editor settings."));
    }
    if (Field == TEXT("ImportedNamespaces")
        && !GetDefault<UBlueprintEditorSettings>()->bEnableNamespaceImportingFeatures)
    {
        return Reject(TEXT("Blueprint Namespace importing features are disabled."));
    }
    return true;
}

bool CanSaveBlueprintNow(FString* OutReason = nullptr)
{
    if (GEditor == nullptr)
    {
        if (OutReason != nullptr)
        {
            *OutReason = TEXT("UE Editor is unavailable.");
        }
        return false;
    }
    if (GEditor->PlayWorld != nullptr)
    {
        const IConsoleVariable* AllowSaving = IConsoleManager::Get().FindConsoleVariable(TEXT("Editor.AllowSavingAssetsDuringPIE"));
        if (AllowSaving == nullptr || AllowSaving->GetInt() == 0)
        {
            if (OutReason != nullptr)
            {
                *OutReason = TEXT("Editor.AllowSavingAssetsDuringPIE is disabled.");
            }
            return false;
        }
    }
    return true;
}

FBlueprintEditor* FindOpenBlueprintEditor(UBlueprint* Blueprint)
{
    if (GEditor == nullptr || Blueprint == nullptr)
    {
        return nullptr;
    }
    UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    IAssetEditorInstance* Instance = AssetEditors != nullptr
        ? AssetEditors->FindEditorForAsset(Blueprint, false)
        : nullptr;
    return Instance != nullptr ? static_cast<FBlueprintEditor*>(Instance) : nullptr;
}

FString BlueprintSchema(UBlueprint* Blueprint)
{
    FString Text = TEXT("schema\n\nfields:\n");
    Text += TEXT("  id: FGuid; read\n");
    Text += TEXT("  type: EBlueprintType; read\n");
    Text += TEXT("  Status: EBlueprintStatus; read, transient\n");
    Text += TEXT("  ParentClass: TSubclassOf<UObject>; read, write; no reset\n");
    static const TCHAR* Fields[] = {
        TEXT("bRunConstructionScriptOnDrag"), TEXT("bRunConstructionScriptInSequencer"),
        TEXT("BlueprintDisplayName"), TEXT("BlueprintDescription"),
        TEXT("BlueprintNamespace"), TEXT("BlueprintCategory"), TEXT("HideCategories"),
        TEXT("bGenerateConstClass"), TEXT("bGenerateAbstractClass"), TEXT("bDeprecate"),
        TEXT("ShouldCookPropertyGuidsValue"), TEXT("CompileMode"), TEXT("ImportedNamespaces")};
    for (const TCHAR* Name : Fields)
    {
        if (FProperty* Property = FindFProperty<FProperty>(Blueprint->GetClass(), Name))
        {
            FString Reason;
            const bool bWritable = IsBlueprintFieldWritableNow(Blueprint, Name, &Reason);
            Text += FString::Printf(
                TEXT("  %s: %s; read%s%s\n"),
                Name,
                *Property->GetCPPType(),
                bWritable ? TEXT(", write, reset") : TEXT(", write unavailable"),
                Reason.IsEmpty() ? TEXT("") : *FString(TEXT("; ") + Reason));
        }
    }
    FString CompileReason;
    const bool bCanCompile = Blueprint->BlueprintType != BPTYPE_MacroLibrary
        && IsBlueprintCompileAllowedDuringPIE(Blueprint);
    if (!bCanCompile)
    {
        CompileReason = Blueprint->BlueprintType == BPTYPE_MacroLibrary
            ? TEXT("Macro Libraries cannot compile directly.")
            : TEXT("Current PIE safety rules forbid compilation.");
    }
    FString SaveReason;
    const bool bCanSave = CanSaveBlueprintNow(&SaveReason);
    Text += TEXT("\npatch:\n");
    if (bCanCompile)
    {
        Text += TEXT("  compile\n");
    }
    else
    {
        Text += FString(TEXT("  compile: unavailable; ")) + CompileReason + TEXT("\n");
    }
    if (bCanSave)
    {
        Text += TEXT("  save\n");
    }
    else
    {
        Text += FString(TEXT("  save: unavailable; ")) + SaveReason + TEXT("\n");
    }
    Text += TEXT("\noperations:\n");
    Text += TEXT("  ImplementInterface(Interface: FTopLevelAssetPath)\n");
    Text += TEXT("  RemoveInterface(Interface: FTopLevelAssetPath, bPreserveFunctions: bool)\n");
    Text += TEXT("  ImplementFunction(function: FMemberReference)\n");
    return Text;
}

FString VariableSchema(const bool bDispatcher)
{
    FString Text = TEXT("schema\n\nfields:\n");
    Text += TEXT("  id: FGuid; read\n");
    Text += bDispatcher
        ? TEXT("  type: FEdGraphPinType; read\n")
        : TEXT("  type: FEdGraphPinType; read, write; no reset\n");
    Text += TEXT("  VarName: FName; read, write\n");
    Text += TEXT("  FriendlyName: FString; read, write, reset\n");
    Text += TEXT("  Category: FText; read, write, reset\n");
    Text += bDispatcher
        ? TEXT("  PropertyFlags: EPropertyFlags; read, write; required CPF_BlueprintAssignable and CPF_BlueprintCallable cannot be cleared\n")
        : TEXT("  PropertyFlags: EPropertyFlags; read, write, reset\n");
    Text += TEXT("  RepNotifyFunc: FName; read, write, reset\n");
    Text += TEXT("  ReplicationCondition: ELifetimeCondition; read, write, reset\n");
    Text += TEXT("  MetaDataArray: TArray<FBPVariableMetaDataEntry>; read, write, reset\n");
    if (!bDispatcher)
    {
        Text += TEXT("  DefaultValue: FString; read, write, reset; compiler staging only\n");
    }
    Text += TEXT("\npatch:\n  move before|after\n  remove\n");
    return Text;
}

FString GraphSchema(UEdGraph* Graph)
{
    FString Text = TEXT("schema\n\nfields:\n");
    Text += TEXT("  id: FGuid; read\n  name: FName; read, write\n  type: EGraphType; read\n");
    Text += TEXT("  Schema: UClass; read\n");
    Text += TEXT("  bEditable: bool; read\n  bAllowDeletion: bool; read\n  bAllowRenaming: bool; read\n");
    UBlueprint* Blueprint = Graph != nullptr ? FBlueprintEditorUtils::FindBlueprintForGraph(Graph) : nullptr;
    const bool bDirectLifecycle = Blueprint != nullptr
        && (Blueprint->FunctionGraphs.Contains(Graph)
            || Blueprint->MacroGraphs.Contains(Graph)
            || Blueprint->UbergraphPages.Contains(Graph));
    Text += TEXT("\npatch:\n");
    Text += Graph != nullptr && Graph->bAllowRenaming && bDirectLifecycle
        ? TEXT("  set name\n")
        : TEXT("  set name: unavailable\n");
    Text += Graph != nullptr && Graph->bAllowDeletion && bDirectLifecycle
        ? TEXT("  remove\n")
        : TEXT("  remove: unavailable; owned by compound or editor-maintained lifecycle\n");
    return Text;
}

FString ComponentSchema(USCS_Node* Node)
{
    FString Text = TEXT("schema\n\nfields:\n");
    Text += TEXT("  id: FGuid; read\n  type: UClass; read\n  name: FName; read, write\n");
    if (Node != nullptr && Node->ComponentTemplate != nullptr)
    {
        for (TFieldIterator<FProperty> It(Node->ComponentTemplate->GetClass()); It; ++It)
        {
            FProperty* Property = *It;
            if (IsPersistentNativeField(Property))
            {
                const bool bWritable = CanWriteNativeTemplateField(Node->ComponentTemplate, Property);
                const bool bResettable = CanResetNativeTemplateField(Node->ComponentTemplate, Property);
                Text += FString::Printf(
                    TEXT("  %s: %s; read%s%s\n"),
                    *Property->GetName(),
                    *Property->GetCPPType(),
                    bWritable ? TEXT(", write") : TEXT(""),
                    bResettable ? TEXT(", reset") : TEXT(""));
            }
        }
    }
    bool bCanMove = false;
    bool bCanRemove = false;
    bool bCanMakeRoot = false;
    bool bCanDuplicate = false;
    UBlueprint* Blueprint = Node != nullptr && Node->GetSCS() != nullptr
        ? Node->GetSCS()->GetBlueprint()
        : nullptr;
    USubobjectDataSubsystem* System = USubobjectDataSubsystem::Get();
    if (Blueprint != nullptr && System != nullptr)
    {
        TArray<FSubobjectDataHandle> Handles;
        System->K2_GatherSubobjectDataForBlueprint(Blueprint, Handles);
        FSubobjectDataHandle ActorHandle = FSubobjectDataHandle::InvalidHandle;
        FSubobjectDataHandle ComponentHandle = FSubobjectDataHandle::InvalidHandle;
        for (const FSubobjectDataHandle& Handle : Handles)
        {
            const FSubobjectData* Data = Handle.GetData();
            if (Data == nullptr)
            {
                continue;
            }
            if (Data->IsRootActor())
            {
                ActorHandle = Handle;
            }
            if (Data->IsComponent()
                && (Data->GetObjectForBlueprint(Blueprint) == Node->ComponentTemplate
                    || Data->GetComponentTemplate() == Node->ComponentTemplate
                    || Data->GetVariableName() == Node->GetVariableName()))
            {
                ComponentHandle = Handle;
            }
        }
        const FSubobjectData* Data = ComponentHandle.GetData();
        if (Data != nullptr)
        {
            TArray<UK2Node_ComponentBoundEvent*> BoundEvents;
            FKismetEditorUtilities::FindAllBoundEventsForComponent(Blueprint, Node->GetVariableName(), BoundEvents);
            const FSubobjectDataHandle SceneRoot = ActorHandle.IsValid()
                ? System->FindSceneRootForSubobject(ActorHandle)
                : FSubobjectDataHandle::InvalidHandle;
            const FSubobjectData* SceneRootData = SceneRoot.GetData();
            bCanMove = Data->IsSceneComponent()
                && Data->CanReparent()
                && ComponentHandle != SceneRoot;
            bCanRemove = Data->CanDelete()
                && !Data->IsDefaultSceneRoot()
                && BoundEvents.IsEmpty();
            bCanMakeRoot = Data->IsSceneComponent()
                && Data->CanReparent()
                && ActorHandle.IsValid()
                && (ComponentHandle == SceneRoot
                    || (SceneRootData != nullptr
                        && (SceneRootData->IsDefaultSceneRoot() || SceneRootData->CanReparent())));
            bCanDuplicate = Data->CanDuplicate();
        }
    }
    Text += TEXT("\npatch:\n");
    Text += bCanMove ? TEXT("  move to component@id\n") : TEXT("  move: unavailable\n");
    Text += bCanRemove ? TEXT("  remove\n") : TEXT("  remove: unavailable\n");
    Text += TEXT("\noperations:\n");
    Text += bCanMakeRoot ? TEXT("  MakeNewSceneRoot()\n") : TEXT("  MakeNewSceneRoot(): unavailable\n");
    Text += bCanDuplicate ? TEXT("  Duplicate() -> component\n") : TEXT("  Duplicate(): unavailable\n");
    return Text;
}

struct FQueryItem
{
    FString Kind;
    FString Name;
    FString Id;
    FString Type;
    FString Search;
    FBPVariableDescription* Variable = nullptr;
    UEdGraph* Graph = nullptr;
    USCS_Node* Component = nullptr;
};

FString FieldValue(const FQueryItem& Item, const FString& Field)
{
    if (Field == TEXT("name")) return Item.Name;
    if (Field == TEXT("id")) return Item.Id;
    if (Field == TEXT("type")) return Item.Type;
    return FString();
}

bool MatchesWhere(const FQueryItem& Item, const TSharedPtr<FJsonObject>& Condition, bool& bSupported)
{
    if (!Condition.IsValid())
    {
        return true;
    }
    FString Kind;
    Condition->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("not"))
    {
        const TSharedPtr<FJsonObject>* Child = nullptr;
        if (!Condition->TryGetObjectField(TEXT("condition"), Child) || Child == nullptr)
        {
            bSupported = false;
            return false;
        }
        return !MatchesWhere(Item, *Child, bSupported);
    }
    if (Kind == TEXT("and") || Kind == TEXT("or"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Conditions = nullptr;
        if (!Condition->TryGetArrayField(TEXT("conditions"), Conditions) || Conditions == nullptr)
        {
            bSupported = false;
            return false;
        }
        bool Result = Kind == TEXT("and");
        for (const TSharedPtr<FJsonValue>& Value : *Conditions)
        {
            const TSharedPtr<FJsonObject>* Child = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Child) || Child == nullptr)
            {
                bSupported = false;
                return false;
            }
            const bool ChildResult = MatchesWhere(Item, *Child, bSupported);
            Result = Kind == TEXT("and") ? Result && ChildResult : Result || ChildResult;
        }
        return Result;
    }
    if (Kind != TEXT("eq") && Kind != TEXT("ne"))
    {
        bSupported = false;
        return false;
    }
    const FString Field = ConditionField(Condition);
    if (Field != TEXT("name") && Field != TEXT("id") && Field != TEXT("type"))
    {
        bSupported = false;
        return false;
    }
    const TSharedPtr<FJsonValue>* Expected = Condition->Values.Find(TEXT("value"));
    const FString ExpectedText = Expected != nullptr ? ExprString(*Expected) : FString();
    const bool bEqual = FieldValue(Item, Field).Equals(ExpectedText, ESearchCase::CaseSensitive);
    return Kind == TEXT("eq") ? bEqual : !bEqual;
}

void SortItems(TArray<FQueryItem>& Items, const TArray<TSharedPtr<FJsonObject>>& OrderBy)
{
    if (OrderBy.IsEmpty())
    {
        return;
    }
    Items.StableSort([&OrderBy](const FQueryItem& A, const FQueryItem& B)
    {
        for (const TSharedPtr<FJsonObject>& Order : OrderBy)
        {
            FString Key;
            FString Direction;
            Order->TryGetStringField(TEXT("key"), Key);
            Order->TryGetStringField(TEXT("direction"), Direction);
            const int32 Compare = FieldValue(A, Key).Compare(FieldValue(B, Key));
            if (Compare != 0)
            {
                return Direction == TEXT("desc") ? Compare > 0 : Compare < 0;
            }
        }
        return false;
    });
}

TArray<FQueryItem> CollectItems(UBlueprint* Blueprint, const FString& Kind)
{
    TArray<FQueryItem> Items;
    if (Kind == TEXT("variables") || Kind == TEXT("dispatchers"))
    {
        const bool bDispatcher = Kind == TEXT("dispatchers");
        for (FBPVariableDescription& Variable : Blueprint->NewVariables)
        {
            if (IsDispatcher(Variable) != bDispatcher)
            {
                continue;
            }
            FQueryItem& Item = Items.AddDefaulted_GetRef();
            Item.Kind = bDispatcher ? TEXT("dispatcher") : TEXT("variable");
            Item.Name = Variable.VarName.ToString();
            Item.Id = GuidText(Variable.VarGuid);
            Item.Type = PinTypeText(Variable.VarType);
            Item.Search = Item.Name + TEXT(" ") + Item.Type + TEXT(" ") + Variable.FriendlyName + TEXT(" ") + Variable.Category.ToString();
            Item.Variable = &Variable;
        }
    }
    else if (Kind == TEXT("graphs"))
    {
        for (UEdGraph* Graph : BlueprintGraphs(Blueprint))
        {
            if (Graph == nullptr)
            {
                continue;
            }
            FQueryItem& Item = Items.AddDefaulted_GetRef();
            Item.Kind = TEXT("graph");
            Item.Name = Graph->GetName();
            Item.Id = GuidText(Graph->GraphGuid);
            Item.Type = GraphType(Graph);
            Item.Search = Item.Name + TEXT(" ") + Item.Type + TEXT(" ") + (Graph->GetSchema() != nullptr ? Graph->GetSchema()->GetClass()->GetPathName() : FString());
            Item.Graph = Graph;
        }
    }
    else if (Kind == TEXT("components") && Blueprint->SimpleConstructionScript != nullptr)
    {
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node == nullptr)
            {
                continue;
            }
            FQueryItem& Item = Items.AddDefaulted_GetRef();
            Item.Kind = TEXT("component");
            Item.Name = Node->GetVariableName().ToString();
            Item.Id = GuidText(Node->VariableGuid);
            Item.Type = Node->ComponentClass != nullptr ? Node->ComponentClass->GetPathName() : FString();
            Item.Search = Item.Name + TEXT(" ") + Item.Type + TEXT(" ") + Node->CategoryName.ToString();
            Item.Component = Node;
        }
    }
    return Items;
}

void EmitItem(
    FSalObjectBuilder& Builder,
    UBlueprint* Blueprint,
    const FString& BlueprintAlias,
    const FQueryItem& Item,
    const bool bComplete)
{
    if (Item.Variable != nullptr)
    {
        AddVariableBinding(Builder, BlueprintAlias, *Item.Variable, Item.Kind == TEXT("dispatcher"));
    }
    else if (Item.Graph != nullptr)
    {
        AddGraphBinding(Builder, BlueprintAlias, Item.Graph);
    }
    else if (Item.Component != nullptr)
    {
        AddComponentBinding(Builder, Blueprint, BlueprintAlias, Item.Component, bComplete);
    }
}

struct FPaletteEntry
{
    FString Id;
    FString Label;
    FString Callee;
    FString Type;
    bool bRequiresType = false;
};

TArray<FPaletteEntry> PaletteEntries(UBlueprint* Blueprint)
{
    TArray<FPaletteEntry> Entries;
    if (Blueprint != nullptr && Blueprint->SupportsGlobalVariables())
    {
        Entries.Add({VariablePaletteId, TEXT("Variable"), TEXT("variable"), TEXT("FEdGraphPinType"), true});
    }
    if (Blueprint != nullptr && Blueprint->SupportsDelegates())
    {
        Entries.Add({DispatcherPaletteId, TEXT("Dispatcher"), TEXT("dispatcher"), FString(), false});
    }
    if (Blueprint != nullptr && Blueprint->SupportsFunctions())
    {
        Entries.Add({FunctionGraphPaletteId, TEXT("Function Graph"), TEXT("graph"), TEXT("GT_Function"), false});
    }
    if (Blueprint != nullptr && Blueprint->SupportsMacros())
    {
        Entries.Add({MacroGraphPaletteId, TEXT("Macro Graph"), TEXT("graph"), TEXT("GT_Macro"), false});
    }
    if (Blueprint != nullptr && Blueprint->SupportsEventGraphs())
    {
        Entries.Add({EventGraphPaletteId, TEXT("Event Graph"), TEXT("graph"), TEXT("GT_Ubergraph"), false});
    }
    if (Blueprint != nullptr && Blueprint->SimpleConstructionScript != nullptr)
    {
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* Class = *It;
            if (Class == nullptr
                || !Class->IsChildOf(UActorComponent::StaticClass())
                || !FKismetEditorUtilities::IsClassABlueprintSpawnableComponent(Class)
                || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
                || Class->GetName().StartsWith(TEXT("SKEL_"))
                || Class->GetName().StartsWith(TEXT("REINST_")))
            {
                continue;
            }
            FPaletteEntry& Entry = Entries.AddDefaulted_GetRef();
            Entry.Id = FString(ComponentPalettePrefix) + Class->GetPathName();
            Entry.Label = Class->GetDisplayNameText().ToString();
            Entry.Callee = TEXT("component");
            Entry.Type = Class->GetPathName();
        }
    }
    Entries.Sort([](const FPaletteEntry& A, const FPaletteEntry& B)
    {
        return A.Label.Compare(B.Label) < 0;
    });
    return Entries;
}

void EmitPalette(FSalObjectBuilder& Builder, const FPaletteEntry& Entry, const bool bSchema)
{
    const FString Alias = Builder.UniqueAlias(Entry.Label);
    TSharedPtr<FJsonObject> Args = CallArgs();
    SetArg(Args, TEXT("palette"), Value::String(Entry.Id));
    if (Entry.bRequiresType)
    {
        SetArg(Args, TEXT("type"), Value::String(TEXT("<FEdGraphPinType native text>")));
    }
    Builder.AddLocalBinding(Alias, Call(Entry.Callee, Args));
    if (bSchema)
    {
        FString Schema = FString::Printf(
            TEXT("schema\n\nconstructor:\n  %s(palette: string%s)\n\nresult type:\n  %s"),
            *Entry.Callee,
            Entry.bRequiresType ? TEXT(", type: FEdGraphPinType") : TEXT(""),
            Entry.Type.IsEmpty() ? *Entry.Callee : *Entry.Type);
        Builder.AddComment(Schema);
    }
}

void AppendCursorToken(FString& Out, const TCHAR Prefix, const FString& Text)
{
    Out.AppendChar(Prefix);
    Out += LexToString(Text.Len());
    Out.AppendChar(TEXT(':'));
    Out += Text;
}

void AppendCanonicalJson(FString& Out, const TSharedPtr<FJsonValue>& Json)
{
    if (!Json.IsValid() || Json->IsNull())
    {
        Out += TEXT("z;");
        return;
    }
    FString String;
    bool bBool = false;
    double Number = 0.0;
    if (Json->TryGetString(String))
    {
        AppendCursorToken(Out, TEXT('s'), String);
        return;
    }
    if (Json->TryGetBool(bBool))
    {
        Out += bBool ? TEXT("b1;") : TEXT("b0;");
        return;
    }
    if (Json->TryGetNumber(Number))
    {
        AppendCursorToken(Out, TEXT('n'), FString::SanitizeFloat(Number));
        return;
    }
    const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    if (Json->TryGetArray(Array) && Array != nullptr)
    {
        Out += TEXT("a[");
        for (const TSharedPtr<FJsonValue>& Item : *Array)
        {
            AppendCanonicalJson(Out, Item);
        }
        Out += TEXT("];");
        return;
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (Json->TryGetObject(Object) && Object != nullptr && (*Object).IsValid())
    {
        TArray<FString> Keys;
        (*Object)->Values.GetKeys(Keys);
        Keys.Sort();
        Out += TEXT("o{");
        for (const FString& Key : Keys)
        {
            AppendCursorToken(Out, TEXT('k'), Key);
            AppendCanonicalJson(Out, (*Object)->Values.FindRef(Key));
        }
        Out += TEXT("};");
        return;
    }
    Out += TEXT("u;");
}

FString BlueprintCursorFingerprint(UBlueprint* Blueprint, const FSalQuery& Query)
{
    FString Canonical;
    AppendCursorToken(Canonical, TEXT('t'), Blueprint != nullptr ? Blueprint->GetPathName() : FString());
    AppendCursorToken(Canonical, TEXT('g'), Blueprint != nullptr ? GuidText(Blueprint->GetBlueprintGuid()) : FString());
    AppendCanonicalJson(Canonical, MakeShared<FJsonValueObject>(Query.Operation));
    AppendCanonicalJson(Canonical, Query.Where.IsValid()
        ? MakeShared<FJsonValueObject>(Query.Where)
        : Value::Null());
    TArray<TSharedPtr<FJsonValue>> Order;
    for (const TSharedPtr<FJsonObject>& Item : Query.OrderBy)
    {
        Order.Add(MakeShared<FJsonValueObject>(Item));
    }
    AppendCanonicalJson(Canonical, MakeShared<FJsonValueArray>(Order));
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(*Canonical, Canonical.Len() * sizeof(TCHAR), Digest);
    return BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
}

FString EncodeBlueprintCursor(UBlueprint* Blueprint, const FSalQuery& Query, const int32 Offset)
{
    return TEXT("blueprint:") + BlueprintCursorFingerprint(Blueprint, Query) + TEXT(":") + LexToString(Offset);
}

bool DecodeBlueprintPage(
    UBlueprint* Blueprint,
    const FSalQuery& Query,
    const int32 DefaultLimit,
    const int32 MaxLimit,
    FSalPage& OutPage)
{
    OutPage.Offset = 0;
    OutPage.Limit = FMath::Clamp(Query.PageLimit > 0 ? Query.PageLimit : DefaultLimit, 1, MaxLimit);
    if (Query.PageAfter.IsEmpty())
    {
        return true;
    }
    TArray<FString> Parts;
    Query.PageAfter.ParseIntoArray(Parts, TEXT(":"), false);
    if (Parts.Num() != 3
        || Parts[0] != TEXT("blueprint")
        || Parts[1] != BlueprintCursorFingerprint(Blueprint, Query)
        || !ParseNonNegativeInt32(Parts[2], OutPage.Offset))
    {
        return false;
    }
    return true;
}

TSharedPtr<FJsonObject> MakeBlueprintPageResult(
    const TSharedPtr<FJsonObject>& Result,
    UBlueprint* Blueprint,
    const FSalQuery& Query,
    const int32 NextOffset,
    const bool bHasNext)
{
    if (Result.IsValid() && bHasNext)
    {
        TSharedPtr<FJsonObject> Page = MakeShared<FJsonObject>();
        Page->SetStringField(TEXT("next"), EncodeBlueprintCursor(Blueprint, Query, NextOffset));
        Result->SetObjectField(TEXT("page"), Page);
    }
    return Result;
}
}

TSharedPtr<FJsonObject> FSalBlueprintInterface::Query(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target)
{
    UBlueprint* Blueprint = Target.Blueprint;
    if (Blueprint == nullptr || Target.Kind != ESalTargetKind::Blueprint)
    {
        return QueryError(
            TEXT("capability.interface_unavailable"),
            TEXT("The blueprint interface requires a resolved Blueprint target."),
            FString(),
            Target.AssetPath);
    }

    FString Operation;
    if (!ReadKind(Query.Operation, Operation))
    {
        return QueryError(TEXT("capability.unsupported_query_operation"), TEXT("Blueprint query has no supported primary operation."));
    }

    const bool bSchema = HasDetail(Query, TEXT("schema"));
    for (const FString& Detail : Query.With)
    {
        if (Detail != TEXT("schema"))
        {
            return QueryError(
                TEXT("capability.detail_unavailable"),
                FString::Printf(TEXT("Blueprint does not support with %s for %s."), *Detail, *Operation),
                Operation,
                Detail,
                TEXT("Use with schema only on an exact object or exact Palette entry."));
        }
    }

    if (Operation == TEXT("summary"))
    {
        if (bSchema || Query.Where.IsValid() || !Query.OrderBy.IsEmpty() || Query.PageLimit > 0 || !Query.PageAfter.IsEmpty())
        {
            return QueryError(
                TEXT("capability.clause_unavailable"),
                TEXT("Blueprint summary accepts no query clauses."),
                Operation);
        }
        FSalObjectBuilder Builder;
        AddBlueprintObject(Builder, Blueprint, Query.Alias, false);
        int32 VariableCount = 0;
        int32 DispatcherCount = 0;
        for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
        {
            IsDispatcher(Variable) ? ++DispatcherCount : ++VariableCount;
        }
        const int32 ComponentCount = Blueprint->SimpleConstructionScript != nullptr
            ? Blueprint->SimpleConstructionScript->GetAllNodes().Num()
            : 0;
        Builder.AddComment(FString::Printf(TEXT("variables: %d"), VariableCount));
        Builder.AddComment(FString::Printf(TEXT("dispatchers: %d"), DispatcherCount));
        Builder.AddComment(FString::Printf(TEXT("graphs: %d"), BlueprintGraphs(Blueprint).Num()));
        Builder.AddComment(FString::Printf(TEXT("components: %d"), ComponentCount));
        return Builder.BuildResult();
    }

    if (Operation == TEXT("palette_entries"))
    {
        if (bSchema || Query.Where.IsValid() || !Query.OrderBy.IsEmpty())
        {
            return QueryError(
                TEXT("capability.clause_unavailable"),
                TEXT("Blueprint palette search accepts only search text and pagination."),
                Operation);
        }
        FString Search;
        Query.Operation->TryGetStringField(TEXT("text"), Search);
        TArray<FPaletteEntry> Entries = PaletteEntries(Blueprint);
        if (!Search.IsEmpty())
        {
            Entries = Entries.FilterByPredicate([&Search](const FPaletteEntry& Entry)
            {
                return Entry.Label.Contains(Search, ESearchCase::IgnoreCase)
                    || Entry.Type.Contains(Search, ESearchCase::IgnoreCase)
                    || Entry.Callee.Contains(Search, ESearchCase::IgnoreCase);
            });
        }
        FSalPage Page;
        if (!DecodeBlueprintPage(Blueprint, Query, 50, 200, Page))
        {
            return QueryError(
                TEXT("validation.invalid_cursor"),
                TEXT("Blueprint cursor does not belong to this target, operation, search, filter, or order."),
                Operation,
                Query.PageAfter,
                TEXT("Restart the same query without page after."));
        }
        FSalObjectBuilder Builder;
        const int32 Start = FMath::Min(Page.Offset, Entries.Num());
        const int32 End = static_cast<int32>(FMath::Min<int64>(static_cast<int64>(Start) + Page.Limit, Entries.Num()));
        for (int32 Index = Start; Index < End; ++Index)
        {
            EmitPalette(Builder, Entries[Index], false);
        }
        return MakeBlueprintPageResult(Builder.BuildResult(), Blueprint, Query, End, End < Entries.Num());
    }

    if (Operation == TEXT("palette"))
    {
        if (Query.Where.IsValid() || !Query.OrderBy.IsEmpty() || Query.PageLimit > 0 || !Query.PageAfter.IsEmpty())
        {
            return QueryError(
                TEXT("capability.clause_unavailable"),
                TEXT("Exact Blueprint Palette lookup accepts only with schema."),
                Operation);
        }
        FString Id;
        Query.Operation->TryGetStringField(TEXT("id"), Id);
        for (const FPaletteEntry& Entry : PaletteEntries(Blueprint))
        {
            if (Entry.Id == Id)
            {
                FSalObjectBuilder Builder;
                EmitPalette(Builder, Entry, bSchema);
                return Builder.BuildResult();
            }
        }
        return QueryError(
            TEXT("resolution.palette_entry_not_found"),
            TEXT("Palette entry is unavailable in the current Blueprint context."),
            Operation,
            Id,
            TEXT("Run palette entries again and use a current id."));
    }

    const bool bCollection = Operation == TEXT("variables")
        || Operation == TEXT("dispatchers")
        || Operation == TEXT("graphs")
        || Operation == TEXT("components");
    if (bCollection)
    {
        if (bSchema)
        {
            return QueryError(
                TEXT("capability.detail_unavailable"),
                TEXT("Blueprint collections remain compact and do not accept with schema."),
                Operation);
        }
        TArray<FQueryItem> Items = CollectItems(Blueprint, Operation);
        FString Search;
        Query.Operation->TryGetStringField(TEXT("text"), Search);
        if (!Search.IsEmpty())
        {
            Items = Items.FilterByPredicate([&Search](const FQueryItem& Item)
            {
                return Item.Search.Contains(Search, ESearchCase::IgnoreCase);
            });
        }
        if (Query.Where.IsValid())
        {
            bool bSupported = true;
            Items = Items.FilterByPredicate([&Query, &bSupported](const FQueryItem& Item)
            {
                return MatchesWhere(Item, Query.Where, bSupported);
            });
            if (!bSupported)
            {
                return QueryError(
                    TEXT("capability.condition_unavailable"),
                    TEXT("Blueprint collections support only = and != on name, id, and type."),
                    Operation);
            }
        }
        for (const TSharedPtr<FJsonObject>& Order : Query.OrderBy)
        {
            FString Key;
            Order->TryGetStringField(TEXT("key"), Key);
            if (Key != TEXT("name") && Key != TEXT("id") && Key != TEXT("type"))
            {
                return QueryError(
                    TEXT("capability.order_unavailable"),
                    TEXT("Blueprint collections order only by name, id, or type."),
                    Operation,
                    Key);
            }
        }
        SortItems(Items, Query.OrderBy);
        FSalPage Page;
        if (!DecodeBlueprintPage(Blueprint, Query, 50, 200, Page))
        {
            return QueryError(
                TEXT("validation.invalid_cursor"),
                TEXT("Blueprint cursor does not belong to this target, operation, search, filter, or order."),
                Operation,
                Query.PageAfter,
                TEXT("Restart the same query without page after."));
        }
        const int32 Start = FMath::Min(Page.Offset, Items.Num());
        const int32 End = static_cast<int32>(FMath::Min<int64>(static_cast<int64>(Start) + Page.Limit, Items.Num()));
        FSalObjectBuilder Builder;
        const FBlueprintOutputContext Context = AddBlueprintObject(Builder, Blueprint, Query.Alias, false);
        for (int32 Index = Start; Index < End; ++Index)
        {
            EmitItem(Builder, Blueprint, Context.BlueprintAlias, Items[Index], false);
        }
        return MakeBlueprintPageResult(Builder.BuildResult(), Blueprint, Query, End, End < Items.Num());
    }

    FString ExactKind = Operation;
    FString ExactName;
    FString ExactId;
    const bool bByName = Operation == TEXT("variable")
        || Operation == TEXT("dispatcher")
        || Operation == TEXT("graph")
        || Operation == TEXT("component");
    const bool bById = Operation == TEXT("blueprint")
        || bByName;
    if (bByName && Query.Operation->TryGetStringField(TEXT("name"), ExactName))
    {
        ExactId.Reset();
    }
    else if (bById && Query.Operation->TryGetStringField(TEXT("id"), ExactId))
    {
        ExactName.Reset();
    }
    else
    {
        return QueryError(
            TEXT("capability.operation_unavailable"),
            FString::Printf(TEXT("Blueprint does not support query operation %s."), *Operation),
            Operation,
            FString(),
            TEXT("Use summary, a Blueprint collection, an exact concrete object, or palette."));
    }
    if (Query.Where.IsValid() || !Query.OrderBy.IsEmpty() || Query.PageLimit > 0 || !Query.PageAfter.IsEmpty())
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Exact Blueprint object lookup accepts only with schema."),
            Operation);
    }

    FSalObjectBuilder Builder;
    const FBlueprintOutputContext Context = AddBlueprintObject(
        Builder,
        Blueprint,
        Query.Alias,
        Operation == TEXT("blueprint"));
    if (Operation == TEXT("blueprint"))
    {
        if (!GuidText(Blueprint->GetBlueprintGuid()).Equals(ExactId, ESearchCase::IgnoreCase))
        {
            return QueryError(TEXT("resolution.object_not_found"), TEXT("Blueprint id does not match the bound target."), Operation, ExactId);
        }
        if (bSchema)
        {
            Builder.AddComment(BlueprintSchema(Blueprint));
        }
        return Builder.BuildResult();
    }

    if (Operation == TEXT("variable") || Operation == TEXT("dispatcher"))
    {
        const bool bDispatcher = Operation == TEXT("dispatcher");
        FBPVariableDescription* Variable = ExactId.IsEmpty()
            ? FindVariableByName(Blueprint, ExactName, bDispatcher)
            : FindVariable(Blueprint, ExactId, bDispatcher);
        if (Variable == nullptr)
        {
            return QueryError(TEXT("resolution.object_not_found"), TEXT("Blueprint declaration was not found or its name is ambiguous."), Operation, ExactId.IsEmpty() ? ExactName : ExactId);
        }
        AddVariableBinding(Builder, Context.BlueprintAlias, *Variable, bDispatcher);
        if (bDispatcher)
        {
            if (UEdGraph* Signature = FindDispatcherGraph(Blueprint, Variable->VarName.ToString()))
            {
                AddGraphBinding(Builder, Context.BlueprintAlias, Signature);
            }
            else
            {
                Builder.AddComment(TEXT("inconsistent Blueprint: Dispatcher Signature Graph is missing"));
            }
        }
        if (bSchema)
        {
            Builder.AddComment(VariableSchema(bDispatcher));
        }
        return Builder.BuildResult();
    }

    if (Operation == TEXT("graph"))
    {
        UEdGraph* Graph = ExactId.IsEmpty() ? FindGraphByName(Blueprint, ExactName) : FindGraph(Blueprint, ExactId);
        if (Graph == nullptr)
        {
            return QueryError(TEXT("resolution.object_not_found"), TEXT("Blueprint Graph was not found or its name is ambiguous."), Operation, ExactId.IsEmpty() ? ExactName : ExactId);
        }
        AddGraphBinding(Builder, Context.BlueprintAlias, Graph);
        if (bSchema)
        {
            Builder.AddComment(GraphSchema(Graph));
        }
        return Builder.BuildResult();
    }

    USCS_Node* Component = ExactId.IsEmpty() ? FindComponentByName(Blueprint, ExactName) : FindComponent(Blueprint, ExactId);
    if (Component == nullptr)
    {
        return QueryError(TEXT("resolution.object_not_found"), TEXT("Blueprint Component was not found or its name is ambiguous."), Operation, ExactId.IsEmpty() ? ExactName : ExactId);
    }
    if (Blueprint->SimpleConstructionScript != nullptr)
    {
        TArray<USCS_Node*> Ancestors;
        for (USCS_Node* Parent = Blueprint->SimpleConstructionScript->FindParentNode(Component);
             Parent != nullptr;
             Parent = Blueprint->SimpleConstructionScript->FindParentNode(Parent))
        {
            Ancestors.Insert(Parent, 0);
        }
        for (USCS_Node* Ancestor : Ancestors)
        {
            AddComponentBinding(Builder, Blueprint, Context.BlueprintAlias, Ancestor, false);
        }
    }
    AddComponentBinding(Builder, Blueprint, Context.BlueprintAlias, Component, true);
    if (bSchema)
    {
        Builder.AddComment(ComponentSchema(Component));
    }
    return Builder.BuildResult();
}

namespace
{
struct FPendingBinding
{
    FString Key;
    FString Name;
    FString OwnerAlias;
    int32 MemberDepth = 0;
    FString Callee;
    FString Palette;
    TSharedPtr<FJsonObject> Args;
    bool bMemberTarget = false;
};

struct FCreatedObject
{
    FString Kind;
    FString StableId;
    UClass* PlannedClass = nullptr;
    FBPVariableDescription* Variable = nullptr;
    UEdGraph* Graph = nullptr;
    UEdGraphNode* Node = nullptr;
    USCS_Node* Component = nullptr;
};

bool DecodeBinding(const TSharedPtr<FJsonObject>& Statement, FPendingBinding& Out)
{
    const TSharedPtr<FJsonObject>* Target = nullptr;
    const TSharedPtr<FJsonObject>* CallObject = nullptr;
    const TSharedPtr<FJsonObject>* Args = nullptr;
    if (!Statement.IsValid()
        || Statement->HasField(TEXT("kind"))
        || !Statement->TryGetObjectField(TEXT("target"), Target)
        || Target == nullptr
        || !(*Target).IsValid()
        || !Statement->TryGetObjectField(TEXT("value"), CallObject)
        || CallObject == nullptr
        || !(*CallObject).IsValid())
    {
        return false;
    }
    FString CallKind;
    if (!ReadKind(*CallObject, CallKind)
        || CallKind != TEXT("call")
        || !(*CallObject)->TryGetStringField(TEXT("callee"), Out.Callee)
        || !(*CallObject)->TryGetObjectField(TEXT("args"), Args)
        || Args == nullptr)
    {
        return false;
    }
    Out.Args = *Args;
    Out.Args->TryGetStringField(TEXT("palette"), Out.Palette);

    FString TargetKind;
    if (!ReadKind(*Target, TargetKind))
    {
        return false;
    }
    if (TargetKind == TEXT("local"))
    {
        if (!(*Target)->TryGetStringField(TEXT("name"), Out.Name))
        {
            return false;
        }
        Out.Key = Out.Name;
        return true;
    }
    if (TargetKind == TEXT("member"))
    {
        TSharedPtr<FJsonObject> Owner;
        TArray<FString> Path;
        FString OwnerKind;
        FString OwnerName;
        if (!ReadMember(*Target, Owner, Path)
            || !ReadRef(Owner, OwnerKind, OwnerName)
            || OwnerKind != TEXT("local"))
        {
            return false;
        }
        Out.Name = Path.Last();
        Out.OwnerAlias = OwnerName;
        Out.MemberDepth = Path.Num();
        Out.Key = OwnerName + TEXT(".") + FString::Join(Path, TEXT("."));
        Out.bMemberTarget = true;
        return true;
    }
    return false;
}

FString RefKey(const TSharedPtr<FJsonObject>& Ref)
{
    FString Kind;
    FString Identity;
    if (ReadRef(Ref, Kind, Identity))
    {
        return Kind == TEXT("local") ? Identity : Kind + TEXT("@") + Identity;
    }
    TSharedPtr<FJsonObject> Owner;
    TArray<FString> Path;
    if (ReadMember(Ref, Owner, Path) && ReadRef(Owner, Kind, Identity))
    {
        return Identity + TEXT(".") + FString::Join(Path, TEXT("."));
    }
    return FString();
}

bool NameExists(UBlueprint* Blueprint, const FString& Name)
{
    if (Blueprint == nullptr || Name.IsEmpty())
    {
        return true;
    }
    const FName Candidate(*Name);
    for (UEdGraph* Graph : BlueprintGraphs(Blueprint))
    {
        if (Graph != nullptr && Graph->GetFName() == Candidate)
        {
            return true;
        }
    }
    if (Blueprint->SimpleConstructionScript != nullptr)
    {
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node != nullptr && Node->GetVariableName() == Candidate)
            {
                return true;
            }
        }
    }
    for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        if (Variable.VarName == Candidate)
        {
            return true;
        }
    }
    return false;
}

UBlueprint* PersistentBlueprintForPreflight(UBlueprint* Blueprint)
{
    if (Blueprint == nullptr || Blueprint->GetOutermost() != GetTransientPackage())
    {
        return Blueprint;
    }
    const FGuid Guid = Blueprint->GetBlueprintGuid();
    UBlueprint* Match = nullptr;
    for (TObjectIterator<UBlueprint> It; It; ++It)
    {
        UBlueprint* Candidate = *It;
        if (Candidate == nullptr
            || Candidate == Blueprint
            || Candidate->GetOutermost() == GetTransientPackage()
            || Candidate->GetBlueprintGuid() != Guid)
        {
            continue;
        }
        if (Match != nullptr)
        {
            return nullptr;
        }
        Match = Candidate;
    }
    return Match;
}

bool FindChildNameCollision(UBlueprint* Blueprint, const FName Name, FString& OutMessage)
{
    UBlueprint* PersistentBlueprint = PersistentBlueprintForPreflight(Blueprint);
    if (PersistentBlueprint == nullptr)
    {
        OutMessage = TEXT("The persistent Blueprint could not be resolved uniquely for child-name preflight.");
        return true;
    }
    TArray<FAssetData> ChildAssets;
    FBlueprintEditorUtils::GetChildrenOfBlueprint(PersistentBlueprint, ChildAssets, true);
    for (const FAssetData& ChildAsset : ChildAssets)
    {
        UBlueprint* Child = Cast<UBlueprint>(ChildAsset.GetAsset());
        if (Child == nullptr)
        {
            OutMessage = FString::Printf(
                TEXT("Child Blueprint could not be loaded for exact name-collision preflight: %s."),
                *ChildAsset.GetObjectPathString());
            return true;
        }
        FString CollisionKind;
        for (const FBPVariableDescription& Variable : Child->NewVariables)
        {
            if (Variable.VarName == Name)
            {
                CollisionKind = IsDispatcher(Variable) ? TEXT("Dispatcher") : TEXT("Variable");
                break;
            }
        }
        if (CollisionKind.IsEmpty() && Child->SimpleConstructionScript != nullptr)
        {
            for (const USCS_Node* Node : Child->SimpleConstructionScript->GetAllNodes())
            {
                if (Node != nullptr && Node->GetVariableName() == Name)
                {
                    CollisionKind = TEXT("Component");
                    break;
                }
            }
        }
        if (CollisionKind.IsEmpty())
        {
            for (const UTimelineTemplate* Timeline : Child->Timelines)
            {
                if (Timeline != nullptr && Timeline->GetFName() == Name)
                {
                    CollisionKind = TEXT("Timeline");
                    break;
                }
            }
        }
        if (CollisionKind.IsEmpty())
        {
            for (const UEdGraph* Graph : Child->FunctionGraphs)
            {
                if (Graph != nullptr && Graph->GetFName() == Name)
                {
                    CollisionKind = TEXT("Function Graph");
                    break;
                }
            }
        }
        if (!CollisionKind.IsEmpty())
        {
            OutMessage = FString::Printf(
                TEXT("Name %s conflicts with child %s %s in %s; UE would silently rename that child object."),
                *Name.ToString(),
                *CollisionKind,
                *Name.ToString(),
                *Child->GetPathName());
            return true;
        }
    }
    return false;
}

UClass* ResolveComponentPalette(const FString& Palette)
{
    if (!Palette.StartsWith(ComponentPalettePrefix))
    {
        return nullptr;
    }
    const FString ClassPath = Palette.Mid(FCString::Strlen(ComponentPalettePrefix));
    UClass* Class = FindObject<UClass>(nullptr, *ClassPath);
    if (Class == nullptr)
    {
        Class = LoadObject<UClass>(nullptr, *ClassPath);
    }
    return Class != nullptr && Class->IsChildOf(UActorComponent::StaticClass()) ? Class : nullptr;
}

FProperty* ConstructorProperty(const FPendingBinding& Binding, const FString& Field)
{
    if (Binding.Callee == TEXT("variable") || Binding.Callee == TEXT("dispatcher"))
    {
        return FBPVariableDescription::StaticStruct()->FindPropertyByName(FName(*Field));
    }
    if (Binding.Callee == TEXT("graph"))
    {
        return FindFProperty<FProperty>(UEdGraph::StaticClass(), *Field);
    }
    if (Binding.Callee == TEXT("component"))
    {
        if (UClass* Class = ResolveComponentPalette(Binding.Palette))
        {
            return FindFProperty<FProperty>(Class, *Field);
        }
    }
    return nullptr;
}

bool IsWritableDeclarationField(const FString& Field, const bool bDispatcher)
{
    static const TSet<FString> WritableFields = {
        TEXT("FriendlyName"),
        TEXT("Category"),
        TEXT("PropertyFlags"),
        TEXT("RepNotifyFunc"),
        TEXT("ReplicationCondition"),
        TEXT("MetaDataArray")};
    return WritableFields.Contains(Field) || (!bDispatcher && Field == TEXT("DefaultValue"));
}

bool ValidateConstructorFields(const FPendingBinding& Binding, FString& OutMessage)
{
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Binding.Args->Values)
    {
        if (Pair.Key == TEXT("palette") || Pair.Key == TEXT("type"))
        {
            continue;
        }
        if (Pair.Key == TEXT("id") || Pair.Key == TEXT("name") || Pair.Key == TEXT("VarName"))
        {
            OutMessage = FString::Printf(TEXT("Constructor field is derived from the binding and cannot be assigned: %s."), *Pair.Key);
            return false;
        }
        if (Binding.Callee == TEXT("variable") || Binding.Callee == TEXT("dispatcher"))
        {
            OutMessage = FString::Printf(
                TEXT("Declaration constructors contain only Palette-owned creation parameters; set %s after add."),
                *Pair.Key);
            return false;
        }
        if (Binding.Callee == TEXT("graph"))
        {
            OutMessage = FString::Printf(TEXT("Graph Palette determines creation state; constructor field is unavailable: %s."), *Pair.Key);
            return false;
        }
        FProperty* Property = ConstructorProperty(Binding, Pair.Key);
        UClass* ComponentClass = Binding.Callee == TEXT("component")
            ? ResolveComponentPalette(Binding.Palette)
            : nullptr;
        const UObject* ComponentDefaults = Binding.Callee == TEXT("component")
            && ComponentClass != nullptr
            ? ComponentClass->GetDefaultObject()
            : nullptr;
        if (Property == nullptr
            || (Binding.Callee == TEXT("component")
                && !CanWriteNativeTemplateField(ComponentDefaults, Property))
            || (Binding.Callee != TEXT("component")
                && Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_Deprecated)))
        {
            OutMessage = FString::Printf(TEXT("Constructor field is unavailable or read-only: %s."), *Pair.Key);
            return false;
        }
        const FString NativeText = NativeTextForProperty(Property, Pair.Value);
        if (NativeText.IsEmpty() && !Pair.Value->IsNull())
        {
            OutMessage = FString::Printf(TEXT("Constructor field cannot be represented as native text: %s."), *Pair.Key);
            return false;
        }
        TArray<uint8> Temp;
        Temp.SetNumZeroed(Property->GetSize());
        Property->InitializeValue(Temp.GetData());
        const TCHAR* End = Property->ImportText_Direct(*NativeText, Temp.GetData(), nullptr, PPF_None, GLog);
        bool bValid = End != nullptr;
        if (End != nullptr)
        {
            while (*End != TEXT('\0') && FChar::IsWhitespace(*End))
            {
                ++End;
            }
            bValid = *End == TEXT('\0');
        }
        Property->DestroyValue(Temp.GetData());
        if (!bValid)
        {
            OutMessage = FString::Printf(TEXT("UE rejected the native constructor value for %s."), *Pair.Key);
            return false;
        }
    }
    return true;
}

bool ApplyConstructorFields(
    UBlueprint* Blueprint,
    const FPendingBinding& Binding,
    const FCreatedObject& Created,
    FString& OutMessage)
{
    void* Container = nullptr;
    UObject* Object = nullptr;
    if (Created.Variable != nullptr)
    {
        Container = Created.Variable;
    }
    else if (Created.Graph != nullptr)
    {
        Container = Created.Graph;
        Object = Created.Graph;
    }
    else if (Created.Component != nullptr && Created.Component->ComponentTemplate != nullptr)
    {
        Container = Created.Component->ComponentTemplate;
        Object = Created.Component->ComponentTemplate;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Binding.Args->Values)
    {
        if (Pair.Key == TEXT("palette") || Pair.Key == TEXT("type"))
        {
            continue;
        }
        FProperty* Property = ConstructorProperty(Binding, Pair.Key);
        if (Property == nullptr || Container == nullptr)
        {
            OutMessage = FString::Printf(TEXT("Constructor field disappeared during apply: %s."), *Pair.Key);
            return false;
        }
        if (Created.Component != nullptr && !CanWriteNativeTemplateField(Object, Property))
        {
            OutMessage = FString::Printf(TEXT("Constructor field is no longer writable on the created Component template: %s."), *Pair.Key);
            return false;
        }
        if (Object != nullptr)
        {
            Object->Modify();
        }
        if (!ImportPropertyValue(
                Property,
                Container,
                NativeTextForProperty(Property, Pair.Value),
                OutMessage))
        {
            return false;
        }
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

bool ValidateBinding(
    UBlueprint* Blueprint,
    const FPendingBinding& Binding,
    const FString& TargetAlias,
    FString& OutMessage)
{
    if (Binding.Palette.IsEmpty())
    {
        OutMessage = TEXT("Every direct creation binding requires a Palette id.");
        return false;
    }
    if (NameExists(Blueprint, Binding.Name))
    {
        OutMessage = FString::Printf(TEXT("Blueprint name is already in use: %s."), *Binding.Name);
        return false;
    }
    if (Binding.Name.Len() >= NAME_SIZE
        || FKismetNameValidator(Blueprint).IsValid(Binding.Name) != EValidatorResult::Ok)
    {
        OutMessage = TEXT("The requested name is not accepted by UE's Kismet name validator.");
        return false;
    }
    if (FindChildNameCollision(Blueprint, FName(*Binding.Name), OutMessage))
    {
        return false;
    }
    if (Binding.Callee == TEXT("variable"))
    {
        if (!Binding.bMemberTarget
            || Binding.OwnerAlias != TargetAlias
            || Binding.MemberDepth != 1
            || Binding.Palette != VariablePaletteId
            || !Blueprint->SupportsGlobalVariables())
        {
            OutMessage = TEXT("Variable creation requires a Blueprint member binding and the current Variable Palette entry.");
            return false;
        }
        FString TypeText;
        FEdGraphPinType PinType;
        if (!Binding.Args->TryGetStringField(TEXT("type"), TypeText) || !ParsePinType(TypeText, PinType))
        {
            OutMessage = TEXT("Variable constructor type is not valid FEdGraphPinType native text.");
            return false;
        }
        return ValidateConstructorFields(Binding, OutMessage);
    }
    if (Binding.Callee == TEXT("dispatcher"))
    {
        if (!Binding.bMemberTarget
            || Binding.OwnerAlias != TargetAlias
            || Binding.MemberDepth != 1
            || Binding.Palette != DispatcherPaletteId)
        {
            OutMessage = TEXT("Dispatcher creation requires a Blueprint member binding and the current Dispatcher Palette entry.");
            return false;
        }
        const bool bAvailable = Blueprint->BlueprintType != BPTYPE_Interface
            && Blueprint->BlueprintType != BPTYPE_MacroLibrary
            && Blueprint->BlueprintType != BPTYPE_FunctionLibrary
            && Blueprint->SupportsDelegates();
        if (!bAvailable)
        {
            OutMessage = TEXT("This Blueprint type cannot own Event Dispatchers.");
        }
        return bAvailable && ValidateConstructorFields(Binding, OutMessage);
    }
    if (Binding.Callee == TEXT("graph"))
    {
        if (Binding.bMemberTarget
            || (Binding.Palette != FunctionGraphPaletteId
                && Binding.Palette != MacroGraphPaletteId
                && Binding.Palette != EventGraphPaletteId))
        {
            OutMessage = TEXT("Graph creation requires a local binding and a current Blueprint Graph Palette entry.");
            return false;
        }
        if ((Binding.Palette == FunctionGraphPaletteId && !Blueprint->SupportsFunctions())
            || (Binding.Palette == MacroGraphPaletteId && !Blueprint->SupportsMacros())
            || (Binding.Palette == EventGraphPaletteId && !Blueprint->SupportsEventGraphs()))
        {
            OutMessage = TEXT("The selected Graph capability is unavailable on this concrete Blueprint type.");
            return false;
        }
        return ValidateConstructorFields(Binding, OutMessage);
    }
    if (Binding.Callee == TEXT("component"))
    {
        UClass* Class = ResolveComponentPalette(Binding.Palette);
        if (Binding.bMemberTarget || Blueprint->SimpleConstructionScript == nullptr || Class == nullptr)
        {
            OutMessage = TEXT("Component creation requires a local binding and a current Component Palette entry on an SCS Blueprint.");
            return false;
        }
        if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        {
            OutMessage = TEXT("The Component Class is not currently constructible.");
            return false;
        }
        if (!FKismetEditorUtilities::IsClassABlueprintSpawnableComponent(Class))
        {
            OutMessage = TEXT("The Component Class is not exposed as a Blueprint-spawnable component.");
            return false;
        }
        const UActorComponent* DefaultComponent = Cast<UActorComponent>(Class->GetDefaultObject());
        if (DefaultComponent == nullptr
            || !FComponentEditorUtils::IsValidVariableNameString(DefaultComponent, Binding.Name)
            || Binding.Name.Len() >= NAME_SIZE)
        {
            OutMessage = TEXT("The requested Component name is not accepted by UE's Component and Kismet name validators.");
            return false;
        }
        return ValidateConstructorFields(Binding, OutMessage);
    }
    OutMessage = FString::Printf(TEXT("Blueprint Palette cannot create %s."), *Binding.Callee);
    return false;
}

bool ValidateAddPlacement(
    UBlueprint* Blueprint,
    const FPendingBinding& Binding,
    const TSharedPtr<FJsonObject>& Add,
    FString& OutMessage)
{
    const bool bHasTo = Add->HasField(TEXT("to"));
    const bool bHasBefore = Add->HasField(TEXT("before"));
    const bool bHasAfter = Add->HasField(TEXT("after"));
    if (Binding.Callee != TEXT("component"))
    {
        if (bHasTo || bHasBefore || bHasAfter)
        {
            OutMessage = TEXT("This Blueprint constructor has no placement form; use bare add.");
            return false;
        }
        return true;
    }
    if (bHasBefore || bHasAfter)
    {
        OutMessage = TEXT("Component creation supports only bare add or add to component@id.");
        return false;
    }
    if (!bHasTo)
    {
        return true;
    }
    const TSharedPtr<FJsonObject>* Destination = nullptr;
    FString Kind;
    FString Id;
    if (!Add->TryGetObjectField(TEXT("to"), Destination)
        || Destination == nullptr
        || !ReadRef(*Destination, Kind, Id)
        || Kind != TEXT("component"))
    {
        OutMessage = TEXT("Component add destination must be component@id.");
        return false;
    }
    UClass* Class = ResolveComponentPalette(Binding.Palette);
    USCS_Node* Parent = FindComponent(Blueprint, Id);
    const USceneComponent* ChildDefaults = Class != nullptr
        ? Cast<USceneComponent>(Class->GetDefaultObject())
        : nullptr;
    const USceneComponent* ParentTemplate = Parent != nullptr
        ? Cast<USceneComponent>(Parent->ComponentTemplate)
        : nullptr;
    if (Parent == nullptr
        || ChildDefaults == nullptr
        || ParentTemplate == nullptr
        || !ParentTemplate->CanAttachAsChild(ChildDefaults, NAME_None)
        || ChildDefaults->Mobility < ParentTemplate->Mobility
        || (ParentTemplate->IsEditorOnly() && !ChildDefaults->IsEditorOnly()))
    {
        OutMessage = TEXT("UE Designer attachment, Mobility, or Editor-only rules reject the explicit Component parent.");
        return false;
    }
    return true;
}

bool ApplyBinding(
    UBlueprint* Blueprint,
    const FPendingBinding& Binding,
    const TSharedPtr<FJsonObject>& Add,
    FCreatedObject& OutCreated,
    FString& OutMessage)
{
    Blueprint->Modify();
    if (Binding.Callee == TEXT("variable"))
    {
        FString TypeText;
        Binding.Args->TryGetStringField(TEXT("type"), TypeText);
        FEdGraphPinType PinType;
        if (!ParsePinType(TypeText, PinType)
            || !FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*Binding.Name), PinType))
        {
            OutMessage = TEXT("UE failed to create the Blueprint Variable.");
            return false;
        }
        OutCreated.Kind = TEXT("variable");
        OutCreated.Variable = FindVariableByName(Blueprint, Binding.Name, false);
        if (OutCreated.Variable != nullptr)
        {
            OutCreated.StableId = GuidText(OutCreated.Variable->VarGuid);
        }
        return OutCreated.Variable != nullptr
            && ApplyConstructorFields(Blueprint, Binding, OutCreated, OutMessage);
    }
    if (Binding.Callee == TEXT("dispatcher"))
    {
        FEdGraphPinType DelegateType;
        DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
        if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*Binding.Name), DelegateType))
        {
            OutMessage = TEXT("UE failed to create the Dispatcher declaration.");
            return false;
        }
        const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
        UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(
            Blueprint,
            FName(*Binding.Name),
            UEdGraph::StaticClass(),
            UEdGraphSchema_K2::StaticClass());
        if (Graph == nullptr || Schema == nullptr)
        {
            FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, FName(*Binding.Name));
            OutMessage = TEXT("UE failed to create the Dispatcher Signature Graph.");
            return false;
        }
        Schema->CreateDefaultNodesForGraph(*Graph);
        Schema->CreateFunctionGraphTerminators(*Graph, static_cast<UClass*>(nullptr));
        Schema->AddExtraFunctionFlags(Graph, FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
        Schema->MarkFunctionEntryAsEditable(Graph, true);
        Blueprint->DelegateSignatureGraphs.Add(Graph);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        OutCreated.Kind = TEXT("dispatcher");
        OutCreated.Variable = FindVariableByName(Blueprint, Binding.Name, true);
        OutCreated.Graph = Graph;
        if (OutCreated.Variable != nullptr)
        {
            OutCreated.StableId = GuidText(OutCreated.Variable->VarGuid);
        }
        return OutCreated.Variable != nullptr
            && ApplyConstructorFields(Blueprint, Binding, OutCreated, OutMessage);
    }
    if (Binding.Callee == TEXT("graph"))
    {
        UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(
            Blueprint,
            FName(*Binding.Name),
            UEdGraph::StaticClass(),
            UEdGraphSchema_K2::StaticClass());
        if (Graph == nullptr)
        {
            OutMessage = TEXT("UE failed to create the Graph.");
            return false;
        }
        if (Binding.Palette == FunctionGraphPaletteId)
        {
            FBlueprintEditorUtils::AddFunctionGraph(Blueprint, Graph, true, static_cast<UClass*>(nullptr));
        }
        else if (Binding.Palette == MacroGraphPaletteId)
        {
            FBlueprintEditorUtils::AddMacroGraph(Blueprint, Graph, true, nullptr);
        }
        else
        {
            FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Graph);
        }
        OutCreated.Kind = TEXT("graph");
        OutCreated.Graph = Graph;
        OutCreated.StableId = GuidText(Graph->GraphGuid);
        return ApplyConstructorFields(Blueprint, Binding, OutCreated, OutMessage);
    }
    if (Binding.Callee == TEXT("component"))
    {
        UClass* Class = ResolveComponentPalette(Binding.Palette);
        USubobjectDataSubsystem* System = USubobjectDataSubsystem::Get();
        if (Class == nullptr || Blueprint->SimpleConstructionScript == nullptr || System == nullptr)
        {
            OutMessage = TEXT("Component Palette entry or UE SubobjectDataSubsystem is stale in this Blueprint context.");
            return false;
        }

        TArray<FSubobjectDataHandle> Handles;
        System->K2_GatherSubobjectDataForBlueprint(Blueprint, Handles);
        FSubobjectDataHandle ActorHandle = FSubobjectDataHandle::InvalidHandle;
        for (const FSubobjectDataHandle& Handle : Handles)
        {
            const FSubobjectData* Data = Handle.GetData();
            if (Data != nullptr && Data->IsRootActor())
            {
                ActorHandle = Handle;
                break;
            }
        }
        if (!ActorHandle.IsValid())
        {
            OutMessage = TEXT("UE did not expose the Blueprint Actor subobject context for Component creation.");
            return false;
        }

        auto HandleForNode = [&Handles, Blueprint](const USCS_Node* Node)
        {
            FSubobjectDataHandle NameMatch = FSubobjectDataHandle::InvalidHandle;
            for (const FSubobjectDataHandle& Handle : Handles)
            {
                const FSubobjectData* Data = Handle.GetData();
                if (Data == nullptr || !Data->IsComponent())
                {
                    continue;
                }
                if (Data->GetObjectForBlueprint(Blueprint) == Node->ComponentTemplate
                    || Data->GetComponentTemplate() == Node->ComponentTemplate)
                {
                    return Handle;
                }
                if (Data->GetVariableName() == Node->GetVariableName())
                {
                    NameMatch = Handle;
                }
            }
            return NameMatch;
        };

        USCS_Node* ExplicitParent = nullptr;
        FSubobjectDataHandle ParentHandle = ActorHandle;
        const TSharedPtr<FJsonObject>* Destination = nullptr;
        if (Add->TryGetObjectField(TEXT("to"), Destination) && Destination != nullptr)
        {
            FString Kind;
            FString Id;
            if (!ReadRef(*Destination, Kind, Id) || Kind != TEXT("component"))
            {
                OutMessage = TEXT("Component add destination must be component@id.");
                return false;
            }
            ExplicitParent = FindComponent(Blueprint, Id);
            if (ExplicitParent == nullptr
                || !Class->IsChildOf(USceneComponent::StaticClass())
                || ExplicitParent->ComponentClass == nullptr
                || !ExplicitParent->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
            {
                OutMessage = TEXT("Component add destination is not a compatible Scene Component.");
                return false;
            }
            ParentHandle = HandleForNode(ExplicitParent);
            const FSubobjectData* ParentData = ParentHandle.GetData();
            const USceneComponent* ParentTemplate = ParentData != nullptr
                ? Cast<USceneComponent>(ParentData->GetObjectForBlueprint(Blueprint))
                : nullptr;
            const USceneComponent* ChildDefaults = Cast<USceneComponent>(Class->GetDefaultObject());
            if (ParentData == nullptr
                || ParentTemplate == nullptr
                || ChildDefaults == nullptr
                || !ParentTemplate->CanAttachAsChild(ChildDefaults, NAME_None)
                || ChildDefaults->Mobility < ParentTemplate->Mobility
                || (ParentTemplate->IsEditorOnly() && !ChildDefaults->IsEditorOnly()))
            {
                OutMessage = TEXT("UE Designer attachment, Mobility, or Editor-only rules reject the explicit Component parent.");
                return false;
            }
        }

        FAddNewSubobjectParams Params;
        Params.ParentHandle = ParentHandle;
        Params.NewClass = Class;
        Params.BlueprintContext = Blueprint;
        Params.bConformTransformToParent = true;
        FText FailReason;
        const FSubobjectDataHandle NewHandle = System->AddNewSubobject(Params, FailReason);
        FSubobjectData* NewData = NewHandle.GetData();
        if (NewData == nullptr)
        {
            OutMessage = FailReason.IsEmpty()
                ? TEXT("UE SubobjectDataSubsystem failed to create the Component.")
                : FailReason.ToString();
            return false;
        }
        auto RemovePartialComponent = [&]()
        {
            System->DeleteSubobjects(ActorHandle, {NewHandle}, Blueprint);
        };
        if (ExplicitParent != nullptr && NewData->GetParentHandle() != ParentHandle)
        {
            RemovePartialComponent();
            OutMessage = TEXT("UE selected a fallback Component parent instead of the explicit destination.");
            return false;
        }
        const FText DesiredName = FText::FromString(Binding.Name);
        FText RenameError;
        if (!System->IsValidRename(NewHandle, DesiredName, RenameError)
            || !System->RenameSubobject(NewHandle, DesiredName)
            || NewData->GetVariableName().ToString() != Binding.Name)
        {
            RemovePartialComponent();
            OutMessage = RenameError.IsEmpty()
                ? TEXT("UE did not preserve the exact requested Component name.")
                : RenameError.ToString();
            return false;
        }
        USCS_Node* Node = FindComponentByName(Blueprint, Binding.Name);
        if (Node == nullptr)
        {
            RemovePartialComponent();
            OutMessage = TEXT("Created Component could not be resolved back to its authored SCS object.");
            return false;
        }
        OutCreated.Kind = TEXT("component");
        OutCreated.Component = Node;
        OutCreated.StableId = GuidText(Node->VariableGuid);
        if (!ApplyConstructorFields(Blueprint, Binding, OutCreated, OutMessage))
        {
            RemovePartialComponent();
            OutCreated.Component = nullptr;
            return false;
        }
        return true;
    }
    return false;
}

struct FResolvedField
{
    FString Kind;
    FString StableId;
    FString Field;
    FProperty* Property = nullptr;
    void* Container = nullptr;
    UObject* Object = nullptr;
    FBPVariableDescription* Variable = nullptr;
    UEdGraph* Graph = nullptr;
    USCS_Node* Component = nullptr;
    bool bPlanned = false;
};

bool ResolveExistingField(
    UBlueprint* Blueprint,
    const FString& TargetAlias,
    const TSharedPtr<FJsonObject>& Member,
    const TMap<FString, FCreatedObject>& Created,
    FResolvedField& Out,
    FString& OutMessage)
{
    TSharedPtr<FJsonObject> Owner;
    TArray<FString> Path;
    FString Kind;
    FString Identity;
    if (!ReadMember(Member, Owner, Path) || !ReadRef(Owner, Kind, Identity))
    {
        OutMessage = TEXT("Blueprint field mutation requires one direct field path.");
        return false;
    }
    if (Kind == TEXT("local") && Identity == TargetAlias && Path.Num() == 2)
    {
        const FCreatedObject* Local = Created.Find(Path[0]);
        if (Local == nullptr || (Local->Kind != TEXT("variable") && Local->Kind != TEXT("dispatcher")))
        {
            OutMessage = TEXT("Blueprint member field mutation requires a declaration materialized earlier in this Patch.");
            return false;
        }
        Identity = Path[0];
        Kind = TEXT("local");
        Path.RemoveAt(0);
    }
    if (Path.Num() != 1)
    {
        OutMessage = TEXT("Blueprint field mutation requires one direct field on a resolved object.");
        return false;
    }
    Out.Field = Path[0];
    Out.Kind = Kind;
    Out.StableId = Identity;
    if (Kind == TEXT("local"))
    {
        if (Identity == TargetAlias)
        {
            Out.Kind = TEXT("blueprint");
            Out.Object = Blueprint;
            Out.Container = Blueprint;
            Out.Property = FindFProperty<FProperty>(Blueprint->GetClass(), *Out.Field);
        }
        else if (const FCreatedObject* Local = Created.Find(Identity))
        {
            Out.Kind = Local->Kind;
            Out.bPlanned = Local->Variable == nullptr
                && Local->Graph == nullptr
                && Local->Node == nullptr
                && Local->Component == nullptr;
            if (!Local->StableId.IsEmpty())
            {
                Out.StableId = Local->StableId;
                Identity = Local->StableId;
                Out.Variable = Out.Kind == TEXT("variable") || Out.Kind == TEXT("dispatcher")
                    ? FindVariable(Blueprint, Identity, Out.Kind == TEXT("dispatcher"))
                    : nullptr;
                Out.Graph = Out.Kind == TEXT("graph") ? FindGraph(Blueprint, Identity) : nullptr;
                Out.Component = Out.Kind == TEXT("component") ? FindComponent(Blueprint, Identity) : nullptr;
            }
            else
            {
                Out.Variable = Local->Variable;
                Out.Graph = Local->Graph;
                Out.Component = Local->Component;
            }
            if (Out.bPlanned && Out.Kind == TEXT("component") && Local->PlannedClass != nullptr)
            {
                Out.Object = Local->PlannedClass->GetDefaultObject();
                Out.Container = Out.Object;
                Out.Property = FindFProperty<FProperty>(Local->PlannedClass, *Out.Field);
            }
            else if (Out.bPlanned && Out.Kind == TEXT("graph"))
            {
                Out.Object = UEdGraph::StaticClass()->GetDefaultObject();
                Out.Container = Out.Object;
                Out.Property = FindFProperty<FProperty>(UEdGraph::StaticClass(), *Out.Field);
            }
        }
        else
        {
            OutMessage = FString::Printf(TEXT("Local alias has not materialized yet: %s."), *Identity);
            return false;
        }
    }
    if (Out.Kind == TEXT("variable") || Out.Kind == TEXT("dispatcher"))
    {
        if (Out.Variable == nullptr && !Out.bPlanned)
        {
            Out.Variable = FindVariable(Blueprint, Identity, Out.Kind == TEXT("dispatcher"));
        }
        static FBPVariableDescription PlannedVariable;
        Out.Container = Out.Variable != nullptr ? static_cast<void*>(Out.Variable) : static_cast<void*>(&PlannedVariable);
        Out.Property = FBPVariableDescription::StaticStruct()->FindPropertyByName(FName(*Out.Field));
    }
    else if (Out.Kind == TEXT("graph"))
    {
        if (Out.Graph == nullptr && !Out.bPlanned)
        {
            Out.Graph = FindGraph(Blueprint, Identity);
        }
        if (!Out.bPlanned)
        {
            Out.Object = Out.Graph;
            Out.Container = Out.Graph;
            Out.Property = Out.Graph != nullptr ? FindFProperty<FProperty>(Out.Graph->GetClass(), *Out.Field) : nullptr;
        }
    }
    else if (Out.Kind == TEXT("component"))
    {
        if (Out.Component == nullptr && !Out.bPlanned)
        {
            Out.Component = FindComponent(Blueprint, Identity);
        }
        if (Out.Field == TEXT("name"))
        {
            return Out.Component != nullptr;
        }
        if (!Out.bPlanned && Out.Component != nullptr && Out.Component->ComponentTemplate != nullptr)
        {
            Out.Object = Out.Component->ComponentTemplate;
            Out.Container = Out.Object;
            Out.Property = FindFProperty<FProperty>(Out.Object->GetClass(), *Out.Field);
        }
        if (!Out.bPlanned && Out.Property == nullptr && Out.Component != nullptr)
        {
            Out.Object = Out.Component;
            Out.Container = Out.Component;
            Out.Property = FindFProperty<FProperty>(Out.Component->GetClass(), *Out.Field);
        }
    }
    if (Out.Field == TEXT("id")
        || Out.Field == TEXT("Status")
        || (Out.Field == TEXT("type") && Out.Kind != TEXT("variable")))
    {
        OutMessage = FString::Printf(TEXT("Field is read-only: %s."), *Out.Field);
        return false;
    }
    if (Out.Kind == TEXT("blueprint"))
    {
        static const TSet<FString> WritableFields = {
            TEXT("ParentClass"),
            TEXT("bRunConstructionScriptOnDrag"),
            TEXT("bRunConstructionScriptInSequencer"),
            TEXT("BlueprintDisplayName"),
            TEXT("BlueprintDescription"),
            TEXT("BlueprintNamespace"),
            TEXT("BlueprintCategory"),
            TEXT("HideCategories"),
            TEXT("bGenerateConstClass"),
            TEXT("bGenerateAbstractClass"),
            TEXT("bDeprecate"),
            TEXT("ShouldCookPropertyGuidsValue"),
            TEXT("CompileMode"),
            TEXT("ImportedNamespaces")};
        if (!WritableFields.Contains(Out.Field))
        {
            OutMessage = FString::Printf(TEXT("Field is not part of Blueprint Class Settings: %s."), *Out.Field);
            return false;
        }
        FString AvailabilityReason;
        if (!IsBlueprintFieldWritableNow(Blueprint, Out.Field, &AvailabilityReason))
        {
            OutMessage = FString::Printf(
                TEXT("Blueprint field is currently unavailable: %s. %s"),
                *Out.Field,
                *AvailabilityReason);
            return false;
        }
    }
    if (Out.Kind == TEXT("blueprint") && Out.Field == TEXT("ParentClass"))
    {
        return true;
    }
    if (Out.Kind == TEXT("graph") && Out.Field == TEXT("name"))
    {
        return Out.Graph != nullptr
            && Out.Graph->bAllowRenaming
            && (Blueprint->FunctionGraphs.Contains(Out.Graph)
                || Blueprint->MacroGraphs.Contains(Out.Graph)
                || Blueprint->UbergraphPages.Contains(Out.Graph));
    }
    if (Out.Kind == TEXT("graph"))
    {
        OutMessage = FString::Printf(TEXT("Graph field is read-only in the Blueprint lifecycle interface: %s."), *Out.Field);
        return false;
    }
    if ((Out.Kind == TEXT("variable") || Out.Kind == TEXT("dispatcher")) && Out.Field == TEXT("VarName"))
    {
        return Out.Variable != nullptr;
    }
    if (Out.Kind == TEXT("variable") && Out.Field == TEXT("type"))
    {
        return Out.Variable != nullptr;
    }
    if ((Out.Kind == TEXT("variable") || Out.Kind == TEXT("dispatcher"))
        && !IsWritableDeclarationField(Out.Field, Out.Kind == TEXT("dispatcher")))
    {
        OutMessage = FString::Printf(TEXT("Declaration field is read-only or not part of the writable schema: %s."), *Out.Field);
        return false;
    }
    if (Out.Property == nullptr || Out.Container == nullptr)
    {
        OutMessage = FString::Printf(TEXT("Field is not available on the resolved %s: %s."), *Out.Kind, *Out.Field);
        return false;
    }
    if ((Out.Kind == TEXT("component") && !CanWriteNativeTemplateField(Out.Object, Out.Property))
        || (Out.Kind != TEXT("component")
            && Out.Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_Deprecated)))
    {
        OutMessage = FString::Printf(TEXT("Field is not writable: %s."), *Out.Field);
        return false;
    }
    return true;
}

bool IsChildOfAny(const UClass* Class, const TSet<const UClass*>& Bases)
{
    if (Class == nullptr)
    {
        return false;
    }
    for (const UClass* Base : Bases)
    {
        if (Base != nullptr && Class->IsChildOf(Base))
        {
            return true;
        }
    }
    return false;
}

bool IsBlueprintCompileAllowedDuringPIE(const UBlueprint* Blueprint)
{
    if (Blueprint == nullptr || GEditor == nullptr || GEditor->PlayWorld == nullptr)
    {
        return true;
    }
    if (Blueprint->CanAlwaysRecompileWhilePlayingInEditor())
    {
        return true;
    }
    const UClass* TestClass = Blueprint->GeneratedClass != nullptr
        ? Blueprint->GeneratedClass.Get()
        : Blueprint->ParentClass.Get();
    if (TestClass == nullptr)
    {
        return false;
    }
    auto MatchesLoadedBase = [TestClass](const TArray<TSoftClassPtr<UObject>>& Bases)
    {
        for (const TSoftClassPtr<UObject>& SoftBase : Bases)
        {
            if (const UClass* Base = SoftBase.Get(); Base != nullptr && TestClass->IsChildOf(Base))
            {
                return true;
            }
        }
        return false;
    };
    return !MatchesLoadedBase(GetDefault<UBlueprintEditorSettings>()->BaseClassesToDisallowRecompilingDuringPlayInEditor)
        && !MatchesLoadedBase(GetDefault<UBlueprintEditorProjectSettings>()->BaseClassesToDisallowRecompilingDuringPlayInEditor);
}

bool ValidateParentClassChange(
    UBlueprint* Blueprint,
    UClass* NewParent,
    FString& OutMessage)
{
    if (Blueprint == nullptr || NewParent == nullptr || Blueprint->ParentClass == nullptr)
    {
        OutMessage = TEXT("ParentClass change requires a resolved Blueprint and Class.");
        return false;
    }
    if (Blueprint->BlueprintType == BPTYPE_Interface
        || Blueprint->BlueprintType == BPTYPE_FunctionLibrary)
    {
        OutMessage = TEXT("UE does not expose reparenting for Interface or Function Library Blueprints.");
        return false;
    }
    if (NewParent->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Interface)
        || NewParent->GetName().StartsWith(TEXT("SKEL_"))
        || NewParent->GetName().StartsWith(TEXT("REINST_"))
        || !FKismetEditorUtilities::CanCreateBlueprintOfClass(NewParent))
    {
        OutMessage = TEXT("The requested Class is deprecated, transient, an Interface, or not a Blueprint base Class.");
        return false;
    }
    if ((Blueprint->GeneratedClass != nullptr
            && (NewParent == Blueprint->GeneratedClass || NewParent->IsChildOf(Blueprint->GeneratedClass)))
        || NewParent->ClassGeneratedBy == Blueprint)
    {
        OutMessage = TEXT("The requested ParentClass would create a Blueprint inheritance cycle.");
        return false;
    }

    const IKismetCompilerInterface& Compiler =
        FModuleManager::LoadModuleChecked<IKismetCompilerInterface>(TEXT("KismetCompiler"));
    UClass* CurrentBlueprintType = nullptr;
    UClass* CurrentGeneratedType = nullptr;
    UClass* NewBlueprintType = nullptr;
    UClass* NewGeneratedType = nullptr;
    Compiler.GetBlueprintTypesForClass(Blueprint->ParentClass, CurrentBlueprintType, CurrentGeneratedType);
    Compiler.GetBlueprintTypesForClass(NewParent, NewBlueprintType, NewGeneratedType);
    if (CurrentBlueprintType == nullptr
        || CurrentGeneratedType == nullptr
        || NewBlueprintType != CurrentBlueprintType
        || NewGeneratedType != CurrentGeneratedType
        || !Blueprint->GetClass()->IsChildOf(NewBlueprintType)
        || (Blueprint->GeneratedClass != nullptr
            && !Blueprint->GeneratedClass->GetClass()->IsChildOf(NewGeneratedType)))
    {
        OutMessage = TEXT("The requested Class requires a different UBlueprint or UBlueprintGeneratedClass family.");
        return false;
    }

    TSet<const UClass*> Allowed;
    TSet<const UClass*> Disallowed;
    Blueprint->GetReparentingRules(Allowed, Disallowed);
    if ((!Allowed.IsEmpty() && !IsChildOfAny(NewParent, Allowed))
        || IsChildOfAny(NewParent, Disallowed))
    {
        OutMessage = TEXT("The Blueprint's native GetReparentingRules reject this ParentClass.");
        return false;
    }

    const UClass* CurrentParent = Blueprint->ParentClass;
    if (CurrentParent->IsChildOf(AActor::StaticClass()))
    {
        const bool bLevelScript = CurrentParent->IsChildOf(ALevelScriptActor::StaticClass());
        if (!NewParent->IsChildOf(AActor::StaticClass())
            || (bLevelScript && (!NewParent->IsChildOf(ALevelScriptActor::StaticClass())
                || !NewParent->HasAnyClassFlags(CLASS_Native)))
            || (!bLevelScript && NewParent->IsChildOf(ALevelScriptActor::StaticClass())))
        {
            OutMessage = TEXT("UE Actor and Level Script reparenting family rules reject this ParentClass.");
            return false;
        }
    }
    else if (CurrentParent->IsChildOf(UAnimInstance::StaticClass()))
    {
        if (!NewParent->IsChildOf(UAnimInstance::StaticClass()))
        {
            OutMessage = TEXT("An Animation Blueprint must remain in the UAnimInstance hierarchy.");
            return false;
        }
    }
    else if (CurrentParent->IsChildOf(UActorComponent::StaticClass()))
    {
        if (!NewParent->IsChildOf(UActorComponent::StaticClass()))
        {
            OutMessage = TEXT("A Component Blueprint must remain in the UActorComponent hierarchy.");
            return false;
        }
    }
    else if (NewParent->IsChildOf(AActor::StaticClass()))
    {
        OutMessage = TEXT("A non-Actor Blueprint cannot be reparented into the Actor hierarchy.");
        return false;
    }
    if (!IsBlueprintCompileAllowedDuringPIE(Blueprint))
    {
        OutMessage = TEXT("Project or editor settings forbid recompiling this Blueprint family during PIE.");
        return false;
    }
    return true;
}

bool EnsureBlueprintCurrent(UBlueprint* Blueprint, FString& OutMessage)
{
    if (Blueprint == nullptr)
    {
        OutMessage = TEXT("Blueprint is unavailable while conforming its new parent.");
        return false;
    }
    FBlueprintEditorUtils::PurgeNullGraphs(Blueprint);
    FKismetEditorUtilities::UpgradeCosmeticallyStaleBlueprint(Blueprint);
    if (FBlueprintEditorUtils::SupportsConstructionScript(Blueprint))
    {
        if (Blueprint->SimpleConstructionScript == nullptr)
        {
            if (Blueprint->GeneratedClass == nullptr)
            {
                OutMessage = TEXT("UE cannot create the required SimpleConstructionScript without a generated Class.");
                return false;
            }
            Blueprint->SimpleConstructionScript = NewObject<USimpleConstructionScript>(Blueprint->GeneratedClass);
            Blueprint->SimpleConstructionScript->SetFlags(RF_Transactional);
        }
        if (UEdGraph* ConstructionScript = FBlueprintEditorUtils::FindUserConstructionScript(Blueprint))
        {
            ConstructionScript->bAllowDeletion = false;
        }
        else if (GetDefault<UBlueprintEditorSettings>()->IsFunctionAllowedForAsset(
            Blueprint,
            UEdGraphSchema_K2::FN_UserConstructionScript))
        {
            UEdGraph* NewConstructionScript = FBlueprintEditorUtils::CreateNewGraph(
                Blueprint,
                UEdGraphSchema_K2::FN_UserConstructionScript,
                UEdGraph::StaticClass(),
                UEdGraphSchema_K2::StaticClass());
            if (NewConstructionScript == nullptr)
            {
                OutMessage = TEXT("UE failed to create the required User Construction Script Graph.");
                return false;
            }
            FBlueprintEditorUtils::AddFunctionGraph(
                Blueprint,
                NewConstructionScript,
                false,
                AActor::StaticClass());
            NewConstructionScript->bAllowDeletion = false;
        }
        Blueprint->SimpleConstructionScript->ValidateSceneRootNodes();
    }
    else
    {
        if (Blueprint->SimpleConstructionScript != nullptr)
        {
            for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
            {
                if (Node != nullptr)
                {
                    FBlueprintEditorUtils::RemoveVariableNodes(Blueprint, Node->GetVariableName());
                }
            }
            Blueprint->SimpleConstructionScript = nullptr;
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        }
        if (UEdGraph* ConstructionScript = FBlueprintEditorUtils::FindUserConstructionScript(Blueprint))
        {
            ConstructionScript->bAllowDeletion = true;
        }
    }
    FBlueprintEditorUtils::ConformCallsToParentFunctions(Blueprint);
    FBlueprintEditorUtils::ConformImplementedEvents(Blueprint);
    FBlueprintEditorUtils::ConformImplementedInterfaces(Blueprint);
    FBlueprintEditorUtils::UpdateOutOfDateCompositeNodes(Blueprint);
    FBlueprintEditorUtils::UpdateTransactionalFlags(Blueprint);
    return true;
}

void AddCompilerComments(
    UBlueprint* Blueprint,
    const FCompilerResultsLog& Log,
    const FString& Prefix,
    TArray<FString>& Comments)
{
    Comments.Add(FString::Printf(
        TEXT("%s: %s; %d errors; %d warnings"),
        *Prefix,
        *EnumName(StaticEnum<EBlueprintStatus>(), Blueprint->Status),
        Log.NumErrors,
        Log.NumWarnings));
    for (const TSharedRef<FTokenizedMessage>& Message : Log.Messages)
    {
        FString Severity = TEXT("info");
        if (Message->GetSeverity() == EMessageSeverity::Error)
        {
            Severity = TEXT("error");
        }
        else if (Message->GetSeverity() == EMessageSeverity::Warning
            || Message->GetSeverity() == EMessageSeverity::PerformanceWarning)
        {
            Severity = TEXT("warning");
        }
        Comments.Add(Severity + TEXT(": ") + Message->ToText().ToString());
    }
}

bool ApplyParentClassChange(
    UBlueprint* Blueprint,
    UClass* NewParent,
    TArray<FString>& Comments,
    FString& OutMessage)
{
    Blueprint->Modify();
    if (Blueprint->GetOutermost() == GetTransientPackage())
    {
        Blueprint->ParentClass = NewParent;
        Comments.Add(FString::Printf(TEXT("reparented: %s"), *NewParent->GetPathName()));
        Comments.Add(TEXT("reparent compile and derived-state conformance validated but not executed in transient preflight"));
        return true;
    }
    if (USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript)
    {
        SCS->Modify();
        for (USCS_Node* Node : SCS->GetAllNodes())
        {
            if (Node != nullptr)
            {
                Node->Modify();
            }
        }
    }

    TSet<FString> OldDefaultImports;
    FBlueprintNamespaceUtilities::GetDefaultImportsForObject(Blueprint, OldDefaultImports);
    Blueprint->ParentClass = NewParent;
    TSet<FString> NewDefaultImports;
    FBlueprintNamespaceUtilities::GetDefaultImportsForObject(Blueprint, NewDefaultImports);
    const TSet<FString> LostDefaultImports = OldDefaultImports.Difference(NewDefaultImports);
    Blueprint->ImportedNamespaces.Append(LostDefaultImports);

    if (!EnsureBlueprintCurrent(Blueprint, OutMessage))
    {
        return false;
    }
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    if (UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass))
    {
        GeneratedClass->PrepareToConformSparseClassData(NewParent->GetSparseClassDataStruct());
    }

    FCompilerResultsLog Log;
    Log.SetSourcePath(Blueprint->GetPathName());
    EBlueprintCompileOptions Options =
        EBlueprintCompileOptions::UseDeltaSerializationDuringReinstancing
        | EBlueprintCompileOptions::SkipNewVariableDefaultsDetection;
    if (GEditor != nullptr && GEditor->PlayWorld != nullptr)
    {
        Options |= EBlueprintCompileOptions::IncludeCDOInReferenceReplacement;
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint, Options, &Log);
    if (!EnsureBlueprintCurrent(Blueprint, OutMessage))
    {
        return false;
    }
    Comments.Add(FString::Printf(TEXT("reparented: %s"), *NewParent->GetPathName()));
    if (!LostDefaultImports.IsEmpty())
    {
        TArray<FString> SortedImports = LostDefaultImports.Array();
        SortedImports.Sort();
        Comments.Add(FString::Printf(
            TEXT("preserved explicit namespace imports: %s"),
            *FString::Join(SortedImports, TEXT(", "))));
    }
    AddCompilerComments(Blueprint, Log, TEXT("reparent compile"), Comments);
    return true;
}

void CollectLoadedDependentBlueprints(UBlueprint* Blueprint, TArray<UBlueprint*>& OutBlueprints)
{
    if (Blueprint == nullptr)
    {
        return;
    }
    for (TObjectIterator<UBlueprint> It; It; ++It)
    {
        UBlueprint* Candidate = *It;
        if (Candidate == nullptr
            || Candidate == Blueprint
            || Candidate->GetOutermost() == GetTransientPackage()
            || Candidate->ParentClass == nullptr)
        {
            continue;
        }
        TArray<UBlueprint*> Parents;
        UBlueprint::GetBlueprintHierarchyFromClass(Candidate->ParentClass, Parents);
        TArray<UClass*> Interfaces;
        FBlueprintEditorUtils::FindImplementedInterfaces(Candidate, true, Interfaces);
        for (UClass* Interface : Interfaces)
        {
            if (UBlueprint* InterfaceBlueprint = UBlueprint::GetBlueprintFromClass(Interface))
            {
                Parents.AddUnique(InterfaceBlueprint);
            }
        }
        if (Parents.Contains(Blueprint))
        {
            OutBlueprints.Add(Candidate);
        }
    }
}

void CollectVariableNodes(
    const FName VariableName,
    const UBlueprint* Blueprint,
    TArray<UK2Node*>& OutNodes)
{
    if (Blueprint == nullptr)
    {
        return;
    }
    TArray<UK2Node*> Nodes;
    FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node>(Blueprint, Nodes);
    for (UK2Node* Node : Nodes)
    {
        if (Node != nullptr && Node->ReferencesVariable(VariableName, nullptr))
        {
            OutNodes.Add(Node);
        }
    }
}

bool ChangeVariableTypeNonInteractive(
    UBlueprint* Blueprint,
    FBPVariableDescription& Variable,
    const FEdGraphPinType& NewType,
    const bool bApply,
    TArray<FString>& Comments,
    FString& OutMessage)
{
    if (Variable.VarType == NewType)
    {
        return true;
    }
    if ((NewType.PinCategory == UEdGraphSchema_K2::PC_Object
            || NewType.PinCategory == UEdGraphSchema_K2::PC_Interface)
        && Cast<UClass>(NewType.PinSubCategoryObject.Get()) == nullptr)
    {
        OutMessage = TEXT("Object and Interface Variable types require a resolved native UClass subcategory.");
        return false;
    }

    UBlueprint* PersistentBlueprint = PersistentBlueprintForPreflight(Blueprint);
    if (PersistentBlueprint == nullptr)
    {
        OutMessage = TEXT("The persistent Blueprint could not be resolved uniquely for Variable type preflight.");
        return false;
    }
    TArray<UBlueprint*> AffectedChildren;
    CollectLoadedDependentBlueprints(PersistentBlueprint, AffectedChildren);
    TArray<UK2Node*> AffectedNodes;
    CollectVariableNodes(Variable.VarName, PersistentBlueprint, AffectedNodes);
    for (UBlueprint* Child : AffectedChildren)
    {
        CollectVariableNodes(Variable.VarName, Child, AffectedNodes);
    }
    Comments.Add(FString::Printf(
        TEXT("%s Variable type: %s; reconstruct nodes: %d; loaded child Blueprints: %d"),
        bApply ? TEXT("changed") : TEXT("would change"),
        *PinTypeText(NewType),
        AffectedNodes.Num(),
        AffectedChildren.Num()));
    if (!bApply)
    {
        return true;
    }

    Blueprint->Modify();
    const bool bBecameBoolean = Variable.VarType.PinCategory != UEdGraphSchema_K2::PC_Boolean
        && NewType.PinCategory == UEdGraphSchema_K2::PC_Boolean;
    const bool bBecameNotBoolean = Variable.VarType.PinCategory == UEdGraphSchema_K2::PC_Boolean
        && NewType.PinCategory != UEdGraphSchema_K2::PC_Boolean;
    if (bBecameBoolean || bBecameNotBoolean)
    {
        Variable.FriendlyName = FName::NameToDisplayString(Variable.VarName.ToString(), bBecameBoolean);
    }
    if (NewType.PinCategory == UEdGraphSchema_K2::PC_Object
        || NewType.PinCategory == UEdGraphSchema_K2::PC_Interface)
    {
        const UClass* ObjectClass = CastChecked<UClass>(NewType.PinSubCategoryObject.Get());
        if (ObjectClass->IsChildOf(AActor::StaticClass()))
        {
            Variable.PropertyFlags |= CPF_DisableEditOnTemplate;
        }
        else
        {
            Variable.PropertyFlags &= ~CPF_DisableEditOnTemplate;
        }
    }
    else
    {
        Variable.PropertyFlags &= ~CPF_DisableEditOnTemplate;
    }
    Variable.VarType = NewType;
    if (Variable.VarType.IsSet() || Variable.VarType.IsMap())
    {
        Variable.PropertyFlags &= ~CPF_Net;
        Variable.PropertyFlags &= ~CPF_RepNotify;
        Variable.RepNotifyFunc = NAME_None;
        Variable.ReplicationCondition = COND_None;
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    const bool bPersistentApply = Blueprint == PersistentBlueprint;
    if (bPersistentApply)
    {
        for (UBlueprint* Child : AffectedChildren)
        {
            if (Child != nullptr)
            {
                Child->Modify();
                FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Child);
            }
        }
    }
    TArray<UK2Node*> NodesToReconstruct;
    if (bPersistentApply)
    {
        NodesToReconstruct = MoveTemp(AffectedNodes);
    }
    else
    {
        CollectVariableNodes(Variable.VarName, Blueprint, NodesToReconstruct);
    }
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    for (UK2Node* Node : NodesToReconstruct)
    {
        if (Node != nullptr)
        {
            Node->Modify();
            Schema->ReconstructNode(*Node, true);
        }
    }
    return true;
}

bool SetOrResetField(
    UBlueprint* Blueprint,
    const FResolvedField& Field,
    const TSharedPtr<FJsonValue>& NewValue,
    const bool bReset,
    const bool bApply,
    bool& bChanged,
    TArray<FString>& Comments,
    FString& OutMessage)
{
    bChanged = false;
    if (Field.Kind == TEXT("blueprint"))
    {
        FString AvailabilityReason;
        if (!IsBlueprintFieldWritableNow(Blueprint, Field.Field, &AvailabilityReason))
        {
            OutMessage = FString::Printf(
                TEXT("Blueprint field is currently unavailable: %s. %s"),
                *Field.Field,
                *AvailabilityReason);
            return false;
        }
    }
    if (Field.Kind == TEXT("blueprint") && Field.Field == TEXT("ParentClass"))
    {
        if (bReset)
        {
            OutMessage = TEXT("ParentClass has no reset operation.");
            return false;
        }
        const FString Path = ExprString(NewValue);
        UClass* Class = FindObject<UClass>(nullptr, *Path);
        if (Class == nullptr)
        {
            Class = LoadObject<UClass>(nullptr, *Path);
        }
        if (Class != nullptr && Blueprint->ParentClass == Class)
        {
            return true;
        }
        if (!ValidateParentClassChange(Blueprint, Class, OutMessage))
        {
            return false;
        }
        bChanged = true;
        if (!bApply)
        {
            Comments.Add(FString::Printf(TEXT("would reparent: %s"), *Class->GetPathName()));
            Comments.Add(TEXT("would conform SCS, parent calls, events, interfaces, and reconstruct all Nodes"));
            Comments.Add(TEXT("would rebase Class Defaults and full compile with UE reparent flags"));
            return true;
        }
        return ApplyParentClassChange(Blueprint, Class, Comments, OutMessage);
    }
    if (Field.Kind == TEXT("graph") && Field.Field == TEXT("name"))
    {
        if (bReset)
        {
            OutMessage = TEXT("Graph name has no reset operation.");
            return false;
        }
        const FString Name = ExprString(NewValue);
        if (Name == Field.Graph->GetName())
        {
            return true;
        }
        if (Name.IsEmpty()
            || Name.Len() >= NAME_SIZE
            || NameExists(Blueprint, Name)
            || FKismetNameValidator(Blueprint).IsValid(Name) != EValidatorResult::Ok)
        {
            OutMessage = TEXT("Graph name is empty or already in use.");
            return false;
        }
        if (FindChildNameCollision(Blueprint, FName(*Name), OutMessage))
        {
            return false;
        }
        bChanged = true;
        if (bApply)
        {
            FBlueprintEditorUtils::RenameGraph(Field.Graph, Name);
            if (Field.Graph->GetName() != Name)
            {
                OutMessage = TEXT("UE did not preserve the exact requested Graph name.");
                return false;
            }
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        }
        return true;
    }
    if ((Field.Kind == TEXT("variable") || Field.Kind == TEXT("dispatcher")) && Field.Field == TEXT("VarName"))
    {
        if (bReset)
        {
            OutMessage = TEXT("VarName has no reset operation.");
            return false;
        }
        const FString Name = ExprString(NewValue);
        if (Field.Variable != nullptr && Name == Field.Variable->VarName.ToString())
        {
            return true;
        }
        if (Name.IsEmpty()
            || Name.Len() >= NAME_SIZE
            || NameExists(Blueprint, Name)
            || FKismetNameValidator(Blueprint).IsValid(Name) != EValidatorResult::Ok)
        {
            OutMessage = TEXT("Declaration name is empty or already in use.");
            return false;
        }
        if (FindChildNameCollision(Blueprint, FName(*Name), OutMessage))
        {
            return false;
        }
        const FName OldName = Field.Variable->VarName;
        if (Field.Kind == TEXT("variable") && !Field.Variable->RepNotifyFunc.IsNone())
        {
            OutMessage = FString::Printf(
                TEXT("Variable rename would clear RepNotifyFunc %s through UE's modal path. Reset RepNotifyFunc earlier in the same Patch."),
                *Field.Variable->RepNotifyFunc.ToString());
            return false;
        }
        UEdGraph* DispatcherGraph = Field.Kind == TEXT("dispatcher")
            ? FindDispatcherGraph(Blueprint, OldName.ToString())
            : nullptr;
        if (Field.Kind == TEXT("dispatcher") && DispatcherGraph == nullptr)
        {
            OutMessage = TEXT("Dispatcher Signature Graph is missing; refusing a partial rename.");
            return false;
        }
        bChanged = true;
        if (bApply)
        {
            FBlueprintEditorUtils::RenameMemberVariable(Blueprint, OldName, FName(*Name));
            if (DispatcherGraph != nullptr && DispatcherGraph->GetName() != Name)
            {
                FBlueprintEditorUtils::RenameGraph(DispatcherGraph, Name);
            }
            if (FindVariableByName(Blueprint, Name, Field.Kind == TEXT("dispatcher")) == nullptr
                || (DispatcherGraph != nullptr && DispatcherGraph->GetName() != Name))
            {
                OutMessage = TEXT("UE did not preserve the exact requested declaration name.");
                return false;
            }
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        }
        return true;
    }
    if (Field.Kind == TEXT("variable") && Field.Field == TEXT("type"))
    {
        if (bReset)
        {
            OutMessage = TEXT("Variable type has no reset operation.");
            return false;
        }
        FString TypeText;
        FEdGraphPinType NewType;
        if (!NewValue.IsValid() || !NewValue->TryGetString(TypeText) || !ParsePinType(TypeText, NewType))
        {
            OutMessage = TEXT("Variable type must be exact FEdGraphPinType native text.");
            return false;
        }
        if (Field.Variable == nullptr || Field.Variable->VarType == NewType)
        {
            return Field.Variable != nullptr;
        }
        bChanged = true;
        return ChangeVariableTypeNonInteractive(
            Blueprint,
            *Field.Variable,
            NewType,
            bApply,
            Comments,
            OutMessage);
    }
    if (Field.Kind == TEXT("component") && Field.Field == TEXT("name"))
    {
        if (bReset)
        {
            OutMessage = TEXT("Component name has no reset operation.");
            return false;
        }
        const FString Name = ExprString(NewValue);
        if (Field.Component != nullptr && Name == Field.Component->GetVariableName().ToString())
        {
            return true;
        }
        const UActorComponent* Template = Field.Component != nullptr
            ? Cast<UActorComponent>(Field.Component->ComponentTemplate)
            : nullptr;
        if (Name.IsEmpty()
            || Name.Len() >= NAME_SIZE
            || NameExists(Blueprint, Name)
            || FKismetNameValidator(Blueprint).IsValid(Name) != EValidatorResult::Ok
            || Template == nullptr
            || !FComponentEditorUtils::IsValidVariableNameString(Template, Name))
        {
            OutMessage = TEXT("Component name is empty or already in use.");
            return false;
        }
        if (FindChildNameCollision(Blueprint, FName(*Name), OutMessage))
        {
            return false;
        }
        bChanged = true;
        if (bApply)
        {
            FBlueprintEditorUtils::RenameComponentMemberVariable(Blueprint, Field.Component, FName(*Name));
            if (Field.Component->GetVariableName() != FName(*Name))
            {
                OutMessage = TEXT("UE did not preserve the exact requested Component name.");
                return false;
            }
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        }
        return true;
    }

    if (Field.Kind == TEXT("dispatcher")
        && Field.Field == TEXT("PropertyFlags")
        && bReset)
    {
        OutMessage = TEXT("Dispatcher PropertyFlags cannot be reset because UE requires CPF_BlueprintAssignable and CPF_BlueprintCallable.");
        return false;
    }

    if (Field.Kind == TEXT("blueprint") && Field.Field == TEXT("ImportedNamespaces") && !bReset)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!NewValue.IsValid() || !NewValue->TryGetArray(Values) || Values == nullptr)
        {
            OutMessage = TEXT("ImportedNamespaces requires an array of registered Namespace strings.");
            return false;
        }
        const FBlueprintNamespaceRegistry& Registry = FBlueprintNamespaceRegistry::Get();
        for (const TSharedPtr<FJsonValue>& Value : *Values)
        {
            FString Namespace;
            if (!Value.IsValid()
                || !Value->TryGetString(Namespace)
                || Namespace.IsEmpty()
                || !Registry.IsRegisteredPath(Namespace))
            {
                OutMessage = FString::Printf(
                    TEXT("Imported Namespace is not registered in the current UE project: %s."),
                    *Namespace);
                return false;
            }
        }
    }

    const FString OldBlueprintNamespace = Blueprint != nullptr ? Blueprint->BlueprintNamespace : FString();
    const TSet<FString> OldImportedNamespaces = Blueprint != nullptr
        ? Blueprint->ImportedNamespaces
        : TSet<FString>();
    TArray<uint8> ImportedValue;
    const void* DefaultsContainer = nullptr;
    FBPVariableDescription VariableDefaults;
    if (bReset)
    {
        if (Field.Kind == TEXT("component")
            && !CanResetNativeTemplateField(Field.Object, Field.Property))
        {
            OutMessage = FString::Printf(TEXT("Field is not resettable on this Component template: %s."), *Field.Field);
            return false;
        }
        if (Field.Kind == TEXT("variable") || Field.Kind == TEXT("dispatcher"))
        {
            DefaultsContainer = &VariableDefaults;
        }
        else
        {
            DefaultsContainer = Field.Object != nullptr ? Field.Object->GetClass()->GetDefaultObject() : nullptr;
        }
        if (DefaultsContainer == nullptr)
        {
            OutMessage = TEXT("UE default object is unavailable for reset.");
            return false;
        }
        if (Field.Property->Identical_InContainer(Field.Container, DefaultsContainer))
        {
            return true;
        }
    }
    else
    {
        const FString NativeText = NativeTextForProperty(Field.Property, NewValue);
        if (NativeText.IsEmpty() && !NewValue->IsNull())
        {
            OutMessage = FString::Printf(TEXT("SAL value cannot be represented as native %s text."), *Field.Property->GetCPPType());
            return false;
        }
        ImportedValue.SetNumZeroed(Field.Property->GetSize());
        Field.Property->InitializeValue(ImportedValue.GetData());
        const TCHAR* End = Field.Property->ImportText_Direct(
            *NativeText,
            ImportedValue.GetData(),
            nullptr,
            PPF_None,
            GLog);
        bool bValid = End != nullptr;
        if (End != nullptr)
        {
            while (*End != TEXT('\0') && FChar::IsWhitespace(*End))
            {
                ++End;
            }
            bValid = *End == TEXT('\0');
        }
        if (!bValid)
        {
            Field.Property->DestroyValue(ImportedValue.GetData());
            OutMessage = FString::Printf(TEXT("UE could not import a native value for %s."), *Field.Field);
            return false;
        }
        if (Field.Kind == TEXT("dispatcher") && Field.Field == TEXT("PropertyFlags"))
        {
            const FNumericProperty* Numeric = CastField<FNumericProperty>(Field.Property);
            const uint64 Flags = Numeric != nullptr
                ? Numeric->GetUnsignedIntPropertyValue(ImportedValue.GetData())
                : 0;
            constexpr uint64 RequiredFlags = CPF_BlueprintAssignable | CPF_BlueprintCallable;
            if (Numeric == nullptr || (Flags & RequiredFlags) != RequiredFlags)
            {
                Field.Property->DestroyValue(ImportedValue.GetData());
                OutMessage = TEXT("Dispatcher PropertyFlags must preserve CPF_BlueprintAssignable and CPF_BlueprintCallable.");
                return false;
            }
        }
        const void* CurrentValue = Field.Property->ContainerPtrToValuePtr<void>(Field.Container);
        const bool bIdentical = Field.Property->Identical(CurrentValue, ImportedValue.GetData());
        Field.Property->DestroyValue(ImportedValue.GetData());
        if (bIdentical)
        {
            return true;
        }
    }
    bChanged = true;
    if (!bApply)
    {
        return true;
    }
    if (Field.Object != nullptr)
    {
        Field.Object->Modify();
    }
    if (bReset)
    {
        Field.Property->CopyCompleteValue_InContainer(Field.Container, DefaultsContainer);
    }
    else
    {
        if (!ImportPropertyValue(
                Field.Property,
                Field.Container,
                NativeTextForProperty(Field.Property, NewValue),
                OutMessage))
        {
            return false;
        }
    }
    if (Field.Kind == TEXT("blueprint") && Field.Field == TEXT("bDeprecate"))
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    }
    else if (Field.Kind == TEXT("blueprint"))
    {
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    }
    else
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    }
    if (Field.Kind == TEXT("blueprint") && Field.Field == TEXT("BlueprintNamespace"))
    {
        FBlueprintNamespaceRegistry& Registry = FBlueprintNamespaceRegistry::Get();
        const bool bPersistent = Blueprint->GetOutermost() != GetTransientPackage();
        if (bPersistent
            && !OldBlueprintNamespace.IsEmpty()
            && Registry.IsRegisteredPath(OldBlueprintNamespace))
        {
            Registry.Rebuild();
        }
        if (bPersistent
            && !Blueprint->BlueprintNamespace.IsEmpty()
            && !Registry.IsRegisteredPath(Blueprint->BlueprintNamespace))
        {
            Registry.RegisterNamespace(Blueprint->BlueprintNamespace);
        }
        FBlueprintEditor* BlueprintEditor = bPersistent ? FindOpenBlueprintEditor(Blueprint) : nullptr;
        if (!OldBlueprintNamespace.IsEmpty()
            && OldImportedNamespaces.Contains(OldBlueprintNamespace)
            && !Registry.IsInclusivePath(OldBlueprintNamespace))
        {
            if (BlueprintEditor != nullptr)
            {
                BlueprintEditor->RemoveNamespace(OldBlueprintNamespace);
            }
            else
            {
                Blueprint->ImportedNamespaces.Remove(OldBlueprintNamespace);
            }
            Comments.Add(TEXT("removed namespace context: ") + OldBlueprintNamespace);
        }
        if (!Blueprint->BlueprintNamespace.IsEmpty()
            && !Blueprint->ImportedNamespaces.Contains(Blueprint->BlueprintNamespace))
        {
            if (BlueprintEditor != nullptr)
            {
                FBlueprintEditor::FImportNamespaceExParameters Params;
                Params.bIsAutoImport = false;
                Params.NamespacesToImport.Add(Blueprint->BlueprintNamespace);
                BlueprintEditor->ImportNamespaceEx(Params);
            }
            else
            {
                Blueprint->ImportedNamespaces.Add(Blueprint->BlueprintNamespace);
            }
            Comments.Add(TEXT("imported namespace context: ") + Blueprint->BlueprintNamespace);
        }
        if (BlueprintEditor != nullptr)
        {
            BlueprintEditor->RefreshInspector();
        }
    }
    else if (Field.Kind == TEXT("blueprint") && Field.Field == TEXT("ImportedNamespaces"))
    {
        const TSet<FString> Removed = OldImportedNamespaces.Difference(Blueprint->ImportedNamespaces);
        const TSet<FString> Added = Blueprint->ImportedNamespaces.Difference(OldImportedNamespaces);
        if (FBlueprintEditor* BlueprintEditor = Blueprint->GetOutermost() != GetTransientPackage()
                ? FindOpenBlueprintEditor(Blueprint)
                : nullptr)
        {
            for (const FString& Namespace : Removed)
            {
                BlueprintEditor->RemoveNamespace(Namespace);
            }
            if (!Added.IsEmpty())
            {
                FBlueprintEditor::FImportNamespaceExParameters Params;
                Params.bIsAutoImport = false;
                Params.NamespacesToImport = Added;
                BlueprintEditor->ImportNamespaceEx(Params);
            }
            BlueprintEditor->RefreshInspector();
        }
        for (const FString& Namespace : Removed)
        {
            Comments.Add(TEXT("removed namespace context: ") + Namespace);
        }
        for (const FString& Namespace : Added)
        {
            Comments.Add(TEXT("imported namespace context: ") + Namespace);
        }
    }
    return true;
}
}

namespace
{
struct FBlueprintSubobjectContext
{
    USubobjectDataSubsystem* System = nullptr;
    FSubobjectDataHandle Actor;
    TArray<FSubobjectDataHandle> Handles;
};

bool GatherBlueprintSubobjects(
    UBlueprint* Blueprint,
    FBlueprintSubobjectContext& Out,
    FString& OutMessage)
{
    Out = FBlueprintSubobjectContext();
    if (Blueprint == nullptr
        || Blueprint->GeneratedClass == nullptr
        || !Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()))
    {
        OutMessage = TEXT("Component hierarchy operations require an Actor Blueprint with a generated Class.");
        return false;
    }
    Out.System = USubobjectDataSubsystem::Get();
    if (Out.System == nullptr)
    {
        OutMessage = TEXT("UE SubobjectDataSubsystem is unavailable.");
        return false;
    }
    Out.System->K2_GatherSubobjectDataForBlueprint(Blueprint, Out.Handles);
    for (const FSubobjectDataHandle& Handle : Out.Handles)
    {
        const FSubobjectData* Data = Handle.GetData();
        if (Data != nullptr && Data->IsRootActor())
        {
            Out.Actor = Handle;
            break;
        }
    }
    if (!Out.Actor.IsValid())
    {
        OutMessage = TEXT("UE did not expose the Blueprint Actor subobject context.");
        return false;
    }
    return true;
}

FSubobjectDataHandle FindComponentHandle(
    const FBlueprintSubobjectContext& Context,
    UBlueprint* Blueprint,
    const USCS_Node* Node)
{
    if (Blueprint == nullptr || Node == nullptr)
    {
        return FSubobjectDataHandle::InvalidHandle;
    }
    FSubobjectDataHandle NameMatch = FSubobjectDataHandle::InvalidHandle;
    for (const FSubobjectDataHandle& Handle : Context.Handles)
    {
        const FSubobjectData* Data = Handle.GetData();
        if (Data == nullptr || !Data->IsComponent())
        {
            continue;
        }
        const UObject* EditableObject = Data->GetObjectForBlueprint(Blueprint);
        if (EditableObject == Node->ComponentTemplate || Data->GetComponentTemplate() == Node->ComponentTemplate)
        {
            return Handle;
        }
        if (Data->GetVariableName() == Node->GetVariableName())
        {
            NameMatch = Handle;
        }
    }
    return NameMatch;
}

USCS_Node* FindComponentForHandle(UBlueprint* Blueprint, const FSubobjectDataHandle& Handle)
{
    const FSubobjectData* Data = Handle.GetData();
    if (Blueprint == nullptr || Data == nullptr || !Data->IsComponent())
    {
        return nullptr;
    }
    if (USCS_Node* Node = FindComponentByName(Blueprint, Data->GetVariableName().ToString()))
    {
        const UObject* EditableObject = Data->GetObjectForBlueprint(Blueprint);
        if (EditableObject == nullptr
            || EditableObject == Node->ComponentTemplate
            || Data->GetComponentTemplate() == Node->ComponentTemplate)
        {
            return Node;
        }
    }
    return nullptr;
}

bool IsHandleAncestorOf(
    const FSubobjectDataHandle& PossibleAncestor,
    FSubobjectDataHandle Descendant)
{
    while (Descendant.IsValid())
    {
        if (Descendant == PossibleAncestor)
        {
            return true;
        }
        const FSubobjectData* Data = Descendant.GetData();
        Descendant = Data != nullptr
            ? Data->GetParentHandle()
            : FSubobjectDataHandle::InvalidHandle;
    }
    return false;
}

bool SpawnBlueprintPreviewActor(
    UBlueprint* Blueprint,
    FPreviewScene& PreviewScene,
    AActor*& OutActor,
    FString& OutMessage)
{
    OutActor = nullptr;
    UWorld* World = PreviewScene.GetWorld();
    if (Blueprint == nullptr
        || Blueprint->GeneratedClass == nullptr
        || !Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass())
        || World == nullptr)
    {
        OutMessage = TEXT("An isolated Blueprint preview world could not be created.");
        return false;
    }
    FVector Location = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
    FActorSpawnParameters SpawnInfo;
    SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnInfo.ObjectFlags = RF_Transient | RF_Transactional;
    {
        FMakeClassSpawnableOnScope TemporarilySpawnable(Blueprint->GeneratedClass);
        OutActor = World->SpawnActor(Blueprint->GeneratedClass, &Location, &Rotation, SpawnInfo);
    }
    if (OutActor == nullptr)
    {
        OutMessage = TEXT("UE could not construct the Blueprint preview Actor required for transform-safe reparenting.");
        return false;
    }
    return true;
}

bool ValidateStableRef(
    UBlueprint* Blueprint,
    const TSharedPtr<FJsonObject>& Ref,
    FString& OutKind,
    FString& OutId,
    FString& OutMessage)
{
    if (!ReadRef(Ref, OutKind, OutId) || OutKind == TEXT("local"))
    {
        OutMessage = TEXT("Operation requires a typed stable reference.");
        return false;
    }
    bool bFound = false;
    if (OutKind == TEXT("variable")) bFound = FindVariable(Blueprint, OutId, false) != nullptr;
    else if (OutKind == TEXT("dispatcher")) bFound = FindVariable(Blueprint, OutId, true) != nullptr;
    else if (OutKind == TEXT("graph")) bFound = FindGraph(Blueprint, OutId) != nullptr;
    else if (OutKind == TEXT("component")) bFound = FindComponent(Blueprint, OutId) != nullptr;
    else
    {
        OutMessage = FString::Printf(TEXT("Blueprint does not own lifecycle for %s."), *OutKind);
        return false;
    }
    if (!bFound)
    {
        OutMessage = FString::Printf(TEXT("%s@%s was not found in the bound Blueprint."), *OutKind, *OutId);
    }
    return bFound;
}

bool IsOverrideFunctionGraph(const UEdGraph* Graph)
{
    if (Graph == nullptr)
    {
        return false;
    }
    TArray<UK2Node_FunctionEntry*> Entries;
    Graph->GetNodesOfClass(Entries);
    for (const UK2Node_FunctionEntry* Entry : Entries)
    {
        if (Entry != nullptr
            && !Entry->FunctionReference.IsSelfContext()
            && Entry->FunctionReference.GetMemberParentClass() != nullptr)
        {
            return true;
        }
    }
    return false;
}

bool FindUnsafeFunctionGraphUsages(
    UBlueprint* Blueprint,
    UEdGraph* Graph,
    FString& OutMessage)
{
    if (Blueprint == nullptr || Graph == nullptr || !Blueprint->FunctionGraphs.Contains(Graph))
    {
        return false;
    }
    UBlueprint* PersistentBlueprint = PersistentBlueprintForPreflight(Blueprint);
    if (PersistentBlueprint == nullptr)
    {
        OutMessage = TEXT("The persistent Blueprint could not be resolved uniquely for Function usage preflight.");
        return true;
    }
    UEdGraph* PersistentGraph = Blueprint == PersistentBlueprint
        ? Graph
        : FindGraph(PersistentBlueprint, GuidText(Graph->GraphGuid));
    if (PersistentGraph == nullptr)
    {
        OutMessage = TEXT("The persistent Function Graph could not be resolved for usage preflight.");
        return true;
    }
    if (IsOverrideFunctionGraph(PersistentGraph)
        || !FBlueprintEditorUtils::IsFunctionUsed(PersistentBlueprint, PersistentGraph->GetFName()))
    {
        return false;
    }

    const UStruct* FunctionScope = PersistentBlueprint->SkeletonGeneratedClass != nullptr
        ? static_cast<UStruct*>(PersistentBlueprint->SkeletonGeneratedClass.Get())
        : static_cast<UStruct*>(PersistentBlueprint->GeneratedClass.Get());
    TArray<FString> Usages;
    int32 TotalUsages = 0;
    for (TObjectIterator<UBlueprint> It; It; ++It)
    {
        UBlueprint* Referencer = *It;
        if (Referencer == nullptr || Referencer->GetOutermost() == GetTransientPackage())
        {
            continue;
        }
        for (UEdGraph* ReferencerGraph : BlueprintGraphs(Referencer))
        {
            if (ReferencerGraph == nullptr)
            {
                continue;
            }
            for (UEdGraphNode* Node : ReferencerGraph->Nodes)
            {
                const UK2Node* K2Node = Cast<UK2Node>(Node);
                if (K2Node == nullptr
                    || !K2Node->ReferencesFunction(PersistentGraph->GetFName(), FunctionScope))
                {
                    continue;
                }
                ++TotalUsages;
                if (Usages.Num() < 16)
                {
                    Usages.Add(FString::Printf(
                        TEXT("%s node@%s in graph@%s"),
                        *Referencer->GetPathName(),
                        *GuidText(K2Node->NodeGuid),
                        *GuidText(ReferencerGraph->GraphGuid)));
                }
            }
        }
    }
    OutMessage = FString::Printf(
        TEXT("Function Graph %s is still used; UE's editor confirmation path would leave invalid call sites. Remove or replace the usages first%s%s."),
        *PersistentGraph->GetName(),
        Usages.IsEmpty() ? TEXT("") : TEXT(": "),
        Usages.IsEmpty() ? TEXT("") : *FString::Join(Usages, TEXT(", ")));
    if (TotalUsages > Usages.Num())
    {
        OutMessage += FString::Printf(TEXT(" (%d additional usages omitted.)"), TotalUsages - Usages.Num());
    }
    return true;
}

bool RemoveObject(
    UBlueprint* Blueprint,
    const FString& Kind,
    const FString& Id,
    const bool bApply,
    bool& bChanged,
    FString& OutMessage)
{
    bChanged = false;
    if (Kind == TEXT("variable") || Kind == TEXT("dispatcher"))
    {
        FBPVariableDescription* Variable = FindVariable(Blueprint, Id, Kind == TEXT("dispatcher"));
        if (Variable == nullptr)
        {
            OutMessage = TEXT("Blueprint declaration no longer exists.");
            return false;
        }
        const FName Name = Variable->VarName;
        UEdGraph* Signature = Kind == TEXT("dispatcher") ? FindDispatcherGraph(Blueprint, Name.ToString()) : nullptr;
        if (Kind == TEXT("dispatcher") && Signature == nullptr)
        {
            OutMessage = TEXT("Dispatcher backing Signature Graph is missing; refusing a partial deletion.");
            return false;
        }
        if (Kind == TEXT("dispatcher"))
        {
            TArray<UK2Node_BaseMCDelegate*> DelegateNodes;
            FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, DelegateNodes);
            TArray<FString> Usages;
            for (const UK2Node_BaseMCDelegate* Node : DelegateNodes)
            {
                if (Node == nullptr
                    || !Node->DelegateReference.IsSelfContext()
                    || Node->DelegateReference.GetMemberName() != Name)
                {
                    continue;
                }
                const UEdGraph* Graph = Node->GetGraph();
                Usages.Add(TEXT("node@") + GuidText(Node->NodeGuid)
                    + (Graph != nullptr ? TEXT(" in graph@") + GuidText(Graph->GraphGuid) : FString()));
            }
            if (!Usages.IsEmpty())
            {
                OutMessage = TEXT("Dispatcher has usage Nodes that UE leaves invalid after deletion: ")
                    + FString::Join(Usages, TEXT(", "))
                    + TEXT(". Remove those Nodes in their Graph Patch first.");
                return false;
            }
        }
        if (bApply)
        {
            FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, Name);
            if (Signature != nullptr)
            {
                FBlueprintEditorUtils::RemoveGraph(Blueprint, Signature);
            }
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        }
        bChanged = true;
        return true;
    }
    if (Kind == TEXT("graph"))
    {
        UEdGraph* Graph = FindGraph(Blueprint, Id);
        const bool bDirectLifecycle = Graph != nullptr
            && (Blueprint->FunctionGraphs.Contains(Graph)
                || Blueprint->MacroGraphs.Contains(Graph)
                || Blueprint->UbergraphPages.Contains(Graph));
        if (Graph == nullptr || !Graph->bAllowDeletion || !bDirectLifecycle)
        {
            OutMessage = TEXT("Graph is missing, editor-maintained, nested, or owned by Dispatcher/Interface compound lifecycle.");
            return false;
        }
        if (FindUnsafeFunctionGraphUsages(Blueprint, Graph, OutMessage))
        {
            return false;
        }
        if (bApply)
        {
            Graph->Modify();
            const UEdGraphSchema* Schema = Graph->GetSchema();
            if (Schema == nullptr || !Schema->TryDeleteGraph(Graph))
            {
                FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
            }
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        }
        bChanged = true;
        return true;
    }
    if (Kind == TEXT("component"))
    {
        USCS_Node* Component = FindComponent(Blueprint, Id);
        FBlueprintSubobjectContext Context;
        if (Component == nullptr || !GatherBlueprintSubobjects(Blueprint, Context, OutMessage))
        {
            if (Component == nullptr)
            {
                OutMessage = TEXT("Component no longer exists.");
            }
            return false;
        }
        const FSubobjectDataHandle Handle = FindComponentHandle(Context, Blueprint, Component);
        const FSubobjectData* Data = Handle.GetData();
        if (Data == nullptr || !Data->CanDelete() || Data->IsDefaultSceneRoot())
        {
            OutMessage = TEXT("UE native Component lifecycle forbids deleting this subobject.");
            return false;
        }
        TArray<UK2Node_ComponentBoundEvent*> BoundEvents;
        FKismetEditorUtilities::FindAllBoundEventsForComponent(
            Blueprint,
            Component->GetVariableName(),
            BoundEvents);
        if (!BoundEvents.IsEmpty())
        {
            TArray<FString> Usages;
            Usages.Reserve(BoundEvents.Num());
            for (const UK2Node_ComponentBoundEvent* Event : BoundEvents)
            {
                if (Event == nullptr)
                {
                    continue;
                }
                const UEdGraph* Graph = Event->GetGraph();
                const FString NodeRef = TEXT("node@") + GuidText(Event->NodeGuid);
                Usages.Add(Graph != nullptr
                    ? NodeRef + TEXT(" in graph@") + GuidText(Graph->GraphGuid)
                    : NodeRef);
            }
            OutMessage = TEXT("Component has bound Event usages that UE leaves invalid after deletion: ")
                + FString::Join(Usages, TEXT(", "))
                + TEXT(". Remove those Nodes in their Graph Patch first.");
            return false;
        }
        if (bApply)
        {
            if (Context.System->DeleteSubobjects(Context.Actor, {Handle}, Blueprint) != 1)
            {
                OutMessage = TEXT("UE SubobjectDataSubsystem did not delete the Component.");
                return false;
            }
        }
        bChanged = true;
        return true;
    }
    return false;
}

template <typename GraphArrayType>
bool ReorderGraphArray(
    GraphArrayType& Graphs,
    UEdGraph* Graph,
    UEdGraph* Anchor,
    const bool bBefore,
    const bool bApply,
    bool& bChanged)
{
    const int32 GraphIndex = Graphs.IndexOfByKey(Graph);
    const int32 AnchorIndex = Graphs.IndexOfByKey(Anchor);
    if (GraphIndex == INDEX_NONE || AnchorIndex == INDEX_NONE)
    {
        return false;
    }
    const bool bAlreadyPlaced = bBefore
        ? GraphIndex + 1 == AnchorIndex
        : AnchorIndex + 1 == GraphIndex;
    if (bAlreadyPlaced)
    {
        bChanged = false;
        return true;
    }
    bChanged = true;
    if (bApply)
    {
        auto Value = Graphs[GraphIndex];
        Graphs.RemoveAt(GraphIndex);
        int32 NewAnchorIndex = Graphs.IndexOfByKey(Anchor);
        Graphs.Insert(Value, bBefore ? NewAnchorIndex : NewAnchorIndex + 1);
    }
    return true;
}

bool ReorderGraph(
    UBlueprint* Blueprint,
    UEdGraph* Graph,
    UEdGraph* Anchor,
    const bool bBefore,
    const bool bApply,
    bool& bChanged)
{
    if (ReorderGraphArray(Blueprint->FunctionGraphs, Graph, Anchor, bBefore, bApply, bChanged)
        || ReorderGraphArray(Blueprint->MacroGraphs, Graph, Anchor, bBefore, bApply, bChanged))
    {
        return true;
    }
    return false;
}

bool MoveObject(
    UBlueprint* Blueprint,
    const TSharedPtr<FJsonObject>& Statement,
    const bool bApply,
    bool& bChanged,
    FString& OutMessage)
{
    bChanged = false;
    const TSharedPtr<FJsonObject>* TargetRef = nullptr;
    if (!Statement->TryGetObjectField(TEXT("target"), TargetRef) || TargetRef == nullptr)
    {
        OutMessage = TEXT("move has no target.");
        return false;
    }
    FString Kind;
    FString Id;
    if (!ValidateStableRef(Blueprint, *TargetRef, Kind, Id, OutMessage))
    {
        return false;
    }
    const TSharedPtr<FJsonObject>* AnchorRef = nullptr;
    bool bBefore = false;
    if (Statement->TryGetObjectField(TEXT("before"), AnchorRef) && AnchorRef != nullptr)
    {
        bBefore = true;
    }
    else if (!Statement->TryGetObjectField(TEXT("after"), AnchorRef) || AnchorRef == nullptr)
    {
        if (Kind == TEXT("component") && Statement->TryGetObjectField(TEXT("to"), AnchorRef) && AnchorRef != nullptr)
        {
            FString ParentKind;
            FString ParentId;
            if (!ReadRef(*AnchorRef, ParentKind, ParentId) || ParentKind != TEXT("component"))
            {
                OutMessage = TEXT("Component move destination must be component@id.");
                return false;
            }
            USCS_Node* Node = FindComponent(Blueprint, Id);
            USCS_Node* Parent = FindComponent(Blueprint, ParentId);
            if (Node == nullptr || Parent == nullptr || Node == Parent)
            {
                OutMessage = TEXT("Component move would create an invalid SCS hierarchy.");
                return false;
            }
            if (Node->ComponentClass == nullptr || Parent->ComponentClass == nullptr
                || !Node->ComponentClass->IsChildOf(USceneComponent::StaticClass())
                || !Parent->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
            {
                OutMessage = TEXT("Only Scene Components can participate in Component hierarchy moves.");
                return false;
            }
            FBlueprintSubobjectContext Context;
            if (!GatherBlueprintSubobjects(Blueprint, Context, OutMessage))
            {
                return false;
            }
            const FSubobjectDataHandle NodeHandle = FindComponentHandle(Context, Blueprint, Node);
            const FSubobjectDataHandle ParentHandle = FindComponentHandle(Context, Blueprint, Parent);
            const FSubobjectData* NodeData = NodeHandle.GetData();
            const FSubobjectData* ParentData = ParentHandle.GetData();
            const USceneComponent* NodeTemplate = NodeData != nullptr
                ? Cast<USceneComponent>(NodeData->GetObjectForBlueprint(Blueprint))
                : nullptr;
            const USceneComponent* ParentTemplate = ParentData != nullptr
                ? Cast<USceneComponent>(ParentData->GetObjectForBlueprint(Blueprint))
                : nullptr;
            if (NodeData == nullptr
                || ParentData == nullptr
                || !NodeData->CanReparent()
                || !NodeData->IsSceneComponent()
                || !ParentData->IsSceneComponent()
                || NodeData->GetBlueprint() != Blueprint
                || ParentData->GetBlueprint() != Blueprint
                || NodeTemplate == nullptr
                || ParentTemplate == nullptr)
            {
                OutMessage = TEXT("UE native Subobject rules forbid this Component reparent.");
                return false;
            }
            if (NodeData->GetParentHandle() == ParentHandle)
            {
                return true;
            }
            if (IsHandleAncestorOf(NodeHandle, ParentHandle))
            {
                OutMessage = TEXT("Component move would create a hierarchy cycle.");
                return false;
            }
            if (!ParentTemplate->CanAttachAsChild(NodeTemplate, NAME_None)
                || NodeTemplate->Mobility < ParentTemplate->Mobility
                || (ParentTemplate->IsEditorOnly() && !NodeTemplate->IsEditorOnly()))
            {
                OutMessage = TEXT("UE Designer CanAttachAsChild, Mobility, or Editor-only rules reject this Component reparent.");
                return false;
            }
            if (Context.System->FindSceneRootForSubobject(Context.Actor) == NodeHandle)
            {
                OutMessage = TEXT("The current Scene Root is changed through MakeNewSceneRoot, not ordinary move.");
                return false;
            }
            bChanged = true;
            if (!bApply)
            {
                return true;
            }

            FPreviewScene::ConstructionValues PreviewValues;
            PreviewValues
                .SetCreateDefaultLighting(false)
                .SetCreatePhysicsScene(false)
                .AllowAudioPlayback(false)
                .SetTransactional(false);
            FPreviewScene PreviewScene(PreviewValues);
            AActor* PreviewActor = nullptr;
            if (!SpawnBlueprintPreviewActor(Blueprint, PreviewScene, PreviewActor, OutMessage))
            {
                return false;
            }
            FReparentSubobjectParams Params;
            Params.NewParentHandle = ParentHandle;
            Params.BlueprintContext = Blueprint;
            Params.ActorPreviewContext = PreviewActor;
            if (!Context.System->ReparentSubobject(Params, NodeHandle))
            {
                OutMessage = TEXT("UE SubobjectDataSubsystem rejected Component reparenting.");
                return false;
            }
            return true;
        }
        OutMessage = TEXT("Blueprint move supports declaration before/after or Component to component@id.");
        return false;
    }
    FString AnchorKind;
    FString AnchorId;
    if (!ReadRef(*AnchorRef, AnchorKind, AnchorId) || AnchorKind != Kind || Id == AnchorId)
    {
        OutMessage = TEXT("move anchor must be a different object of the same concrete kind.");
        return false;
    }
    if (Kind == TEXT("graph"))
    {
        UEdGraph* Graph = FindGraph(Blueprint, Id);
        UEdGraph* Anchor = FindGraph(Blueprint, AnchorId);
        if (Graph == nullptr
            || Anchor == nullptr
            || !ReorderGraph(Blueprint, Graph, Anchor, bBefore, false, bChanged))
        {
            OutMessage = TEXT("Graphs can only be reordered inside the same native Blueprint Graph collection.");
            return false;
        }
        if (bApply && bChanged)
        {
            Blueprint->Modify();
            bool bAppliedChange = false;
            if (!ReorderGraph(Blueprint, Graph, Anchor, bBefore, true, bAppliedChange) || !bAppliedChange)
            {
                OutMessage = TEXT("UE Graph collection changed between reparent validation and apply.");
                return false;
            }
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        }
        return true;
    }
    if (Kind != TEXT("variable") && Kind != TEXT("dispatcher"))
    {
        OutMessage = TEXT("Only Variable and Dispatcher authored order has a stable native before/after path.");
        return false;
    }
    FBPVariableDescription* Variable = FindVariable(Blueprint, Id, Kind == TEXT("dispatcher"));
    FBPVariableDescription* Anchor = FindVariable(Blueprint, AnchorId, Kind == TEXT("dispatcher"));
    if (Variable == nullptr || Anchor == nullptr)
    {
        OutMessage = TEXT("move declaration or anchor no longer exists.");
        return false;
    }
    const int32 VariableIndex = Blueprint->NewVariables.IndexOfByPredicate(
        [Variable](const FBPVariableDescription& Candidate) { return &Candidate == Variable; });
    const int32 AnchorIndex = Blueprint->NewVariables.IndexOfByPredicate(
        [Anchor](const FBPVariableDescription& Candidate) { return &Candidate == Anchor; });
    if (VariableIndex == INDEX_NONE || AnchorIndex == INDEX_NONE)
    {
        OutMessage = TEXT("Blueprint declaration order changed during move resolution.");
        return false;
    }
    if ((bBefore && VariableIndex + 1 == AnchorIndex)
        || (!bBefore && AnchorIndex + 1 == VariableIndex))
    {
        return true;
    }
    bChanged = true;
    if (bApply)
    {
        UStruct* Scope = Blueprint->SkeletonGeneratedClass != nullptr
            ? static_cast<UStruct*>(Blueprint->SkeletonGeneratedClass)
            : static_cast<UStruct*>(Blueprint->GeneratedClass);
        if (Scope == nullptr)
        {
            OutMessage = TEXT("Blueprint variable scope is unavailable.");
            return false;
        }
        const bool bMoved = bBefore
            ? FBlueprintEditorUtils::MoveVariableBeforeVariable(Blueprint, Scope, Variable->VarName, Anchor->VarName, false)
            : FBlueprintEditorUtils::MoveVariableAfterVariable(Blueprint, Scope, Variable->VarName, Anchor->VarName, false);
        if (!bMoved)
        {
            OutMessage = TEXT("UE rejected declaration reordering.");
            return false;
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    }
    return true;
}

bool ResolveInterfaceClass(const FString& Path, UClass*& OutClass)
{
    OutClass = FindObject<UClass>(nullptr, *Path);
    if (OutClass == nullptr)
    {
        OutClass = LoadObject<UClass>(nullptr, *Path);
    }
    return OutClass != nullptr && OutClass->HasAnyClassFlags(CLASS_Interface);
}

int32 FindDirectInterfaceIndex(const UBlueprint* Blueprint, const UClass* InterfaceClass)
{
    if (Blueprint == nullptr || InterfaceClass == nullptr)
    {
        return INDEX_NONE;
    }
    return Blueprint->ImplementedInterfaces.IndexOfByPredicate(
        [InterfaceClass](const FBPInterfaceDescription& Description)
        {
            return Description.Interface == InterfaceClass;
        });
}

bool ValidateInterfaceAddition(
    UBlueprint* Blueprint,
    UClass* InterfaceClass,
    FString& OutMessage)
{
    if (!FBlueprintEditorUtils::DoesSupportImplementingInterfaces(Blueprint))
    {
        OutMessage = TEXT("This Blueprint type does not support implementing Interfaces.");
        return false;
    }
    if (InterfaceClass == nullptr
        || InterfaceClass->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists)
        || !FKismetEditorUtilities::CanBlueprintImplementInterface(Blueprint, InterfaceClass))
    {
        OutMessage = TEXT("The requested Interface is deprecated, current-context prohibited, or not Blueprint implementable.");
        return false;
    }
    if (FindDirectInterfaceIndex(Blueprint, InterfaceClass) != INDEX_NONE
        || (Blueprint->ParentClass != nullptr && Blueprint->ParentClass->ImplementsInterface(InterfaceClass)))
    {
        OutMessage = TEXT("The requested Interface is already implemented directly or through inheritance.");
        return false;
    }
    for (TFieldIterator<UFunction> It(InterfaceClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        UFunction* Function = *It;
        const bool bAnimationFunction = Function->HasMetaData(FBlueprintMetadata::MD_AnimBlueprintFunction);
        if (bAnimationFunction && !Blueprint->IsA<UAnimBlueprint>())
        {
            OutMessage = FString::Printf(
                TEXT("Interface function %s requires an Animation Blueprint."),
                *Function->GetName());
            return false;
        }
        const bool bNeedsGraph = bAnimationFunction
            || (UEdGraphSchema_K2::CanKismetOverrideFunction(Function)
                && !UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(Function));
        if (bNeedsGraph && NameExists(Blueprint, Function->GetName()))
        {
            OutMessage = FString::Printf(
                TEXT("Interface function conflicts with an existing Blueprint declaration: %s."),
                *Function->GetName());
            return false;
        }
    }
    return true;
}

bool RemoveDirectInterface(
    UBlueprint* Blueprint,
    UClass* InterfaceClass,
    const bool bPreserveFunctions,
    const bool bApply,
    TArray<FString>& Comments,
    FString& OutMessage)
{
    const int32 InterfaceIndex = FindDirectInterfaceIndex(Blueprint, InterfaceClass);
    if (InterfaceIndex == INDEX_NONE)
    {
        OutMessage = TEXT("Only an Interface directly declared by this Blueprint can be removed.");
        return false;
    }
    const FBPInterfaceDescription& Description = Blueprint->ImplementedInterfaces[InterfaceIndex];
    TArray<UK2Node_Event*> InterfaceEvents;
    FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, InterfaceEvents);
    InterfaceEvents = InterfaceEvents.FilterByPredicate(
        [InterfaceClass](UK2Node_Event* Event)
        {
            return Event != nullptr
                && Event->EventReference.GetMemberParentClass(Event->GetBlueprintClassFromNode()) == InterfaceClass;
        });

    TArray<FAssetData> ChildAssets;
    FBlueprintEditorUtils::GetChildrenOfBlueprint(Blueprint, ChildAssets);
    Comments.Add(FString::Printf(
        TEXT("%s interface: %s; preserve functions: %s; graphs: %d; events: %d"),
        bApply ? TEXT("removed") : TEXT("would remove"),
        *InterfaceClass->GetPathName(),
        bPreserveFunctions ? TEXT("true") : TEXT("false"),
        Description.Graphs.Num(),
        InterfaceEvents.Num()));
    for (const FAssetData& ChildAsset : ChildAssets)
    {
        Comments.Add(FString::Printf(
            TEXT("%s child refresh: %s%s"),
            bApply ? TEXT("refreshed") : TEXT("would refresh"),
            *ChildAsset.GetObjectPathString(),
            ChildAsset.IsAssetLoaded() ? TEXT("") : TEXT(" (deferred until load)")));
    }
    if (!bApply)
    {
        return true;
    }

    Blueprint->Modify();
    FBPInterfaceDescription& MutableDescription = Blueprint->ImplementedInterfaces[InterfaceIndex];
    for (TFieldIterator<UFunction> It(InterfaceClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        UFunction* Function = *It;
        const FName FunctionName = Function->GetFName();
        if (FunctionName == UEdGraphSchema_K2::FN_ExecuteUbergraphBase
            || FBlueprintEditorUtils::RemoveInterfaceFunction(
                Blueprint,
                MutableDescription,
                Function,
                bPreserveFunctions))
        {
            continue;
        }

        UK2Node_Event* EventToRemove = nullptr;
        for (UK2Node_Event* Event : InterfaceEvents)
        {
            if (Event != nullptr && Event->EventReference.GetMemberName() == FunctionName)
            {
                EventToRemove = Event;
                break;
            }
        }
        if (EventToRemove == nullptr)
        {
            continue;
        }
        UEdGraph* EventGraph = EventToRemove->GetGraph();
        if (EventGraph == nullptr)
        {
            OutMessage = TEXT("An Interface Event has no owning Graph.");
            return false;
        }
        EventGraph->Modify();
        EventToRemove->Modify();
        if (bPreserveFunctions)
        {
            const UFunction* Signature = EventToRemove->FindEventSignatureFunction();
            if (Signature == nullptr)
            {
                OutMessage = FString::Printf(
                    TEXT("Interface Event %s has no preservable native signature."),
                    *FunctionName.ToString());
                return false;
            }
            UK2Node_CustomEvent* Replacement = UK2Node_CustomEvent::CreateFromFunction(
                FVector2D(EventToRemove->NodePosX, EventToRemove->NodePosY),
                EventGraph,
                FunctionName.ToString(),
                Signature,
                false);
            if (Replacement == nullptr)
            {
                OutMessage = FString::Printf(
                    TEXT("UE failed to preserve Interface Event %s as a Custom Event."),
                    *FunctionName.ToString());
                return false;
            }
            const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
            for (UEdGraphPin* OldPin : EventToRemove->Pins)
            {
                UEdGraphPin* NewPin = OldPin != nullptr ? Replacement->FindPin(OldPin->PinName) : nullptr;
                if (OldPin == nullptr || NewPin == nullptr)
                {
                    OutMessage = FString::Printf(
                        TEXT("UE could not preserve every Pin on Interface Event %s."),
                        *FunctionName.ToString());
                    return false;
                }
                Schema->MovePinLinks(*OldPin, *NewPin);
            }
        }
        EventGraph->RemoveNode(EventToRemove);
    }

    const int32 CurrentIndex = FindDirectInterfaceIndex(Blueprint, InterfaceClass);
    if (CurrentIndex == INDEX_NONE)
    {
        OutMessage = TEXT("Interface declaration disappeared during removal.");
        return false;
    }
    Blueprint->ImplementedInterfaces.RemoveAt(CurrentIndex, 1);
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    for (const FAssetData& ChildAsset : ChildAssets)
    {
        if (!ChildAsset.IsAssetLoaded())
        {
            continue;
        }
        UBlueprint* Child = Cast<UBlueprint>(ChildAsset.FastGetAsset(false));
        if (Child != nullptr)
        {
            Child->Modify();
            FBlueprintEditorUtils::MarkBlueprintAsModified(Child);
        }
    }
    return FindDirectInterfaceIndex(Blueprint, InterfaceClass) == INDEX_NONE;
}

bool ResolveFunctionPath(
    UBlueprint* Blueprint,
    const FString& Path,
    UFunction*& OutFunction,
    UClass*& OutOverrideClass,
    FString& OutMessage)
{
    OutFunction = nullptr;
    OutOverrideClass = nullptr;
    int32 Separator = INDEX_NONE;
    if (!Path.FindLastChar(TEXT(':'), Separator)
        || Separator <= 0
        || Separator + 1 >= Path.Len())
    {
        OutMessage = TEXT("function must be an exact <Class Path>:<FunctionName> reference.");
        return false;
    }
    const FString ClassPath = Path.Left(Separator);
    const FName FunctionName(*Path.Mid(Separator + 1));
    UClass* RequestedClass = FindObject<UClass>(nullptr, *ClassPath);
    if (RequestedClass == nullptr)
    {
        RequestedClass = LoadObject<UClass>(nullptr, *ClassPath);
    }
    UFunction* RequestedFunction = RequestedClass != nullptr
        ? RequestedClass->FindFunctionByName(FunctionName)
        : nullptr;
    UFunction* EffectiveFunction = nullptr;
    UClass* EffectiveOwner = FBlueprintEditorUtils::GetOverrideFunctionClass(
        Blueprint,
        FunctionName,
        &EffectiveFunction);
    if (RequestedClass == nullptr
        || RequestedFunction == nullptr
        || EffectiveFunction == nullptr
        || EffectiveOwner == nullptr
        || RequestedFunction->GetOwnerClass()->GetAuthoritativeClass()
            != EffectiveFunction->GetOwnerClass()->GetAuthoritativeClass())
    {
        OutMessage = TEXT("The Function Path does not name the effective overrideable Function in this Blueprint context.");
        return false;
    }
    const UClass* BlueprintClass = Blueprint->SkeletonGeneratedClass != nullptr
        ? Blueprint->SkeletonGeneratedClass.Get()
        : Blueprint->ParentClass.Get();
    const bool bReachableParent = BlueprintClass != nullptr && BlueprintClass->IsChildOf(RequestedClass);
    const bool bReachableInterface = RequestedClass->HasAnyClassFlags(CLASS_Interface)
        && FBlueprintEditorUtils::ImplementsInterface(Blueprint, true, RequestedClass);
    if ((!bReachableParent && !bReachableInterface)
        || !UEdGraphSchema_K2::CanKismetOverrideFunction(EffectiveFunction)
        || !Blueprint->AllowFunctionOverride(EffectiveFunction)
        || FObjectEditorUtils::IsFunctionHiddenFromClass(EffectiveFunction, BlueprintClass))
    {
        OutMessage = TEXT("UE does not expose this Function as an override for the resolved Blueprint.");
        return false;
    }
    OutFunction = EffectiveFunction;
    OutOverrideClass = EffectiveOwner;
    return true;
}

bool ImplementFunction(
    UBlueprint* Blueprint,
    const FString& FunctionPath,
    const bool bApply,
    bool& bChanged,
    UEdGraph*& OutGraph,
    UEdGraphNode*& OutNode,
    TArray<FString>& Comments,
    FString& OutMessage)
{
    bChanged = false;
    OutGraph = nullptr;
    OutNode = nullptr;
    if (Blueprint->BlueprintType == BPTYPE_MacroLibrary)
    {
        OutMessage = TEXT("Blueprint Macro Libraries cannot implement Functions.");
        return false;
    }
    UFunction* Function = nullptr;
    UClass* OverrideClass = nullptr;
    if (!ResolveFunctionPath(Blueprint, FunctionPath, Function, OverrideClass, OutMessage))
    {
        return false;
    }
    if (bApply)
    {
        FBlueprintEditorUtils::ConformImplementedInterfaces(Blueprint);
        if (!ResolveFunctionPath(Blueprint, FunctionPath, Function, OverrideClass, OutMessage))
        {
            return false;
        }
    }

    const FName FunctionName = Function->GetFName();
    if (UK2Node_Event* ExistingEvent = FBlueprintEditorUtils::FindOverrideForFunction(
        Blueprint,
        OverrideClass,
        FunctionName))
    {
        const UEdGraph* Graph = ExistingEvent->GetGraph();
        OutGraph = ExistingEvent->GetGraph();
        OutNode = ExistingEvent;
        Comments.Add(TEXT("implementation: node@") + GuidText(ExistingEvent->NodeGuid)
            + (Graph != nullptr ? TEXT(" in graph@") + GuidText(Graph->GraphGuid) : FString()));
        return true;
    }

    TSet<FName> GraphNames;
    FBlueprintEditorUtils::GetAllGraphNames(Blueprint, GraphNames);
    UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    const bool bAsEvent = UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(Function)
        && !GraphNames.Contains(FunctionName)
        && EventGraph != nullptr;
    if (bAsEvent)
    {
        if (FBlueprintEditorUtils::FindCustomEventNode(Blueprint, FunctionName) != nullptr)
        {
            OutMessage = TEXT("A same-named Custom Event conflicts with the requested Function implementation.");
            return false;
        }
        bChanged = true;
        if (!bApply)
        {
            Comments.Add(FString::Printf(
                TEXT("would implement function as Event Node in graph@%s"),
                *GuidText(EventGraph->GraphGuid)));
            return true;
        }
        UK2Node_Event* NewEvent = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_Event>(
            EventGraph,
            EventGraph->GetGoodPlaceForNewNode(),
            EK2NewNodeFlags::None,
            [FunctionName, OverrideClass](UK2Node_Event* Node)
            {
                Node->EventReference.SetExternalMember(FunctionName, OverrideClass);
                Node->bOverrideFunction = true;
            });
        if (NewEvent == nullptr || !NewEvent->NodeGuid.IsValid())
        {
            OutMessage = TEXT("UE failed to create the override Event Node.");
            return false;
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        OutGraph = EventGraph;
        OutNode = NewEvent;
        Comments.Add(TEXT("implemented function: node@") + GuidText(NewEvent->NodeGuid)
            + TEXT(" in graph@") + GuidText(EventGraph->GraphGuid));
        return true;
    }

    UEdGraph* ExistingGraph = FindObject<UEdGraph>(Blueprint, *FunctionName.ToString());
    if (ExistingGraph != nullptr)
    {
        bool bOwnedImplementation = Blueprint->FunctionGraphs.Contains(ExistingGraph);
        for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
        {
            bOwnedImplementation |= Interface.Graphs.Contains(ExistingGraph);
        }
        if (!bOwnedImplementation)
        {
            OutMessage = TEXT("A same-named non-Function Graph conflicts with the requested implementation.");
            return false;
        }
        Comments.Add(TEXT("implementation: graph@") + GuidText(ExistingGraph->GraphGuid));
        OutGraph = ExistingGraph;
        return true;
    }
    if (NameExists(Blueprint, FunctionName.ToString()))
    {
        OutMessage = TEXT("A Blueprint declaration conflicts with the requested Function implementation.");
        return false;
    }
    bChanged = true;
    if (!bApply)
    {
        Comments.Add(TEXT("would implement function as Function Graph: ") + FunctionName.ToString());
        return true;
    }
    UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
        Blueprint,
        FunctionName,
        UEdGraph::StaticClass(),
        UEdGraphSchema_K2::StaticClass());
    if (NewGraph == nullptr)
    {
        OutMessage = TEXT("UE failed to allocate the override Function Graph.");
        return false;
    }
    FBlueprintEditorUtils::AddFunctionGraph(Blueprint, NewGraph, false, OverrideClass);
    if (!Blueprint->FunctionGraphs.Contains(NewGraph) || !NewGraph->GraphGuid.IsValid())
    {
        OutMessage = TEXT("UE failed to register the override Function Graph.");
        return false;
    }
    Comments.Add(TEXT("implemented function: graph@") + GuidText(NewGraph->GraphGuid));
    OutGraph = NewGraph;
    return true;
}

bool InvokeOperation(
    UBlueprint* Blueprint,
    const FString& TargetAlias,
    const TSharedPtr<FJsonObject>& Statement,
    const bool bApply,
    TMap<FString, FCreatedObject>& Created,
    bool& bChanged,
    TArray<FString>& Comments,
    FString& OutMessage)
{
    bChanged = false;
    const TSharedPtr<FJsonObject>* TargetRef = nullptr;
    const TSharedPtr<FJsonObject>* Args = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
    FString Operation;
    if (!Statement->TryGetObjectField(TEXT("target"), TargetRef)
        || TargetRef == nullptr
        || !Statement->TryGetStringField(TEXT("operation"), Operation)
        || !Statement->TryGetObjectField(TEXT("args"), Args)
        || Args == nullptr
        || !Statement->TryGetArrayField(TEXT("outputs"), Outputs)
        || Outputs == nullptr)
    {
        OutMessage = TEXT("invoke is incomplete.");
        return false;
    }
    FString Kind;
    FString Identity;
    if (!ReadRef(*TargetRef, Kind, Identity))
    {
        OutMessage = TEXT("invoke target is invalid.");
        return false;
    }
    if (Kind == TEXT("local") && Identity == TargetAlias
        && (Operation == TEXT("ImplementInterface") || Operation == TEXT("RemoveInterface")))
    {
        if (!Outputs->IsEmpty())
        {
            OutMessage = TEXT("This Blueprint operation has no output object.");
            return false;
        }
        const TSharedPtr<FJsonValue>* InterfaceValue = (*Args)->Values.Find(TEXT("Interface"));
        const FString InterfacePath = InterfaceValue != nullptr ? ExprString(*InterfaceValue) : FString();
        UClass* InterfaceClass = nullptr;
        if (!ResolveInterfaceClass(InterfacePath, InterfaceClass))
        {
            OutMessage = TEXT("Interface argument does not resolve to a UE Interface Class.");
            return false;
        }
        if (Operation == TEXT("ImplementInterface"))
        {
            if ((*Args)->Values.Num() != 1)
            {
                OutMessage = TEXT("ImplementInterface accepts exactly Interface.");
                return false;
            }
            if (!ValidateInterfaceAddition(Blueprint, InterfaceClass, OutMessage))
            {
                return false;
            }
            bChanged = true;
            Comments.Add(FString::Printf(
                TEXT("%s interface: %s"),
                bApply ? TEXT("implemented") : TEXT("would implement"),
                *InterfaceClass->GetPathName()));
            if (bApply)
            {
                if (!FBlueprintEditorUtils::ImplementNewInterface(Blueprint, InterfaceClass->GetClassPathName()))
                {
                    OutMessage = TEXT("UE failed to implement the requested Interface.");
                    return false;
                }
                const int32 InterfaceIndex = FindDirectInterfaceIndex(Blueprint, InterfaceClass);
                if (InterfaceIndex == INDEX_NONE)
                {
                    OutMessage = TEXT("UE did not preserve the requested direct Interface declaration.");
                    return false;
                }
                int32 GraphIndex = 0;
                for (UEdGraph* Graph : Blueprint->ImplementedInterfaces[InterfaceIndex].Graphs)
                {
                    if (Graph == nullptr || !Graph->GraphGuid.IsValid())
                    {
                        continue;
                    }
                    FCreatedObject Navigation;
                    Navigation.Kind = TEXT("graph");
                    Navigation.StableId = GuidText(Graph->GraphGuid);
                    Navigation.Graph = Graph;
                    Created.Add(
                        FString::Printf(TEXT("__ImplementInterfaceGraph%d"), GraphIndex++),
                        Navigation);
                }
            }
            return true;
        }
        if ((*Args)->Values.Num() != 2)
        {
            OutMessage = TEXT("RemoveInterface requires exactly Interface and bPreserveFunctions.");
            return false;
        }
        bool bPreserveFunctions = false;
        if (!(*Args)->TryGetBoolField(TEXT("bPreserveFunctions"), bPreserveFunctions))
        {
            OutMessage = TEXT("RemoveInterface requires boolean bPreserveFunctions.");
            return false;
        }
        TArray<FGuid> InterfaceGraphIds;
        TSet<FGuid> ExistingNodeIds;
        if (bApply && bPreserveFunctions)
        {
            const int32 InterfaceIndex = FindDirectInterfaceIndex(Blueprint, InterfaceClass);
            if (InterfaceIndex != INDEX_NONE)
            {
                for (UEdGraph* Graph : Blueprint->ImplementedInterfaces[InterfaceIndex].Graphs)
                {
                    if (Graph != nullptr && Graph->GraphGuid.IsValid())
                    {
                        InterfaceGraphIds.Add(Graph->GraphGuid);
                    }
                }
            }
            for (UEdGraph* Graph : BlueprintGraphs(Blueprint))
            {
                if (Graph == nullptr)
                {
                    continue;
                }
                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    if (Node != nullptr && Node->NodeGuid.IsValid())
                    {
                        ExistingNodeIds.Add(Node->NodeGuid);
                    }
                }
            }
        }
        const bool bRemoved = RemoveDirectInterface(
            Blueprint,
            InterfaceClass,
            bPreserveFunctions,
            bApply,
            Comments,
            OutMessage);
        if (!bRemoved)
        {
            return false;
        }
        bChanged = true;
        if (bApply && bPreserveFunctions)
        {
            int32 NavigationIndex = 0;
            for (const FGuid& GraphId : InterfaceGraphIds)
            {
                if (UEdGraph* Graph = FindGraph(Blueprint, GuidText(GraphId)))
                {
                    FCreatedObject Navigation;
                    Navigation.Kind = TEXT("graph");
                    Navigation.StableId = GuidText(GraphId);
                    Navigation.Graph = Graph;
                    Created.Add(
                        FString::Printf(TEXT("__RemoveInterfaceGraph%d"), NavigationIndex++),
                        Navigation);
                }
            }
            for (UEdGraph* Graph : BlueprintGraphs(Blueprint))
            {
                if (Graph == nullptr)
                {
                    continue;
                }
                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    if (Node == nullptr
                        || !Node->NodeGuid.IsValid()
                        || ExistingNodeIds.Contains(Node->NodeGuid))
                    {
                        continue;
                    }
                    FCreatedObject Navigation;
                    Navigation.Kind = TEXT("node");
                    Navigation.StableId = GuidText(Node->NodeGuid);
                    Navigation.Graph = Graph;
                    Navigation.Node = Node;
                    Created.Add(
                        FString::Printf(TEXT("__RemoveInterfaceNode%d"), NavigationIndex++),
                        Navigation);
                }
            }
        }
        return true;
    }
    if (Kind == TEXT("local") && Identity == TargetAlias && Operation == TEXT("ImplementFunction"))
    {
        if (!Outputs->IsEmpty() || (*Args)->Values.Num() != 1)
        {
            OutMessage = TEXT("ImplementFunction accepts exactly function and has no output alias.");
            return false;
        }
        const TSharedPtr<FJsonValue>* FunctionValue = (*Args)->Values.Find(TEXT("function"));
        const FString FunctionPath = FunctionValue != nullptr ? ExprString(*FunctionValue) : FString();
        if (FunctionPath.IsEmpty())
        {
            OutMessage = TEXT("ImplementFunction requires an exact Function Path.");
            return false;
        }
        UEdGraph* ImplementationGraph = nullptr;
        UEdGraphNode* ImplementationNode = nullptr;
        const bool bImplemented = ImplementFunction(
            Blueprint,
            FunctionPath,
            bApply,
            bChanged,
            ImplementationGraph,
            ImplementationNode,
            Comments,
            OutMessage);
        if (bImplemented && ImplementationGraph != nullptr)
        {
            FCreatedObject Navigation;
            Navigation.Kind = ImplementationNode != nullptr ? TEXT("node") : TEXT("graph");
            Navigation.StableId = ImplementationNode != nullptr
                ? GuidText(ImplementationNode->NodeGuid)
                : GuidText(ImplementationGraph->GraphGuid);
            Navigation.Graph = ImplementationGraph;
            Navigation.Node = ImplementationNode;
            Created.Add(TEXT("__ImplementFunctionNavigation"), Navigation);
        }
        return bImplemented;
    }
    USCS_Node* Component = nullptr;
    UClass* PlannedComponentClass = nullptr;
    if (Kind == TEXT("component"))
    {
        Component = FindComponent(Blueprint, Identity);
    }
    else if (Kind == TEXT("local") && Identity != TargetAlias)
    {
        if (FCreatedObject* Local = Created.Find(Identity); Local != nullptr && Local->Kind == TEXT("component"))
        {
            Component = Local->Component;
            PlannedComponentClass = Local->PlannedClass;
            Kind = TEXT("component");
        }
    }
    if (Kind == TEXT("component")
        && (Operation == TEXT("MakeNewSceneRoot") || Operation == TEXT("Duplicate")))
    {
        if (Component == nullptr && PlannedComponentClass == nullptr)
        {
            OutMessage = TEXT("Component operation target no longer exists.");
            return false;
        }
        if (Operation == TEXT("MakeNewSceneRoot"))
        {
            if (!Outputs->IsEmpty())
            {
                OutMessage = TEXT("MakeNewSceneRoot has no output object.");
                return false;
            }
            UClass* ComponentClass = Component != nullptr ? Component->ComponentClass.Get() : PlannedComponentClass;
            if (ComponentClass == nullptr || !ComponentClass->IsChildOf(USceneComponent::StaticClass()))
            {
                OutMessage = TEXT("Only a Scene Component can become the Blueprint Scene Root.");
                return false;
            }
            if (Component == nullptr)
            {
                // A newly planned local Component is always authored by this Blueprint. The same
                // native checks run against its concrete handle during the apply pass.
                bChanged = true;
                return true;
            }
            FBlueprintSubobjectContext Context;
            if (!GatherBlueprintSubobjects(Blueprint, Context, OutMessage))
            {
                return false;
            }
            const FSubobjectDataHandle Handle = FindComponentHandle(Context, Blueprint, Component);
            const FSubobjectData* Data = Handle.GetData();
            const FSubobjectDataHandle CurrentRoot = Context.System->FindSceneRootForSubobject(Context.Actor);
            const FSubobjectData* CurrentRootData = CurrentRoot.GetData();
            const USceneComponent* NewRootTemplate = Data != nullptr
                ? Cast<USceneComponent>(Data->GetObjectForBlueprint(Blueprint))
                : nullptr;
            if (Data == nullptr
                || !Data->IsSceneComponent()
                || !Data->CanReparent()
                || Data->GetBlueprint() != Blueprint
                || NewRootTemplate == nullptr)
            {
                OutMessage = TEXT("UE native Subobject rules forbid promoting this Component to Scene Root.");
                return false;
            }
            if (Handle == CurrentRoot)
            {
                return true;
            }
            if (CurrentRootData == nullptr
                || (!CurrentRootData->IsDefaultSceneRoot() && !CurrentRootData->CanReparent()))
            {
                OutMessage = TEXT("The current Scene Root cannot be replaced by UE native Subobject rules.");
                return false;
            }
            TArray<FSubobjectDataHandle> FutureChildren;
            if (CurrentRootData->IsDefaultSceneRoot())
            {
                for (const FSubobjectDataHandle& Child : CurrentRootData->GetChildrenHandles())
                {
                    if (Child != Handle)
                    {
                        FutureChildren.Add(Child);
                    }
                }
            }
            else
            {
                FutureChildren.Add(CurrentRoot);
            }
            for (const FSubobjectDataHandle& ChildHandle : FutureChildren)
            {
                const FSubobjectData* ChildData = ChildHandle.GetData();
                const USceneComponent* ChildTemplate = ChildData != nullptr
                    ? Cast<USceneComponent>(ChildData->GetObjectForBlueprint(Blueprint))
                    : nullptr;
                if (ChildData == nullptr
                    || ChildTemplate == nullptr
                    || !NewRootTemplate->CanAttachAsChild(ChildTemplate, NAME_None)
                    || ChildTemplate->Mobility < NewRootTemplate->Mobility
                    || (NewRootTemplate->IsEditorOnly() && !ChildTemplate->IsEditorOnly()))
                {
                    OutMessage = TEXT("UE Designer attachment, Mobility, or Editor-only rules reject the resulting Scene Root hierarchy.");
                    return false;
                }
            }
            bChanged = true;
            if (bApply && !Context.System->MakeNewSceneRoot(Context.Actor, Handle, Blueprint))
            {
                OutMessage = TEXT("UE SubobjectDataSubsystem could not promote the Component to Scene Root.");
                return false;
            }
            if (bApply)
            {
                FBlueprintEditorUtils::PostEditChangeBlueprintActors(Blueprint, true);
            }
            return true;
        }

        if (Outputs->Num() != 1)
        {
            OutMessage = TEXT("Duplicate produces exactly one Component output alias.");
            return false;
        }
        const TSharedPtr<FJsonObject>* Output = nullptr;
        FString OutputAlias;
        FString Selector;
        if (!(*Outputs)[0].IsValid()
            || !(*Outputs)[0]->TryGetObject(Output)
            || Output == nullptr
            || !(*Output)->TryGetStringField(TEXT("alias"), OutputAlias)
            || OutputAlias.IsEmpty())
        {
            OutMessage = TEXT("Duplicate output alias is invalid.");
            return false;
        }
        (*Output)->TryGetStringField(TEXT("selector"), Selector);
        if (!Selector.IsEmpty())
        {
            OutMessage = TEXT("Duplicate has one unambiguous Component output and takes no selector.");
            return false;
        }
        if (OutputAlias == TargetAlias || Created.Contains(OutputAlias))
        {
            OutMessage = TEXT("Duplicate output alias is already defined.");
            return false;
        }
        UClass* ComponentClass = Component != nullptr ? Component->ComponentClass.Get() : PlannedComponentClass;
        if (Component == nullptr)
        {
            FCreatedObject Planned;
            Planned.Kind = TEXT("component");
            Planned.PlannedClass = ComponentClass;
            Created.Add(OutputAlias, Planned);
            bChanged = true;
            return true;
        }
        FBlueprintSubobjectContext Context;
        if (!GatherBlueprintSubobjects(Blueprint, Context, OutMessage))
        {
            return false;
        }
        const FSubobjectDataHandle Handle = FindComponentHandle(Context, Blueprint, Component);
        const FSubobjectData* Data = Handle.GetData();
        if (Data == nullptr || !Data->CanDuplicate())
        {
            OutMessage = TEXT("UE native Subobject rules forbid duplicating this Component.");
            return false;
        }
        if (!bApply)
        {
            FCreatedObject Planned;
            Planned.Kind = TEXT("component");
            Planned.PlannedClass = ComponentClass;
            Created.Add(OutputAlias, Planned);
            bChanged = true;
            return true;
        }
        TArray<FSubobjectDataHandle> NewHandles;
        Context.System->DuplicateSubobjects(Context.Actor, {Handle}, Blueprint, NewHandles);
        if (NewHandles.Num() != 1)
        {
            OutMessage = TEXT("UE SubobjectDataSubsystem did not produce exactly one duplicated Component.");
            return false;
        }
        USCS_Node* NewComponent = FindComponentForHandle(Blueprint, NewHandles[0]);
        if (NewComponent == nullptr)
        {
            OutMessage = TEXT("Duplicated Component could not be resolved back to its authored SCS object.");
            return false;
        }
        FCreatedObject CreatedComponent;
        CreatedComponent.Kind = TEXT("component");
        CreatedComponent.StableId = GuidText(NewComponent->VariableGuid);
        CreatedComponent.PlannedClass = NewComponent->ComponentClass;
        CreatedComponent.Component = NewComponent;
        Created.Add(OutputAlias, CreatedComponent);
        bChanged = true;
        return true;
    }
    OutMessage = FString::Printf(TEXT("Operation %s is not available on this exact target."), *Operation);
    return false;
}

void AddPatchReadback(
    FSalObjectBuilder& Builder,
    UBlueprint* Blueprint,
    const FString& BlueprintAlias,
    const FString& Kind,
    const FString& Id,
    TSet<FString>& Emitted)
{
    if (Kind.IsEmpty() || Id.IsEmpty())
    {
        return;
    }
    const FString Key = Kind + TEXT("@") + Id;
    if (Emitted.Contains(Key))
    {
        return;
    }
    if (Kind == TEXT("variable") || Kind == TEXT("dispatcher"))
    {
        const bool bDispatcher = Kind == TEXT("dispatcher");
        if (FBPVariableDescription* Variable = FindVariable(Blueprint, Id, bDispatcher))
        {
            AddVariableBinding(Builder, BlueprintAlias, *Variable, bDispatcher);
            if (bDispatcher)
            {
                if (UEdGraph* Signature = FindDispatcherGraph(Blueprint, Variable->VarName.ToString()))
                {
                    AddGraphBinding(Builder, BlueprintAlias, Signature);
                }
            }
            Emitted.Add(Key);
        }
        return;
    }
    if (Kind == TEXT("graph"))
    {
        if (UEdGraph* Graph = FindGraph(Blueprint, Id))
        {
            AddGraphBinding(Builder, BlueprintAlias, Graph);
            Emitted.Add(Key);
        }
        return;
    }
    if (Kind == TEXT("node"))
    {
        UEdGraphNode* Node = nullptr;
        UEdGraph* Graph = nullptr;
        for (UEdGraph* CandidateGraph : BlueprintGraphs(Blueprint))
        {
            if (CandidateGraph == nullptr)
            {
                continue;
            }
            for (UEdGraphNode* CandidateNode : CandidateGraph->Nodes)
            {
                if (CandidateNode != nullptr
                    && GuidText(CandidateNode->NodeGuid).Equals(Id, ESearchCase::IgnoreCase))
                {
                    if (Node != nullptr)
                    {
                        return;
                    }
                    Node = CandidateNode;
                    Graph = CandidateGraph;
                }
            }
        }
        if (Node == nullptr || Graph == nullptr)
        {
            return;
        }
        const FString GraphKey = TEXT("graph@") + GuidText(Graph->GraphGuid);
        const FString GraphAlias = Builder.UniqueAlias(Graph->GetName());
        Builder.AddLocalBinding(GraphAlias, GraphValue(Graph, BlueprintAlias));
        Emitted.Add(GraphKey);

        FString PreferredAlias = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
        if (PreferredAlias.IsEmpty())
        {
            PreferredAlias = Node->GetClass()->GetName();
        }
        const FString NodeAlias = Builder.UniqueAlias(PreferredAlias);
        TSharedPtr<FJsonObject> NodeArgs = CallArgs();
        SetArg(NodeArgs, TEXT("graph"), Value::Local(GraphAlias));
        SetArg(NodeArgs, TEXT("id"), Value::String(GuidText(Node->NodeGuid)));
        SetArg(NodeArgs, TEXT("type"), Value::String(Node->GetClass()->GetPathName()));
        Builder.AddLocalBinding(NodeAlias, Call(TEXT("node"), NodeArgs));

        TSet<FString> PinMembers;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin == nullptr)
            {
                continue;
            }
            FString Member = Pin->PinName.IsNone() ? TEXT("pin") : Pin->PinName.ToString();
            const FString BaseMember = Member;
            int32 Suffix = 2;
            while (PinMembers.Contains(Member))
            {
                Member = BaseMember + LexToString(Suffix++);
            }
            PinMembers.Add(Member);
            TSharedPtr<FJsonObject> PinArgs = CallArgs();
            SetArg(PinArgs, TEXT("id"), Value::String(GuidText(Pin->PinId)));
            SetArg(PinArgs, TEXT("type"), Value::String(PinTypeText(Pin->PinType)));
            SetArg(PinArgs, TEXT("direction"), Value::Name(Pin->Direction == EGPD_Output ? TEXT("out") : TEXT("in")));
            if (!Pin->DefaultValue.IsEmpty())
            {
                SetArg(PinArgs, TEXT("DefaultValue"), Value::String(Pin->DefaultValue));
            }
            Builder.AddMemberBinding(NodeAlias, {Member}, Call(TEXT("pin"), PinArgs));
        }
        Emitted.Add(Key);
        return;
    }
    if (Kind == TEXT("component"))
    {
        if (USCS_Node* Component = FindComponent(Blueprint, Id))
        {
            AddComponentBinding(Builder, Blueprint, BlueprintAlias, Component, true);
            Emitted.Add(Key);
        }
    }
}

struct FCompilerReadbackContext
{
    FSalObjectBuilder& Builder;
    TMap<const UBlueprint*, FString> BlueprintAliases;
    TMap<const UEdGraph*, FString> GraphAliases;
    TMap<const UEdGraphNode*, FString> NodeAliases;
    TMap<const UEdGraphNode*, TSet<FString>> PinMembers;
    TSet<FGuid> Pins;

    FString EnsureBlueprint(UBlueprint* Blueprint)
    {
        if (const FString* Existing = BlueprintAliases.Find(Blueprint))
        {
            return *Existing;
        }
        if (Blueprint == nullptr)
        {
            return FString();
        }
        const FBlueprintOutputContext Context = AddBlueprintObject(
            Builder,
            Blueprint,
            Blueprint->GetName(),
            false);
        BlueprintAliases.Add(Blueprint, Context.BlueprintAlias);
        return Context.BlueprintAlias;
    }

    FString EnsureGraph(UEdGraph* Graph)
    {
        if (const FString* Existing = GraphAliases.Find(Graph))
        {
            return *Existing;
        }
        UBlueprint* Owner = Graph != nullptr
            ? FBlueprintEditorUtils::FindBlueprintForGraph(Graph)
            : nullptr;
        const FString BlueprintAlias = EnsureBlueprint(Owner);
        if (Graph == nullptr || BlueprintAlias.IsEmpty() || !Graph->GraphGuid.IsValid())
        {
            return FString();
        }
        const FString Alias = Builder.UniqueAlias(Graph->GetName());
        Builder.AddLocalBinding(Alias, GraphValue(Graph, BlueprintAlias));
        GraphAliases.Add(Graph, Alias);
        return Alias;
    }

    FString EnsureNode(UEdGraphNode* Node)
    {
        if (const FString* Existing = NodeAliases.Find(Node))
        {
            return *Existing;
        }
        UEdGraph* Graph = Node != nullptr ? Node->GetGraph() : nullptr;
        const FString GraphAlias = EnsureGraph(Graph);
        if (Node == nullptr || GraphAlias.IsEmpty() || !Node->NodeGuid.IsValid())
        {
            return FString();
        }
        FString PreferredAlias = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
        if (PreferredAlias.IsEmpty())
        {
            PreferredAlias = Node->GetClass()->GetName();
        }
        const FString Alias = Builder.UniqueAlias(PreferredAlias);
        TSharedPtr<FJsonObject> Args = CallArgs();
        SetArg(Args, TEXT("graph"), Value::Local(GraphAlias));
        SetArg(Args, TEXT("id"), Value::String(GuidText(Node->NodeGuid)));
        SetArg(Args, TEXT("type"), Value::String(Node->GetClass()->GetPathName()));
        Builder.AddLocalBinding(Alias, Call(TEXT("node"), Args));
        NodeAliases.Add(Node, Alias);
        return Alias;
    }

    bool EnsurePin(const UEdGraphPin* Pin)
    {
        if (Pin == nullptr || !Pin->PinId.IsValid())
        {
            return false;
        }
        if (Pins.Contains(Pin->PinId))
        {
            return true;
        }
        UEdGraphNode* Node = Pin->GetOwningNode();
        const FString NodeAlias = EnsureNode(Node);
        if (Node == nullptr || NodeAlias.IsEmpty())
        {
            return false;
        }
        FString Member = Pin->PinName.IsNone() ? TEXT("pin") : Pin->PinName.ToString();
        TSet<FString>& UsedMembers = PinMembers.FindOrAdd(Node);
        const FString BaseMember = Member;
        int32 Suffix = 2;
        while (UsedMembers.Contains(Member))
        {
            Member = BaseMember + LexToString(Suffix++);
        }
        UsedMembers.Add(Member);
        TSharedPtr<FJsonObject> Args = CallArgs();
        SetArg(Args, TEXT("id"), Value::String(GuidText(Pin->PinId)));
        SetArg(Args, TEXT("type"), Value::String(PinTypeText(Pin->PinType)));
        SetArg(Args, TEXT("direction"), Value::Name(Pin->Direction == EGPD_Output ? TEXT("out") : TEXT("in")));
        if (!Pin->DefaultValue.IsEmpty())
        {
            SetArg(Args, TEXT("DefaultValue"), Value::String(Pin->DefaultValue));
        }
        Builder.AddMemberBinding(NodeAlias, {Member}, Call(TEXT("pin"), Args));
        Pins.Add(Pin->PinId);
        return true;
    }
};

void AddCompilerDiagnostics(
    FSalObjectBuilder& Builder,
    UBlueprint* Blueprint,
    const FString& BlueprintAlias,
    const FCompilerResultsLog& Log)
{
    FCompilerReadbackContext Context{Builder};
    Context.BlueprintAliases.Add(Blueprint, BlueprintAlias);

    int32 MessageCount = Log.Messages.Num();
    if (MessageCount > 0
        && Log.Messages.Last()->GetSeverity() == EMessageSeverity::Info)
    {
        // FBlueprintCompilationManager closes the top-level compiler event by
        // appending one localized timing summary. The passed log uses its
        // default non-detailed event mode, so this final message is provenance,
        // not a source diagnostic.
        --MessageCount;
    }
    for (int32 MessageIndex = 0; MessageIndex < MessageCount; ++MessageIndex)
    {
        const TSharedRef<FTokenizedMessage>& Message = Log.Messages[MessageIndex];
        TArray<FString> Refs;
        for (const TSharedRef<IMessageToken>& Token : Message->GetMessageTokens())
        {
            if (Token->GetType() != EMessageToken::EdGraph)
            {
                continue;
            }
            const TSharedRef<FEdGraphToken> GraphToken = StaticCastSharedRef<FEdGraphToken>(Token);
            const UEdGraphPin* Pin = GraphToken->GetPin();
            if (Pin != nullptr)
            {
                Pin = Log.FindSourcePin(Pin);
            }
            const UObject* GraphObject = GraphToken->GetGraphObject();
            if (GraphObject != nullptr)
            {
                GraphObject = Log.FindSourceObject(GraphObject);
            }
            UEdGraphNode* Node = const_cast<UEdGraphNode*>(Cast<UEdGraphNode>(GraphObject));
            UEdGraph* Graph = const_cast<UEdGraph*>(Cast<UEdGraph>(GraphObject));
            if (Pin != nullptr)
            {
                Node = Pin->GetOwningNode();
            }
            if (Node != nullptr)
            {
                Graph = Node->GetGraph();
            }
            if (Graph != nullptr && Context.EnsureGraph(Graph).IsEmpty())
            {
                Graph = nullptr;
            }
            if (Graph != nullptr)
            {
                Refs.AddUnique(TEXT("graph@") + GuidText(Graph->GraphGuid));
            }
            if (Node != nullptr && !Context.EnsureNode(Node).IsEmpty())
            {
                Refs.AddUnique(TEXT("node@") + GuidText(Node->NodeGuid));
            }
            if (Pin != nullptr && Context.EnsurePin(Pin))
            {
                Refs.AddUnique(TEXT("pin@") + GuidText(Pin->PinId));
            }
        }

        FString Severity = TEXT("info");
        if (Message->GetSeverity() == EMessageSeverity::Error)
        {
            Severity = TEXT("error");
        }
        else if (Message->GetSeverity() == EMessageSeverity::Warning
            || Message->GetSeverity() == EMessageSeverity::PerformanceWarning)
        {
            Severity = TEXT("warning");
        }
        Builder.AddComment(
            Severity
            + (Refs.IsEmpty() ? FString() : TEXT(" ") + FString::Join(Refs, TEXT(" ")))
            + TEXT(": ")
            + Message->ToText().ToString());
    }
}

TSharedPtr<FJsonObject> BlueprintCompileObject(
    UBlueprint* Blueprint,
    const FString& Alias,
    const TArray<FString>& Comments,
    const int32 DiagnosticInsertIndex,
    const FCompilerResultsLog& Log)
{
    FSalObjectBuilder Builder;
    const FBlueprintOutputContext Context = AddBlueprintObject(Builder, Blueprint, Alias, true);
    const int32 InsertAt = FMath::Clamp(DiagnosticInsertIndex, 0, Comments.Num());
    for (int32 Index = 0; Index < InsertAt; ++Index)
    {
        Builder.AddComment(Comments[Index]);
    }
    AddCompilerDiagnostics(Builder, Blueprint, Context.BlueprintAlias, Log);
    for (int32 Index = InsertAt; Index < Comments.Num(); ++Index)
    {
        Builder.AddComment(Comments[Index]);
    }
    return Builder.BuildObject();
}

TSharedPtr<FJsonObject> BlueprintPatchObject(
    UBlueprint* Blueprint,
    const FString& Alias,
    const TArray<FString>& Comments,
    const FSalPatch* Patch = nullptr,
    const TMap<FString, FCreatedObject>* Created = nullptr)
{
    FSalObjectBuilder Builder;
    const FBlueprintOutputContext Context = AddBlueprintObject(Builder, Blueprint, Alias, true);
    TSet<FString> Emitted;
    if (Created != nullptr)
    {
        for (const TPair<FString, FCreatedObject>& Pair : *Created)
        {
            AddPatchReadback(
                Builder,
                Blueprint,
                Context.BlueprintAlias,
                Pair.Value.Kind,
                Pair.Value.StableId,
                Emitted);
        }
    }
    if (Patch != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : Patch->Statements)
        {
            const TSharedPtr<FJsonObject>* Statement = nullptr;
            const TSharedPtr<FJsonObject>* Target = nullptr;
            if (!Value.IsValid()
                || !Value->TryGetObject(Statement)
                || Statement == nullptr
                || !(*Statement)->TryGetObjectField(TEXT("target"), Target)
                || Target == nullptr)
            {
                continue;
            }
            FString Kind;
            FString Id;
            if (ReadRef(*Target, Kind, Id))
            {
                if (Kind == TEXT("local") && Created != nullptr)
                {
                    if (const FCreatedObject* Local = Created->Find(Id))
                    {
                        Kind = Local->Kind;
                        Id = Local->StableId;
                    }
                }
                AddPatchReadback(Builder, Blueprint, Context.BlueprintAlias, Kind, Id, Emitted);
                continue;
            }
            TSharedPtr<FJsonObject> Owner;
            TArray<FString> Path;
            if (ReadMember(*Target, Owner, Path) && ReadRef(Owner, Kind, Id))
            {
                if (Kind == TEXT("local") && Created != nullptr)
                {
                    const FCreatedObject* Local = Created->Find(Id);
                    if (Local == nullptr && Id == Patch->Alias && !Path.IsEmpty())
                    {
                        Local = Created->Find(Path[0]);
                    }
                    if (Local != nullptr)
                    {
                        Kind = Local->Kind;
                        Id = Local->StableId;
                    }
                }
                AddPatchReadback(Builder, Blueprint, Context.BlueprintAlias, Kind, Id, Emitted);
            }
        }
    }
    for (const FString& Comment : Comments)
    {
        Builder.AddComment(Comment);
    }
    return Builder.BuildObject();
}

UBlueprint* MakeTransientBlueprintPlan(UBlueprint* Source, FString& OutMessage)
{
    if (Source == nullptr)
    {
        OutMessage = TEXT("Blueprint is unavailable for transient preflight.");
        return nullptr;
    }
    UPackage* TransientPackage = GetTransientPackage();
    UClass* BlueprintGeneratedClassType = Source->GetBlueprintClass();
    if (BlueprintGeneratedClassType == nullptr)
    {
        OutMessage = TEXT("Blueprint subtype does not expose a generated Class type for transient preflight.");
        return nullptr;
    }
    const bool bWasDuplicatingReadOnly = Source->bDuplicatingReadOnly;
    Source->bDuplicatingReadOnly = true;
    UBlueprint* Copy = Cast<UBlueprint>(StaticDuplicateObject(
        Source,
        TransientPackage,
        MakeUniqueObjectName(TransientPackage, Source->GetClass(), Source->GetFName())));
    Source->bDuplicatingReadOnly = bWasDuplicatingReadOnly;
    if (Copy == nullptr)
    {
        OutMessage = TEXT("UE failed to duplicate the Blueprint into a transient preflight model.");
        return nullptr;
    }
    Copy->SetFlags(RF_Transient | RF_Transactional);

    UClass* PlanGeneratedClass = NewObject<UClass>(
        TransientPackage,
        BlueprintGeneratedClassType,
        MakeUniqueObjectName(TransientPackage, BlueprintGeneratedClassType, FName(*(Source->GetName() + TEXT("_Plan_C")))),
        RF_Transient | RF_Transactional);
    if (PlanGeneratedClass == nullptr)
    {
        OutMessage = TEXT("UE failed to create a transient generated Class for Blueprint preflight.");
        return nullptr;
    }
    PlanGeneratedClass->SetSuperStruct(Copy->ParentClass);
    PlanGeneratedClass->ClassGeneratedBy = Copy;
    Copy->GeneratedClass = PlanGeneratedClass;
    Copy->SkeletonGeneratedClass = PlanGeneratedClass;

    if (Source->SimpleConstructionScript != nullptr)
    {
        USimpleConstructionScript* PlanSCS = Cast<USimpleConstructionScript>(StaticDuplicateObject(
            Source->SimpleConstructionScript,
            PlanGeneratedClass,
            Source->SimpleConstructionScript->GetFName()));
        UBlueprintGeneratedClass* PlanBPGC = Cast<UBlueprintGeneratedClass>(PlanGeneratedClass);
        if (PlanSCS == nullptr || PlanBPGC == nullptr)
        {
            OutMessage = TEXT("UE failed to duplicate SimpleConstructionScript state for transient preflight.");
            return nullptr;
        }
        PlanSCS->SetFlags(RF_Transient | RF_Transactional);
        Copy->SimpleConstructionScript = PlanSCS;
        PlanBPGC->SimpleConstructionScript = PlanSCS;
        for (USCS_Node* Node : PlanSCS->GetAllNodes())
        {
            if (Node == nullptr)
            {
                continue;
            }
            Node->SetFlags(RF_Transient | RF_Transactional);
            if (Node->ComponentTemplate != nullptr)
            {
                UActorComponent* Template = Cast<UActorComponent>(StaticDuplicateObject(
                    Node->ComponentTemplate,
                    PlanGeneratedClass,
                    Node->ComponentTemplate->GetFName()));
                if (Template == nullptr)
                {
                    OutMessage = TEXT("UE failed to duplicate a Component Template for transient preflight.");
                    return nullptr;
                }
                Template->SetFlags(RF_Transient | RF_Transactional);
                Node->ComponentTemplate = Template;
            }
        }
    }
    return Copy;
}

void ConvertAppliedCommentsToPlan(TArray<FString>& Comments)
{
    for (FString& Comment : Comments)
    {
        if (Comment.StartsWith(TEXT("added: ")))
        {
            Comment = TEXT("would add: ") + Comment.Mid(7);
        }
        else if (Comment.StartsWith(TEXT("applied: ")))
        {
            Comment = TEXT("would apply: ") + Comment.Mid(9);
        }
        else if (Comment.StartsWith(TEXT("removed: ")))
        {
            Comment = TEXT("would remove: ") + Comment.Mid(9);
        }
        else if (Comment.StartsWith(TEXT("implemented interface: ")))
        {
            Comment = TEXT("would implement interface: ") + Comment.Mid(23);
        }
        else if (Comment.StartsWith(TEXT("removed interface: ")))
        {
            Comment = TEXT("would remove interface: ") + Comment.Mid(19);
        }
        else if (Comment.StartsWith(TEXT("refreshed child: ")))
        {
            Comment = TEXT("would refresh child: ") + Comment.Mid(17);
        }
        else if (Comment.StartsWith(TEXT("implemented function: ")))
        {
            Comment = TEXT("would implement function: ") + Comment.Mid(22);
        }
        else if (Comment.StartsWith(TEXT("changed Variable type: ")))
        {
            Comment = TEXT("would change Variable type: ") + Comment.Mid(23);
        }
        else if (Comment.StartsWith(TEXT("reparented: ")))
        {
            Comment = TEXT("would reparent: ") + Comment.Mid(12);
        }
        Comment.ReplaceInline(TEXT("will remove interface:"), TEXT("would remove interface:"));
        Comment.ReplaceInline(TEXT("interface child refresh:"), TEXT("would refresh child:"));
    }
}

TSharedPtr<FJsonObject> BuildBlueprintPlan(
    const FSalPatch& Patch,
    const TArray<FString>& Effects)
{
    TSharedPtr<FJsonObject> Plan = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Operations;
    Operations.Reserve(Patch.Statements.Num());
    for (int32 Index = 0; Index < Patch.Statements.Num(); ++Index)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        if (!Patch.Statements[Index].IsValid()
            || !Patch.Statements[Index]->TryGetObject(Statement)
            || Statement == nullptr)
        {
            continue;
        }
        FString Kind;
        if (!ReadKind(*Statement, Kind))
        {
            Kind = TEXT("binding");
        }
        TSharedPtr<FJsonObject> Operation = MakeShared<FJsonObject>();
        Operation->SetNumberField(TEXT("index"), Index);
        Operation->SetStringField(TEXT("operation"), Kind);
        const TSharedPtr<FJsonObject>* Target = nullptr;
        if ((*Statement)->TryGetObjectField(TEXT("target"), Target) && Target != nullptr)
        {
            const FString Ref = RefKey(*Target);
            if (!Ref.IsEmpty())
            {
                Operation->SetStringField(TEXT("ref"), Ref);
            }
        }
        FString InvokeOperation;
        if (Kind == TEXT("invoke") && (*Statement)->TryGetStringField(TEXT("operation"), InvokeOperation))
        {
            Operation->SetStringField(TEXT("invoke"), InvokeOperation);
        }
        Operations.Add(MakeShared<FJsonValueObject>(Operation));
    }
    Plan->SetArrayField(TEXT("operations"), Operations);
    TArray<TSharedPtr<FJsonValue>> EffectValues;
    EffectValues.Reserve(Effects.Num());
    for (const FString& Effect : Effects)
    {
        EffectValues.Add(MakeShared<FJsonValueString>(Effect));
    }
    Plan->SetArrayField(TEXT("effects"), EffectValues);
    return Plan;
}

bool ValidateTerminalSequence(const TArray<TSharedPtr<FJsonValue>>& Statements, bool& bCompile, bool& bSave)
{
    bCompile = false;
    bSave = false;
    if (Statements.Num() < 1 || Statements.Num() > 2)
    {
        return false;
    }
    for (int32 Index = 0; Index < Statements.Num(); ++Index)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        FString Kind;
        if (!Statements[Index].IsValid()
            || !Statements[Index]->TryGetObject(Statement)
            || Statement == nullptr
            || !ReadKind(*Statement, Kind))
        {
            return false;
        }
        if (Kind == TEXT("compile") && Index == 0 && !bCompile)
        {
            bCompile = true;
        }
        else if (Kind == TEXT("save") && Index == Statements.Num() - 1 && !bSave)
        {
            bSave = true;
        }
        else
        {
            return false;
        }
    }
    return bCompile || bSave;
}
}

TSharedPtr<FJsonObject> FSalBlueprintInterface::Patch(
    const FSalPatch& Patch,
    const FSalResolvedTarget& Target)
{
    UBlueprint* Blueprint = Target.Blueprint;
    if (Blueprint == nullptr || Target.Kind != ESalTargetKind::Blueprint)
    {
        return MutationError(
            Patch,
            Target,
            TEXT("capability.interface_unavailable"),
            TEXT("The blueprint interface requires a resolved Blueprint target."),
            TEXT("patch"));
    }

    bool bCompile = false;
    bool bSave = false;
    bool bHasTerminal = false;
    for (const TSharedPtr<FJsonValue>& Value : Patch.Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        FString Kind;
        if (Value.IsValid() && Value->TryGetObject(Statement) && Statement != nullptr && ReadKind(*Statement, Kind))
        {
            bHasTerminal |= Kind == TEXT("compile") || Kind == TEXT("save");
        }
    }
    if (bHasTerminal)
    {
        if (!ValidateTerminalSequence(Patch.Statements, bCompile, bSave))
        {
            return MutationError(
                Patch,
                Target,
                TEXT("validation.finalization_must_be_independent"),
                TEXT("Blueprint terminal Patch must be compile, save, or compile followed by save, with no source mutations."),
                TEXT("compile"));
        }
        if (bCompile && Blueprint->BlueprintType == BPTYPE_MacroLibrary)
        {
            return MutationError(
                Patch,
                Target,
                TEXT("capability.compile_unavailable"),
                TEXT("UE disables direct compilation for Blueprint Macro Libraries."),
                TEXT("compile"));
        }
        if (bCompile && !IsBlueprintCompileAllowedDuringPIE(Blueprint))
        {
            return MutationError(
                Patch,
                Target,
                TEXT("capability.compile_unavailable"),
                TEXT("Current UE PIE safety rules forbid recompiling this Blueprint family."),
                TEXT("compile"));
        }
        FString SaveUnavailableReason;
        if (bSave && !CanSaveBlueprintNow(&SaveUnavailableReason))
        {
            return MutationError(
                Patch,
                Target,
                TEXT("capability.save_unavailable"),
                SaveUnavailableReason,
                TEXT("save"));
        }
        TArray<FString> Comments;
        if (Patch.bDryRun)
        {
            if (bCompile) Comments.Add(TEXT("would compile: full; compiler diagnostics are not predicted"));
            if (bSave) Comments.Add(FString::Printf(TEXT("would save: %s"), *Blueprint->GetOutermost()->GetName()));
            return MakeMutationResult(
                BlueprintPatchObject(Blueprint, Patch.Alias, Comments),
                {},
                true,
                true,
                false,
                Target.AssetPath,
                TEXT("patch"),
                BuildBlueprintPlan(Patch, Comments));
        }

        bool bApplied = false;
        TUniquePtr<FCompilerResultsLog> CompileLog;
        int32 DiagnosticInsertIndex = INDEX_NONE;
        if (bCompile)
        {
            UPackage* Package = Blueprint->GetOutermost();
            const bool bWasDirty = Package != nullptr && Package->IsDirty();
            CompileLog = MakeUnique<FCompilerResultsLog>();
            CompileLog->SetSourcePath(Blueprint->GetPathName());
            EBlueprintCompileOptions Options = EBlueprintCompileOptions::SkipSave;
            if (GEditor != nullptr && GEditor->PlayWorld != nullptr)
            {
                Options |= EBlueprintCompileOptions::IncludeCDOInReferenceReplacement;
            }
            FKismetEditorUtilities::CompileBlueprint(Blueprint, Options, CompileLog.Get());
            if (Package != nullptr && !bWasDirty)
            {
                Package->SetDirtyFlag(false);
            }
            Comments.Add(FString::Printf(
                TEXT("compile: %s; %d errors; %d warnings"),
                *EnumName(StaticEnum<EBlueprintStatus>(), Blueprint->Status),
                CompileLog->NumErrors,
                CompileLog->NumWarnings));
            DiagnosticInsertIndex = Comments.Num();
            bApplied = true;
        }
        if (bSave)
        {
            UPackage* Package = Blueprint->GetOutermost();
            if (Package == nullptr)
            {
                return MutationError(Patch, Target, TEXT("resolution.package_not_found"), TEXT("Blueprint owning Package is unavailable."), TEXT("save"), FString(), FString(), bApplied);
            }
            if (!Package->IsDirty())
            {
                Comments.Add(TEXT("save: already clean"));
            }
            else
            {
                TArray<UPackage*> Packages{Package};
                if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, true))
                {
                    return MutationError(Patch, Target, TEXT("validation.save_failed"), TEXT("UE failed to save the Blueprint Package."), TEXT("save"), Package->GetName(), FString(), bApplied);
                }
                Comments.Add(TEXT("save: saved"));
                bApplied = true;
            }
        }
        return MakeMutationResult(
            CompileLog != nullptr
                ? BlueprintCompileObject(
                    Blueprint,
                    Patch.Alias,
                    Comments,
                    DiagnosticInsertIndex,
                    *CompileLog)
                : BlueprintPatchObject(Blueprint, Patch.Alias, Comments),
            {},
            false,
            true,
            bApplied,
            Target.AssetPath,
            TEXT("patch"));
    }

    TMap<FString, FPendingBinding> Bindings;
    TSet<FName> PendingNames;
    for (const TSharedPtr<FJsonValue>& Value : Patch.Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(Statement) || Statement == nullptr)
        {
            return MutationError(Patch, Target, TEXT("validation.patch_state_invalid"), TEXT("Decoded Patch contains a statement that the Blueprint executor cannot represent."), TEXT("patch"));
        }
        if (!(*Statement)->HasField(TEXT("kind")))
        {
            FPendingBinding Binding;
            if (!DecodeBinding(*Statement, Binding)
                || Bindings.Contains(Binding.Key)
                || PendingNames.Contains(FName(*Binding.Name)))
            {
                return MutationError(Patch, Target, TEXT("validation.creation_invalid"), TEXT("Creation binding is invalid or duplicated in this Blueprint Patch."), TEXT("binding"));
            }
            FString Message;
            if (!ValidateBinding(Blueprint, Binding, Patch.Alias, Message))
            {
                return MutationError(Patch, Target, TEXT("validation.creation_invalid"), Message, TEXT("add"), Binding.Key, TEXT("Refresh the exact Palette entry with with schema."));
            }
            PendingNames.Add(FName(*Binding.Name));
            Bindings.Add(Binding.Key, MoveTemp(Binding));
        }
    }

    auto Execute = [&](const bool bApply, TMap<FString, FCreatedObject>& Created, TArray<FString>& Comments, bool& bChanged) -> TSharedPtr<FJsonObject>
    {
        bChanged = false;
        TSet<FString> ConsumedBindings;
        for (int32 Index = 0; Index < Patch.Statements.Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>* Statement = nullptr;
            Patch.Statements[Index]->TryGetObject(Statement);
            if (Statement == nullptr || !(*Statement).IsValid() || !(*Statement)->HasField(TEXT("kind")))
            {
                continue;
            }
            FString Kind;
            ReadKind(*Statement, Kind);
            FString Message;
            if (Kind == TEXT("add"))
            {
                const TSharedPtr<FJsonObject>* TargetRef = nullptr;
                if (!(*Statement)->TryGetObjectField(TEXT("target"), TargetRef) || TargetRef == nullptr)
                {
                    return MutationError(Patch, Target, TEXT("validation.invalid_add"), TEXT("Decoded add has no resolvable binding target."), Kind);
                }
                const FString Key = RefKey(*TargetRef);
                const FPendingBinding* Binding = Bindings.Find(Key);
                if (Binding == nullptr || ConsumedBindings.Contains(Key))
                {
                    return MutationError(Patch, Target, TEXT("resolution.binding_not_found"), TEXT("add must consume one preceding, unconsumed Palette binding."), Kind, Key);
                }
                if (!ValidateAddPlacement(Blueprint, *Binding, *Statement, Message))
                {
                    return MutationError(Patch, Target, TEXT("validation.placement_invalid"), Message, Kind, Key);
                }
                ConsumedBindings.Add(Key);
                if (bApply)
                {
                    FCreatedObject NewObject;
                    if (!ApplyBinding(Blueprint, *Binding, *Statement, NewObject, Message))
                    {
                        return MutationError(Patch, Target, TEXT("validation.creation_invalid"), Message, Kind, Key);
                    }
                    Created.Add(Binding->Name, NewObject);
                }
                else
                {
                    FCreatedObject PlannedObject;
                    PlannedObject.Kind = Binding->Callee;
                    PlannedObject.PlannedClass = Binding->Callee == TEXT("component")
                        ? ResolveComponentPalette(Binding->Palette)
                        : nullptr;
                    Created.Add(Binding->Name, PlannedObject);
                }
                bChanged = true;
                Comments.Add((bApply ? FString(TEXT("added: ")) : FString(TEXT("would add: "))) + Key);
            }
            else if (Kind == TEXT("set") || Kind == TEXT("reset"))
            {
                const TSharedPtr<FJsonObject>* Member = nullptr;
                if (!(*Statement)->TryGetObjectField(TEXT("target"), Member) || Member == nullptr)
                {
                    return MutationError(Patch, Target, TEXT("validation.invalid_field_target"), TEXT("Decoded field mutation has no resolvable member target."), Kind);
                }
                FResolvedField Field;
                if (!ResolveExistingField(Blueprint, Patch.Alias, *Member, Created, Field, Message))
                {
                    return MutationError(Patch, Target, TEXT("resolution.field_not_found"), Message, Kind, RefKey(*Member));
                }
                const TSharedPtr<FJsonValue>* NewValue = (*Statement)->Values.Find(TEXT("value"));
                bool bFieldChanged = false;
                if (!SetOrResetField(
                    Blueprint,
                    Field,
                    NewValue != nullptr ? *NewValue : nullptr,
                    Kind == TEXT("reset"),
                    bApply,
                    bFieldChanged,
                    Comments,
                    Message))
                {
                    return MutationError(Patch, Target, TEXT("validation.field_value_invalid"), Message, Kind, RefKey(*Member));
                }
                bChanged |= bFieldChanged;
                Comments.Add(
                    (!bFieldChanged
                        ? FString(TEXT("no-op: "))
                        : (bApply ? FString(TEXT("applied: ")) : FString(TEXT("would apply: "))))
                    + Kind + TEXT(" ") + RefKey(*Member));
            }
            else if (Kind == TEXT("remove"))
            {
                const TSharedPtr<FJsonObject>* Ref = nullptr;
                FString RefKind;
                FString Id;
                if (!(*Statement)->TryGetObjectField(TEXT("target"), Ref)
                    || Ref == nullptr
                    || !ValidateStableRef(Blueprint, *Ref, RefKind, Id, Message))
                {
                    return MutationError(Patch, Target, TEXT("validation.remove_invalid"), Message, Kind, Ref != nullptr ? RefKey(*Ref) : FString());
                }
                bool bOperationChanged = false;
                if (!RemoveObject(Blueprint, RefKind, Id, bApply, bOperationChanged, Message))
                {
                    return MutationError(Patch, Target, TEXT("validation.remove_invalid"), Message, Kind, RefKey(*Ref));
                }
                bChanged |= bOperationChanged;
                Comments.Add(
                    (bApply ? FString(TEXT("removed: ")) : FString(TEXT("would remove: ")))
                    + RefKind + TEXT("@") + Id);
            }
            else if (Kind == TEXT("move"))
            {
                bool bOperationChanged = false;
                if (!MoveObject(Blueprint, *Statement, bApply, bOperationChanged, Message))
                {
                    return MutationError(Patch, Target, TEXT("capability.move_unavailable"), Message, Kind, FString(), TEXT("Inspect the exact object's with schema result for its current native move surface."));
                }
                bChanged |= bOperationChanged;
                Comments.Add(!bOperationChanged ? TEXT("no-op: move") : (bApply ? TEXT("applied: move") : TEXT("would apply: move")));
            }
            else if (Kind == TEXT("invoke"))
            {
                bool bOperationChanged = false;
                if (!InvokeOperation(Blueprint, Patch.Alias, *Statement, bApply, Created, bOperationChanged, Comments, Message))
                {
                    return MutationError(Patch, Target, TEXT("capability.operation_unavailable"), Message, Kind, FString(), TEXT("Use with schema on the exact target before invoking an Operation."));
                }
                bChanged |= bOperationChanged;
                Comments.Add(!bOperationChanged ? TEXT("no-op: invoke") : (bApply ? TEXT("applied: invoke") : TEXT("would apply: invoke")));
            }
            else
            {
                return MutationError(
                    Patch,
                    Target,
                    TEXT("capability.operation_unavailable"),
                    FString::Printf(TEXT("Blueprint Patch does not own %s."), *Kind),
                    Kind);
            }
        }
        for (const TPair<FString, FPendingBinding>& Pair : Bindings)
        {
            if (!ConsumedBindings.Contains(Pair.Key))
            {
                return MutationError(Patch, Target, TEXT("validation.unused_binding"), TEXT("Every creation binding must be consumed exactly once by add."), TEXT("add"), Pair.Key);
            }
        }
        return nullptr;
    };

    if (GEditor == nullptr || GEditor->IsTransactionActive())
    {
        return MutationError(
            Patch,
            Target,
            TEXT("capability.transaction_unavailable"),
            TEXT("Blueprint Patch requires an isolated editor transaction for transient preflight and atomic apply."),
            TEXT("patch"));
    }

    FString PlanMessage;
    UBlueprint* OriginalBlueprint = Blueprint;
    UBlueprint* PlanningBlueprint = MakeTransientBlueprintPlan(Blueprint, PlanMessage);
    if (PlanningBlueprint == nullptr)
    {
        return MutationError(
            Patch,
            Target,
            TEXT("capability.preflight_unavailable"),
            PlanMessage,
            TEXT("patch"));
    }
    TMap<FString, FCreatedObject> PreflightCreated;
    TArray<FString> PlannedComments;
    bool bWouldChange = false;
    TSharedPtr<FJsonObject> PreflightError;
    {
        FScopedTransaction PreflightTransaction(NSLOCTEXT("Loomle", "SalBlueprintPreflight", "SAL Blueprint Preflight"));
        Blueprint = PlanningBlueprint;
        PreflightError = Execute(true, PreflightCreated, PlannedComments, bWouldChange);
        Blueprint = OriginalBlueprint;
        PreflightTransaction.Cancel();
    }
    Blueprint = OriginalBlueprint;
    if (PreflightError.IsValid())
    {
        return PreflightError;
    }
    ConvertAppliedCommentsToPlan(PlannedComments);
    if (Patch.bDryRun)
    {
        return MakeMutationResult(
            BlueprintPatchObject(Blueprint, Patch.Alias, PlannedComments),
            {},
            true,
            true,
            false,
            Target.AssetPath,
            TEXT("patch"),
            BuildBlueprintPlan(Patch, PlannedComments));
    }
    if (!bWouldChange)
    {
        return MakeMutationResult(
            BlueprintPatchObject(Blueprint, Patch.Alias, PlannedComments),
            {},
            false,
            true,
            false,
            Target.AssetPath,
            TEXT("patch"));
    }
    UPackage* Package = Blueprint->GetOutermost();
    const bool bWasDirty = Package != nullptr && Package->IsDirty();
    FBlueprintEditorUtils::UpdateTransactionalFlags(Blueprint);
    TMap<FString, FCreatedObject> Created;
    TArray<FString> AppliedComments;
    bool bChanged = false;
    bool bTransactionCaptured = false;
    TSharedPtr<FJsonObject> ApplyError;
    {
        FScopedTransaction Transaction(NSLOCTEXT("Loomle", "SalBlueprintPatch", "SAL Blueprint Patch"));
        bTransactionCaptured = Blueprint->Modify(false);
        if (!bTransactionCaptured)
        {
            ApplyError = MutationError(
                Patch,
                Target,
                TEXT("capability.transaction_unavailable"),
                TEXT("UE could not capture the Blueprint in the SAL editor transaction."),
                TEXT("patch"));
        }
        else
        {
            ApplyError = Execute(true, Created, AppliedComments, bChanged);
            if (!ApplyError.IsValid() && bChanged)
            {
                Blueprint->MarkPackageDirty();
            }
        }
    }
    if (!bTransactionCaptured)
    {
        return ApplyError;
    }
    if (ApplyError.IsValid() || !bChanged)
    {
        const bool bRolledBack = GEditor->UndoTransaction(false);
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(bRolledBack ? bWasDirty : true);
        }
        if (!bRolledBack)
        {
            return MutationError(
                Patch,
                Target,
                TEXT("validation.rollback_failed"),
                TEXT("UE failed to undo the private SAL transaction after an incomplete or no-op Blueprint Patch."),
                TEXT("patch"),
                FString(),
                FString(),
                true);
        }
        if (ApplyError.IsValid())
        {
            return ApplyError;
        }
        return MakeMutationResult(
            BlueprintPatchObject(Blueprint, Patch.Alias, AppliedComments, &Patch, &Created),
            {},
            false,
            true,
            false,
            Target.AssetPath,
            TEXT("patch"));
    }
    return MakeMutationResult(
        BlueprintPatchObject(Blueprint, Patch.Alias, AppliedComments, &Patch, &Created),
        {},
        false,
        true,
        bChanged,
        Target.AssetPath,
        TEXT("patch"));
}
}
