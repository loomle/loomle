// Copyright 2026 Loomle contributors.

// Blueprint-domain tool adapters.
namespace
{
TSharedPtr<FJsonObject> BuildBlueprintNodeEditCapabilities(const TSharedPtr<FJsonObject>& Node);

TSharedPtr<FJsonObject> MakeBlueprintGraphAssetRef(const FString& AssetPath, const FString& GraphName, const FString& GraphId = FString())
{
    TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("asset"));
    Ref->SetStringField(TEXT("assetPath"), AssetPath);
    if (!GraphId.IsEmpty())
    {
        Ref->SetStringField(TEXT("graphId"), GraphId);
        Ref->SetStringField(TEXT("id"), GraphId);
    }
    if (!GraphName.IsEmpty())
    {
        Ref->SetStringField(TEXT("graphName"), GraphName);
    }
    return Ref;
}

TSharedPtr<FJsonObject> MakeBlueprintInlineGraphRef(const FString& NodeGuid, const FString& AssetPath)
{
    TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("inline"));
    Ref->SetStringField(TEXT("nodeGuid"), NodeGuid);
    Ref->SetStringField(TEXT("assetPath"), AssetPath);
    return Ref;
}

FString DescribeBlueprintCustomEventReplication(const uint32 FunctionFlags)
{
    const uint32 NetFlags = FunctionFlags & (FUNC_NetMulticast | FUNC_NetServer | FUNC_NetClient);
    if ((NetFlags & FUNC_NetMulticast) != 0)
    {
        return TEXT("netMulticast");
    }
    if ((NetFlags & FUNC_NetServer) != 0)
    {
        return TEXT("server");
    }
    if ((NetFlags & FUNC_NetClient) != 0)
    {
        return TEXT("owningClient");
    }
    return TEXT("none");
}

FString DescribeBlueprintReplicationCondition(const ELifetimeCondition Condition)
{
    if (const UEnum* ConditionEnum = StaticEnum<ELifetimeCondition>())
    {
        const FString Name = ConditionEnum->GetNameStringByValue(static_cast<int64>(Condition));
        if (!Name.IsEmpty())
        {
            return Name;
        }
    }

    return FString::Printf(TEXT("%d"), static_cast<int32>(Condition));
}

FString NormalizeBlueprintGraphKindForDomain(FString GraphKind)
{
    if (GraphKind.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
    {
        return TEXT("root");
    }
    if (GraphKind.Equals(TEXT("Function"), ESearchCase::IgnoreCase))
    {
        return TEXT("function");
    }
    if (GraphKind.Equals(TEXT("Macro"), ESearchCase::IgnoreCase))
    {
        return TEXT("macro");
    }
    if (GraphKind.Equals(TEXT("Interface"), ESearchCase::IgnoreCase))
    {
        return TEXT("function");
    }
    return GraphKind;
}

UEdGraphNode* ResolveBlueprintGraphNodeByToken(UEdGraph* Graph, const FString& NodeToken);

bool TryReadBlueprintJsonUInt64Field(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, uint64& Out)
{
    Out = 0;
    if (!Object.IsValid())
    {
        return false;
    }

    double Number = 0.0;
    if (Object->TryGetNumberField(FieldName, Number))
    {
        if (Number < 0.0)
        {
            return false;
        }
        Out = static_cast<uint64>(Number);
        return true;
    }

    FString Text;
    if (Object->TryGetStringField(FieldName, Text))
    {
        return LexTryParseString<uint64>(Out, *Text);
    }

    return false;
}

TArray<TSharedPtr<FJsonValue>> CloneBlueprintJsonArrayField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
{
    const TArray<TSharedPtr<FJsonValue>>* Field = nullptr;
    if (Object.IsValid() && Object->TryGetArrayField(FieldName, Field) && Field != nullptr)
    {
        return *Field;
    }
    return {};
}

FString SerializeBlueprintJsonObjectCondensed(const TSharedPtr<FJsonObject>& Object)
{
    if (!Object.IsValid())
    {
        return TEXT("{}");
    }

    FString Serialized;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Serialized);
    FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
    return Serialized;
}

void CopyBlueprintOptionalStringField(const TSharedPtr<FJsonObject>& Source, const TSharedPtr<FJsonObject>& Dest, const TCHAR* FieldName)
{
    FString Value;
    if (Source.IsValid() && Dest.IsValid() && Source->TryGetStringField(FieldName, Value))
    {
        Dest->SetStringField(FieldName, Value);
    }
}

void CopyBlueprintOptionalObjectField(const TSharedPtr<FJsonObject>& Source, const TSharedPtr<FJsonObject>& Dest, const TCHAR* FieldName)
{
    const TSharedPtr<FJsonObject>* Value = nullptr;
    if (Source.IsValid() && Dest.IsValid() && Source->TryGetObjectField(FieldName, Value) && Value != nullptr && (*Value).IsValid())
    {
        Dest->SetObjectField(FieldName, CloneJsonObject(*Value));
    }
}

FString BuildBlueprintDiagnosticIdentityKey(const TSharedPtr<FJsonObject>& Diagnostic)
{
    if (!Diagnostic.IsValid())
    {
        return TEXT("");
    }

    FString Code;
    FString Message;
    FString NodeId;
    FString SourceKind;
    double Seq = 0.0;
    Diagnostic->TryGetStringField(TEXT("code"), Code);
    Diagnostic->TryGetStringField(TEXT("message"), Message);
    Diagnostic->TryGetStringField(TEXT("nodeId"), NodeId);
    Diagnostic->TryGetStringField(TEXT("sourceKind"), SourceKind);
    Diagnostic->TryGetNumberField(TEXT("seq"), Seq);
    return FString::Printf(TEXT("%s|%s|%s|%s|%.0f"), *Code, *Message, *NodeId, *SourceKind, Seq);
}

void AppendUniqueBlueprintDiagnostic(
    TArray<TSharedPtr<FJsonValue>>& Diagnostics,
    TSet<FString>& SeenDiagnosticKeys,
    const TSharedPtr<FJsonObject>& Diagnostic)
{
    if (!Diagnostic.IsValid())
    {
        return;
    }

    const FString DiagnosticKey = BuildBlueprintDiagnosticIdentityKey(Diagnostic);
    if (!DiagnosticKey.IsEmpty() && SeenDiagnosticKeys.Contains(DiagnosticKey))
    {
        return;
    }

    if (!DiagnosticKey.IsEmpty())
    {
        SeenDiagnosticKeys.Add(DiagnosticKey);
    }

    Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
}

TSet<FString> BuildBlueprintDiagnosticIdentitySet(const TArray<TSharedPtr<FJsonValue>>& Diagnostics)
{
    TSet<FString> SeenDiagnosticKeys;
    for (const TSharedPtr<FJsonValue>& DiagnosticValue : Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        if (!DiagnosticValue.IsValid() || !DiagnosticValue->TryGetObject(Diagnostic) || Diagnostic == nullptr || !(*Diagnostic).IsValid())
        {
            continue;
        }

        const FString DiagnosticKey = BuildBlueprintDiagnosticIdentityKey(*Diagnostic);
        if (!DiagnosticKey.IsEmpty())
        {
            SeenDiagnosticKeys.Add(DiagnosticKey);
        }
    }

    return SeenDiagnosticKeys;
}

FString DetermineBlueprintVerifyStatus(const TArray<TSharedPtr<FJsonValue>>& Diagnostics)
{
    bool bHasWarning = false;
    for (const TSharedPtr<FJsonValue>& DiagnosticValue : Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        if (!DiagnosticValue.IsValid() || !DiagnosticValue->TryGetObject(Diagnostic) || Diagnostic == nullptr || !(*Diagnostic).IsValid())
        {
            continue;
        }

        FString Severity;
        if ((*Diagnostic)->TryGetStringField(TEXT("severity"), Severity))
        {
            Severity = Severity.ToLower();
            if (Severity.Equals(TEXT("error")))
            {
                return TEXT("error");
            }
            if (Severity.Equals(TEXT("warning")))
            {
                bHasWarning = true;
            }
        }
    }

    return bHasWarning ? TEXT("warn") : TEXT("ok");
}

TSharedPtr<FJsonObject> MakeBlueprintRecentDiagnosticEvent(const TSharedPtr<FJsonObject>& Event)
{
    if (!Event.IsValid())
    {
        return nullptr;
    }

    FString Message;
    if (!Event->TryGetStringField(TEXT("message"), Message) || Message.IsEmpty())
    {
        return nullptr;
    }

    FString Severity = TEXT("error");
    Event->TryGetStringField(TEXT("severity"), Severity);

    FString Timestamp;
    Event->TryGetStringField(TEXT("ts"), Timestamp);

    TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
    Diagnostic->SetStringField(TEXT("code"), TEXT("RECENT_DIAGNOSTIC_EVENT"));
    Diagnostic->SetStringField(TEXT("severity"), Severity.IsEmpty() ? TEXT("error") : Severity.ToLower());
    Diagnostic->SetStringField(TEXT("message"), Message);
    Diagnostic->SetStringField(TEXT("sourceKind"), TEXT("diagnostic"));
    FString Source;
    if (Event->TryGetStringField(TEXT("source"), Source) && !Source.IsEmpty())
    {
        Diagnostic->SetStringField(TEXT("source"), Source);
    }
    if (!Timestamp.IsEmpty())
    {
        Diagnostic->SetStringField(TEXT("timestamp"), Timestamp);
    }

    FString AssetPath;
    if (!Event->TryGetStringField(TEXT("assetPath"), AssetPath))
    {
        const TSharedPtr<FJsonObject>* Context = nullptr;
        if (Event->TryGetObjectField(TEXT("context"), Context) && Context != nullptr && (*Context).IsValid())
        {
            (*Context)->TryGetStringField(TEXT("assetPath"), AssetPath);
        }
    }
    if (!AssetPath.IsEmpty())
    {
        Diagnostic->SetStringField(TEXT("assetPath"), AssetPath);
    }

    return Diagnostic;
}

bool ShouldDescribeBlueprintProperty(const FProperty* Property)
{
    if (Property == nullptr)
    {
        return false;
    }

    if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_Parm))
    {
        return false;
    }

    return Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible);
}

TSharedPtr<FJsonObject> MakeBlueprintDescribePropertyType(const FProperty* Property)
{
    TSharedPtr<FJsonObject> TypeObject = MakeShared<FJsonObject>();
    if (Property == nullptr)
    {
        return TypeObject;
    }

    TypeObject->SetStringField(TEXT("cppType"), Property->GetCPPType());
    TypeObject->SetStringField(TEXT("propertyClass"), Property->GetClass() ? Property->GetClass()->GetName() : TEXT(""));

    FString ValueKind = TEXT("unknown");
    FString ObjectClassPath;

    if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        ValueKind = TEXT("struct");
        ObjectClassPath = StructProperty->Struct ? StructProperty->Struct->GetPathName() : TEXT("");
    }
    else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        ValueKind = TEXT("object");
        ObjectClassPath = ObjectProperty->PropertyClass ? ObjectProperty->PropertyClass->GetPathName() : TEXT("");
    }
    else if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
    {
        ValueKind = TEXT("class");
        ObjectClassPath = ClassProperty->MetaClass ? ClassProperty->MetaClass->GetPathName() : TEXT("");
    }
    else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
    {
        ValueKind = TEXT("enum");
        ObjectClassPath = EnumProperty->GetEnum() ? EnumProperty->GetEnum()->GetPathName() : TEXT("");
    }
    else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
    {
        if (ByteProperty->Enum != nullptr)
        {
            ValueKind = TEXT("enum");
            ObjectClassPath = ByteProperty->Enum->GetPathName();
        }
        else
        {
            ValueKind = TEXT("number");
        }
    }
    else if (Property->IsA<FArrayProperty>())
    {
        ValueKind = TEXT("array");
    }
    else if (Property->IsA<FSetProperty>())
    {
        ValueKind = TEXT("set");
    }
    else if (Property->IsA<FMapProperty>())
    {
        ValueKind = TEXT("map");
    }
    else if (Property->IsA<FBoolProperty>())
    {
        ValueKind = TEXT("bool");
    }
    else if (Property->IsA<FNumericProperty>())
    {
        ValueKind = TEXT("number");
    }
    else if (Property->IsA<FStrProperty>() || Property->IsA<FNameProperty>() || Property->IsA<FTextProperty>())
    {
        ValueKind = TEXT("string");
    }

    TypeObject->SetStringField(TEXT("kind"), ValueKind);
    if (!ObjectClassPath.IsEmpty())
    {
        TypeObject->SetStringField(TEXT("objectClassPath"), ObjectClassPath);
    }

    return TypeObject;
}

TSharedPtr<FJsonObject> MakeBlueprintDescribePropertyEntry(const FProperty* Property, UClass* OwnerClass)
{
    TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
    if (Property == nullptr)
    {
        return Entry;
    }

    Entry->SetStringField(TEXT("name"), Property->GetName());
    Entry->SetStringField(TEXT("displayName"), Property->GetDisplayNameText().ToString());
    Entry->SetStringField(TEXT("ownerClassPath"), OwnerClass ? OwnerClass->GetPathName() : TEXT(""));
    Entry->SetBoolField(TEXT("editable"), Property->HasAnyPropertyFlags(CPF_Edit));
    Entry->SetBoolField(TEXT("blueprintVisible"), Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
    const bool bReplicated = Property->HasAnyPropertyFlags(CPF_Net);
    const bool bRepNotify = Property->HasAnyPropertyFlags(CPF_RepNotify) || Property->RepNotifyFunc != NAME_None;
    Entry->SetBoolField(TEXT("isReplicated"), bReplicated);
    Entry->SetBoolField(TEXT("isRepNotify"), bRepNotify);
    Entry->SetStringField(TEXT("replication"), bRepNotify ? TEXT("repNotify") : (bReplicated ? TEXT("replicated") : TEXT("none")));
    Entry->SetStringField(TEXT("repNotifyFunc"), Property->RepNotifyFunc != NAME_None ? Property->RepNotifyFunc.ToString() : TEXT(""));
    Entry->SetStringField(TEXT("replicationCondition"), DescribeBlueprintReplicationCondition(Property->GetBlueprintReplicationCondition()));
    Entry->SetNumberField(TEXT("replicationConditionValue"), static_cast<int32>(Property->GetBlueprintReplicationCondition()));
    Entry->SetObjectField(TEXT("type"), MakeBlueprintDescribePropertyType(Property));

    const FString Category = Property->GetMetaData(TEXT("Category"));
    if (!Category.IsEmpty())
    {
        Entry->SetStringField(TEXT("category"), Category);
    }

    return Entry;
}

FString BlueprintEnumValueToString(const UEnum* Enum, int64 Value)
{
    if (Enum == nullptr)
    {
        return FString::Printf(TEXT("%lld"), Value);
    }

    FString Name = Enum->GetNameStringByValue(Value);
    if (Name.IsEmpty())
    {
        Name = Enum->GetNameByValue(Value).ToString();
    }
    return Name.IsEmpty() ? FString::Printf(TEXT("%lld"), Value) : Name;
}

bool TryParseBlueprintEnumValue(const UEnum* Enum, const FString& Text, int64& OutValue)
{
    OutValue = 0;
    if (Enum == nullptr)
    {
        return LexTryParseString<int64>(OutValue, *Text);
    }

    const int64 NamedValue = Enum->GetValueByNameString(Text, EGetByNameFlags::None);
    if (NamedValue != INDEX_NONE)
    {
        OutValue = NamedValue;
        return true;
    }

    return LexTryParseString<int64>(OutValue, *Text);
}

TSharedPtr<FJsonObject> MakeBlueprintClassSettingsObject(const UBlueprint* Blueprint)
{
    TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
    if (Blueprint == nullptr)
    {
        return Settings;
    }

    Settings->SetStringField(TEXT("blueprintType"), BlueprintEnumValueToString(StaticEnum<EBlueprintType>(), static_cast<int64>(Blueprint->BlueprintType.GetValue())));
    Settings->SetStringField(TEXT("status"), BlueprintEnumValueToString(StaticEnum<EBlueprintStatus>(), static_cast<int64>(Blueprint->Status.GetValue())));
    Settings->SetStringField(TEXT("compileMode"), BlueprintEnumValueToString(StaticEnum<EBlueprintCompileMode>(), static_cast<int64>(Blueprint->CompileMode)));
    Settings->SetStringField(TEXT("displayName"), Blueprint->BlueprintDisplayName);
    Settings->SetStringField(TEXT("description"), Blueprint->BlueprintDescription);
    Settings->SetStringField(TEXT("namespace"), Blueprint->BlueprintNamespace);
    Settings->SetStringField(TEXT("category"), Blueprint->BlueprintCategory);
    TArray<TSharedPtr<FJsonValue>> HideCategories;
    for (const FString& Category : Blueprint->HideCategories)
    {
        HideCategories.Add(MakeShared<FJsonValueString>(Category));
    }
    Settings->SetArrayField(TEXT("hideCategories"), HideCategories);
    Settings->SetBoolField(TEXT("runConstructionScriptOnDrag"), Blueprint->bRunConstructionScriptOnDrag);
    Settings->SetBoolField(TEXT("runConstructionScriptInSequencer"), Blueprint->bRunConstructionScriptInSequencer);
    Settings->SetBoolField(TEXT("generateConstClass"), Blueprint->bGenerateConstClass);
    Settings->SetBoolField(TEXT("generateAbstractClass"), Blueprint->bGenerateAbstractClass);
    Settings->SetBoolField(TEXT("deprecated"), Blueprint->bDeprecate);
    Settings->SetStringField(TEXT("shouldCookPropertyGuids"), BlueprintEnumValueToString(StaticEnum<EShouldCookBlueprintPropertyGuids>(), static_cast<int64>(Blueprint->ShouldCookPropertyGuidsValue)));
    return Settings;
}

TSharedPtr<FJsonObject> MakeBlueprintMetadataObject(const UObject* Object)
{
    TSharedPtr<FJsonObject> Metadata = MakeShared<FJsonObject>();
    if (Object == nullptr)
    {
        return Metadata;
    }

    if (const TMap<FName, FString>* Map = FMetaData::GetMapForObject(Object))
    {
        for (const TPair<FName, FString>& Pair : *Map)
        {
            Metadata->SetStringField(Pair.Key.ToString(), Pair.Value);
        }
    }
    return Metadata;
}

bool ShouldDescribeBlueprintClassDefaultProperty(const FProperty* Property)
{
    if (Property == nullptr)
    {
        return false;
    }

    if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_Parm))
    {
        return false;
    }

    if (!Property->HasAnyPropertyFlags(CPF_Edit))
    {
        return false;
    }

    if (Property->IsA<FMulticastDelegateProperty>() || Property->IsA<FDelegateProperty>())
    {
        return false;
    }

    return true;
}

bool CanSerializeBlueprintClassDefaultProperty(const FProperty* Property)
{
    if (Property == nullptr)
    {
        return false;
    }

    if (Property->IsA<FArrayProperty>() || Property->IsA<FSetProperty>() || Property->IsA<FMapProperty>())
    {
        return false;
    }

    if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        const UScriptStruct* Struct = StructProperty->Struct;
        return Struct == TBaseStructure<FVector>::Get()
            || Struct == TBaseStructure<FRotator>::Get()
            || Struct == TBaseStructure<FTransform>::Get()
            || Struct == TBaseStructure<FLinearColor>::Get()
            || Struct == TBaseStructure<FColor>::Get();
    }

    return Property->IsA<FBoolProperty>()
        || Property->IsA<FNumericProperty>()
        || Property->IsA<FNameProperty>()
        || Property->IsA<FStrProperty>()
        || Property->IsA<FTextProperty>()
        || Property->IsA<FEnumProperty>()
        || Property->IsA<FByteProperty>()
        || Property->IsA<FObjectPropertyBase>()
        || Property->IsA<FClassProperty>();
}

bool TryBlueprintJsonValueToImportText(const TSharedPtr<FJsonValue>& Value, FString& OutText, FString& OutError)
{
    if (!Value.IsValid() || Value->Type == EJson::Null || Value->Type == EJson::None)
    {
        OutError = TEXT("Class default value must not be null.");
        return false;
    }

    switch (Value->Type)
    {
    case EJson::String:
        OutText = Value->AsString();
        return true;
    case EJson::Number:
        OutText = FString::SanitizeFloat(Value->AsNumber());
        return true;
    case EJson::Boolean:
        OutText = Value->AsBool() ? TEXT("true") : TEXT("false");
        return true;
    default:
        OutError = TEXT("Class default value must be a string, number, or boolean.");
        return false;
    }
}

bool TryReadBlueprintClassDefaultValue(const FProperty* Property, const UObject* Container, FString& OutValue)
{
    OutValue.Reset();
    if (Property == nullptr || Container == nullptr)
    {
        return false;
    }

    return FBlueprintEditorUtils::PropertyValueToString(
        Property,
        reinterpret_cast<const uint8*>(Container),
        OutValue,
        const_cast<UObject*>(Container));
}

bool TryImportBlueprintClassDefaultValue(
    const FProperty* Property,
    const FString& ImportText,
    uint8* DirectValue,
    UObject* OwningObject,
    FString& OutError)
{
    if (Property == nullptr || DirectValue == nullptr)
    {
        OutError = TEXT("Class default property storage is unavailable.");
        return false;
    }

    if (!FBlueprintEditorUtils::PropertyValueFromString_Direct(Property, ImportText, DirectValue, OwningObject, PPF_InstanceSubobjects))
    {
        OutError = FString::Printf(TEXT("Invalid class default value for '%s': %s"), *Property->GetName(), *ImportText);
        return false;
    }

    return true;
}

bool ValidateBlueprintClassDefaultImportText(const FProperty* Property, const FString& ImportText, const UObject* SourceContainer, FString& OutError)
{
    if (Property == nullptr || SourceContainer == nullptr)
    {
        OutError = TEXT("Class default property source is unavailable.");
        return false;
    }

    uint8* TempValue = static_cast<uint8*>(FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment()));
    if (TempValue == nullptr)
    {
        OutError = TEXT("Failed to allocate temporary class default storage.");
        return false;
    }

    Property->InitializeValue(TempValue);
    const void* SourceValue = Property->ContainerPtrToValuePtr<void>(SourceContainer);
    if (SourceValue != nullptr)
    {
        Property->CopyCompleteValue(TempValue, SourceValue);
    }

    const bool bOk = TryImportBlueprintClassDefaultValue(Property, ImportText, TempValue, const_cast<UObject*>(SourceContainer), OutError);
    Property->DestroyValue(TempValue);
    FMemory::Free(TempValue);
    return bOk;
}

TSharedPtr<FJsonObject> MakeBlueprintClassDefaultMutationObject(
    const FProperty* Property,
    UClass* BlueprintClass,
    UObject* CDO,
    UObject* ParentCDO)
{
    TSharedPtr<FJsonObject> Entry = MakeBlueprintDescribePropertyEntry(
        Property,
        const_cast<UClass*>(Property ? Property->GetOwnerClass() : nullptr));
    if (Property == nullptr)
    {
        return Entry;
    }

    FString Value;
    if (TryReadBlueprintClassDefaultValue(Property, CDO, Value))
    {
        Entry->SetStringField(TEXT("value"), Value);
    }
    else
    {
        Entry->SetField(TEXT("value"), MakeShared<FJsonValueNull>());
    }

    UClass* ParentClass = BlueprintClass ? BlueprintClass->GetSuperClass() : nullptr;
    const UClass* OwnerClass = Property->GetOwnerClass();
    const bool bInheritedProperty = OwnerClass != nullptr && ParentClass != nullptr && ParentClass->IsChildOf(OwnerClass);
    if (bInheritedProperty && ParentCDO != nullptr)
    {
        FString InheritedValue;
        if (TryReadBlueprintClassDefaultValue(Property, ParentCDO, InheritedValue))
        {
            Entry->SetStringField(TEXT("inheritedValue"), InheritedValue);
        }
        else
        {
            Entry->SetField(TEXT("inheritedValue"), MakeShared<FJsonValueNull>());
        }
    }
    else
    {
        Entry->SetField(TEXT("inheritedValue"), MakeShared<FJsonValueNull>());
    }

    const bool bOverridden = !bInheritedProperty || ParentCDO == nullptr || !Property->Identical_InContainer(CDO, ParentCDO);
    Entry->SetBoolField(TEXT("overridden"), bOverridden);
    return Entry;
}

TSharedPtr<FJsonObject> MakeBlueprintClassDefaultsObject(UClass* BlueprintClass)
{
    TSharedPtr<FJsonObject> Defaults = MakeShared<FJsonObject>();
    Defaults->SetStringField(TEXT("source"), TEXT("generatedClassCDO"));
    Defaults->SetStringField(TEXT("comparison"), TEXT("parentClassCDO"));

    TArray<TSharedPtr<FJsonValue>> Properties;
    int32 OmittedCount = 0;

    UObject* CDO = BlueprintClass ? BlueprintClass->GetDefaultObject(false) : nullptr;
    UClass* ParentClass = BlueprintClass ? BlueprintClass->GetSuperClass() : nullptr;
    UObject* ParentCDO = ParentClass ? ParentClass->GetDefaultObject(false) : nullptr;

    Defaults->SetStringField(TEXT("objectPath"), CDO ? CDO->GetPathName() : TEXT(""));
    Defaults->SetStringField(TEXT("parentObjectPath"), ParentCDO ? ParentCDO->GetPathName() : TEXT(""));

    if (CDO != nullptr)
    {
        for (TFieldIterator<FProperty> It(BlueprintClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            FProperty* Property = *It;
            if (!ShouldDescribeBlueprintClassDefaultProperty(Property))
            {
                continue;
            }

            const UClass* OwnerClass = Property->GetOwnerClass();
            const bool bInheritedProperty = OwnerClass != nullptr && ParentClass != nullptr && ParentClass->IsChildOf(OwnerClass);
            const bool bOverridden = !bInheritedProperty || ParentCDO == nullptr || !Property->Identical_InContainer(CDO, ParentCDO);
            if (!bOverridden)
            {
                continue;
            }

            if (!CanSerializeBlueprintClassDefaultProperty(Property))
            {
                ++OmittedCount;
                continue;
            }

            FString Value;
            if (!FBlueprintEditorUtils::PropertyValueToString(Property, reinterpret_cast<const uint8*>(CDO), Value, CDO))
            {
                ++OmittedCount;
                continue;
            }

            TSharedPtr<FJsonObject> Entry = MakeBlueprintDescribePropertyEntry(Property, const_cast<UClass*>(OwnerClass));
            Entry->SetStringField(TEXT("value"), Value);
            Entry->SetBoolField(TEXT("overridden"), true);

            if (bInheritedProperty && ParentCDO != nullptr)
            {
                FString InheritedValue;
                if (FBlueprintEditorUtils::PropertyValueToString(Property, reinterpret_cast<const uint8*>(ParentCDO), InheritedValue, ParentCDO))
                {
                    Entry->SetStringField(TEXT("inheritedValue"), InheritedValue);
                }
                else
                {
                    Entry->SetField(TEXT("inheritedValue"), MakeShared<FJsonValueNull>());
                }
            }
            else
            {
                Entry->SetField(TEXT("inheritedValue"), MakeShared<FJsonValueNull>());
            }

            Properties.Add(MakeShared<FJsonValueObject>(Entry));
        }
    }

    Defaults->SetArrayField(TEXT("properties"), Properties);
    Defaults->SetNumberField(TEXT("propertyCount"), Properties.Num());
    Defaults->SetNumberField(TEXT("omittedCount"), OmittedCount);
    return Defaults;
}

TSharedPtr<FJsonObject> MakeBlueprintDescribePinEntry(const UEdGraphPin* Pin)
{
    TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
    if (Pin == nullptr)
    {
        return Entry;
    }

    Entry->SetStringField(TEXT("name"), Pin->PinName.ToString());
    Entry->SetStringField(
        TEXT("direction"),
        Pin->Direction == EGPD_Input ? TEXT("input") : (Pin->Direction == EGPD_Output ? TEXT("output") : TEXT("unknown")));
    Entry->SetStringField(TEXT("cppType"), UEdGraphSchema_K2::TypeToText(Pin->PinType).ToString());

    TSharedPtr<FJsonObject> TypeObject = MakeShared<FJsonObject>();
    TypeObject->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
    TypeObject->SetStringField(TEXT("subCategory"), Pin->PinType.PinSubCategory.ToString());
    TypeObject->SetStringField(TEXT("object"), Pin->PinType.PinSubCategoryObject.IsValid() ? Pin->PinType.PinSubCategoryObject->GetPathName() : TEXT(""));
    Entry->SetObjectField(TEXT("type"), TypeObject);

    TSharedPtr<FJsonObject> DefaultObject = MakeShared<FJsonObject>();
    DefaultObject->SetStringField(TEXT("value"), Pin->DefaultValue);
    DefaultObject->SetStringField(TEXT("object"), Pin->DefaultObject ? Pin->DefaultObject->GetPathName() : TEXT(""));
    DefaultObject->SetStringField(TEXT("text"), Pin->DefaultTextValue.ToString());
    Entry->SetObjectField(TEXT("default"), DefaultObject);

    return Entry;
}

void AppendBlueprintDescribeGraphPins(const UEdGraph* Graph, const FString& EntryNodeClassName, TArray<TSharedPtr<FJsonValue>>& OutPins)
{
    if (Graph == nullptr)
    {
        return;
    }

    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node == nullptr || Node->GetClass() == nullptr || !Node->GetClass()->GetName().Contains(EntryNodeClassName))
        {
            continue;
        }

        for (const UEdGraphPin* Pin : Node->Pins)
        {
            OutPins.Add(MakeShared<FJsonValueObject>(MakeBlueprintDescribePinEntry(Pin)));
        }
        return;
    }
}

TSharedPtr<FJsonObject> BuildBlueprintClassDescribeResult(const FString& AssetPath)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
    if (Blueprint == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
        Result->SetStringField(TEXT("message"), TEXT("Blueprint asset not found."));
        return Result;
    }

    UClass* BlueprintClass = Blueprint->GeneratedClass ? Blueprint->GeneratedClass : Blueprint->SkeletonGeneratedClass;
    if (BlueprintClass == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), TEXT("Blueprint class is not available."));
        return Result;
    }

    Result->SetBoolField(TEXT("isError"), false);
    Result->SetStringField(TEXT("mode"), TEXT("class"));
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("blueprintClass"), BlueprintClass->GetPathName());
    Result->SetStringField(TEXT("parentClass"), BlueprintClass->GetSuperClass() ? BlueprintClass->GetSuperClass()->GetPathName() : TEXT(""));
    Result->SetStringField(TEXT("parentClassPath"), Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : TEXT(""));

    TSharedPtr<FJsonObject> ClassObject = MakeShared<FJsonObject>();
    ClassObject->SetStringField(TEXT("generatedClassPath"), Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetPathName() : TEXT(""));
    ClassObject->SetStringField(TEXT("skeletonClassPath"), Blueprint->SkeletonGeneratedClass ? Blueprint->SkeletonGeneratedClass->GetPathName() : TEXT(""));
    TSharedPtr<FJsonObject> ClassFlags = MakeShared<FJsonObject>();
    ClassFlags->SetBoolField(TEXT("abstract"), BlueprintClass->HasAnyClassFlags(CLASS_Abstract));
    ClassFlags->SetBoolField(TEXT("const"), BlueprintClass->HasAnyClassFlags(CLASS_Const));
    ClassFlags->SetBoolField(TEXT("deprecated"), BlueprintClass->HasAnyClassFlags(CLASS_Deprecated));
    ClassFlags->SetBoolField(TEXT("native"), BlueprintClass->HasAnyClassFlags(CLASS_Native));
    ClassObject->SetObjectField(TEXT("classFlags"), ClassFlags);
    if (const UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(BlueprintClass))
    {
        ClassObject->SetNumberField(TEXT("numReplicatedProperties"), GeneratedClass->NumReplicatedProperties);
    }
    else
    {
        ClassObject->SetNumberField(TEXT("numReplicatedProperties"), 0);
    }
    Result->SetObjectField(TEXT("class"), ClassObject);

    Result->SetObjectField(TEXT("settings"), MakeBlueprintClassSettingsObject(Blueprint));

    Result->SetObjectField(TEXT("classDefaults"), MakeBlueprintClassDefaultsObject(BlueprintClass));

    TSharedPtr<FJsonObject> Metadata = MakeShared<FJsonObject>();
    Metadata->SetObjectField(TEXT("asset"), MakeBlueprintMetadataObject(Blueprint));
    Metadata->SetObjectField(TEXT("generatedClass"), MakeBlueprintMetadataObject(BlueprintClass));
    Metadata->SetObjectField(TEXT("parentClass"), MakeBlueprintMetadataObject(Blueprint->ParentClass));
    Result->SetObjectField(TEXT("metadata"), Metadata);

    TArray<TSharedPtr<FJsonValue>> ImplementedInterfaces;
    FString InterfacesJson;
    FString InterfacesError;
    if (FLoomleBlueprintAdapter::ListImplementedInterfaces(AssetPath, InterfacesJson, InterfacesError))
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InterfacesJson);
        FJsonSerializer::Deserialize(Reader, ImplementedInterfaces);
    }
    Result->SetArrayField(TEXT("implementedInterfaces"), ImplementedInterfaces);

    TArray<TSharedPtr<FJsonValue>> Variables;
    TSet<FString> SeenVariables;
    for (UClass* Class = BlueprintClass; Class != nullptr; Class = Class->GetSuperClass())
    {
        for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        {
            FProperty* Property = *It;
            if (!ShouldDescribeBlueprintProperty(Property) || SeenVariables.Contains(Property->GetName()))
            {
                continue;
            }

            SeenVariables.Add(Property->GetName());
            Variables.Add(MakeShared<FJsonValueObject>(MakeBlueprintDescribePropertyEntry(Property, Class)));
        }
    }
    Result->SetArrayField(TEXT("variables"), Variables);

    TArray<TSharedPtr<FJsonValue>> Functions;
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (Graph == nullptr)
        {
            continue;
        }

        TSharedPtr<FJsonObject> FunctionEntry = MakeShared<FJsonObject>();
        FunctionEntry->SetStringField(TEXT("name"), Graph->GetName());
        FunctionEntry->SetStringField(TEXT("displayName"), FName::NameToDisplayString(Graph->GetName(), false));
        FunctionEntry->SetStringField(TEXT("graphName"), Graph->GetName());
        FunctionEntry->SetStringField(TEXT("kind"), TEXT("function"));

        TArray<TSharedPtr<FJsonValue>> Pins;
        AppendBlueprintDescribeGraphPins(Graph, TEXT("K2Node_FunctionEntry"), Pins);
        FunctionEntry->SetArrayField(TEXT("pins"), Pins);
        Functions.Add(MakeShared<FJsonValueObject>(FunctionEntry));
    }
    Result->SetArrayField(TEXT("functions"), Functions);

    TArray<TSharedPtr<FJsonValue>> InterfaceFunctions;
    for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
    {
        for (UEdGraph* Graph : InterfaceDesc.Graphs)
        {
            if (Graph == nullptr)
            {
                continue;
            }

            TSharedPtr<FJsonObject> FunctionEntry = MakeShared<FJsonObject>();
            FunctionEntry->SetStringField(TEXT("name"), Graph->GetName());
            FunctionEntry->SetStringField(TEXT("displayName"), FName::NameToDisplayString(Graph->GetName(), false));
            FunctionEntry->SetStringField(TEXT("graphName"), Graph->GetName());
            FunctionEntry->SetStringField(TEXT("kind"), TEXT("interface"));
            if (InterfaceDesc.Interface != nullptr)
            {
                FunctionEntry->SetStringField(TEXT("interfaceClassPath"), InterfaceDesc.Interface->GetPathName());
            }

            TArray<TSharedPtr<FJsonValue>> Pins;
            AppendBlueprintDescribeGraphPins(Graph, TEXT("K2Node_FunctionEntry"), Pins);
            FunctionEntry->SetArrayField(TEXT("pins"), Pins);
            InterfaceFunctions.Add(MakeShared<FJsonValueObject>(FunctionEntry));
        }
    }
    Result->SetArrayField(TEXT("interfaceFunctions"), InterfaceFunctions);

    TArray<TSharedPtr<FJsonValue>> Macros;
    for (UEdGraph* Graph : Blueprint->MacroGraphs)
    {
        if (Graph == nullptr)
        {
            continue;
        }

        TSharedPtr<FJsonObject> MacroEntry = MakeShared<FJsonObject>();
        MacroEntry->SetStringField(TEXT("name"), Graph->GetName());
        MacroEntry->SetStringField(TEXT("displayName"), FName::NameToDisplayString(Graph->GetName(), false));
        MacroEntry->SetStringField(TEXT("graphName"), Graph->GetName());
        MacroEntry->SetStringField(TEXT("kind"), TEXT("macro"));

        TArray<TSharedPtr<FJsonValue>> Pins;
        AppendBlueprintDescribeGraphPins(Graph, TEXT("K2Node_Tunnel"), Pins);
        MacroEntry->SetArrayField(TEXT("pins"), Pins);
        Macros.Add(MakeShared<FJsonValueObject>(MacroEntry));
    }
    Result->SetArrayField(TEXT("macros"), Macros);

    TArray<TSharedPtr<FJsonValue>> Dispatchers;
    for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
    {
        if (Graph == nullptr)
        {
            continue;
        }

        TSharedPtr<FJsonObject> DispatcherEntry = MakeShared<FJsonObject>();
        DispatcherEntry->SetStringField(TEXT("name"), Graph->GetName());
        DispatcherEntry->SetStringField(TEXT("displayName"), FName::NameToDisplayString(Graph->GetName(), false));
        DispatcherEntry->SetStringField(TEXT("graphName"), Graph->GetName());
        DispatcherEntry->SetStringField(TEXT("kind"), TEXT("dispatcher"));

        TArray<TSharedPtr<FJsonValue>> Pins;
        AppendBlueprintDescribeGraphPins(Graph, TEXT("K2Node_FunctionEntry"), Pins);
        DispatcherEntry->SetArrayField(TEXT("pins"), Pins);
        Dispatchers.Add(MakeShared<FJsonValueObject>(DispatcherEntry));
    }
    Result->SetArrayField(TEXT("dispatchers"), Dispatchers);

    TArray<TSharedPtr<FJsonValue>> EventSignatures;
    for (UEdGraph* EventGraph : Blueprint->UbergraphPages)
    {
        if (EventGraph == nullptr)
        {
            continue;
        }

        for (const UEdGraphNode* Node : EventGraph->Nodes)
        {
            if (Node == nullptr || Node->GetClass() == nullptr)
            {
                continue;
            }

            const FString NodeClassName = Node->GetClass()->GetName();
            if (!NodeClassName.Contains(TEXT("K2Node_Event")) && !NodeClassName.Contains(TEXT("K2Node_CustomEvent")))
            {
                continue;
            }

            TSharedPtr<FJsonObject> EventEntry = MakeShared<FJsonObject>();
            EventEntry->SetStringField(TEXT("name"), Node->GetName());
            EventEntry->SetStringField(TEXT("displayName"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            EventEntry->SetStringField(TEXT("nodeClassPath"), Node->GetClass()->GetPathName());
            EventEntry->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
            EventEntry->SetStringField(TEXT("graphName"), EventGraph->GetName());
            EventEntry->SetStringField(TEXT("graphId"), EventGraph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
            EventEntry->SetStringField(TEXT("eventKind"), TEXT("engine"));
            if (const UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node))
            {
                EventEntry->SetStringField(TEXT("name"), CustomEvent->CustomFunctionName.ToString());
                EventEntry->SetStringField(TEXT("eventKind"), TEXT("custom"));
                EventEntry->SetBoolField(TEXT("isCustomEvent"), true);
                EventEntry->SetNumberField(TEXT("functionFlags"), static_cast<int32>(CustomEvent->FunctionFlags));
                EventEntry->SetBoolField(TEXT("isReplicated"), (CustomEvent->FunctionFlags & FUNC_Net) != 0);
                EventEntry->SetStringField(TEXT("replication"), DescribeBlueprintCustomEventReplication(CustomEvent->FunctionFlags));
                EventEntry->SetBoolField(TEXT("reliable"), (CustomEvent->FunctionFlags & FUNC_NetReliable) != 0);
                EventEntry->SetBoolField(TEXT("isOverride"), CustomEvent->bOverrideFunction);
                EventEntry->SetBoolField(TEXT("isEditable"), CustomEvent->IsEditable());
                EventEntry->SetBoolField(TEXT("callInEditor"), CustomEvent->bCallInEditor);
                EventEntry->SetBoolField(TEXT("deprecated"), CustomEvent->bIsDeprecated);
                EventEntry->SetStringField(TEXT("deprecationMessage"), CustomEvent->DeprecationMessage);
            }
            else
            {
                EventEntry->SetBoolField(TEXT("isCustomEvent"), false);
            }

            TArray<TSharedPtr<FJsonValue>> Pins;
            for (const UEdGraphPin* Pin : Node->Pins)
            {
                Pins.Add(MakeShared<FJsonValueObject>(MakeBlueprintDescribePinEntry(Pin)));
            }
            EventEntry->SetArrayField(TEXT("pins"), Pins);
            EventSignatures.Add(MakeShared<FJsonValueObject>(EventEntry));
        }
    }
    Result->SetArrayField(TEXT("eventSignatures"), EventSignatures);

    TArray<TSharedPtr<FJsonValue>> Components;
    if (Blueprint->SimpleConstructionScript != nullptr)
    {
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node == nullptr)
            {
                continue;
            }

            TSharedPtr<FJsonObject> ComponentEntry = MakeShared<FJsonObject>();
            ComponentEntry->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
            UBlueprintGeneratedClass* BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(BlueprintClass);
            UActorComponent* Template = BlueprintGeneratedClass ? Node->GetActualComponentTemplate(BlueprintGeneratedClass) : nullptr;
            ComponentEntry->SetStringField(TEXT("componentClassPath"), Template && Template->GetClass() ? Template->GetClass()->GetPathName() : TEXT(""));
            if (Template != nullptr)
            {
                ComponentEntry->SetStringField(TEXT("templatePath"), Template->GetPathName());
            }
            Components.Add(MakeShared<FJsonValueObject>(ComponentEntry));
        }
    }
    Result->SetArrayField(TEXT("components"), Components);

    return Result;
}

struct FBlueprintGraphQueryShapeOptions
{
    TArray<FString> NodeClasses;
    TSet<FString> NodeIds;
    FString Text;
    int32 Limit = 200;
    int32 Offset = 0;
    bool bLimitExplicit = false;
    bool bIncludeConnections = false;
    bool bCursorValid = true;
    FString CursorError;
};

bool ParseBlueprintGraphQueryCursor(const FString& Cursor, int32& OutOffset, FString& OutError)
{
    OutOffset = 0;
    OutError.Empty();

    const FString TrimmedCursor = Cursor.TrimStartAndEnd();
    if (TrimmedCursor.IsEmpty())
    {
        return true;
    }

    FString Prefix;
    FString OffsetText;
    if (!TrimmedCursor.Split(TEXT(":"), &Prefix, &OffsetText) || !Prefix.Equals(TEXT("offset")))
    {
        OutError = TEXT("cursor must use the format offset:<non-negative integer>.");
        return false;
    }

    OffsetText = OffsetText.TrimStartAndEnd();
    if (OffsetText.IsEmpty())
    {
        OutError = TEXT("cursor offset is missing.");
        return false;
    }

    for (TCHAR Character : OffsetText)
    {
        if (!FChar::IsDigit(Character))
        {
            OutError = TEXT("cursor offset must be a non-negative integer.");
            return false;
        }
    }

    const int64 ParsedOffset = FCString::Atoi64(*OffsetText);
    if (ParsedOffset < 0 || ParsedOffset > TNumericLimits<int32>::Max())
    {
        OutError = TEXT("cursor offset is out of range.");
        return false;
    }

    OutOffset = static_cast<int32>(ParsedOffset);
    return true;
}

FString BuildBlueprintGraphQueryCursor(const int32 Offset)
{
    return FString::Printf(TEXT("offset:%d"), FMath::Max(Offset, 0));
}

FBlueprintGraphQueryShapeOptions ParseBlueprintGraphQueryShapeOptions(const TSharedPtr<FJsonObject>& Arguments)
{
    FBlueprintGraphQueryShapeOptions Options;

    if (!Arguments.IsValid())
    {
        return Options;
    }

    double LimitNumber = 0.0;
    if (Arguments->TryGetNumberField(TEXT("limit"), LimitNumber))
    {
        Options.Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, 1000);
        Options.bLimitExplicit = true;
    }

    FString Cursor;
    if (Arguments->TryGetStringField(TEXT("cursor"), Cursor) && !Cursor.TrimStartAndEnd().IsEmpty())
    {
        Options.bCursorValid = ParseBlueprintGraphQueryCursor(Cursor, Options.Offset, Options.CursorError);
    }

    const TArray<TSharedPtr<FJsonValue>>* NodeIds = nullptr;
    if (Arguments->TryGetArrayField(TEXT("nodeIds"), NodeIds) && NodeIds != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& NodeIdValue : *NodeIds)
        {
            FString NodeId;
            if (NodeIdValue.IsValid() && NodeIdValue->TryGetString(NodeId) && !NodeId.IsEmpty())
            {
                Options.NodeIds.Add(NodeId);
            }
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* NodeClasses = nullptr;
    if (Arguments->TryGetArrayField(TEXT("nodeClasses"), NodeClasses) && NodeClasses != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& NodeClassValue : *NodeClasses)
        {
            FString NodeClass;
            if (NodeClassValue.IsValid() && NodeClassValue->TryGetString(NodeClass) && !NodeClass.IsEmpty())
            {
                Options.NodeClasses.Add(NodeClass);
            }
        }
    }

    FString Text;
    if (Arguments->TryGetStringField(TEXT("text"), Text))
    {
        Options.Text = Text.TrimStartAndEnd();
    }

    bool bIncludeConnections = false;
    if (Arguments->TryGetBoolField(TEXT("includeConnections"), bIncludeConnections))
    {
        Options.bIncludeConnections = bIncludeConnections;
    }

    return Options;
}

bool BlueprintQueryNodeMatchesShapeOptions(const TSharedPtr<FJsonObject>& NodeObject, const FBlueprintGraphQueryShapeOptions& Options)
{
    if (!NodeObject.IsValid())
    {
        return false;
    }

    FString NodeId;
    NodeObject->TryGetStringField(TEXT("id"), NodeId);
    FString NodeGuid;
    NodeObject->TryGetStringField(TEXT("guid"), NodeGuid);
    if (Options.NodeIds.Num() > 0 && !Options.NodeIds.Contains(NodeId) && !Options.NodeIds.Contains(NodeGuid))
    {
        return false;
    }

    FString NodeClassPath;
    NodeObject->TryGetStringField(TEXT("nodeClassPath"), NodeClassPath);
    if (NodeClassPath.IsEmpty())
    {
        NodeObject->TryGetStringField(TEXT("classPath"), NodeClassPath);
    }

    if (Options.NodeClasses.Num() > 0)
    {
        bool bClassMatched = false;
        for (const FString& FilterClass : Options.NodeClasses)
        {
            if (NodeClassPath.Equals(FilterClass))
            {
                bClassMatched = true;
                break;
            }
        }

        if (!bClassMatched)
        {
            return false;
        }
    }

    return true;
}

void PruneBlueprintQueryNodeLinks(const TSharedPtr<FJsonObject>& NodeObject, const TSet<FString>& AllowedNodeIds)
{
    if (!NodeObject.IsValid())
    {
        return;
    }

    const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
    if (!NodeObject->TryGetArrayField(TEXT("pins"), Pins) || Pins == nullptr)
    {
        return;
    }

    for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
    {
        const TSharedPtr<FJsonObject>* PinObject = nullptr;
        if (!PinValue.IsValid() || !PinValue->TryGetObject(PinObject) || PinObject == nullptr || !(*PinObject).IsValid())
        {
            continue;
        }

        auto FilterLinks = [&AllowedNodeIds](const TArray<TSharedPtr<FJsonValue>>* Links) -> TArray<TSharedPtr<FJsonValue>>
        {
            TArray<TSharedPtr<FJsonValue>> FilteredLinks;
            if (Links == nullptr)
            {
                return FilteredLinks;
            }

            for (const TSharedPtr<FJsonValue>& LinkValue : *Links)
            {
                const TSharedPtr<FJsonObject>* LinkObject = nullptr;
                if (!LinkValue.IsValid() || !LinkValue->TryGetObject(LinkObject) || LinkObject == nullptr || !(*LinkObject).IsValid())
                {
                    continue;
                }

                FString LinkedNodeId;
                (*LinkObject)->TryGetStringField(TEXT("toNodeId"), LinkedNodeId);
                if (LinkedNodeId.IsEmpty())
                {
                    (*LinkObject)->TryGetStringField(TEXT("nodeGuid"), LinkedNodeId);
                }

                if (AllowedNodeIds.Contains(LinkedNodeId))
                {
                    FilteredLinks.Add(LinkValue);
                }
            }

            return FilteredLinks;
        };

        const TArray<TSharedPtr<FJsonValue>>* Links = nullptr;
        if ((*PinObject)->TryGetArrayField(TEXT("links"), Links))
        {
            (*PinObject)->SetArrayField(TEXT("links"), FilterLinks(Links));
        }

        const TArray<TSharedPtr<FJsonValue>>* LinkedTo = nullptr;
        if ((*PinObject)->TryGetArrayField(TEXT("linkedTo"), LinkedTo))
        {
            (*PinObject)->SetArrayField(TEXT("linkedTo"), FilterLinks(LinkedTo));
        }
    }
}

FString BuildBlueprintQueryRevision(const TSharedPtr<FJsonObject>& Result, const FString& Signature)
{
    if (!Result.IsValid())
    {
        return TEXT("");
    }

    FString AssetPath;
    Result->TryGetStringField(TEXT("assetPath"), AssetPath);
    FString GraphName;
    Result->TryGetStringField(TEXT("graphName"), GraphName);
    return FString::Printf(TEXT("bp:%08x"), GetTypeHash(AssetPath + TEXT("|") + GraphName + TEXT("|") + Signature));
}

void SetBlueprintNodeChildGraphRef(const TSharedPtr<FJsonObject>& Node, const TSharedPtr<FJsonObject>& ChildGraphRef)
{
    if (!Node.IsValid() || !ChildGraphRef.IsValid())
    {
        return;
    }

    Node->SetObjectField(TEXT("childGraphRef"), ChildGraphRef);
}

void SetBlueprintNodeInlineChildGraphRef(const TSharedPtr<FJsonObject>& Node, const FString& OwnerNodeGuid, const FString& AssetPath)
{
    SetBlueprintNodeChildGraphRef(Node, MakeBlueprintInlineGraphRef(OwnerNodeGuid, AssetPath));
}

bool ResolveBlueprintInlineGraphName(const FString& AssetPath, const FString& NodeGuid, FString& OutGraphName, FString& OutError)
{
    FString NodesJson;
    OutGraphName.Empty();
    OutError.Empty();
    return FLoomleBlueprintAdapter::ListCompositeSubgraphNodes(AssetPath, NodeGuid, OutGraphName, NodesJson, OutError)
        && !OutGraphName.IsEmpty();
}

bool BuildBlueprintQueryAddress(
    const TSharedPtr<FJsonObject>& Arguments,
    FString& OutAssetPath,
    FString& OutGraphName,
    FString& OutInlineNodeGuid,
    const TSharedPtr<FJsonObject>& Result)
{
    OutAssetPath.Empty();
    OutGraphName.Empty();
    OutInlineNodeGuid.Empty();

    const TSharedPtr<FJsonObject>* GraphRefObj = nullptr;
    const bool bHasGraphRef = Arguments.IsValid()
        && Arguments->TryGetObjectField(TEXT("graphRef"), GraphRefObj)
        && GraphRefObj != nullptr
        && (*GraphRefObj).IsValid();

    FString ProvidedGraphName;
    const bool bHasGraphName = Arguments.IsValid()
        && Arguments->TryGetStringField(TEXT("graphName"), ProvidedGraphName)
        && !ProvidedGraphName.IsEmpty();

    if (bHasGraphRef && bHasGraphName)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("Supply either graphRef (Mode B) or graphName (Mode A), not both."));
        return false;
    }

    if (bHasGraphRef)
    {
        FString Kind;
        if (!(*GraphRefObj)->TryGetStringField(TEXT("kind"), Kind) || Kind.IsEmpty())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("GRAPH_REF_INVALID"));
            Result->SetStringField(TEXT("message"), TEXT("graphRef.kind is required."));
            return false;
        }

        if (!(*GraphRefObj)->TryGetStringField(TEXT("assetPath"), OutAssetPath) || OutAssetPath.IsEmpty())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("GRAPH_REF_INVALID"));
            Result->SetStringField(TEXT("message"), TEXT("graphRef.assetPath is required."));
            return false;
        }

        OutAssetPath = NormalizeAssetPath(OutAssetPath);
        Kind = Kind.ToLower();
        if (Kind.Equals(TEXT("asset")))
        {
            FString GraphId;
            if ((!(*GraphRefObj)->TryGetStringField(TEXT("graphId"), GraphId) || GraphId.IsEmpty())
                && (!(*GraphRefObj)->TryGetStringField(TEXT("id"), GraphId) || GraphId.IsEmpty()))
            {
                GraphId.Empty();
            }

            if (!GraphId.IsEmpty())
            {
                FString ResolveError;
                if (!FLoomleBlueprintAdapter::ResolveGraphNameById(OutAssetPath, GraphId, OutGraphName, ResolveError))
                {
                    Result->SetBoolField(TEXT("isError"), true);
                    Result->SetStringField(TEXT("code"), TEXT("GRAPH_NOT_FOUND"));
                    Result->SetStringField(TEXT("message"), ResolveError.IsEmpty() ? TEXT("Failed to resolve graphRef.graphId.") : ResolveError);
                    return false;
                }
                return true;
            }

            if (!(*GraphRefObj)->TryGetStringField(TEXT("graphName"), OutGraphName) || OutGraphName.IsEmpty())
            {
                OutGraphName = TEXT("EventGraph");
            }
            return true;
        }

        if (!Kind.Equals(TEXT("inline")))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("GRAPH_REF_INVALID"));
            Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Unsupported graphRef.kind: %s"), *Kind));
            return false;
        }

        if (!(*GraphRefObj)->TryGetStringField(TEXT("nodeGuid"), OutInlineNodeGuid) || OutInlineNodeGuid.IsEmpty())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("GRAPH_REF_INVALID"));
            Result->SetStringField(TEXT("message"), TEXT("graphRef.nodeGuid is required for kind=inline."));
            return false;
        }

        FString ResolveError;
        if (!ResolveBlueprintInlineGraphName(OutAssetPath, OutInlineNodeGuid, OutGraphName, ResolveError))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("GRAPH_NOT_FOUND"));
            Result->SetStringField(TEXT("message"), ResolveError.IsEmpty() ? TEXT("Failed to resolve inline graphRef.") : ResolveError);
            return false;
        }
        return true;
    }

    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), OutAssetPath) || OutAssetPath.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.assetPath is required (Mode A) or supply graphRef (Mode B)."));
        return false;
    }

    OutAssetPath = NormalizeAssetPath(OutAssetPath);
    if (!bHasGraphName)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.graphName is required (Mode A) or supply graphRef (Mode B)."));
        return false;
    }

    OutGraphName = ProvidedGraphName;
    return true;
}

TSharedPtr<FJsonObject> MakeBlueprintEffectiveGraphRef(const FString& AssetPath, const FString& GraphName, const FString& InlineNodeGuid)
{
    if (!InlineNodeGuid.IsEmpty())
    {
        return MakeBlueprintInlineGraphRef(InlineNodeGuid, AssetPath);
    }
    FString GraphId;
    FString Error;
    FLoomleBlueprintAdapter::ResolveGraphIdByName(AssetPath, GraphName, GraphId, Error);
    return MakeBlueprintGraphAssetRef(AssetPath, GraphName, GraphId);
}

}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildBlueprintGraphListToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    FString AssetPath;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.assetPath is required."));
        return Result;
    }

    AssetPath = NormalizeAssetPath(AssetPath);

    bool bIncludeCompositeSubgraphs = false;
    Arguments->TryGetBoolField(TEXT("includeCompositeSubgraphs"), bIncludeCompositeSubgraphs);

    FString GraphsJson;
    FString Error;
    if (!FLoomleBlueprintAdapter::ListBlueprintGraphs(AssetPath, GraphsJson, Error))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), Error.IsEmpty() ? TEXT("blueprint.graph.list failed") : Error);
        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> Graphs;
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(GraphsJson);
        FJsonSerializer::Deserialize(Reader, Graphs);
    }

    for (TSharedPtr<FJsonValue>& GraphValue : Graphs)
    {
        const TSharedPtr<FJsonObject>* GraphObj = nullptr;
        if (!GraphValue.IsValid() || !GraphValue->TryGetObject(GraphObj) || GraphObj == nullptr || !(*GraphObj).IsValid())
        {
            continue;
        }

        FString GraphName;
        (*GraphObj)->TryGetStringField(TEXT("graphName"), GraphName);
        FString GraphId;
        (*GraphObj)->TryGetStringField(TEXT("graphId"), GraphId);

        FString GraphKind;
        (*GraphObj)->TryGetStringField(TEXT("graphKind"), GraphKind);
        (*GraphObj)->SetStringField(TEXT("graphKind"), NormalizeBlueprintGraphKindForDomain(GraphKind));
        (*GraphObj)->SetObjectField(TEXT("graphRef"), MakeBlueprintGraphAssetRef(AssetPath, GraphName, GraphId));
        (*GraphObj)->SetField(TEXT("parentGraphRef"), MakeShared<FJsonValueNull>());
        (*GraphObj)->SetField(TEXT("ownerNodeId"), MakeShared<FJsonValueNull>());
        (*GraphObj)->SetStringField(TEXT("loadStatus"), TEXT("loaded"));
    }

    if (bIncludeCompositeSubgraphs)
    {
        FString SubgraphJson;
        FString SubgraphError;
        if (FLoomleBlueprintAdapter::ListCompositeSubgraphs(AssetPath, SubgraphJson, SubgraphError))
        {
            TArray<TSharedPtr<FJsonValue>> SubgraphEntries;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SubgraphJson);
            if (FJsonSerializer::Deserialize(Reader, SubgraphEntries))
            {
                for (const TSharedPtr<FJsonValue>& SubgraphValue : SubgraphEntries)
                {
                    const TSharedPtr<FJsonObject>* SubgraphObj = nullptr;
                    if (!SubgraphValue.IsValid()
                        || !SubgraphValue->TryGetObject(SubgraphObj)
                        || SubgraphObj == nullptr
                        || !(*SubgraphObj).IsValid())
                    {
                        continue;
                    }

                    FString ParentGraphName;
                    (*SubgraphObj)->TryGetStringField(TEXT("parentGraphName"), ParentGraphName);

                    FString OwnerNodeId;
                    (*SubgraphObj)->TryGetStringField(TEXT("ownerNodeId"), OwnerNodeId);
                    if (OwnerNodeId.IsEmpty())
                    {
                        continue;
                    }

                    TSharedPtr<FJsonObject> SubEntry = CloneJsonObject(*SubgraphObj);
                    if (!SubEntry.IsValid())
                    {
                        continue;
                    }

                    FString SubgraphKind;
                    SubEntry->TryGetStringField(TEXT("graphKind"), SubgraphKind);
                    SubEntry->SetStringField(TEXT("graphKind"), NormalizeBlueprintGraphKindForDomain(SubgraphKind));
                    SubEntry->SetObjectField(TEXT("graphRef"), MakeBlueprintInlineGraphRef(OwnerNodeId, AssetPath));
                    SubEntry->SetObjectField(TEXT("parentGraphRef"), MakeBlueprintGraphAssetRef(AssetPath, ParentGraphName));
                    SubEntry->SetStringField(TEXT("loadStatus"), TEXT("loaded"));
                    Graphs.Add(MakeShared<FJsonValueObject>(SubEntry));
                }
            }
        }
    }

    Result->SetStringField(TEXT("graphType"), TEXT("blueprint"));
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetArrayField(TEXT("graphs"), Graphs);
    Result->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildBlueprintEditToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    FString AssetPath;
    FString Operation;
    if (!Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath)
        || AssetPath.IsEmpty()
        || !Arguments->TryGetStringField(TEXT("operation"), Operation)
        || Operation.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("blueprint.class.edit requires assetPath and operation."));
        return Result;
    }

    AssetPath = NormalizeAssetPath(AssetPath);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("operation"), Operation);

    const TSharedPtr<FJsonObject>* ArgsObject = nullptr;
    const TSharedPtr<FJsonObject> EmptyArgs = MakeShared<FJsonObject>();
    const TSharedPtr<FJsonObject> EffectiveArgs =
        (Arguments->TryGetObjectField(TEXT("args"), ArgsObject) && ArgsObject != nullptr && (*ArgsObject).IsValid())
            ? *ArgsObject
            : EmptyArgs;

    bool bDryRun = false;
    Arguments->TryGetBoolField(TEXT("dryRun"), bDryRun);
    Result->SetBoolField(TEXT("dryRun"), bDryRun);

    if (Operation.Equals(TEXT("create"), ESearchCase::IgnoreCase))
    {
        FString ParentClassPath;
        EffectiveArgs->TryGetStringField(TEXT("parentClassPath"), ParentClassPath);
        if (ParentClassPath.IsEmpty())
        {
            ParentClassPath = TEXT("/Script/Engine.Actor");
        }

        if (bDryRun)
        {
            Result->SetBoolField(TEXT("applied"), false);
            Result->SetStringField(TEXT("parentClassPath"), ParentClassPath);
            return Result;
        }

        FString BlueprintObjectPath;
        FString Error;
        const bool bOk = FLoomleBlueprintAdapter::CreateBlueprint(AssetPath, ParentClassPath, BlueprintObjectPath, Error);
        if (!bOk)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
            Result->SetStringField(TEXT("message"), Error.IsEmpty() ? TEXT("blueprint.class.edit create failed") : Error);
            return Result;
        }

        Result->SetBoolField(TEXT("applied"), true);
        Result->SetStringField(TEXT("blueprintObjectPath"), BlueprintObjectPath);
        Result->SetStringField(TEXT("parentClassPath"), ParentClassPath);
        return Result;
    }

    if (Operation.Equals(TEXT("setParent"), ESearchCase::IgnoreCase))
    {
        FString ParentClassPath;
        EffectiveArgs->TryGetStringField(TEXT("parentClassPath"), ParentClassPath);
        if (ParentClassPath.IsEmpty())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), TEXT("blueprint.class.edit setParent requires args.parentClassPath."));
            return Result;
        }

        if (bDryRun)
        {
            Result->SetBoolField(TEXT("applied"), false);
            Result->SetStringField(TEXT("parentClassPath"), ParentClassPath);
            return Result;
        }

        FString Error;
        const bool bOk = FLoomleBlueprintAdapter::SetParentClass(AssetPath, ParentClassPath, Error);
        if (!bOk)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), Error.IsEmpty() ? TEXT("blueprint.class.edit setParent failed") : Error);
            return Result;
        }

        Result->SetBoolField(TEXT("applied"), true);
        Result->SetStringField(TEXT("parentClassPath"), ParentClassPath);
        return Result;
    }

    if (Operation.Equals(TEXT("setSettings"), ESearchCase::IgnoreCase))
    {
        const TSharedPtr<FJsonObject>* SettingsObjectPtr = nullptr;
        if (!EffectiveArgs->TryGetObjectField(TEXT("settings"), SettingsObjectPtr) || SettingsObjectPtr == nullptr || !SettingsObjectPtr->IsValid())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), TEXT("blueprint.class.edit setSettings requires args.settings."));
            return Result;
        }

        const TSharedPtr<FJsonObject> SettingsObject = *SettingsObjectPtr;
        if (SettingsObject->Values.Num() == 0)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), TEXT("blueprint.class.edit setSettings requires at least one setting."));
            return Result;
        }

        static const TSet<FString> WritableSettings = {
            TEXT("displayName"),
            TEXT("description"),
            TEXT("namespace"),
            TEXT("category"),
            TEXT("hideCategories"),
            TEXT("runConstructionScriptOnDrag"),
            TEXT("runConstructionScriptInSequencer"),
            TEXT("generateConstClass"),
            TEXT("generateAbstractClass"),
            TEXT("deprecated"),
            TEXT("shouldCookPropertyGuids"),
            TEXT("compileMode"),
        };

        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : SettingsObject->Values)
        {
            if (!WritableSettings.Contains(Pair.Key))
            {
                Result->SetBoolField(TEXT("isError"), true);
                Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
                Result->SetStringField(TEXT("message"), FString::Printf(TEXT("blueprint.class.edit setSettings does not support setting '%s'."), *Pair.Key));
                return Result;
            }
        }

        UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
        if (Blueprint == nullptr)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
            Result->SetStringField(TEXT("message"), TEXT("Blueprint asset not found."));
            return Result;
        }

        Result->SetObjectField(TEXT("previousSettings"), MakeBlueprintClassSettingsObject(Blueprint));

        FString StringValue;
        bool bBoolValue = false;
        TArray<FString> HideCategories;
        int64 EnumValue = 0;
        bool bStructural = false;

        if (SettingsObject->HasField(TEXT("displayName")) && !SettingsObject->TryGetStringField(TEXT("displayName"), StringValue))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), TEXT("blueprint.class.edit setSettings displayName must be a string."));
            return Result;
        }
        if (SettingsObject->HasField(TEXT("description")) && !SettingsObject->TryGetStringField(TEXT("description"), StringValue))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), TEXT("blueprint.class.edit setSettings description must be a string."));
            return Result;
        }
        if (SettingsObject->HasField(TEXT("namespace")) && !SettingsObject->TryGetStringField(TEXT("namespace"), StringValue))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), TEXT("blueprint.class.edit setSettings namespace must be a string."));
            return Result;
        }
        if (SettingsObject->HasField(TEXT("category")) && !SettingsObject->TryGetStringField(TEXT("category"), StringValue))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), TEXT("blueprint.class.edit setSettings category must be a string."));
            return Result;
        }
        if (SettingsObject->HasField(TEXT("hideCategories")) && !SettingsObject->TryGetStringArrayField(TEXT("hideCategories"), HideCategories))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), TEXT("blueprint.class.edit setSettings hideCategories must be an array of strings."));
            return Result;
        }
        for (const TCHAR* FieldName : {
                 TEXT("runConstructionScriptOnDrag"),
                 TEXT("runConstructionScriptInSequencer"),
                 TEXT("generateConstClass"),
                 TEXT("generateAbstractClass"),
                 TEXT("deprecated"),
             })
        {
            if (SettingsObject->HasField(FieldName) && !SettingsObject->TryGetBoolField(FieldName, bBoolValue))
            {
                Result->SetBoolField(TEXT("isError"), true);
                Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
                Result->SetStringField(TEXT("message"), FString::Printf(TEXT("blueprint.class.edit setSettings %s must be a boolean."), FieldName));
                return Result;
            }
        }
        if (SettingsObject->HasField(TEXT("compileMode")))
        {
            if (!SettingsObject->TryGetStringField(TEXT("compileMode"), StringValue)
                || !TryParseBlueprintEnumValue(StaticEnum<EBlueprintCompileMode>(), StringValue, EnumValue))
            {
                Result->SetBoolField(TEXT("isError"), true);
                Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
                Result->SetStringField(TEXT("message"), TEXT("blueprint.class.edit setSettings compileMode must be a valid EBlueprintCompileMode name."));
                return Result;
            }
        }
        if (SettingsObject->HasField(TEXT("shouldCookPropertyGuids")))
        {
            if (!SettingsObject->TryGetStringField(TEXT("shouldCookPropertyGuids"), StringValue)
                || !TryParseBlueprintEnumValue(StaticEnum<EShouldCookBlueprintPropertyGuids>(), StringValue, EnumValue))
            {
                Result->SetBoolField(TEXT("isError"), true);
                Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
                Result->SetStringField(TEXT("message"), TEXT("blueprint.class.edit setSettings shouldCookPropertyGuids must be a valid EShouldCookBlueprintPropertyGuids name."));
                return Result;
            }
        }
        if (SettingsObject->HasField(TEXT("deprecated"))
            && Blueprint->ParentClass != nullptr
            && Blueprint->ParentClass->HasAnyClassFlags(CLASS_Deprecated))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), TEXT("Cannot change deprecated setting because the parent class is deprecated."));
            return Result;
        }

        if (bDryRun)
        {
            Result->SetBoolField(TEXT("applied"), false);
            Result->SetObjectField(TEXT("settings"), MakeBlueprintClassSettingsObject(Blueprint));
            Result->SetObjectField(TEXT("requestedSettings"), SettingsObject);
            return Result;
        }

        Blueprint->Modify();

        if (SettingsObject->TryGetStringField(TEXT("displayName"), StringValue))
        {
            Blueprint->BlueprintDisplayName = StringValue;
        }
        if (SettingsObject->TryGetStringField(TEXT("description"), StringValue))
        {
            Blueprint->BlueprintDescription = StringValue;
        }
        if (SettingsObject->TryGetStringField(TEXT("namespace"), StringValue))
        {
            Blueprint->BlueprintNamespace = StringValue;
        }
        if (SettingsObject->TryGetStringField(TEXT("category"), StringValue))
        {
            Blueprint->BlueprintCategory = StringValue;
        }
        if (SettingsObject->TryGetStringArrayField(TEXT("hideCategories"), HideCategories))
        {
            Blueprint->HideCategories = HideCategories;
        }
        if (SettingsObject->TryGetBoolField(TEXT("runConstructionScriptOnDrag"), bBoolValue))
        {
            Blueprint->bRunConstructionScriptOnDrag = bBoolValue;
        }
        if (SettingsObject->TryGetBoolField(TEXT("runConstructionScriptInSequencer"), bBoolValue))
        {
            Blueprint->bRunConstructionScriptInSequencer = bBoolValue;
        }
        if (SettingsObject->TryGetBoolField(TEXT("generateConstClass"), bBoolValue))
        {
            Blueprint->bGenerateConstClass = bBoolValue;
            bStructural = true;
        }
        if (SettingsObject->TryGetBoolField(TEXT("generateAbstractClass"), bBoolValue))
        {
            Blueprint->bGenerateAbstractClass = bBoolValue;
            bStructural = true;
        }
        if (SettingsObject->TryGetBoolField(TEXT("deprecated"), bBoolValue))
        {
            Blueprint->bDeprecate = bBoolValue;
            bStructural = true;
        }
        if (SettingsObject->TryGetStringField(TEXT("compileMode"), StringValue)
            && TryParseBlueprintEnumValue(StaticEnum<EBlueprintCompileMode>(), StringValue, EnumValue))
        {
            Blueprint->CompileMode = static_cast<EBlueprintCompileMode>(EnumValue);
            bStructural = true;
        }
        if (SettingsObject->TryGetStringField(TEXT("shouldCookPropertyGuids"), StringValue)
            && TryParseBlueprintEnumValue(StaticEnum<EShouldCookBlueprintPropertyGuids>(), StringValue, EnumValue))
        {
            Blueprint->ShouldCookPropertyGuidsValue = static_cast<EShouldCookBlueprintPropertyGuids>(EnumValue);
            bStructural = true;
        }

        if (bStructural)
        {
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        }
        else
        {
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        }
        Blueprint->MarkPackageDirty();

        Result->SetBoolField(TEXT("applied"), true);
        Result->SetObjectField(TEXT("settings"), MakeBlueprintClassSettingsObject(Blueprint));
        Result->SetBoolField(TEXT("structural"), bStructural);
        return Result;
    }

    if (Operation.Equals(TEXT("setDefault"), ESearchCase::IgnoreCase))
    {
        FString PropertyName;
        EffectiveArgs->TryGetStringField(TEXT("property"), PropertyName);
        if (PropertyName.IsEmpty())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), TEXT("blueprint.class.edit setDefault requires args.property."));
            return Result;
        }

        TSharedPtr<FJsonValue> ValueJson = EffectiveArgs->TryGetField(TEXT("value"));
        FString ImportText;
        FString ValueError;
        if (!TryBlueprintJsonValueToImportText(ValueJson, ImportText, ValueError))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), ValueError);
            return Result;
        }

        UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
        if (Blueprint == nullptr)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
            Result->SetStringField(TEXT("message"), TEXT("Blueprint asset not found."));
            return Result;
        }

        UClass* BlueprintClass = Blueprint->GeneratedClass ? Blueprint->GeneratedClass : Blueprint->SkeletonGeneratedClass;
        if (BlueprintClass == nullptr)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
            Result->SetStringField(TEXT("message"), TEXT("Blueprint class is not available."));
            return Result;
        }

        UObject* CDO = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
        if (CDO == nullptr)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
            Result->SetStringField(TEXT("message"), TEXT("Blueprint generated class CDO is not available."));
            return Result;
        }

        FProperty* Property = BlueprintClass->FindPropertyByName(FName(*PropertyName));
        if (Property == nullptr)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("PROPERTY_NOT_FOUND"));
            Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Class default property not found: %s"), *PropertyName));
            return Result;
        }
        if (!ShouldDescribeBlueprintClassDefaultProperty(Property))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("PROPERTY_NOT_EDITABLE"));
            Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Class default property is not editable: %s"), *PropertyName));
            return Result;
        }
        if (!CanSerializeBlueprintClassDefaultProperty(Property))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_PROPERTY_TYPE"));
            Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Class default property type is not supported yet: %s"), *PropertyName));
            return Result;
        }

        UClass* ParentClass = BlueprintClass->GetSuperClass();
        UObject* ParentCDO = ParentClass ? ParentClass->GetDefaultObject(false) : nullptr;
        Result->SetStringField(TEXT("property"), Property->GetName());
        Result->SetStringField(TEXT("importText"), ImportText);
        Result->SetObjectField(TEXT("previousDefault"), MakeBlueprintClassDefaultMutationObject(Property, BlueprintClass, CDO, ParentCDO));

        FString ImportError;
        if (!ValidateBlueprintClassDefaultImportText(Property, ImportText, CDO, ImportError))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_DEFAULT_VALUE"));
            Result->SetStringField(TEXT("message"), ImportError);
            return Result;
        }

        if (bDryRun)
        {
            Result->SetBoolField(TEXT("applied"), false);
            Result->SetObjectField(TEXT("default"), MakeBlueprintClassDefaultMutationObject(Property, BlueprintClass, CDO, ParentCDO));
            return Result;
        }

        uint8* DirectValue = Property->ContainerPtrToValuePtr<uint8>(CDO);
        if (DirectValue == nullptr)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
            Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Class default property storage not found: %s"), *PropertyName));
            return Result;
        }

        Blueprint->Modify();
        CDO->Modify();

        if (!TryImportBlueprintClassDefaultValue(Property, ImportText, DirectValue, CDO, ImportError))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_DEFAULT_VALUE"));
            Result->SetStringField(TEXT("message"), ImportError);
            return Result;
        }

        FPropertyChangedEvent ChangedEvent(Property, EPropertyChangeType::ValueSet);
        CDO->PostEditChangeProperty(ChangedEvent);
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        Blueprint->MarkPackageDirty();

        Result->SetBoolField(TEXT("applied"), true);
        Result->SetObjectField(TEXT("default"), MakeBlueprintClassDefaultMutationObject(Property, BlueprintClass, CDO, ParentCDO));
        return Result;
    }

    if (Operation.Equals(TEXT("addInterface"), ESearchCase::IgnoreCase) || Operation.Equals(TEXT("removeInterface"), ESearchCase::IgnoreCase))
    {
        FString InterfaceClassPath;
        EffectiveArgs->TryGetStringField(TEXT("interfaceClassPath"), InterfaceClassPath);
        if (InterfaceClassPath.IsEmpty())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(
                TEXT("message"),
                Operation.Equals(TEXT("addInterface"), ESearchCase::IgnoreCase)
                    ? TEXT("blueprint.class.edit addInterface requires args.interfaceClassPath.")
                    : TEXT("blueprint.class.edit removeInterface requires args.interfaceClassPath."));
            return Result;
        }

        bool bPreserveFunctions = false;
        if (Operation.Equals(TEXT("removeInterface"), ESearchCase::IgnoreCase))
        {
            EffectiveArgs->TryGetBoolField(TEXT("preserveFunctions"), bPreserveFunctions);
            Result->SetBoolField(TEXT("preserveFunctions"), bPreserveFunctions);
        }

        if (bDryRun)
        {
            Result->SetBoolField(TEXT("applied"), false);
            Result->SetStringField(TEXT("interfaceClassPath"), InterfaceClassPath);
            return Result;
        }

        FString Error;
        bool bOk = false;
        if (Operation.Equals(TEXT("addInterface"), ESearchCase::IgnoreCase))
        {
            bOk = FLoomleBlueprintAdapter::AddInterface(AssetPath, InterfaceClassPath, Error);
        }
        else
        {
            bOk = FLoomleBlueprintAdapter::RemoveInterface(AssetPath, InterfaceClassPath, bPreserveFunctions, Error);
        }

        if (!bOk)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), Error.IsEmpty() ? TEXT("blueprint.class.edit interface operation failed") : Error);
            return Result;
        }

        Result->SetBoolField(TEXT("applied"), true);
        Result->SetStringField(TEXT("interfaceClassPath"), InterfaceClassPath);
        return Result;
    }

    Result->SetBoolField(TEXT("isError"), true);
    Result->SetStringField(TEXT("code"), TEXT("NOT_IMPLEMENTED"));
    Result->SetStringField(
        TEXT("message"),
        FString::Printf(TEXT("blueprint.class.edit supports setParent, setSettings, setDefault, addInterface, and removeInterface; got: %s"), *Operation));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildBlueprintEnumInspectToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    FString AssetPath;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("blueprint.enum.inspect requires assetPath."));
        return Result;
    }

    AssetPath = NormalizeAssetPath(AssetPath);
    FString EnumJson;
    FString Error;
    if (!FLoomleBlueprintAdapter::DescribeUserDefinedEnum(AssetPath, EnumJson, Error))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), Error.Contains(TEXT("not found")) ? TEXT("ASSET_NOT_FOUND") : TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), Error.IsEmpty() ? TEXT("blueprint.enum.inspect failed") : Error);
        return Result;
    }

    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(EnumJson);
    FJsonSerializer::Deserialize(Reader, Result);
    Result->SetBoolField(TEXT("isError"), false);
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildBlueprintEnumEditToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    FString AssetPath;
    FString Operation;
    if (!Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath)
        || AssetPath.IsEmpty()
        || !Arguments->TryGetStringField(TEXT("operation"), Operation)
        || Operation.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("blueprint.enum.edit requires assetPath and operation."));
        return Result;
    }

    AssetPath = NormalizeAssetPath(AssetPath);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("operation"), Operation);

    const TSharedPtr<FJsonObject>* ArgsObject = nullptr;
    const TSharedPtr<FJsonObject> EmptyArgs = MakeShared<FJsonObject>();
    const TSharedPtr<FJsonObject> EffectiveArgs =
        (Arguments->TryGetObjectField(TEXT("args"), ArgsObject) && ArgsObject != nullptr && (*ArgsObject).IsValid())
            ? *ArgsObject
            : EmptyArgs;

    bool bDryRun = false;
    Arguments->TryGetBoolField(TEXT("dryRun"), bDryRun);
    Result->SetBoolField(TEXT("dryRun"), bDryRun);
    if (bDryRun)
    {
        Result->SetBoolField(TEXT("applied"), false);
        return Result;
    }

    const FString PayloadJson = SerializeBlueprintJsonObjectCondensed(EffectiveArgs);
    FString EnumJson;
    FString Error;
    bool bOk = false;
    if (Operation.Equals(TEXT("create"), ESearchCase::IgnoreCase))
    {
        bOk = FLoomleBlueprintAdapter::CreateUserDefinedEnum(AssetPath, PayloadJson, EnumJson, Error);
    }
    else if (Operation.Equals(TEXT("updateEntries"), ESearchCase::IgnoreCase))
    {
        bOk = FLoomleBlueprintAdapter::UpdateUserDefinedEnumEntries(AssetPath, PayloadJson, EnumJson, Error);
    }
    else
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("NOT_IMPLEMENTED"));
        Result->SetStringField(TEXT("message"), FString::Printf(TEXT("blueprint.enum.edit does not support operation yet: %s"), *Operation));
        return Result;
    }

    if (!bOk)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(
            TEXT("code"),
            Error.Contains(TEXT("already exists"))
                ? TEXT("ALREADY_EXISTS")
                : Error.Contains(TEXT("not found"))
                ? TEXT("ASSET_NOT_FOUND")
                : TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), Error.IsEmpty() ? TEXT("blueprint.enum.edit failed") : Error);
        return Result;
    }

    TSharedPtr<FJsonObject> EnumObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(EnumJson);
    if (FJsonSerializer::Deserialize(Reader, EnumObject) && EnumObject.IsValid())
    {
        Result->SetObjectField(TEXT("enum"), EnumObject);
        FString EnumPath;
        if (EnumObject->TryGetStringField(TEXT("enumPath"), EnumPath))
        {
            Result->SetStringField(TEXT("enumPath"), EnumPath);
        }
        const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
        if (EnumObject->TryGetArrayField(TEXT("entries"), Entries) && Entries != nullptr)
        {
            Result->SetArrayField(TEXT("entries"), *Entries);
        }
    }

    Result->SetBoolField(TEXT("applied"), true);
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildBlueprintStructInspectToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    FString AssetPath;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("blueprint.struct.inspect requires assetPath."));
        return Result;
    }

    AssetPath = NormalizeAssetPath(AssetPath);
    FString StructJson;
    FString Error;
    if (!FLoomleBlueprintAdapter::DescribeUserDefinedStruct(AssetPath, StructJson, Error))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), Error.Contains(TEXT("not found")) ? TEXT("ASSET_NOT_FOUND") : TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), Error.IsEmpty() ? TEXT("blueprint.struct.inspect failed") : Error);
        return Result;
    }

    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StructJson);
    FJsonSerializer::Deserialize(Reader, Result);
    Result->SetBoolField(TEXT("isError"), false);
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildBlueprintStructEditToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    FString AssetPath;
    FString Operation;
    if (!Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath)
        || AssetPath.IsEmpty()
        || !Arguments->TryGetStringField(TEXT("operation"), Operation)
        || Operation.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("blueprint.struct.edit requires assetPath and operation."));
        return Result;
    }

    AssetPath = NormalizeAssetPath(AssetPath);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("operation"), Operation);

    if (const TSharedPtr<FJsonObject> Blocked = BuildEditorMutationLifecycleBlockResult(
            TEXT("blueprint.struct.edit"),
            Arguments,
            AssetPath,
            TEXT("")))
    {
        return Blocked;
    }

    const TSharedPtr<FJsonObject>* ArgsObject = nullptr;
    const TSharedPtr<FJsonObject> EmptyArgs = MakeShared<FJsonObject>();
    const TSharedPtr<FJsonObject> EffectiveArgs =
        (Arguments->TryGetObjectField(TEXT("args"), ArgsObject) && ArgsObject != nullptr && (*ArgsObject).IsValid())
            ? *ArgsObject
            : EmptyArgs;

    bool bDryRun = false;
    Arguments->TryGetBoolField(TEXT("dryRun"), bDryRun);
    Result->SetBoolField(TEXT("dryRun"), bDryRun);
    if (bDryRun)
    {
        Result->SetBoolField(TEXT("applied"), false);
        return Result;
    }

    FString PreviousRevision;
    if (!Operation.Equals(TEXT("create"), ESearchCase::IgnoreCase))
    {
        FString PreviousJson;
        FString PreviousError;
        if (!FLoomleBlueprintAdapter::DescribeUserDefinedStruct(AssetPath, PreviousJson, PreviousError))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), PreviousError.Contains(TEXT("not found")) ? TEXT("ASSET_NOT_FOUND") : TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), PreviousError.IsEmpty() ? TEXT("Failed to resolve current Blueprint struct revision.") : PreviousError);
            return Result;
        }
        TSharedPtr<FJsonObject> PreviousObject;
        const TSharedRef<TJsonReader<>> PreviousReader = TJsonReaderFactory<>::Create(PreviousJson);
        FJsonSerializer::Deserialize(PreviousReader, PreviousObject);
        if (PreviousObject.IsValid())
        {
            PreviousObject->TryGetStringField(TEXT("revision"), PreviousRevision);
        }

        FString ExpectedRevision;
        if (Arguments->TryGetStringField(TEXT("expectedRevision"), ExpectedRevision) && !ExpectedRevision.IsEmpty() && !ExpectedRevision.Equals(PreviousRevision, ESearchCase::CaseSensitive))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("REVISION_CONFLICT"));
            Result->SetStringField(TEXT("message"), FString::Printf(TEXT("expectedRevision mismatch: expected %s but current revision is %s."), *ExpectedRevision, *PreviousRevision));
            Result->SetStringField(TEXT("previousRevision"), PreviousRevision);
            return Result;
        }
    }

    const FString PayloadJson = SerializeBlueprintJsonObjectCondensed(EffectiveArgs);
    FString StructJson;
    FString Error;
    FString ErrorCode;
    bool bOk = false;
    if (Operation.Equals(TEXT("create"), ESearchCase::IgnoreCase))
    {
        bOk = FLoomleBlueprintAdapter::CreateUserDefinedStruct(AssetPath, PayloadJson, StructJson, Error);
        if (!bOk)
        {
            ErrorCode = Error.Contains(TEXT("already exists"))
                ? TEXT("ALREADY_EXISTS")
                : TEXT("INVALID_ARGUMENT");
        }
    }
    else
    {
        bOk = FLoomleBlueprintAdapter::EditUserDefinedStruct(AssetPath, Operation, PayloadJson, StructJson, Error, ErrorCode);
    }

    if (!bOk)
    {
        Result->SetBoolField(TEXT("isError"), true);
        if (ErrorCode.IsEmpty())
        {
            ErrorCode = Error.Contains(TEXT("not found")) ? TEXT("ASSET_NOT_FOUND") : TEXT("INVALID_ARGUMENT");
        }
        Result->SetStringField(TEXT("code"), ErrorCode);
        Result->SetStringField(TEXT("message"), Error.IsEmpty() ? TEXT("blueprint.struct.edit failed") : Error);
        Result->SetBoolField(TEXT("applied"), false);
        Result->SetStringField(TEXT("previousRevision"), PreviousRevision);
        return Result;
    }

    TSharedPtr<FJsonObject> StructObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StructJson);
    if (FJsonSerializer::Deserialize(Reader, StructObject) && StructObject.IsValid())
    {
        Result->SetObjectField(TEXT("struct"), StructObject);
        FString StructPath;
        if (StructObject->TryGetStringField(TEXT("structPath"), StructPath))
        {
            Result->SetStringField(TEXT("structPath"), StructPath);
        }
        FString NewRevision;
        StructObject->TryGetStringField(TEXT("revision"), NewRevision);
        Result->SetStringField(TEXT("previousRevision"), PreviousRevision);
        Result->SetStringField(TEXT("newRevision"), NewRevision);
        Result->SetStringField(TEXT("revision"), NewRevision);
        const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
        if (StructObject->TryGetArrayField(TEXT("fields"), Fields) && Fields != nullptr)
        {
            Result->SetArrayField(TEXT("fields"), *Fields);
        }
    }
    Result->SetBoolField(TEXT("applied"), true);
    Result->SetBoolField(TEXT("changed"), true);
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildBlueprintMemberEditToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    FString AssetPath;
    FString MemberKind;
    FString Operation;
    if (!Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath)
        || AssetPath.IsEmpty()
        || !Arguments->TryGetStringField(TEXT("memberKind"), MemberKind)
        || MemberKind.IsEmpty()
        || !Arguments->TryGetStringField(TEXT("operation"), Operation)
        || Operation.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("blueprint.member.edit requires assetPath, memberKind, and operation."));
        return Result;
    }

    AssetPath = NormalizeAssetPath(AssetPath);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("memberKind"), MemberKind);
    Result->SetStringField(TEXT("operation"), Operation);

    const TSharedPtr<FJsonObject>* ArgsObject = nullptr;
    const TSharedPtr<FJsonObject> EmptyArgs = MakeShared<FJsonObject>();
    const TSharedPtr<FJsonObject> EffectiveArgs =
        (Arguments->TryGetObjectField(TEXT("args"), ArgsObject) && ArgsObject != nullptr && (*ArgsObject).IsValid())
            ? *ArgsObject
            : EmptyArgs;

    bool bDryRun = false;
    Arguments->TryGetBoolField(TEXT("dryRun"), bDryRun);
    Result->SetBoolField(TEXT("dryRun"), bDryRun);

    const FString PayloadJson = SerializeBlueprintJsonObjectCondensed(EffectiveArgs);
    FString Error;
    bool bOk = false;

    const bool bSupportedMemberKind =
        MemberKind.Equals(TEXT("component"), ESearchCase::IgnoreCase)
        || MemberKind.Equals(TEXT("variable"), ESearchCase::IgnoreCase)
        || MemberKind.Equals(TEXT("function"), ESearchCase::IgnoreCase)
        || MemberKind.Equals(TEXT("macro"), ESearchCase::IgnoreCase)
        || MemberKind.Equals(TEXT("dispatcher"), ESearchCase::IgnoreCase)
        || MemberKind.Equals(TEXT("event"), ESearchCase::IgnoreCase)
        || MemberKind.Equals(TEXT("customEvent"), ESearchCase::IgnoreCase);
    if (!bSupportedMemberKind)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("NOT_IMPLEMENTED"));
        Result->SetStringField(
            TEXT("message"),
            FString::Printf(TEXT("blueprint.member.edit does not support memberKind: %s"), *MemberKind));
        return Result;
    }

    if (!FLoomleBlueprintAdapter::ValidateMemberEditRequest(AssetPath, MemberKind, Operation, PayloadJson, Error))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), Error.IsEmpty() ? TEXT("blueprint.member.edit request is invalid.") : Error);
        return Result;
    }

    if (bDryRun)
    {
        Result->SetBoolField(TEXT("applied"), false);
        return Result;
    }

    if (MemberKind.Equals(TEXT("component"), ESearchCase::IgnoreCase))
    {
        bOk = FLoomleBlueprintAdapter::EditComponentMember(AssetPath, Operation, PayloadJson, Error);
    }
    else if (MemberKind.Equals(TEXT("variable"), ESearchCase::IgnoreCase))
    {
        bOk = FLoomleBlueprintAdapter::EditVariableMember(AssetPath, Operation, PayloadJson, Error);
    }
    else if (MemberKind.Equals(TEXT("function"), ESearchCase::IgnoreCase))
    {
        bOk = FLoomleBlueprintAdapter::EditFunctionMember(AssetPath, Operation, PayloadJson, Error);
    }
    else if (MemberKind.Equals(TEXT("macro"), ESearchCase::IgnoreCase))
    {
        bOk = FLoomleBlueprintAdapter::EditMacroMember(AssetPath, Operation, PayloadJson, Error);
    }
    else if (MemberKind.Equals(TEXT("dispatcher"), ESearchCase::IgnoreCase))
    {
        bOk = FLoomleBlueprintAdapter::EditDispatcherMember(AssetPath, Operation, PayloadJson, Error);
    }
    else if (MemberKind.Equals(TEXT("event"), ESearchCase::IgnoreCase) || MemberKind.Equals(TEXT("customEvent"), ESearchCase::IgnoreCase))
    {
        bOk = FLoomleBlueprintAdapter::EditEventMember(AssetPath, Operation, PayloadJson, Error);
    }

    if (!bOk)
    {
        TSharedPtr<FJsonObject> StructuredError;
        const TSharedRef<TJsonReader<>> ErrorReader = TJsonReaderFactory<>::Create(Error);
        const bool bHasStructuredError = FJsonSerializer::Deserialize(ErrorReader, StructuredError) && StructuredError.IsValid();
        FString StructuredCode;
        FString StructuredMessage;
        FString StructuredReason;
        if (bHasStructuredError)
        {
            StructuredError->TryGetStringField(TEXT("code"), StructuredCode);
            StructuredError->TryGetStringField(TEXT("message"), StructuredMessage);
            StructuredError->TryGetStringField(TEXT("reason"), StructuredReason);
        }

        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(
            TEXT("code"),
            !StructuredCode.IsEmpty()
                ? StructuredCode
                : Error.Contains(TEXT("requires")) || Error.Contains(TEXT("Unsupported")) || Error.Contains(TEXT("Failed to resolve"))
                ? TEXT("INVALID_ARGUMENT")
                : TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), !StructuredMessage.IsEmpty() ? StructuredMessage : (Error.IsEmpty() ? TEXT("blueprint.member.edit failed") : Error));
        if (!StructuredReason.IsEmpty())
        {
            Result->SetStringField(TEXT("reason"), StructuredReason);
        }
        if (bHasStructuredError)
        {
            Result->SetObjectField(TEXT("details"), StructuredError);
        }
        return Result;
    }

    Result->SetBoolField(TEXT("applied"), true);
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildBlueprintGraphInspectToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    auto BuildBlueprintQueryBaseResult = [](const TSharedPtr<FJsonObject>& BaseArguments) -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("isError"), false);

        FString AssetPath;
        FString GraphName;
        FString InlineNodeGuid;
        if (!BuildBlueprintQueryAddress(BaseArguments, AssetPath, GraphName, InlineNodeGuid, Result))
        {
            return Result;
        }

        const bool bBaseSnapshotRequest = BaseArguments.IsValid() && BaseArguments->HasTypedField<EJson::Boolean>(TEXT("_loomleBaseSnapshot"))
            && BaseArguments->GetBoolField(TEXT("_loomleBaseSnapshot"));

        FString RequestedLayoutDetail = TEXT("basic");
        if (BaseArguments.IsValid())
        {
            BaseArguments->TryGetStringField(TEXT("layoutDetail"), RequestedLayoutDetail);
        }
        RequestedLayoutDetail = RequestedLayoutDetail.ToLower();
        if (!RequestedLayoutDetail.Equals(TEXT("measured")))
        {
            RequestedLayoutDetail = TEXT("basic");
        }
        const FString AppliedLayoutDetail = RequestedLayoutDetail.Equals(TEXT("measured")) ? TEXT("basic") : RequestedLayoutDetail;

        FLoomleBlueprintNodeListOptions BlueprintListOptions;
        FLoomleBlueprintNodeListStats BlueprintListStats;
        if (!bBaseSnapshotRequest && BaseArguments.IsValid())
        {
            const FBlueprintGraphQueryShapeOptions QueryShapeOptions = ParseBlueprintGraphQueryShapeOptions(BaseArguments);
            BlueprintListOptions.NodeClasses = QueryShapeOptions.NodeClasses;
            BlueprintListOptions.NodeIds = QueryShapeOptions.NodeIds.Array();
            BlueprintListOptions.Offset = QueryShapeOptions.Offset;
            BlueprintListOptions.Limit = QueryShapeOptions.bLimitExplicit ? QueryShapeOptions.Limit : 50;
            BlueprintListOptions.bIncludeConnections = QueryShapeOptions.bIncludeConnections;
        }

        FString NodesJson;
        FString Error;
        bool bOk = false;
        if (!InlineNodeGuid.IsEmpty())
        {
            FString SubgraphNameOut;
            bOk = FLoomleBlueprintAdapter::ListCompositeSubgraphNodes(
                AssetPath,
                InlineNodeGuid,
                SubgraphNameOut,
                NodesJson,
                Error,
                !bBaseSnapshotRequest ? &BlueprintListOptions : nullptr,
                !bBaseSnapshotRequest ? &BlueprintListStats : nullptr);
            if (bOk && GraphName.IsEmpty())
            {
                GraphName = SubgraphNameOut;
            }
            if (!bOk)
            {
                const FString DomainCode = Error.Contains(TEXT("not a composite"))
                    ? TEXT("GRAPH_REF_NOT_COMPOSITE")
                    : (Error.Contains(TEXT("not found")) ? TEXT("NODE_NOT_FOUND") : TEXT("GRAPH_REF_INVALID"));
                Result->SetBoolField(TEXT("isError"), true);
                Result->SetStringField(TEXT("code"), DomainCode);
                Result->SetStringField(TEXT("message"), Error.IsEmpty() ? TEXT("blueprint.graph.inspect (inline ref) failed") : Error);
                return Result;
            }
        }
        else
        {
            bOk = FLoomleBlueprintAdapter::ListGraphNodes(
                AssetPath,
                GraphName,
                NodesJson,
                Error,
                !bBaseSnapshotRequest ? &BlueprintListOptions : nullptr,
                !bBaseSnapshotRequest ? &BlueprintListStats : nullptr);
            if (!bOk)
            {
                Result->SetBoolField(TEXT("isError"), true);
                Result->SetStringField(TEXT("code"), Error.Contains(TEXT("Graph not found")) ? TEXT("GRAPH_NOT_FOUND") : TEXT("INTERNAL_ERROR"));
                Result->SetStringField(TEXT("message"), Error.IsEmpty() ? TEXT("blueprint.graph.inspect failed") : Error);
                return Result;
            }
        }

        TArray<TSharedPtr<FJsonValue>> Nodes;
        {
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NodesJson);
            FJsonSerializer::Deserialize(Reader, Nodes);
        }

        TArray<TSharedPtr<FJsonValue>> SnapshotNodes;
        TArray<TSharedPtr<FJsonValue>> Edges;
        TArray<TSharedPtr<FJsonValue>> Diagnostics;
        TArray<FString> SignatureNodeTokens;
        TArray<FString> SignatureEdgeTokens;

        for (const TSharedPtr<FJsonValue>& NodeValue : Nodes)
        {
            const TSharedPtr<FJsonObject>* NodeObj = nullptr;
            if (!NodeValue.IsValid() || !NodeValue->TryGetObject(NodeObj) || NodeObj == nullptr || !(*NodeObj).IsValid())
            {
                continue;
            }

            const TSharedPtr<FJsonObject> NodeEditCapabilities = BuildBlueprintNodeEditCapabilities(*NodeObj);
            bool bHasNodeEditCapabilities = false;
            if (NodeEditCapabilities.IsValid())
            {
                NodeEditCapabilities->TryGetBoolField(TEXT("hasPinOperations"), bHasNodeEditCapabilities);
            }
            if (bHasNodeEditCapabilities)
            {
                (*NodeObj)->SetBoolField(TEXT("hasNodeEditCapabilities"), true);
                (*NodeObj)->SetStringField(TEXT("inspectWith"), TEXT("blueprint.node.inspect"));
            }

            SnapshotNodes.Add(NodeValue);

            FString FromNodeId;
            (*NodeObj)->TryGetStringField(TEXT("guid"), FromNodeId);
            if (!FromNodeId.IsEmpty())
            {
                SignatureNodeTokens.Add(FromNodeId);
            }

            const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
            if (!(*NodeObj)->TryGetArrayField(TEXT("pins"), Pins) || Pins == nullptr)
            {
                continue;
            }

            for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
            {
                const TSharedPtr<FJsonObject>* PinObj = nullptr;
                if (!PinValue.IsValid() || !PinValue->TryGetObject(PinObj) || PinObj == nullptr || !(*PinObj).IsValid())
                {
                    continue;
                }

                FString FromPin;
                (*PinObj)->TryGetStringField(TEXT("name"), FromPin);

                const TArray<TSharedPtr<FJsonValue>>* Linked = nullptr;
                if (!(*PinObj)->TryGetArrayField(TEXT("linkedTo"), Linked) || Linked == nullptr)
                {
                    continue;
                }

                for (const TSharedPtr<FJsonValue>& LinkValue : *Linked)
                {
                    const TSharedPtr<FJsonObject>* LinkObj = nullptr;
                    if (!LinkValue.IsValid() || !LinkValue->TryGetObject(LinkObj) || LinkObj == nullptr || !(*LinkObj).IsValid())
                    {
                        continue;
                    }

                    FString ToNodeId;
                    FString ToPin;
                    (*LinkObj)->TryGetStringField(TEXT("nodeGuid"), ToNodeId);
                    (*LinkObj)->TryGetStringField(TEXT("pin"), ToPin);
                    if (ToNodeId.IsEmpty() || ToPin.IsEmpty())
                    {
                        continue;
                    }

                    TSharedPtr<FJsonObject> Edge = MakeShared<FJsonObject>();
                    Edge->SetStringField(TEXT("fromNodeId"), FromNodeId);
                    Edge->SetStringField(TEXT("fromPin"), FromPin);
                    Edge->SetStringField(TEXT("toNodeId"), ToNodeId);
                    Edge->SetStringField(TEXT("toPin"), ToPin);
                    Edges.Add(MakeShared<FJsonValueObject>(Edge));
                    SignatureEdgeTokens.Add(FromNodeId + TEXT("|") + FromPin + TEXT("->") + ToNodeId + TEXT("|") + ToPin);
                }
            }
        }

        UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
        if (Blueprint != nullptr)
        {
            TMap<FGuid, UEdGraphNode*> GuidToNode;
            TArray<UEdGraph*> AllGraphs;
            AllGraphs.Append(Blueprint->UbergraphPages);
            AllGraphs.Append(Blueprint->FunctionGraphs);
            AllGraphs.Append(Blueprint->MacroGraphs);
            for (UEdGraph* Graph : AllGraphs)
            {
                if (Graph == nullptr)
                {
                    continue;
                }
                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    if (Node != nullptr)
                    {
                        GuidToNode.Add(Node->NodeGuid, Node);
                    }
                }
            }

            for (TSharedPtr<FJsonValue>& SnapshotNodeValue : SnapshotNodes)
            {
                const TSharedPtr<FJsonObject>* SnapshotNodeObj = nullptr;
                if (!SnapshotNodeValue.IsValid() || !SnapshotNodeValue->TryGetObject(SnapshotNodeObj) || SnapshotNodeObj == nullptr || !(*SnapshotNodeObj).IsValid())
                {
                    continue;
                }

                FString NodeClassPath;
                (*SnapshotNodeObj)->TryGetStringField(TEXT("nodeClassPath"), NodeClassPath);
                if (!NodeClassPath.Contains(TEXT("K2Node_Composite")))
                {
                    continue;
                }

                FString GuidText;
                (*SnapshotNodeObj)->TryGetStringField(TEXT("guid"), GuidText);
                FGuid NodeGuid;
                if (!FGuid::Parse(GuidText, NodeGuid))
                {
                    continue;
                }

                UEdGraphNode** FoundNode = GuidToNode.Find(NodeGuid);
                if (FoundNode == nullptr || *FoundNode == nullptr)
                {
                    continue;
                }

                FObjectPropertyBase* BoundGraphProp = FindFProperty<FObjectPropertyBase>((*FoundNode)->GetClass(), TEXT("BoundGraph"));
                if (BoundGraphProp == nullptr)
                {
                    continue;
                }

                UEdGraph* SubGraph = Cast<UEdGraph>(BoundGraphProp->GetObjectPropertyValue_InContainer(*FoundNode));
                if (SubGraph == nullptr)
                {
                    continue;
                }

                SetBlueprintNodeInlineChildGraphRef(*SnapshotNodeObj, GuidText, AssetPath);
            }
        }

        Algo::Sort(SignatureNodeTokens);
        Algo::Sort(SignatureEdgeTokens);
        const FString Signature = FString::Join(SignatureNodeTokens, TEXT(";")) + TEXT("#") + FString::Join(SignatureEdgeTokens, TEXT(";"));

        TSharedPtr<FJsonObject> Snapshot = MakeShared<FJsonObject>();
        Snapshot->SetStringField(TEXT("signature"), Signature);
        Snapshot->SetArrayField(TEXT("nodes"), SnapshotNodes);
        Snapshot->SetArrayField(TEXT("edges"), Edges);

        TSharedPtr<FJsonObject> LayoutCapabilities = MakeShared<FJsonObject>();
        LayoutCapabilities->SetBoolField(TEXT("canReadPosition"), true);
        LayoutCapabilities->SetBoolField(TEXT("canReadSize"), false);
        LayoutCapabilities->SetBoolField(TEXT("canReadBounds"), false);
        LayoutCapabilities->SetBoolField(TEXT("canMoveNode"), true);
        LayoutCapabilities->SetBoolField(TEXT("canBatchMove"), true);
        LayoutCapabilities->SetBoolField(TEXT("supportsMeasuredGeometry"), false);
        LayoutCapabilities->SetStringField(TEXT("positionSource"), TEXT("model"));
        LayoutCapabilities->SetStringField(TEXT("sizeSource"), TEXT("partial"));

        TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
        const int32 EffectiveTotalNodes = (!bBaseSnapshotRequest && BlueprintListStats.TotalNodes > 0) ? BlueprintListStats.TotalNodes : Nodes.Num();
        const int32 EffectiveMatchingNodes = (!bBaseSnapshotRequest && BlueprintListStats.MatchingNodes > 0) ? BlueprintListStats.MatchingNodes : SnapshotNodes.Num();
        const bool bTruncated = EffectiveMatchingNodes > (BlueprintListOptions.Offset + SnapshotNodes.Num());
        Meta->SetNumberField(TEXT("totalNodes"), EffectiveTotalNodes);
        Meta->SetNumberField(TEXT("returnedNodes"), SnapshotNodes.Num());
        Meta->SetNumberField(TEXT("totalEdges"), Edges.Num());
        Meta->SetNumberField(TEXT("returnedEdges"), Edges.Num());
        Meta->SetBoolField(TEXT("truncated"), bTruncated);
        Meta->SetObjectField(TEXT("layoutCapabilities"), LayoutCapabilities);
        Meta->SetStringField(TEXT("layoutDetailRequested"), RequestedLayoutDetail);
        Meta->SetStringField(TEXT("layoutDetailApplied"), AppliedLayoutDetail);

        if (RequestedLayoutDetail.Equals(TEXT("measured")) && !AppliedLayoutDetail.Equals(TEXT("measured")))
        {
            TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
            Diagnostic->SetStringField(TEXT("code"), TEXT("LAYOUT_DETAIL_DOWNGRADED"));
            Diagnostic->SetStringField(TEXT("message"), TEXT("Requested measured layout detail is not yet supported for this graph query; returned basic layout data."));
            Diagnostic->SetStringField(TEXT("requestedDetail"), RequestedLayoutDetail);
            Diagnostic->SetStringField(TEXT("appliedDetail"), AppliedLayoutDetail);
            Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
        }

        Result->SetStringField(TEXT("graphType"), TEXT("blueprint"));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetStringField(TEXT("graphName"), GraphName);
        Result->SetObjectField(TEXT("graphRef"), MakeBlueprintEffectiveGraphRef(AssetPath, GraphName, InlineNodeGuid));
        Result->SetObjectField(TEXT("semanticSnapshot"), Snapshot);
        Result->SetStringField(TEXT("revision"), BuildBlueprintQueryRevision(Result, Signature));
        Result->SetStringField(TEXT("nextCursor"), bTruncated ? BuildBlueprintGraphQueryCursor(BlueprintListOptions.Offset + SnapshotNodes.Num()) : TEXT(""));
        Result->SetObjectField(TEXT("meta"), Meta);
        Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
        return Result;
    };

    auto ShapeBlueprintQueryResult = [](const TSharedPtr<FJsonObject>& BaseResult, const TSharedPtr<FJsonObject>& QueryArguments) -> TSharedPtr<FJsonObject>
    {
        if (!BaseResult.IsValid())
        {
            TSharedPtr<FJsonObject> ErrorResult = MakeShared<FJsonObject>();
            ErrorResult->SetBoolField(TEXT("isError"), true);
            ErrorResult->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
            ErrorResult->SetStringField(TEXT("message"), TEXT("blueprint.graph.inspect produced an invalid result."));
            return ErrorResult;
        }

        bool bIsError = false;
        if (BaseResult->TryGetBoolField(TEXT("isError"), bIsError) && bIsError)
        {
            return CloneJsonObject(BaseResult);
        }

        TSharedPtr<FJsonObject> Result = CloneJsonObject(BaseResult);
        if (!Result.IsValid())
        {
            return BaseResult;
        }

        const FBlueprintGraphQueryShapeOptions ShapeOptions = ParseBlueprintGraphQueryShapeOptions(QueryArguments);
        if (!ShapeOptions.bCursorValid)
        {
            TSharedPtr<FJsonObject> ErrorResult = MakeShared<FJsonObject>();
            ErrorResult->SetBoolField(TEXT("isError"), true);
            ErrorResult->SetStringField(TEXT("code"), TEXT("INVALID_CURSOR"));
            ErrorResult->SetStringField(TEXT("message"), ShapeOptions.CursorError.IsEmpty() ? TEXT("blueprint.graph.inspect cursor is invalid.") : ShapeOptions.CursorError);
            return ErrorResult;
        }

        const TSharedPtr<FJsonObject>* SnapshotObject = nullptr;
        if (!Result->TryGetObjectField(TEXT("semanticSnapshot"), SnapshotObject) || SnapshotObject == nullptr || !(*SnapshotObject).IsValid())
        {
            return Result;
        }

        const TArray<TSharedPtr<FJsonValue>>* SnapshotNodes = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* SnapshotEdges = nullptr;
        (*SnapshotObject)->TryGetArrayField(TEXT("nodes"), SnapshotNodes);
        (*SnapshotObject)->TryGetArrayField(TEXT("edges"), SnapshotEdges);
        const int32 OriginalNodeCount = SnapshotNodes ? SnapshotNodes->Num() : 0;
        const int32 OriginalEdgeCount = SnapshotEdges ? SnapshotEdges->Num() : 0;

        TArray<TSharedPtr<FJsonValue>> ShapedNodes;
        TArray<TSharedPtr<FJsonValue>> ShapedEdges;
        TSet<FString> IncludedNodeIds;
        TArray<FString> SignatureNodeTokens;
        TArray<FString> SignatureEdgeTokens;
        int32 MatchingNodeCount = 0;

        if (SnapshotNodes != nullptr)
        {
            ShapedNodes.Reserve(FMath::Min(ShapeOptions.Limit, SnapshotNodes->Num()));
            for (const TSharedPtr<FJsonValue>& NodeValue : *SnapshotNodes)
            {
                const TSharedPtr<FJsonObject>* NodeObject = nullptr;
                if (!NodeValue.IsValid() || !NodeValue->TryGetObject(NodeObject) || NodeObject == nullptr || !(*NodeObject).IsValid())
                {
                    continue;
                }

                if (!BlueprintQueryNodeMatchesShapeOptions(*NodeObject, ShapeOptions))
                {
                    continue;
                }

                ++MatchingNodeCount;
                if (MatchingNodeCount <= ShapeOptions.Offset)
                {
                    continue;
                }
                if (ShapedNodes.Num() >= ShapeOptions.Limit)
                {
                    continue;
                }

                FString NodeId;
                (*NodeObject)->TryGetStringField(TEXT("id"), NodeId);
                FString NodeGuid;
                (*NodeObject)->TryGetStringField(TEXT("guid"), NodeGuid);
                if (NodeId.IsEmpty())
                {
                    NodeId = NodeGuid;
                }

                IncludedNodeIds.Add(NodeId);
                SignatureNodeTokens.Add(NodeId);
                ShapedNodes.Add(NodeValue);
            }
        }

        for (const TSharedPtr<FJsonValue>& NodeValue : ShapedNodes)
        {
            const TSharedPtr<FJsonObject>* NodeObject = nullptr;
            if (!NodeValue.IsValid() || !NodeValue->TryGetObject(NodeObject) || NodeObject == nullptr || !(*NodeObject).IsValid())
            {
                continue;
            }

            if (!ShapeOptions.bIncludeConnections)
            {
                PruneBlueprintQueryNodeLinks(*NodeObject, IncludedNodeIds);
            }
        }

        if (SnapshotEdges != nullptr)
        {
            for (const TSharedPtr<FJsonValue>& EdgeValue : *SnapshotEdges)
            {
                const TSharedPtr<FJsonObject>* EdgeObject = nullptr;
                if (!EdgeValue.IsValid() || !EdgeValue->TryGetObject(EdgeObject) || EdgeObject == nullptr || !(*EdgeObject).IsValid())
                {
                    continue;
                }

                FString FromNodeId;
                FString ToNodeId;
                FString FromPin;
                FString ToPin;
                (*EdgeObject)->TryGetStringField(TEXT("fromNodeId"), FromNodeId);
                (*EdgeObject)->TryGetStringField(TEXT("toNodeId"), ToNodeId);
                const bool bBothEndpointsIncluded = IncludedNodeIds.Contains(FromNodeId) && IncludedNodeIds.Contains(ToNodeId);
                const bool bTouchesIncludedNode = IncludedNodeIds.Contains(FromNodeId) || IncludedNodeIds.Contains(ToNodeId);
                if (!bBothEndpointsIncluded && (!ShapeOptions.bIncludeConnections || !bTouchesIncludedNode))
                {
                    continue;
                }

                (*EdgeObject)->TryGetStringField(TEXT("fromPin"), FromPin);
                (*EdgeObject)->TryGetStringField(TEXT("toPin"), ToPin);
                SignatureEdgeTokens.Add(FromNodeId + TEXT("|") + FromPin + TEXT("->") + ToNodeId + TEXT("|") + ToPin);
                ShapedEdges.Add(EdgeValue);
            }
        }

        Algo::Sort(SignatureNodeTokens);
        Algo::Sort(SignatureEdgeTokens);
        const FString Signature = FString::Join(SignatureNodeTokens, TEXT(";")) + TEXT("#") + FString::Join(SignatureEdgeTokens, TEXT(";"));
        const bool bTruncated = (ShapeOptions.Offset + ShapedNodes.Num()) < MatchingNodeCount;

        (*SnapshotObject)->SetStringField(TEXT("signature"), Signature);
        (*SnapshotObject)->SetArrayField(TEXT("nodes"), ShapedNodes);
        (*SnapshotObject)->SetArrayField(TEXT("edges"), ShapedEdges);
        Result->SetStringField(TEXT("revision"), BuildBlueprintQueryRevision(Result, Signature));
        Result->SetStringField(TEXT("nextCursor"), bTruncated ? BuildBlueprintGraphQueryCursor(ShapeOptions.Offset + ShapedNodes.Num()) : TEXT(""));

        const TSharedPtr<FJsonObject>* MetaObject = nullptr;
        if (!Result->TryGetObjectField(TEXT("meta"), MetaObject) || MetaObject == nullptr || !(*MetaObject).IsValid())
        {
            TSharedPtr<FJsonObject> NewMeta = MakeShared<FJsonObject>();
            Result->SetObjectField(TEXT("meta"), NewMeta);
            Result->TryGetObjectField(TEXT("meta"), MetaObject);
        }

        (*MetaObject)->SetNumberField(TEXT("totalNodes"), OriginalNodeCount);
        (*MetaObject)->SetNumberField(TEXT("returnedNodes"), ShapedNodes.Num());
        (*MetaObject)->SetNumberField(TEXT("totalEdges"), OriginalEdgeCount);
        (*MetaObject)->SetNumberField(TEXT("returnedEdges"), ShapedEdges.Num());
        (*MetaObject)->SetBoolField(TEXT("truncated"), bTruncated);
        return Result;
    };

    TSharedPtr<FJsonObject> BaseArguments = CloneJsonObject(Arguments);
    if (!BaseArguments.IsValid())
    {
        BaseArguments = MakeShared<FJsonObject>();
    }
    BaseArguments->SetBoolField(TEXT("_loomleBaseSnapshot"), true);

    TSharedPtr<FJsonObject> BaseResult;
    if (!IsInGameThread())
    {
        TPromise<TSharedPtr<FJsonObject>> ResponsePromise;
        TFuture<TSharedPtr<FJsonObject>> ResponseFuture = ResponsePromise.GetFuture();
        AsyncTask(ENamedThreads::GameThread, [BuildBlueprintQueryBaseResult, BaseArguments, Promise = MoveTemp(ResponsePromise)]() mutable
        {
            Promise.SetValue(BuildBlueprintQueryBaseResult(BaseArguments));
        });

        static constexpr uint32 GameThreadTimeoutMs = 30000;
        if (!ResponseFuture.WaitFor(FTimespan::FromMilliseconds(GameThreadTimeoutMs)))
        {
            TSharedPtr<FJsonObject> TimeoutResult = MakeShared<FJsonObject>();
            TimeoutResult->SetBoolField(TEXT("isError"), true);
            TimeoutResult->SetStringField(TEXT("code"), TEXT("EXECUTION_TIMEOUT"));
            TimeoutResult->SetStringField(TEXT("message"), TEXT("EXECUTION_TIMEOUT"));
            return TimeoutResult;
        }

        BaseResult = ResponseFuture.Get();
    }
    else
    {
        BaseResult = BuildBlueprintQueryBaseResult(BaseArguments);
    }

    return ShapeBlueprintQueryResult(BaseResult, Arguments);
}

namespace
{
FString GetBlueprintNodeClassText(const TSharedPtr<FJsonObject>& Node)
{
    FString ClassText;
    FString FieldValue;
    for (const TCHAR* FieldName : {TEXT("className"), TEXT("classPath"), TEXT("nodeClassPath")})
    {
        if (Node.IsValid() && Node->TryGetStringField(FieldName, FieldValue) && !FieldValue.IsEmpty())
        {
            if (!ClassText.IsEmpty())
            {
                ClassText += TEXT(" ");
            }
            ClassText += FieldValue;
        }
    }
    return ClassText;
}

bool BlueprintNodeClassContains(const TSharedPtr<FJsonObject>& Node, const TCHAR* Needle)
{
    return GetBlueprintNodeClassText(Node).Contains(Needle);
}

void AddBlueprintNodePinEditOperation(
    TArray<TSharedPtr<FJsonValue>>& Operations,
    const FString& Operation,
    const FString& Role,
    const FString& NameMode,
    const FString& Summary)
{
    TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
    Entry->SetStringField(TEXT("operation"), Operation);
    Entry->SetStringField(TEXT("role"), Role);
    Entry->SetStringField(TEXT("nameMode"), NameMode);
    Entry->SetStringField(TEXT("summary"), Summary);
    Operations.Add(MakeShared<FJsonValueObject>(Entry));
}

TArray<TSharedPtr<FJsonValue>> MakeBlueprintNodePinNamesByDirection(
    const TSharedPtr<FJsonObject>& Node,
    const FString& Direction,
    const bool bExecOnly,
    const bool bExcludeDefault)
{
    TArray<TSharedPtr<FJsonValue>> Names;
    const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
    if (!Node.IsValid() || !Node->TryGetArrayField(TEXT("pins"), Pins) || Pins == nullptr)
    {
        return Names;
    }

    for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
    {
        const TSharedPtr<FJsonObject>* Pin = nullptr;
        if (!PinValue.IsValid() || !PinValue->TryGetObject(Pin) || Pin == nullptr || !(*Pin).IsValid())
        {
            continue;
        }

        FString PinName;
        FString PinDirection;
        FString PinCategory;
        (*Pin)->TryGetStringField(TEXT("name"), PinName);
        (*Pin)->TryGetStringField(TEXT("direction"), PinDirection);
        (*Pin)->TryGetStringField(TEXT("category"), PinCategory);
        if (PinName.IsEmpty() || !PinDirection.Equals(Direction, ESearchCase::IgnoreCase))
        {
            continue;
        }
        if (bExecOnly && !PinCategory.Equals(TEXT("exec"), ESearchCase::IgnoreCase))
        {
            continue;
        }
        if (bExcludeDefault && PinName.Equals(TEXT("Default"), ESearchCase::IgnoreCase))
        {
            continue;
        }
        Names.Add(MakeShared<FJsonValueString>(PinName));
    }
    return Names;
}

TSharedPtr<FJsonObject> BuildBlueprintNodeEditCapabilities(const TSharedPtr<FJsonObject>& Node)
{
    TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Operations;
    TArray<TSharedPtr<FJsonValue>> Notes;

    if (BlueprintNodeClassContains(Node, TEXT("K2Node_SwitchName")) || BlueprintNodeClassContains(Node, TEXT("K2Node_SwitchString")))
    {
        AddBlueprintNodePinEditOperation(Operations, TEXT("addPin"), TEXT("case"), TEXT("required"), TEXT("Add a named switch case output pin."));
        AddBlueprintNodePinEditOperation(Operations, TEXT("removePin"), TEXT("case"), TEXT("target"), TEXT("Remove one switch case output pin."));
        AddBlueprintNodePinEditOperation(Operations, TEXT("renamePin"), TEXT("case"), TEXT("required"), TEXT("Rename one switch case output pin."));
        Notes.Add(MakeShared<FJsonValueString>(TEXT("SwitchName and SwitchString store editable cases in PinNames.")));
    }
    else if (BlueprintNodeClassContains(Node, TEXT("K2Node_SwitchInteger")))
    {
        AddBlueprintNodePinEditOperation(Operations, TEXT("addPin"), TEXT("case"), TEXT("generated"), TEXT("Append the next contiguous integer case output pin."));
        AddBlueprintNodePinEditOperation(Operations, TEXT("removePin"), TEXT("case"), TEXT("target"), TEXT("Remove an integer case output pin subject to UE node rules."));
        Notes.Add(MakeShared<FJsonValueString>(TEXT("SwitchInteger cases are contiguous from StartIndex; arbitrary case names are not supported.")));
    }
    else if (BlueprintNodeClassContains(Node, TEXT("K2Node_SwitchEnum")))
    {
        AddBlueprintNodePinEditOperation(Operations, TEXT("addPin"), TEXT("case"), TEXT("generated"), TEXT("Reveal the next hidden enum case output pin."));
        AddBlueprintNodePinEditOperation(Operations, TEXT("removePin"), TEXT("case"), TEXT("target"), TEXT("Hide one enum case output pin and break its links."));
        Notes.Add(MakeShared<FJsonValueString>(TEXT("SwitchEnum cases come from the enum; edit shows or hides existing cases.")));
    }
    else if (BlueprintNodeClassContains(Node, TEXT("K2Node_ExecutionSequence")) || BlueprintNodeClassContains(Node, TEXT("K2Node_MultiGate")))
    {
        AddBlueprintNodePinEditOperation(Operations, TEXT("addPin"), TEXT("exec"), TEXT("generated"), TEXT("Add one execution output pin."));
        AddBlueprintNodePinEditOperation(Operations, TEXT("insertPin"), TEXT("exec"), TEXT("generated"), TEXT("Insert one execution output pin before or after a target pin when supported."));
        AddBlueprintNodePinEditOperation(Operations, TEXT("removePin"), TEXT("exec"), TEXT("target"), TEXT("Remove one execution output pin when UE allows it."));
    }
    else if (BlueprintNodeClassContains(Node, TEXT("K2Node_DoOnceMultiInput")))
    {
        AddBlueprintNodePinEditOperation(Operations, TEXT("addPin"), TEXT("exec"), TEXT("generated"), TEXT("Add one DoOnce input/output execution pair."));
        AddBlueprintNodePinEditOperation(Operations, TEXT("removePin"), TEXT("exec"), TEXT("target"), TEXT("Remove one DoOnce input/output execution pair."));
    }
    else if (BlueprintNodeClassContains(Node, TEXT("K2Node_MakeMap")))
    {
        AddBlueprintNodePinEditOperation(Operations, TEXT("addPin"), TEXT("pair"), TEXT("generated"), TEXT("Add one key/value input pair."));
        AddBlueprintNodePinEditOperation(Operations, TEXT("removePin"), TEXT("pair"), TEXT("target"), TEXT("Remove one key/value input pair."));
    }
    else if (BlueprintNodeClassContains(Node, TEXT("K2Node_MakeArray")) || BlueprintNodeClassContains(Node, TEXT("K2Node_MakeSet")))
    {
        AddBlueprintNodePinEditOperation(Operations, TEXT("addPin"), TEXT("input"), TEXT("generated"), TEXT("Add one container element input pin."));
        AddBlueprintNodePinEditOperation(Operations, TEXT("removePin"), TEXT("input"), TEXT("target"), TEXT("Remove one container element input pin."));
    }
    else if (BlueprintNodeClassContains(Node, TEXT("K2Node_Select")))
    {
        AddBlueprintNodePinEditOperation(Operations, TEXT("addPin"), TEXT("option"), TEXT("generated"), TEXT("Add one selectable option pin."));
        AddBlueprintNodePinEditOperation(Operations, TEXT("removePin"), TEXT("option"), TEXT("optional"), TEXT("Remove the last removable selectable option pin, matching UE RemoveOptionPinToNode behavior."));
        Notes.Add(MakeShared<FJsonValueString>(TEXT("Select removePin reduces the option count; UE does not remove an arbitrary named option pin.")));
    }
    else if (BlueprintNodeClassContains(Node, TEXT("K2Node_PromotableOperator")) || BlueprintNodeClassContains(Node, TEXT("K2Node_CommutativeAssociativeBinaryOperator")))
    {
        AddBlueprintNodePinEditOperation(Operations, TEXT("addPin"), TEXT("input"), TEXT("generated"), TEXT("Add one operator input pin."));
        AddBlueprintNodePinEditOperation(Operations, TEXT("removePin"), TEXT("input"), TEXT("target"), TEXT("Remove one operator input pin."));
    }
    else if (BlueprintNodeClassContains(Node, TEXT("K2Node_FormatText")))
    {
        AddBlueprintNodePinEditOperation(Operations, TEXT("addPin"), TEXT("argument"), TEXT("optional"), TEXT("Add one Format Text argument pin."));
        AddBlueprintNodePinEditOperation(Operations, TEXT("removePin"), TEXT("argument"), TEXT("target"), TEXT("Remove one Format Text argument pin."));
        AddBlueprintNodePinEditOperation(Operations, TEXT("renamePin"), TEXT("argument"), TEXT("required"), TEXT("Rename one Format Text argument pin."));
        AddBlueprintNodePinEditOperation(Operations, TEXT("movePin"), TEXT("argument"), TEXT("target"), TEXT("Move one Format Text argument before or after another argument."));
        Notes.Add(MakeShared<FJsonValueString>(TEXT("FormatText arguments are node-local argument pins; format-string-derived pins may reconstruct.")));
    }
    else if (BlueprintNodeClassContains(Node, TEXT("K2Node_SetFieldsInStruct")))
    {
        AddBlueprintNodePinEditOperation(Operations, TEXT("removePin"), TEXT("field"), TEXT("target"), TEXT("Hide one struct field pin or all other visible field pins."));
        AddBlueprintNodePinEditOperation(Operations, TEXT("restorePins"), TEXT("field"), TEXT("generated"), TEXT("Restore all hidden struct field pins."));
        Notes.Add(MakeShared<FJsonValueString>(TEXT("SetFieldsInStruct edits field visibility; it does not create new schema fields.")));
    }

    Capabilities->SetArrayField(TEXT("pinOperations"), Operations);
    Capabilities->SetBoolField(TEXT("hasPinOperations"), Operations.Num() > 0);
    if (Notes.Num() > 0)
    {
        Capabilities->SetArrayField(TEXT("notes"), Notes);
    }
    return Capabilities;
}

TSharedPtr<FJsonObject> BuildBlueprintNodeInspectState(const TSharedPtr<FJsonObject>& Node)
{
    TSharedPtr<FJsonObject> State = MakeShared<FJsonObject>();
    if (BlueprintNodeClassContains(Node, TEXT("K2Node_Switch")))
    {
        State->SetArrayField(TEXT("casePins"), MakeBlueprintNodePinNamesByDirection(Node, TEXT("output"), true, true));
    }
    else if (BlueprintNodeClassContains(Node, TEXT("K2Node_ExecutionSequence")) || BlueprintNodeClassContains(Node, TEXT("K2Node_MultiGate")))
    {
        State->SetArrayField(TEXT("execPins"), MakeBlueprintNodePinNamesByDirection(Node, TEXT("output"), true, false));
    }
    else if (BlueprintNodeClassContains(Node, TEXT("K2Node_FormatText")))
    {
        State->SetArrayField(TEXT("argumentPins"), MakeBlueprintNodePinNamesByDirection(Node, TEXT("input"), false, false));
    }
    else if (BlueprintNodeClassContains(Node, TEXT("K2Node_Select")))
    {
        State->SetArrayField(TEXT("optionPins"), MakeBlueprintNodePinNamesByDirection(Node, TEXT("input"), false, false));
    }
    else if (BlueprintNodeClassContains(Node, TEXT("K2Node_SetFieldsInStruct")))
    {
        State->SetArrayField(TEXT("fieldPins"), MakeBlueprintNodePinNamesByDirection(Node, TEXT("input"), false, false));
    }
    else
    {
        State->SetArrayField(TEXT("inputPins"), MakeBlueprintNodePinNamesByDirection(Node, TEXT("input"), false, false));
    }
    return State;
}
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildBlueprintNodeInspectToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    FString AssetPath;
    FString GraphName;
    FString InlineNodeGuid;
    if (!BuildBlueprintQueryAddress(Arguments, AssetPath, GraphName, InlineNodeGuid, Result))
    {
        return Result;
    }

    FString NodeId;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("nodeId"), NodeId) || NodeId.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("blueprint.node.inspect requires nodeId."));
        return Result;
    }

    TSharedPtr<FJsonObject> QueryArgs = MakeShared<FJsonObject>();
    if (!InlineNodeGuid.IsEmpty())
    {
        QueryArgs->SetObjectField(TEXT("graphRef"), MakeBlueprintInlineGraphRef(InlineNodeGuid, AssetPath));
    }
    else
    {
        QueryArgs->SetStringField(TEXT("assetPath"), AssetPath);
        QueryArgs->SetStringField(TEXT("graphName"), GraphName);
    }
    TArray<TSharedPtr<FJsonValue>> NodeIds;
    NodeIds.Add(MakeShared<FJsonValueString>(NodeId));
    QueryArgs->SetArrayField(TEXT("nodeIds"), NodeIds);
    QueryArgs->SetNumberField(TEXT("limit"), 1);
    QueryArgs->SetBoolField(TEXT("includeConnections"), true);

    const TSharedPtr<FJsonObject> QueryResult = BuildBlueprintGraphInspectToolResult(QueryArgs);
    bool bQueryIsError = false;
    if (!QueryResult.IsValid() || (QueryResult->TryGetBoolField(TEXT("isError"), bQueryIsError) && bQueryIsError))
    {
        return QueryResult.IsValid() ? QueryResult : Result;
    }

    const TSharedPtr<FJsonObject>* Snapshot = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
    if (!QueryResult->TryGetObjectField(TEXT("semanticSnapshot"), Snapshot) || Snapshot == nullptr || !(*Snapshot).IsValid()
        || !(*Snapshot)->TryGetArrayField(TEXT("nodes"), Nodes) || Nodes == nullptr || Nodes->Num() == 0)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("NODE_NOT_FOUND"));
        Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Node not found in graph: %s"), *NodeId));
        return Result;
    }

    const TSharedPtr<FJsonObject>* Node = nullptr;
    if (!(*Nodes)[0].IsValid() || !(*Nodes)[0]->TryGetObject(Node) || Node == nullptr || !(*Node).IsValid())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), TEXT("blueprint.node.inspect resolved an invalid node payload."));
        return Result;
    }

    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("graphName"), GraphName);
    Result->SetObjectField(TEXT("graphRef"), MakeBlueprintEffectiveGraphRef(AssetPath, GraphName, InlineNodeGuid));
    Result->SetObjectField(TEXT("node"), *Node);
    const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
    if ((*Node)->TryGetArrayField(TEXT("pins"), Pins) && Pins != nullptr)
    {
        Result->SetArrayField(TEXT("pins"), *Pins);
    }
    else
    {
        Result->SetArrayField(TEXT("pins"), {});
    }
    Result->SetObjectField(TEXT("state"), BuildBlueprintNodeInspectState(*Node));
    Result->SetObjectField(TEXT("editCapabilities"), BuildBlueprintNodeEditCapabilities(*Node));
    return Result;
}

namespace
{
TSharedPtr<FJsonObject> MakeBlueprintNodeEditResult(bool bOk, bool bChanged, const FString& Operation, const FString& NodeId, const FString& ErrorCode = TEXT(""), const FString& ErrorMessage = TEXT(""))
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), !bOk);
    if (!bOk)
    {
        Result->SetStringField(TEXT("code"), ErrorCode.IsEmpty() ? TEXT("INTERNAL_ERROR") : ErrorCode);
        Result->SetStringField(TEXT("message"), ErrorMessage.IsEmpty() ? TEXT("blueprint.node.edit failed.") : ErrorMessage);
    }

    TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
    OpResult->SetNumberField(TEXT("index"), 0);
    OpResult->SetStringField(TEXT("op"), Operation);
    OpResult->SetBoolField(TEXT("ok"), bOk);
    OpResult->SetBoolField(TEXT("skipped"), false);
    OpResult->SetBoolField(TEXT("changed"), bChanged);
    OpResult->SetStringField(TEXT("nodeId"), NodeId);
    OpResult->SetStringField(TEXT("errorCode"), bOk ? TEXT("") : (ErrorCode.IsEmpty() ? TEXT("INTERNAL_ERROR") : ErrorCode));
    OpResult->SetStringField(TEXT("errorMessage"), bOk ? TEXT("") : ErrorMessage);
    TArray<TSharedPtr<FJsonValue>> OpResults;
    OpResults.Add(MakeShared<FJsonValueObject>(OpResult));
    Result->SetArrayField(TEXT("opResults"), OpResults);
    Result->SetArrayField(TEXT("commandResults"), OpResults);
    return Result;
}

FString ReadBlueprintNodeEditPinName(const TSharedPtr<FJsonObject>& Args)
{
    FString PinName;
    if (!Args.IsValid())
    {
        return PinName;
    }
    if (Args->TryGetStringField(TEXT("pin"), PinName) && !PinName.IsEmpty())
    {
        return PinName;
    }
    const TSharedPtr<FJsonObject>* Target = nullptr;
    if (Args->TryGetObjectField(TEXT("target"), Target) && Target != nullptr && (*Target).IsValid())
    {
        (*Target)->TryGetStringField(TEXT("pin"), PinName);
    }
    return PinName;
}

int32 FindFormatTextArgumentIndex(UK2Node_FormatText* FormatTextNode, const FString& PinName)
{
    if (FormatTextNode == nullptr)
    {
        return INDEX_NONE;
    }
    for (int32 Index = 0; Index < FormatTextNode->GetArgumentCount(); ++Index)
    {
        if (FormatTextNode->GetArgumentName(Index).ToString().Equals(PinName, ESearchCase::CaseSensitive))
        {
            return Index;
        }
    }
    return INDEX_NONE;
}

bool ReadBlueprintNodeEditTargetPinName(const TSharedPtr<FJsonObject>& Args, FString& OutPinName)
{
    OutPinName.Reset();
    if (!Args.IsValid())
    {
        return false;
    }
    const TSharedPtr<FJsonObject>* Target = nullptr;
    if (Args->TryGetObjectField(TEXT("target"), Target) && Target != nullptr && (*Target).IsValid())
    {
        (*Target)->TryGetStringField(TEXT("pin"), OutPinName);
    }
    return !OutPinName.IsEmpty();
}

void ReconstructBlueprintNodeAfterLocalEdit(UEdGraphNode* Node)
{
    if (Node != nullptr)
    {
        Node->ReconstructNode();
    }
}
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildBlueprintNodeEditToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    FString GuardAssetPath;
    FString GuardGraphName;
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("assetPath"), GuardAssetPath);
        Arguments->TryGetStringField(TEXT("graphName"), GuardGraphName);
        const TSharedPtr<FJsonObject>* GraphObject = nullptr;
        if (GuardGraphName.IsEmpty()
            && Arguments->TryGetObjectField(TEXT("graph"), GraphObject)
            && GraphObject != nullptr
            && (*GraphObject).IsValid())
        {
            (*GraphObject)->TryGetStringField(TEXT("name"), GuardGraphName);
        }
    }
    if (const TSharedPtr<FJsonObject> Blocked = BuildEditorMutationLifecycleBlockResult(
            TEXT("blueprint.node.edit"),
            Arguments,
            GuardAssetPath,
            GuardGraphName))
    {
        return Blocked;
    }

    FString AssetPath;
    FString GraphName;
    FString InlineNodeGuid;
    TSharedPtr<FJsonObject> AddressResult = MakeShared<FJsonObject>();
    AddressResult->SetBoolField(TEXT("isError"), false);
    if (!BuildBlueprintQueryAddress(Arguments, AssetPath, GraphName, InlineNodeGuid, AddressResult))
    {
        return AddressResult;
    }

    FString NodeId;
    FString Operation;
    if (!Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("nodeId"), NodeId) || NodeId.IsEmpty()
        || !Arguments->TryGetStringField(TEXT("operation"), Operation) || Operation.IsEmpty())
    {
        return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("INVALID_ARGUMENT"), TEXT("blueprint.node.edit requires nodeId and operation."));
    }

    const TSharedPtr<FJsonObject>* ArgsPtr = nullptr;
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    if (Arguments->TryGetObjectField(TEXT("args"), ArgsPtr) && ArgsPtr != nullptr && (*ArgsPtr).IsValid())
    {
        Args = *ArgsPtr;
    }
    bool bDryRun = false;
    Arguments->TryGetBoolField(TEXT("dryRun"), bDryRun);

    UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
    if (Blueprint == nullptr)
    {
        return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("ASSET_NOT_FOUND"), TEXT("Blueprint asset not found."));
    }
    UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, GraphName);
    if (TargetGraph == nullptr)
    {
        return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("GRAPH_NOT_FOUND"), TEXT("Blueprint graph not found."));
    }
    UEdGraphNode* TargetNode = ResolveBlueprintGraphNodeByToken(TargetGraph, NodeId);
    if (TargetNode == nullptr)
    {
        return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("NODE_NOT_FOUND"), TEXT("Blueprint graph node not found."));
    }

    if (bDryRun)
    {
        return MakeBlueprintNodeEditResult(true, false, Operation, NodeId);
    }

    auto FinalizeChanged = [&]()
    {
        TargetGraph->NotifyGraphChanged();
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
    };

    const FString OperationLower = Operation.ToLower();
    TargetGraph->Modify();
    TargetNode->Modify();
    Blueprint->Modify();

    if (OperationLower.Equals(TEXT("addpin")))
    {
        FString Role;
        Args->TryGetStringField(TEXT("role"), Role);
        FString Name;
        Args->TryGetStringField(TEXT("name"), Name);

        if (UK2Node_SwitchName* SwitchNameNode = Cast<UK2Node_SwitchName>(TargetNode))
        {
            const FName NewName = Name.IsEmpty() ? FName(*FString::Printf(TEXT("Case_%d"), SwitchNameNode->PinNames.Num())) : FName(*Name);
            if (SwitchNameNode->PinNames.Contains(NewName))
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("INVALID_ARGUMENT"), TEXT("Switch case already exists."));
            }
            SwitchNameNode->PinNames.Add(NewName);
            ReconstructBlueprintNodeAfterLocalEdit(TargetNode);
        }
        else if (UK2Node_SwitchString* SwitchStringNode = Cast<UK2Node_SwitchString>(TargetNode))
        {
            const FName NewName = Name.IsEmpty() ? FName(*FString::Printf(TEXT("Case_%d"), SwitchStringNode->PinNames.Num())) : FName(*Name);
            if (SwitchStringNode->PinNames.Contains(NewName))
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("INVALID_ARGUMENT"), TEXT("Switch case already exists."));
            }
            SwitchStringNode->PinNames.Add(NewName);
            ReconstructBlueprintNodeAfterLocalEdit(TargetNode);
        }
        else if (UK2Node_FormatText* FormatTextNode = Cast<UK2Node_FormatText>(TargetNode))
        {
            FormatTextNode->AddArgumentPin();
            if (!Name.IsEmpty() && FormatTextNode->GetArgumentCount() > 0)
            {
                FormatTextNode->SetArgumentName(FormatTextNode->GetArgumentCount() - 1, FName(*Name));
                ReconstructBlueprintNodeAfterLocalEdit(TargetNode);
            }
        }
        else if (UK2Node_Switch* SwitchNode = Cast<UK2Node_Switch>(TargetNode))
        {
            SwitchNode->AddPinToSwitchNode();
            ReconstructBlueprintNodeAfterLocalEdit(TargetNode);
        }
        else if (IK2Node_AddPinInterface* AddPinNode = Cast<IK2Node_AddPinInterface>(TargetNode))
        {
            if (!AddPinNode->CanAddPin())
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("UNSUPPORTED_NODE_OPERATION"), TEXT("This node cannot add another pin."));
            }
            AddPinNode->AddInputPin();
            ReconstructBlueprintNodeAfterLocalEdit(TargetNode);
        }
        else
        {
            return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("UNSUPPORTED_NODE_OPERATION"), TEXT("This node does not support addPin."));
        }
        FinalizeChanged();
        return MakeBlueprintNodeEditResult(true, true, Operation, NodeId);
    }

    if (OperationLower.Equals(TEXT("removepin")))
    {
        const FString PinName = ReadBlueprintNodeEditPinName(Args);
        UEdGraphPin* Pin = !PinName.IsEmpty() ? FindPinByName(TargetNode, PinName) : nullptr;

        if (UK2Node_Select* SelectNode = Cast<UK2Node_Select>(TargetNode))
        {
            if (!SelectNode->CanRemoveOptionPinToNode())
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("UNSUPPORTED_NODE_OPERATION"), TEXT("This Select node cannot remove another option pin."));
            }
            if (!PinName.IsEmpty() && Pin == nullptr)
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("PIN_NOT_FOUND"), TEXT("Pin not found."));
            }
            SelectNode->RemoveOptionPinToNode();
        }
        else if (UK2Node_SetFieldsInStruct* SetFieldsNode = Cast<UK2Node_SetFieldsInStruct>(TargetNode))
        {
            FString Mode;
            Args->TryGetStringField(TEXT("mode"), Mode);
            if (PinName.IsEmpty())
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("INVALID_ARGUMENT"), TEXT("SetFieldsInStruct removePin requires args.pin or args.target.pin."));
            }
            if (Pin == nullptr)
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("PIN_NOT_FOUND"), TEXT("Pin not found."));
            }
            if (!UK2Node_SetFieldsInStruct::ShowCustomPinActions(Pin, false))
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("UNSUPPORTED_PIN_OPERATION"), TEXT("This struct field pin cannot be hidden."));
            }
            SetFieldsNode->RemoveFieldPins(
                Pin,
                Mode.Equals(TEXT("otherPins"), ESearchCase::IgnoreCase)
                    ? UK2Node_SetFieldsInStruct::EPinsToRemove::AllOtherPins
                    : UK2Node_SetFieldsInStruct::EPinsToRemove::GivenPin);
        }
        else
        {
            if (PinName.IsEmpty())
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("INVALID_ARGUMENT"), TEXT("removePin requires args.pin or args.target.pin."));
            }
            if (Pin == nullptr)
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("PIN_NOT_FOUND"), TEXT("Pin not found."));
            }

            if (UK2Node_ExecutionSequence* SequenceNode = Cast<UK2Node_ExecutionSequence>(TargetNode))
        {
            if (!SequenceNode->CanRemoveExecutionPin())
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("UNSUPPORTED_NODE_OPERATION"), TEXT("This execution node cannot remove another pin."));
            }
            SequenceNode->RemovePinFromExecutionNode(Pin);
        }
        else if (UK2Node_Switch* SwitchNode = Cast<UK2Node_Switch>(TargetNode))
        {
            if (!SwitchNode->CanRemoveExecutionPin(Pin))
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("UNSUPPORTED_NODE_OPERATION"), TEXT("This switch pin cannot be removed."));
            }
            SwitchNode->RemovePinFromSwitchNode(Pin);
        }
        else if (UK2Node_FormatText* FormatTextNode = Cast<UK2Node_FormatText>(TargetNode))
        {
            const int32 Index = FindFormatTextArgumentIndex(FormatTextNode, PinName);
            if (Index == INDEX_NONE)
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("PIN_NOT_FOUND"), TEXT("FormatText argument not found."));
            }
            FormatTextNode->RemoveArgument(Index);
        }
        else if (IK2Node_AddPinInterface* AddPinNode = Cast<IK2Node_AddPinInterface>(TargetNode))
        {
            if (!AddPinNode->CanRemovePin(Pin))
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("UNSUPPORTED_NODE_OPERATION"), TEXT("This pin cannot be removed."));
            }
            AddPinNode->RemoveInputPin(Pin);
            ReconstructBlueprintNodeAfterLocalEdit(TargetNode);
        }
            else
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("UNSUPPORTED_NODE_OPERATION"), TEXT("This node does not support removePin."));
            }
        }
        FinalizeChanged();
        return MakeBlueprintNodeEditResult(true, true, Operation, NodeId);
    }

    if (OperationLower.Equals(TEXT("insertpin")))
    {
        const FString PinName = ReadBlueprintNodeEditPinName(Args);
        FString Position;
        Args->TryGetStringField(TEXT("position"), Position);
        if (PinName.IsEmpty())
        {
            return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("INVALID_ARGUMENT"), TEXT("insertPin requires args.target.pin."));
        }
        UK2Node_ExecutionSequence* SequenceNode = Cast<UK2Node_ExecutionSequence>(TargetNode);
        UEdGraphPin* Pin = FindPinByName(TargetNode, PinName);
        if (SequenceNode == nullptr)
        {
            return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("UNSUPPORTED_NODE_OPERATION"), TEXT("insertPin is only supported by Sequence-style execution nodes."));
        }
        if (Pin == nullptr)
        {
            return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("PIN_NOT_FOUND"), TEXT("Insert anchor pin not found."));
        }
        SequenceNode->InsertPinIntoExecutionNode(Pin, Position.Equals(TEXT("before"), ESearchCase::IgnoreCase) ? EPinInsertPosition::Before : EPinInsertPosition::After);
        FinalizeChanged();
        return MakeBlueprintNodeEditResult(true, true, Operation, NodeId);
    }

    if (OperationLower.Equals(TEXT("renamepin")))
    {
        const FString PinName = ReadBlueprintNodeEditPinName(Args);
        FString Name;
        Args->TryGetStringField(TEXT("name"), Name);
        if (PinName.IsEmpty() || Name.IsEmpty())
        {
            return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("INVALID_ARGUMENT"), TEXT("renamePin requires args.pin and args.name."));
        }

        if (UK2Node_SwitchName* SwitchNameNode = Cast<UK2Node_SwitchName>(TargetNode))
        {
            const int32 Index = SwitchNameNode->PinNames.IndexOfByKey(FName(*PinName));
            if (Index == INDEX_NONE)
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("PIN_NOT_FOUND"), TEXT("Switch case not found."));
            }
            SwitchNameNode->PinNames[Index] = FName(*Name);
            ReconstructBlueprintNodeAfterLocalEdit(TargetNode);
        }
        else if (UK2Node_SwitchString* SwitchStringNode = Cast<UK2Node_SwitchString>(TargetNode))
        {
            const int32 Index = SwitchStringNode->PinNames.IndexOfByKey(FName(*PinName));
            if (Index == INDEX_NONE)
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("PIN_NOT_FOUND"), TEXT("Switch case not found."));
            }
            SwitchStringNode->PinNames[Index] = FName(*Name);
            ReconstructBlueprintNodeAfterLocalEdit(TargetNode);
        }
        else if (UK2Node_FormatText* FormatTextNode = Cast<UK2Node_FormatText>(TargetNode))
        {
            const int32 Index = FindFormatTextArgumentIndex(FormatTextNode, PinName);
            if (Index == INDEX_NONE)
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("PIN_NOT_FOUND"), TEXT("FormatText argument not found."));
            }
            FormatTextNode->SetArgumentName(Index, FName(*Name));
            ReconstructBlueprintNodeAfterLocalEdit(TargetNode);
        }
        else
        {
            return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("UNSUPPORTED_NODE_OPERATION"), TEXT("This node does not support renamePin."));
        }
        FinalizeChanged();
        return MakeBlueprintNodeEditResult(true, true, Operation, NodeId);
    }

    if (OperationLower.Equals(TEXT("movepin")))
    {
        const FString PinName = ReadBlueprintNodeEditPinName(Args);
        FString TargetPinName;
        FString Position;
        Args->TryGetStringField(TEXT("position"), Position);
        if (PinName.IsEmpty() || !ReadBlueprintNodeEditTargetPinName(Args, TargetPinName))
        {
            return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("INVALID_ARGUMENT"), TEXT("movePin requires args.pin and args.target.pin."));
        }

        if (UK2Node_FormatText* FormatTextNode = Cast<UK2Node_FormatText>(TargetNode))
        {
            int32 FromIndex = FindFormatTextArgumentIndex(FormatTextNode, PinName);
            int32 TargetIndex = FindFormatTextArgumentIndex(FormatTextNode, TargetPinName);
            if (FromIndex == INDEX_NONE || TargetIndex == INDEX_NONE)
            {
                return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("PIN_NOT_FOUND"), TEXT("FormatText movePin argument not found."));
            }
            if (FromIndex == TargetIndex)
            {
                return MakeBlueprintNodeEditResult(true, false, Operation, NodeId);
            }

            int32 DesiredIndex = TargetIndex;
            if (Position.Equals(TEXT("after"), ESearchCase::IgnoreCase))
            {
                DesiredIndex = TargetIndex + 1;
            }
            if (FromIndex < DesiredIndex)
            {
                --DesiredIndex;
            }

            while (FromIndex < DesiredIndex)
            {
                FormatTextNode->SwapArguments(FromIndex, FromIndex + 1);
                ++FromIndex;
            }
            while (FromIndex > DesiredIndex)
            {
                FormatTextNode->SwapArguments(FromIndex, FromIndex - 1);
                --FromIndex;
            }
            ReconstructBlueprintNodeAfterLocalEdit(TargetNode);
        }
        else
        {
            return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("UNSUPPORTED_NODE_OPERATION"), TEXT("This node does not support movePin."));
        }
        FinalizeChanged();
        return MakeBlueprintNodeEditResult(true, true, Operation, NodeId);
    }

    if (OperationLower.Equals(TEXT("restorepins")))
    {
        if (UK2Node_SetFieldsInStruct* SetFieldsNode = Cast<UK2Node_SetFieldsInStruct>(TargetNode))
        {
            if (SetFieldsNode->AllPinsAreShown())
            {
                return MakeBlueprintNodeEditResult(true, false, Operation, NodeId);
            }
            SetFieldsNode->RestoreAllPins();
        }
        else
        {
            return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("UNSUPPORTED_NODE_OPERATION"), TEXT("This node does not support restorePins."));
        }
        FinalizeChanged();
        return MakeBlueprintNodeEditResult(true, true, Operation, NodeId);
    }

    return MakeBlueprintNodeEditResult(false, false, Operation, NodeId, TEXT("INVALID_ARGUMENT"), TEXT("Unsupported blueprint.node.edit operation."));
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildBlueprintGraphEditToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    FString GuardAssetPath;
    FString GuardGraphName;
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("assetPath"), GuardAssetPath);
        Arguments->TryGetStringField(TEXT("graphName"), GuardGraphName);
        const TSharedPtr<FJsonObject>* GraphObject = nullptr;
        if (GuardGraphName.IsEmpty()
            && Arguments->TryGetObjectField(TEXT("graph"), GraphObject)
            && GraphObject != nullptr
            && (*GraphObject).IsValid())
        {
            (*GraphObject)->TryGetStringField(TEXT("name"), GuardGraphName);
        }
    }
    if (const TSharedPtr<FJsonObject> Blocked = BuildEditorMutationLifecycleBlockResult(
            TEXT("blueprint.graph.edit"),
            Arguments,
            GuardAssetPath,
            GuardGraphName))
    {
        return Blocked;
    }

    TSharedPtr<FJsonObject> AddressResult = MakeShared<FJsonObject>();
    AddressResult->SetBoolField(TEXT("isError"), false);

    FString AssetPath;
    FString GraphName;
    FString InlineNodeGuid;
    if (!BuildBlueprintQueryAddress(Arguments, AssetPath, GraphName, InlineNodeGuid, AddressResult))
    {
        return AddressResult;
    }

    const TArray<TSharedPtr<FJsonValue>>* Ops = nullptr;
    if (!Arguments.IsValid() || !Arguments->TryGetArrayField(TEXT("ops"), Ops) || Ops == nullptr)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.ops must be an array."));
        return Result;
    }

    if (Arguments.IsValid() && Ops != nullptr)
    {
        for (int32 Index = 0; Index < Ops->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>* OpObj = nullptr;
            if (!(*Ops)[Index].IsValid() || !(*Ops)[Index]->TryGetObject(OpObj) || OpObj == nullptr || !(*OpObj).IsValid())
            {
                continue;
            }

            FString Op;
            (*OpObj)->TryGetStringField(TEXT("op"), Op);
            Op = Op.ToLower();
            if (!Op.Equals(TEXT("runscript")))
            {
                continue;
            }

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_OP"));
            Result->SetStringField(TEXT("message"), TEXT("blueprint.graph.edit does not support runScript."));
            Result->SetBoolField(TEXT("applied"), false);
            Result->SetBoolField(TEXT("partialApplied"), false);

            FString ErrorAssetPath;
            if (Arguments->TryGetStringField(TEXT("assetPath"), ErrorAssetPath) && !ErrorAssetPath.IsEmpty())
            {
                Result->SetStringField(TEXT("assetPath"), NormalizeAssetPath(ErrorAssetPath));
            }

            FString ErrorGraphName;
            if (Arguments->TryGetStringField(TEXT("graphName"), ErrorGraphName) && !ErrorGraphName.IsEmpty())
            {
                Result->SetStringField(TEXT("graphName"), ErrorGraphName);
            }

            Result->SetStringField(TEXT("graphType"), TEXT("blueprint"));
            Result->SetStringField(TEXT("previousRevision"), TEXT(""));
            Result->SetStringField(TEXT("newRevision"), TEXT(""));

            TArray<TSharedPtr<FJsonValue>> OpResults;
            TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
            OpResult->SetNumberField(TEXT("index"), Index);
            OpResult->SetStringField(TEXT("op"), Op);
            OpResult->SetBoolField(TEXT("ok"), false);
            OpResult->SetBoolField(TEXT("skipped"), false);
            OpResult->SetStringField(TEXT("errorCode"), TEXT("UNSUPPORTED_OP"));
            OpResult->SetStringField(TEXT("errorMessage"), TEXT("blueprint.graph.edit does not support runScript."));
            OpResults.Add(MakeShared<FJsonValueObject>(OpResult));
            Result->SetArrayField(TEXT("opResults"), OpResults);

            TArray<TSharedPtr<FJsonValue>> Diagnostics;
            TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
            Diagnostic->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_OP"));
            Diagnostic->SetStringField(TEXT("severity"), TEXT("error"));
            Diagnostic->SetStringField(TEXT("message"), TEXT("blueprint.graph.edit does not support runScript."));
            Diagnostic->SetStringField(TEXT("sourceKind"), TEXT("graph.edit"));
            Diagnostic->SetStringField(TEXT("op"), Op);
            Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
            Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
            return Result;
        }
    }

    FString ExpectedRevision;
    if (Arguments->TryGetStringField(TEXT("expectedRevision"), ExpectedRevision) && !ExpectedRevision.IsEmpty())
    {
        TSharedPtr<FJsonObject> RevisionQueryArgs = MakeShared<FJsonObject>();
        if (!InlineNodeGuid.IsEmpty())
        {
            RevisionQueryArgs->SetObjectField(TEXT("graphRef"), MakeBlueprintInlineGraphRef(InlineNodeGuid, AssetPath));
        }
        else
        {
            RevisionQueryArgs->SetStringField(TEXT("assetPath"), AssetPath);
            RevisionQueryArgs->SetStringField(TEXT("graphName"), GraphName);
        }

        const TSharedPtr<FJsonObject> RevisionResult = BuildBlueprintGraphInspectToolResult(RevisionQueryArgs);
        if (!RevisionResult.IsValid())
        {
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
            Result->SetStringField(TEXT("message"), TEXT("Failed to resolve current blueprint revision."));
            return Result;
        }

        bool bRevisionError = false;
        RevisionResult->TryGetBoolField(TEXT("isError"), bRevisionError);
        if (bRevisionError)
        {
            return RevisionResult;
        }

        FString CurrentRevision;
        if (!RevisionResult->TryGetStringField(TEXT("revision"), CurrentRevision) || CurrentRevision.IsEmpty())
        {
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
            Result->SetStringField(TEXT("message"), TEXT("blueprint.graph.inspect did not return a revision for blueprint.graph.edit."));
            return Result;
        }

        if (!ExpectedRevision.Equals(CurrentRevision, ESearchCase::CaseSensitive))
        {
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("REVISION_CONFLICT"));
            Result->SetStringField(
                TEXT("message"),
                FString::Printf(TEXT("expectedRevision mismatch: expected %s but current revision is %s."), *ExpectedRevision, *CurrentRevision));
            Result->SetBoolField(TEXT("applied"), false);
            Result->SetBoolField(TEXT("partialApplied"), false);
            Result->SetStringField(TEXT("graphType"), TEXT("blueprint"));
            Result->SetStringField(TEXT("assetPath"), AssetPath);
            Result->SetStringField(TEXT("graphName"), GraphName);
            Result->SetObjectField(TEXT("graphRef"), MakeBlueprintEffectiveGraphRef(AssetPath, GraphName, InlineNodeGuid));
            Result->SetStringField(TEXT("previousRevision"), CurrentRevision);
            Result->SetStringField(TEXT("newRevision"), CurrentRevision);
            Result->SetArrayField(TEXT("opResults"), TArray<TSharedPtr<FJsonValue>>{});
            Result->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
            return Result;
        }
    }

    FString IdempotencyKey;
    Arguments->TryGetStringField(TEXT("idempotencyKey"), IdempotencyKey);
    IdempotencyKey = IdempotencyKey.TrimStartAndEnd();

    const FString IdempotencyRegistryKey = IdempotencyKey.IsEmpty()
        ? FString()
        : FString::Printf(
            TEXT("blueprint|%s|%s|%s|%s"),
            *AssetPath,
            *GraphName,
            *InlineNodeGuid,
            *IdempotencyKey);
    FString RequestFingerprint;
    if (!IdempotencyRegistryKey.IsEmpty())
    {
        TSharedPtr<FJsonObject> FingerprintSource = CloneJsonObject(Arguments);
        if (!FingerprintSource.IsValid())
        {
            FingerprintSource = MakeShared<FJsonObject>();
            if (Arguments.IsValid())
            {
                for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Arguments->Values)
                {
                    FingerprintSource->SetField(Field.Key, Field.Value);
                }
            }
        }
        FingerprintSource->RemoveField(TEXT("idempotencyKey"));
        RequestFingerprint = SerializeBlueprintJsonObjectCondensed(FingerprintSource);

        {
            const double NowSeconds = FPlatformTime::Seconds();
            FScopeLock Lock(&MutateIdempotencyRegistryMutex);

            TArray<FString> ExpiredKeys;
            for (const TPair<FString, FMutateIdempotencyEntry>& Entry : MutateIdempotencyRegistry)
            {
                if ((NowSeconds - Entry.Value.CreatedAtSeconds) > LoomleBridgeConstants::MutateIdempotencyTtlSeconds)
                {
                    ExpiredKeys.Add(Entry.Key);
                }
            }
            for (const FString& ExpiredKey : ExpiredKeys)
            {
                MutateIdempotencyRegistry.Remove(ExpiredKey);
            }

            if (const FMutateIdempotencyEntry* Existing = MutateIdempotencyRegistry.Find(IdempotencyRegistryKey))
            {
                if (!Existing->RequestFingerprint.Equals(RequestFingerprint, ESearchCase::CaseSensitive))
                {
                    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
                    Result->SetBoolField(TEXT("isError"), true);
                    Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
                    Result->SetStringField(
                        TEXT("message"),
                        TEXT("idempotencyKey was already used for a different blueprint.graph.edit request in this graph scope."));
                    return Result;
                }

                if (Existing->Result.IsValid())
                {
                    TSharedPtr<FJsonObject> ReplayResult = CloneJsonObject(Existing->Result);
                    if (ReplayResult.IsValid())
                    {
                        return ReplayResult;
                    }
                }
            }
        }
    }

    auto BuildRevisionQueryArgs = [&](const FString& EffectiveGraphName, const FString& EffectiveInlineNodeGuid) -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> QueryArgs = MakeShared<FJsonObject>();
        if (!EffectiveInlineNodeGuid.IsEmpty())
        {
            QueryArgs->SetObjectField(TEXT("graphRef"), MakeBlueprintInlineGraphRef(EffectiveInlineNodeGuid, AssetPath));
        }
        else
        {
            QueryArgs->SetStringField(TEXT("assetPath"), AssetPath);
            QueryArgs->SetStringField(TEXT("graphName"), EffectiveGraphName);
        }
        return QueryArgs;
    };

    auto ResolveRevision = [&](const FString& EffectiveGraphName, const FString& EffectiveInlineNodeGuid, FString& OutRevision, FString& OutCode, FString& OutMessage) -> bool
    {
        OutRevision.Empty();
        OutCode.Empty();
        OutMessage.Empty();

        const TSharedPtr<FJsonObject> RevisionResult = BuildBlueprintGraphInspectToolResult(BuildRevisionQueryArgs(EffectiveGraphName, EffectiveInlineNodeGuid));
        if (!RevisionResult.IsValid())
        {
            OutCode = TEXT("INTERNAL_ERROR");
            OutMessage = TEXT("Failed to resolve current blueprint revision.");
            return false;
        }

        bool bRevisionError = false;
        RevisionResult->TryGetBoolField(TEXT("isError"), bRevisionError);
        if (bRevisionError)
        {
            RevisionResult->TryGetStringField(TEXT("code"), OutCode);
            RevisionResult->TryGetStringField(TEXT("message"), OutMessage);
            if (OutCode.IsEmpty())
            {
                OutCode = TEXT("INTERNAL_ERROR");
            }
            if (OutMessage.IsEmpty())
            {
                OutMessage = TEXT("Failed to resolve current blueprint revision.");
            }
            return false;
        }

        if (!RevisionResult->TryGetStringField(TEXT("revision"), OutRevision) || OutRevision.IsEmpty())
        {
            OutCode = TEXT("INTERNAL_ERROR");
            OutMessage = TEXT("blueprint.graph.inspect did not return a revision for blueprint.graph.edit.");
            return false;
        }

        return true;
    };

    FString PreviousRevision;
    FString PreviousRevisionCode;
    FString PreviousRevisionMessage;
    if (!ResolveRevision(GraphName, InlineNodeGuid, PreviousRevision, PreviousRevisionCode, PreviousRevisionMessage))
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), PreviousRevisionCode);
        Result->SetStringField(TEXT("message"), PreviousRevisionMessage);
        return Result;
    }

    bool bStopOnError = true;
    bool bContinueOnError = false;
    Arguments->TryGetBoolField(TEXT("continueOnError"), bContinueOnError);
    if (bContinueOnError)
    {
        bStopOnError = false;
    }

    int32 MaxOps = 200;
    if (const TSharedPtr<FJsonObject>* ExecutionPolicy = nullptr;
        Arguments->TryGetObjectField(TEXT("executionPolicy"), ExecutionPolicy) && ExecutionPolicy && (*ExecutionPolicy).IsValid())
    {
        bool StopOnErrorValue = true;
        if ((*ExecutionPolicy)->TryGetBoolField(TEXT("stopOnError"), StopOnErrorValue))
        {
            bStopOnError = StopOnErrorValue;
        }
        double MaxOpsNumber = 0.0;
        if ((*ExecutionPolicy)->TryGetNumberField(TEXT("maxOps"), MaxOpsNumber))
        {
            MaxOps = FMath::Clamp(static_cast<int32>(MaxOpsNumber), 1, 200);
        }
    }

    if (Ops->Num() > MaxOps)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("LIMIT_EXCEEDED"));
        Result->SetStringField(TEXT("message"), FString::Printf(TEXT("arguments.ops exceeds executionPolicy.maxOps (%d)."), MaxOps));
        return Result;
    }

    bool bDryRun = false;
    Arguments->TryGetBoolField(TEXT("dryRun"), bDryRun);

    TMap<FString, FString> NodeRefs;
    TArray<TSharedPtr<FJsonValue>> OpResults;
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    bool bAnyError = false;
    bool bAnyChanged = false;
    FString FirstErrorCode;
    FString FirstErrorMessage;

    struct FBlueprintGraphEditResolvedGraph
    {
        UBlueprint* Blueprint = nullptr;
        UEdGraph* Graph = nullptr;
    };

    TMap<FString, FBlueprintGraphEditResolvedGraph> ResolvedGraphCache;
    TSet<UEdGraph*> DeferredNotifyGraphs;
    TSet<UBlueprint*> DeferredModifiedBlueprints;

    auto ResolveCachedGraph = [&](const FString& EffectiveGraphName, FBlueprintGraphEditResolvedGraph& OutResolved, FString& OutError) -> bool
    {
        const FString CacheKey = FString::Printf(TEXT("%s|%s"), *AssetPath, *EffectiveGraphName);
        if (const FBlueprintGraphEditResolvedGraph* Existing = ResolvedGraphCache.Find(CacheKey))
        {
            OutResolved = *Existing;
            return OutResolved.Blueprint != nullptr && OutResolved.Graph != nullptr;
        }

        FBlueprintGraphEditResolvedGraph Resolved;
        Resolved.Blueprint = LoadBlueprintByAssetPath(AssetPath);
        Resolved.Graph = ResolveBlueprintGraph(Resolved.Blueprint, EffectiveGraphName);
        ResolvedGraphCache.Add(CacheKey, Resolved);
        OutResolved = Resolved;
        if (Resolved.Blueprint == nullptr || Resolved.Graph == nullptr)
        {
            OutError = TEXT("Failed to resolve blueprint/target graph.");
            return false;
        }
        return true;
    };

    auto DeferGraphModified = [&](UBlueprint* Blueprint, UEdGraph* Graph)
    {
        if (Blueprint != nullptr && Graph != nullptr && !bDryRun)
        {
            DeferredNotifyGraphs.Add(Graph);
            DeferredModifiedBlueprints.Add(Blueprint);
        }
    };

    auto FlushDeferredGraphNotifications = [&]()
    {
        for (UEdGraph* Graph : DeferredNotifyGraphs)
        {
            if (Graph != nullptr)
            {
                Graph->NotifyGraphChanged();
            }
        }
        for (UBlueprint* Blueprint : DeferredModifiedBlueprints)
        {
            if (Blueprint != nullptr)
            {
                FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
                Blueprint->MarkPackageDirty();
            }
        }
        DeferredNotifyGraphs.Empty();
        DeferredModifiedBlueprints.Empty();
    };

    struct FPaletteRequestCacheScope
    {
        int32 CacheId = INDEX_NONE;

        FPaletteRequestCacheScope()
            : CacheId(FLoomleBlueprintAdapter::BeginPaletteRequestCache())
        {
        }

        ~FPaletteRequestCacheScope()
        {
            FLoomleBlueprintAdapter::EndPaletteRequestCache(CacheId);
        }
    };

    FPaletteRequestCacheScope PaletteRequestCache;

    for (int32 Index = 0; Index < Ops->Num(); ++Index)
    {
        const TSharedPtr<FJsonObject>* OpObj = nullptr;
        if (!(*Ops)[Index].IsValid() || !(*Ops)[Index]->TryGetObject(OpObj) || OpObj == nullptr || !(*OpObj).IsValid())
        {
            continue;
        }

        const double OpStartSeconds = FPlatformTime::Seconds();

        TSharedPtr<FJsonObject> SingleOp = CloneJsonObject(*OpObj);
        if (!SingleOp.IsValid())
        {
            SingleOp = MakeShared<FJsonObject>();
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*OpObj)->Values)
            {
                SingleOp->SetField(Field.Key, Field.Value);
            }
        }

        FString OpName;
        SingleOp->TryGetStringField(TEXT("op"), OpName);
        OpName = OpName.ToLower();

        FString ClientRef;
        SingleOp->TryGetStringField(TEXT("clientRef"), ClientRef);

        const TSharedPtr<FJsonObject>* ArgsObjPtr = nullptr;
        TSharedPtr<FJsonObject> SingleArgsObject =
            (SingleOp->TryGetObjectField(TEXT("args"), ArgsObjPtr) && ArgsObjPtr != nullptr && (*ArgsObjPtr).IsValid())
                ? CloneJsonObject(*ArgsObjPtr)
                : MakeShared<FJsonObject>();
        if (!SingleArgsObject.IsValid())
        {
            SingleArgsObject = MakeShared<FJsonObject>();
        }

        TFunction<void(const TSharedPtr<FJsonObject>&)> RewriteObjectRefs;
        RewriteObjectRefs = [&](const TSharedPtr<FJsonObject>& Object)
        {
            if (!Object.IsValid())
            {
                return;
            }

            FString NodeRef;
            if (Object->TryGetStringField(TEXT("nodeRef"), NodeRef) && !NodeRef.IsEmpty())
            {
                if (const FString* ResolvedNodeId = NodeRefs.Find(NodeRef))
                {
                    Object->SetStringField(TEXT("nodeId"), *ResolvedNodeId);
                }
            }

            const TArray<TSharedPtr<FJsonValue>>* NodeIds = nullptr;
            if (Object->TryGetArrayField(TEXT("nodeIds"), NodeIds) && NodeIds != nullptr)
            {
                TArray<TSharedPtr<FJsonValue>> RewrittenNodeIds;
                RewrittenNodeIds.Reserve(NodeIds->Num());
                for (const TSharedPtr<FJsonValue>& NodeIdValue : *NodeIds)
                {
                    FString NodeId;
                    if (NodeIdValue.IsValid() && NodeIdValue->TryGetString(NodeId))
                    {
                        if (const FString* ResolvedNodeId = NodeRefs.Find(NodeId))
                        {
                            RewrittenNodeIds.Add(MakeShared<FJsonValueString>(*ResolvedNodeId));
                        }
                        else
                        {
                            RewrittenNodeIds.Add(NodeIdValue);
                        }
                    }
                    else
                    {
                        RewrittenNodeIds.Add(NodeIdValue);
                    }
                }
                Object->SetArrayField(TEXT("nodeIds"), RewrittenNodeIds);
            }

            for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
            {
                if (!Field.Value.IsValid())
                {
                    continue;
                }

                const TSharedPtr<FJsonObject>* ChildObject = nullptr;
                if (Field.Value->TryGetObject(ChildObject) && ChildObject != nullptr && (*ChildObject).IsValid())
                {
                    RewriteObjectRefs(*ChildObject);
                    continue;
                }

                const TArray<TSharedPtr<FJsonValue>>* ChildArray = nullptr;
                if (Field.Value->TryGetArray(ChildArray) && ChildArray != nullptr)
                {
                    for (const TSharedPtr<FJsonValue>& ChildValue : *ChildArray)
                    {
                        const TSharedPtr<FJsonObject>* ArrayObject = nullptr;
                        if (ChildValue.IsValid() && ChildValue->TryGetObject(ArrayObject) && ArrayObject != nullptr && (*ArrayObject).IsValid())
                        {
                            RewriteObjectRefs(*ArrayObject);
                        }
                    }
                }
            }
        };
        RewriteObjectRefs(SingleArgsObject);
        SingleOp->SetObjectField(TEXT("args"), SingleArgsObject);

        FString EffectiveGraphName = GraphName;
        FString EffectiveInlineNodeGuid = InlineNodeGuid;
        const TSharedPtr<FJsonObject>* TargetGraphRefObj = nullptr;
        const bool bHasTargetGraphRef = SingleOp->TryGetObjectField(TEXT("targetGraphRef"), TargetGraphRefObj)
            && TargetGraphRefObj != nullptr
            && (*TargetGraphRefObj).IsValid();

        const TSharedPtr<FJsonObject>* ArgsGraphRefObj = nullptr;
        const bool bHasArgsGraphRef = SingleArgsObject->TryGetObjectField(TEXT("graphRef"), ArgsGraphRefObj)
            && ArgsGraphRefObj != nullptr
            && (*ArgsGraphRefObj).IsValid();

        FString TargetGraphName;
        const bool bHasTargetGraphName = SingleOp->TryGetStringField(TEXT("targetGraphName"), TargetGraphName) && !TargetGraphName.IsEmpty();

        TSharedPtr<FJsonObject> EffectiveGraphRef;
        if (bHasTargetGraphRef && bHasTargetGraphName)
        {
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), TEXT("Supply either targetGraphRef/args.graphRef or targetGraphName, not both."));
            FlushDeferredGraphNotifications();
            return Result;
        }
        else if (bHasTargetGraphRef)
        {
            EffectiveGraphRef = CloneJsonObject(*TargetGraphRefObj);
            EffectiveInlineNodeGuid.Empty();
            EffectiveGraphName.Empty();
        }
        else if (bHasArgsGraphRef)
        {
            EffectiveGraphRef = CloneJsonObject(*ArgsGraphRefObj);
            EffectiveInlineNodeGuid.Empty();
            EffectiveGraphName.Empty();
        }
        else if (bHasTargetGraphName)
        {
            EffectiveGraphName = TargetGraphName;
            EffectiveInlineNodeGuid.Empty();
        }
        else if (!EffectiveInlineNodeGuid.IsEmpty())
        {
            EffectiveGraphRef = MakeBlueprintInlineGraphRef(EffectiveInlineNodeGuid, AssetPath);
        }

        SingleOp->RemoveField(TEXT("targetGraphName"));
        SingleOp->RemoveField(TEXT("targetGraphRef"));
        SingleArgsObject->RemoveField(TEXT("graphRef"));
        SingleOp->SetObjectField(TEXT("args"), SingleArgsObject);

        TFunction<bool(const TSharedPtr<FJsonObject>&, FString&)> ResolveSingleNodeToken;
        ResolveSingleNodeToken = [&](const TSharedPtr<FJsonObject>& Object, FString& OutNodeToken) -> bool
        {
            OutNodeToken.Empty();
            if (!Object.IsValid())
            {
                return false;
            }
            const TSharedPtr<FJsonObject>* NestedNode = nullptr;
            if (Object->TryGetObjectField(TEXT("node"), NestedNode) && NestedNode != nullptr && (*NestedNode).IsValid())
            {
                return ResolveSingleNodeToken(*NestedNode, OutNodeToken);
            }

            FString Candidate;
            const bool bHasToken = (Object->TryGetStringField(TEXT("nodeId"), Candidate) && !Candidate.IsEmpty())
                || (Object->TryGetStringField(TEXT("nodeRef"), Candidate) && !Candidate.IsEmpty())
                || (Object->TryGetStringField(TEXT("alias"), Candidate) && !Candidate.IsEmpty())
                || (Object->TryGetStringField(TEXT("nodePath"), Candidate) && !Candidate.IsEmpty())
                || (Object->TryGetStringField(TEXT("path"), Candidate) && !Candidate.IsEmpty())
                || (Object->TryGetStringField(TEXT("nodeName"), Candidate) && !Candidate.IsEmpty())
                || (Object->TryGetStringField(TEXT("name"), Candidate) && !Candidate.IsEmpty());
            if (!bHasToken)
            {
                return false;
            }
            if (const FString* ResolvedNodeId = NodeRefs.Find(Candidate))
            {
                OutNodeToken = *ResolvedNodeId;
            }
            else
            {
                OutNodeToken = Candidate;
            }
            return true;
        };

        auto ReadIntField = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, int32 DefaultValue) -> int32
        {
            if (!Object.IsValid())
            {
                return DefaultValue;
            }

            double Number = 0.0;
            if (Object->TryGetNumberField(FieldName, Number))
            {
                return static_cast<int32>(Number);
            }
            return DefaultValue;
        };

        auto HasExplicitPosition = [](const TSharedPtr<FJsonObject>& Object) -> bool
        {
            return Object.IsValid() && Object->HasTypedField<EJson::Object>(TEXT("position"));
        };

        auto ReadPoint = [](const TSharedPtr<FJsonObject>& Object, int32& OutX, int32& OutY)
        {
            OutX = 0;
            OutY = 0;
            if (!Object.IsValid())
            {
                return;
            }

            const TSharedPtr<FJsonObject>* Position = nullptr;
            if (Object->TryGetObjectField(TEXT("position"), Position) && Position != nullptr && (*Position).IsValid())
            {
                double Xn = 0.0;
                double Yn = 0.0;
                (*Position)->TryGetNumberField(TEXT("x"), Xn);
                (*Position)->TryGetNumberField(TEXT("y"), Yn);
                OutX = static_cast<int32>(Xn);
                OutY = static_cast<int32>(Yn);
            }
        };

        auto ResolvePinName = [](const TSharedPtr<FJsonObject>& Object, FString& OutPinName) -> bool
        {
            OutPinName.Empty();
            if (!Object.IsValid())
            {
                return false;
            }
            return (Object->TryGetStringField(TEXT("pin"), OutPinName) && !OutPinName.IsEmpty())
                || (Object->TryGetStringField(TEXT("pinName"), OutPinName) && !OutPinName.IsEmpty());
        };

        auto ResolvePinEndpoint = [&](const TSharedPtr<FJsonObject>& Parent, const TCHAR* FieldName, FString& OutNodeId, FString& OutPinName) -> bool
        {
            OutNodeId.Empty();
            OutPinName.Empty();
            const TSharedPtr<FJsonObject>* EndpointObj = nullptr;
            if (!Parent.IsValid()
                || !Parent->TryGetObjectField(FieldName, EndpointObj)
                || EndpointObj == nullptr
                || !(*EndpointObj).IsValid())
            {
                return false;
            }
            return ResolveSingleNodeToken(*EndpointObj, OutNodeId) && ResolvePinName(*EndpointObj, OutPinName);
        };

        auto SerializeForMutate = [](const TSharedPtr<FJsonObject>& Object) -> FString
        {
            if (!Object.IsValid())
            {
                return TEXT("{}");
            }

            FString Out;
            const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
            FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
            return Out;
        };

        auto ResolveInsertionPoint = [&](const TSharedPtr<FJsonObject>& Object, int32& OutX, int32& OutY) -> bool
        {
            OutX = 0;
            OutY = 0;

            if (HasExplicitPosition(Object))
            {
                ReadPoint(Object, OutX, OutY);
                return true;
            }

            UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
            UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, EffectiveGraphName);
            if (Blueprint == nullptr || TargetGraph == nullptr)
            {
                return false;
            }

            auto EstimateNodeSize = [](const UEdGraphNode* Node) -> FVector2D
            {
                if (const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
                {
                    return FVector2D(
                        FMath::Max(160.0f, static_cast<float>(CommentNode->NodeWidth)),
                        FMath::Max(120.0f, static_cast<float>(CommentNode->NodeHeight)));
                }
                return FVector2D(280.0f, 160.0f);
            };

            auto ResolveGraphNodeByTokenLocal = [](UEdGraph* Graph, const FString& NodeToken) -> UEdGraphNode*
            {
                if (Graph == nullptr || NodeToken.IsEmpty())
                {
                    return nullptr;
                }

                if (UEdGraphNode* Node = FindNodeByGuid(Graph, NodeToken))
                {
                    return Node;
                }

                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    if (Node == nullptr)
                    {
                        continue;
                    }
                    if (Node->GetPathName().Equals(NodeToken, ESearchCase::IgnoreCase)
                        || Node->GetName().Equals(NodeToken, ESearchCase::IgnoreCase))
                    {
                        return Node;
                    }
                }
                return nullptr;
            };

            UEdGraphNode* AnchorNode = nullptr;
            FString AnchorPinName;
            auto TryResolveAnchorField = [&](const TCHAR* FieldName) -> bool
            {
                const TSharedPtr<FJsonObject>* AnchorObj = nullptr;
                if (!Object.IsValid()
                    || !Object->TryGetObjectField(FieldName, AnchorObj)
                    || AnchorObj == nullptr
                    || !(*AnchorObj).IsValid())
                {
                    return false;
                }

                FString AnchorToken;
                if (!ResolveSingleNodeToken(*AnchorObj, AnchorToken) || AnchorToken.IsEmpty())
                {
                    return false;
                }

                AnchorNode = ResolveGraphNodeByTokenLocal(TargetGraph, AnchorToken);
                if (AnchorNode == nullptr)
                {
                    return false;
                }

                ResolvePinName(*AnchorObj, AnchorPinName);
                return true;
            };

            if (!TryResolveAnchorField(TEXT("anchor")))
            {
                if (!TryResolveAnchorField(TEXT("near")))
                {
                    if (!TryResolveAnchorField(TEXT("from")))
                    {
                        TryResolveAnchorField(TEXT("target"));
                    }
                }
            }

            if (AnchorNode != nullptr)
            {
                const FVector2D AnchorSize = EstimateNodeSize(AnchorNode);
                OutX = AnchorNode->NodePosX + static_cast<int32>(AnchorSize.X) + 96;
                OutY = AnchorNode->NodePosY;
                if (!AnchorPinName.IsEmpty())
                {
                    if (UEdGraphPin* AnchorPin = FindPinByName(AnchorNode, AnchorPinName))
                    {
                        if (AnchorPin->Direction == EGPD_Input)
                        {
                            OutX = AnchorNode->NodePosX - 384;
                        }
                    }
                }
            }
            else
            {
                UEdGraphNode* RightmostNode = nullptr;
                for (UEdGraphNode* Node : TargetGraph->Nodes)
                {
                    if (Node == nullptr)
                    {
                        continue;
                    }
                    if (RightmostNode == nullptr || Node->NodePosX > RightmostNode->NodePosX)
                    {
                        RightmostNode = Node;
                    }
                }

                if (RightmostNode != nullptr)
                {
                    const FVector2D AnchorSize = EstimateNodeSize(RightmostNode);
                    OutX = RightmostNode->NodePosX + static_cast<int32>(AnchorSize.X) + 96;
                    OutY = RightmostNode->NodePosY;
                }
            }

            const int32 GridSize = 16;
            const int32 VerticalStep = 192;
            const FVector2D CandidateSize(280.0f, 160.0f);
            OutX = FMath::GridSnap(OutX, GridSize);
            OutY = FMath::GridSnap(OutY, GridSize);

            bool bCollides = true;
            while (bCollides)
            {
                bCollides = false;
                const FBox2D CandidateRect(
                    FVector2D(OutX, OutY),
                    FVector2D(OutX + CandidateSize.X, OutY + CandidateSize.Y));

                for (UEdGraphNode* ExistingNode : TargetGraph->Nodes)
                {
                    if (ExistingNode == nullptr || ExistingNode == AnchorNode)
                    {
                        continue;
                    }

                    const FVector2D ExistingSize = EstimateNodeSize(ExistingNode);
                    const FBox2D ExistingRect(
                        FVector2D(ExistingNode->NodePosX, ExistingNode->NodePosY),
                        FVector2D(ExistingNode->NodePosX + ExistingSize.X, ExistingNode->NodePosY + ExistingSize.Y));
                    if (CandidateRect.Intersect(ExistingRect))
                    {
                        OutY = FMath::GridSnap(OutY + VerticalStep, GridSize);
                        bCollides = true;
                        break;
                    }
                }
            }

            return true;
        };

        auto BuildDirectSingleResult = [&](bool bOk, bool bChanged, const FString& ErrorCode, const FString& ErrorMessage, const FString& NodeId = TEXT("")) -> TSharedPtr<FJsonObject>
        {
            TSharedPtr<FJsonObject> StructuredError;
            FString StructuredCode;
            FString StructuredMessage;
            if (!bOk && !ErrorMessage.IsEmpty())
            {
                const TSharedRef<TJsonReader<>> ErrorReader = TJsonReaderFactory<>::Create(ErrorMessage);
                if (FJsonSerializer::Deserialize(ErrorReader, StructuredError) && StructuredError.IsValid())
                {
                    StructuredError->TryGetStringField(TEXT("code"), StructuredCode);
                    StructuredError->TryGetStringField(TEXT("message"), StructuredMessage);
                }
            }
            const FString ActualErrorCode = bOk
                ? TEXT("")
                : (!ErrorCode.IsEmpty()
                    ? ErrorCode
                    : (!StructuredCode.IsEmpty() ? StructuredCode : TEXT("INTERNAL_ERROR")));
            const FString ActualErrorMessage = bOk
                ? TEXT("")
                : (!StructuredMessage.IsEmpty()
                    ? StructuredMessage
                    : (ErrorMessage.IsEmpty() ? TEXT("blueprint.graph.edit failed") : ErrorMessage));

            TSharedPtr<FJsonObject> DirectResult = MakeShared<FJsonObject>();
            DirectResult->SetBoolField(TEXT("isError"), !bOk);
            if (!bOk)
            {
                DirectResult->SetStringField(TEXT("code"), ActualErrorCode);
                DirectResult->SetStringField(TEXT("message"), ActualErrorMessage);
                if (StructuredError.IsValid())
                {
                    DirectResult->SetObjectField(TEXT("details"), StructuredError);
                }
            }

            TArray<TSharedPtr<FJsonValue>> DirectOpResults;
            TSharedPtr<FJsonObject> DirectOpResult = MakeShared<FJsonObject>();
            DirectOpResult->SetNumberField(TEXT("index"), 0);
            DirectOpResult->SetStringField(TEXT("op"), OpName);
            DirectOpResult->SetBoolField(TEXT("ok"), bOk);
            DirectOpResult->SetBoolField(TEXT("skipped"), false);
            DirectOpResult->SetBoolField(TEXT("changed"), bChanged);
            DirectOpResult->SetStringField(TEXT("errorCode"), ActualErrorCode);
            DirectOpResult->SetStringField(TEXT("errorMessage"), ActualErrorMessage);
            if (!bOk && StructuredError.IsValid())
            {
                DirectOpResult->SetObjectField(TEXT("details"), StructuredError);
            }
            if (!NodeId.IsEmpty())
            {
                DirectOpResult->SetStringField(TEXT("nodeId"), NodeId);
            }
            DirectOpResults.Add(MakeShared<FJsonValueObject>(DirectOpResult));
            DirectResult->SetArrayField(TEXT("opResults"), DirectOpResults);

            TArray<TSharedPtr<FJsonValue>> DirectDiagnostics;
            if (!bOk)
            {
                TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
                Diagnostic->SetStringField(TEXT("code"), ActualErrorCode);
                Diagnostic->SetStringField(TEXT("severity"), TEXT("error"));
                Diagnostic->SetStringField(TEXT("message"), ActualErrorMessage);
                Diagnostic->SetStringField(TEXT("sourceKind"), TEXT("graph.edit"));
                Diagnostic->SetStringField(TEXT("op"), OpName);
                if (StructuredError.IsValid())
                {
                    Diagnostic->SetObjectField(TEXT("details"), StructuredError);
                }
                if (!NodeId.IsEmpty())
                {
                    Diagnostic->SetStringField(TEXT("nodeId"), NodeId);
                }
                DirectDiagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
            }
            DirectResult->SetArrayField(TEXT("diagnostics"), DirectDiagnostics);
            return DirectResult;
        };

        auto MakeGraphDiff = []() -> TSharedPtr<FJsonObject>
        {
            TSharedPtr<FJsonObject> Diff = MakeShared<FJsonObject>();
            Diff->SetArrayField(TEXT("nodesAdded"), {});
            Diff->SetArrayField(TEXT("nodesRemoved"), {});
            Diff->SetArrayField(TEXT("nodesMoved"), {});
            Diff->SetArrayField(TEXT("pinDefaultsChanged"), {});
            Diff->SetArrayField(TEXT("linksAdded"), {});
            Diff->SetArrayField(TEXT("linksRemoved"), {});
            Diff->SetArrayField(TEXT("eventReplicationChanged"), {});
            return Diff;
        };

        auto AppendDiffObject = [](const TSharedPtr<FJsonObject>& Diff, const TCHAR* FieldName, const TSharedPtr<FJsonObject>& Entry)
        {
            if (!Diff.IsValid() || !Entry.IsValid())
            {
                return;
            }
            TArray<TSharedPtr<FJsonValue>> Values = CloneBlueprintJsonArrayField(Diff, FieldName);
            Values.Add(MakeShared<FJsonValueObject>(Entry));
            Diff->SetArrayField(FieldName, Values);
        };

        auto MakeNodeDiffEntry = [](const FString& NodeId, const FString& NodeClassPath = TEXT(""), const FString& Title = TEXT("")) -> TSharedPtr<FJsonObject>
        {
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("nodeId"), NodeId);
            if (!NodeClassPath.IsEmpty())
            {
                Entry->SetStringField(TEXT("nodeClassPath"), NodeClassPath);
            }
            if (!Title.IsEmpty())
            {
                Entry->SetStringField(TEXT("title"), Title);
            }
            return Entry;
        };

        auto AttachDiffToSingleResult = [](const TSharedPtr<FJsonObject>& Result, const TSharedPtr<FJsonObject>& Diff)
        {
            if (!Result.IsValid() || !Diff.IsValid())
            {
                return;
            }
            const TArray<TSharedPtr<FJsonValue>>* OpResultsArray = nullptr;
            if (Result->TryGetArrayField(TEXT("opResults"), OpResultsArray) && OpResultsArray != nullptr && OpResultsArray->Num() > 0)
            {
                const TSharedPtr<FJsonObject>* OpResultObject = nullptr;
                if ((*OpResultsArray)[0].IsValid() && (*OpResultsArray)[0]->TryGetObject(OpResultObject) && OpResultObject != nullptr && (*OpResultObject).IsValid())
                {
                    (*OpResultObject)->SetObjectField(TEXT("diff"), Diff);
                }
            }
        };

        auto MakePinDefaultSummary = [&](const FString& NodeId, const FString& PinName) -> TSharedPtr<FJsonObject>
        {
            TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
            Summary->SetStringField(TEXT("value"), TEXT(""));
            Summary->SetStringField(TEXT("object"), TEXT(""));
            Summary->SetStringField(TEXT("text"), TEXT(""));

            UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
            UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, EffectiveGraphName);
            UEdGraphNode* TargetNode = ResolveBlueprintGraphNodeByToken(TargetGraph, NodeId);
            UEdGraphPin* Pin = TargetNode != nullptr ? TargetNode->FindPin(*PinName) : nullptr;
            if (Pin == nullptr)
            {
                return Summary;
            }

            Summary->SetStringField(TEXT("value"), Pin->DefaultValue);
            Summary->SetStringField(TEXT("object"), Pin->DefaultObject ? Pin->DefaultObject->GetPathName() : TEXT(""));
            Summary->SetStringField(TEXT("text"), Pin->DefaultTextValue.ToString());
            return Summary;
        };

        auto AttachNodeAddedDiff = [&](const TSharedPtr<FJsonObject>& Result, const FString& NodeId, const FString& NodeClassPath)
        {
            if (!Result.IsValid() || NodeId.IsEmpty())
            {
                return;
            }
            TSharedPtr<FJsonObject> Diff = MakeGraphDiff();
            AppendDiffObject(Diff, TEXT("nodesAdded"), MakeNodeDiffEntry(NodeId, NodeClassPath));
            AttachDiffToSingleResult(Result, Diff);
        };

        auto ErrorCodeFromPrefixedMessage = [](const FString& ErrorMessage) -> FString
        {
            if (ErrorMessage.StartsWith(TEXT("CONNECT_REQUIRES_OUTPUT_TO_INPUT")))
            {
                return TEXT("CONNECT_REQUIRES_OUTPUT_TO_INPUT");
            }
            if (ErrorMessage.StartsWith(TEXT("CONNECT_PIN_TYPE_MISMATCH")))
            {
                return TEXT("CONNECT_PIN_TYPE_MISMATCH");
            }
            if (ErrorMessage.StartsWith(TEXT("NODE_REF_NOT_FOUND")))
            {
                return TEXT("NODE_REF_NOT_FOUND");
            }
            if (ErrorMessage.StartsWith(TEXT("PIN_REF_NOT_FOUND")))
            {
                return TEXT("PIN_REF_NOT_FOUND");
            }
            if (ErrorMessage.StartsWith(TEXT("LINK_NOT_FOUND")))
            {
                return TEXT("LINK_NOT_FOUND");
            }
            return TEXT("");
        };

        auto ReadStringAlias = [&](std::initializer_list<const TCHAR*> FieldNames, FString& OutValue) -> bool
        {
            OutValue.Empty();
            for (const TCHAR* FieldName : FieldNames)
            {
                if (SingleArgsObject.IsValid() && SingleArgsObject->TryGetStringField(FieldName, OutValue) && !OutValue.IsEmpty())
                {
                    return true;
                }
            }
            return false;
        };

        auto AppendSecondarySurfaceHint = [](const TSharedPtr<FJsonObject>& Result, const TSharedPtr<FJsonObject>& Surface, const FString& Code, const FString& Message)
        {
            if (!Result.IsValid() || !Surface.IsValid())
            {
                return;
            }

            const TArray<TSharedPtr<FJsonValue>>* OpResultsArray = nullptr;
            if (Result->TryGetArrayField(TEXT("opResults"), OpResultsArray) && OpResultsArray != nullptr && OpResultsArray->Num() > 0)
            {
                const TSharedPtr<FJsonObject>* OpResultObject = nullptr;
                if ((*OpResultsArray)[0].IsValid() && (*OpResultsArray)[0]->TryGetObject(OpResultObject) && OpResultObject != nullptr && (*OpResultObject).IsValid())
                {
                    (*OpResultObject)->SetObjectField(TEXT("secondarySurface"), Surface);
                }
            }

            TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
            Diagnostic->SetStringField(TEXT("code"), Code);
            Diagnostic->SetStringField(TEXT("severity"), TEXT("info"));
            Diagnostic->SetStringField(TEXT("message"), Message);
            Diagnostic->SetStringField(TEXT("sourceKind"), TEXT("graph.edit"));
            Diagnostic->SetObjectField(TEXT("secondarySurface"), Surface);
            TArray<TSharedPtr<FJsonValue>> Diagnostics = CloneBlueprintJsonArrayField(Result, TEXT("diagnostics"));
            Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
            Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
        };

        auto MakeBlueprintMemberSurface = [](const FString& MemberKind, const FString& Name, const FString& Reason, std::initializer_list<const TCHAR*> Operations) -> TSharedPtr<FJsonObject>
        {
            TSharedPtr<FJsonObject> Surface = MakeShared<FJsonObject>();
            Surface->SetStringField(TEXT("kind"), TEXT("blueprintMember"));
            Surface->SetStringField(TEXT("tool"), TEXT("blueprint.member.edit"));
            Surface->SetStringField(TEXT("memberKind"), MemberKind);
            Surface->SetStringField(TEXT("name"), Name);
            Surface->SetStringField(TEXT("reason"), Reason);
            Surface->SetBoolField(TEXT("editable"), true);
            TArray<TSharedPtr<FJsonValue>> OperationValues;
            for (const TCHAR* Operation : Operations)
            {
                OperationValues.Add(MakeShared<FJsonValueString>(FString(Operation)));
            }
            Surface->SetArrayField(TEXT("operations"), OperationValues);
            return Surface;
        };

        TSharedPtr<FJsonObject> SingleResult;
        if (!ClientRef.IsEmpty() && NodeRefs.Contains(ClientRef))
        {
            SingleResult = BuildDirectSingleResult(
                false,
                false,
                TEXT("INVALID_ARGUMENT"),
                FString::Printf(TEXT("Duplicate clientRef: %s"), *ClientRef));
        }
        else if (OpName.Equals(TEXT("addfrompalette")))
        {
            FString EntryId;
            ReadStringAlias({TEXT("entryId"), TEXT("id")}, EntryId);
            if (EntryId.IsEmpty())
            {
                const TSharedPtr<FJsonObject>* EntryObject = nullptr;
                if (SingleArgsObject->TryGetObjectField(TEXT("entry"), EntryObject) && EntryObject != nullptr && (*EntryObject).IsValid())
                {
                    (*EntryObject)->TryGetStringField(TEXT("id"), EntryId);
                }
            }

            const TSharedPtr<FJsonObject>* PaletteEntryObject = nullptr;
            if (!SingleArgsObject->HasField(TEXT("contextSensitive"))
                && SingleArgsObject->TryGetObjectField(TEXT("entry"), PaletteEntryObject)
                && PaletteEntryObject != nullptr
                && (*PaletteEntryObject).IsValid())
            {
                if ((*PaletteEntryObject)->HasTypedField<EJson::Boolean>(TEXT("contextSensitive")))
                {
                    const bool bEntryContextSensitive = (*PaletteEntryObject)->GetBoolField(TEXT("contextSensitive"));
                    SingleArgsObject->SetBoolField(TEXT("contextSensitive"), bEntryContextSensitive);
                }
            }

            if (EntryId.IsEmpty())
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("addFromPalette requires entry.id."));
            }
            else
            {
                int32 X = 0;
                int32 Y = 0;
                ResolveInsertionPoint(SingleArgsObject, X, Y);
                FString NewNodeId;
                FString Error;
                const FString PayloadJson = SerializeForMutate(SingleArgsObject);
                const bool bOk = FLoomleBlueprintAdapter::AddNodeFromPalette(
                    AssetPath,
                    EffectiveGraphName,
                    EntryId,
                    PayloadJson,
                    X,
                    Y,
                    NewNodeId,
                    Error,
                    bDryRun,
                    PaletteRequestCache.CacheId);
                SingleResult = BuildDirectSingleResult(bOk, bOk && !bDryRun, TEXT(""), Error, NewNodeId);
                if (bOk)
                {
                    if (bDryRun)
                    {
                        TSharedPtr<FJsonObject> Diff = MakeGraphDiff();
                        AttachDiffToSingleResult(SingleResult, Diff);
                    }
                    else
                    {
                        AttachNodeAddedDiff(SingleResult, NewNodeId, TEXT("palette"));
                    }
                }
            }
        }
        else if (OpName.Equals(TEXT("addcommentbox")))
        {
            if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
            }
            else
            {
                int32 X = 0;
                int32 Y = 0;
                ResolveInsertionPoint(SingleArgsObject, X, Y);
                const int32 Width = ReadIntField(SingleArgsObject, TEXT("width"), 400);
                const int32 Height = ReadIntField(SingleArgsObject, TEXT("height"), 160);
                FString CommentText;
                ReadStringAlias({TEXT("text"), TEXT("comment")}, CommentText);
                FString NewNodeId;
                FString Error;
                const bool bOk = FLoomleBlueprintAdapter::AddCommentNode(
                    AssetPath,
                    EffectiveGraphName,
                    CommentText,
                    X,
                    Y,
                    Width,
                    Height,
                    NewNodeId,
                    Error);
                SingleResult = BuildDirectSingleResult(bOk, bOk, TEXT(""), Error, NewNodeId);
                if (bOk)
                {
                    AttachNodeAddedDiff(SingleResult, NewNodeId, TEXT("/Script/UnrealEd.EdGraphNode_Comment"));
                }
            }
        }
        else if (OpName.Equals(TEXT("addreroute")))
        {
            if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
            }
            else
            {
                int32 X = 0;
                int32 Y = 0;
                ResolveInsertionPoint(SingleArgsObject, X, Y);
                FString NewNodeId;
                FString Error;
                const bool bOk = FLoomleBlueprintAdapter::AddKnotNode(AssetPath, EffectiveGraphName, X, Y, NewNodeId, Error);
                SingleResult = BuildDirectSingleResult(bOk, bOk, TEXT(""), Error, NewNodeId);
                if (bOk)
                {
                    AttachNodeAddedDiff(SingleResult, NewNodeId, TEXT("/Script/BlueprintGraph.K2Node_Knot"));
                }
            }
        }
        else if (OpName.Equals(TEXT("duplicatenode")))
        {
            FString SourceNodeId;
            if (!ResolveSingleNodeToken(SingleArgsObject, SourceNodeId))
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("duplicateNode requires a target node reference."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
            }
            else
            {
                const int32 Dx = ReadIntField(SingleArgsObject, TEXT("dx"), ReadIntField(SingleArgsObject, TEXT("deltaX"), 48));
                const int32 Dy = ReadIntField(SingleArgsObject, TEXT("dy"), ReadIntField(SingleArgsObject, TEXT("deltaY"), 48));
                FString NewNodeId;
                FString Error;
                const bool bOk = FLoomleBlueprintAdapter::DuplicateNode(AssetPath, EffectiveGraphName, SourceNodeId, Dx, Dy, NewNodeId, Error);
                SingleResult = BuildDirectSingleResult(bOk, bOk && !bDryRun, TEXT(""), Error, NewNodeId);
                if (bOk)
                {
                    AttachNodeAddedDiff(SingleResult, NewNodeId, TEXT(""));
                }
            }
        }
        else if (OpName.Equals(TEXT("layoutgraph")))
        {
            FString LayoutScope = TEXT("touched");
            SingleArgsObject->TryGetStringField(TEXT("scope"), LayoutScope);
            LayoutScope = LayoutScope.ToLower();

            TArray<FString> LayoutNodeIds;
            const TArray<TSharedPtr<FJsonValue>>* NodeIdsArray = nullptr;
            if (SingleArgsObject->TryGetArrayField(TEXT("nodeIds"), NodeIdsArray) && NodeIdsArray != nullptr)
            {
                for (const TSharedPtr<FJsonValue>& NodeIdValue : *NodeIdsArray)
                {
                    FString NodeId;
                    if (NodeIdValue.IsValid() && NodeIdValue->TryGetString(NodeId) && !NodeId.IsEmpty())
                    {
                        LayoutNodeIds.AddUnique(NodeId);
                    }
                }
            }

            if (LayoutScope.Equals(TEXT("touched")))
            {
                TArray<FString> PendingNodeIds;
                ResolvePendingGraphLayoutNodes(TEXT("blueprint"), AssetPath, EffectiveGraphName, PendingNodeIds, false);
                for (const FString& PendingNodeId : PendingNodeIds)
                {
                    LayoutNodeIds.AddUnique(PendingNodeId);
                }
            }

            if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
            }
            else
            {
                TArray<FString> MovedNodeIds;
                FString Error;
                const bool bOk = ApplyBlueprintLayout(AssetPath, EffectiveGraphName, LayoutScope, LayoutNodeIds, MovedNodeIds, Error);
                SingleResult = BuildDirectSingleResult(bOk, bOk && MovedNodeIds.Num() > 0, TEXT(""), Error);
                if (bOk && LayoutScope.Equals(TEXT("touched")))
                {
                    TArray<FString> IgnoredNodeIds;
                    ResolvePendingGraphLayoutNodes(TEXT("blueprint"), AssetPath, EffectiveGraphName, IgnoredNodeIds, true);
                }
            }
        }
        else if (OpName.Equals(TEXT("compile")))
        {
            if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
            }
            else
            {
                FString Error;
                const bool bOk = FLoomleBlueprintAdapter::CompileBlueprint(AssetPath, EffectiveGraphName, Error);
                SingleResult = BuildDirectSingleResult(bOk, false, TEXT(""), Error);
            }
        }
        else if (OpName.Equals(TEXT("connectpins")) || OpName.Equals(TEXT("disconnectpins")))
        {
            FString FromNodeId;
            FString FromPinName;
            FString ToNodeId;
            FString ToPinName;
            if (!ResolvePinEndpoint(SingleArgsObject, TEXT("from"), FromNodeId, FromPinName)
                || !ResolvePinEndpoint(SingleArgsObject, TEXT("to"), ToNodeId, ToPinName))
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), OpName.Equals(TEXT("connectpins"))
                    ? TEXT("connectPins requires from/to node references with pins.")
                    : TEXT("disconnectPins requires from/to node references with pins."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
            }
            else
            {
                FString Error;
                bool bOk = false;
                FBlueprintGraphEditResolvedGraph Resolved;
                if (ResolveCachedGraph(EffectiveGraphName, Resolved, Error))
                {
                    UEdGraphNode* FromNode = ResolveBlueprintGraphNodeByToken(Resolved.Graph, FromNodeId);
                    UEdGraphNode* ToNode = ResolveBlueprintGraphNodeByToken(Resolved.Graph, ToNodeId);
                    if (FromNode == nullptr)
                    {
                        Error = FString::Printf(TEXT("NODE_REF_NOT_FOUND: from.node id was not found in graph: %s"), *FromNodeId);
                    }
                    else if (ToNode == nullptr)
                    {
                        Error = FString::Printf(TEXT("NODE_REF_NOT_FOUND: to.node id was not found in graph: %s"), *ToNodeId);
                    }
                    else
                    {
                        UEdGraphPin* FromPin = FindPinByName(FromNode, FromPinName);
                        UEdGraphPin* ToPin = FindPinByName(ToNode, ToPinName);
                        if (FromPin == nullptr)
                        {
                            Error = FString::Printf(TEXT("PIN_REF_NOT_FOUND: from pin was not found on node %s: %s"), *FromNodeId, *FromPinName);
                        }
                        else if (ToPin == nullptr)
                        {
                            Error = FString::Printf(TEXT("PIN_REF_NOT_FOUND: to pin was not found on node %s: %s"), *ToNodeId, *ToPinName);
                        }
                        else if (FromPin->Direction != EGPD_Output || ToPin->Direction != EGPD_Input)
                        {
                            Error = OpName.Equals(TEXT("connectpins"))
                                ? TEXT("CONNECT_REQUIRES_OUTPUT_TO_INPUT: connect requires from to be an output pin and to to be an input pin.")
                                : TEXT("CONNECT_REQUIRES_OUTPUT_TO_INPUT: disconnect requires from to be an output pin and to to be an input pin.");
                        }
                        else if (OpName.Equals(TEXT("disconnectpins")) && !(FromPin->LinkedTo.Contains(ToPin) || ToPin->LinkedTo.Contains(FromPin)))
                        {
                            Error = TEXT("LINK_NOT_FOUND: specified pin link does not exist; disconnect treated it as a no-op.");
                            bOk = true;
                        }
                        else
                        {
                            const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
                            if (Schema == nullptr)
                            {
                                Error = TEXT("INTERNAL_ERROR: Failed to resolve K2 graph schema.");
                            }
                            else if (OpName.Equals(TEXT("connectpins")))
                            {
                                const FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
                                if (Response.Response == CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE || Response.Response == CONNECT_RESPONSE_MAKE_WITH_PROMOTION)
                                {
                                    Error = FString::Printf(TEXT("CONNECT_PIN_TYPE_MISMATCH: Pins require conversion or promotion, which blueprint.graph.edit does not auto-insert. %s"), *Response.Message.ToString());
                                }
                                else if (Response.Response == CONNECT_RESPONSE_DISALLOW)
                                {
                                    Error = FString::Printf(TEXT("CONNECT_PIN_TYPE_MISMATCH: %s"), *Response.Message.ToString());
                                }
                                else
                                {
                                    Resolved.Blueprint->Modify();
                                    Resolved.Graph->Modify();
                                    FromNode->Modify();
                                    ToNode->Modify();
                                    if (Schema->TryCreateConnection(FromPin, ToPin))
                                    {
                                        bOk = true;
                                        DeferGraphModified(Resolved.Blueprint, Resolved.Graph);
                                    }
                                    else
                                    {
                                        Error = FString::Printf(TEXT("CONNECT_PIN_TYPE_MISMATCH: %s"), *Response.Message.ToString());
                                    }
                                }
                            }
                            else
                            {
                                Resolved.Blueprint->Modify();
                                Resolved.Graph->Modify();
                                FromNode->Modify();
                                ToNode->Modify();
                                FromPin->BreakLinkTo(ToPin);
                                bOk = true;
                                DeferGraphModified(Resolved.Blueprint, Resolved.Graph);
                            }
                        }
                    }
                }
                const FString ErrorCode = ErrorCodeFromPrefixedMessage(Error);
                const bool bChanged = bOk && !ErrorCode.Equals(TEXT("LINK_NOT_FOUND"));
                SingleResult = BuildDirectSingleResult(bOk, bChanged, ErrorCode, Error);
                if (bChanged)
                {
                    TSharedPtr<FJsonObject> Diff = MakeGraphDiff();
                    TSharedPtr<FJsonObject> LinkEntry = MakeShared<FJsonObject>();
                    LinkEntry->SetStringField(TEXT("fromNodeId"), FromNodeId);
                    LinkEntry->SetStringField(TEXT("fromPin"), FromPinName);
                    LinkEntry->SetStringField(TEXT("toNodeId"), ToNodeId);
                    LinkEntry->SetStringField(TEXT("toPin"), ToPinName);
                    AppendDiffObject(Diff, OpName.Equals(TEXT("connectpins")) ? TEXT("linksAdded") : TEXT("linksRemoved"), LinkEntry);
                    AttachDiffToSingleResult(SingleResult, Diff);
                }
            }
        }
        else if (OpName.Equals(TEXT("breakpinlinks")))
        {
            FString TargetNodeId;
            FString TargetPinName;
            if (!ResolvePinEndpoint(SingleArgsObject, TEXT("target"), TargetNodeId, TargetPinName))
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("breakPinLinks requires a target node reference with pin."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""), TargetNodeId);
            }
            else
            {
                FString Error;
                const bool bOk = FLoomleBlueprintAdapter::BreakPinLinks(AssetPath, EffectiveGraphName, TargetNodeId, TargetPinName, Error);
                SingleResult = BuildDirectSingleResult(bOk, bOk, TEXT(""), Error, TargetNodeId);
                if (!bOk)
                {
                    FString DetailsJson;
                    FString DetailsError;
                    if (FLoomleBlueprintAdapter::DescribePinTarget(AssetPath, EffectiveGraphName, TargetNodeId, TargetPinName, DetailsJson, DetailsError))
                    {
                        TSharedPtr<FJsonObject> DetailsObject;
                        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DetailsJson);
                        if (FJsonSerializer::Deserialize(Reader, DetailsObject) && DetailsObject.IsValid())
                        {
                            const TArray<TSharedPtr<FJsonValue>>* OpResultsArray = nullptr;
                            if (SingleResult->TryGetArrayField(TEXT("opResults"), OpResultsArray) && OpResultsArray != nullptr && OpResultsArray->Num() > 0)
                            {
                                const TSharedPtr<FJsonObject>* OpResultObj = nullptr;
                                if ((*OpResultsArray)[0].IsValid() && (*OpResultsArray)[0]->TryGetObject(OpResultObj) && OpResultObj != nullptr && (*OpResultObj).IsValid())
                                {
                                    (*OpResultObj)->SetObjectField(TEXT("details"), DetailsObject);
                                }
                            }
                        }
                    }
                }
            }
        }
        else if (OpName.Equals(TEXT("reconstructnode")))
        {
            FString TargetNodeId;
            if (!ResolveSingleNodeToken(SingleArgsObject, TargetNodeId))
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("reconstructNode requires a target node reference."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""), TargetNodeId);
            }
            else
            {
                bool bPreserveLinks = true;
                SingleArgsObject->TryGetBoolField(TEXT("preserveLinks"), bPreserveLinks);

                UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
                UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, EffectiveGraphName);
                UEdGraphNode* TargetNode = ResolveBlueprintGraphNodeByToken(TargetGraph, TargetNodeId);
                if (Blueprint == nullptr || TargetGraph == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("GRAPH_NOT_FOUND"), TEXT("Failed to resolve target graph."), TargetNodeId);
                }
                else if (TargetNode == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("Failed to resolve reconstructNode target."), TargetNodeId);
                }
                else
                {
                    struct FBlueprintLinkSnapshot
                    {
                        FString PinName;
                        FString LinkedNodeId;
                        FString LinkedPinName;
                    };

                    TArray<TSharedPtr<FJsonValue>> PinsBefore;
                    TArray<FBlueprintLinkSnapshot> LinksBefore;
                    for (UEdGraphPin* Pin : TargetNode->Pins)
                    {
                        if (Pin == nullptr)
                        {
                            continue;
                        }
                        PinsBefore.Add(MakeShared<FJsonValueString>(Pin->PinName.ToString()));
                        for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                        {
                            if (LinkedPin == nullptr || LinkedPin->GetOwningNode() == nullptr)
                            {
                                continue;
                            }
                            FBlueprintLinkSnapshot Snapshot;
                            Snapshot.PinName = Pin->PinName.ToString();
                            Snapshot.LinkedNodeId = LinkedPin->GetOwningNode()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
                            Snapshot.LinkedPinName = LinkedPin->PinName.ToString();
                            LinksBefore.Add(Snapshot);
                        }
                    }

                    Blueprint->Modify();
                    TargetGraph->Modify();
                    TargetNode->Modify();
                    if (!bPreserveLinks)
                    {
                        for (UEdGraphPin* Pin : TargetNode->Pins)
                        {
                            if (Pin != nullptr)
                            {
                                Pin->BreakAllPinLinks();
                            }
                        }
                    }
                    TargetNode->ReconstructNode();

                    const UEdGraphSchema* Schema = TargetGraph->GetSchema();
                    int32 LinksPreserved = 0;
                    TArray<TSharedPtr<FJsonValue>> LinksDropped;
                    if (bPreserveLinks && Schema != nullptr)
                    {
                        for (const FBlueprintLinkSnapshot& Snapshot : LinksBefore)
                        {
                            UEdGraphPin* NewPin = FindPinByName(TargetNode, Snapshot.PinName);
                            UEdGraphNode* LinkedNode = ResolveBlueprintGraphNodeByToken(TargetGraph, Snapshot.LinkedNodeId);
                            UEdGraphPin* LinkedPin = LinkedNode != nullptr ? FindPinByName(LinkedNode, Snapshot.LinkedPinName) : nullptr;
                            FString DropReason;
                            if (NewPin == nullptr)
                            {
                                DropReason = TEXT("targetPinMissing");
                            }
                            else if (LinkedPin == nullptr)
                            {
                                DropReason = TEXT("linkedPinMissing");
                            }
                            else if (NewPin->LinkedTo.Contains(LinkedPin) || LinkedPin->LinkedTo.Contains(NewPin))
                            {
                                ++LinksPreserved;
                                continue;
                            }
                            else
                            {
                                UEdGraphPin* OutputPin = NewPin->Direction == EGPD_Output ? NewPin : LinkedPin;
                                UEdGraphPin* InputPin = NewPin->Direction == EGPD_Input ? NewPin : LinkedPin;
                                if (OutputPin == nullptr || InputPin == nullptr || OutputPin->Direction != EGPD_Output || InputPin->Direction != EGPD_Input)
                                {
                                    DropReason = TEXT("directionMismatch");
                                }
                                else
                                {
                                    const FPinConnectionResponse Response = Schema->CanCreateConnection(OutputPin, InputPin);
                                    if (Response.Response == CONNECT_RESPONSE_MAKE || Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_A || Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_B || Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_AB)
                                    {
                                        if (Schema->TryCreateConnection(OutputPin, InputPin))
                                        {
                                            ++LinksPreserved;
                                            continue;
                                        }
                                        DropReason = TEXT("connectionRejected");
                                    }
                                    else
                                    {
                                        DropReason = Response.Message.ToString();
                                    }
                                }
                            }

                            TSharedPtr<FJsonObject> Drop = MakeShared<FJsonObject>();
                            Drop->SetStringField(TEXT("pin"), Snapshot.PinName);
                            Drop->SetStringField(TEXT("linkedNodeId"), Snapshot.LinkedNodeId);
                            Drop->SetStringField(TEXT("linkedPin"), Snapshot.LinkedPinName);
                            Drop->SetStringField(TEXT("reason"), DropReason);
                            LinksDropped.Add(MakeShared<FJsonValueObject>(Drop));
                        }
                    }

                    TArray<TSharedPtr<FJsonValue>> PinsAfter;
                    for (UEdGraphPin* Pin : TargetNode->Pins)
                    {
                        if (Pin != nullptr)
                        {
                            PinsAfter.Add(MakeShared<FJsonValueString>(Pin->PinName.ToString()));
                        }
                    }

                    TargetGraph->NotifyGraphChanged();
                    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
                    Blueprint->MarkPackageDirty();

                    SingleResult = BuildDirectSingleResult(true, true, TEXT(""), TEXT(""), TargetNodeId);
                    const TArray<TSharedPtr<FJsonValue>>* OpResultsArray = nullptr;
                    if (SingleResult->TryGetArrayField(TEXT("opResults"), OpResultsArray) && OpResultsArray != nullptr && OpResultsArray->Num() > 0)
                    {
                        const TSharedPtr<FJsonObject>* OpResultObj = nullptr;
                        if ((*OpResultsArray)[0].IsValid() && (*OpResultsArray)[0]->TryGetObject(OpResultObj) && OpResultObj != nullptr && (*OpResultObj).IsValid())
                        {
                            (*OpResultObj)->SetArrayField(TEXT("pinsBefore"), PinsBefore);
                            (*OpResultObj)->SetArrayField(TEXT("pinsAfter"), PinsAfter);
                            (*OpResultObj)->SetNumberField(TEXT("linksPreserved"), LinksPreserved);
                            (*OpResultObj)->SetArrayField(TEXT("linksDropped"), LinksDropped);
                        }
                    }
                }
            }
        }
        else if (OpName.Equals(TEXT("rebindmatchingpins")))
        {
            FString FromNodeId;
            FString ToNodeId;
            const TSharedPtr<FJsonObject>* FromNodeObj = nullptr;
            const TSharedPtr<FJsonObject>* ToNodeObj = nullptr;
            const bool bHasFrom = SingleArgsObject->TryGetObjectField(TEXT("fromNode"), FromNodeObj) && FromNodeObj != nullptr && (*FromNodeObj).IsValid();
            const bool bHasTo = SingleArgsObject->TryGetObjectField(TEXT("toNode"), ToNodeObj) && ToNodeObj != nullptr && (*ToNodeObj).IsValid();
            if (!bHasFrom || !bHasTo || !ResolveSingleNodeToken(*FromNodeObj, FromNodeId) || !ResolveSingleNodeToken(*ToNodeObj, ToNodeId))
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("rebindMatchingPins requires fromNode and toNode references."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""), ToNodeId);
            }
            else
            {
                UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
                UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, EffectiveGraphName);
                UEdGraphNode* FromNode = ResolveBlueprintGraphNodeByToken(TargetGraph, FromNodeId);
                UEdGraphNode* ToNode = ResolveBlueprintGraphNodeByToken(TargetGraph, ToNodeId);
                const UEdGraphSchema* Schema = TargetGraph != nullptr ? TargetGraph->GetSchema() : nullptr;
                if (Blueprint == nullptr || TargetGraph == nullptr || Schema == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("GRAPH_NOT_FOUND"), TEXT("Failed to resolve target graph."), ToNodeId);
                }
                else if (FromNode == nullptr || ToNode == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("Failed to resolve rebindMatchingPins nodes."), ToNodeId);
                }
                else
                {
                    int32 LinksRebound = 0;
                    TArray<TSharedPtr<FJsonValue>> PinsMatched;
                    TArray<TSharedPtr<FJsonValue>> PinsUnmatched;
                    TArray<TSharedPtr<FJsonValue>> LinksDropped;
                    TSharedPtr<FJsonObject> Diff = MakeGraphDiff();

                    Blueprint->Modify();
                    TargetGraph->Modify();
                    FromNode->Modify();
                    ToNode->Modify();

                    for (UEdGraphPin* FromPin : FromNode->Pins)
                    {
                        if (FromPin == nullptr || FromPin->LinkedTo.Num() == 0)
                        {
                            continue;
                        }
                        UEdGraphPin* ToPin = FindPinByName(ToNode, FromPin->PinName.ToString());
                        if (ToPin == nullptr || ToPin->Direction != FromPin->Direction)
                        {
                            PinsUnmatched.Add(MakeShared<FJsonValueString>(FromPin->PinName.ToString()));
                            for (UEdGraphPin* LinkedPin : FromPin->LinkedTo)
                            {
                                TSharedPtr<FJsonObject> Drop = MakeShared<FJsonObject>();
                                Drop->SetStringField(TEXT("pin"), FromPin->PinName.ToString());
                                Drop->SetStringField(TEXT("reason"), ToPin == nullptr ? TEXT("targetPinMissing") : TEXT("directionMismatch"));
                                if (LinkedPin != nullptr && LinkedPin->GetOwningNode() != nullptr)
                                {
                                    Drop->SetStringField(TEXT("linkedNodeId"), LinkedPin->GetOwningNode()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
                                    Drop->SetStringField(TEXT("linkedPin"), LinkedPin->PinName.ToString());
                                }
                                LinksDropped.Add(MakeShared<FJsonValueObject>(Drop));
                            }
                            continue;
                        }

                        PinsMatched.Add(MakeShared<FJsonValueString>(FromPin->PinName.ToString()));
                        TArray<UEdGraphPin*> LinkedPins = FromPin->LinkedTo;
                        for (UEdGraphPin* LinkedPin : LinkedPins)
                        {
                            if (LinkedPin == nullptr)
                            {
                                continue;
                            }

                            UEdGraphPin* OutputPin = ToPin->Direction == EGPD_Output ? ToPin : LinkedPin;
                            UEdGraphPin* InputPin = ToPin->Direction == EGPD_Input ? ToPin : LinkedPin;
                            FString DropReason;
                            if (OutputPin == nullptr || InputPin == nullptr || OutputPin->Direction != EGPD_Output || InputPin->Direction != EGPD_Input)
                            {
                                DropReason = TEXT("directionMismatch");
                            }
                            else
                            {
                                const FPinConnectionResponse Response = Schema->CanCreateConnection(OutputPin, InputPin);
                                if (Response.Response == CONNECT_RESPONSE_MAKE || Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_A || Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_B || Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_AB)
                                {
                                    if (Schema->TryCreateConnection(OutputPin, InputPin))
                                    {
                                        if (FromPin->LinkedTo.Contains(LinkedPin) || LinkedPin->LinkedTo.Contains(FromPin))
                                        {
                                            FromPin->BreakLinkTo(LinkedPin);
                                        }
                                        ++LinksRebound;

                                        TSharedPtr<FJsonObject> Removed = MakeShared<FJsonObject>();
                                        TSharedPtr<FJsonObject> Added = MakeShared<FJsonObject>();
                                        if (FromPin->Direction == EGPD_Output)
                                        {
                                            Removed->SetStringField(TEXT("fromNodeId"), FromNodeId);
                                            Removed->SetStringField(TEXT("fromPin"), FromPin->PinName.ToString());
                                            Removed->SetStringField(TEXT("toNodeId"), LinkedPin->GetOwningNode() ? LinkedPin->GetOwningNode()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower) : TEXT(""));
                                            Removed->SetStringField(TEXT("toPin"), LinkedPin->PinName.ToString());
                                            Added->SetStringField(TEXT("fromNodeId"), ToNodeId);
                                            Added->SetStringField(TEXT("fromPin"), ToPin->PinName.ToString());
                                            Added->SetStringField(TEXT("toNodeId"), Removed->GetStringField(TEXT("toNodeId")));
                                            Added->SetStringField(TEXT("toPin"), LinkedPin->PinName.ToString());
                                        }
                                        else
                                        {
                                            Removed->SetStringField(TEXT("fromNodeId"), LinkedPin->GetOwningNode() ? LinkedPin->GetOwningNode()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower) : TEXT(""));
                                            Removed->SetStringField(TEXT("fromPin"), LinkedPin->PinName.ToString());
                                            Removed->SetStringField(TEXT("toNodeId"), FromNodeId);
                                            Removed->SetStringField(TEXT("toPin"), FromPin->PinName.ToString());
                                            Added->SetStringField(TEXT("fromNodeId"), Removed->GetStringField(TEXT("fromNodeId")));
                                            Added->SetStringField(TEXT("fromPin"), LinkedPin->PinName.ToString());
                                            Added->SetStringField(TEXT("toNodeId"), ToNodeId);
                                            Added->SetStringField(TEXT("toPin"), ToPin->PinName.ToString());
                                        }
                                        AppendDiffObject(Diff, TEXT("linksRemoved"), Removed);
                                        AppendDiffObject(Diff, TEXT("linksAdded"), Added);
                                        continue;
                                    }
                                    DropReason = TEXT("connectionRejected");
                                }
                                else
                                {
                                    DropReason = Response.Message.ToString();
                                }
                            }

                            TSharedPtr<FJsonObject> Drop = MakeShared<FJsonObject>();
                            Drop->SetStringField(TEXT("pin"), FromPin->PinName.ToString());
                            Drop->SetStringField(TEXT("reason"), DropReason);
                            if (LinkedPin->GetOwningNode() != nullptr)
                            {
                                Drop->SetStringField(TEXT("linkedNodeId"), LinkedPin->GetOwningNode()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
                                Drop->SetStringField(TEXT("linkedPin"), LinkedPin->PinName.ToString());
                            }
                            LinksDropped.Add(MakeShared<FJsonValueObject>(Drop));
                        }
                    }

                    TargetGraph->NotifyGraphChanged();
                    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
                    Blueprint->MarkPackageDirty();

                    SingleResult = BuildDirectSingleResult(true, LinksRebound > 0, TEXT(""), TEXT(""), ToNodeId);
                    const TArray<TSharedPtr<FJsonValue>>* OpResultsArray = nullptr;
                    if (SingleResult->TryGetArrayField(TEXT("opResults"), OpResultsArray) && OpResultsArray != nullptr && OpResultsArray->Num() > 0)
                    {
                        const TSharedPtr<FJsonObject>* OpResultObj = nullptr;
                        if ((*OpResultsArray)[0].IsValid() && (*OpResultsArray)[0]->TryGetObject(OpResultObj) && OpResultObj != nullptr && (*OpResultObj).IsValid())
                        {
                            (*OpResultObj)->SetNumberField(TEXT("linksRebound"), LinksRebound);
                            (*OpResultObj)->SetArrayField(TEXT("pinsMatched"), PinsMatched);
                            (*OpResultObj)->SetArrayField(TEXT("pinsUnmatched"), PinsUnmatched);
                            (*OpResultObj)->SetArrayField(TEXT("linksDropped"), LinksDropped);
                            (*OpResultObj)->SetObjectField(TEXT("diff"), Diff);
                        }
                    }
                }
            }
        }
        else if (OpName.Equals(TEXT("moveinputlinks")))
        {
            FString FromNodeId;
            FString FromPinName;
            FString ToNodeId;
            FString ToPinName;
            if (!ResolvePinEndpoint(SingleArgsObject, TEXT("from"), FromNodeId, FromPinName)
                || !ResolvePinEndpoint(SingleArgsObject, TEXT("to"), ToNodeId, ToPinName))
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("moveInputLinks requires from/to node references with pins."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""), ToNodeId);
            }
            else
            {
                UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
                UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, EffectiveGraphName);
                UEdGraphNode* FromNode = ResolveBlueprintGraphNodeByToken(TargetGraph, FromNodeId);
                UEdGraphNode* ToNode = ResolveBlueprintGraphNodeByToken(TargetGraph, ToNodeId);
                const UEdGraphSchema* Schema = TargetGraph != nullptr ? TargetGraph->GetSchema() : nullptr;
                UEdGraphPin* FromPin = FromNode != nullptr ? FindPinByName(FromNode, FromPinName) : nullptr;
                UEdGraphPin* ToPin = ToNode != nullptr ? FindPinByName(ToNode, ToPinName) : nullptr;
                if (Blueprint == nullptr || TargetGraph == nullptr || Schema == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("GRAPH_NOT_FOUND"), TEXT("Failed to resolve target graph."), ToNodeId);
                }
                else if (FromNode == nullptr || ToNode == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("Failed to resolve moveInputLinks nodes."), ToNodeId);
                }
                else if (FromPin == nullptr || ToPin == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("PIN_REF_NOT_FOUND"), TEXT("Failed to resolve moveInputLinks pins."), ToNodeId);
                }
                else if (FromPin->Direction != EGPD_Input || ToPin->Direction != EGPD_Input)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("moveInputLinks requires input pins."), ToNodeId);
                }
                else
                {
                    int32 LinksMoved = 0;
                    TArray<TSharedPtr<FJsonValue>> LinksDropped;
                    TSharedPtr<FJsonObject> Diff = MakeGraphDiff();
                    TArray<UEdGraphPin*> UpstreamPins = FromPin->LinkedTo;

                    Blueprint->Modify();
                    TargetGraph->Modify();
                    FromNode->Modify();
                    ToNode->Modify();

                    for (UEdGraphPin* UpstreamPin : UpstreamPins)
                    {
                        if (UpstreamPin == nullptr || UpstreamPin->GetOwningNode() == nullptr)
                        {
                            continue;
                        }
                        UpstreamPin->GetOwningNode()->Modify();
                        FString DropReason;
                        if (UpstreamPin->Direction != EGPD_Output)
                        {
                            DropReason = TEXT("upstreamPinNotOutput");
                        }
                        else
                        {
                            const FPinConnectionResponse Response = Schema->CanCreateConnection(UpstreamPin, ToPin);
                            if (Response.Response == CONNECT_RESPONSE_MAKE || Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_A || Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_B || Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_AB)
                            {
                                if (Schema->TryCreateConnection(UpstreamPin, ToPin))
                                {
                                    if (UpstreamPin->LinkedTo.Contains(FromPin) || FromPin->LinkedTo.Contains(UpstreamPin))
                                    {
                                        UpstreamPin->BreakLinkTo(FromPin);
                                    }
                                    ++LinksMoved;

                                    const FString UpstreamNodeId = UpstreamPin->GetOwningNode()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
                                    TSharedPtr<FJsonObject> Removed = MakeShared<FJsonObject>();
                                    Removed->SetStringField(TEXT("fromNodeId"), UpstreamNodeId);
                                    Removed->SetStringField(TEXT("fromPin"), UpstreamPin->PinName.ToString());
                                    Removed->SetStringField(TEXT("toNodeId"), FromNodeId);
                                    Removed->SetStringField(TEXT("toPin"), FromPinName);
                                    TSharedPtr<FJsonObject> Added = MakeShared<FJsonObject>();
                                    Added->SetStringField(TEXT("fromNodeId"), UpstreamNodeId);
                                    Added->SetStringField(TEXT("fromPin"), UpstreamPin->PinName.ToString());
                                    Added->SetStringField(TEXT("toNodeId"), ToNodeId);
                                    Added->SetStringField(TEXT("toPin"), ToPinName);
                                    AppendDiffObject(Diff, TEXT("linksRemoved"), Removed);
                                    AppendDiffObject(Diff, TEXT("linksAdded"), Added);
                                    continue;
                                }
                                DropReason = TEXT("connectionRejected");
                            }
                            else
                            {
                                DropReason = Response.Message.ToString();
                            }
                        }

                        TSharedPtr<FJsonObject> Drop = MakeShared<FJsonObject>();
                        Drop->SetStringField(TEXT("fromNodeId"), UpstreamPin->GetOwningNode()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
                        Drop->SetStringField(TEXT("fromPin"), UpstreamPin->PinName.ToString());
                        Drop->SetStringField(TEXT("toNodeId"), ToNodeId);
                        Drop->SetStringField(TEXT("toPin"), ToPinName);
                        Drop->SetStringField(TEXT("reason"), DropReason);
                        LinksDropped.Add(MakeShared<FJsonValueObject>(Drop));
                    }

                    TargetGraph->NotifyGraphChanged();
                    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
                    Blueprint->MarkPackageDirty();

                    SingleResult = BuildDirectSingleResult(true, LinksMoved > 0, TEXT(""), TEXT(""), ToNodeId);
                    const TArray<TSharedPtr<FJsonValue>>* OpResultsArray = nullptr;
                    if (SingleResult->TryGetArrayField(TEXT("opResults"), OpResultsArray) && OpResultsArray != nullptr && OpResultsArray->Num() > 0)
                    {
                        const TSharedPtr<FJsonObject>* OpResultObj = nullptr;
                        if ((*OpResultsArray)[0].IsValid() && (*OpResultsArray)[0]->TryGetObject(OpResultObj) && OpResultObj != nullptr && (*OpResultObj).IsValid())
                        {
                            (*OpResultObj)->SetNumberField(TEXT("upstreamLinksMoved"), LinksMoved);
                            (*OpResultObj)->SetArrayField(TEXT("linksDropped"), LinksDropped);
                            (*OpResultObj)->SetObjectField(TEXT("diff"), Diff);
                        }
                    }
                }
            }
        }
        else if (OpName.Equals(TEXT("setpindefault")))
        {
            FString TargetNodeId;
            FString TargetPinName;
            if (!ResolvePinEndpoint(SingleArgsObject, TEXT("target"), TargetNodeId, TargetPinName))
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("setPinDefault requires a target node reference with pin."));
            }
            else
            {
                FString Value;
                FString ObjectPath;
                FString TextValue;
                SingleArgsObject->TryGetStringField(TEXT("value"), Value);
                SingleArgsObject->TryGetStringField(TEXT("object"), ObjectPath);
                SingleArgsObject->TryGetStringField(TEXT("defaultObject"), ObjectPath);
                SingleArgsObject->TryGetStringField(TEXT("defaultObjectPath"), ObjectPath);
                SingleArgsObject->TryGetStringField(TEXT("text"), TextValue);
                if (bDryRun)
                {
                    SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""), TargetNodeId);
                }
                else
                {
                    const TSharedPtr<FJsonObject> BeforeDefault = MakePinDefaultSummary(TargetNodeId, TargetPinName);
                    FString Error;
                    const bool bOk = FLoomleBlueprintAdapter::SetPinDefaultValue(AssetPath, EffectiveGraphName, TargetNodeId, TargetPinName, Value, ObjectPath, TextValue, Error);
                    if (!bOk && Error.StartsWith(TEXT("PIN_DEFAULT_REQUIRES_UNLINKED_PIN")))
                    {
                        SingleResult = BuildDirectSingleResult(false, false, TEXT("PIN_DEFAULT_REQUIRES_UNLINKED_PIN"), Error, TargetNodeId);
                    }
                    else if (!bOk && Error.StartsWith(TEXT("PIN_DEFAULT_OBJECT_NOT_FOUND")))
                    {
                        SingleResult = BuildDirectSingleResult(false, false, TEXT("PIN_DEFAULT_OBJECT_NOT_FOUND"), Error, TargetNodeId);
                    }
                    else if (!bOk && Error.StartsWith(TEXT("PIN_DEFAULT_OBJECT_INVALID_FOR_PIN")))
                    {
                        SingleResult = BuildDirectSingleResult(false, false, TEXT("PIN_DEFAULT_OBJECT_INVALID_FOR_PIN"), Error, TargetNodeId);
                    }
                    else
                    {
                        SingleResult = BuildDirectSingleResult(bOk, bOk, TEXT(""), Error, TargetNodeId);
                    }
                    if (bOk)
                    {
                        TSharedPtr<FJsonObject> Diff = MakeGraphDiff();
                        TSharedPtr<FJsonObject> PinDefaultEntry = MakeShared<FJsonObject>();
                        PinDefaultEntry->SetStringField(TEXT("nodeId"), TargetNodeId);
                        PinDefaultEntry->SetStringField(TEXT("pin"), TargetPinName);
                        PinDefaultEntry->SetObjectField(TEXT("before"), BeforeDefault);
                        PinDefaultEntry->SetObjectField(TEXT("after"), MakePinDefaultSummary(TargetNodeId, TargetPinName));
                        AppendDiffObject(Diff, TEXT("pinDefaultsChanged"), PinDefaultEntry);
                        AttachDiffToSingleResult(SingleResult, Diff);
                    }
                }
            }
        }
        else if (OpName.Equals(TEXT("removenode")))
        {
            FString TargetNodeId;
            if (!ResolveSingleNodeToken(SingleArgsObject, TargetNodeId))
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("removeNode requires a target node reference."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""), TargetNodeId);
            }
            else
            {
                FString RemovedNodeClassPath;
                FString RemovedNodeTitle;
                if (UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath))
                {
                    if (UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, EffectiveGraphName))
                    {
                        if (UEdGraphNode* TargetNode = ResolveBlueprintGraphNodeByToken(TargetGraph, TargetNodeId))
                        {
                            RemovedNodeClassPath = TargetNode->GetClass() ? TargetNode->GetClass()->GetPathName() : TEXT("");
                            RemovedNodeTitle = TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
                        }
                    }
                }
                FString Error;
                const bool bOk = FLoomleBlueprintAdapter::RemoveNode(AssetPath, EffectiveGraphName, TargetNodeId, Error);
                SingleResult = BuildDirectSingleResult(bOk, bOk, TEXT(""), Error, TargetNodeId);
                if (bOk)
                {
                    TSharedPtr<FJsonObject> Diff = MakeGraphDiff();
                    AppendDiffObject(Diff, TEXT("nodesRemoved"), MakeNodeDiffEntry(TargetNodeId, RemovedNodeClassPath, RemovedNodeTitle));
                    AttachDiffToSingleResult(SingleResult, Diff);
                }
            }
        }
        else if (OpName.Equals(TEXT("movenode")) || OpName.Equals(TEXT("movenodeby")))
        {
            FString TargetNodeId;
            if (!ResolveSingleNodeToken(SingleArgsObject, TargetNodeId))
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), FString::Printf(TEXT("%s requires a target node reference."), *OpName));
            }
            else
            {
                int32 X = ReadIntField(SingleArgsObject, TEXT("x"), 0);
                int32 Y = ReadIntField(SingleArgsObject, TEXT("y"), 0);
                if (OpName.Equals(TEXT("movenodeby")))
                {
                    UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
                    UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, EffectiveGraphName);
                    UEdGraphNode* TargetNode = ResolveBlueprintGraphNodeByToken(TargetGraph, TargetNodeId);
                    if (TargetNode == nullptr)
                    {
                        SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("Failed to resolve moveNodeBy target."), TargetNodeId);
                    }
                    else
                    {
                        const int32 Dx = ReadIntField(SingleArgsObject, TEXT("dx"), ReadIntField(SingleArgsObject, TEXT("deltaX"), 0));
                        const int32 Dy = ReadIntField(SingleArgsObject, TEXT("dy"), ReadIntField(SingleArgsObject, TEXT("deltaY"), 0));
                        X = TargetNode->NodePosX + Dx;
                        Y = TargetNode->NodePosY + Dy;
                    }
                }

                if (!SingleResult.IsValid())
                {
                    if (bDryRun)
                    {
                        SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""), TargetNodeId);
                    }
                    else
                    {
                        int32 BeforeX = 0;
                        int32 BeforeY = 0;
                        if (UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath))
                        {
                            if (UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, EffectiveGraphName))
                            {
                                if (UEdGraphNode* TargetNode = ResolveBlueprintGraphNodeByToken(TargetGraph, TargetNodeId))
                                {
                                    BeforeX = TargetNode->NodePosX;
                                    BeforeY = TargetNode->NodePosY;
                                }
                            }
                        }
                        FString Error;
                        const bool bOk = FLoomleBlueprintAdapter::MoveNode(AssetPath, EffectiveGraphName, TargetNodeId, X, Y, Error);
                        SingleResult = BuildDirectSingleResult(bOk, bOk, TEXT(""), Error, TargetNodeId);
                        if (bOk)
                        {
                            TSharedPtr<FJsonObject> Diff = MakeGraphDiff();
                            TSharedPtr<FJsonObject> MoveEntry = MakeShared<FJsonObject>();
                            MoveEntry->SetStringField(TEXT("nodeId"), TargetNodeId);
                            TSharedPtr<FJsonObject> BeforePosition = MakeShared<FJsonObject>();
                            BeforePosition->SetNumberField(TEXT("x"), BeforeX);
                            BeforePosition->SetNumberField(TEXT("y"), BeforeY);
                            TSharedPtr<FJsonObject> AfterPosition = MakeShared<FJsonObject>();
                            AfterPosition->SetNumberField(TEXT("x"), X);
                            AfterPosition->SetNumberField(TEXT("y"), Y);
                            MoveEntry->SetObjectField(TEXT("before"), BeforePosition);
                            MoveEntry->SetObjectField(TEXT("after"), AfterPosition);
                            AppendDiffObject(Diff, TEXT("nodesMoved"), MoveEntry);
                            AttachDiffToSingleResult(SingleResult, Diff);
                        }
                    }
                }
            }
        }
        else if (OpName.Equals(TEXT("movenodes")))
        {
            const TArray<TSharedPtr<FJsonValue>>* NodeIdsArray = nullptr;
            if (!SingleArgsObject->TryGetArrayField(TEXT("nodeIds"), NodeIdsArray) || NodeIdsArray == nullptr || NodeIdsArray->Num() == 0)
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("moveNodes requires nodeIds."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
            }
            else
            {
                UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
                UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, EffectiveGraphName);
                if (TargetGraph == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("GRAPH_NOT_FOUND"), TEXT("Failed to resolve target graph for moveNodes."));
                }
                else
                {
                    const int32 Dx = ReadIntField(SingleArgsObject, TEXT("dx"), ReadIntField(SingleArgsObject, TEXT("deltaX"), 0));
                    const int32 Dy = ReadIntField(SingleArgsObject, TEXT("dy"), ReadIntField(SingleArgsObject, TEXT("deltaY"), 0));
                    FString Error;
                    bool bOk = true;
                    for (const TSharedPtr<FJsonValue>& NodeIdValue : *NodeIdsArray)
                    {
                        FString NodeId;
                        if (!NodeIdValue.IsValid() || !NodeIdValue->TryGetString(NodeId) || NodeId.IsEmpty())
                        {
                            continue;
                        }
                        UEdGraphNode* TargetNode = ResolveBlueprintGraphNodeByToken(TargetGraph, NodeId);
                        if (TargetNode == nullptr)
                        {
                            bOk = false;
                            Error = FString::Printf(TEXT("Failed to resolve moveNodes target: %s"), *NodeId);
                            break;
                        }
                        if (!FLoomleBlueprintAdapter::MoveNode(AssetPath, EffectiveGraphName, NodeId, TargetNode->NodePosX + Dx, TargetNode->NodePosY + Dy, Error))
                        {
                            bOk = false;
                            break;
                        }
                    }
                    SingleResult = BuildDirectSingleResult(bOk, bOk, TEXT(""), Error);
                }
            }
        }
        else if (OpName.Equals(TEXT("setnodecomment")))
        {
            FString TargetNodeId;
            if (!ResolveSingleNodeToken(SingleArgsObject, TargetNodeId))
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("setNodeComment requires a target node reference."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""), TargetNodeId);
            }
            else
            {
                FString Comment;
                SingleArgsObject->TryGetStringField(TEXT("comment"), Comment);
                if (Comment.IsEmpty())
                {
                    SingleArgsObject->TryGetStringField(TEXT("value"), Comment);
                }
                FString Error;
                const bool bOk = FLoomleBlueprintAdapter::SetNodeComment(AssetPath, EffectiveGraphName, TargetNodeId, Comment, Error);
                SingleResult = BuildDirectSingleResult(bOk, bOk && !bDryRun, TEXT(""), Error, TargetNodeId);
            }
        }
        else if (OpName.Equals(TEXT("setnodeenabled")))
        {
            FString TargetNodeId;
            if (!ResolveSingleNodeToken(SingleArgsObject, TargetNodeId))
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("setNodeEnabled requires a target node reference."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""), TargetNodeId);
            }
            else
            {
                bool bEnabled = true;
                SingleArgsObject->TryGetBoolField(TEXT("enabled"), bEnabled);
                FString Error;
                const bool bOk = FLoomleBlueprintAdapter::SetNodeEnabled(AssetPath, EffectiveGraphName, TargetNodeId, bEnabled, Error);
                SingleResult = BuildDirectSingleResult(bOk, bOk && !bDryRun, TEXT(""), Error, TargetNodeId);
            }
        }
        else if (OpName.Equals(TEXT("addgraph")) || OpName.Equals(TEXT("addfunctiongraph")))
        {
            FString NewGraphName;
            SingleArgsObject->TryGetStringField(TEXT("graphName"), NewGraphName);
            if (NewGraphName.IsEmpty())
            {
                SingleArgsObject->TryGetStringField(TEXT("name"), NewGraphName);
            }
            FString GraphKind;
            if (OpName.Equals(TEXT("addgraph")))
            {
                ReadStringAlias({TEXT("kind")}, GraphKind);
            }
            if (NewGraphName.IsEmpty())
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("addGraph requires graphName."));
            }
            else if (OpName.Equals(TEXT("addgraph")) && GraphKind.Equals(TEXT("macro"), ESearchCase::IgnoreCase))
            {
                if (bDryRun)
                {
                    SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
                }
                else
                {
                    FString Error;
                    const bool bOk = FLoomleBlueprintAdapter::AddMacroGraph(AssetPath, NewGraphName, Error);
                    SingleResult = BuildDirectSingleResult(bOk, bOk && !bDryRun, TEXT(""), Error);
                }
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
            }
            else
            {
                FString Error;
                const bool bOk = FLoomleBlueprintAdapter::AddFunctionGraph(AssetPath, NewGraphName, Error);
                SingleResult = BuildDirectSingleResult(bOk, bOk && !bDryRun, TEXT(""), Error);
            }
        }
        else if (OpName.Equals(TEXT("addmacrograph")))
        {
            FString NewGraphName;
            SingleArgsObject->TryGetStringField(TEXT("graphName"), NewGraphName);
            if (NewGraphName.IsEmpty())
            {
                SingleArgsObject->TryGetStringField(TEXT("name"), NewGraphName);
            }
            if (NewGraphName.IsEmpty())
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("addMacroGraph requires graphName."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
            }
            else
            {
                FString Error;
                const bool bOk = FLoomleBlueprintAdapter::AddMacroGraph(AssetPath, NewGraphName, Error);
                SingleResult = BuildDirectSingleResult(bOk, bOk && !bDryRun, TEXT(""), Error);
            }
        }
        else if (OpName.Equals(TEXT("renamegraph")))
        {
            FString OldGraphName;
            FString NewGraphName;
            SingleArgsObject->TryGetStringField(TEXT("oldGraphName"), OldGraphName);
            SingleArgsObject->TryGetStringField(TEXT("newGraphName"), NewGraphName);
            if (OldGraphName.IsEmpty())
            {
                SingleArgsObject->TryGetStringField(TEXT("graphName"), OldGraphName);
            }
            if (OldGraphName.IsEmpty())
            {
                OldGraphName = EffectiveGraphName;
            }
            if (NewGraphName.IsEmpty())
            {
                SingleArgsObject->TryGetStringField(TEXT("name"), NewGraphName);
            }
            if (OldGraphName.IsEmpty() || NewGraphName.IsEmpty())
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("renameGraph requires oldGraphName and newGraphName."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
            }
            else
            {
                FString Error;
                const bool bOk = FLoomleBlueprintAdapter::RenameGraph(AssetPath, OldGraphName, NewGraphName, Error);
                SingleResult = BuildDirectSingleResult(bOk, bOk && !bDryRun, TEXT(""), Error);
            }
        }
        else if (OpName.Equals(TEXT("deletegraph")))
        {
            FString DeleteGraphName;
            SingleArgsObject->TryGetStringField(TEXT("graphName"), DeleteGraphName);
            if (DeleteGraphName.IsEmpty())
            {
                SingleArgsObject->TryGetStringField(TEXT("name"), DeleteGraphName);
            }
            if (DeleteGraphName.IsEmpty())
            {
                DeleteGraphName = EffectiveGraphName;
            }
            if (DeleteGraphName.IsEmpty())
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("deleteGraph requires graphName."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
            }
            else
            {
                FString Error;
                const bool bOk = FLoomleBlueprintAdapter::DeleteGraph(AssetPath, DeleteGraphName, Error);
                SingleResult = BuildDirectSingleResult(bOk, bOk && !bDryRun, TEXT(""), Error);
            }
        }

        if (!SingleResult.IsValid())
        {
            SingleResult = BuildDirectSingleResult(
                false,
                false,
                TEXT("UNSUPPORTED_OP"),
                FString::Printf(TEXT("blueprint.graph.edit does not support op: %s"), *OpName));
        }

        if (!SingleResult.IsValid())
        {
            TSharedPtr<FJsonObject> FallbackOpResult = MakeShared<FJsonObject>();
            FallbackOpResult->SetNumberField(TEXT("index"), Index);
            FallbackOpResult->SetStringField(TEXT("op"), OpName);
            FallbackOpResult->SetBoolField(TEXT("ok"), false);
            FallbackOpResult->SetStringField(TEXT("errorCode"), TEXT("INTERNAL_ERROR"));
            FallbackOpResult->SetStringField(TEXT("errorMessage"), TEXT("Single-op blueprint mutate returned an invalid result."));
            FallbackOpResult->SetNumberField(TEXT("durationMs"), (FPlatformTime::Seconds() - OpStartSeconds) * 1000.0);
            OpResults.Add(MakeShared<FJsonValueObject>(FallbackOpResult));
            bAnyError = true;
            if (FirstErrorCode.IsEmpty())
            {
                FirstErrorCode = TEXT("INTERNAL_ERROR");
            }
            if (FirstErrorMessage.IsEmpty())
            {
                FirstErrorMessage = TEXT("Single-op blueprint mutate returned an invalid result.");
            }
            if (bStopOnError)
            {
                break;
            }
            continue;
        }

        const TArray<TSharedPtr<FJsonValue>> SingleDiagnostics = CloneBlueprintJsonArrayField(SingleResult, TEXT("diagnostics"));
        Diagnostics.Append(SingleDiagnostics);

        bool bSingleError = false;
        SingleResult->TryGetBoolField(TEXT("isError"), bSingleError);
        const TArray<TSharedPtr<FJsonValue>> SingleOpResults = CloneBlueprintJsonArrayField(SingleResult, TEXT("opResults"));
        const TSharedPtr<FJsonObject>* FirstSingleOpResult = nullptr;
        if (SingleOpResults.Num() > 0
            && SingleOpResults[0].IsValid()
            && SingleOpResults[0]->TryGetObject(FirstSingleOpResult)
            && FirstSingleOpResult != nullptr
            && (*FirstSingleOpResult).IsValid())
        {
            TSharedPtr<FJsonObject> RewrittenOpResult = CloneJsonObject(*FirstSingleOpResult);
            if (!RewrittenOpResult.IsValid())
            {
                RewrittenOpResult = *FirstSingleOpResult;
            }
            RewrittenOpResult->SetNumberField(TEXT("index"), Index);
            RewrittenOpResult->SetNumberField(TEXT("durationMs"), (FPlatformTime::Seconds() - OpStartSeconds) * 1000.0);
            OpResults.Add(MakeShared<FJsonValueObject>(RewrittenOpResult));

            FString CreatedNodeId;
            if (RewrittenOpResult->TryGetStringField(TEXT("nodeId"), CreatedNodeId) && !CreatedNodeId.IsEmpty() && !ClientRef.IsEmpty())
            {
                NodeRefs.FindOrAdd(ClientRef) = CreatedNodeId;
            }

            bool bChanged = false;
            RewrittenOpResult->TryGetBoolField(TEXT("changed"), bChanged);
            if (bChanged && !bDryRun)
            {
                bAnyChanged = true;
            }
        }
        else
        {
            TSharedPtr<FJsonObject> FallbackOpResult = MakeShared<FJsonObject>();
            FallbackOpResult->SetNumberField(TEXT("index"), Index);
            FallbackOpResult->SetStringField(TEXT("op"), OpName);
            FallbackOpResult->SetBoolField(TEXT("ok"), !bSingleError);
            FString SingleCode;
            FString SingleMessage;
            SingleResult->TryGetStringField(TEXT("code"), SingleCode);
            SingleResult->TryGetStringField(TEXT("message"), SingleMessage);
            FallbackOpResult->SetStringField(TEXT("errorCode"), bSingleError ? SingleCode : TEXT(""));
            FallbackOpResult->SetStringField(TEXT("errorMessage"), bSingleError ? SingleMessage : TEXT(""));
            FallbackOpResult->SetNumberField(TEXT("durationMs"), (FPlatformTime::Seconds() - OpStartSeconds) * 1000.0);
            OpResults.Add(MakeShared<FJsonValueObject>(FallbackOpResult));
        }

        if (bSingleError)
        {
            bAnyError = true;
            if (FirstErrorCode.IsEmpty())
            {
                SingleResult->TryGetStringField(TEXT("code"), FirstErrorCode);
            }
            if (FirstErrorMessage.IsEmpty())
            {
                SingleResult->TryGetStringField(TEXT("message"), FirstErrorMessage);
            }
            if (bStopOnError)
            {
                break;
            }
        }
    }

    FlushDeferredGraphNotifications();

    TSharedPtr<FJsonObject> AggregateDiff = MakeShared<FJsonObject>();
    static const TCHAR* DiffFields[] = {
        TEXT("nodesAdded"),
        TEXT("nodesRemoved"),
        TEXT("nodesMoved"),
        TEXT("pinDefaultsChanged"),
        TEXT("linksAdded"),
        TEXT("linksRemoved"),
        TEXT("eventReplicationChanged"),
    };
    for (const TCHAR* DiffField : DiffFields)
    {
        AggregateDiff->SetArrayField(DiffField, {});
    }
    for (const TSharedPtr<FJsonValue>& OpResultValue : OpResults)
    {
        const TSharedPtr<FJsonObject>* OpResultObject = nullptr;
        if (!OpResultValue.IsValid()
            || !OpResultValue->TryGetObject(OpResultObject)
            || OpResultObject == nullptr
            || !(*OpResultObject).IsValid())
        {
            continue;
        }

        const TSharedPtr<FJsonObject>* OpDiff = nullptr;
        if (!(*OpResultObject)->TryGetObjectField(TEXT("diff"), OpDiff)
            || OpDiff == nullptr
            || !(*OpDiff).IsValid())
        {
            continue;
        }

        for (const TCHAR* DiffField : DiffFields)
        {
            TArray<TSharedPtr<FJsonValue>> AggregateValues = CloneBlueprintJsonArrayField(AggregateDiff, DiffField);
            const TArray<TSharedPtr<FJsonValue>> OpValues = CloneBlueprintJsonArrayField(*OpDiff, DiffField);
            AggregateValues.Append(OpValues);
            AggregateDiff->SetArrayField(DiffField, AggregateValues);
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), bAnyError);
    if (bAnyError)
    {
        Result->SetStringField(TEXT("code"), FirstErrorCode.IsEmpty() ? TEXT("INTERNAL_ERROR") : FirstErrorCode);
        Result->SetStringField(TEXT("message"), FirstErrorMessage.IsEmpty() ? TEXT("blueprint.graph.edit failed") : FirstErrorMessage);
    }
    Result->SetBoolField(TEXT("applied"), !bDryRun && !bAnyError);
    Result->SetBoolField(TEXT("dryRun"), bDryRun);
    Result->SetBoolField(TEXT("partialApplied"), bAnyError && bAnyChanged);
    Result->SetStringField(TEXT("graphType"), TEXT("blueprint"));
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("graphName"), GraphName);
    Result->SetObjectField(TEXT("graphRef"), MakeBlueprintEffectiveGraphRef(AssetPath, GraphName, InlineNodeGuid));
    Result->SetStringField(TEXT("previousRevision"), PreviousRevision);

    FString NewRevision = PreviousRevision;
    if (!bDryRun && !bAnyError)
    {
        FString NewRevisionCode;
        FString NewRevisionMessage;
        if (ResolveRevision(GraphName, InlineNodeGuid, NewRevision, NewRevisionCode, NewRevisionMessage))
        {
            Result->SetStringField(TEXT("newRevision"), NewRevision);
        }
        else
        {
            Result->SetStringField(TEXT("newRevision"), PreviousRevision);
        }
    }
    else
    {
        Result->SetStringField(TEXT("newRevision"), PreviousRevision);
    }

    Result->SetArrayField(TEXT("opResults"), OpResults);
    Result->SetObjectField(TEXT("diff"), AggregateDiff);
    Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
    if (!IdempotencyRegistryKey.IsEmpty() && !bAnyError)
    {
        FScopeLock Lock(&MutateIdempotencyRegistryMutex);
        MutateIdempotencyRegistry.FindOrAdd(IdempotencyRegistryKey) = FMutateIdempotencyEntry{
            RequestFingerprint,
            CloneJsonObject(Result),
            FPlatformTime::Seconds(),
        };
        while (MutateIdempotencyRegistry.Num() > LoomleBridgeConstants::MaxMutateIdempotencyEntries)
        {
            FString OldestKey;
            double OldestTime = TNumericLimits<double>::Max();
            for (const TPair<FString, FMutateIdempotencyEntry>& Entry : MutateIdempotencyRegistry)
            {
                if (Entry.Value.CreatedAtSeconds < OldestTime)
                {
                    OldestTime = Entry.Value.CreatedAtSeconds;
                    OldestKey = Entry.Key;
                }
            }
            if (OldestKey.IsEmpty())
            {
                break;
            }
            MutateIdempotencyRegistry.Remove(OldestKey);
        }
    }
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildBlueprintCompileToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    if (!bDiagnosticStoreInitialized)
    {
        InitializeDiagnosticStore();
    }

    uint64 DiagnosticSeqBeforeVerify = 0;
    {
        FScopeLock Lock(&DiagnosticStoreMutex);
        DiagnosticSeqBeforeVerify = NextDiagnosticSeq > 0 ? (NextDiagnosticSeq - 1) : 0;
    }

    FString RequestedAssetPath;
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("assetPath"), RequestedAssetPath);
        RequestedAssetPath = NormalizeAssetPath(RequestedAssetPath);
    }

    auto ReadRecentBlueprintVerifyDiagnosticEvents = [&](const uint64 FromSeq, const FString& AssetPath) -> TArray<TSharedPtr<FJsonValue>>
    {
        TArray<TSharedPtr<FJsonValue>> Items;

        FScopeLock Lock(&DiagnosticStoreMutex);
        TArray<FString> Lines;
        if (DiagnosticStoreFilePath.IsEmpty() || !FPaths::FileExists(DiagnosticStoreFilePath))
        {
            return Items;
        }

        FFileHelper::LoadFileToStringArray(Lines, *DiagnosticStoreFilePath);
        for (const FString& Line : Lines)
        {
            if (Line.TrimStartAndEnd().IsEmpty())
            {
                continue;
            }

            TSharedPtr<FJsonObject> Event;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
            if (!FJsonSerializer::Deserialize(Reader, Event) || !Event.IsValid())
            {
                continue;
            }

            uint64 Seq = 0;
            if (!TryReadBlueprintJsonUInt64Field(Event, TEXT("seq"), Seq) || Seq <= FromSeq)
            {
                continue;
            }

            FString EventAssetPath;
            Event->TryGetStringField(TEXT("assetPath"), EventAssetPath);
            if (EventAssetPath.IsEmpty())
            {
                const TSharedPtr<FJsonObject>* Context = nullptr;
                if (Event->TryGetObjectField(TEXT("context"), Context) && Context != nullptr && (*Context).IsValid())
                {
                    (*Context)->TryGetStringField(TEXT("assetPath"), EventAssetPath);
                }
            }

            if (!AssetPath.IsEmpty() && !EventAssetPath.IsEmpty() && EventAssetPath.StartsWith(AssetPath))
            {
                Items.Add(MakeShared<FJsonValueObject>(Event));
                if (Items.Num() >= 64)
                {
                    break;
                }
            }
        }

        return Items;
    };

    const TSharedPtr<FJsonObject> QueryResult = BuildBlueprintGraphInspectToolResult(Arguments);
    bool bQueryError = false;
    QueryResult->TryGetBoolField(TEXT("isError"), bQueryError);
    if (bQueryError)
    {
        return QueryResult;
    }

    const TSharedPtr<FJsonObject> MutateArgs = CloneJsonObject(Arguments);
    TArray<TSharedPtr<FJsonValue>> Ops;
    TSharedPtr<FJsonObject> CompileOp = MakeShared<FJsonObject>();
    CompileOp->SetStringField(TEXT("op"), TEXT("compile"));
    Ops.Add(MakeShared<FJsonValueObject>(CompileOp));
    MutateArgs->SetArrayField(TEXT("ops"), Ops);

    const TSharedPtr<FJsonObject> MutateResult = BuildBlueprintGraphEditToolResult(MutateArgs);
    bool bMutateError = false;
    MutateResult->TryGetBoolField(TEXT("isError"), bMutateError);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);
    CopyBlueprintOptionalStringField(MutateResult, Result, TEXT("graphType"));
    if (!Result->HasField(TEXT("graphType")))
    {
        CopyBlueprintOptionalStringField(QueryResult, Result, TEXT("graphType"));
    }
    CopyBlueprintOptionalStringField(MutateResult, Result, TEXT("assetPath"));
    if (!Result->HasField(TEXT("assetPath")))
    {
        CopyBlueprintOptionalStringField(QueryResult, Result, TEXT("assetPath"));
    }
    CopyBlueprintOptionalStringField(MutateResult, Result, TEXT("graphName"));
    if (!Result->HasField(TEXT("graphName")))
    {
        CopyBlueprintOptionalStringField(QueryResult, Result, TEXT("graphName"));
    }
    CopyBlueprintOptionalObjectField(MutateResult, Result, TEXT("graphRef"));
    if (!Result->HasField(TEXT("graphRef")))
    {
        CopyBlueprintOptionalObjectField(QueryResult, Result, TEXT("graphRef"));
    }
    CopyBlueprintOptionalStringField(MutateResult, Result, TEXT("previousRevision"));
    CopyBlueprintOptionalStringField(MutateResult, Result, TEXT("newRevision"));

    FString AssetPath;
    Result->TryGetStringField(TEXT("assetPath"), AssetPath);

    const TArray<TSharedPtr<FJsonValue>> QueryDiagnostics = CloneBlueprintJsonArrayField(QueryResult, TEXT("diagnostics"));
    TArray<TSharedPtr<FJsonValue>> CompileDiagnostics = CloneBlueprintJsonArrayField(MutateResult, TEXT("diagnostics"));
    TArray<TSharedPtr<FJsonValue>> Diagnostics = QueryDiagnostics;
    Diagnostics.Append(CompileDiagnostics);
    TSet<FString> SeenDiagnosticKeys = BuildBlueprintDiagnosticIdentitySet(Diagnostics);
    TSet<FString> SeenCompileDiagnosticKeys = BuildBlueprintDiagnosticIdentitySet(CompileDiagnostics);

    TSharedPtr<FJsonObject> QueryReport = MakeShared<FJsonObject>();
    CopyBlueprintOptionalStringField(QueryResult, QueryReport, TEXT("revision"));
    const TSharedPtr<FJsonObject>* Meta = nullptr;
    if (QueryResult->TryGetObjectField(TEXT("meta"), Meta) && Meta != nullptr && (*Meta).IsValid())
    {
        QueryReport->SetObjectField(TEXT("queryMeta"), CloneJsonObject(*Meta));
    }
    QueryReport->SetArrayField(TEXT("diagnostics"), QueryDiagnostics);
    Result->SetObjectField(TEXT("queryReport"), QueryReport);

    const TArray<TSharedPtr<FJsonValue>> OpResults = CloneBlueprintJsonArrayField(MutateResult, TEXT("opResults"));
    bool bCompileOpReportedOk = false;
    bool bHasCompileOpResult = false;
    bool bCompilationChanged = false;
    const TSharedPtr<FJsonObject>* FirstOpResult = nullptr;
    if (OpResults.Num() > 0
        && OpResults[0].IsValid()
        && OpResults[0]->TryGetObject(FirstOpResult)
        && FirstOpResult != nullptr
        && (*FirstOpResult).IsValid())
    {
        bHasCompileOpResult = true;
        (*FirstOpResult)->TryGetBoolField(TEXT("ok"), bCompileOpReportedOk);
        (*FirstOpResult)->TryGetBoolField(TEXT("changed"), bCompilationChanged);
    }
    const bool bCompileSucceeded = !bMutateError && (!bHasCompileOpResult || bCompileOpReportedOk);

    FString MutateCode;
    FString MutateMessage;
    MutateResult->TryGetStringField(TEXT("code"), MutateCode);
    MutateResult->TryGetStringField(TEXT("message"), MutateMessage);

    if (bMutateError)
    {
        TSharedPtr<FJsonObject> CompileFailureDiagnostic = MakeShared<FJsonObject>();
        CompileFailureDiagnostic->SetStringField(TEXT("code"), MutateCode.IsEmpty() ? TEXT("COMPILE_FAILED") : MutateCode);
        CompileFailureDiagnostic->SetStringField(TEXT("severity"), TEXT("error"));
        CompileFailureDiagnostic->SetStringField(TEXT("message"), MutateMessage.IsEmpty() ? TEXT("Graph compile failed.") : MutateMessage);
        CompileFailureDiagnostic->SetStringField(TEXT("sourceKind"), TEXT("compile"));
        if (!AssetPath.IsEmpty())
        {
            CompileFailureDiagnostic->SetStringField(TEXT("assetPath"), AssetPath);
        }
        AppendUniqueBlueprintDiagnostic(CompileDiagnostics, SeenCompileDiagnosticKeys, CompileFailureDiagnostic);
        AppendUniqueBlueprintDiagnostic(Diagnostics, SeenDiagnosticKeys, CompileFailureDiagnostic);
    }

    const TArray<TSharedPtr<FJsonValue>> RecentDiagnosticEvents = ReadRecentBlueprintVerifyDiagnosticEvents(DiagnosticSeqBeforeVerify, AssetPath.IsEmpty() ? RequestedAssetPath : AssetPath);
    for (const TSharedPtr<FJsonValue>& EventValue : RecentDiagnosticEvents)
    {
        const TSharedPtr<FJsonObject>* Event = nullptr;
        if (!EventValue.IsValid() || !EventValue->TryGetObject(Event) || Event == nullptr || !(*Event).IsValid())
        {
            continue;
        }

        const TSharedPtr<FJsonObject> Diagnostic = MakeBlueprintRecentDiagnosticEvent(*Event);
        AppendUniqueBlueprintDiagnostic(CompileDiagnostics, SeenCompileDiagnosticKeys, Diagnostic);
        AppendUniqueBlueprintDiagnostic(Diagnostics, SeenDiagnosticKeys, Diagnostic);
    }

    Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
    Result->SetStringField(TEXT("status"), bCompileSucceeded ? DetermineBlueprintVerifyStatus(Diagnostics) : TEXT("error"));
    Result->SetStringField(
        TEXT("summary"),
        bCompileSucceeded
            ? (Diagnostics.Num() > 0
                ? TEXT("Blueprint verification completed with compile-backed confirmation and surfaced diagnostics.")
                : TEXT("Blueprint verification succeeded with compile-backed confirmation."))
            : (RecentDiagnosticEvents.Num() > 0
                ? TEXT("Blueprint verification failed during compile-backed confirmation and captured recent Unreal diagnostic events.")
                : (Diagnostics.Num() > 0
                    ? TEXT("Blueprint verification failed during compile-backed confirmation but did surface follow-up diagnostics.")
                    : TEXT("Blueprint verification failed during compile-backed confirmation and Unreal did not expose deeper diagnostics."))));

    TSharedPtr<FJsonObject> CompileReport = MakeShared<FJsonObject>();
    CompileReport->SetBoolField(TEXT("compiled"), bCompileSucceeded);
    CompileReport->SetBoolField(TEXT("compilationChanged"), bCompilationChanged);
    bool bApplied = false;
    bool bPartialApplied = false;
    MutateResult->TryGetBoolField(TEXT("applied"), bApplied);
    MutateResult->TryGetBoolField(TEXT("partialApplied"), bPartialApplied);
    CompileReport->SetBoolField(TEXT("applied"), bApplied);
    CompileReport->SetBoolField(TEXT("partialApplied"), bPartialApplied);
    CompileReport->SetArrayField(TEXT("opResults"), OpResults);
    CompileReport->SetArrayField(TEXT("diagnostics"), CompileDiagnostics);
    CompileReport->SetArrayField(TEXT("recentEvents"), RecentDiagnosticEvents);
    if (!MutateCode.IsEmpty())
    {
        CompileReport->SetStringField(TEXT("code"), MutateCode);
    }
    if (!MutateMessage.IsEmpty())
    {
        CompileReport->SetStringField(TEXT("message"), MutateMessage);
    }
    if (FirstOpResult != nullptr && (*FirstOpResult).IsValid())
    {
        FString FirstOpErrorCode;
        FString FirstOpErrorMessage;
        if ((*FirstOpResult)->TryGetStringField(TEXT("errorCode"), FirstOpErrorCode) && !FirstOpErrorCode.IsEmpty())
        {
            CompileReport->SetStringField(TEXT("opErrorCode"), FirstOpErrorCode);
        }
        if ((*FirstOpResult)->TryGetStringField(TEXT("errorMessage"), FirstOpErrorMessage) && !FirstOpErrorMessage.IsEmpty())
        {
            CompileReport->SetStringField(TEXT("opErrorMessage"), FirstOpErrorMessage);
        }
    }
    Result->SetObjectField(TEXT("compileReport"), CompileReport);

    if (bMutateError)
    {
        Result->SetStringField(TEXT("code"), MutateCode.IsEmpty() ? TEXT("COMPILE_FAILED") : MutateCode);
        Result->SetStringField(TEXT("message"), MutateMessage.IsEmpty() ? TEXT("Graph compile failed.") : MutateMessage);
    }

    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildBlueprintInspectToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    FString AssetPath;
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("assetPath"), AssetPath);
    }

    AssetPath = NormalizeAssetPath(AssetPath);
    if (AssetPath.IsEmpty())
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("blueprint.inspect requires assetPath."));
        return Result;
    }

    return BuildBlueprintClassDescribeResult(AssetPath);
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildBlueprintPaletteToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    FString AssetPath;
    FString GraphName;
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("assetPath"), AssetPath);
        Arguments->TryGetStringField(TEXT("graphName"), GraphName);
    }

    AssetPath = NormalizeAssetPath(AssetPath);
    if (AssetPath.IsEmpty())
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("blueprint.palette requires assetPath."));
        return Result;
    }

    FString PayloadJson = SerializeBlueprintJsonObjectCondensed(Arguments);
    FString OutJson;
    FString OutError;
    if (!FLoomleBlueprintAdapter::SearchBlueprintPalette(AssetPath, GraphName, PayloadJson, OutJson, OutError))
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("PALETTE_QUERY_FAILED"));
        Result->SetStringField(TEXT("message"), OutError.IsEmpty() ? TEXT("blueprint.palette failed.") : OutError);
        return Result;
    }

    TSharedPtr<FJsonObject> Parsed;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OutJson);
    if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), TEXT("Failed to parse blueprint.palette result."));
        return Result;
    }

    Parsed->SetBoolField(TEXT("isError"), false);
    return Parsed;
}
