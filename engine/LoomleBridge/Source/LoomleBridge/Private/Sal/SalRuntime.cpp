// Copyright 2026 Loomle contributors.

#include "SalRuntime.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "LoomleMutationResult.h"
#include "UObject/FieldPathProperty.h"
#include "UObject/PropertyOptional.h"
#include "UObject/UnrealType.h"

namespace Loomle::Sal
{
namespace
{
struct FNativeNamedPropertyFlag
{
    uint64 Value;
    const TCHAR* Name;
};
}

FString ExprString(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid() || Value->IsNull())
    {
        return FString();
    }
    FString Text;
    if (Value->TryGetString(Text))
    {
        return Text;
    }
    double Number = 0.0;
    if (Value->TryGetNumber(Number))
    {
        return FString::SanitizeFloat(Number);
    }
    bool bBool = false;
    if (Value->TryGetBool(bBool))
    {
        return bBool ? TEXT("true") : TEXT("false");
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (Value->TryGetObject(Object) && Object != nullptr && (*Object).IsValid())
    {
        FString Kind;
        (*Object)->TryGetStringField(TEXT("kind"), Kind);
        if (Kind == TEXT("name") || Kind == TEXT("local"))
        {
            (*Object)->TryGetStringField(TEXT("name"), Text);
            return Text;
        }
        (*Object)->TryGetStringField(TEXT("id"), Text);
        return Text;
    }
    return FString();
}

FString ConditionField(const TSharedPtr<FJsonObject>& Condition)
{
    const TSharedPtr<FJsonObject>* Field = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
    if (!Condition.IsValid()
        || !Condition->TryGetObjectField(TEXT("field"), Field)
        || Field == nullptr
        || !(*Field)->TryGetArrayField(TEXT("path"), Path)
        || Path == nullptr)
    {
        return FString();
    }
    TArray<FString> Segments;
    for (const TSharedPtr<FJsonValue>& Value : *Path)
    {
        FString Segment;
        if (Value.IsValid() && Value->TryGetString(Segment))
        {
            Segments.Add(Segment);
        }
    }
    return FString::Join(Segments, TEXT("."));
}

bool HasDetail(const FSalQuery& Query, const FString& Detail)
{
    return Query.With.Contains(Detail);
}

bool ParseNonNegativeInt32(const FString& Text, int32& OutValue)
{
    OutValue = 0;
    if (Text.IsEmpty())
    {
        return false;
    }
    uint64 Parsed = 0;
    for (const TCHAR Character : Text)
    {
        if (!FChar::IsDigit(Character))
        {
            return false;
        }
        Parsed = Parsed * 10 + static_cast<uint64>(Character - TEXT('0'));
        if (Parsed > static_cast<uint64>(MAX_int32))
        {
            return false;
        }
    }
    OutValue = static_cast<int32>(Parsed);
    return true;
}

FString ExportPropertyValue(const FProperty* Property, const void* Container)
{
    if (Property == nullptr || Container == nullptr)
    {
        return FString();
    }
    FString Text;
    const void* Value = Property->ContainerPtrToValuePtr<void>(Container);
    Property->ExportTextItem_Direct(Text, Value, nullptr, nullptr, PPF_None);
    return Text;
}

bool ImportPropertyValue(FProperty* Property, void* Container, const FString& Text, FString& OutError)
{
    OutError.Reset();
    if (Property == nullptr || Container == nullptr)
    {
        OutError = TEXT("Property or value container is unavailable.");
        return false;
    }
    void* Value = Property->ContainerPtrToValuePtr<void>(Container);
    const TCHAR* End = Property->ImportText_Direct(*Text, Value, nullptr, PPF_None, GLog);
    if (End == nullptr)
    {
        OutError = FString::Printf(TEXT("UE could not import value for %s."), *Property->GetName());
        return false;
    }
    while (*End != TEXT('\0') && FChar::IsWhitespace(*End))
    {
        ++End;
    }
    if (*End != TEXT('\0'))
    {
        OutError = FString::Printf(TEXT("Value for %s contains unconsumed text."), *Property->GetName());
        return false;
    }
    return true;
}

TSharedPtr<FJsonValue> NativeValue(const FString& Text)
{
    return MakeShared<FJsonValueString>(Text);
}

FString NativePropertyTypeText(const FProperty* Property)
{
    if (Property == nullptr)
    {
        return FString();
    }
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        return FString::Printf(TEXT("ArrayProperty(%s)"), *NativePropertyTypeText(Array->Inner));
    }
    if (const FSetProperty* Set = CastField<FSetProperty>(Property))
    {
        return FString::Printf(TEXT("SetProperty(%s)"), *NativePropertyTypeText(Set->ElementProp));
    }
    if (const FMapProperty* Map = CastField<FMapProperty>(Property))
    {
        return FString::Printf(
            TEXT("MapProperty(%s, %s)"),
            *NativePropertyTypeText(Map->KeyProp),
            *NativePropertyTypeText(Map->ValueProp));
    }
    if (const FOptionalProperty* Optional = CastField<FOptionalProperty>(Property))
    {
        return FString::Printf(
            TEXT("OptionalProperty(%s)"),
            *NativePropertyTypeText(Optional->GetValueProperty()));
    }
    if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
    {
        return FString::Printf(
            TEXT("EnumProperty(%s, %s)"),
            Enum->GetEnum() != nullptr ? *Enum->GetEnum()->GetName() : TEXT("None"),
            *NativePropertyTypeText(Enum->GetUnderlyingProperty()));
    }
    if (const FByteProperty* Byte = CastField<FByteProperty>(Property); Byte != nullptr && Byte->Enum != nullptr)
    {
        return FString::Printf(TEXT("ByteProperty(%s)"), *Byte->Enum->GetName());
    }
    if (const FFieldPathProperty* FieldPath = CastField<FFieldPathProperty>(Property))
    {
        return FString::Printf(
            TEXT("FieldPathProperty(%s)"),
            FieldPath->PropertyClass != nullptr ? *FieldPath->PropertyClass->GetName() : TEXT("None"));
    }
    if (const FSoftClassProperty* SoftClass = CastField<FSoftClassProperty>(Property))
    {
        return FString::Printf(TEXT("SoftClassProperty(%s)"), *GetPathNameSafe(SoftClass->MetaClass));
    }
    if (const FClassProperty* Class = CastField<FClassProperty>(Property))
    {
        return FString::Printf(TEXT("ClassProperty(%s)"), *GetPathNameSafe(Class->MetaClass));
    }
    if (const FSoftObjectProperty* SoftObject = CastField<FSoftObjectProperty>(Property))
    {
        return FString::Printf(TEXT("SoftObjectProperty(%s)"), *GetPathNameSafe(SoftObject->PropertyClass));
    }
    if (const FWeakObjectProperty* WeakObject = CastField<FWeakObjectProperty>(Property))
    {
        return FString::Printf(TEXT("WeakObjectProperty(%s)"), *GetPathNameSafe(WeakObject->PropertyClass));
    }
    if (const FLazyObjectProperty* LazyObject = CastField<FLazyObjectProperty>(Property))
    {
        return FString::Printf(TEXT("LazyObjectProperty(%s)"), *GetPathNameSafe(LazyObject->PropertyClass));
    }
    if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Property))
    {
        return FString::Printf(
            TEXT("%s(%s)"),
            *Property->GetClass()->GetName(),
            *GetPathNameSafe(Object->PropertyClass));
    }
    if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        return FString::Printf(TEXT("StructProperty(%s)"), *GetPathNameSafe(Struct->Struct));
    }
    if (const FInterfaceProperty* Interface = CastField<FInterfaceProperty>(Property))
    {
        return FString::Printf(TEXT("InterfaceProperty(%s)"), *GetPathNameSafe(Interface->InterfaceClass));
    }
    if (const FDelegateProperty* Delegate = CastField<FDelegateProperty>(Property))
    {
        return FString::Printf(TEXT("DelegateProperty(%s)"), *GetPathNameSafe(Delegate->SignatureFunction));
    }
    if (const FMulticastDelegateProperty* Delegate = CastField<FMulticastDelegateProperty>(Property))
    {
        return FString::Printf(
            TEXT("%s(%s)"),
            *Property->GetClass()->GetName(),
            *GetPathNameSafe(Delegate->SignatureFunction));
    }
    return Property->GetClass()->GetName();
}

FString NativePropertyFlagsText(uint64 Flags)
{
    static const FNativeNamedPropertyFlag Known[] = {
        {CPF_Edit, TEXT("CPF_Edit")},
        {CPF_ConstParm, TEXT("CPF_ConstParm")},
        {CPF_BlueprintVisible, TEXT("CPF_BlueprintVisible")},
        {CPF_ExportObject, TEXT("CPF_ExportObject")},
        {CPF_BlueprintReadOnly, TEXT("CPF_BlueprintReadOnly")},
        {CPF_Net, TEXT("CPF_Net")},
        {CPF_EditFixedSize, TEXT("CPF_EditFixedSize")},
        {CPF_Parm, TEXT("CPF_Parm")},
        {CPF_OutParm, TEXT("CPF_OutParm")},
        {CPF_ZeroConstructor, TEXT("CPF_ZeroConstructor")},
        {CPF_ReturnParm, TEXT("CPF_ReturnParm")},
        {CPF_DisableEditOnTemplate, TEXT("CPF_DisableEditOnTemplate")},
        {CPF_NonNullable, TEXT("CPF_NonNullable")},
        {CPF_Transient, TEXT("CPF_Transient")},
        {CPF_Config, TEXT("CPF_Config")},
        {CPF_RequiredParm, TEXT("CPF_RequiredParm")},
        {CPF_DisableEditOnInstance, TEXT("CPF_DisableEditOnInstance")},
        {CPF_EditConst, TEXT("CPF_EditConst")},
        {CPF_GlobalConfig, TEXT("CPF_GlobalConfig")},
        {CPF_InstancedReference, TEXT("CPF_InstancedReference")},
        {CPF_SaveGame, TEXT("CPF_SaveGame")},
        {CPF_NoClear, TEXT("CPF_NoClear")},
        {CPF_Virtual, TEXT("CPF_Virtual")},
        {CPF_ReferenceParm, TEXT("CPF_ReferenceParm")},
        {CPF_BlueprintAssignable, TEXT("CPF_BlueprintAssignable")},
        {CPF_Deprecated, TEXT("CPF_Deprecated")},
        {CPF_RepSkip, TEXT("CPF_RepSkip")},
        {CPF_RepNotify, TEXT("CPF_RepNotify")},
        {CPF_Interp, TEXT("CPF_Interp")},
        {CPF_NonTransactional, TEXT("CPF_NonTransactional")},
        {CPF_EditorOnly, TEXT("CPF_EditorOnly")},
        {CPF_AutoWeak, TEXT("CPF_AutoWeak")},
        {CPF_ContainsInstancedReference, TEXT("CPF_ContainsInstancedReference")},
        {CPF_AssetRegistrySearchable, TEXT("CPF_AssetRegistrySearchable")},
        {CPF_SimpleDisplay, TEXT("CPF_SimpleDisplay")},
        {CPF_AdvancedDisplay, TEXT("CPF_AdvancedDisplay")},
        {CPF_Protected, TEXT("CPF_Protected")},
        {CPF_BlueprintCallable, TEXT("CPF_BlueprintCallable")},
        {CPF_BlueprintAuthorityOnly, TEXT("CPF_BlueprintAuthorityOnly")},
        {CPF_TextExportTransient, TEXT("CPF_TextExportTransient")},
        {CPF_NonPIEDuplicateTransient, TEXT("CPF_NonPIEDuplicateTransient")},
        {CPF_ExposeOnSpawn, TEXT("CPF_ExposeOnSpawn")},
        {CPF_PersistentInstance, TEXT("CPF_PersistentInstance")},
        {CPF_UObjectWrapper, TEXT("CPF_UObjectWrapper")},
        {CPF_HasGetValueTypeHash, TEXT("CPF_HasGetValueTypeHash")},
        {CPF_NativeAccessSpecifierPublic, TEXT("CPF_NativeAccessSpecifierPublic")},
        {CPF_NativeAccessSpecifierProtected, TEXT("CPF_NativeAccessSpecifierProtected")},
        {CPF_NativeAccessSpecifierPrivate, TEXT("CPF_NativeAccessSpecifierPrivate")},
        {CPF_SkipSerialization, TEXT("CPF_SkipSerialization")},
        {CPF_TObjectPtr, TEXT("CPF_TObjectPtr")},
        {CPF_AllowSelfReference, TEXT("CPF_AllowSelfReference")},
    };
    if (Flags == 0)
    {
        return TEXT("0");
    }
    TArray<FString> Names;
    uint64 Remaining = Flags;
    for (const FNativeNamedPropertyFlag& Flag : Known)
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

TSharedPtr<FJsonObject> MakeMutationResult(
    const TSharedPtr<FJsonObject>& Object,
    const TArray<TSharedPtr<FJsonObject>>& InDiagnostics,
    const bool bDryRun,
    const bool bValid,
    const bool bApplied,
    const FString& AssetPath,
    const FString& Operation,
    const TSharedPtr<FJsonObject>& Planned,
    const TSharedPtr<FJsonObject>& ResolvedRefs,
    const TSharedPtr<FJsonObject>& Diff)
{
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    Diagnostics.Reserve(InDiagnostics.Num());
    for (const TSharedPtr<FJsonObject>& Diagnostic : InDiagnostics)
    {
        if (Diagnostic.IsValid())
        {
            Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
        }
    }
    return LoomleMutation::BuildMutationResult(
        Object,
        Diagnostics,
        bDryRun,
        bValid,
        bApplied,
        AssetPath,
        Operation,
        Planned,
        ResolvedRefs,
        Diff);
}

}
