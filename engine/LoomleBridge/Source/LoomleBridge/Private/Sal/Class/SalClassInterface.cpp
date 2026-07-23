// Copyright 2026 Loomle contributors.

#include "SalClassInterface.h"

#include "../SalDiagnostics.h"
#include "../SalObjectBuilder.h"
#include "../SalRuntime.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EditorCategoryUtils.h"
#include "FileHelpers.h"
#include "Kismet/KismetSystemLibrary.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/Crc.h"
#include "Misc/PackageName.h"
#include "ObjectEditorUtils.h"
#include "ScopedTransaction.h"
#include "UObject/Class.h"
#include "UObject/CoreNetTypes.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/PropertyAccessUtil.h"
#include "UObject/Script.h"
#include "UObject/UnrealType.h"

namespace Loomle::Sal
{
namespace
{
constexpr int32 DefaultPageLimit = 50;

struct FNamedFlag64
{
    uint64 Value;
    const TCHAR* Name;
};

struct FOwnedSparseData
{
    ~FOwnedSparseData()
    {
        if (Struct != nullptr && Data != nullptr)
        {
            Struct->DestroyStruct(Data);
            FMemory::Free(Data);
        }
    }

    UScriptStruct* Struct = nullptr;
    void* Data = nullptr;
};

struct FDefaultEntry
{
    FProperty* Property = nullptr;
    const void* Container = nullptr;
    const void* ArchetypeContainer = nullptr;
    bool bSparse = false;
    bool bIntroducedHere = false;
    bool bOverridden = false;
    TSharedPtr<FOwnedSparseData> OwnedContainer;
    TSharedPtr<FOwnedSparseData> OwnedArchetypeContainer;
};

struct FPropertyMatch
{
    FProperty* Property = nullptr;
    int32 Score = 0;
};

struct FFunctionMatch
{
    UFunction* Function = nullptr;
    int32 Score = 0;
};

struct FDefaultMatch
{
    FDefaultEntry Entry;
    int32 Score = 0;
};

struct FPlannedEdit
{
    ~FPlannedEdit()
    {
        if (Property != nullptr && DesiredValue != nullptr)
        {
            Property->DestroyAndFreeValue(DesiredValue);
        }
    }

    int32 Index = INDEX_NONE;
    FString Kind;
    FString Name;
    TArray<FString> RequestedValues;
    TArray<FString> BeforeValues;
    TArray<FString> AfterValues;
    FProperty* Property = nullptr;
    bool bSparse = false;
    bool bChanged = false;
    bool bWasOverridden = false;
    bool bIntroducedHere = false;
    bool bInitialOverridden = false;
    bool bFinalChanged = false;
    void* DesiredValue = nullptr;
};

struct FPropertyBackup
{
    ~FPropertyBackup()
    {
        if (Property != nullptr && Value != nullptr)
        {
            Property->DestroyAndFreeValue(Value);
        }
    }

    FProperty* Property = nullptr;
    void* Value = nullptr;
};

TSharedPtr<FJsonObject> ErrorResult(
    const FString& Code,
    const FString& Message,
    const FString& Operation = FString(),
    const TArray<FString>& Supported = {})
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(Code, Message).Interface(TEXT("class"));
    if (!Operation.IsEmpty())
    {
        Diagnostic.Operation(Operation);
    }
    if (!Supported.IsEmpty())
    {
        Diagnostic.Supported(Supported);
    }
    return FSalDiagnostics::Result(Diagnostic.Build());
}

FString JoinFlags(uint64 Flags, const TArrayView<const FNamedFlag64> Known)
{
    if (Flags == 0)
    {
        return TEXT("0");
    }
    TArray<FString> Names;
    uint64 Remaining = Flags;
    for (const FNamedFlag64& Flag : Known)
    {
        if ((Remaining & Flag.Value) != 0)
        {
            Names.Add(Flag.Name);
            Remaining &= ~Flag.Value;
        }
    }
    if (Remaining != 0)
    {
        Names.Add(FString::Printf(TEXT("0x%016llx"), static_cast<unsigned long long>(Remaining)));
    }
    return FString::Join(Names, TEXT(" | "));
}

FString ClassFlagsText(const EClassFlags Flags)
{
    static const FNamedFlag64 Known[] = {
        {CLASS_Abstract, TEXT("CLASS_Abstract")},
        {CLASS_DefaultConfig, TEXT("CLASS_DefaultConfig")},
        {CLASS_Config, TEXT("CLASS_Config")},
        {CLASS_Transient, TEXT("CLASS_Transient")},
        {CLASS_Optional, TEXT("CLASS_Optional")},
        {CLASS_MatchedSerializers, TEXT("CLASS_MatchedSerializers")},
        {CLASS_ProjectUserConfig, TEXT("CLASS_ProjectUserConfig")},
        {CLASS_Native, TEXT("CLASS_Native")},
        {CLASS_NotPlaceable, TEXT("CLASS_NotPlaceable")},
        {CLASS_PerObjectConfig, TEXT("CLASS_PerObjectConfig")},
        {CLASS_ReplicationDataIsSetUp, TEXT("CLASS_ReplicationDataIsSetUp")},
        {CLASS_EditInlineNew, TEXT("CLASS_EditInlineNew")},
        {CLASS_CollapseCategories, TEXT("CLASS_CollapseCategories")},
        {CLASS_Interface, TEXT("CLASS_Interface")},
        {CLASS_PerPlatformConfig, TEXT("CLASS_PerPlatformConfig")},
        {CLASS_Const, TEXT("CLASS_Const")},
        {CLASS_NeedsDeferredDependencyLoading, TEXT("CLASS_NeedsDeferredDependencyLoading")},
        {CLASS_CompiledFromBlueprint, TEXT("CLASS_CompiledFromBlueprint")},
        {CLASS_MinimalAPI, TEXT("CLASS_MinimalAPI")},
        {CLASS_RequiredAPI, TEXT("CLASS_RequiredAPI")},
        {CLASS_DefaultToInstanced, TEXT("CLASS_DefaultToInstanced")},
        {CLASS_TokenStreamAssembled, TEXT("CLASS_TokenStreamAssembled")},
        {CLASS_HasInstancedReference, TEXT("CLASS_HasInstancedReference")},
        {CLASS_Hidden, TEXT("CLASS_Hidden")},
        {CLASS_Deprecated, TEXT("CLASS_Deprecated")},
        {CLASS_HideDropDown, TEXT("CLASS_HideDropDown")},
        {CLASS_GlobalUserConfig, TEXT("CLASS_GlobalUserConfig")},
        {CLASS_Intrinsic, TEXT("CLASS_Intrinsic")},
        {CLASS_Constructed, TEXT("CLASS_Constructed")},
        {CLASS_ConfigDoNotCheckDefaults, TEXT("CLASS_ConfigDoNotCheckDefaults")},
        {CLASS_NewerVersionExists, TEXT("CLASS_NewerVersionExists")},
    };
    return JoinFlags(static_cast<uint32>(Flags), Known);
}

FString PropertyFlagsText(const EPropertyFlags Flags)
{
    return NativePropertyFlagsText(static_cast<uint64>(Flags));
}

FString FunctionFlagsText(const EFunctionFlags Flags)
{
    static const FNamedFlag64 Known[] = {
        {FUNC_Final, TEXT("FUNC_Final")},
        {FUNC_RequiredAPI, TEXT("FUNC_RequiredAPI")},
        {FUNC_BlueprintAuthorityOnly, TEXT("FUNC_BlueprintAuthorityOnly")},
        {FUNC_BlueprintCosmetic, TEXT("FUNC_BlueprintCosmetic")},
        {FUNC_Net, TEXT("FUNC_Net")},
        {FUNC_NetReliable, TEXT("FUNC_NetReliable")},
        {FUNC_NetRequest, TEXT("FUNC_NetRequest")},
        {FUNC_Exec, TEXT("FUNC_Exec")},
        {FUNC_Native, TEXT("FUNC_Native")},
        {FUNC_Event, TEXT("FUNC_Event")},
        {FUNC_NetResponse, TEXT("FUNC_NetResponse")},
        {FUNC_Static, TEXT("FUNC_Static")},
        {FUNC_NetMulticast, TEXT("FUNC_NetMulticast")},
        {FUNC_UbergraphFunction, TEXT("FUNC_UbergraphFunction")},
        {FUNC_MulticastDelegate, TEXT("FUNC_MulticastDelegate")},
        {FUNC_Public, TEXT("FUNC_Public")},
        {FUNC_Private, TEXT("FUNC_Private")},
        {FUNC_Protected, TEXT("FUNC_Protected")},
        {FUNC_Delegate, TEXT("FUNC_Delegate")},
        {FUNC_NetServer, TEXT("FUNC_NetServer")},
        {FUNC_HasOutParms, TEXT("FUNC_HasOutParms")},
        {FUNC_HasDefaults, TEXT("FUNC_HasDefaults")},
        {FUNC_NetClient, TEXT("FUNC_NetClient")},
        {FUNC_DLLImport, TEXT("FUNC_DLLImport")},
        {FUNC_BlueprintCallable, TEXT("FUNC_BlueprintCallable")},
        {FUNC_BlueprintEvent, TEXT("FUNC_BlueprintEvent")},
        {FUNC_BlueprintPure, TEXT("FUNC_BlueprintPure")},
        {FUNC_EditorOnly, TEXT("FUNC_EditorOnly")},
        {FUNC_Const, TEXT("FUNC_Const")},
        {FUNC_NetValidate, TEXT("FUNC_NetValidate")},
    };
    return JoinFlags(static_cast<uint32>(Flags), Known);
}

FString PropertyTypeText(const FProperty* Property)
{
    return NativePropertyTypeText(Property);
}

const void* DirectPropertyElement(const FProperty* Property, const void* Value, const int32 Index)
{
    return static_cast<const uint8*>(Value) + Property->GetElementSize() * Index;
}

void* DirectPropertyElement(FProperty* Property, void* Value, const int32 Index)
{
    return static_cast<uint8*>(Value) + Property->GetElementSize() * Index;
}

TArray<FString> ExportDirectPropertyValues(const FProperty* Property, const void* Value)
{
    TArray<FString> Values;
    if (Property == nullptr || Value == nullptr)
    {
        return Values;
    }
    Values.Reserve(Property->ArrayDim);
    for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
    {
        FString Text;
        Property->ExportTextItem_Direct(Text, DirectPropertyElement(Property, Value, Index), nullptr, nullptr, PPF_None);
        Values.Add(MoveTemp(Text));
    }
    return Values;
}

TArray<FString> ExportPropertyValues(const FProperty* Property, const void* Container)
{
    return Property != nullptr && Container != nullptr
        ? ExportDirectPropertyValues(Property, Property->ContainerPtrToValuePtr<const void>(Container))
        : TArray<FString>();
}

TSharedPtr<FJsonValue> NativePropertyValues(const TArray<FString>& Values)
{
    if (Values.Num() == 1)
    {
        return NativeValue(Values[0]);
    }
    TArray<TSharedPtr<FJsonValue>> Elements;
    Elements.Reserve(Values.Num());
    for (const FString& Value : Values)
    {
        Elements.Add(NativeValue(Value));
    }
    return MakeShared<FJsonValueArray>(Elements);
}

bool IdenticalCompletePropertyValue(const FProperty* Property, const void* Left, const void* Right)
{
    if (Property == nullptr || Left == nullptr || Right == nullptr)
    {
        return Left == Right;
    }
    for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
    {
        if (!Property->Identical(
                DirectPropertyElement(Property, Left, Index),
                DirectPropertyElement(Property, Right, Index)))
        {
            return false;
        }
    }
    return true;
}

bool IdenticalCompleteInContainers(const FProperty* Property, const void* Left, const void* Right)
{
    return Property != nullptr && Left != nullptr && Right != nullptr
        && IdenticalCompletePropertyValue(
            Property,
            Property->ContainerPtrToValuePtr<const void>(Left),
            Property->ContainerPtrToValuePtr<const void>(Right));
}

bool IsIdentifier(const FString& Text)
{
    auto IsAsciiAlpha = [](const TCHAR Character)
    {
        return (Character >= TEXT('A') && Character <= TEXT('Z'))
            || (Character >= TEXT('a') && Character <= TEXT('z'));
    };
    auto IsAsciiDigit = [](const TCHAR Character)
    {
        return Character >= TEXT('0') && Character <= TEXT('9');
    };
    if (Text.IsEmpty() || !(IsAsciiAlpha(Text[0]) || Text[0] == TEXT('_')))
    {
        return false;
    }
    for (int32 Index = 1; Index < Text.Len(); ++Index)
    {
        if (!(IsAsciiAlpha(Text[Index]) || IsAsciiDigit(Text[Index]) || Text[Index] == TEXT('_')))
        {
            return false;
        }
    }
    return Text != TEXT("true") && Text != TEXT("false") && Text != TEXT("null");
}

TSharedPtr<FJsonObject> SortedMetaData(const TMap<FName, FString>* Map)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    if (Map == nullptr)
    {
        return Result;
    }
    TArray<FName> Keys;
    Map->GetKeys(Keys);
    Keys.Sort(FNameLexicalLess());
    for (const FName Key : Keys)
    {
        const FString Name = Key.ToString();
        if (IsIdentifier(Name))
        {
            Result->SetStringField(Name, Map->FindChecked(Key));
        }
    }
    return Result;
}

TSharedPtr<FJsonObject> MetaDataFor(const FField* Field)
{
#if WITH_METADATA
    return SortedMetaData(Field != nullptr ? Field->GetMetaDataMap() : nullptr);
#else
    return MakeShared<FJsonObject>();
#endif
}

TSharedPtr<FJsonObject> EffectiveClassMetaData(const UClass* Class)
{
#if WITH_METADATA
    TArray<const UClass*> Hierarchy;
    for (const UClass* Cursor = Class; Cursor != nullptr; Cursor = Cursor->GetSuperClass())
    {
        Hierarchy.Add(Cursor);
    }
    TMap<FName, FString> Merged;
    for (int32 Index = Hierarchy.Num() - 1; Index >= 0; --Index)
    {
        if (const TMap<FName, FString>* Map = FMetaData::GetMapForObject(Hierarchy[Index]))
        {
            for (const TPair<FName, FString>& Pair : *Map)
            {
                Merged.Add(Pair.Key, Pair.Value);
            }
        }
    }
    return SortedMetaData(&Merged);
#else
    return MakeShared<FJsonObject>();
#endif
}

TSharedPtr<FJsonObject> EffectiveFunctionMetaData(const UFunction* Function)
{
#if WITH_METADATA
    TArray<const UFunction*> Hierarchy;
    for (const UFunction* Cursor = Function; Cursor != nullptr; Cursor = Cursor->GetSuperFunction())
    {
        Hierarchy.Add(Cursor);
    }
    TMap<FName, FString> Merged;
    for (int32 Index = Hierarchy.Num() - 1; Index >= 0; --Index)
    {
        if (const TMap<FName, FString>* Map = FMetaData::GetMapForObject(Hierarchy[Index]))
        {
            for (const TPair<FName, FString>& Pair : *Map)
            {
                Merged.Add(Pair.Key, Pair.Value);
            }
        }
    }
    return SortedMetaData(&Merged);
#else
    return MakeShared<FJsonObject>();
#endif
}

TSharedPtr<FJsonValue> CompactClassValue(const UClass* Class)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    if (Class != nullptr)
    {
        Args->SetStringField(TEXT("path"), Class->GetPathName());
    }
    return Value::Call(TEXT("class"), Args);
}

TSharedPtr<FJsonValue> CompleteClassValue(const UClass* Class)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("path"), Class->GetPathName());
    Args->SetStringField(TEXT("type"), Class->GetClass()->GetPathName());
    if (Class->GetSuperClass() != nullptr)
    {
        Args->SetStringField(TEXT("SuperClass"), Class->GetSuperClass()->GetPathName());
    }
    if (!Class->ClassConfigName.IsNone())
    {
        Args->SetStringField(TEXT("ClassConfigName"), Class->ClassConfigName.ToString());
    }
    Args->SetStringField(TEXT("ClassFlags"), ClassFlagsText(Class->GetClassFlags()));
    if (Class->ClassGeneratedBy != nullptr)
    {
        Args->SetStringField(TEXT("ClassGeneratedBy"), Class->ClassGeneratedBy->GetPathName());
    }
    const TSharedPtr<FJsonObject> MetaData = EffectiveClassMetaData(Class);
    if (!MetaData->Values.IsEmpty())
    {
        Args->SetObjectField(TEXT("MetaData"), MetaData);
    }
    TArray<const FImplementedInterface*> EffectiveInterfaces;
    TSet<const UClass*> InterfaceClasses;
    for (const UClass* Cursor = Class; Cursor != nullptr; Cursor = Cursor->GetSuperClass())
    {
        for (const FImplementedInterface& Interface : Cursor->Interfaces)
        {
            if (Interface.Class != nullptr && !InterfaceClasses.Contains(Interface.Class))
            {
                InterfaceClasses.Add(Interface.Class);
                EffectiveInterfaces.Add(&Interface);
            }
        }
    }
    if (!EffectiveInterfaces.IsEmpty())
    {
        TArray<TSharedPtr<FJsonValue>> Interfaces;
        for (const FImplementedInterface* Interface : EffectiveInterfaces)
        {
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("Class"), GetPathNameSafe(Interface->Class));
            Entry->SetNumberField(TEXT("PointerOffset"), Interface->PointerOffset);
            Entry->SetBoolField(TEXT("bImplementedByK2"), Interface->bImplementedByK2);
            Interfaces.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Args->SetArrayField(TEXT("Interfaces"), Interfaces);
    }
    return Value::Call(TEXT("class"), Args);
}

TSharedPtr<FJsonValue> PropertyValue(const FProperty* Property, const bool bComplete)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("path"), Property->GetPathName());
    Args->SetStringField(TEXT("type"), PropertyTypeText(Property));
    if (bComplete)
    {
        Args->SetStringField(TEXT("PropertyFlags"), PropertyFlagsText(Property->GetPropertyFlags()));
        if (Property->ArrayDim != 1)
        {
            Args->SetNumberField(TEXT("ArrayDim"), Property->ArrayDim);
        }
        if (!Property->RepNotifyFunc.IsNone())
        {
            Args->SetStringField(TEXT("RepNotifyFunc"), Property->RepNotifyFunc.ToString());
        }
        if (Property->GetBlueprintReplicationCondition() != COND_None)
        {
            if (const UEnum* LifetimeCondition = StaticEnum<ELifetimeCondition>())
            {
                Args->SetStringField(
                    TEXT("BlueprintReplicationCondition"),
                    LifetimeCondition->GetNameStringByValue(Property->GetBlueprintReplicationCondition()));
            }
        }
        const TSharedPtr<FJsonObject> MetaData = MetaDataFor(Property);
        if (!MetaData->Values.IsEmpty())
        {
            Args->SetObjectField(TEXT("MetaData"), MetaData);
        }
    }
    return Value::Call(TEXT("property"), Args);
}

TSharedPtr<FJsonValue> FunctionValue(const UFunction* Function, const bool bComplete)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("path"), Function->GetPathName());
    Args->SetStringField(TEXT("type"), Function->GetClass()->GetPathName());
    if (bComplete)
    {
        Args->SetStringField(TEXT("FunctionFlags"), FunctionFlagsText(Function->FunctionFlags));
        if (Function->RPCId != 0)
        {
            Args->SetNumberField(TEXT("RPCId"), Function->RPCId);
        }
        if (Function->RPCResponseId != 0)
        {
            Args->SetNumberField(TEXT("RPCResponseId"), Function->RPCResponseId);
        }
        const TSharedPtr<FJsonObject> MetaData = EffectiveFunctionMetaData(Function);
        if (!MetaData->Values.IsEmpty())
        {
            Args->SetObjectField(TEXT("MetaData"), MetaData);
        }
    }
    return Value::Call(TEXT("function"), Args);
}

TArray<FProperty*> EffectiveOrdinaryProperties(UClass* Class)
{
    TArray<FProperty*> Result;
    TSet<FName> Names;
    for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        FProperty* Property = *It;
        if (Property != nullptr && !Names.Contains(Property->GetFName()))
        {
            Names.Add(Property->GetFName());
            Result.Add(Property);
        }
    }
    return Result;
}

TArray<FProperty*> EffectiveSparseProperties(UClass* Class)
{
    TArray<FProperty*> Result;
    if (Class == nullptr || Class->GetSparseClassDataStruct() == nullptr)
    {
        return Result;
    }
    TSet<FName> Names;
    for (TFieldIterator<FProperty> It(Class->GetSparseClassDataStruct(), EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        FProperty* Property = *It;
        if (Property != nullptr && !Names.Contains(Property->GetFName()))
        {
            Names.Add(Property->GetFName());
            Result.Add(Property);
        }
    }
    return Result;
}

TArray<FProperty*> EffectiveProperties(UClass* Class)
{
    TArray<FProperty*> Result = EffectiveOrdinaryProperties(Class);
    TSet<FName> Names;
    for (const FProperty* Property : Result)
    {
        Names.Add(Property->GetFName());
    }
    for (FProperty* Property : EffectiveSparseProperties(Class))
    {
        if (!Names.Contains(Property->GetFName()))
        {
            Names.Add(Property->GetFName());
            Result.Add(Property);
        }
    }
    return Result;
}

bool IsSparseProperty(const UClass* Class, const FProperty* Property)
{
    return Class != nullptr
        && Property != nullptr
        && Class->GetSparseClassDataStruct() != nullptr
        && Property->GetOwnerStruct() != nullptr
        && Class->GetSparseClassDataStruct()->IsChildOf(Property->GetOwnerStruct());
}

void AppendEffectiveFunctions(UClass* Class, TArray<UFunction*>& OutFunctions, TSet<FName>& Names)
{
    if (Class == nullptr)
    {
        return;
    }
    for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
    {
        UFunction* Function = *It;
        if (Function != nullptr && !Names.Contains(Function->GetFName()))
        {
            Names.Add(Function->GetFName());
            OutFunctions.Add(Function);
        }
    }
    for (const FImplementedInterface& Interface : Class->Interfaces)
    {
        if (Interface.Class == nullptr)
        {
            continue;
        }
        for (TFieldIterator<UFunction> It(Interface.Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            UFunction* InterfaceFunction = *It;
            if (InterfaceFunction == nullptr || Names.Contains(InterfaceFunction->GetFName()))
            {
                continue;
            }
            UFunction* Effective = Class->FindFunctionByName(InterfaceFunction->GetFName());
            if (Effective != nullptr)
            {
                Names.Add(Effective->GetFName());
                OutFunctions.Add(Effective);
            }
        }
    }
    AppendEffectiveFunctions(Class->GetSuperClass(), OutFunctions, Names);
}

TArray<UFunction*> EffectiveFunctions(UClass* Class)
{
    TArray<UFunction*> Result;
    TSet<FName> Names;
    AppendEffectiveFunctions(Class, Result, Names);
    return Result;
}

bool IsDefaultProperty(const UClass* Class, const FProperty* Property)
{
    if (Property == nullptr
        || !Property->HasAnyPropertyFlags(CPF_Edit | CPF_Config)
        || Property->HasAnyPropertyFlags(CPF_Parm | CPF_Transient | CPF_Deprecated | CPF_DisableEditOnTemplate)
        || Property->HasMetaData(TEXT("InlineEditConditionToggle"))
        || Property->HasMetaData(TEXT("HideInDetailPanel")))
    {
        return false;
    }
    const FName Category = FObjectEditorUtils::GetCategoryFName(Property);
    return Class == nullptr
        || Category.IsNone()
        || !FEditorCategoryUtils::IsCategoryHiddenFromClass(Class, Category.ToString());
}

struct FSparseReadView
{
    UScriptStruct* Struct = nullptr;
    const void* Data = nullptr;
    TSharedPtr<FOwnedSparseData> Owned;
};

FSparseReadView ReadSparseData(UClass* Class)
{
    FSparseReadView View;
    if (Class == nullptr || Class->GetSparseClassDataStruct() == nullptr)
    {
        return View;
    }
    View.Struct = Class->GetSparseClassDataStruct();
    const void* Data = Class->GetSparseClassData(EGetSparseClassDataMethod::ReturnIfNull);
    if (Data != nullptr)
    {
        View.Data = Data;
        return View;
    }

    const FSparseReadView Archetype = ReadSparseData(Class->GetSuperClass());
    if (Archetype.Data != nullptr && View.Struct == Archetype.Struct)
    {
        return Archetype;
    }

    View.Owned = MakeShared<FOwnedSparseData>();
    View.Owned->Struct = View.Struct;
    View.Owned->Data = FMemory::Malloc(View.Struct->GetStructureSize(), View.Struct->GetMinAlignment());
    View.Struct->InitializeStruct(View.Owned->Data);
    if (Archetype.Data != nullptr && Archetype.Struct != nullptr)
    {
        if (View.Struct->IsChildOf(Archetype.Struct))
        {
            Archetype.Struct->CopyScriptStruct(View.Owned->Data, Archetype.Data);
        }
        else if (Archetype.Struct->IsChildOf(View.Struct))
        {
            View.Struct->CopyScriptStruct(View.Owned->Data, Archetype.Data);
        }
    }
    View.Data = View.Owned->Data;
    return View;
}

int32 DefaultDisplayPriority(const FProperty* Property)
{
    const FString Value = Property != nullptr ? Property->GetMetaData(TEXT("DisplayPriority")) : FString();
    if (Value.IsEmpty() || !FCString::IsNumeric(*Value))
    {
        return MAX_int32;
    }
    return FCString::Atoi(*Value);
}

void OrderDefaultCategory(TArray<FDefaultEntry>& Entries)
{
    TMap<FName, TArray<TPair<FDefaultEntry, int32>>> Deferred;
    TArray<TPair<FDefaultEntry, int32>> Ordered;
    auto InsertByPriority = [](TArray<TPair<FDefaultEntry, int32>>& Into, const FDefaultEntry& Entry)
    {
        const int32 Priority = DefaultDisplayPriority(Entry.Property);
        int32 InsertIndex = Into.Num();
        if (Priority != MAX_int32)
        {
            for (int32 Index = 0; Index < Into.Num(); ++Index)
            {
                if (Priority < Into[Index].Value)
                {
                    InsertIndex = Index;
                    break;
                }
            }
        }
        Into.Insert(TPair<FDefaultEntry, int32>(Entry, Priority), InsertIndex);
    };

    for (const FDefaultEntry& Entry : Entries)
    {
        const FString DisplayAfter = Entry.Property->GetMetaData(TEXT("DisplayAfter"));
        if (DisplayAfter.IsEmpty())
        {
            InsertByPriority(Ordered, Entry);
        }
        else
        {
            InsertByPriority(Deferred.FindOrAdd(FName(*DisplayAfter)), Entry);
        }
    }
    int32 PreviousDeferredCount = INDEX_NONE;
    while (!Deferred.IsEmpty() && PreviousDeferredCount != Deferred.Num())
    {
        PreviousDeferredCount = Deferred.Num();
        for (int32 Index = 0; Index < Ordered.Num(); ++Index)
        {
            if (TArray<TPair<FDefaultEntry, int32>>* After = Deferred.Find(Ordered[Index].Key.Property->GetFName()))
            {
                Ordered.Insert(*After, Index + 1);
                Deferred.Remove(Ordered[Index].Key.Property->GetFName());
            }
        }
    }
    TArray<TPair<FDefaultEntry, int32>> Unresolved;
    for (TPair<FName, TArray<TPair<FDefaultEntry, int32>>>& Pair : Deferred)
    {
        Unresolved.Append(Pair.Value);
    }
    Unresolved.Sort([](const TPair<FDefaultEntry, int32>& Left, const TPair<FDefaultEntry, int32>& Right)
    {
        return Left.Key.Property->GetPathName() < Right.Key.Property->GetPathName();
    });
    Ordered.Append(Unresolved);
    Entries.Reset(Ordered.Num());
    for (const TPair<FDefaultEntry, int32>& Pair : Ordered)
    {
        Entries.Add(Pair.Key);
    }
}

void OrderDefaultsForDisplay(UClass* Class, TArray<FDefaultEntry>& Entries)
{
    TArray<FName> Categories;
    TMap<FName, TArray<FDefaultEntry>> ByCategory;
    for (const FDefaultEntry& Entry : Entries)
    {
        const FName Category = FObjectEditorUtils::GetCategoryFName(Entry.Property);
        if (!ByCategory.Contains(Category))
        {
            Categories.Add(Category);
        }
        ByCategory.FindOrAdd(Category).Add(Entry);
    }

    TMap<FString, int32> ExplicitOrder;
#if WITH_EDITORONLY_DATA
    if (const UBlueprint* Blueprint = Class != nullptr ? Cast<UBlueprint>(Class->ClassGeneratedBy) : nullptr)
    {
        for (const FName Category : Blueprint->CategorySorting)
        {
            ExplicitOrder.FindOrAdd(Category.ToString(), ExplicitOrder.Num());
        }
    }
#endif
    if (Class != nullptr)
    {
        TArray<FString> Prioritized;
        Class->GetPrioritizeCategories(Prioritized);
        for (const FString& Category : Prioritized)
        {
            ExplicitOrder.FindOrAdd(FEditorCategoryUtils::GetCategoryDisplayString(Category), ExplicitOrder.Num());
        }
    }
    Categories.StableSort([&ExplicitOrder](const FName Left, const FName Right)
    {
        const int32* LeftOrder = ExplicitOrder.Find(FEditorCategoryUtils::GetCategoryDisplayString(Left.ToString()));
        const int32* RightOrder = ExplicitOrder.Find(FEditorCategoryUtils::GetCategoryDisplayString(Right.ToString()));
        if (LeftOrder != nullptr && RightOrder != nullptr)
        {
            return *LeftOrder < *RightOrder;
        }
        return LeftOrder != nullptr;
    });

    Entries.Reset();
    for (const FName Category : Categories)
    {
        TArray<FDefaultEntry>& CategoryEntries = ByCategory.FindChecked(Category);
        OrderDefaultCategory(CategoryEntries);
        Entries.Append(CategoryEntries);
    }
}

TArray<FDefaultEntry> EffectiveDefaults(UClass* Class)
{
    TArray<FDefaultEntry> Result;
    if (Class == nullptr)
    {
        return Result;
    }
    UObject* CDO = Class->GetDefaultObject();
    UObject* SuperCDO = Class->GetSuperClass() != nullptr ? Class->GetSuperClass()->GetDefaultObject() : nullptr;
    TSet<FName> Names;
    for (FProperty* Property : EffectiveOrdinaryProperties(Class))
    {
        if (!IsDefaultProperty(Class, Property) || Names.Contains(Property->GetFName()))
        {
            continue;
        }
        Names.Add(Property->GetFName());
        FDefaultEntry Entry;
        Entry.Property = Property;
        Entry.Container = CDO;
        Entry.ArchetypeContainer = SuperCDO;
        Entry.bIntroducedHere = Property->GetOwnerClass() == Class;
        Entry.bOverridden = Entry.bIntroducedHere
            || SuperCDO == nullptr
            || !IdenticalCompleteInContainers(Property, CDO, SuperCDO);
        Result.Add(Entry);
    }

    UScriptStruct* SparseStruct = Class->GetSparseClassDataStruct();
    const FSparseReadView SparseView = ReadSparseData(Class);
    const void* SparseData = SparseView.Data;
    UClass* SuperClass = Class->GetSuperClass();
    const FSparseReadView SuperSparseView = ReadSparseData(SuperClass);
    const void* SuperSparseData = SuperSparseView.Data;
    if (SparseStruct != nullptr && SparseData != nullptr)
    {
        for (TFieldIterator<FProperty> It(SparseStruct, EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            FProperty* Property = *It;
            if (!IsDefaultProperty(Class, Property) || Names.Contains(Property->GetFName()))
            {
                continue;
            }
            Names.Add(Property->GetFName());
            FDefaultEntry Entry;
            Entry.Property = Property;
            Entry.Container = SparseData;
            Entry.ArchetypeContainer = SuperSparseData;
            Entry.OwnedContainer = SparseView.Owned;
            Entry.OwnedArchetypeContainer = SuperSparseView.Owned;
            Entry.bSparse = true;
            Entry.bIntroducedHere = Property->GetOwnerStruct() == SparseStruct
                && (SuperClass == nullptr || SparseStruct != SuperClass->GetSparseClassDataStruct());
            Entry.bOverridden = Entry.bIntroducedHere
                || SuperSparseData == nullptr
                || !IdenticalCompleteInContainers(Property, SparseData, SuperSparseData);
            Result.Add(Entry);
        }
    }
    OrderDefaultsForDisplay(Class, Result);
    return Result;
}

FProperty* FindExactProperty(UClass* Class, const FString& Name, bool* bOutAmbiguous = nullptr)
{
    if (bOutAmbiguous != nullptr) *bOutAmbiguous = false;
    const TArray<FProperty*> Properties = EffectiveProperties(Class);
    const FName Selector(*Name);
    for (FProperty* Property : Properties)
    {
        if (Property->GetFName() == Selector)
        {
            return Property;
        }
    }
    FProperty* Match = nullptr;
    for (FProperty* Property : Properties)
    {
        if (Property->GetAuthoredName() == Name)
        {
            if (Match != nullptr)
            {
                if (bOutAmbiguous != nullptr) *bOutAmbiguous = true;
                return nullptr;
            }
            Match = Property;
        }
    }
    return Match;
}

UFunction* FindExactFunction(UClass* Class, const FString& Name, bool* bOutAmbiguous = nullptr)
{
    if (bOutAmbiguous != nullptr) *bOutAmbiguous = false;
    if (UFunction* NativeMatch = Class->FindFunctionByName(FName(*Name)))
    {
        return NativeMatch;
    }
    UFunction* Match = nullptr;
    for (UFunction* Function : EffectiveFunctions(Class))
    {
        if (Function->GetAuthoredName() == Name)
        {
            if (Match != nullptr)
            {
                if (bOutAmbiguous != nullptr) *bOutAmbiguous = true;
                return nullptr;
            }
            Match = Function;
        }
    }
    return Match;
}

bool FindExactDefault(UClass* Class, const FString& Name, FDefaultEntry& OutEntry, bool* bOutAmbiguous = nullptr)
{
    if (bOutAmbiguous != nullptr) *bOutAmbiguous = false;
    const TArray<FDefaultEntry> Defaults = EffectiveDefaults(Class);
    const FName Selector(*Name);
    for (const FDefaultEntry& Entry : Defaults)
    {
        if (Entry.Property->GetFName() == Selector)
        {
            OutEntry = Entry;
            return true;
        }
    }
    const FDefaultEntry* Match = nullptr;
    for (const FDefaultEntry& Entry : Defaults)
    {
        if (Entry.Property->GetAuthoredName() == Name)
        {
            if (Match != nullptr)
            {
                if (bOutAmbiguous != nullptr) *bOutAmbiguous = true;
                return false;
            }
            Match = &Entry;
        }
    }
    if (Match != nullptr)
    {
        OutEntry = *Match;
        return true;
    }
    return false;
}

int32 TextScore(const FString& NativeName, const FString& AuthoredName, const FString& Path, const FString& Text)
{
    if (Text.IsEmpty())
    {
        return 0;
    }
    if (NativeName.Equals(Text, ESearchCase::IgnoreCase) || AuthoredName.Equals(Text, ESearchCase::IgnoreCase))
    {
        return 100;
    }
    if (NativeName.StartsWith(Text, ESearchCase::IgnoreCase) || AuthoredName.StartsWith(Text, ESearchCase::IgnoreCase))
    {
        return 90;
    }
    if (NativeName.Contains(Text, ESearchCase::IgnoreCase) || AuthoredName.Contains(Text, ESearchCase::IgnoreCase))
    {
        return 80;
    }
    if (Path.Contains(Text, ESearchCase::IgnoreCase))
    {
        return 60;
    }
    return INDEX_NONE;
}

int32 PropertyScore(const FProperty* Property, const FString& Text)
{
    int32 Score = TextScore(Property->GetName(), Property->GetAuthoredName(), Property->GetPathName(), Text);
    if (Score != INDEX_NONE || Text.IsEmpty())
    {
        return Score;
    }
    static const FName Keys[] = {TEXT("DisplayName"), TEXT("Category"), TEXT("ToolTip")};
    for (const FName Key : Keys)
    {
        if (Property->GetMetaData(Key).Contains(Text, ESearchCase::IgnoreCase))
        {
            return 50;
        }
    }
    return INDEX_NONE;
}

int32 FunctionScore(const UFunction* Function, const FString& Text)
{
    int32 Score = TextScore(Function->GetName(), Function->GetAuthoredName(), Function->GetPathName(), Text);
    if (Score != INDEX_NONE || Text.IsEmpty())
    {
        return Score;
    }
    static const FName Keys[] = {TEXT("DisplayName"), TEXT("Category"), TEXT("ToolTip"), TEXT("Keywords")};
    for (const FName Key : Keys)
    {
        if (Function->GetMetaData(Key).Contains(Text, ESearchCase::IgnoreCase))
        {
            return 50;
        }
    }
    return INDEX_NONE;
}

bool ReadTrueOverrideCondition(const TSharedPtr<FJsonObject>& Where)
{
    if (!Where.IsValid())
    {
        return false;
    }
    FString Kind;
    bool bValue = false;
    return Where->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("eq")
        && ConditionField(Where) == TEXT("overridden")
        && Where->TryGetBoolField(TEXT("value"), bValue)
        && bValue;
}

bool ValidateQuery(const FSalQuery& Query, const FSalResolvedTarget& Target, FString& OutOperation, FString& OutError)
{
    static const TArray<FString> Supported = {
        TEXT("summary"), TEXT("properties"), TEXT("property"), TEXT("functions"), TEXT("function"), TEXT("defaults"), TEXT("default")};
    if (Target.Kind != ESalTargetKind::Class || Target.Class == nullptr)
    {
        OutError = TEXT("Class Query requires one exact class(path: ...) target.");
        return false;
    }
    if (!Query.Operation.IsValid() || !Query.Operation->TryGetStringField(TEXT("kind"), OutOperation) || !Supported.Contains(OutOperation))
    {
        OutError = TEXT("Unsupported Class Query operation.");
        return false;
    }
    const bool bSingular = OutOperation == TEXT("property") || OutOperation == TEXT("function") || OutOperation == TEXT("default");
    const bool bCollection = OutOperation == TEXT("properties") || OutOperation == TEXT("functions") || OutOperation == TEXT("defaults");
    for (const FString& Detail : Query.With)
    {
        if (!bSingular || Detail != TEXT("schema"))
        {
            OutError = FString::Printf(TEXT("Class operation %s does not support with %s."), *OutOperation, *Detail);
            return false;
        }
    }
    if (!Query.OrderBy.IsEmpty())
    {
        OutError = TEXT("Class defines no order by clause.");
        return false;
    }
    if (!bCollection && (Query.PageLimit > 0 || !Query.PageAfter.IsEmpty()))
    {
        OutError = TEXT("Only plural Class operations support pagination.");
        return false;
    }
    if (Query.Where.IsValid() && !(OutOperation == TEXT("defaults") && ReadTrueOverrideCondition(Query.Where)))
    {
        OutError = TEXT("Class supports only where overridden = true on defaults.");
        return false;
    }
    return true;
}

FString ClassQuerySignature(const FSalQuery& Query, const UClass* Class)
{
    FString Operation;
    FString Text;
    Query.Operation->TryGetStringField(TEXT("kind"), Operation);
    Query.Operation->TryGetStringField(TEXT("text"), Text);
    const FString Signature = FString::Printf(
        TEXT("%s|%s|%s|%s"),
        Class != nullptr ? *Class->GetPathName() : TEXT(""),
        *Operation,
        *Text,
        ReadTrueOverrideCondition(Query.Where) ? TEXT("overridden") : TEXT("all"));
    return FString::Printf(TEXT("%08x"), FCrc::StrCrc32(*Signature));
}

bool DecodeClassPage(const FSalQuery& Query, const UClass* Class, FSalPage& OutPage)
{
    OutPage.Limit = FMath::Clamp(Query.PageLimit > 0 ? Query.PageLimit : DefaultPageLimit, 1, 200);
    OutPage.Offset = 0;
    if (Query.PageAfter.IsEmpty())
    {
        return true;
    }
    TArray<FString> Parts;
    Query.PageAfter.ParseIntoArray(Parts, TEXT(":"), false);
    if (Parts.Num() != 3
        || Parts[0] != TEXT("class")
        || Parts[1] != ClassQuerySignature(Query, Class)
        || !ParseNonNegativeInt32(Parts[2], OutPage.Offset))
    {
        return false;
    }
    return true;
}

FString EncodeClassCursor(const FSalQuery& Query, const UClass* Class, const int32 Offset)
{
    return FString::Printf(TEXT("class:%s:%d"), *ClassQuerySignature(Query, Class), Offset);
}

FString CommentScalar(FString Value)
{
    Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
    return TEXT("\"") + Value + TEXT("\"");
}

FString NativeValuesComment(const TArray<FString>& Values)
{
    if (Values.Num() == 1)
    {
        return CommentScalar(Values[0]);
    }
    TArray<FString> Elements;
    Elements.Reserve(Values.Num());
    for (const FString& Value : Values)
    {
        Elements.Add(CommentScalar(Value));
    }
    return TEXT("[") + FString::Join(Elements, TEXT(", ")) + TEXT("]");
}

FString PropertySource(const FProperty* Property)
{
    if (Property == nullptr)
    {
        return TEXT("unknown Reflection source");
    }
    if (const UClass* OwnerClass = Property->GetOwnerClass())
    {
        if (const UBlueprint* Blueprint = Cast<UBlueprint>(OwnerClass->ClassGeneratedBy))
        {
#if WITH_EDITORONLY_DATA
            const int32 VariableIndex = FBlueprintEditorUtils::FindNewVariableIndex(
                const_cast<UBlueprint*>(Blueprint),
                Property->GetFName());
            if (Blueprint->NewVariables.IsValidIndex(VariableIndex))
            {
                const FBPVariableDescription& Variable = Blueprint->NewVariables[VariableIndex];
                return FString::Printf(
                    TEXT("%s variable %s (VarGuid %s)"),
                    *Blueprint->GetPathName(),
                    *Variable.VarName.ToString(),
                    *Variable.VarGuid.ToString(EGuidFormats::DigitsWithHyphens));
            }
#endif
            return Blueprint->GetPathName();
        }
    }
    const FString ModuleRelativePath = Property->GetMetaData(TEXT("ModuleRelativePath"));
    if (!ModuleRelativePath.IsEmpty())
    {
        return TEXT("native C++ ") + ModuleRelativePath;
    }
    return Property->GetOwnerStruct() != nullptr
        ? Property->GetOwnerStruct()->GetPathName()
        : TEXT("unknown Reflection source");
}

FString FunctionSource(const UFunction* Function)
{
    if (Function != nullptr)
    {
        if (const UClass* OwnerClass = Function->GetOuterUClass())
        {
            if (const UBlueprint* Blueprint = Cast<UBlueprint>(OwnerClass->ClassGeneratedBy))
            {
                return Blueprint->GetPathName();
            }
        }
        const FString ModuleRelativePath = Function->GetMetaData(TEXT("ModuleRelativePath"));
        if (!ModuleRelativePath.IsEmpty())
        {
            return TEXT("native C++ ") + ModuleRelativePath;
        }
    }
    return Function != nullptr && Function->GetOuterUClass() != nullptr
        ? Function->GetOuterUClass()->GetPathName()
        : TEXT("unknown Reflection source");
}

void AppendFieldMetadataSchema(FString& Text, const FField* Field, const FString& Source)
{
#if WITH_METADATA
    const TMap<FName, FString>* Map = Field != nullptr ? Field->GetMetaDataMap() : nullptr;
    if (Map == nullptr || Map->IsEmpty())
    {
        return;
    }
    TArray<FName> Keys;
    Map->GetKeys(Keys);
    Keys.Sort(FNameLexicalLess());
    Text += TEXT("\n  metadata:");
    for (const FName Key : Keys)
    {
        Text += FString::Printf(
            TEXT("\n    %s:\n      value: %s\n      source: %s\n      writable: false"),
            *Key.ToString(),
            *CommentScalar(Map->FindChecked(Key)),
            *Source);
    }
#endif
}

void AppendFunctionMetadataSchema(FString& Text, const UFunction* Function)
{
#if WITH_METADATA
    struct FMetadataSource
    {
        FString Value;
        FString Source;
    };
    TArray<const UFunction*> Hierarchy;
    for (const UFunction* Cursor = Function; Cursor != nullptr; Cursor = Cursor->GetSuperFunction())
    {
        Hierarchy.Add(Cursor);
    }
    TMap<FName, FMetadataSource> Effective;
    for (int32 Index = Hierarchy.Num() - 1; Index >= 0; --Index)
    {
        if (const TMap<FName, FString>* Map = FMetaData::GetMapForObject(Hierarchy[Index]))
        {
            for (const TPair<FName, FString>& Pair : *Map)
            {
                FMetadataSource Entry;
                Entry.Value = Pair.Value;
                Entry.Source = FunctionSource(Hierarchy[Index]);
                Effective.Add(Pair.Key, MoveTemp(Entry));
            }
        }
    }
    if (Effective.IsEmpty())
    {
        return;
    }
    TArray<FName> Keys;
    Effective.GetKeys(Keys);
    Keys.Sort(FNameLexicalLess());
    Text += TEXT("\n  metadata:");
    for (const FName Key : Keys)
    {
        const FMetadataSource& Entry = Effective.FindChecked(Key);
        Text += FString::Printf(
            TEXT("\n    %s:\n      value: %s\n      source: %s\n      writable: false"),
            *Key.ToString(),
            *CommentScalar(Entry.Value),
            *Entry.Source);
    }
#endif
}

void AddPropertySchema(
    FSalObjectBuilder& Builder,
    const UClass* Class,
    const FProperty* Property,
    const bool bDefault,
    const bool bWritable,
    const bool bSparse)
{
    const bool bMemberRepresentable = !bDefault || FSalObjectBuilder::IsIdentifier(Property->GetName());
    const bool bEffectiveWritable = bWritable && bMemberRepresentable;
    const bool bResettable = bEffectiveWritable && !Property->HasMetaData(TEXT("NoResetToDefault"));
    const FString Source = PropertySource(Property);
    FString Text = FString::Printf(
        TEXT("schema:\n  subject: %s\n  type: %s\n  source: %s\n  writable: %s\n  resettable: %s"),
        bDefault ? TEXT("default") : TEXT("property"),
        *PropertyTypeText(Property),
        *Source,
        bEffectiveWritable ? TEXT("true") : TEXT("false"),
        bResettable ? TEXT("true") : TEXT("false"));
    if (!bMemberRepresentable)
    {
        Text += FString::Printf(
            TEXT("\n  native name: %s\n  reason: native name is not a SAL identifier; Class Default member path and Patch are unavailable"),
            *CommentScalar(Property->GetName()));
    }
    if (Property->ArrayDim > 1)
    {
        Text += FString::Printf(
            TEXT("\n  value shape: fixed array of %d native UE strings"),
            Property->ArrayDim);
    }
    if (bSparse)
    {
        Text += FString::Printf(TEXT("\n  storage: sparse class data\n  struct: %s"), *GetPathNameSafe(Property->GetOwnerStruct()));
    }
    if (Property->HasAnyPropertyFlags(CPF_Config))
    {
        Text += FString::Printf(
            TEXT("\n  storage: config\n  config: %s\n  section: %s\n  key: %s"),
            Class != nullptr ? *Class->ClassConfigName.ToString() : TEXT(""),
            Class != nullptr ? *Class->GetPathName() : TEXT(""),
            *Property->GetName());
    }
    AppendFieldMetadataSchema(Text, Property, Source);
    Builder.AddComment(Text);
}

void AddFunctionSchema(FSalObjectBuilder& Builder, const UFunction* Function)
{
    const FString Source = FunctionSource(Function);
    FString Text = FString::Printf(
        TEXT("schema:\n  subject: function\n  path: %s\n  writable: false\n  parameters: native CPF_Parm properties in declaration order"),
        *Function->GetPathName());
    Text += TEXT("\n  source: ") + Source;
    AppendFunctionMetadataSchema(Text, Function);
    Builder.AddComment(Text);
}

void GatherBlueprintGraphs(const UBlueprint* Blueprint, TArray<const UEdGraph*>& OutGraphs)
{
    if (Blueprint == nullptr)
    {
        return;
    }
    for (const UEdGraph* Graph : Blueprint->UbergraphPages) OutGraphs.Add(Graph);
    for (const UEdGraph* Graph : Blueprint->FunctionGraphs) OutGraphs.Add(Graph);
    for (const UEdGraph* Graph : Blueprint->MacroGraphs) OutGraphs.Add(Graph);
    for (const UEdGraph* Graph : Blueprint->DelegateSignatureGraphs) OutGraphs.Add(Graph);
    for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
    {
        for (const UEdGraph* Graph : Interface.Graphs) OutGraphs.Add(Graph);
    }
    OutGraphs.Remove(nullptr);
}

bool FunctionSourceNameMatches(const UFunction* Function, const FName Candidate)
{
    return Function != nullptr
        && !Candidate.IsNone()
        && (Candidate == Function->GetFName()
            || Candidate.ToString() == Function->GetAuthoredName());
}

void AddFunctionNavigation(FSalObjectBuilder& Builder, const UFunction* Function)
{
    if (Function == nullptr)
    {
        return;
    }
    if (const UFunction* SuperFunction = Function->GetSuperFunction())
    {
        Builder.AddComment(TEXT("override: ") + SuperFunction->GetPathName());
    }

    const UClass* OwnerClass = Function->GetOuterUClass();
    const UBlueprint* Blueprint = OwnerClass != nullptr ? Cast<UBlueprint>(OwnerClass->ClassGeneratedBy) : nullptr;
    if (Blueprint == nullptr)
    {
        Builder.AddComment(TEXT("source: ") + FunctionSource(Function));
        return;
    }
    Builder.AddComment(TEXT("source: ") + Blueprint->GetPathName());

    const UEdGraph* SourceGraph = nullptr;
    const UEdGraphNode* SourceNode = nullptr;
    TArray<const UEdGraph*> Graphs;
    GatherBlueprintGraphs(Blueprint, Graphs);
    for (const UEdGraph* Graph : Graphs)
    {
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (const UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
            {
                if (FunctionSourceNameMatches(Function, Entry->CustomGeneratedFunctionName)
                    || FunctionSourceNameMatches(Function, Entry->FunctionReference.GetMemberName())
                    || FunctionSourceNameMatches(Function, Graph->GetFName()))
                {
                    SourceGraph = Graph;
                    SourceNode = Entry;
                    break;
                }
            }
            if (const UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node))
            {
                if (FunctionSourceNameMatches(Function, Event->CustomFunctionName))
                {
                    SourceGraph = Graph;
                    SourceNode = Event;
                    break;
                }
            }
        }
        if (SourceGraph != nullptr)
        {
            break;
        }
    }
    if (SourceGraph != nullptr)
    {
        Builder.AddComment(FString::Printf(
            TEXT("source graph: graph@%s, path: %s"),
            *SourceGraph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphens),
            *SourceGraph->GetPathName()));
    }
    if (SourceNode != nullptr && SourceNode->NodeGuid.IsValid())
    {
        Builder.AddComment(TEXT("source node: node@") + SourceNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    }
}

bool EvaluateKnownEditCondition(
    UClass* Class,
    const FProperty* Property,
    const TMap<FString, TSharedPtr<FPlannedEdit>>* ProvisionalEdits,
    FString& OutReason)
{
    FString Expression = Property != nullptr ? Property->GetMetaData(TEXT("EditCondition")) : FString();
    Expression.TrimStartAndEndInline();
    if (Expression.IsEmpty())
    {
        return true;
    }

    bool bNegated = false;
    bool bExpected = true;
    if (Expression.StartsWith(TEXT("!")))
    {
        bNegated = true;
        Expression.RightChopInline(1);
        Expression.TrimStartAndEndInline();
    }
    const struct
    {
        const TCHAR* Suffix;
        bool Value;
    } Comparisons[] = {
        {TEXT("==true"), true},
        {TEXT("==false"), false},
        {TEXT("!=true"), false},
        {TEXT("!=false"), true},
    };
    FString Compact = Expression.Replace(TEXT(" "), TEXT(""));
    for (const auto& Comparison : Comparisons)
    {
        if (Compact.EndsWith(Comparison.Suffix, ESearchCase::IgnoreCase))
        {
            bExpected = Comparison.Value;
            Compact.LeftChopInline(FCString::Strlen(Comparison.Suffix));
            break;
        }
    }
    if (bNegated)
    {
        bExpected = !bExpected;
    }
    if (!IsIdentifier(Compact))
    {
        OutReason = TEXT("The Default uses a complex EditCondition that this Class adapter cannot safely evaluate.");
        return false;
    }

    FDefaultEntry ConditionEntry;
    if (!FindExactDefault(Class, Compact, ConditionEntry) || ConditionEntry.Container == nullptr)
    {
        OutReason = FString::Printf(TEXT("The EditCondition Property could not be resolved: %s."), *Compact);
        return false;
    }
    const FBoolProperty* BoolProperty = CastField<FBoolProperty>(ConditionEntry.Property);
    if (BoolProperty == nullptr)
    {
        OutReason = TEXT("Only native boolean EditCondition expressions can be safely evaluated by this Class adapter.");
        return false;
    }
    const TSharedPtr<FPlannedEdit> Provisional = ProvisionalEdits != nullptr
        ? ProvisionalEdits->FindRef(ConditionEntry.Property->GetPathName())
        : nullptr;
    const bool bActual = Provisional.IsValid()
        ? BoolProperty->GetPropertyValue(Provisional->DesiredValue)
        : BoolProperty->GetPropertyValue_InContainer(ConditionEntry.Container);
    if (bActual != bExpected)
    {
        OutReason = FString::Printf(TEXT("UE EditCondition is currently false: %s."), *Property->GetMetaData(TEXT("EditCondition")));
        return false;
    }
    return true;
}

bool CanWriteDefault(
    UClass* Class,
    const FDefaultEntry& Entry,
    FString& OutReason,
    const TMap<FString, TSharedPtr<FPlannedEdit>>* ProvisionalEdits = nullptr)
{
    OutReason.Reset();
    UObject* CDO = Class != nullptr ? Class->GetDefaultObject() : nullptr;
    UBlueprint* Blueprint = Class != nullptr ? Cast<UBlueprint>(Class->ClassGeneratedBy) : nullptr;
    UPackage* SourcePackage = Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
    if (Class == nullptr
        || CDO == nullptr
        || Blueprint == nullptr
        || SourcePackage == nullptr
        || Blueprint->HasAnyFlags(RF_Transient)
        || SourcePackage == GetTransientPackage()
        || FPackageName::IsTempPackage(SourcePackage->GetName()))
    {
        OutReason = TEXT("Only Blueprint Generated Classes with a durable source may edit Defaults.");
        return false;
    }
    if (Entry.Property == nullptr || !Entry.Property->HasAnyPropertyFlags(CPF_Edit))
    {
        OutReason = TEXT("The Property is not an editable Class Default.");
        return false;
    }
    if (Entry.Property->HasAnyPropertyFlags(CPF_Config | CPF_EditConst | CPF_Transient | CPF_Deprecated | CPF_DisableEditOnTemplate))
    {
        OutReason = TEXT("Config, read-only, transient, deprecated, and template-disabled Defaults are read-only.");
        return false;
    }
    if (Entry.Property->ContainsInstancedObjectProperty())
    {
        OutReason = TEXT("Component Templates, default subobjects, and other instanced object Defaults are outside the Class Patch surface.");
        return false;
    }
    if (!CDO->CanEditChange(Entry.Property))
    {
        OutReason = TEXT("UE CanEditChange rejected this Default in the current Class state.");
        return false;
    }
    if (!EvaluateKnownEditCondition(Class, Entry.Property, ProvisionalEdits, OutReason))
    {
        return false;
    }
    if (PropertyAccessUtil::CanSetPropertyValue(
            Entry.Property,
            PropertyAccessUtil::EditorReadOnlyFlags,
            true) != EPropertyAccessResultFlags::Success)
    {
        OutReason = TEXT("UE Property Access rejected this Default in the current Class state.");
        return false;
    }
    return true;
}

FString DefaultSource(const UClass* Class, const FDefaultEntry& Entry)
{
    if (Entry.Property->HasAnyPropertyFlags(CPF_Config))
    {
        return TEXT("effective config");
    }
    if (Entry.bOverridden)
    {
        return TEXT("local override");
    }
    return Class->GetSuperClass() != nullptr
        ? FString::Printf(TEXT("inherited from %s"), *Class->GetSuperClass()->GetPathName())
        : TEXT("initialized default");
}

void AddDefaultGroup(
    FSalObjectBuilder& Builder,
    const FString& ClassAlias,
    UClass* Class,
    const FDefaultEntry& Entry,
    const bool bPropertyBinding,
    const bool bSchema,
    const FString& Action = FString())
{
    if (bPropertyBinding)
    {
        const FString PropertyAlias = Builder.UniqueAlias(Entry.Property->GetAuthoredName());
        Builder.AddLocalBinding(PropertyAlias, PropertyValue(Entry.Property, false));
    }
    const FString NativeName = Entry.Property->GetName();
    const TSharedPtr<FJsonValue> DefaultValue = NativePropertyValues(
        ExportPropertyValues(Entry.Property, Entry.Container));
    if (FSalObjectBuilder::IsIdentifier(NativeName))
    {
        Builder.AddMemberBinding(ClassAlias, {NativeName}, DefaultValue);
    }
    else
    {
        Builder.AddLocalBinding(
            Builder.UniqueAlias(Entry.Property->GetAuthoredName() + TEXT("Default")),
            DefaultValue);
        Builder.AddComment(FString::Printf(
            TEXT("owner: %s\nClass Default member path: unavailable in SAL identifier syntax\nnative name: %s\nPatch: unavailable"),
            *ClassAlias,
            *CommentScalar(NativeName)));
    }
    if (!Action.IsEmpty())
    {
        Builder.AddComment(TEXT("applied: ") + Action);
    }
    Builder.AddComment(TEXT("value: ") + DefaultSource(Class, Entry));
    if (Entry.bSparse)
    {
        Builder.AddComment(TEXT("storage: sparse class data"));
        Builder.AddComment(TEXT("struct: ") + GetPathNameSafe(Entry.Property->GetOwnerStruct()));
    }
    if (Entry.Property->HasAnyPropertyFlags(CPF_Config))
    {
        Builder.AddComment(FString::Printf(
            TEXT("config: %s, section: %s, key: %s"),
            *Class->ClassConfigName.ToString(),
            *Class->GetPathName(),
            *Entry.Property->GetName()));
    }
    else if (Entry.bOverridden && Class->ClassGeneratedBy != nullptr)
    {
        Builder.AddComment(TEXT("source: ") + Class->ClassGeneratedBy->GetPathName());
    }
    else if (!Entry.bOverridden && Class->GetSuperClass() != nullptr)
    {
        const UClass* SuperClass = Class->GetSuperClass();
        Builder.AddComment(TEXT("source: ") + (SuperClass->ClassGeneratedBy != nullptr
            ? SuperClass->ClassGeneratedBy->GetPathName()
            : SuperClass->GetPathName()));
    }
    if (bSchema)
    {
        FString Reason;
        AddPropertySchema(Builder, Class, Entry.Property, true, CanWriteDefault(Class, Entry, Reason), Entry.bSparse);
        if (!Reason.IsEmpty())
        {
            Builder.AddComment(TEXT("read-only reason: ") + Reason);
        }
    }
}

TSharedPtr<FJsonObject> ClassCurrentObject(const FSalResolvedTarget& Target)
{
    FSalObjectBuilder Builder;
    if (Target.Class != nullptr)
    {
        const FString Alias = Builder.UniqueAlias(Target.Class->GetName());
        Builder.AddLocalBinding(Alias, CompactClassValue(Target.Class));
    }
    return Builder.BuildObject();
}

TSharedPtr<FJsonObject> SavePlan(const FSalResolvedTarget& Target, const bool bDirty)
{
    TSharedPtr<FJsonObject> Plan = MakeShared<FJsonObject>();
    Plan->SetStringField(TEXT("operation"), TEXT("save"));
    Plan->SetStringField(TEXT("assetPath"), Target.AssetPath);
    Plan->SetBoolField(TEXT("dirty"), bDirty);
    return Plan;
}

TSharedPtr<FJsonObject> BuildPlanned(const TArray<TSharedPtr<FPlannedEdit>>& Edits)
{
    TArray<TSharedPtr<FJsonValue>> Operations;
    for (const TSharedPtr<FPlannedEdit>& Edit : Edits)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetNumberField(TEXT("index"), Edit->Index);
        Item->SetStringField(TEXT("operation"), Edit->Kind);
        Item->SetStringField(TEXT("property"), Edit->Property->GetPathName());
        if (Edit->Kind == TEXT("set"))
        {
            Item->SetField(TEXT("value"), NativePropertyValues(Edit->RequestedValues));
        }
        Item->SetBoolField(TEXT("changed"), Edit->bChanged);
        Operations.Add(MakeShared<FJsonValueObject>(Item));
    }
    TSharedPtr<FJsonObject> Planned = MakeShared<FJsonObject>();
    Planned->SetArrayField(TEXT("operations"), Operations);
    return Planned;
}

TSharedPtr<FJsonObject> BuildDiff(const TArray<TSharedPtr<FPlannedEdit>>& Edits)
{
    TArray<TSharedPtr<FJsonValue>> Changes;
    for (const TSharedPtr<FPlannedEdit>& Edit : Edits)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("property"), Edit->Property->GetPathName());
        Item->SetField(TEXT("before"), NativePropertyValues(Edit->BeforeValues));
        Item->SetField(TEXT("after"), NativePropertyValues(Edit->AfterValues));
        Item->SetBoolField(TEXT("overrideBefore"), Edit->bWasOverridden);
        Item->SetBoolField(TEXT("overrideAfter"), Edit->Kind == TEXT("set") || Edit->bIntroducedHere);
        Changes.Add(MakeShared<FJsonValueObject>(Item));
    }
    TSharedPtr<FJsonObject> Diff = MakeShared<FJsonObject>();
    Diff->SetArrayField(TEXT("changes"), Changes);
    return Diff;
}

TSharedPtr<FJsonObject> BuildResolvedRefs(const FSalResolvedTarget& Target, const TArray<TSharedPtr<FPlannedEdit>>& Edits)
{
    TSharedPtr<FJsonObject> Refs = MakeShared<FJsonObject>();
    Refs->SetStringField(TEXT("class"), Target.Class != nullptr ? Target.Class->GetPathName() : FString());
    Refs->SetStringField(TEXT("source"), Target.Class != nullptr && Target.Class->ClassGeneratedBy != nullptr
        ? Target.Class->ClassGeneratedBy->GetPathName()
        : FString());
    TArray<TSharedPtr<FJsonValue>> Properties;
    for (const TSharedPtr<FPlannedEdit>& Edit : Edits)
    {
        Properties.Add(MakeShared<FJsonValueString>(Edit->Property->GetPathName()));
    }
    Refs->SetArrayField(TEXT("properties"), Properties);
    return Refs;
}

bool DecodeMemberTarget(
    const TSharedPtr<FJsonObject>& Statement,
    const FString& TargetAlias,
    FString& OutName,
    FString& OutError)
{
    const TSharedPtr<FJsonObject>* Target = nullptr;
    const TSharedPtr<FJsonObject>* Owner = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
    FString TargetKind;
    FString OwnerKind;
    FString OwnerName;
    if (!Statement.IsValid()
        || !Statement->TryGetObjectField(TEXT("target"), Target)
        || Target == nullptr
        || !(*Target)->TryGetStringField(TEXT("kind"), TargetKind)
        || TargetKind != TEXT("member")
        || !(*Target)->TryGetObjectField(TEXT("object"), Owner)
        || Owner == nullptr
        || !(*Owner)->TryGetStringField(TEXT("kind"), OwnerKind)
        || OwnerKind != TEXT("local")
        || !(*Owner)->TryGetStringField(TEXT("name"), OwnerName)
        || OwnerName != TargetAlias
        || !(*Target)->TryGetArrayField(TEXT("path"), Path)
        || Path == nullptr
        || Path->Num() != 1
        || !(*Path)[0]->TryGetString(OutName)
        || OutName.IsEmpty())
    {
        OutError = TEXT("Class set/reset requires exactly one member path owned by the Patch target alias.");
        return false;
    }
    return true;
}

bool ImportPropertyElement(
    FProperty* Property,
    UObject* Owner,
    const FString& Text,
    void* Value,
    const int32 Index,
    FString& OutError)
{
    const TCHAR* End = Property->ImportText_Direct(
        *Text,
        DirectPropertyElement(Property, Value, Index),
        Owner,
        PPF_None,
        GLog);
    if (End == nullptr)
    {
        OutError = FString::Printf(TEXT("UE could not import value for %s[%d]."), *Property->GetName(), Index);
        return false;
    }
    while (*End != TEXT('\0') && FChar::IsWhitespace(*End))
    {
        ++End;
    }
    if (*End != TEXT('\0'))
    {
        OutError = FString::Printf(TEXT("Value for %s[%d] contains unconsumed text."), *Property->GetName(), Index);
        return false;
    }
    return true;
}

bool ParseDesiredValue(
    FProperty* Property,
    UObject* Owner,
    const TSharedPtr<FJsonValue>& Expression,
    void*& OutValue,
    TArray<FString>& OutTexts,
    FString& OutError)
{
    OutValue = Property->AllocateAndInitializeValue();
    OutTexts.Reset();
    if (Property->ArrayDim == 1)
    {
        FString Text;
        if (!Expression.IsValid() || !Expression->TryGetString(Text))
        {
            OutError = TEXT("A scalar or dynamic-container Default requires one complete native UE value string.");
            return false;
        }
        OutTexts.Add(Text);
        return ImportPropertyElement(Property, Owner, Text, OutValue, 0, OutError);
    }

    const TArray<TSharedPtr<FJsonValue>>* Elements = nullptr;
    if (!Expression.IsValid() || !Expression->TryGetArray(Elements) || Elements == nullptr || Elements->Num() != Property->ArrayDim)
    {
        OutError = FString::Printf(
            TEXT("Fixed-array Default %s requires exactly %d native UE value strings."),
            *Property->GetName(),
            Property->ArrayDim);
        return false;
    }
    OutTexts.Reserve(Property->ArrayDim);
    for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
    {
        FString Text;
        if (!(*Elements)[Index].IsValid() || !(*Elements)[Index]->TryGetString(Text))
        {
            OutError = FString::Printf(TEXT("Fixed-array element %s[%d] must be one native UE value string."), *Property->GetName(), Index);
            return false;
        }
        OutTexts.Add(Text);
        if (!ImportPropertyElement(Property, Owner, Text, OutValue, Index, OutError))
        {
            return false;
        }
    }
    return true;
}

bool ClassMatchesMetadataToken(const UClass* Class, FString Token, const bool bAllowParents)
{
    Token.TrimStartAndEndInline();
    Token.TrimQuotesInline();
    for (const UClass* Cursor = Class; Cursor != nullptr; Cursor = bAllowParents ? Cursor->GetSuperClass() : nullptr)
    {
        if (Cursor->GetPathName().Equals(Token, ESearchCase::IgnoreCase)
            || Cursor->GetName().Equals(Token, ESearchCase::IgnoreCase)
            || Cursor->GetAuthoredName().Equals(Token, ESearchCase::IgnoreCase))
        {
            return true;
        }
        if (!bAllowParents)
        {
            break;
        }
    }
    return false;
}

bool ClassMatchesMetadataList(const UClass* Class, const FString& List, const bool bAllowParents)
{
    TArray<FString> Tokens;
    List.ParseIntoArray(Tokens, TEXT(","), true);
    for (FString Token : Tokens)
    {
        if (ClassMatchesMetadataToken(Class, MoveTemp(Token), bAllowParents))
        {
            return true;
        }
    }
    return false;
}

bool ValidateDesiredElement(const FProperty* Property, const void* Value, FString& OutError)
{
    if (Property == nullptr || Value == nullptr)
    {
        OutError = TEXT("The imported Class Default value is unavailable.");
        return false;
    }
    if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
    {
        const bool bUnsigned = Property->IsA<FByteProperty>()
            || Property->IsA<FUInt16Property>()
            || Property->IsA<FUInt32Property>()
            || Property->IsA<FUInt64Property>();
        const long double Number = Numeric->IsFloatingPoint()
            ? static_cast<long double>(Numeric->GetFloatingPointPropertyValue(Value))
            : bUnsigned
                ? static_cast<long double>(Numeric->GetUnsignedIntPropertyValue(Value))
                : static_cast<long double>(Numeric->GetSignedIntPropertyValue(Value));
        const FString ClampMin = Property->GetMetaData(TEXT("ClampMin"));
        const FString ClampMax = Property->GetMetaData(TEXT("ClampMax"));
        if (!ClampMin.IsEmpty() && Number < static_cast<long double>(FCString::Atod(*ClampMin)))
        {
            OutError = FString::Printf(TEXT("Value violates ClampMin %s for %s."), *ClampMin, *Property->GetName());
            return false;
        }
        if (!ClampMax.IsEmpty() && Number > static_cast<long double>(FCString::Atod(*ClampMax)))
        {
            OutError = FString::Printf(TEXT("Value violates ClampMax %s for %s."), *ClampMax, *Property->GetName());
            return false;
        }
    }
    if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue(Value);
        if (ObjectValue == nullptr
            && (Property->HasAnyPropertyFlags(CPF_NonNullable) || Property->HasAnyPropertyFlags(CPF_NoClear)))
        {
            OutError = FString::Printf(TEXT("%s does not allow a null value."), *Property->GetName());
            return false;
        }
        if (ObjectValue != nullptr)
        {
            const UClass* CandidateClass = Cast<UClass>(ObjectValue);
            if (CandidateClass == nullptr)
            {
                CandidateClass = ObjectValue->GetClass();
            }
            const FString AllowedClasses = Property->GetMetaData(TEXT("AllowedClasses"));
            if (!AllowedClasses.IsEmpty() && !ClassMatchesMetadataList(CandidateClass, AllowedClasses, true))
            {
                OutError = FString::Printf(TEXT("%s is outside AllowedClasses: %s."), *CandidateClass->GetPathName(), *AllowedClasses);
                return false;
            }
            const FString DisallowedClasses = Property->GetMetaData(TEXT("DisallowedClasses"));
            if (!DisallowedClasses.IsEmpty() && ClassMatchesMetadataList(CandidateClass, DisallowedClasses, true))
            {
                OutError = FString::Printf(TEXT("%s matches DisallowedClasses: %s."), *CandidateClass->GetPathName(), *DisallowedClasses);
                return false;
            }
            if (Property->GetBoolMetaData(TEXT("ExactClass"))
                && ObjectProperty->PropertyClass != nullptr
                && CandidateClass != ObjectProperty->PropertyClass)
            {
                OutError = FString::Printf(TEXT("%s requires exact class %s."), *Property->GetName(), *ObjectProperty->PropertyClass->GetPathName());
                return false;
            }
            const FString MustImplement = Property->GetMetaData(TEXT("MustImplement"));
            if (!MustImplement.IsEmpty())
            {
                bool bImplements = false;
                for (const UClass* Cursor = CandidateClass; Cursor != nullptr && !bImplements; Cursor = Cursor->GetSuperClass())
                {
                    for (const FImplementedInterface& Interface : Cursor->Interfaces)
                    {
                        if (ClassMatchesMetadataToken(Interface.Class, MustImplement, true))
                        {
                            bImplements = true;
                            break;
                        }
                    }
                }
                if (!bImplements)
                {
                    OutError = FString::Printf(TEXT("%s does not satisfy MustImplement %s."), *CandidateClass->GetPathName(), *MustImplement);
                    return false;
                }
            }
        }
    }
    return true;
}

bool ValidateDesiredValue(const FProperty* Property, const void* Value, FString& OutError)
{
    if (Property == nullptr || Value == nullptr)
    {
        OutError = TEXT("The imported Class Default value is unavailable.");
        return false;
    }
    for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
    {
        if (!ValidateDesiredElement(Property, DirectPropertyElement(Property, Value, Index), OutError))
        {
            OutError = FString::Printf(TEXT("%s[%d]: %s"), *Property->GetName(), Index, *OutError);
            return false;
        }
    }
    return true;
}

TSharedPtr<FJsonObject> BuildPatchReadback(
    const FSalResolvedTarget& Target,
    const TArray<TSharedPtr<FPlannedEdit>>& Edits,
    const bool bDryRun,
    const bool bApplied)
{
    FSalObjectBuilder Builder;
    const FString ClassAlias = Builder.UniqueAlias(Target.Class->GetName());
    Builder.AddLocalBinding(ClassAlias, CompactClassValue(Target.Class));
    for (int32 Index = 0; Index < Edits.Num(); ++Index)
    {
        const TSharedPtr<FPlannedEdit>& Edit = Edits[Index];
        bool bHasLaterEdit = false;
        for (int32 Later = Index + 1; Later < Edits.Num(); ++Later)
        {
            if (Edits[Later]->Name == Edit->Name)
            {
                bHasLaterEdit = true;
                break;
            }
        }
        if (bHasLaterEdit)
        {
            continue;
        }
        FDefaultEntry Current;
        if (!FindExactDefault(Target.Class, Edit->Name, Current))
        {
            continue;
        }
        AddDefaultGroup(Builder, ClassAlias, Target.Class, Current, true, false, bApplied && Edit->bFinalChanged ? Edit->Kind : FString());
        if (bDryRun)
        {
            Builder.AddComment(Edit->Kind == TEXT("set")
                ? TEXT("planned: set ") + NativeValuesComment(Edit->RequestedValues)
                : TEXT("planned: reset"));
            Builder.AddComment(TEXT("valid: true"));
            Builder.AddComment(TEXT("applied: false"));
        }
    }
    return Builder.BuildObject();
}

bool IsLastEditForProperty(const TArray<TSharedPtr<FPlannedEdit>>& Edits, const int32 Index)
{
    for (int32 Later = Index + 1; Later < Edits.Num(); ++Later)
    {
        if (Edits[Later]->Name == Edits[Index]->Name)
        {
            return false;
        }
    }
    return true;
}
}

TSharedPtr<FJsonObject> FSalClassInterface::Query(const FSalQuery& Query, const FSalResolvedTarget& Target)
{
    FString Operation;
    FString ValidationError;
    if (!ValidateQuery(Query, Target, Operation, ValidationError))
    {
        return ErrorResult(
            TEXT("capability.unsupported_query"),
            ValidationError,
            Operation,
            {TEXT("summary"), TEXT("properties"), TEXT("property"), TEXT("functions"), TEXT("function"), TEXT("defaults"), TEXT("default")});
    }

    UClass* Class = Target.Class;
    FSalObjectBuilder Builder;
    if (Operation == TEXT("summary"))
    {
        const FString Alias = Builder.UniqueAlias(Class->GetName());
        Builder.AddLocalBinding(Alias, CompleteClassValue(Class));
        const TArray<FProperty*> Properties = EffectiveProperties(Class);
        const TArray<UFunction*> Functions = EffectiveFunctions(Class);
        const TArray<FDefaultEntry> Defaults = EffectiveDefaults(Class);
        int32 OverrideCount = 0;
        for (const FDefaultEntry& Entry : Defaults)
        {
            OverrideCount += Entry.bOverridden && !Entry.Property->HasAnyPropertyFlags(CPF_Config) ? 1 : 0;
        }
        Builder.AddComment(FString::Printf(TEXT("properties: %d effective"), Properties.Num()));
        Builder.AddComment(FString::Printf(TEXT("functions: %d effective"), Functions.Num()));
        Builder.AddComment(FString::Printf(TEXT("defaults: %d effective"), Defaults.Num()));
        Builder.AddComment(FString::Printf(TEXT("default overrides: %d local"), OverrideCount));
        return Builder.BuildResult();
    }

    if (Operation == TEXT("property"))
    {
        FString Name;
        Query.Operation->TryGetStringField(TEXT("name"), Name);
        bool bAmbiguous = false;
        FProperty* Property = FindExactProperty(Class, Name, &bAmbiguous);
        if (Property == nullptr)
        {
            return ErrorResult(
                bAmbiguous ? TEXT("resolution.ambiguous_selector") : TEXT("resolution.property_not_found"),
                bAmbiguous
                    ? FString::Printf(TEXT("Property authored name is ambiguous in %s; use the native FName: %s."), *Class->GetPathName(), *Name)
                    : FString::Printf(TEXT("Property was not found in %s: %s."), *Class->GetPathName(), *Name),
                Operation);
        }
        const FString Alias = Builder.UniqueAlias(Property->GetAuthoredName());
        Builder.AddLocalBinding(Alias, PropertyValue(Property, true));
        if (HasDetail(Query, TEXT("schema")))
        {
            AddPropertySchema(Builder, Class, Property, false, false, IsSparseProperty(Class, Property));
        }
        return Builder.BuildResult();
    }

    if (Operation == TEXT("function"))
    {
        FString Name;
        Query.Operation->TryGetStringField(TEXT("name"), Name);
        bool bAmbiguous = false;
        UFunction* Function = FindExactFunction(Class, Name, &bAmbiguous);
        if (Function == nullptr)
        {
            return ErrorResult(
                bAmbiguous ? TEXT("resolution.ambiguous_selector") : TEXT("resolution.function_not_found"),
                bAmbiguous
                    ? FString::Printf(TEXT("Function authored name is ambiguous in %s; use the native FName: %s."), *Class->GetPathName(), *Name)
                    : FString::Printf(TEXT("Function was not found in %s: %s."), *Class->GetPathName(), *Name),
                Operation);
        }
        const FString Alias = Builder.UniqueAlias(Function->GetAuthoredName());
        Builder.AddLocalBinding(Alias, FunctionValue(Function, true));
        for (TFieldIterator<FProperty> It(Function, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        {
            FProperty* Parameter = *It;
            if (Parameter != nullptr && Parameter->HasAnyPropertyFlags(CPF_Parm))
            {
                const FString ParameterAlias = Builder.UniqueAlias(Parameter->GetAuthoredName());
                Builder.AddLocalBinding(ParameterAlias, PropertyValue(Parameter, true));
            }
        }
        AddFunctionNavigation(Builder, Function);
        if (HasDetail(Query, TEXT("schema")))
        {
            AddFunctionSchema(Builder, Function);
        }
        return Builder.BuildResult();
    }

    if (Operation == TEXT("default"))
    {
        FString Name;
        Query.Operation->TryGetStringField(TEXT("name"), Name);
        FDefaultEntry Entry;
        bool bAmbiguous = false;
        if (!FindExactDefault(Class, Name, Entry, &bAmbiguous))
        {
            return ErrorResult(
                bAmbiguous ? TEXT("resolution.ambiguous_selector") : TEXT("resolution.default_not_found"),
                bAmbiguous
                    ? FString::Printf(TEXT("Default authored name is ambiguous in %s; use the native FName: %s."), *Class->GetPathName(), *Name)
                    : FString::Printf(TEXT("Default was not found in %s: %s."), *Class->GetPathName(), *Name),
                Operation);
        }
        const FString ClassAlias = Builder.UniqueAlias(Class->GetName());
        Builder.AddLocalBinding(ClassAlias, CompactClassValue(Class));
        AddDefaultGroup(Builder, ClassAlias, Class, Entry, true, HasDetail(Query, TEXT("schema")));
        return Builder.BuildResult();
    }

    FString SearchText;
    Query.Operation->TryGetStringField(TEXT("text"), SearchText);
    FSalPage Page;
    if (!DecodeClassPage(Query, Class, Page))
    {
        return ErrorResult(
            TEXT("validation.invalid_cursor"),
            TEXT("Class cursor does not belong to this Query. Re-run the first page."),
            Operation);
    }
    int32 Total = 0;
    int32 End = 0;

    if (Operation == TEXT("properties"))
    {
        TArray<FPropertyMatch> Matches;
        for (FProperty* Property : EffectiveProperties(Class))
        {
            const int32 Score = PropertyScore(Property, SearchText);
            if (Score != INDEX_NONE)
            {
                Matches.Add({Property, Score});
            }
        }
        if (!SearchText.IsEmpty())
        {
            Matches.Sort([](const FPropertyMatch& Left, const FPropertyMatch& Right)
            {
                return Left.Score != Right.Score
                    ? Left.Score > Right.Score
                    : Left.Property->GetPathName() < Right.Property->GetPathName();
            });
        }
        Total = Matches.Num();
        const int32 Start = FMath::Min(Page.Offset, Total);
        End = static_cast<int32>(FMath::Min<int64>(static_cast<int64>(Start) + Page.Limit, Total));
        for (int32 Index = Start; Index < End; ++Index)
        {
            const FString Alias = Builder.UniqueAlias(Matches[Index].Property->GetAuthoredName());
            Builder.AddLocalBinding(Alias, PropertyValue(Matches[Index].Property, false));
        }
    }
    else if (Operation == TEXT("functions"))
    {
        TArray<FFunctionMatch> Matches;
        for (UFunction* Function : EffectiveFunctions(Class))
        {
            const int32 Score = FunctionScore(Function, SearchText);
            if (Score != INDEX_NONE)
            {
                Matches.Add({Function, Score});
            }
        }
        if (!SearchText.IsEmpty())
        {
            Matches.Sort([](const FFunctionMatch& Left, const FFunctionMatch& Right)
            {
                return Left.Score != Right.Score
                    ? Left.Score > Right.Score
                    : Left.Function->GetPathName() < Right.Function->GetPathName();
            });
        }
        Total = Matches.Num();
        const int32 Start = FMath::Min(Page.Offset, Total);
        End = static_cast<int32>(FMath::Min<int64>(static_cast<int64>(Start) + Page.Limit, Total));
        for (int32 Index = Start; Index < End; ++Index)
        {
            const FString Alias = Builder.UniqueAlias(Matches[Index].Function->GetAuthoredName());
            Builder.AddLocalBinding(Alias, FunctionValue(Matches[Index].Function, false));
        }
    }
    else
    {
        const bool bOnlyOverridden = ReadTrueOverrideCondition(Query.Where);
        TArray<FDefaultMatch> Matches;
        for (const FDefaultEntry& Entry : EffectiveDefaults(Class))
        {
            if (bOnlyOverridden && (!Entry.bOverridden || Entry.Property->HasAnyPropertyFlags(CPF_Config)))
            {
                continue;
            }
            const int32 Score = PropertyScore(Entry.Property, SearchText);
            if (Score != INDEX_NONE)
            {
                Matches.Add({Entry, Score});
            }
        }
        if (!SearchText.IsEmpty())
        {
            Matches.Sort([](const FDefaultMatch& Left, const FDefaultMatch& Right)
            {
                return Left.Score != Right.Score
                    ? Left.Score > Right.Score
                    : Left.Entry.Property->GetPathName() < Right.Entry.Property->GetPathName();
            });
        }
        Total = Matches.Num();
        const int32 Start = FMath::Min(Page.Offset, Total);
        End = static_cast<int32>(FMath::Min<int64>(static_cast<int64>(Start) + Page.Limit, Total));
        const FString ClassAlias = Builder.UniqueAlias(Class->GetName());
        Builder.AddLocalBinding(ClassAlias, CompactClassValue(Class));
        FName LastCategory = NAME_None;
        bool bHasCategory = false;
        for (int32 Index = Start; Index < End; ++Index)
        {
            if (SearchText.IsEmpty())
            {
                const FName Category = FObjectEditorUtils::GetCategoryFName(Matches[Index].Entry.Property);
                if (!Category.IsNone() && (!bHasCategory || Category != LastCategory))
                {
                    Builder.AddComment(TEXT("category: ") + Category.ToString());
                    LastCategory = Category;
                    bHasCategory = true;
                }
            }
            AddDefaultGroup(Builder, ClassAlias, Class, Matches[Index].Entry, false, false);
        }
    }

    if (Total == 0)
    {
        Builder.AddComment(TEXT("no matches"));
    }
    TSharedPtr<FJsonObject> Result = Builder.BuildResult();
    if (End < Total)
    {
        TSharedPtr<FJsonObject> PageObject = MakeShared<FJsonObject>();
        PageObject->SetStringField(TEXT("next"), EncodeClassCursor(Query, Class, End));
        Result->SetObjectField(TEXT("page"), PageObject);
    }
    return Result;
}

TSharedPtr<FJsonObject> FSalClassInterface::Patch(const FSalPatch& Patch, const FSalResolvedTarget& Target)
{
    if (Target.Kind != ESalTargetKind::Class || Target.Class == nullptr)
    {
        return MakeMutationResult(
            nullptr,
            {FSalDiagnostics::Error(TEXT("validation.exact_class_required"), TEXT("Class Patch requires one exact class(path: ...) target."))
                .Interface(TEXT("class"))
                .Build()},
            Patch.bDryRun,
            false,
            false,
            Target.AssetPath,
            TEXT("defaults"));
    }

    UClass* Class = Target.Class;
    UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy);
    if (Patch.Statements.Num() == 1)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        FString Kind;
        if (Patch.Statements[0].IsValid()
            && Patch.Statements[0]->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("save"))
        {
            UPackage* SourcePackage = Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
            if (Blueprint == nullptr
                || SourcePackage == nullptr
                || Blueprint->HasAnyFlags(RF_Transient)
                || SourcePackage == GetTransientPackage()
                || FPackageName::IsTempPackage(SourcePackage->GetName()))
            {
                return MakeMutationResult(
                    ClassCurrentObject(Target),
                    {FSalDiagnostics::Error(TEXT("validation.class_source_required"), TEXT("Class save requires a persistent Blueprint Generated Class source."))
                        .Interface(TEXT("class"))
                        .Operation(TEXT("save"))
                        .Build()},
                    Patch.bDryRun,
                    false,
                    false,
                    Target.AssetPath,
                    TEXT("save"));
            }
            const bool bDirty = SourcePackage->IsDirty();
            const TSharedPtr<FJsonObject> Planned = SavePlan(Target, bDirty);
            if (Patch.bDryRun || !bDirty)
            {
                return MakeMutationResult(ClassCurrentObject(Target), {}, Patch.bDryRun, true, false, Target.AssetPath, TEXT("save"), Planned);
            }
            const TArray<UPackage*> Packages = {SourcePackage};
            if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, true))
            {
                return MakeMutationResult(
                    ClassCurrentObject(Target),
                    {FSalDiagnostics::Error(TEXT("validation.save_failed"), TEXT("UE failed to save the source Blueprint Package."))
                        .Interface(TEXT("class"))
                        .Operation(TEXT("save"))
                        .Ref(Blueprint->GetPathName())
                        .Build()},
                    false,
                    false,
                    false,
                    Target.AssetPath,
                    TEXT("save"),
                    Planned);
            }
            return MakeMutationResult(ClassCurrentObject(Target), {}, false, true, true, Target.AssetPath, TEXT("save"), Planned);
        }
    }

    if (Blueprint == nullptr)
    {
        return MakeMutationResult(
            ClassCurrentObject(Target),
            {FSalDiagnostics::Error(TEXT("validation.class_defaults_read_only"), TEXT("Native and source-less Classes have read-only Defaults."))
                .Interface(TEXT("class"))
                .Operation(TEXT("defaults"))
                .Ref(Class->GetPathName())
                .Build()},
            Patch.bDryRun,
            false,
            false,
            Target.AssetPath,
            TEXT("defaults"));
    }

    UObject* CDO = Class->GetDefaultObject();
    TArray<TSharedPtr<FPlannedEdit>> Edits;
    TMap<FString, TSharedPtr<FPlannedEdit>> LastEditByProperty;
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    for (int32 Index = 0; Index < Patch.Statements.Num(); ++Index)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        FString Kind;
        if (!Patch.Statements[Index].IsValid()
            || !Patch.Statements[Index]->TryGetObject(Statement)
            || Statement == nullptr
            || !(*Statement)->TryGetStringField(TEXT("kind"), Kind)
            || !(Kind == TEXT("set") || Kind == TEXT("reset")))
        {
            Diagnostics.Add(FSalDiagnostics::Error(TEXT("capability.unsupported_patch_operation"), TEXT("Class Defaults Patch supports only set and reset; save must be an independent request."))
                .Interface(TEXT("class"))
                .Path({TEXT("object"), TEXT("statements"), FString::FromInt(Index)})
                .Supported({TEXT("set"), TEXT("reset"), TEXT("save")})
                .Build());
            continue;
        }

        FString Name;
        FString Error;
        if (!DecodeMemberTarget(*Statement, Patch.Alias, Name, Error))
        {
            Diagnostics.Add(FSalDiagnostics::Error(TEXT("validation.invalid_default_target"), Error)
                .Interface(TEXT("class"))
                .Path({TEXT("object"), TEXT("statements"), FString::FromInt(Index), TEXT("target")})
                .Build());
            continue;
        }
        FDefaultEntry Entry;
        bool bAmbiguous = false;
        if (!FindExactDefault(Class, Name, Entry, &bAmbiguous))
        {
            Diagnostics.Add(FSalDiagnostics::Error(
                    bAmbiguous ? TEXT("resolution.ambiguous_selector") : TEXT("resolution.default_not_found"),
                    bAmbiguous
                        ? FString::Printf(TEXT("Default authored name is ambiguous; use the native FName: %s."), *Name)
                        : FString::Printf(TEXT("Default was not found: %s."), *Name))
                .Interface(TEXT("class"))
                .Path({TEXT("object"), TEXT("statements"), FString::FromInt(Index), TEXT("target")})
                .Ref(Name)
                .Build());
            continue;
        }
        if (!CanWriteDefault(Class, Entry, Error, &LastEditByProperty))
        {
            Diagnostics.Add(FSalDiagnostics::Error(TEXT("validation.default_read_only"), Error)
                .Interface(TEXT("class"))
                .Path({TEXT("object"), TEXT("statements"), FString::FromInt(Index), TEXT("target")})
                .Ref(Entry.Property->GetPathName())
                .Build());
            continue;
        }
        if (Kind == TEXT("reset") && Entry.Property->HasMetaData(TEXT("NoResetToDefault")))
        {
            Diagnostics.Add(FSalDiagnostics::Error(TEXT("validation.default_not_resettable"), TEXT("UE metadata disables reset for this Class Default."))
                .Interface(TEXT("class"))
                .Path({TEXT("object"), TEXT("statements"), FString::FromInt(Index), TEXT("target")})
                .Ref(Entry.Property->GetPathName())
                .Build());
            continue;
        }

        TSharedPtr<FPlannedEdit> Edit = MakeShared<FPlannedEdit>();
        Edit->Index = Index;
        Edit->Kind = Kind;
        Edit->Name = Entry.Property->GetName();
        Edit->Property = Entry.Property;
        Edit->bSparse = Entry.bSparse;
        Edit->bIntroducedHere = Entry.bIntroducedHere;
        const FString PropertyPath = Entry.Property->GetPathName();
        const TSharedPtr<FPlannedEdit> PreviousEdit = LastEditByProperty.FindRef(PropertyPath);
        const void* ProvisionalValue = PreviousEdit.IsValid()
            ? PreviousEdit->DesiredValue
            : Entry.Property->ContainerPtrToValuePtr<void>(Entry.Container);
        Edit->bWasOverridden = PreviousEdit.IsValid()
            ? PreviousEdit->Kind == TEXT("set") || PreviousEdit->bIntroducedHere
            : Entry.bOverridden;
        Edit->bInitialOverridden = PreviousEdit.IsValid()
            ? PreviousEdit->bInitialOverridden
            : Entry.bOverridden;
        Edit->BeforeValues = PreviousEdit.IsValid()
            ? PreviousEdit->AfterValues
            : ExportPropertyValues(Entry.Property, Entry.Container);
        if (Kind == TEXT("set"))
        {
            const TSharedPtr<FJsonValue> Requested = (*Statement)->TryGetField(TEXT("value"));
            if (!ParseDesiredValue(Edit->Property, CDO, Requested, Edit->DesiredValue, Edit->RequestedValues, Error))
            {
                Diagnostics.Add(FSalDiagnostics::Error(TEXT("validation.default_import_failed"), Error)
                    .Interface(TEXT("class"))
                    .Path({TEXT("object"), TEXT("statements"), FString::FromInt(Index), TEXT("value")})
                    .Build());
                continue;
            }
            if (!ValidateDesiredValue(Edit->Property, Edit->DesiredValue, Error))
            {
                Diagnostics.Add(FSalDiagnostics::Error(TEXT("validation.default_constraint_failed"), Error)
                    .Interface(TEXT("class"))
                    .Path({TEXT("object"), TEXT("statements"), FString::FromInt(Index), TEXT("value")})
                    .Actual(NativeValuesComment(Edit->RequestedValues))
                    .Build());
                continue;
            }
            Edit->AfterValues = ExportDirectPropertyValues(Edit->Property, Edit->DesiredValue);
            Edit->bChanged = !Edit->bWasOverridden
                || !IdenticalCompletePropertyValue(Edit->Property, ProvisionalValue, Edit->DesiredValue);
        }
        else
        {
            Edit->DesiredValue = Edit->Property->AllocateAndInitializeValue();
            if (!Entry.bIntroducedHere && Entry.ArchetypeContainer != nullptr)
            {
                const void* ArchetypeValue = Edit->Property->ContainerPtrToValuePtr<void>(Entry.ArchetypeContainer);
                Edit->Property->CopyCompleteValue(Edit->DesiredValue, ArchetypeValue);
            }
            Edit->AfterValues = ExportDirectPropertyValues(Edit->Property, Edit->DesiredValue);
            Edit->bChanged = Entry.bIntroducedHere
                ? !IdenticalCompletePropertyValue(Edit->Property, ProvisionalValue, Edit->DesiredValue)
                : Edit->bWasOverridden;
        }
        Edits.Add(Edit);
        LastEditByProperty.Add(PropertyPath, Edit);
    }

    for (int32 Index = 0; Index < Edits.Num(); ++Index)
    {
        const TSharedPtr<FPlannedEdit>& Edit = Edits[Index];
        if (!IsLastEditForProperty(Edits, Index)
            || Edit->Kind != TEXT("set")
            || Edit->bIntroducedHere)
        {
            continue;
        }
        FDefaultEntry Entry;
        if (FindExactDefault(Class, Edit->Name, Entry) && Entry.ArchetypeContainer != nullptr)
        {
            const void* ArchetypeValue = Edit->Property->ContainerPtrToValuePtr<const void>(Entry.ArchetypeContainer);
            if (IdenticalCompletePropertyValue(Edit->Property, Edit->DesiredValue, ArchetypeValue))
            {
                Diagnostics.Add(FSalDiagnostics::Error(
                        TEXT("validation.default_equal_inherited_not_durable"),
                        TEXT("UE 5.7 Blueprint CDO serialization cannot persist an explicit override equal to the inherited value; use reset or set a distinct value."))
                    .Interface(TEXT("class"))
                    .Path({TEXT("object"), TEXT("statements"), FString::FromInt(Edit->Index), TEXT("value")})
                    .Ref(Edit->Property->GetPathName())
                    .Actual(NativeValuesComment(Edit->RequestedValues))
                    .Build());
            }
        }
    }

    if (!Diagnostics.IsEmpty())
    {
        return MakeMutationResult(
            ClassCurrentObject(Target),
            Diagnostics,
            Patch.bDryRun,
            false,
            false,
            Target.AssetPath,
            TEXT("defaults"),
            BuildPlanned(Edits),
            BuildResolvedRefs(Target, Edits),
            BuildDiff(Edits));
    }

    for (int32 Index = 0; Index < Edits.Num(); ++Index)
    {
        if (!IsLastEditForProperty(Edits, Index))
        {
            continue;
        }
        const TSharedPtr<FPlannedEdit>& Edit = Edits[Index];
        FDefaultEntry Current;
        if (FindExactDefault(Class, Edit->Name, Current))
        {
            const void* CurrentValue = Edit->Property->ContainerPtrToValuePtr<void>(Current.Container);
            const bool bFinalOverride = Edit->Kind == TEXT("set") || Edit->bIntroducedHere;
            Edit->bFinalChanged = Edit->bInitialOverridden != bFinalOverride
                || !IdenticalCompletePropertyValue(Edit->Property, CurrentValue, Edit->DesiredValue);
        }
    }

    const TSharedPtr<FJsonObject> Planned = BuildPlanned(Edits);
    const TSharedPtr<FJsonObject> ResolvedRefs = BuildResolvedRefs(Target, Edits);
    const TSharedPtr<FJsonObject> Diff = BuildDiff(Edits);
    if (Patch.bDryRun)
    {
        return MakeMutationResult(
            BuildPatchReadback(Target, Edits, true, false),
            {},
            true,
            true,
            false,
            Target.AssetPath,
            TEXT("defaults"),
            Planned,
            ResolvedRefs,
            Diff);
    }

    TArray<TSharedPtr<FPlannedEdit>> FinalEdits;
    bool bAnyChanged = false;
    for (int32 Index = 0; Index < Edits.Num(); ++Index)
    {
        const TSharedPtr<FPlannedEdit>& Edit = Edits[Index];
        if (!IsLastEditForProperty(Edits, Index))
        {
            continue;
        }
        bAnyChanged |= Edit->bFinalChanged;
        if (Edit->bFinalChanged)
        {
            FinalEdits.Add(Edit);
        }
    }
    if (!bAnyChanged)
    {
        return MakeMutationResult(
            BuildPatchReadback(Target, Edits, false, false),
            {},
            false,
            true,
            false,
            Target.AssetPath,
            TEXT("defaults"),
            Planned,
            ResolvedRefs,
            Diff);
    }

    if (GEditor == nullptr || !GEditor->CanTransact() || GEditor->IsTransactionActive())
    {
        return MakeMutationResult(
            ClassCurrentObject(Target),
            {FSalDiagnostics::Error(TEXT("capability.transaction_unavailable"), TEXT("Class Defaults Patch requires one available top-level UE editor transaction."))
                .Interface(TEXT("class"))
                .Operation(TEXT("defaults"))
                .Build()},
            false,
            false,
            false,
            Target.AssetPath,
            TEXT("defaults"),
            Planned,
            ResolvedRefs,
            Diff);
    }

    TArray<TSharedPtr<FPropertyBackup>> Backups;
    Backups.Reserve(FinalEdits.Num());
    for (const TSharedPtr<FPlannedEdit>& Edit : FinalEdits)
    {
        FDefaultEntry Current;
        if (!FindExactDefault(Class, Edit->Name, Current) || Current.Container == nullptr)
        {
            return MakeMutationResult(
                ClassCurrentObject(Target),
                {FSalDiagnostics::Error(TEXT("validation.default_backup_failed"), TEXT("The live Class Default could not be captured before apply."))
                    .Interface(TEXT("class"))
                    .Operation(Edit->Kind)
                    .Ref(Edit->Property->GetPathName())
                    .Build()},
                false,
                false,
                false,
                Target.AssetPath,
                TEXT("defaults"),
                Planned,
                ResolvedRefs,
                Diff);
        }
        TSharedPtr<FPropertyBackup> Backup = MakeShared<FPropertyBackup>();
        Backup->Property = Edit->Property;
        Backup->Value = Edit->Property->AllocateAndInitializeValue();
        Edit->Property->CopyCompleteValue(
            Backup->Value,
            Edit->Property->ContainerPtrToValuePtr<const void>(Current.Container));
        Backups.Add(Backup);
    }

    UPackage* SourcePackage = Blueprint->GetOutermost();
    const bool bSourcePackageWasDirty = SourcePackage != nullptr && SourcePackage->IsDirty();
    FScopedTransaction Transaction(FText::FromString(TEXT("SAL Edit Class Defaults")));
    if (!Transaction.IsOutstanding())
    {
        return MakeMutationResult(
            ClassCurrentObject(Target),
            {FSalDiagnostics::Error(TEXT("capability.transaction_unavailable"), TEXT("UE did not open the required Class Defaults transaction."))
                .Interface(TEXT("class"))
                .Operation(TEXT("defaults"))
                .Build()},
            false,
            false,
            false,
            Target.AssetPath,
            TEXT("defaults"),
            Planned,
            ResolvedRefs,
            Diff);
    }
    UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Class);
    const bool bSparseSerializableBefore = GeneratedClass != nullptr && GeneratedClass->bIsSparseClassDataSerializable;
    Blueprint->Modify();
    Class->Modify();
    CDO->Modify();
    void* WritableSparseData = nullptr;
    for (const TSharedPtr<FPlannedEdit>& Edit : FinalEdits)
    {
        if (Edit->bSparse)
        {
            WritableSparseData = Class->GetOrCreateSparseClassData();
            break;
        }
    }
    if (WritableSparseData == nullptr)
    {
        bool bRequiresSparseData = false;
        for (const TSharedPtr<FPlannedEdit>& Edit : FinalEdits)
        {
            bRequiresSparseData |= Edit->bSparse;
        }
        if (bRequiresSparseData)
        {
            Transaction.Cancel();
            if (SourcePackage != nullptr && !bSourcePackageWasDirty)
            {
                SourcePackage->SetDirtyFlag(false);
            }
            return MakeMutationResult(
                ClassCurrentObject(Target),
                {FSalDiagnostics::Error(TEXT("validation.sparse_data_unavailable"), TEXT("UE could not allocate writable Sparse Class Data."))
                    .Interface(TEXT("class"))
                    .Operation(TEXT("defaults"))
                    .Ref(Class->GetPathName())
                    .Build()},
                false,
                false,
                false,
                Target.AssetPath,
                TEXT("defaults"),
                Planned,
                ResolvedRefs,
                Diff);
        }
    }
    else if (GeneratedClass != nullptr)
    {
        GeneratedClass->bIsSparseClassDataSerializable = true;
    }
    for (int32 Index = 0; Index < FinalEdits.Num(); ++Index)
    {
        const TSharedPtr<FPlannedEdit>& Edit = FinalEdits[Index];
        if (!UKismetSystemLibrary::Generic_SetEditorProperty(
                CDO,
                Edit->Property->GetFName(),
                Edit->DesiredValue,
                Edit->Property,
                EPropertyAccessChangeNotifyMode::Default))
        {
            bool bRollbackSucceeded = true;
            for (int32 RollbackIndex = Index; RollbackIndex >= 0; --RollbackIndex)
            {
                const TSharedPtr<FPropertyBackup>& Backup = Backups[RollbackIndex];
                bRollbackSucceeded &= UKismetSystemLibrary::Generic_SetEditorProperty(
                    CDO,
                    Backup->Property->GetFName(),
                    Backup->Value,
                    Backup->Property,
                    EPropertyAccessChangeNotifyMode::Default);
            }
            if (GeneratedClass != nullptr)
            {
                GeneratedClass->bIsSparseClassDataSerializable = bSparseSerializableBefore;
            }
            if (bRollbackSucceeded)
            {
                // Manual restoration returned the object to its original state,
                // so this failed attempt must not remain in the undo stack.
                Transaction.Cancel();
            }
            if (SourcePackage != nullptr)
            {
                // If restoration failed, preserve the transaction as a possible
                // user recovery path and never present the partial state as clean.
                SourcePackage->SetDirtyFlag(bRollbackSucceeded ? bSourcePackageWasDirty : true);
            }
            return MakeMutationResult(
                ClassCurrentObject(Target),
                {FSalDiagnostics::Error(
                        bRollbackSucceeded ? TEXT("validation.default_write_failed") : TEXT("validation.default_rollback_failed"),
                        bRollbackSucceeded
                            ? TEXT("UE Property Access failed while applying the preflighted Class Default edit; all attempted values were restored.")
                            : TEXT("UE Property Access failed and at least one attempted value could not be restored."))
                    .Interface(TEXT("class"))
                    .Operation(Edit->Kind)
                    .Ref(Edit->Property->GetPathName())
                    .Build()},
                false,
                false,
                !bRollbackSucceeded,
                Target.AssetPath,
                TEXT("defaults"),
                Planned,
                ResolvedRefs,
                Diff);
        }
    }
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    return MakeMutationResult(
        BuildPatchReadback(Target, Edits, false, true),
        {},
        false,
        true,
        true,
        Target.AssetPath,
        TEXT("defaults"),
        Planned,
        ResolvedRefs,
        Diff);
}
}
