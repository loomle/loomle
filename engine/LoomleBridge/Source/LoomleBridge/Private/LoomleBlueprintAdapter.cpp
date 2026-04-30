#include "LoomleBlueprintAdapter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/TimelineTemplate.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "K2Node_AddComponent.h"
#include "K2Node_AddComponentByClass.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Knot.h"
#include "K2Node_Self.h"
#include "K2Node_Timeline.h"
#include "K2Node_Tunnel.h"
#include "K2Node_TunnelBoundary.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphUtilities.h"
#include "Misc/Guid.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/PackageName.h"
#include "UObject/Interface.h"
#include "UObject/UnrealType.h"

using FCondensedJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

namespace LoomleBlueprintAdapterInternal
{
    static const TCHAR* DefaultBlueprintMacroLibraryAssetPath = TEXT("/Engine/EditorBlueprintResources/StandardMacros");

    struct FBlueprintFunctionDescriptor
    {
        FString ClassPath;
        FString FunctionName;
        FString SignatureId;
    };

    struct FBlueprintMacroDescriptor
    {
        FString MacroLibraryAssetPath;
        FString MacroGraphName;
    };

    static FBlueprintMacroDescriptor DescribeMacroNode(const UK2Node_MacroInstance* MacroNode);
    static FString DescribeCustomEventReplication(const uint32 FunctionFlags);
    static FString JsonObjectToCondensedString(const TSharedPtr<FJsonObject>& Object);
    static FString JsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& ArrayValues);
    static bool TryGetStringFieldAny(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> FieldNames, FString& OutValue);
    static TSharedPtr<FJsonObject> TryGetObjectFieldAny(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> FieldNames);
    static TArray<TSharedPtr<FJsonValue>> TryGetArrayFieldAny(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> FieldNames);

    static bool IsMemberEditOperationSupported(const FString& OperationLower, std::initializer_list<const TCHAR*> SupportedOperations)
    {
        for (const TCHAR* SupportedOperation : SupportedOperations)
        {
            if (OperationLower.Equals(SupportedOperation, ESearchCase::CaseSensitive))
            {
                return true;
            }
        }
        return false;
    }

    static FString SuggestMemberEditOperation(const FString& OperationLower)
    {
        if (OperationLower.Equals(TEXT("add"), ESearchCase::CaseSensitive))
        {
            return TEXT("create");
        }
        if (OperationLower.Equals(TEXT("remove"), ESearchCase::CaseSensitive))
        {
            return TEXT("delete");
        }
        return TEXT("");
    }

    static bool RejectUnsupportedMemberEditOperation(
        const FString& MemberKindForMessage,
        const FString& Operation,
        const FString& OperationLower,
        FString& OutError)
    {
        const FString Suggestion = SuggestMemberEditOperation(OperationLower);
        OutError = Suggestion.IsEmpty()
            ? FString::Printf(TEXT("Unsupported %s operation: %s"), *MemberKindForMessage, *Operation)
            : FString::Printf(TEXT("Unsupported %s operation: %s. Did you mean %s?"), *MemberKindForMessage, *Operation, *Suggestion);
        return false;
    }

    static FString BoolToJsonString(bool bValue)
    {
        return bValue ? TEXT("true") : TEXT("false");
    }

    static FString GetEnumValueName(const UEnum* Enum, int64 Value)
    {
        if (Enum == nullptr)
        {
            return TEXT("");
        }

        const FString FullName = Enum->GetNameStringByValue(Value);
        return FullName;
    }

    static TSharedPtr<FJsonObject> MakeTransformSummaryObject(const FTransform& Transform)
    {
        TSharedPtr<FJsonObject> TransformObject = MakeShared<FJsonObject>();

        const FVector Location = Transform.GetLocation();
        const FRotator Rotation = Transform.Rotator();
        const FVector Scale = Transform.GetScale3D();

        TSharedPtr<FJsonObject> LocationObject = MakeShared<FJsonObject>();
        LocationObject->SetNumberField(TEXT("x"), Location.X);
        LocationObject->SetNumberField(TEXT("y"), Location.Y);
        LocationObject->SetNumberField(TEXT("z"), Location.Z);
        TransformObject->SetObjectField(TEXT("location"), LocationObject);

        TSharedPtr<FJsonObject> RotationObject = MakeShared<FJsonObject>();
        RotationObject->SetNumberField(TEXT("pitch"), Rotation.Pitch);
        RotationObject->SetNumberField(TEXT("yaw"), Rotation.Yaw);
        RotationObject->SetNumberField(TEXT("roll"), Rotation.Roll);
        TransformObject->SetObjectField(TEXT("rotation"), RotationObject);

        TSharedPtr<FJsonObject> ScaleObject = MakeShared<FJsonObject>();
        ScaleObject->SetNumberField(TEXT("x"), Scale.X);
        ScaleObject->SetNumberField(TEXT("y"), Scale.Y);
        ScaleObject->SetNumberField(TEXT("z"), Scale.Z);
        TransformObject->SetObjectField(TEXT("scale"), ScaleObject);

        return TransformObject;
    }

    static bool ShouldSurfaceEmbeddedTemplateProperty(const FProperty* Property)
    {
        if (Property == nullptr)
        {
            return false;
        }

        if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_Parm))
        {
            return false;
        }

        if (!Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
        {
            return false;
        }

        if (Property->IsA<FArrayProperty>() || Property->IsA<FSetProperty>() || Property->IsA<FMapProperty>())
        {
            return false;
        }

        if (Property->IsA<FMulticastDelegateProperty>())
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
            || Property->IsA<FObjectPropertyBase>()
            || Property->IsA<FClassProperty>();
    }

    static TSharedPtr<FJsonObject> BuildEmbeddedTemplatePropertySummary(UObject* TemplateObject)
    {
        if (TemplateObject == nullptr || TemplateObject->GetClass() == nullptr)
        {
            return nullptr;
        }

        UObject* ClassDefaults = TemplateObject->GetClass()->GetDefaultObject(false);
        if (ClassDefaults == nullptr)
        {
            return nullptr;
        }

        TArray<TSharedPtr<FJsonValue>> Overrides;
        int32 OverrideCount = 0;
        constexpr int32 MaxSurfacedOverrides = 24;

        for (TFieldIterator<FProperty> PropertyIt(TemplateObject->GetClass(), EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
        {
            FProperty* Property = *PropertyIt;
            if (!ShouldSurfaceEmbeddedTemplateProperty(Property))
            {
                continue;
            }

            if (Property->Identical_InContainer(TemplateObject, ClassDefaults))
            {
                continue;
            }

            ++OverrideCount;
            if (Overrides.Num() >= MaxSurfacedOverrides)
            {
                continue;
            }

            FString ValueText;
            if (!FBlueprintEditorUtils::PropertyValueToString(
                    Property,
                    reinterpret_cast<const uint8*>(TemplateObject),
                    ValueText,
                    TemplateObject))
            {
                continue;
            }

            TSharedPtr<FJsonObject> PropertyObject = MakeShared<FJsonObject>();
            PropertyObject->SetStringField(TEXT("name"), Property->GetName());
            PropertyObject->SetStringField(TEXT("type"), Property->GetClass() ? Property->GetClass()->GetName() : TEXT(""));
            PropertyObject->SetStringField(TEXT("valueText"), ValueText);
            Overrides.Add(MakeShared<FJsonValueObject>(PropertyObject));
        }

        TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
        Summary->SetNumberField(TEXT("overrideCount"), OverrideCount);
        Summary->SetBoolField(TEXT("truncated"), OverrideCount > Overrides.Num());
        Summary->SetArrayField(TEXT("overrides"), Overrides);
        return Summary;
    }

    static TSharedPtr<FJsonObject> BuildTimelineEmbeddedTemplateSummary(const UK2Node_Timeline* TimelineNode)
    {
        if (TimelineNode == nullptr)
        {
            return nullptr;
        }

        UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(const_cast<UK2Node_Timeline*>(TimelineNode));
        if (Blueprint == nullptr)
        {
            return nullptr;
        }

        UTimelineTemplate* TimelineTemplate = Blueprint->FindTimelineTemplateByVariableName(TimelineNode->TimelineName);
        if (TimelineTemplate == nullptr)
        {
            return nullptr;
        }

        TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
        Summary->SetStringField(TEXT("surfaceKind"), TEXT("embedded_template"));
        Summary->SetStringField(TEXT("templateKind"), TEXT("timeline"));
        Summary->SetStringField(TEXT("timelineName"), TimelineNode->TimelineName.ToString());
        Summary->SetStringField(TEXT("templateName"), TimelineTemplate->GetName());
        Summary->SetStringField(TEXT("templatePath"), TimelineTemplate->GetPathName());
        Summary->SetStringField(TEXT("variableName"), TimelineTemplate->GetVariableName().ToString());
        Summary->SetStringField(TEXT("timelineGuid"), TimelineNode->TimelineGuid.ToString(EGuidFormats::DigitsWithHyphens));
        Summary->SetNumberField(TEXT("length"), TimelineTemplate->TimelineLength);
        Summary->SetStringField(TEXT("lengthMode"), GetEnumValueName(StaticEnum<ETimelineLengthMode>(), TimelineTemplate->LengthMode.GetValue()));
        Summary->SetBoolField(TEXT("autoPlay"), TimelineTemplate->bAutoPlay);
        Summary->SetBoolField(TEXT("loop"), TimelineTemplate->bLoop);
        Summary->SetBoolField(TEXT("replicated"), TimelineTemplate->bReplicated);
        Summary->SetBoolField(TEXT("ignoreTimeDilation"), TimelineTemplate->bIgnoreTimeDilation);
        Summary->SetStringField(TEXT("updateFunctionName"), TimelineTemplate->GetUpdateFunctionName().ToString());
        Summary->SetStringField(TEXT("finishedFunctionName"), TimelineTemplate->GetFinishedFunctionName().ToString());

        TSharedPtr<FJsonObject> TrackSummary = MakeShared<FJsonObject>();
        TrackSummary->SetNumberField(TEXT("eventTrackCount"), TimelineTemplate->EventTracks.Num());
        TrackSummary->SetNumberField(TEXT("floatTrackCount"), TimelineTemplate->FloatTracks.Num());
        TrackSummary->SetNumberField(TEXT("vectorTrackCount"), TimelineTemplate->VectorTracks.Num());
        TrackSummary->SetNumberField(TEXT("linearColorTrackCount"), TimelineTemplate->LinearColorTracks.Num());

        TArray<TSharedPtr<FJsonValue>> Tracks;
        for (const FTTEventTrack& Track : TimelineTemplate->EventTracks)
        {
            TSharedPtr<FJsonObject> TrackObject = MakeShared<FJsonObject>();
            TrackObject->SetStringField(TEXT("kind"), TEXT("event"));
            TrackObject->SetStringField(TEXT("name"), Track.GetTrackName().ToString());
            TrackObject->SetStringField(TEXT("functionName"), Track.GetFunctionName().ToString());
            TrackObject->SetBoolField(TEXT("externalCurve"), Track.bIsExternalCurve);
            Tracks.Add(MakeShared<FJsonValueObject>(TrackObject));
        }
        for (const FTTFloatTrack& Track : TimelineTemplate->FloatTracks)
        {
            TSharedPtr<FJsonObject> TrackObject = MakeShared<FJsonObject>();
            TrackObject->SetStringField(TEXT("kind"), TEXT("float"));
            TrackObject->SetStringField(TEXT("name"), Track.GetTrackName().ToString());
            TrackObject->SetStringField(TEXT("propertyName"), Track.GetPropertyName().ToString());
            TrackObject->SetBoolField(TEXT("externalCurve"), Track.bIsExternalCurve);
            Tracks.Add(MakeShared<FJsonValueObject>(TrackObject));
        }
        for (const FTTVectorTrack& Track : TimelineTemplate->VectorTracks)
        {
            TSharedPtr<FJsonObject> TrackObject = MakeShared<FJsonObject>();
            TrackObject->SetStringField(TEXT("kind"), TEXT("vector"));
            TrackObject->SetStringField(TEXT("name"), Track.GetTrackName().ToString());
            TrackObject->SetStringField(TEXT("propertyName"), Track.GetPropertyName().ToString());
            TrackObject->SetBoolField(TEXT("externalCurve"), Track.bIsExternalCurve);
            Tracks.Add(MakeShared<FJsonValueObject>(TrackObject));
        }
        for (const FTTLinearColorTrack& Track : TimelineTemplate->LinearColorTracks)
        {
            TSharedPtr<FJsonObject> TrackObject = MakeShared<FJsonObject>();
            TrackObject->SetStringField(TEXT("kind"), TEXT("linear_color"));
            TrackObject->SetStringField(TEXT("name"), Track.GetTrackName().ToString());
            TrackObject->SetStringField(TEXT("propertyName"), Track.GetPropertyName().ToString());
            TrackObject->SetBoolField(TEXT("externalCurve"), Track.bIsExternalCurve);
            Tracks.Add(MakeShared<FJsonValueObject>(TrackObject));
        }
        TrackSummary->SetArrayField(TEXT("tracks"), Tracks);
        Summary->SetObjectField(TEXT("trackSummary"), TrackSummary);

        return Summary;
    }

    static TSharedPtr<FJsonObject> BuildAddComponentEmbeddedTemplateSummary(const UK2Node_AddComponent* AddComponentNode)
    {
        if (AddComponentNode == nullptr)
        {
            return nullptr;
        }

        TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
        Summary->SetStringField(TEXT("surfaceKind"), TEXT("embedded_template"));
        Summary->SetStringField(TEXT("templateKind"), TEXT("component"));
        Summary->SetStringField(TEXT("templateBlueprint"), AddComponentNode->TemplateBlueprint);
        Summary->SetStringField(TEXT("templateTypeClassPath"), AddComponentNode->TemplateType ? AddComponentNode->TemplateType->GetPathName() : TEXT(""));

        if (const UEdGraphPin* TemplateNamePin = AddComponentNode->FindPin(TEXT("TemplateName")))
        {
            Summary->SetStringField(TEXT("templateName"), TemplateNamePin->DefaultValue);
        }

        bool bManualAttachment = false;
        if (const UEdGraphPin* ManualAttachmentPin = AddComponentNode->FindPin(TEXT("bManualAttachment")))
        {
            bManualAttachment = ManualAttachmentPin->DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase);
            Summary->SetStringField(TEXT("manualAttachmentDefault"), BoolToJsonString(bManualAttachment));
        }
        Summary->SetStringField(TEXT("attachPolicy"), bManualAttachment ? TEXT("manual") : TEXT("auto_root"));

        if (UActorComponent* Template = AddComponentNode->GetTemplateFromNode())
        {
            Summary->SetBoolField(TEXT("templateResolved"), true);
            Summary->SetStringField(TEXT("componentClassPath"), Template->GetClass() ? Template->GetClass()->GetPathName() : TEXT(""));
            Summary->SetStringField(TEXT("templateObjectPath"), Template->GetPathName());
            if (TSharedPtr<FJsonObject> TemplatePropertySummary = BuildEmbeddedTemplatePropertySummary(Template))
            {
                Summary->SetObjectField(TEXT("templatePropertySummary"), TemplatePropertySummary);
            }

            if (const USceneComponent* SceneTemplate = Cast<USceneComponent>(Template))
            {
                Summary->SetObjectField(TEXT("relativeTransformSummary"), MakeTransformSummaryObject(SceneTemplate->GetRelativeTransform()));
                Summary->SetStringField(TEXT("attachParentName"), SceneTemplate->GetAttachParent() ? SceneTemplate->GetAttachParent()->GetName() : TEXT(""));
            }
        }
        else
        {
            Summary->SetBoolField(TEXT("templateResolved"), false);
        }

        return Summary;
    }

    static TArray<TSharedPtr<FJsonValue>> MakeStringValueArray(const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> JsonValues;
        for (const FString& Value : Values)
        {
            JsonValues.Add(MakeShared<FJsonValueString>(Value));
        }
        return JsonValues;
    }

    static TSharedPtr<FJsonObject> BuildPinSignatureSummary(const UEdGraphNode* Node)
    {
        if (Node == nullptr)
        {
            return nullptr;
        }

        TArray<FString> InputPins;
        TArray<FString> OutputPins;
        TArray<FString> ExecInputPins;
        TArray<FString> ExecOutputPins;

        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin == nullptr)
            {
                continue;
            }

            const FString PinName = Pin->PinName.ToString();
            if (Pin->Direction == EGPD_Input)
            {
                InputPins.Add(PinName);
                if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                {
                    ExecInputPins.Add(PinName);
                }
            }
            else if (Pin->Direction == EGPD_Output)
            {
                OutputPins.Add(PinName);
                if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                {
                    ExecOutputPins.Add(PinName);
                }
            }
        }

        TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
        Summary->SetNumberField(TEXT("inputCount"), InputPins.Num());
        Summary->SetNumberField(TEXT("outputCount"), OutputPins.Num());
        Summary->SetNumberField(TEXT("execInputCount"), ExecInputPins.Num());
        Summary->SetNumberField(TEXT("execOutputCount"), ExecOutputPins.Num());
        Summary->SetArrayField(TEXT("inputPins"), MakeStringValueArray(InputPins));
        Summary->SetArrayField(TEXT("outputPins"), MakeStringValueArray(OutputPins));
        Summary->SetArrayField(TEXT("execInputPins"), MakeStringValueArray(ExecInputPins));
        Summary->SetArrayField(TEXT("execOutputPins"), MakeStringValueArray(ExecOutputPins));
        return Summary;
    }

    static TSharedPtr<FJsonObject> BuildAddComponentByClassContextSummary(const UK2Node_AddComponentByClass* AddComponentByClassNode)
    {
        if (AddComponentByClassNode == nullptr)
        {
            return nullptr;
        }

        TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
        Summary->SetStringField(TEXT("surfaceKind"), TEXT("context_sensitive_construct"));
        Summary->SetStringField(TEXT("constructKind"), TEXT("add_component_by_class"));

        const UK2Node_ConstructObjectFromClass* ConstructNode = AddComponentByClassNode;
        const UEdGraphPin* ClassPin = ConstructNode->GetClassPin();
        const UClass* SelectedClass = ConstructNode->GetClassToSpawn();
        const UEdGraphPin* ResultPin = ConstructNode->GetResultPin();
        const UEdGraphPin* ManualAttachmentPin = AddComponentByClassNode->FindPin(TEXT("bManualAttachment"));
        const UEdGraphPin* RelativeTransformPin = AddComponentByClassNode->FindPin(TEXT("RelativeTransform"));

        Summary->SetStringField(TEXT("selectedClassPath"), SelectedClass ? SelectedClass->GetPathName() : TEXT(""));
        Summary->SetStringField(TEXT("selectedClassName"), SelectedClass ? SelectedClass->GetName() : TEXT(""));
        Summary->SetBoolField(TEXT("hasLinkedClassInput"), ClassPin != nullptr && ClassPin->LinkedTo.Num() > 0);
        Summary->SetBoolField(TEXT("classSelected"), SelectedClass != nullptr);

        const bool bIsSceneComponentClass = SelectedClass != nullptr && SelectedClass->IsChildOf(USceneComponent::StaticClass());
        Summary->SetBoolField(TEXT("isSceneComponentClass"), bIsSceneComponentClass);
        Summary->SetStringField(
            TEXT("constructionMode"),
            SelectedClass == nullptr ? TEXT("class_unspecified")
                : (bIsSceneComponentClass ? TEXT("actor_scene_component") : TEXT("actor_component")));

        Summary->SetBoolField(TEXT("manualAttachmentVisible"), ManualAttachmentPin != nullptr && !ManualAttachmentPin->bHidden);
        Summary->SetBoolField(TEXT("relativeTransformVisible"), RelativeTransformPin != nullptr && !RelativeTransformPin->bHidden);
        if (ManualAttachmentPin != nullptr)
        {
            const bool bManualAttachment =
                ManualAttachmentPin->DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase)
                || ManualAttachmentPin->AutogeneratedDefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase);
            Summary->SetStringField(TEXT("manualAttachmentDefault"), BoolToJsonString(bManualAttachment));
        }

        if (ResultPin != nullptr)
        {
            const UObject* ResultClassObject = ResultPin->PinType.PinSubCategoryObject.Get();
            const UClass* ResultClass = Cast<UClass>(ResultClassObject);
            Summary->SetStringField(TEXT("resultClassPath"), ResultClass ? ResultClass->GetPathName() : TEXT(""));
        }

        static const TSet<FName> StaticPins = {
            UEdGraphSchema_K2::PN_Execute,
            UEdGraphSchema_K2::PN_Then,
            UEdGraphSchema_K2::PN_Self,
            UEdGraphSchema_K2::PN_ReturnValue,
            FName(TEXT("Class")),
            FName(TEXT("bManualAttachment")),
            FName(TEXT("RelativeTransform")),
        };

        TArray<TSharedPtr<FJsonValue>> DynamicPins;
        for (const UEdGraphPin* Pin : AddComponentByClassNode->Pins)
        {
            if (Pin == nullptr || Pin->Direction != EGPD_Input || StaticPins.Contains(Pin->PinName))
            {
                continue;
            }

            TSharedPtr<FJsonObject> PinObject = MakeShared<FJsonObject>();
            PinObject->SetStringField(TEXT("name"), Pin->PinName.ToString());
            PinObject->SetBoolField(TEXT("hidden"), Pin->bHidden);
            PinObject->SetBoolField(TEXT("advanced"), Pin->bAdvancedView);
            PinObject->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
            PinObject->SetStringField(
                TEXT("subCategoryObject"),
                Pin->PinType.PinSubCategoryObject.IsValid() && Pin->PinType.PinSubCategoryObject.Get()
                    ? Pin->PinType.PinSubCategoryObject.Get()->GetPathName()
                    : TEXT(""));
            DynamicPins.Add(MakeShared<FJsonValueObject>(PinObject));
        }
        Summary->SetNumberField(TEXT("dynamicPinCount"), DynamicPins.Num());
        Summary->SetArrayField(TEXT("exposedDynamicPins"), DynamicPins);

        TArray<TSharedPtr<FJsonValue>> ContextAssumptions;
        ContextAssumptions.Add(MakeShared<FJsonValueString>(TEXT("requires_actor_execution_context")));
        ContextAssumptions.Add(MakeShared<FJsonValueString>(TEXT("dynamic_pins_follow_selected_component_class")));
        if (bIsSceneComponentClass)
        {
            ContextAssumptions.Add(MakeShared<FJsonValueString>(TEXT("scene_component_attachment_controls_enabled")));
        }
        Summary->SetArrayField(TEXT("contextAssumptions"), ContextAssumptions);

        return Summary;
    }

    static TSharedPtr<FJsonObject> BuildGraphBoundarySummary(const UEdGraphNode* Node)
    {
        if (Node == nullptr)
        {
            return nullptr;
        }

        TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
        Summary->SetStringField(TEXT("surfaceKind"), TEXT("graph_boundary_summary"));

        if (const UEdGraph* OwningGraph = Node->GetGraph())
        {
            Summary->SetStringField(TEXT("owningGraphName"), OwningGraph->GetName());
            Summary->SetStringField(TEXT("owningGraphPath"), OwningGraph->GetPathName());
        }
        Summary->SetObjectField(TEXT("pinSignature"), BuildPinSignatureSummary(Node));

        if (const UK2Node_Composite* CompositeNode = Cast<UK2Node_Composite>(Node))
        {
            Summary->SetStringField(TEXT("structureRole"), TEXT("graph_boundary"));
            Summary->SetStringField(TEXT("boundaryKind"), TEXT("composite"));
            Summary->SetStringField(TEXT("entryExitSemantics"), TEXT("collapsed_subgraph"));
            Summary->SetBoolField(TEXT("hasBoundGraph"), CompositeNode->BoundGraph != nullptr);
            Summary->SetStringField(TEXT("boundGraphName"), CompositeNode->BoundGraph ? CompositeNode->BoundGraph->GetName() : TEXT(""));
            Summary->SetStringField(TEXT("boundGraphPath"), CompositeNode->BoundGraph ? CompositeNode->BoundGraph->GetPathName() : TEXT(""));
            Summary->SetStringField(TEXT("entryNodeId"), CompositeNode->GetEntryNode() ? CompositeNode->GetEntryNode()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens) : TEXT(""));
            Summary->SetStringField(TEXT("exitNodeId"), CompositeNode->GetExitNode() ? CompositeNode->GetExitNode()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens) : TEXT(""));
            return Summary;
        }

        if (const UK2Node_FunctionEntry* FunctionEntryNode = Cast<UK2Node_FunctionEntry>(Node))
        {
            const int32 FunctionFlags = FunctionEntryNode->GetFunctionFlags();
            Summary->SetStringField(TEXT("structureRole"), TEXT("graph_boundary"));
            Summary->SetStringField(TEXT("boundaryKind"), TEXT("function_entry"));
            Summary->SetStringField(TEXT("entryExitSemantics"), TEXT("entry"));
            Summary->SetStringField(TEXT("functionName"), FunctionEntryNode->FunctionReference.GetMemberName().ToString());
            Summary->SetStringField(TEXT("customGeneratedFunctionName"), FunctionEntryNode->CustomGeneratedFunctionName.ToString());
            Summary->SetNumberField(TEXT("localVariableCount"), FunctionEntryNode->LocalVariables.Num());
            Summary->SetBoolField(TEXT("enforceConstCorrectness"), FunctionEntryNode->bEnforceConstCorrectness);
            Summary->SetBoolField(TEXT("isPure"), (FunctionFlags & FUNC_BlueprintPure) != 0);
            Summary->SetBoolField(TEXT("isConst"), (FunctionFlags & FUNC_Const) != 0);
            Summary->SetNumberField(TEXT("functionFlags"), FunctionFlags);
            Summary->SetBoolField(TEXT("isEditable"), true);
            return Summary;
        }

        if (const UK2Node_FunctionResult* FunctionResultNode = Cast<UK2Node_FunctionResult>(Node))
        {
            Summary->SetStringField(TEXT("structureRole"), TEXT("graph_boundary"));
            Summary->SetStringField(TEXT("boundaryKind"), TEXT("function_result"));
            Summary->SetStringField(TEXT("entryExitSemantics"), TEXT("exit"));
            Summary->SetStringField(TEXT("functionName"), FunctionResultNode->FunctionReference.GetMemberName().ToString());
            Summary->SetNumberField(TEXT("resultNodeCount"), FunctionResultNode->GetAllResultNodes().Num());
            Summary->SetBoolField(TEXT("isEditable"), true);
            return Summary;
        }

        if (const UK2Node_CustomEvent* CustomEventNode = Cast<UK2Node_CustomEvent>(Node))
        {
            Summary->SetStringField(TEXT("structureRole"), TEXT("graph_boundary"));
            Summary->SetStringField(TEXT("boundaryKind"), TEXT("custom_event"));
            Summary->SetStringField(TEXT("entryExitSemantics"), TEXT("entry"));
            Summary->SetStringField(TEXT("eventKind"), TEXT("custom"));
            Summary->SetStringField(TEXT("eventName"), CustomEventNode->CustomFunctionName.ToString());
            Summary->SetNumberField(TEXT("functionFlags"), static_cast<int32>(CustomEventNode->FunctionFlags));
            Summary->SetBoolField(TEXT("isReplicated"), (CustomEventNode->FunctionFlags & FUNC_Net) != 0);
            Summary->SetStringField(TEXT("replication"), DescribeCustomEventReplication(CustomEventNode->FunctionFlags));
            Summary->SetBoolField(TEXT("reliable"), (CustomEventNode->FunctionFlags & FUNC_NetReliable) != 0);
            Summary->SetBoolField(TEXT("isOverride"), CustomEventNode->bOverrideFunction);
            Summary->SetBoolField(TEXT("isEditable"), CustomEventNode->IsEditable());
            Summary->SetBoolField(TEXT("callInEditor"), CustomEventNode->bCallInEditor);
            Summary->SetBoolField(TEXT("deprecated"), CustomEventNode->bIsDeprecated);
            Summary->SetStringField(TEXT("deprecationMessage"), CustomEventNode->DeprecationMessage);
            return Summary;
        }

        if (const UK2Node_TunnelBoundary* TunnelBoundaryNode = Cast<UK2Node_TunnelBoundary>(Node))
        {
            Summary->SetStringField(TEXT("structureRole"), TEXT("graph_boundary"));
            Summary->SetStringField(TEXT("boundaryKind"), TEXT("tunnel_boundary"));
            Summary->SetStringField(TEXT("baseName"), TunnelBoundaryNode->BaseName.ToString());
            Summary->SetStringField(
                TEXT("boundarySite"),
                StaticEnum<ETunnelBoundaryType>()
                    ? StaticEnum<ETunnelBoundaryType>()->GetNameStringByValue(static_cast<int64>(TunnelBoundaryNode->GetTunnelBoundaryType()))
                    : TEXT(""));
            Summary->SetStringField(TEXT("entryExitSemantics"), TEXT("boundary_site"));
            return Summary;
        }

        if (const UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
        {
            const FBlueprintMacroDescriptor Descriptor = DescribeMacroNode(MacroNode);
            Summary->SetStringField(TEXT("structureRole"), TEXT("graph_boundary"));
            Summary->SetStringField(TEXT("boundaryKind"), TEXT("macro_instance"));
            Summary->SetStringField(TEXT("entryExitSemantics"), TEXT("macro_bridge"));
            Summary->SetStringField(TEXT("macroGraphName"), Descriptor.MacroGraphName);
            Summary->SetStringField(TEXT("macroLibraryAssetPath"), Descriptor.MacroLibraryAssetPath);
            Summary->SetStringField(TEXT("macroSourceBlueprintPath"), MacroNode->GetSourceBlueprint() ? MacroNode->GetSourceBlueprint()->GetPathName() : TEXT(""));
            return Summary;
        }

        if (const UK2Node_Tunnel* TunnelNode = Cast<UK2Node_Tunnel>(Node))
        {
            Summary->SetStringField(TEXT("structureRole"), TEXT("graph_boundary"));
            Summary->SetStringField(
                TEXT("boundaryKind"),
                TunnelNode->DrawNodeAsEntry() ? TEXT("tunnel_entry")
                    : (TunnelNode->DrawNodeAsExit() ? TEXT("tunnel_exit") : TEXT("tunnel")));
            Summary->SetStringField(
                TEXT("entryExitSemantics"),
                TunnelNode->DrawNodeAsEntry() ? TEXT("entry")
                    : (TunnelNode->DrawNodeAsExit() ? TEXT("exit") : TEXT("bridge")));
            Summary->SetBoolField(TEXT("canHaveInputs"), TunnelNode->bCanHaveInputs);
            Summary->SetBoolField(TEXT("canHaveOutputs"), TunnelNode->bCanHaveOutputs);
            Summary->SetStringField(TEXT("inputSinkNodeId"), TunnelNode->GetInputSink() ? TunnelNode->GetInputSink()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens) : TEXT(""));
            Summary->SetStringField(TEXT("outputSourceNodeId"), TunnelNode->GetOutputSource() ? TunnelNode->GetOutputSource()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens) : TEXT(""));
            return Summary;
        }

        return nullptr;
    }

    static TSharedPtr<FJsonObject> MakeLayoutObject(
        int32 PositionX,
        int32 PositionY,
        const FString& Source,
        bool bReliable,
        const TOptional<FVector2D>& Size = TOptional<FVector2D>())
    {
        TSharedPtr<FJsonObject> Layout = MakeShared<FJsonObject>();

        TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
        Position->SetNumberField(TEXT("x"), PositionX);
        Position->SetNumberField(TEXT("y"), PositionY);
        Layout->SetObjectField(TEXT("position"), Position);

        if (Size.IsSet())
        {
            const FVector2D SizeValue = Size.GetValue();

            TSharedPtr<FJsonObject> SizeObject = MakeShared<FJsonObject>();
            SizeObject->SetNumberField(TEXT("width"), SizeValue.X);
            SizeObject->SetNumberField(TEXT("height"), SizeValue.Y);
            Layout->SetObjectField(TEXT("size"), SizeObject);

            TSharedPtr<FJsonObject> Bounds = MakeShared<FJsonObject>();
            Bounds->SetNumberField(TEXT("left"), PositionX);
            Bounds->SetNumberField(TEXT("top"), PositionY);
            Bounds->SetNumberField(TEXT("right"), PositionX + SizeValue.X);
            Bounds->SetNumberField(TEXT("bottom"), PositionY + SizeValue.Y);
            Layout->SetObjectField(TEXT("bounds"), Bounds);
        }

        Layout->SetStringField(TEXT("source"), Source);
        Layout->SetBoolField(TEXT("reliable"), bReliable);
        Layout->SetStringField(TEXT("sizeSource"), Size.IsSet() ? TEXT("model") : TEXT("unsupported"));
        Layout->SetStringField(TEXT("boundsSource"), Size.IsSet() ? TEXT("model") : TEXT("unsupported"));
        return Layout;
    }

    static UBlueprint* LoadBlueprintByAssetPath(const FString& AssetPath)
    {
        if (!FPackageName::IsValidLongPackageName(AssetPath))
        {
            return nullptr;
        }

        const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
        const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
        return LoadObject<UBlueprint>(nullptr, *ObjectPath);
    }

    static UUserDefinedEnum* LoadUserDefinedEnumByAssetPath(const FString& AssetPath)
    {
        if (!FPackageName::IsValidLongPackageName(AssetPath))
        {
            return nullptr;
        }

        const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
        const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
        return LoadObject<UUserDefinedEnum>(nullptr, *ObjectPath);
    }

    static TSharedPtr<FJsonObject> SerializeUserDefinedEnum(const UUserDefinedEnum* Enum)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        if (Enum == nullptr)
        {
            return Result;
        }

        Result->SetStringField(TEXT("assetPath"), Enum->GetOutermost() ? Enum->GetOutermost()->GetName() : TEXT(""));
        Result->SetStringField(TEXT("enumPath"), Enum->GetPathName());
        Result->SetStringField(TEXT("name"), Enum->GetName());
        Result->SetBoolField(TEXT("isUserDefinedEnum"), true);

        TArray<TSharedPtr<FJsonValue>> Entries;
        const int32 EntryCount = FMath::Max(0, Enum->NumEnums() - 1);
        for (int32 Index = 0; Index < EntryCount; ++Index)
        {
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("name"), Enum->GetAuthoredNameStringByIndex(Index));
            Entry->SetStringField(TEXT("fullName"), Enum->GetNameStringByIndex(Index));
            Entry->SetStringField(TEXT("displayName"), Enum->GetDisplayNameTextByIndex(Index).ToString());
            Entry->SetNumberField(TEXT("value"), static_cast<double>(Enum->GetValueByIndex(Index)));
            Entries.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Result->SetArrayField(TEXT("entries"), Entries);
        Result->SetNumberField(TEXT("entryCount"), EntryCount);
        return Result;
    }

    static bool ParseEnumEntrySpecs(
        const TSharedPtr<FJsonObject>& Payload,
        TArray<FString>& OutEntryNames,
        TMap<FString, FString>& OutDisplayNames,
        FString& OutError)
    {
        OutEntryNames.Reset();
        OutDisplayNames.Reset();
        OutError.Empty();

        const TArray<TSharedPtr<FJsonValue>> Entries = TryGetArrayFieldAny(Payload, {TEXT("entries"), TEXT("enumerators")});
        if (Entries.Num() == 0)
        {
            OutError = TEXT("Blueprint enum edit requires args.entries.");
            return false;
        }

        TSet<FString> SeenNames;
        for (const TSharedPtr<FJsonValue>& EntryValue : Entries)
        {
            FString Name;
            FString DisplayName;
            if (EntryValue.IsValid() && EntryValue->Type == EJson::String)
            {
                Name = EntryValue->AsString();
            }
            else
            {
                const TSharedPtr<FJsonObject> EntryObject = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
                if (!EntryObject.IsValid())
                {
                    OutError = TEXT("Blueprint enum entries must be strings or objects.");
                    return false;
                }
                TryGetStringFieldAny(EntryObject, {TEXT("name"), TEXT("entryName"), TEXT("enumeratorName")}, Name);
                TryGetStringFieldAny(EntryObject, {TEXT("displayName"), TEXT("display")}, DisplayName);
            }

            Name = Name.TrimStartAndEnd();
            if (Name.IsEmpty())
            {
                OutError = TEXT("Blueprint enum entry name cannot be empty.");
                return false;
            }
            if (Name.Contains(TEXT("::")) || Name.Contains(TEXT(".")) || Name.Contains(TEXT(" ")))
            {
                OutError = FString::Printf(TEXT("Blueprint enum entry names must be short names without spaces or qualifiers: %s"), *Name);
                return false;
            }
            if (SeenNames.Contains(Name))
            {
                OutError = FString::Printf(TEXT("Duplicate Blueprint enum entry name: %s"), *Name);
                return false;
            }

            SeenNames.Add(Name);
            OutEntryNames.Add(Name);
            if (!DisplayName.IsEmpty())
            {
                OutDisplayNames.Add(Name, DisplayName);
            }
        }

        const TSharedPtr<FJsonObject> DisplayNamesObject = TryGetObjectFieldAny(Payload, {TEXT("displayNames")});
        if (DisplayNamesObject.IsValid())
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : DisplayNamesObject->Values)
            {
                if (Pair.Value.IsValid())
                {
                    OutDisplayNames.Add(Pair.Key, Pair.Value->AsString());
                }
            }
        }

        return true;
    }

    static bool ApplyUserDefinedEnumEntries(
        UUserDefinedEnum* Enum,
        const TArray<FString>& EntryNames,
        const TMap<FString, FString>& DisplayNames,
        FString& OutError)
    {
        OutError.Empty();
        if (Enum == nullptr)
        {
            OutError = TEXT("Blueprint enum asset not found.");
            return false;
        }

        TArray<TPair<FName, int64>> Names;
        for (int32 Index = 0; Index < EntryNames.Num(); ++Index)
        {
            const FString& EntryName = EntryNames[Index];
            const FName ShortName(*EntryName);
            const FName FullName(*Enum->GenerateFullEnumName(*EntryName));
            if (!ShortName.IsValidXName(INVALID_OBJECTNAME_CHARACTERS))
            {
                OutError = FString::Printf(TEXT("Invalid Blueprint enum entry name: %s"), *EntryName);
                return false;
            }
            Names.Emplace(FullName, Index);
        }

        Enum->Modify();
        if (!Enum->SetEnums(Names, UEnum::ECppForm::Namespaced))
        {
            OutError = TEXT("Failed to update Blueprint enum entries.");
            return false;
        }
        FEnumEditorUtils::EnsureAllDisplayNamesExist(Enum);

        for (int32 Index = 0; Index < EntryNames.Num(); ++Index)
        {
            if (const FString* DisplayName = DisplayNames.Find(EntryNames[Index]))
            {
                FEnumEditorUtils::SetEnumeratorDisplayName(Enum, Index, FText::FromString(*DisplayName));
            }
        }

        Enum->MarkPackageDirty();
        return true;
    }

    static UClass* ResolveClass(const FString& ClassPathOrName)
    {
        if (ClassPathOrName.IsEmpty())
        {
            return nullptr;
        }

        if (UClass* LoadedClass = LoadObject<UClass>(nullptr, *ClassPathOrName))
        {
            return LoadedClass;
        }

        if (UObject* Found = StaticFindObject(UObject::StaticClass(), nullptr, *ClassPathOrName))
        {
            return Cast<UClass>(Found);
        }

        const FString Normalized = ClassPathOrName.StartsWith(TEXT("/Script/"))
            ? ClassPathOrName
            : FString::Printf(TEXT("/Script/Engine.%s"), *ClassPathOrName);
        return LoadObject<UClass>(nullptr, *Normalized);
    }

    static FString NormalizeAssetObjectPath(const FString& PathOrPackage)
    {
        if (PathOrPackage.IsEmpty() || PathOrPackage.Contains(TEXT(".")) || PathOrPackage.StartsWith(TEXT("/Script/")))
        {
            return PathOrPackage;
        }
        if (FPackageName::IsValidLongPackageName(PathOrPackage))
        {
            const FString AssetName = FPackageName::GetLongPackageAssetName(PathOrPackage);
            return FString::Printf(TEXT("%s.%s"), *PathOrPackage, *AssetName);
        }
        return PathOrPackage;
    }

    static UObject* ResolveDefaultObject(const FString& ObjectPath)
    {
        if (ObjectPath.IsEmpty())
        {
            return nullptr;
        }

        const FString NormalizedPath = NormalizeAssetObjectPath(ObjectPath);
        if (UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *NormalizedPath))
        {
            return Loaded;
        }
        return ResolveClass(ObjectPath);
    }

    static UEdGraph* GetEventGraph(UBlueprint* Blueprint)
    {
        return Blueprint ? FBlueprintEditorUtils::FindEventGraph(Blueprint) : nullptr;
    }

    static UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName)
    {
        if (!Blueprint || GraphName.IsEmpty())
        {
            return nullptr;
        }

        auto Match = [&GraphName](UEdGraph* Graph) -> bool
        {
            return Graph && (Graph->GetName().Equals(GraphName) || Graph->GetFName().ToString().Equals(GraphName));
        };

        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            if (Match(Graph))
            {
                return Graph;
            }
        }
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (Match(Graph))
            {
                return Graph;
            }
        }
        for (UEdGraph* Graph : Blueprint->MacroGraphs)
        {
            if (Match(Graph))
            {
                return Graph;
            }
        }
        for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
        {
            if (Match(Graph))
            {
                return Graph;
            }
        }
        for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
        {
            for (UEdGraph* Graph : InterfaceDesc.Graphs)
            {
                if (Match(Graph))
                {
                    return Graph;
                }
            }
        }
        return nullptr;
    }

    static UEdGraph* FindGraphById(UBlueprint* Blueprint, const FString& GraphId)
    {
        if (!Blueprint || GraphId.IsEmpty())
        {
            return nullptr;
        }

        FGuid ParsedGuid;
        const bool bHasParsedGuid = FGuid::Parse(GraphId, ParsedGuid);
        auto Match = [&GraphId, &ParsedGuid, bHasParsedGuid](UEdGraph* Graph) -> bool
        {
            if (!Graph)
            {
                return false;
            }
            if (bHasParsedGuid && Graph->GraphGuid == ParsedGuid)
            {
                return true;
            }
            return Graph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphensLower).Equals(GraphId, ESearchCase::IgnoreCase);
        };

        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            if (Match(Graph))
            {
                return Graph;
            }
        }
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (Match(Graph))
            {
                return Graph;
            }
        }
        for (UEdGraph* Graph : Blueprint->MacroGraphs)
        {
            if (Match(Graph))
            {
                return Graph;
            }
        }
        for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
        {
            if (Match(Graph))
            {
                return Graph;
            }
        }
        for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
        {
            for (UEdGraph* Graph : InterfaceDesc.Graphs)
            {
                if (Match(Graph))
                {
                    return Graph;
                }
            }
        }
        return nullptr;
    }

    static UEdGraph* ResolveTargetGraph(UBlueprint* Blueprint, const FString& GraphName)
    {
        if (!Blueprint)
        {
            return nullptr;
        }

        if (GraphName.IsEmpty())
        {
            return GetEventGraph(Blueprint);
        }

        if (UEdGraph* NamedGraph = FindGraphByName(Blueprint, GraphName))
        {
            return NamedGraph;
        }

        if (GraphName.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase))
        {
            return GetEventGraph(Blueprint);
        }

        return nullptr;
    }

    static UEdGraph* ResolveMacroGraph(const FString& MacroLibraryAssetPath, const FString& MacroGraphName)
    {
        if (MacroGraphName.IsEmpty())
        {
            return nullptr;
        }

        const FString EffectiveMacroLibraryAssetPath = MacroLibraryAssetPath.IsEmpty()
            ? FString(DefaultBlueprintMacroLibraryAssetPath)
            : MacroLibraryAssetPath;

        UBlueprint* MacroLibrary = LoadBlueprintByAssetPath(EffectiveMacroLibraryAssetPath);
        return FindGraphByName(MacroLibrary, MacroGraphName);
    }

    static FString ResolveMacroLibraryAssetPathOrDefault(const FString& MacroLibraryAssetPath)
    {
        return MacroLibraryAssetPath.IsEmpty()
            ? FString(DefaultBlueprintMacroLibraryAssetPath)
            : MacroLibraryAssetPath;
    }

    static TArray<TSharedPtr<FJsonValue>> MakeMacroGraphNameArray(UBlueprint* MacroLibrary)
    {
        TArray<TSharedPtr<FJsonValue>> Names;
        if (MacroLibrary == nullptr)
        {
            return Names;
        }
        for (UEdGraph* MacroGraph : MacroLibrary->MacroGraphs)
        {
            if (MacroGraph != nullptr)
            {
                Names.Add(MakeShared<FJsonValueString>(MacroGraph->GetName()));
            }
        }
        Names.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
        {
            FString Left;
            FString Right;
            if (A.IsValid())
            {
                A->TryGetString(Left);
            }
            if (B.IsValid())
            {
                B->TryGetString(Right);
            }
            return Left < Right;
        });
        return Names;
    }

    static FString MakeMacroNodeError(
        const FString& Code,
        const FString& Message,
        const FString& MacroLibraryAssetPath,
        const FString& MacroGraphName,
        UBlueprint* MacroLibrary = nullptr)
    {
        TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
        Error->SetStringField(TEXT("code"), Code);
        Error->SetStringField(TEXT("message"), Message);

        TSharedPtr<FJsonObject> Requested = MakeShared<FJsonObject>();
        Requested->SetStringField(TEXT("macroLibraryAssetPath"), MacroLibraryAssetPath);
        Requested->SetStringField(TEXT("macroGraphName"), MacroGraphName);
        Error->SetObjectField(TEXT("requested"), Requested);

        if (MacroLibrary != nullptr)
        {
            Error->SetArrayField(TEXT("availableMacroGraphs"), MakeMacroGraphNameArray(MacroLibrary));
            Error->SetStringField(TEXT("suggestion"), TEXT("Use one of availableMacroGraphs as macroGraphName, or inspect the target macro library asset for the exact graph name."));
        }
        return JsonObjectToCondensedString(Error);
    }

    static FBlueprintFunctionDescriptor DescribeCallFunctionNode(const UK2Node_CallFunction* CallNode)
    {
        FBlueprintFunctionDescriptor Descriptor;
        if (CallNode == nullptr)
        {
            return Descriptor;
        }

        const UFunction* TargetFunction = CallNode->GetTargetFunction();
        UClass* ParentClass = CallNode->FunctionReference.GetMemberParentClass();
        FName MemberName = CallNode->FunctionReference.GetMemberName();
        FGuid MemberGuid = CallNode->FunctionReference.GetMemberGuid();

        if ((!ParentClass || MemberName.IsNone()) && TargetFunction)
        {
            ParentClass = TargetFunction->GetOuterUClass();
            MemberName = TargetFunction->GetFName();
        }

        Descriptor.ClassPath = ParentClass ? ParentClass->GetPathName() : TEXT("");
        Descriptor.FunctionName = MemberName.ToString();
        Descriptor.SignatureId = MemberGuid.ToString(EGuidFormats::DigitsWithHyphens);
        if ((Descriptor.SignatureId.IsEmpty()
                || Descriptor.SignatureId.Equals(TEXT("00000000-0000-0000-0000-000000000000")))
            && TargetFunction)
        {
            Descriptor.SignatureId = TargetFunction->GetPathName();
        }

        return Descriptor;
    }

    static UFunction* ResolveFunctionByDescriptor(const FBlueprintFunctionDescriptor& Descriptor)
    {
        if (!Descriptor.SignatureId.IsEmpty()
            && !Descriptor.SignatureId.Equals(TEXT("00000000-0000-0000-0000-000000000000")))
        {
            if (UFunction* FunctionBySignature = FindObject<UFunction>(nullptr, *Descriptor.SignatureId))
            {
                return FunctionBySignature;
            }
            if (UFunction* LoadedFunctionBySignature = LoadObject<UFunction>(nullptr, *Descriptor.SignatureId))
            {
                return LoadedFunctionBySignature;
            }
        }

        UClass* FunctionClass = ResolveClass(Descriptor.ClassPath);
        if (FunctionClass == nullptr || Descriptor.FunctionName.IsEmpty())
        {
            return nullptr;
        }
        return FunctionClass->FindFunctionByName(*Descriptor.FunctionName);
    }

    static bool AddCallFunctionNodeByDescriptor(
        const FString& BlueprintAssetPath,
        const FString& GraphName,
        const FBlueprintFunctionDescriptor& Descriptor,
        const int32 NodePosX,
        const int32 NodePosY,
        FString& OutNodeGuid,
        FString& OutError)
    {
        OutNodeGuid.Empty();
        OutError.Empty();

        UBlueprint* Blueprint = LoadBlueprintByAssetPath(BlueprintAssetPath);
        UEdGraph* EventGraph = ResolveTargetGraph(Blueprint, GraphName);
        UFunction* Function = ResolveFunctionByDescriptor(Descriptor);
        if (!Blueprint || !EventGraph || !Function)
        {
            OutError = TEXT("Failed to resolve blueprint/target graph/function.");
            return false;
        }

        FGraphNodeCreator<UK2Node_CallFunction> Creator(*EventGraph);
        UK2Node_CallFunction* Node = Creator.CreateNode();
        Node->NodePosX = NodePosX;
        Node->NodePosY = NodePosY;
        Node->SetFromFunction(Function);
        Node->AllocateDefaultPins();
        Creator.Finalize();

        OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
        return true;
    }

    static FBlueprintFunctionDescriptor ParseFunctionDescriptorFromPayload(const TSharedPtr<FJsonObject>& Payload)
    {
        FBlueprintFunctionDescriptor Descriptor;
        if (!Payload.IsValid())
        {
            return Descriptor;
        }

        const TSharedPtr<FJsonObject>* FunctionReference = nullptr;
        if (Payload->TryGetObjectField(TEXT("functionReference"), FunctionReference)
            && FunctionReference != nullptr
            && (*FunctionReference).IsValid())
        {
            (*FunctionReference)->TryGetStringField(TEXT("classPath"), Descriptor.ClassPath);
            (*FunctionReference)->TryGetStringField(TEXT("functionName"), Descriptor.FunctionName);
            (*FunctionReference)->TryGetStringField(TEXT("signatureId"), Descriptor.SignatureId);
        }

        if (Descriptor.ClassPath.IsEmpty())
        {
            Payload->TryGetStringField(TEXT("functionClassPath"), Descriptor.ClassPath);
        }
        if (Descriptor.FunctionName.IsEmpty())
        {
            Payload->TryGetStringField(TEXT("functionName"), Descriptor.FunctionName);
        }
        if (Descriptor.SignatureId.IsEmpty())
        {
            Payload->TryGetStringField(TEXT("signatureId"), Descriptor.SignatureId);
        }

        return Descriptor;
    }

    static FBlueprintMacroDescriptor DescribeMacroNode(const UK2Node_MacroInstance* MacroNode)
    {
        FBlueprintMacroDescriptor Descriptor;
        if (MacroNode == nullptr)
        {
            return Descriptor;
        }

        if (UEdGraph* MacroGraph = MacroNode->GetMacroGraph())
        {
            Descriptor.MacroGraphName = MacroGraph->GetName();
            if (UBlueprint* MacroLibrary = FBlueprintEditorUtils::FindBlueprintForGraph(MacroGraph))
            {
                Descriptor.MacroLibraryAssetPath = MacroLibrary->GetOutermost()
                    ? MacroLibrary->GetOutermost()->GetName()
                    : MacroLibrary->GetPathName();
            }
        }

        if (Descriptor.MacroLibraryAssetPath.IsEmpty() && !Descriptor.MacroGraphName.IsEmpty())
        {
            Descriptor.MacroLibraryAssetPath = DefaultBlueprintMacroLibraryAssetPath;
        }

        return Descriptor;
    }

    static FBlueprintMacroDescriptor ParseMacroDescriptorFromPayload(const TSharedPtr<FJsonObject>& Payload)
    {
        FBlueprintMacroDescriptor Descriptor;
        if (!Payload.IsValid())
        {
            return Descriptor;
        }

        const TSharedPtr<FJsonObject>* MacroObject = nullptr;
        if (Payload->TryGetObjectField(TEXT("macro"), MacroObject)
            && MacroObject != nullptr
            && (*MacroObject).IsValid())
        {
            (*MacroObject)->TryGetStringField(TEXT("macroLibraryAssetPath"), Descriptor.MacroLibraryAssetPath);
            (*MacroObject)->TryGetStringField(TEXT("macroGraphName"), Descriptor.MacroGraphName);
            if (Descriptor.MacroGraphName.IsEmpty())
            {
                (*MacroObject)->TryGetStringField(TEXT("macroGraph"), Descriptor.MacroGraphName);
            }
        }

        if (Descriptor.MacroLibraryAssetPath.IsEmpty())
        {
            Payload->TryGetStringField(TEXT("macroLibraryAssetPath"), Descriptor.MacroLibraryAssetPath);
        }
        if (Descriptor.MacroGraphName.IsEmpty())
        {
            Payload->TryGetStringField(TEXT("macroGraphName"), Descriptor.MacroGraphName);
        }
        if (Descriptor.MacroGraphName.IsEmpty())
        {
            Payload->TryGetStringField(TEXT("macroGraph"), Descriptor.MacroGraphName);
        }

        return Descriptor;
    }

    static bool AddSelfNode(
        const FString& BlueprintAssetPath,
        const FString& GraphName,
        const int32 NodePosX,
        const int32 NodePosY,
        FString& OutNodeGuid,
        FString& OutError)
    {
        OutNodeGuid.Empty();
        OutError.Empty();

        UBlueprint* Blueprint = LoadBlueprintByAssetPath(BlueprintAssetPath);
        UEdGraph* TargetGraph = ResolveTargetGraph(Blueprint, GraphName);
        if (!Blueprint || !TargetGraph)
        {
            OutError = TEXT("Failed to resolve blueprint/target graph.");
            return false;
        }

        FGraphNodeCreator<UK2Node_Self> Creator(*TargetGraph);
        UK2Node_Self* Node = Creator.CreateNode();
        Node->NodePosX = NodePosX;
        Node->NodePosY = NodePosY;
        Node->AllocateDefaultPins();
        Creator.Finalize();

        OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
        return true;
    }

    static bool ParsePayloadJson(const FString& PayloadJson, TSharedPtr<FJsonObject>& OutPayload)
    {
        OutPayload = MakeShared<FJsonObject>();
        if (PayloadJson.IsEmpty())
        {
            return true;
        }

        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PayloadJson);
        return FJsonSerializer::Deserialize(Reader, OutPayload) && OutPayload.IsValid();
    }

    static bool TryGetStringFieldAny(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> FieldNames, FString& OutValue)
    {
        OutValue.Empty();
        if (!Object.IsValid())
        {
            return false;
        }

        for (const TCHAR* FieldName : FieldNames)
        {
            FString Value;
            if (Object->TryGetStringField(FieldName, Value) && !Value.IsEmpty())
            {
                OutValue = Value;
                return true;
            }
        }

        return false;
    }

    static bool TryGetBoolFieldAny(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> FieldNames, bool& OutValue)
    {
        if (!Object.IsValid())
        {
            return false;
        }

        for (const TCHAR* FieldName : FieldNames)
        {
            bool Value = false;
            if (Object->TryGetBoolField(FieldName, Value))
            {
                OutValue = Value;
                return true;
            }
        }

        return false;
    }

    static bool TryGetNumberFieldAny(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> FieldNames, double& OutValue)
    {
        if (!Object.IsValid())
        {
            return false;
        }

        for (const TCHAR* FieldName : FieldNames)
        {
            double Value = 0.0;
            if (Object->TryGetNumberField(FieldName, Value))
            {
                OutValue = Value;
                return true;
            }
        }

        return false;
    }

    static TSharedPtr<FJsonObject> TryGetObjectFieldAny(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> FieldNames)
    {
        if (!Object.IsValid())
        {
            return nullptr;
        }

        for (const TCHAR* FieldName : FieldNames)
        {
            const TSharedPtr<FJsonObject>* Nested = nullptr;
            if (Object->TryGetObjectField(FieldName, Nested) && Nested != nullptr && (*Nested).IsValid())
            {
                return *Nested;
            }
        }

        return nullptr;
    }

    static TArray<TSharedPtr<FJsonValue>> TryGetArrayFieldAny(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> FieldNames)
    {
        if (!Object.IsValid())
        {
            return {};
        }

        for (const TCHAR* FieldName : FieldNames)
        {
            const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
            if (Object->TryGetArrayField(FieldName, Values) && Values != nullptr)
            {
                return *Values;
            }
        }

        return {};
    }

    static UEnum* ResolveEnum(const FString& EnumPathOrName)
    {
        if (EnumPathOrName.IsEmpty())
        {
            return nullptr;
        }

        if (UEnum* EnumByPath = FindObject<UEnum>(nullptr, *EnumPathOrName))
        {
            return EnumByPath;
        }
        if (UEnum* LoadedEnum = LoadObject<UEnum>(nullptr, *EnumPathOrName))
        {
            return LoadedEnum;
        }
        return FindFirstObject<UEnum>(*EnumPathOrName);
    }

    static UScriptStruct* ResolveStruct(const FString& StructPathOrName)
    {
        if (StructPathOrName.IsEmpty())
        {
            return nullptr;
        }

        if (UScriptStruct* StructByPath = FindObject<UScriptStruct>(nullptr, *StructPathOrName))
        {
            return StructByPath;
        }
        if (UScriptStruct* LoadedStruct = LoadObject<UScriptStruct>(nullptr, *StructPathOrName))
        {
            return LoadedStruct;
        }
        return FindFirstObject<UScriptStruct>(*StructPathOrName);
    }

    static bool ParseVectorObject(const TSharedPtr<FJsonObject>& Object, FVector& OutValue)
    {
        if (!Object.IsValid())
        {
            return false;
        }

        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
        if (!TryGetNumberFieldAny(Object, {TEXT("x")}, X)
            || !TryGetNumberFieldAny(Object, {TEXT("y")}, Y)
            || !TryGetNumberFieldAny(Object, {TEXT("z")}, Z))
        {
            return false;
        }

        OutValue = FVector(X, Y, Z);
        return true;
    }

    static bool TryReadVectorFieldAny(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> FieldNames, FVector& OutValue)
    {
        if (!Object.IsValid())
        {
            return false;
        }

        for (const TCHAR* FieldName : FieldNames)
        {
            if (TSharedPtr<FJsonObject> Nested = TryGetObjectFieldAny(Object, {FieldName}))
            {
                if (ParseVectorObject(Nested, OutValue))
                {
                    return true;
                }
            }
        }

        return ParseVectorObject(Object, OutValue);
    }

    static bool ParsePinTypeObject(const TSharedPtr<FJsonObject>& Object, FEdGraphPinType& OutPinType, FString& OutError)
    {
        OutError.Empty();
        if (!Object.IsValid())
        {
            OutError = TEXT("Pin type object is required.");
            return false;
        }

        FString Category;
        if (!TryGetStringFieldAny(Object, {TEXT("category"), TEXT("type")}, Category))
        {
            OutError = TEXT("Pin type requires category.");
            return false;
        }

        const FString CategoryLower = Category.ToLower();
        OutPinType = FEdGraphPinType();
        OutPinType.ContainerType = EPinContainerType::None;

        FString ObjectPath;
        TryGetStringFieldAny(Object, {TEXT("objectPath"), TEXT("object"), TEXT("classPath"), TEXT("structPath"), TEXT("enumPath")}, ObjectPath);

        if (CategoryLower.Equals(TEXT("exec")))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
        }
        else if (CategoryLower.Equals(TEXT("bool")))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
        }
        else if (CategoryLower.Equals(TEXT("byte")))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
        }
        else if (CategoryLower.Equals(TEXT("int")))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
        }
        else if (CategoryLower.Equals(TEXT("int64")))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
        }
        else if (CategoryLower.Equals(TEXT("float")) || CategoryLower.Equals(TEXT("real")))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
            OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
        }
        else if (CategoryLower.Equals(TEXT("double")))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
            OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
        }
        else if (CategoryLower.Equals(TEXT("name")))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
        }
        else if (CategoryLower.Equals(TEXT("string")))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
        }
        else if (CategoryLower.Equals(TEXT("text")))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
        }
        else if (CategoryLower.Equals(TEXT("object")))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
            OutPinType.PinSubCategoryObject = ResolveClass(ObjectPath);
        }
        else if (CategoryLower.Equals(TEXT("class")))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Class;
            OutPinType.PinSubCategoryObject = ResolveClass(ObjectPath);
        }
        else if (CategoryLower.Equals(TEXT("softobject")))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
            OutPinType.PinSubCategoryObject = ResolveClass(ObjectPath);
        }
        else if (CategoryLower.Equals(TEXT("softclass")))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
            OutPinType.PinSubCategoryObject = ResolveClass(ObjectPath);
        }
        else if (CategoryLower.Equals(TEXT("struct")))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
            OutPinType.PinSubCategoryObject = ResolveStruct(ObjectPath);
        }
        else if (CategoryLower.Equals(TEXT("enum")))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
            OutPinType.PinSubCategoryObject = ResolveEnum(ObjectPath);
        }
        else if (CategoryLower.Equals(TEXT("wildcard")))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
        }
        else
        {
            OutError = FString::Printf(TEXT("Unsupported pin type category: %s"), *Category);
            return false;
        }

        FString Container;
        if (TryGetStringFieldAny(Object, {TEXT("container")}, Container))
        {
            const FString ContainerLower = Container.ToLower();
            if (ContainerLower.Equals(TEXT("array")))
            {
                OutPinType.ContainerType = EPinContainerType::Array;
            }
            else if (ContainerLower.Equals(TEXT("set")))
            {
                OutPinType.ContainerType = EPinContainerType::Set;
            }
            else if (ContainerLower.Equals(TEXT("map")))
            {
                OutPinType.ContainerType = EPinContainerType::Map;
            }
        }

        bool bReference = false;
        if (TryGetBoolFieldAny(Object, {TEXT("reference"), TEXT("isReference")}, bReference))
        {
            OutPinType.bIsReference = bReference;
        }
        bool bConst = false;
        if (TryGetBoolFieldAny(Object, {TEXT("const"), TEXT("isConst")}, bConst))
        {
            OutPinType.bIsConst = bConst;
        }

        const bool bNeedsObject =
            OutPinType.PinCategory == UEdGraphSchema_K2::PC_Object
            || OutPinType.PinCategory == UEdGraphSchema_K2::PC_Class
            || OutPinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject
            || OutPinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass
            || OutPinType.PinCategory == UEdGraphSchema_K2::PC_Struct
            || CategoryLower.Equals(TEXT("enum"));
        if (bNeedsObject && !ObjectPath.IsEmpty() && !OutPinType.PinSubCategoryObject.IsValid())
        {
            OutError = FString::Printf(TEXT("Failed to resolve type object: %s"), *ObjectPath);
            return false;
        }

        return true;
    }

    static bool ParsePinTypeFromPayload(const TSharedPtr<FJsonObject>& Payload, FEdGraphPinType& OutPinType, FString& OutError)
    {
        if (TSharedPtr<FJsonObject> TypeObject = TryGetObjectFieldAny(Payload, {TEXT("type"), TEXT("pinType"), TEXT("varType")}))
        {
            return ParsePinTypeObject(TypeObject, OutPinType, OutError);
        }

        return ParsePinTypeObject(Payload, OutPinType, OutError);
    }

    static FBPVariableDescription* FindVariableDescription(UBlueprint* Blueprint, const FName VariableName)
    {
        if (Blueprint == nullptr || VariableName.IsNone())
        {
            return nullptr;
        }

        const int32 Index = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableName);
        if (Index == INDEX_NONE || !Blueprint->NewVariables.IsValidIndex(Index))
        {
            return nullptr;
        }

        return &Blueprint->NewVariables[Index];
    }

    static UStruct* ResolveBlueprintMemberVariableScope(UBlueprint* Blueprint)
    {
        if (Blueprint == nullptr)
        {
            return nullptr;
        }

        if (Blueprint->SkeletonGeneratedClass != nullptr)
        {
            return Blueprint->SkeletonGeneratedClass;
        }

        return Blueprint->GeneratedClass;
    }

    static UEdGraph* FindFunctionLikeGraph(UBlueprint* Blueprint, const FString& GraphName)
    {
        return FindGraphByName(Blueprint, GraphName);
    }

    static void GetAllUberGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs)
    {
        if (Blueprint == nullptr)
        {
            return;
        }

        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            if (Graph != nullptr)
            {
                OutGraphs.Add(Graph);
            }
        }
    }

    static UK2Node_CustomEvent* FindCustomEventNode(UBlueprint* Blueprint, const FString& EventName)
    {
        if (Blueprint == nullptr || EventName.IsEmpty())
        {
            return nullptr;
        }

        TArray<UEdGraph*> UberGraphs;
        GetAllUberGraphs(Blueprint, UberGraphs);
        for (UEdGraph* Graph : UberGraphs)
        {
            TArray<UK2Node_CustomEvent*> CustomEvents;
            Graph->GetNodesOfClass(CustomEvents);
            for (UK2Node_CustomEvent* CustomEvent : CustomEvents)
            {
                if (CustomEvent != nullptr && CustomEvent->CustomFunctionName.ToString().Equals(EventName, ESearchCase::IgnoreCase))
                {
                    return CustomEvent;
                }
            }
        }

        return nullptr;
    }

    static FString NormalizeBlueprintToken(FString Value)
    {
        Value = Value.TrimStartAndEnd().ToLower();
        Value.ReplaceInline(TEXT("_"), TEXT(""));
        Value.ReplaceInline(TEXT("-"), TEXT(""));
        Value.ReplaceInline(TEXT(" "), TEXT(""));
        return Value;
    }

    static FString DescribeCustomEventReplication(const uint32 FunctionFlags)
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

    static bool TryParseCustomEventReplication(const FString& Value, uint32& OutReplicationFlag, FString& OutError)
    {
        OutError.Empty();
        const FString Normalized = NormalizeBlueprintToken(Value);
        if (Normalized.IsEmpty()
            || Normalized.Equals(TEXT("none"))
            || Normalized.Equals(TEXT("notreplicated"))
            || Normalized.Equals(TEXT("local")))
        {
            OutReplicationFlag = 0;
            return true;
        }
        if (Normalized.Equals(TEXT("server")) || Normalized.Equals(TEXT("runonserver")))
        {
            OutReplicationFlag = FUNC_NetServer;
            return true;
        }
        if (Normalized.Equals(TEXT("client"))
            || Normalized.Equals(TEXT("owningclient"))
            || Normalized.Equals(TEXT("runonowningclient"))
            || Normalized.Equals(TEXT("runonclient")))
        {
            OutReplicationFlag = FUNC_NetClient;
            return true;
        }
        if (Normalized.Equals(TEXT("multicast")) || Normalized.Equals(TEXT("netmulticast")))
        {
            OutReplicationFlag = FUNC_NetMulticast;
            return true;
        }

        OutError = FString::Printf(TEXT("Unsupported custom event replication mode: %s"), *Value);
        return false;
    }

    static bool ApplyCustomEventNetworkSettings(
        UBlueprint* Blueprint,
        UK2Node_CustomEvent* CustomEvent,
        const TSharedPtr<FJsonObject>& Payload,
        FString& OutError)
    {
        OutError.Empty();
        if (Blueprint == nullptr || CustomEvent == nullptr || !Payload.IsValid())
        {
            return true;
        }

        FString Replication;
        const bool bHasReplication = TryGetStringFieldAny(Payload, {TEXT("replication"), TEXT("rpc"), TEXT("netMode")}, Replication);
        bool bReliable = false;
        const bool bHasReliable = TryGetBoolFieldAny(Payload, {TEXT("reliable"), TEXT("isReliable")}, bReliable);
        if (!bHasReplication && !bHasReliable)
        {
            return true;
        }

        CustomEvent->Modify();

        if (bHasReplication)
        {
            uint32 ReplicationFlag = 0;
            if (!TryParseCustomEventReplication(Replication, ReplicationFlag, OutError))
            {
                return false;
            }

            const uint32 FlagsToClear = FUNC_Net | FUNC_NetMulticast | FUNC_NetServer | FUNC_NetClient;
            CustomEvent->FunctionFlags &= ~FlagsToClear;
            if (ReplicationFlag != 0)
            {
                CustomEvent->FunctionFlags |= FUNC_Net | ReplicationFlag;
            }
            else
            {
                CustomEvent->FunctionFlags &= ~FUNC_NetReliable;
            }
        }

        if (bHasReliable)
        {
            const bool bHasNetReplication = (CustomEvent->FunctionFlags & FUNC_Net) != 0;
            if (bReliable && !bHasNetReplication)
            {
                OutError = TEXT("Reliable custom events require a replicated event mode.");
                return false;
            }

            if (bReliable)
            {
                CustomEvent->FunctionFlags |= FUNC_NetReliable;
            }
            else
            {
                CustomEvent->FunctionFlags &= ~FUNC_NetReliable;
            }
        }

        CustomEvent->ReconstructNode();
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
        return true;
    }

    static void RemoveAllUserDefinedPins(UK2Node_EditablePinBase* Node)
    {
        if (Node == nullptr)
        {
            return;
        }

        for (int32 Index = Node->UserDefinedPins.Num() - 1; Index >= 0; --Index)
        {
            if (Node->UserDefinedPins[Index].IsValid())
            {
                Node->RemoveUserDefinedPinByName(Node->UserDefinedPins[Index]->PinName);
            }
        }
    }

    static bool ApplyPinSpecsToNode(
        UK2Node_EditablePinBase* Node,
        const TArray<TSharedPtr<FJsonValue>>& Specs,
        const EEdGraphPinDirection DesiredDirection,
        FString& OutError)
    {
        OutError.Empty();
        if (Node == nullptr)
        {
            OutError = TEXT("Editable graph node is missing.");
            return false;
        }

        RemoveAllUserDefinedPins(Node);

        for (const TSharedPtr<FJsonValue>& Value : Specs)
        {
            const TSharedPtr<FJsonObject> Spec = Value.IsValid() ? Value->AsObject() : nullptr;
            if (!Spec.IsValid())
            {
                OutError = TEXT("Pin specification entries must be objects.");
                return false;
            }

            FString PinNameText;
            if (!TryGetStringFieldAny(Spec, {TEXT("name"), TEXT("pinName")}, PinNameText))
            {
                OutError = TEXT("Pin specification requires name.");
                return false;
            }

            FEdGraphPinType PinType;
            if (!ParsePinTypeFromPayload(Spec, PinType, OutError))
            {
                return false;
            }

            const FName PinName(*PinNameText);
            if (Node->CreateUserDefinedPin(PinName, PinType, DesiredDirection, false) == nullptr)
            {
                OutError = FString::Printf(TEXT("Failed to create user pin: %s"), *PinNameText);
                return false;
            }

            FString DefaultValue;
            if (TryGetStringFieldAny(Spec, {TEXT("defaultValue"), TEXT("default")}, DefaultValue))
            {
                TSharedPtr<FUserPinInfo>* PinInfo = Node->UserDefinedPins.FindByPredicate(
                    [&PinName](const TSharedPtr<FUserPinInfo>& UserPin)
                    {
                        return UserPin.IsValid() && UserPin->PinName == PinName;
                    });
                if (PinInfo != nullptr && PinInfo->IsValid())
                {
                    (*PinInfo)->PinDefaultValue = DefaultValue;
                    Node->ModifyUserDefinedPinDefaultValue(*PinInfo, DefaultValue);
                }
            }
        }

        Node->ReconstructNode();
        return true;
    }

    static bool ApplySignatureToGraph(
        UBlueprint* Blueprint,
        UEdGraph* Graph,
        const TArray<TSharedPtr<FJsonValue>>& Inputs,
        const TArray<TSharedPtr<FJsonValue>>& Outputs,
        FString& OutError)
    {
        OutError.Empty();
        if (Blueprint == nullptr || Graph == nullptr)
        {
            OutError = TEXT("Failed to resolve blueprint/function graph.");
            return false;
        }

        TWeakObjectPtr<UK2Node_EditablePinBase> EntryNodeWeak;
        TWeakObjectPtr<UK2Node_EditablePinBase> ResultNodeWeak;
        FBlueprintEditorUtils::GetEntryAndResultNodes(Graph, EntryNodeWeak, ResultNodeWeak);

        UK2Node_EditablePinBase* EntryNode = EntryNodeWeak.Get();
        UK2Node_EditablePinBase* ResultNode = ResultNodeWeak.Get();
        if (EntryNode == nullptr)
        {
            OutError = TEXT("Editable entry node was not found.");
            return false;
        }

        if (Outputs.Num() > 0 && ResultNode == nullptr)
        {
            ResultNode = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(EntryNode);
        }

        EntryNode->Modify();
        if (ResultNode != nullptr)
        {
            ResultNode->Modify();
        }

        if (!ApplyPinSpecsToNode(EntryNode, Inputs, EGPD_Output, OutError))
        {
            return false;
        }

        if (ResultNode != nullptr)
        {
            if (!ApplyPinSpecsToNode(ResultNode, Outputs, EGPD_Input, OutError))
            {
                return false;
            }
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
        return true;
    }

    static bool ApplySignatureToCustomEvent(
        UBlueprint* Blueprint,
        UK2Node_CustomEvent* CustomEvent,
        const TArray<TSharedPtr<FJsonValue>>& Inputs,
        FString& OutError)
    {
        OutError.Empty();
        if (Blueprint == nullptr || CustomEvent == nullptr)
        {
            OutError = TEXT("Failed to resolve blueprint/custom event.");
            return false;
        }

        CustomEvent->Modify();
        if (!ApplyPinSpecsToNode(CustomEvent, Inputs, EGPD_Output, OutError))
        {
            return false;
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
        return true;
    }

    static TSharedPtr<FJsonObject> MakeSimplePinSummary(const UEdGraphPin* Pin)
    {
        TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
        if (Pin == nullptr)
        {
            return Summary;
        }

        Summary->SetStringField(TEXT("name"), Pin->PinName.ToString());
        Summary->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Output ? TEXT("output") : TEXT("input"));
        Summary->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
        Summary->SetStringField(TEXT("subCategory"), Pin->PinType.PinSubCategory.ToString());
        Summary->SetStringField(TEXT("object"), Pin->PinType.PinSubCategoryObject.IsValid() ? Pin->PinType.PinSubCategoryObject->GetPathName() : TEXT(""));
        return Summary;
    }

    static TArray<TSharedPtr<FJsonValue>> MakeCustomEventActualPins(const UK2Node_CustomEvent* CustomEvent)
    {
        TArray<TSharedPtr<FJsonValue>> Pins;
        if (CustomEvent == nullptr)
        {
            return Pins;
        }

        for (const UEdGraphPin* Pin : CustomEvent->Pins)
        {
            if (Pin != nullptr)
            {
                Pins.Add(MakeShared<FJsonValueObject>(MakeSimplePinSummary(Pin)));
            }
        }
        return Pins;
    }

    static TArray<TSharedPtr<FJsonValue>> MakeCustomEventActualInputs(const UK2Node_CustomEvent* CustomEvent)
    {
        TArray<TSharedPtr<FJsonValue>> Inputs;
        if (CustomEvent == nullptr)
        {
            return Inputs;
        }

        for (const UEdGraphPin* Pin : CustomEvent->Pins)
        {
            if (Pin == nullptr || Pin->Direction != EGPD_Output || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
            {
                continue;
            }
            Inputs.Add(MakeShared<FJsonValueObject>(MakeSimplePinSummary(Pin)));
        }
        return Inputs;
    }

    static FString JsonObjectToCondensedString(const TSharedPtr<FJsonObject>& Object)
    {
        FString Output;
        if (!Object.IsValid())
        {
            return Output;
        }
        TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
        FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
        return Output;
    }

    static FString MakeCustomEventInputError(
        const FString& Code,
        const FString& Reason,
        const FString& Message,
        const FString& Stage,
        const FString& EventName,
        const UK2Node_CustomEvent* CustomEvent,
        const TSharedPtr<FJsonObject>& RequestedInput,
        const FString& Suggestion)
    {
        TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
        Error->SetStringField(TEXT("code"), Code);
        Error->SetStringField(TEXT("reason"), Reason);
        Error->SetStringField(TEXT("message"), Message);
        Error->SetStringField(TEXT("stage"), Stage);
        Error->SetStringField(TEXT("eventName"), EventName);
        Error->SetStringField(TEXT("graphName"), CustomEvent && CustomEvent->GetGraph() ? CustomEvent->GetGraph()->GetName() : TEXT(""));
        Error->SetStringField(TEXT("suggestion"), Suggestion);
        Error->SetArrayField(TEXT("actualInputs"), MakeCustomEventActualInputs(CustomEvent));
        Error->SetArrayField(TEXT("actualPins"), MakeCustomEventActualPins(CustomEvent));
        if (RequestedInput.IsValid())
        {
            Error->SetObjectField(TEXT("requestedInput"), RequestedInput);
        }
        return JsonObjectToCondensedString(Error);
    }

    static bool AddInputToCustomEvent(
        UBlueprint* Blueprint,
        UK2Node_CustomEvent* CustomEvent,
        const TSharedPtr<FJsonObject>& Payload,
        FString& OutError)
    {
        OutError.Empty();
        if (Blueprint == nullptr || CustomEvent == nullptr)
        {
            OutError = MakeCustomEventInputError(
                TEXT("CUSTOM_EVENT_NODE_NOT_EDITABLE"),
                TEXT("nodeNotEditable"),
                TEXT("Failed to resolve editable custom event node."),
                TEXT("resolveTarget"),
                TEXT(""),
                CustomEvent,
                Payload,
                TEXT("Verify memberKind=event targets an existing UK2Node_CustomEvent."));
            return false;
        }

        TSharedPtr<FJsonObject> InputSpec = Payload;
        if (TSharedPtr<FJsonObject> NestedInput = TryGetObjectFieldAny(Payload, {TEXT("input"), TEXT("parameter")}))
        {
            InputSpec = NestedInput;
        }

        FString InputName;
        if (!TryGetStringFieldAny(InputSpec, {TEXT("inputName"), TEXT("name"), TEXT("pinName")}, InputName))
        {
            OutError = MakeCustomEventInputError(
                TEXT("CUSTOM_EVENT_INPUT_REQUIRES_NAME"),
                TEXT("inputNameRequired"),
                TEXT("Custom event addInput requires inputName."),
                TEXT("validateInput"),
                CustomEvent->CustomFunctionName.ToString(),
                CustomEvent,
                InputSpec,
                TEXT("Pass inputName, name, or input.name."));
            return false;
        }

        const FName PinName(*InputName);
        if (CustomEvent->FindPin(PinName) != nullptr)
        {
            OutError = MakeCustomEventInputError(
                TEXT("CUSTOM_EVENT_PIN_NAME_CONFLICT"),
                TEXT("pinNameConflict"),
                FString::Printf(TEXT("Custom event input already exists: %s"), *InputName),
                TEXT("validateInput"),
                CustomEvent->CustomFunctionName.ToString(),
                CustomEvent,
                InputSpec,
                TEXT("Choose a unique inputName or inspect the event's actualInputs before adding."));
            return false;
        }

        FEdGraphPinType PinType;
        FString TypeError;
        if (!ParsePinTypeFromPayload(InputSpec, PinType, TypeError))
        {
            OutError = MakeCustomEventInputError(
                TEXT("CUSTOM_EVENT_INPUT_TYPE_NOT_SUPPORTED"),
                TEXT("typeNotSupported"),
                TypeError.IsEmpty() ? TEXT("Custom event input type is not supported.") : TypeError,
                TEXT("parseType"),
                CustomEvent->CustomFunctionName.ToString(),
                CustomEvent,
                InputSpec,
                TEXT("Use a supported Blueprint pin type shape, for example {\"category\":\"string\"} or {\"category\":\"object\",\"object\":\"/Script/Engine.Actor\"}."));
            return false;
        }

        CustomEvent->Modify();
        if (CustomEvent->CreateUserDefinedPin(PinName, PinType, EGPD_Output, false) == nullptr)
        {
            OutError = MakeCustomEventInputError(
                TEXT("CUSTOM_EVENT_INPUT_CREATE_FAILED"),
                TEXT("nodeNotEditable"),
                FString::Printf(TEXT("Failed to create custom event input pin: %s"), *InputName),
                TEXT("createPin"),
                CustomEvent->CustomFunctionName.ToString(),
                CustomEvent,
                InputSpec,
                TEXT("Inspect actualPins and retry with a valid editable custom event node."));
            return false;
        }

        CustomEvent->ReconstructNode();
        if (CustomEvent->FindPin(PinName) == nullptr)
        {
            OutError = MakeCustomEventInputError(
                TEXT("CUSTOM_EVENT_PIN_REFRESH_REQUIRED"),
                TEXT("pinRefreshRequired"),
                FString::Printf(TEXT("Custom event input was created but pin did not appear after reconstruction: %s"), *InputName),
                TEXT("refreshPins"),
                CustomEvent->CustomFunctionName.ToString(),
                CustomEvent,
                InputSpec,
                TEXT("Compile or refresh the Blueprint, then inspect the event pins before connecting."));
            return false;
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
        return true;
    }

    static bool ReorderUserDefinedPins(
        UK2Node_EditablePinBase* Node,
        const FString& PinNameToMove,
        const FString& TargetPinName,
        const bool bMoveBefore,
        FString& OutError)
    {
        OutError.Empty();
        if (Node == nullptr)
        {
            OutError = TEXT("Editable graph node is missing.");
            return false;
        }

        const FName MoveName(*PinNameToMove);
        const FName TargetName(*TargetPinName);
        const int32 MoveIndex = Node->UserDefinedPins.IndexOfByPredicate(
            [&MoveName](const TSharedPtr<FUserPinInfo>& Pin) { return Pin.IsValid() && Pin->PinName == MoveName; });
        const int32 TargetIndex = Node->UserDefinedPins.IndexOfByPredicate(
            [&TargetName](const TSharedPtr<FUserPinInfo>& Pin) { return Pin.IsValid() && Pin->PinName == TargetName; });
        if (MoveIndex == INDEX_NONE || TargetIndex == INDEX_NONE)
        {
            OutError = TEXT("Pin reorder requires both movePinName and targetPinName to exist.");
            return false;
        }

        int32 NewIndex = TargetIndex;
        if (!bMoveBefore)
        {
            ++NewIndex;
        }
        if (MoveIndex < NewIndex)
        {
            --NewIndex;
        }
        if (MoveIndex == NewIndex)
        {
            return true;
        }

        Node->Modify();
        TSharedPtr<FUserPinInfo> PinInfo = Node->UserDefinedPins[MoveIndex];
        Node->UserDefinedPins.RemoveAt(MoveIndex);
        Node->UserDefinedPins.Insert(PinInfo, NewIndex);
        Node->ReconstructNode();
        return true;
    }

    static void AppendSCSNodeTree(USCS_Node* Node, TArray<USCS_Node*>& OutNodes)
    {
        if (Node == nullptr)
        {
            return;
        }

        OutNodes.Add(Node);
        for (USCS_Node* ChildNode : Node->GetChildNodes())
        {
            AppendSCSNodeTree(ChildNode, OutNodes);
        }
    }

    static void RefreshSCSAllNodes(USimpleConstructionScript* SCS)
    {
        if (SCS == nullptr)
        {
            return;
        }

        TArray<USCS_Node*>& AllNodes = const_cast<TArray<USCS_Node*>&>(SCS->GetAllNodes());
        AllNodes.Reset();
        for (USCS_Node* RootNode : SCS->GetRootNodes())
        {
            if (RootNode != nullptr)
            {
                AppendSCSNodeTree(RootNode, AllNodes);
            }
        }
    }

    static bool ReorderSCSChildren(USCS_Node* ParentNode, USCS_Node* NodeToMove, USCS_Node* TargetNode, const bool bMoveBefore, FString& OutError)
    {
        OutError.Empty();
        if (ParentNode == nullptr)
        {
            OutError = TEXT("Component reorder requires a parent node.");
            return false;
        }

        TArray<USCS_Node*>& Children = const_cast<TArray<USCS_Node*>&>(ParentNode->GetChildNodes());
        const int32 MoveIndex = Children.IndexOfByKey(NodeToMove);
        const int32 TargetIndex = Children.IndexOfByKey(TargetNode);
        if (MoveIndex == INDEX_NONE || TargetIndex == INDEX_NONE)
        {
            OutError = TEXT("Component reorder requires both nodes to share the same parent.");
            return false;
        }
        if (MoveIndex == TargetIndex)
        {
            return true;
        }

        int32 TargetIndexAfterRemoval = TargetIndex;
        if (MoveIndex < TargetIndex)
        {
            --TargetIndexAfterRemoval;
        }
        const int32 InsertIndex = FMath::Clamp(
            bMoveBefore ? TargetIndexAfterRemoval : TargetIndexAfterRemoval + 1,
            0,
            Children.Num() - 1);

        ParentNode->Modify();
        Children.RemoveAt(MoveIndex);
        Children.Insert(NodeToMove, InsertIndex);
        RefreshSCSAllNodes(ParentNode->GetSCS());
        return true;
    }

    static bool ReorderSCSRoots(USimpleConstructionScript* SCS, USCS_Node* NodeToMove, USCS_Node* TargetNode, const bool bMoveBefore, FString& OutError)
    {
        OutError.Empty();
        if (SCS == nullptr)
        {
            OutError = TEXT("Component reorder requires SCS.");
            return false;
        }

        TArray<USCS_Node*>& Roots = const_cast<TArray<USCS_Node*>&>(SCS->GetRootNodes());
        const int32 MoveIndex = Roots.IndexOfByKey(NodeToMove);
        const int32 TargetIndex = Roots.IndexOfByKey(TargetNode);
        if (MoveIndex == INDEX_NONE || TargetIndex == INDEX_NONE)
        {
            OutError = TEXT("Component reorder requires both root nodes to exist.");
            return false;
        }
        if (MoveIndex == TargetIndex)
        {
            return true;
        }

        int32 TargetIndexAfterRemoval = TargetIndex;
        if (MoveIndex < TargetIndex)
        {
            --TargetIndexAfterRemoval;
        }
        const int32 InsertIndex = FMath::Clamp(
            bMoveBefore ? TargetIndexAfterRemoval : TargetIndexAfterRemoval + 1,
            0,
            Roots.Num() - 1);

        SCS->Modify();
        Roots.RemoveAt(MoveIndex);
        Roots.Insert(NodeToMove, InsertIndex);
        RefreshSCSAllNodes(SCS);
        SCS->ValidateSceneRootNodes();
        return true;
    }

    static bool IsSameOrDescendantNode(USCS_Node* CandidateParent, USCS_Node* Node)
    {
        if (CandidateParent == nullptr || Node == nullptr)
        {
            return false;
        }

        if (CandidateParent == Node)
        {
            return true;
        }

        return CandidateParent->IsChildOf(Node);
    }

    static void ClearSCSNodeParent(USCS_Node* Node)
    {
        if (Node == nullptr)
        {
            return;
        }

        Node->Modify();
        Node->bIsParentComponentNative = false;
        Node->ParentComponentOrVariableName = NAME_None;
        Node->ParentComponentOwnerClassName = NAME_None;
    }

    static void AppendGraphListEntries(const TArray<UEdGraph*>& Graphs, const FString& GraphKind, TArray<TSharedPtr<FJsonValue>>& OutGraphs)
    {
        for (UEdGraph* Graph : Graphs)
        {
            if (!Graph)
            {
                continue;
            }

            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            const FString GraphId = Graph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
            Entry->SetStringField(TEXT("id"), GraphId);
            Entry->SetStringField(TEXT("graphId"), GraphId);
            Entry->SetStringField(TEXT("graphName"), Graph->GetName());
            Entry->SetStringField(TEXT("graphKind"), GraphKind);
            Entry->SetStringField(TEXT("graphClassPath"), Graph->GetClass() ? Graph->GetClass()->GetPathName() : TEXT(""));
            OutGraphs.Add(MakeShared<FJsonValueObject>(Entry));
        }
    }

    static UEdGraphNode* ResolveNodeByToken(UEdGraph* Graph, const FString& NodeToken);

    static void CollectRootGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs)
    {
        OutGraphs.Reset();
        if (!Blueprint)
        {
            return;
        }

        OutGraphs.Append(Blueprint->UbergraphPages);
        OutGraphs.Append(Blueprint->FunctionGraphs);
        OutGraphs.Append(Blueprint->MacroGraphs);
    }

    static UEdGraph* ResolveCompositeBoundGraph(UEdGraphNode* Node)
    {
        if (Node == nullptr)
        {
            return nullptr;
        }

        FObjectPropertyBase* BoundGraphProp = FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("BoundGraph"));
        if (BoundGraphProp == nullptr)
        {
            return nullptr;
        }

        return Cast<UEdGraph>(BoundGraphProp->GetObjectPropertyValue_InContainer(Node));
    }

    static bool ResolveCompositeSubgraphByNodeToken(
        UBlueprint* Blueprint,
        const FString& NodeToken,
        UEdGraph*& OutParentGraph,
        UEdGraphNode*& OutOwnerNode,
        UEdGraph*& OutSubgraph,
        FString& OutError)
    {
        OutParentGraph = nullptr;
        OutOwnerNode = nullptr;
        OutSubgraph = nullptr;
        OutError.Empty();

        if (Blueprint == nullptr)
        {
            OutError = TEXT("Blueprint is required.");
            return false;
        }
        if (NodeToken.IsEmpty())
        {
            OutError = TEXT("CompositeNodeGuid is required.");
            return false;
        }

        TArray<UEdGraph*> RootGraphs;
        CollectRootGraphs(Blueprint, RootGraphs);
        for (UEdGraph* Graph : RootGraphs)
        {
            if (Graph == nullptr)
            {
                continue;
            }

            UEdGraphNode* Node = ResolveNodeByToken(Graph, NodeToken);
            if (Node == nullptr)
            {
                continue;
            }

            UEdGraph* SubGraph = ResolveCompositeBoundGraph(Node);
            if (SubGraph == nullptr)
            {
                OutError = FString::Printf(TEXT("Node %s is not a composite node (no BoundGraph property)."), *NodeToken);
                return false;
            }

            OutParentGraph = Graph;
            OutOwnerNode = Node;
            OutSubgraph = SubGraph;
            return true;
        }

        OutError = FString::Printf(TEXT("Node with token %s not found in blueprint %s."), *NodeToken, *Blueprint->GetPathName());
        return false;
    }

    static USCS_Node* FindComponentNode(UBlueprint* Blueprint, const FString& ComponentName)
    {
        if (!Blueprint || !Blueprint->SimpleConstructionScript)
        {
            return nullptr;
        }

        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node && Node->GetVariableName().ToString().Equals(ComponentName))
            {
                return Node;
            }
        }
        return nullptr;
    }

    static UEdGraphNode* FindNodeByGuid(UEdGraph* Graph, const FString& NodeGuidText)
    {
        if (!Graph)
        {
            return nullptr;
        }

        FGuid NodeGuid;
        if (!FGuid::Parse(NodeGuidText, NodeGuid))
        {
            return nullptr;
        }

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node && Node->NodeGuid == NodeGuid)
            {
                return Node;
            }
        }
        return nullptr;
    }

    static UEdGraphNode* FindNodeByPath(UEdGraph* Graph, const FString& NodePath)
    {
        if (!Graph || NodePath.IsEmpty())
        {
            return nullptr;
        }

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node && Node->GetPathName().Equals(NodePath, ESearchCase::IgnoreCase))
            {
                return Node;
            }
        }
        return nullptr;
    }

    static UEdGraphNode* FindNodeByName(UEdGraph* Graph, const FString& NodeName)
    {
        if (!Graph || NodeName.IsEmpty())
        {
            return nullptr;
        }

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node && Node->GetName().Equals(NodeName, ESearchCase::IgnoreCase))
            {
                return Node;
            }
        }
        return nullptr;
    }

    static UEdGraphNode* ResolveNodeByToken(UEdGraph* Graph, const FString& NodeToken)
    {
        if (NodeToken.IsEmpty())
        {
            return nullptr;
        }

        if (UEdGraphNode* Node = FindNodeByGuid(Graph, NodeToken))
        {
            return Node;
        }

        if (UEdGraphNode* Node = FindNodeByPath(Graph, NodeToken))
        {
            return Node;
        }

        return FindNodeByName(Graph, NodeToken);
    }

    static FString NormalizePinToken(FString Value)
    {
        Value = Value.ToLower();
        Value.ReplaceInline(TEXT(" "), TEXT(""));
        Value.ReplaceInline(TEXT("_"), TEXT(""));
        Value.ReplaceInline(TEXT("-"), TEXT(""));
        return Value;
    }

    static FString NormalizeIdentifier(FString Value)
    {
        Value = Value.ToLower();
        FString Out;
        Out.Reserve(Value.Len());
        for (const TCHAR Ch : Value)
        {
            if (FChar::IsAlnum(Ch))
            {
                Out.AppendChar(Ch);
            }
        }
        return Out;
    }

    static bool ResolveVariableReferenceFromNode(const UK2Node_Variable* VariableNode, FString& OutMemberName, UClass*& OutOwnerClass, FString& OutSignatureId)
    {
        OutMemberName = VariableNode ? VariableNode->VariableReference.GetMemberName().ToString() : TEXT("");
        OutOwnerClass = VariableNode ? VariableNode->VariableReference.GetMemberParentClass() : nullptr;
        OutSignatureId = VariableNode ? VariableNode->VariableReference.GetMemberGuid().ToString(EGuidFormats::DigitsWithHyphens) : TEXT("");

        if (!VariableNode)
        {
            return false;
        }

        if (const FProperty* Property = VariableNode->GetPropertyForVariable())
        {
            if (OutMemberName.IsEmpty())
            {
                OutMemberName = Property->GetName();
            }
            if (OutOwnerClass == nullptr)
            {
                OutOwnerClass = Property->GetOwnerClass();
            }
            if (OutSignatureId.IsEmpty() || OutSignatureId.Equals(TEXT("00000000-0000-0000-0000-000000000000")))
            {
                OutSignatureId = FString::Printf(TEXT("%s:%s"),
                    OutOwnerClass ? *OutOwnerClass->GetPathName() : TEXT(""),
                    *Property->GetName());
            }
            return true;
        }

        return !OutMemberName.IsEmpty() || OutOwnerClass != nullptr;
    }

    static UEdGraphPin* ResolvePin(UEdGraphNode* Node, const FString& RequestedName)
    {
        if (!Node)
        {
            return nullptr;
        }

        if (UEdGraphPin* Direct = Node->FindPin(*RequestedName))
        {
            return Direct;
        }

        const FString Key = NormalizePinToken(RequestedName);

        if (UK2Node_DynamicCast* DynamicCastNode = Cast<UK2Node_DynamicCast>(Node))
        {
            if (Key.Equals(TEXT("object")) || Key.Equals(TEXT("objecttocast")))
            {
                if (UEdGraphPin* P = DynamicCastNode->GetCastSourcePin()) return P;
            }
            if (Key.Equals(TEXT("castsucceeded")) || Key.Equals(TEXT("success")) || Key.Equals(TEXT("valid")))
            {
                if (UEdGraphPin* P = DynamicCastNode->GetValidCastPin()) return P;
            }
            if (Key.Equals(TEXT("castfailed")) || Key.Equals(TEXT("invalid")) || Key.Equals(TEXT("fail")))
            {
                if (UEdGraphPin* P = DynamicCastNode->GetInvalidCastPin()) return P;
            }
            if (Key.Equals(TEXT("as")) || Key.Equals(TEXT("result")) || Key.Equals(TEXT("castresult")) || Key.StartsWith(TEXT("as")))
            {
                if (UEdGraphPin* P = DynamicCastNode->GetCastResultPin()) return P;
            }
        }

        auto TrySchemaPin = [Node](const FName& SchemaPinName) -> UEdGraphPin*
        {
            return Node->FindPin(SchemaPinName);
        };

        if (Key.Equals(TEXT("execute")) || Key.Equals(TEXT("execin")))
        {
            if (UEdGraphPin* P = TrySchemaPin(UEdGraphSchema_K2::PN_Execute)) return P;
        }
        if (Key.Equals(TEXT("then")) || Key.Equals(TEXT("execout")))
        {
            if (UEdGraphPin* P = TrySchemaPin(UEdGraphSchema_K2::PN_Then)) return P;
        }
        if (Key.Equals(TEXT("self")))
        {
            if (UEdGraphPin* P = TrySchemaPin(UEdGraphSchema_K2::PN_Self)) return P;
        }
        if (Key.Equals(TEXT("object")) || Key.Equals(TEXT("objecttocast")))
        {
            if (UEdGraphPin* P = TrySchemaPin(UEdGraphSchema_K2::PN_ObjectToCast)) return P;
        }
        if (Key.Equals(TEXT("castsucceeded")) || Key.Equals(TEXT("success")))
        {
            if (UEdGraphPin* P = TrySchemaPin(UEdGraphSchema_K2::PN_CastSucceeded)) return P;
        }
        if (Key.Equals(TEXT("input")) || Key.Equals(TEXT("inputpin")))
        {
            if (UEdGraphPin* P = Node->FindPin(TEXT("InputPin"))) return P;
        }
        if (Key.Equals(TEXT("output")) || Key.Equals(TEXT("outputpin")))
        {
            if (UEdGraphPin* P = Node->FindPin(TEXT("OutputPin"))) return P;
        }

        const FString NormalizedRequested = NormalizePinToken(RequestedName);
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin && NormalizePinToken(Pin->PinName.ToString()).Equals(NormalizedRequested))
            {
                return Pin;
            }
        }
        return nullptr;
    }

    static FString PinDirectionToString(EEdGraphPinDirection Direction)
    {
        switch (Direction)
        {
            case EGPD_Input:
                return TEXT("input");
            case EGPD_Output:
                return TEXT("output");
            default:
                return TEXT("unknown");
        }
    }

    static TSharedPtr<FJsonObject> SerializePin(const UEdGraphPin* Pin)
    {
        TSharedPtr<FJsonObject> PinObject = MakeShared<FJsonObject>();
        if (!Pin)
        {
            return PinObject;
        }

        PinObject->SetStringField(TEXT("name"), Pin->PinName.ToString());
        PinObject->SetStringField(TEXT("direction"), PinDirectionToString(Pin->Direction));
        PinObject->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
        PinObject->SetStringField(TEXT("subCategory"), Pin->PinType.PinSubCategory.ToString());
        PinObject->SetStringField(TEXT("subCategoryObject"), Pin->PinType.PinSubCategoryObject.IsValid() ? Pin->PinType.PinSubCategoryObject->GetPathName() : TEXT(""));
        PinObject->SetBoolField(TEXT("isReference"), Pin->PinType.bIsReference);
        PinObject->SetBoolField(TEXT("isConst"), Pin->PinType.bIsConst);
        PinObject->SetBoolField(TEXT("isArray"), Pin->PinType.ContainerType == EPinContainerType::Array);
        PinObject->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
        PinObject->SetStringField(TEXT("defaultObject"), Pin->DefaultObject ? Pin->DefaultObject->GetPathName() : TEXT(""));
        PinObject->SetStringField(TEXT("defaultText"), Pin->DefaultTextValue.ToString());

        TSharedPtr<FJsonObject> PinTypeObject = MakeShared<FJsonObject>();
        PinTypeObject->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
        PinTypeObject->SetStringField(TEXT("subCategory"), Pin->PinType.PinSubCategory.ToString());
        PinTypeObject->SetStringField(TEXT("object"), Pin->PinType.PinSubCategoryObject.IsValid() ? Pin->PinType.PinSubCategoryObject->GetPathName() : TEXT(""));
        FString ContainerType = TEXT("none");
        if (Pin->PinType.ContainerType == EPinContainerType::Array)
        {
            ContainerType = TEXT("array");
        }
        else if (Pin->PinType.ContainerType == EPinContainerType::Set)
        {
            ContainerType = TEXT("set");
        }
        else if (Pin->PinType.ContainerType == EPinContainerType::Map)
        {
            ContainerType = TEXT("map");
        }
        PinTypeObject->SetStringField(TEXT("container"), ContainerType);
        PinObject->SetObjectField(TEXT("type"), PinTypeObject);

        TSharedPtr<FJsonObject> PinDefaultObject = MakeShared<FJsonObject>();
        PinDefaultObject->SetStringField(TEXT("value"), Pin->DefaultValue);
        PinDefaultObject->SetStringField(TEXT("object"), Pin->DefaultObject ? Pin->DefaultObject->GetPathName() : TEXT(""));
        PinDefaultObject->SetStringField(TEXT("text"), Pin->DefaultTextValue.ToString());
        PinObject->SetObjectField(TEXT("default"), PinDefaultObject);

        TArray<TSharedPtr<FJsonValue>> Links;
        TArray<TSharedPtr<FJsonValue>> SemanticLinks;
        TSet<FString> SeenLinks;
        auto AddLinkedPin = [&Links, &SemanticLinks, &SeenLinks](const UEdGraphPin* LinkedPin)
        {
            if (!LinkedPin)
            {
                return;
            }

            const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNodeUnchecked();
            const FString LinkedNodeGuid = LinkedNode ? LinkedNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens) : TEXT("");
            const FString LinkKey = LinkedNodeGuid + TEXT("|") + LinkedPin->PinName.ToString();
            if (SeenLinks.Contains(LinkKey))
            {
                return;
            }
            SeenLinks.Add(LinkKey);

            TSharedPtr<FJsonObject> LinkObject = MakeShared<FJsonObject>();
            LinkObject->SetStringField(TEXT("pin"), LinkedPin->PinName.ToString());

            if (LinkedNode)
            {
                LinkObject->SetStringField(TEXT("nodeName"), LinkedNode->GetName());
                LinkObject->SetStringField(TEXT("nodeGuid"), LinkedNodeGuid);
                LinkObject->SetStringField(TEXT("nodePath"), LinkedNode->GetPathName());
            }

            Links.Add(MakeShared<FJsonValueObject>(LinkObject));

            TSharedPtr<FJsonObject> SemanticLinkObject = MakeShared<FJsonObject>();
            SemanticLinkObject->SetStringField(TEXT("toPin"), LinkedPin->PinName.ToString());
            SemanticLinkObject->SetStringField(TEXT("toNodeId"), LinkedNodeGuid);
            SemanticLinks.Add(MakeShared<FJsonValueObject>(SemanticLinkObject));
        };

        for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
        {
            AddLinkedPin(LinkedPin);
        }

        if (const UEdGraphNode* OwningNode = Pin->GetOwningNodeUnchecked())
        {
            if (const UEdGraph* Graph = OwningNode->GetGraph())
            {
                for (const UEdGraphNode* GraphNode : Graph->Nodes)
                {
                    if (GraphNode == nullptr)
                    {
                        continue;
                    }
                    for (const UEdGraphPin* CandidatePin : GraphNode->Pins)
                    {
                        if (CandidatePin != nullptr && CandidatePin != Pin && CandidatePin->LinkedTo.Contains(Pin))
                        {
                            AddLinkedPin(CandidatePin);
                        }
                    }
                }
            }
        }
        PinObject->SetArrayField(TEXT("linkedTo"), Links);
        PinObject->SetArrayField(TEXT("links"), SemanticLinks);

        return PinObject;
    }

    static TSharedPtr<FJsonObject> SerializeNode(const UEdGraphNode* Node)
    {
        TSharedPtr<FJsonObject> NodeObject = MakeShared<FJsonObject>();
        if (!Node)
        {
            return NodeObject;
        }

        const FString NodeClassPath = Node->GetClass() ? Node->GetClass()->GetPathName() : TEXT("");
        const FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
        NodeObject->SetStringField(TEXT("name"), Node->GetName());
        NodeObject->SetStringField(TEXT("guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        NodeObject->SetStringField(TEXT("id"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        NodeObject->SetStringField(TEXT("className"), Node->GetClass()->GetName());
        NodeObject->SetStringField(TEXT("classPath"), NodeClassPath);
        NodeObject->SetStringField(TEXT("nodeClassPath"), NodeClassPath);
        NodeObject->SetStringField(TEXT("path"), Node->GetPathName());
        NodeObject->SetNumberField(TEXT("nodePosX"), Node->NodePosX);
        NodeObject->SetNumberField(TEXT("nodePosY"), Node->NodePosY);
        NodeObject->SetStringField(TEXT("nodeTitle"), NodeTitle);
        NodeObject->SetStringField(TEXT("nodeTitleFull"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        NodeObject->SetStringField(TEXT("title"), NodeTitle);
        NodeObject->SetBoolField(TEXT("isNodeEnabled"), Node->IsNodeEnabled());
        NodeObject->SetBoolField(TEXT("enabled"), Node->IsNodeEnabled());

        TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
        Position->SetNumberField(TEXT("x"), Node->NodePosX);
        Position->SetNumberField(TEXT("y"), Node->NodePosY);
        NodeObject->SetObjectField(TEXT("position"), Position);
        NodeObject->SetObjectField(
            TEXT("layout"),
            MakeLayoutObject(Node->NodePosX, Node->NodePosY, TEXT("model"), true));

        TSharedPtr<FJsonObject> MemberReference = MakeShared<FJsonObject>();
        MemberReference->SetStringField(TEXT("memberKind"), TEXT(""));
        MemberReference->SetStringField(TEXT("ownerClassPath"), TEXT(""));
        MemberReference->SetStringField(TEXT("memberName"), TEXT(""));
        MemberReference->SetStringField(TEXT("signatureId"), TEXT(""));

        TSharedPtr<FJsonObject> FunctionReference = MakeShared<FJsonObject>();
        FunctionReference->SetStringField(TEXT("classPath"), TEXT(""));
        FunctionReference->SetStringField(TEXT("functionName"), TEXT(""));
        FunctionReference->SetStringField(TEXT("signatureId"), TEXT(""));

        if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
        {
            const FBlueprintFunctionDescriptor Descriptor = DescribeCallFunctionNode(CallNode);
            MemberReference->SetStringField(TEXT("memberKind"), TEXT("function"));
            MemberReference->SetStringField(TEXT("ownerClassPath"), Descriptor.ClassPath);
            MemberReference->SetStringField(TEXT("memberName"), Descriptor.FunctionName);
            MemberReference->SetStringField(TEXT("signatureId"), Descriptor.SignatureId);

            FunctionReference->SetStringField(TEXT("classPath"), Descriptor.ClassPath);
            FunctionReference->SetStringField(TEXT("functionName"), Descriptor.FunctionName);
            FunctionReference->SetStringField(TEXT("signatureId"), Descriptor.SignatureId);
        }
        else if (const UK2Node_VariableGet* VariableGetNode = Cast<UK2Node_VariableGet>(Node))
        {
            FString MemberName;
            UClass* ParentClass = nullptr;
            FString SignatureId;
            ResolveVariableReferenceFromNode(VariableGetNode, MemberName, ParentClass, SignatureId);
            MemberReference->SetStringField(TEXT("memberKind"), TEXT("variable"));
            MemberReference->SetStringField(TEXT("ownerClassPath"), ParentClass ? ParentClass->GetPathName() : TEXT(""));
            MemberReference->SetStringField(TEXT("memberName"), MemberName);
            MemberReference->SetStringField(TEXT("rawMemberName"), VariableGetNode->VariableReference.GetMemberName().ToString());
            MemberReference->SetStringField(TEXT("resolvedOwnerClassPath"), ParentClass ? ParentClass->GetPathName() : TEXT(""));
            MemberReference->SetStringField(TEXT("signatureId"), SignatureId);
        }
        else if (const UK2Node_VariableSet* VariableSetNode = Cast<UK2Node_VariableSet>(Node))
        {
            FString MemberName;
            UClass* ParentClass = nullptr;
            FString SignatureId;
            ResolveVariableReferenceFromNode(VariableSetNode, MemberName, ParentClass, SignatureId);
            MemberReference->SetStringField(TEXT("memberKind"), TEXT("variable"));
            MemberReference->SetStringField(TEXT("ownerClassPath"), ParentClass ? ParentClass->GetPathName() : TEXT(""));
            MemberReference->SetStringField(TEXT("memberName"), MemberName);
            MemberReference->SetStringField(TEXT("rawMemberName"), VariableSetNode->VariableReference.GetMemberName().ToString());
            MemberReference->SetStringField(TEXT("resolvedOwnerClassPath"), ParentClass ? ParentClass->GetPathName() : TEXT(""));
            MemberReference->SetStringField(TEXT("signatureId"), SignatureId);
        }

        NodeObject->SetObjectField(TEXT("memberReference"), MemberReference);
        NodeObject->SetObjectField(TEXT("functionReference"), FunctionReference);

        TSharedPtr<FJsonObject> K2Extensions = MakeShared<FJsonObject>();
        if (const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
        {
            TSharedPtr<FJsonObject> CastExt = MakeShared<FJsonObject>();
            CastExt->SetStringField(TEXT("targetClassPath"), CastNode->TargetType ? CastNode->TargetType->GetPathName() : TEXT(""));
            K2Extensions->SetObjectField(TEXT("cast"), CastExt);
        }
        if (const UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
        {
            const FBlueprintMacroDescriptor Descriptor = DescribeMacroNode(MacroNode);
            TSharedPtr<FJsonObject> MacroExt = MakeShared<FJsonObject>();
            MacroExt->SetStringField(TEXT("macroGraph"), Descriptor.MacroGraphName);
            MacroExt->SetStringField(TEXT("macroGraphName"), Descriptor.MacroGraphName);
            MacroExt->SetStringField(TEXT("macroLibraryAssetPath"), Descriptor.MacroLibraryAssetPath);
            K2Extensions->SetObjectField(TEXT("macro"), MacroExt);
        }
        if (const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
        {
            TSharedPtr<FJsonObject> CommentExt = MakeShared<FJsonObject>();
            CommentExt->SetStringField(TEXT("text"), CommentNode->NodeComment);
            CommentExt->SetNumberField(TEXT("width"), CommentNode->NodeWidth);
            CommentExt->SetNumberField(TEXT("height"), CommentNode->NodeHeight);
            K2Extensions->SetObjectField(TEXT("comment"), CommentExt);
            NodeObject->SetObjectField(
                TEXT("layout"),
                MakeLayoutObject(
                    Node->NodePosX,
                    Node->NodePosY,
                    TEXT("model"),
                    true,
                    FVector2D(CommentNode->NodeWidth, CommentNode->NodeHeight)));
        }
        if (const UK2Node_Timeline* TimelineNode = Cast<UK2Node_Timeline>(Node))
        {
            TSharedPtr<FJsonObject> TimelineExt = MakeShared<FJsonObject>();
            TimelineExt->SetStringField(TEXT("timelineName"), TimelineNode->TimelineName.ToString());
            K2Extensions->SetObjectField(TEXT("timeline"), TimelineExt);
        }
        NodeObject->SetObjectField(TEXT("k2Extensions"), K2Extensions);

        TSharedPtr<FJsonObject> EmbeddedTemplate;
        if (const UK2Node_Timeline* TimelineNode = Cast<UK2Node_Timeline>(Node))
        {
            EmbeddedTemplate = BuildTimelineEmbeddedTemplateSummary(TimelineNode);
        }
        else if (const UK2Node_AddComponent* AddComponentNode = Cast<UK2Node_AddComponent>(Node))
        {
            EmbeddedTemplate = BuildAddComponentEmbeddedTemplateSummary(AddComponentNode);
        }
        if (EmbeddedTemplate.IsValid())
        {
            NodeObject->SetObjectField(TEXT("embeddedTemplate"), EmbeddedTemplate);
            NodeObject->SetObjectField(TEXT("effectiveSettings"), EmbeddedTemplate);
        }

        if (const UK2Node_AddComponentByClass* AddComponentByClassNode = Cast<UK2Node_AddComponentByClass>(Node))
        {
            if (TSharedPtr<FJsonObject> ContextSensitiveConstruct = BuildAddComponentByClassContextSummary(AddComponentByClassNode))
            {
                NodeObject->SetObjectField(TEXT("contextSensitiveConstruct"), ContextSensitiveConstruct);
            }
        }

        if (TSharedPtr<FJsonObject> GraphBoundarySummary = BuildGraphBoundarySummary(Node))
        {
            NodeObject->SetObjectField(TEXT("graphBoundarySummary"), GraphBoundarySummary);
        }

        TArray<TSharedPtr<FJsonValue>> Pins;
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            Pins.Add(MakeShared<FJsonValueObject>(SerializePin(Pin)));
        }
        NodeObject->SetArrayField(TEXT("pins"), Pins);

        return NodeObject;
    }

    static FString JsonObjectToString(const TSharedPtr<FJsonObject>& Object)
    {
        FString Output;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
        return Output;
    }

    static FString JsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& ArrayValues)
    {
        FString Output;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        FJsonSerializer::Serialize(ArrayValues, Writer);
        return Output;
    }

    static bool NodeClassMatches(const UEdGraphNode* Node, const FString& Filter)
    {
        if (!Node || Filter.IsEmpty())
        {
            return false;
        }

        const FString NormalizedFilter = NormalizePinToken(Filter);
        const UClass* NodeClass = Node->GetClass();
        if (!NodeClass)
        {
            return false;
        }

        const FString ClassName = NormalizePinToken(NodeClass->GetName());
        const FString ClassPath = NormalizePinToken(NodeClass->GetPathName());
        return ClassName.Equals(NormalizedFilter) || ClassPath.Equals(NormalizedFilter);
    }

    static bool NodeMatchesListOptions(const UEdGraphNode* Node, const FLoomleBlueprintNodeListOptions* Options)
    {
        if (!Node)
        {
            return false;
        }
        if (Options == nullptr)
        {
            return true;
        }

        const FString NodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
        if (Options->NodeIds.Num() > 0)
        {
            bool bIdMatched = false;
            for (const FString& RequestedId : Options->NodeIds)
            {
                if (RequestedId.Equals(NodeGuid, ESearchCase::IgnoreCase))
                {
                    bIdMatched = true;
                    break;
                }
            }
            if (!bIdMatched)
            {
                return false;
            }
        }

        if (Options->NodeClasses.Num() > 0)
        {
            bool bClassMatched = false;
            for (const FString& NodeClassFilter : Options->NodeClasses)
            {
                if (NodeClassMatches(Node, NodeClassFilter))
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

        if (!Options->Text.IsEmpty())
        {
            const FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
            const FString NodeName = Node->GetName();
            const FString NodeClassPath = Node->GetClass() ? Node->GetClass()->GetPathName() : TEXT("");
            if (!NodeTitle.Contains(Options->Text, ESearchCase::IgnoreCase)
                && !NodeName.Contains(Options->Text, ESearchCase::IgnoreCase)
                && !NodeGuid.Contains(Options->Text, ESearchCase::IgnoreCase)
                && !NodeClassPath.Contains(Options->Text, ESearchCase::IgnoreCase))
            {
                return false;
            }
        }

        return true;
    }

    static void SerializeFilteredNodeList(
        const TArray<UEdGraphNode*>& GraphNodes,
        const FString& GraphName,
        const FLoomleBlueprintNodeListOptions* Options,
        FString& OutNodesJson,
        FLoomleBlueprintNodeListStats* OutStats)
    {
        TArray<TSharedPtr<FJsonValue>> Nodes;
        int32 MatchingNodes = 0;
        const int32 Offset = Options ? FMath::Max(Options->Offset, 0) : 0;
        const int32 Limit = Options ? FMath::Max(Options->Limit, 1) : TNumericLimits<int32>::Max();

        Nodes.Reserve(FMath::Min(Limit, GraphNodes.Num()));
        for (const UEdGraphNode* Node : GraphNodes)
        {
            if (!NodeMatchesListOptions(Node, Options))
            {
                continue;
            }

            ++MatchingNodes;
            if (MatchingNodes <= Offset)
            {
                continue;
            }
            if (Nodes.Num() >= Limit)
            {
                continue;
            }

            TSharedPtr<FJsonObject> NodeObject = SerializeNode(Node);
            NodeObject->SetStringField(TEXT("graphName"), GraphName);
            Nodes.Add(MakeShared<FJsonValueObject>(NodeObject));
        }

        if (OutStats)
        {
            OutStats->TotalNodes = GraphNodes.Num();
            OutStats->MatchingNodes = MatchingNodes;
        }
        OutNodesJson = JsonArrayToString(Nodes);
    }

    static const FProperty* ResolveVariableProperty(
        UBlueprint* Blueprint,
        const FString& VariableName,
        const FString& VariableClassPath,
        bool& bOutSelfContext,
        UClass*& OutOwnerClass,
        FString* OutResolvedVariableName = nullptr)
    {
        bOutSelfContext = true;
        OutOwnerClass = nullptr;
        if (OutResolvedVariableName)
        {
            OutResolvedVariableName->Empty();
        }
        if (!Blueprint || VariableName.IsEmpty())
        {
            return nullptr;
        }

        UClass* SearchClass = nullptr;
        if (!VariableClassPath.IsEmpty())
        {
            SearchClass = ResolveClass(VariableClassPath);
            if (!SearchClass)
            {
                return nullptr;
            }
        }
        else
        {
            SearchClass = Blueprint->GeneratedClass ? Blueprint->GeneratedClass : Blueprint->SkeletonGeneratedClass;
        }

        const FProperty* Property = nullptr;
        const FString TargetNormalized = NormalizeIdentifier(VariableName);
        for (UClass* Class = SearchClass; Class != nullptr && Property == nullptr; Class = Class->GetSuperClass())
        {
            Property = FindFProperty<FProperty>(Class, *VariableName);
            if (Property)
            {
                OutOwnerClass = Class;
                if (OutResolvedVariableName)
                {
                    *OutResolvedVariableName = Property->GetName();
                }
                break;
            }

            for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
            {
                const FProperty* Candidate = *It;
                if (!Candidate)
                {
                    continue;
                }

                const FString CandidateName = Candidate->GetName();
                const FString CandidateDisplay = FName::NameToDisplayString(CandidateName, false);
                const FString CandidateDisplayText = Candidate->GetDisplayNameText().ToString();

                if (NormalizeIdentifier(CandidateName).Equals(TargetNormalized)
                    || NormalizeIdentifier(CandidateDisplay).Equals(TargetNormalized)
                    || NormalizeIdentifier(CandidateDisplayText).Equals(TargetNormalized))
                {
                    Property = Candidate;
                    OutOwnerClass = Class;
                    if (OutResolvedVariableName)
                    {
                        *OutResolvedVariableName = CandidateName;
                    }
                    break;
                }
            }
        }
        if (!Property)
        {
            return nullptr;
        }

        UClass* BlueprintClass = Blueprint->GeneratedClass ? Blueprint->GeneratedClass : Blueprint->SkeletonGeneratedClass;
        bOutSelfContext = (OutOwnerClass == nullptr) || (BlueprintClass != nullptr && OutOwnerClass->IsChildOf(BlueprintClass));
        return Property;
    }

    static bool AddTimelineNode(
        const FString& BlueprintAssetPath,
        const FString& GraphName,
        const TSharedPtr<FJsonObject>& Payload,
        int32 NodePosX,
        int32 NodePosY,
        FString& OutNodeGuid,
        FString& OutError)
    {
        OutNodeGuid.Empty();
        OutError.Empty();

        UBlueprint* Blueprint = LoadBlueprintByAssetPath(BlueprintAssetPath);
        UEdGraph* TargetGraph = ResolveTargetGraph(Blueprint, GraphName);
        if (!Blueprint || !TargetGraph)
        {
            OutError = TEXT("Failed to resolve blueprint/target graph.");
            return false;
        }

        FGraphNodeCreator<UK2Node_Timeline> Creator(*TargetGraph);
        UK2Node_Timeline* Node = Creator.CreateNode();
        if (Node == nullptr)
        {
            OutError = TEXT("Failed to create timeline node.");
            return false;
        }

        Node->NodePosX = NodePosX;
        Node->NodePosY = NodePosY;
        if (!Node->IsCompatibleWithGraph(TargetGraph))
        {
            OutError = TEXT("Timeline is not compatible with the target graph context.");
            return false;
        }

        FString RequestedTimelineName;
        if (Payload.IsValid())
        {
            Payload->TryGetStringField(TEXT("timelineName"), RequestedTimelineName);
        }

        FName TimelineName = NAME_None;
        if (!RequestedTimelineName.IsEmpty())
        {
            const FName RequestedName(*RequestedTimelineName);
            if (Blueprint->FindTimelineTemplateByVariableName(RequestedName) == nullptr)
            {
                TimelineName = RequestedName;
            }
        }
        if (TimelineName.IsNone())
        {
            TimelineName = FBlueprintEditorUtils::FindUniqueTimelineName(Blueprint);
        }

        Node->TimelineName = TimelineName;
        if (UTimelineTemplate* Template = FBlueprintEditorUtils::AddNewTimeline(Blueprint, TimelineName))
        {
            Node->bAutoPlay = Template->bAutoPlay;
            Node->bLoop = Template->bLoop;
            Node->bReplicated = Template->bReplicated;
            Node->bIgnoreTimeDilation = Template->bIgnoreTimeDilation;
            Node->ErrorMsg.Empty();
            Node->bHasCompilerMessage = false;
        }
        else
        {
            OutError = TEXT("Failed to create Blueprint timeline template.");
            return false;
        }

        Node->AllocateDefaultPins();
        Creator.Finalize();
        OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
        return true;
    }

    static bool AddEmbeddedAddComponentNode(
        const FString& BlueprintAssetPath,
        const FString& GraphName,
        const TSharedPtr<FJsonObject>& Payload,
        int32 NodePosX,
        int32 NodePosY,
        FString& OutNodeGuid,
        FString& OutError)
    {
        OutNodeGuid.Empty();
        OutError.Empty();

        UBlueprint* Blueprint = LoadBlueprintByAssetPath(BlueprintAssetPath);
        UEdGraph* TargetGraph = ResolveTargetGraph(Blueprint, GraphName);
        if (!Blueprint || !TargetGraph)
        {
            OutError = TEXT("Failed to resolve blueprint/target graph.");
            return false;
        }

        FString ComponentClassPath;
        FString RequestedComponentName;
        if (Payload.IsValid())
        {
            Payload->TryGetStringField(TEXT("componentClassPath"), ComponentClassPath);
            if (ComponentClassPath.IsEmpty())
            {
                Payload->TryGetStringField(TEXT("templateTypeClassPath"), ComponentClassPath);
            }
            Payload->TryGetStringField(TEXT("componentName"), RequestedComponentName);
        }

        UClass* ComponentClass = ResolveClass(ComponentClassPath);
        if (ComponentClass == nullptr || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
        {
            OutError = FString::Printf(TEXT("Failed to resolve component class: %s"), *ComponentClassPath);
            return false;
        }

        FGraphNodeCreator<UK2Node_AddComponent> Creator(*TargetGraph);
        UK2Node_AddComponent* Node = Creator.CreateNode();
        if (Node == nullptr)
        {
            OutError = TEXT("Failed to create AddComponent node.");
            return false;
        }

        Node->NodePosX = NodePosX;
        Node->NodePosY = NodePosY;

        if (!Node->IsCompatibleWithGraph(TargetGraph))
        {
            OutError = TEXT("AddComponent is not compatible with the target graph context.");
            return false;
        }

        UFunction* AddComponentFunc = FindFieldChecked<UFunction>(AActor::StaticClass(), UK2Node_AddComponent::GetAddComponentFunctionName());
        Node->FunctionReference.SetFromField<UFunction>(AddComponentFunc, FBlueprintEditorUtils::IsActorBased(Blueprint));
        Node->TemplateType = ComponentClass;
        Node->AllocateDefaultPins();
        Creator.Finalize();

        UActorComponent* ComponentTemplate = nullptr;
        if (FKismetEditorUtilities::IsClassABlueprintSpawnableComponent(ComponentClass))
        {
            UObject* TemplateOuter = Blueprint->GeneratedClass ? static_cast<UObject*>(Blueprint->GeneratedClass) : static_cast<UObject*>(Blueprint);
            FString RequestedTemplateName = RequestedComponentName.IsEmpty()
                ? ComponentClass->GetName()
                : RequestedComponentName;
            RequestedTemplateName += TEXT("_GEN_VARIABLE");
            const FName TemplateName = MakeUniqueObjectName(TemplateOuter, ComponentClass, *RequestedTemplateName);
            ComponentTemplate = NewObject<UActorComponent>(
                TemplateOuter,
                ComponentClass,
                TemplateName,
                RF_ArchetypeObject | RF_Public | RF_Transactional);
            Blueprint->ComponentTemplates.Add(ComponentTemplate);
        }

        if (UEdGraphPin* TemplateNamePin = Node->GetTemplateNamePinChecked())
        {
            TemplateNamePin->DefaultValue = ComponentTemplate ? ComponentTemplate->GetName() : TEXT("");
        }
        Node->ReconstructNode();

        OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
        return true;
    }

    static bool AddCompositeNode(
        const FString& BlueprintAssetPath,
        const FString& GraphName,
        const TSharedPtr<FJsonObject>& Payload,
        int32 NodePosX,
        int32 NodePosY,
        FString& OutNodeGuid,
        FString& OutError)
    {
        OutNodeGuid.Empty();
        OutError.Empty();

        UBlueprint* Blueprint = LoadBlueprintByAssetPath(BlueprintAssetPath);
        UEdGraph* TargetGraph = ResolveTargetGraph(Blueprint, GraphName);
        if (!Blueprint || !TargetGraph)
        {
            OutError = TEXT("Failed to resolve blueprint/target graph.");
            return false;
        }

        FGraphNodeCreator<UK2Node_Composite> Creator(*TargetGraph);
        UK2Node_Composite* Node = Creator.CreateNode();
        if (Node == nullptr)
        {
            OutError = TEXT("Failed to create Composite node.");
            return false;
        }

        Node->NodePosX = NodePosX;
        Node->NodePosY = NodePosY;
        if (!Node->IsCompatibleWithGraph(TargetGraph))
        {
            OutError = TEXT("Composite node is not compatible with the target graph context.");
            return false;
        }

        Node->AllocateDefaultPins();
        Creator.Finalize();

        FString RequestedGraphName;
        if (Payload.IsValid())
        {
            Payload->TryGetStringField(TEXT("collapsedGraphName"), RequestedGraphName);
            if (RequestedGraphName.IsEmpty())
            {
                Payload->TryGetStringField(TEXT("nodeTitle"), RequestedGraphName);
            }
        }
        if (!RequestedGraphName.IsEmpty())
        {
            Node->OnRenameNode(RequestedGraphName);
        }

        OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
        return true;
    }

    static bool AddContextSensitiveAddComponentByClassNode(
        const FString& BlueprintAssetPath,
        const FString& GraphName,
        const TSharedPtr<FJsonObject>& Payload,
        int32 NodePosX,
        int32 NodePosY,
        FString& OutNodeGuid,
        FString& OutError)
    {
        OutNodeGuid.Empty();
        OutError.Empty();

        UBlueprint* Blueprint = LoadBlueprintByAssetPath(BlueprintAssetPath);
        UEdGraph* TargetGraph = ResolveTargetGraph(Blueprint, GraphName);
        if (!Blueprint || !TargetGraph)
        {
            OutError = TEXT("Failed to resolve blueprint/target graph.");
            return false;
        }

        FGraphNodeCreator<UK2Node_AddComponentByClass> Creator(*TargetGraph);
        UK2Node_AddComponentByClass* Node = Creator.CreateNode();
        if (Node == nullptr)
        {
            OutError = TEXT("Failed to create AddComponentByClass node.");
            return false;
        }

        Node->NodePosX = NodePosX;
        Node->NodePosY = NodePosY;
        if (!Node->IsCompatibleWithGraph(TargetGraph))
        {
            OutError = TEXT("AddComponentByClass is not compatible with the target graph context.");
            return false;
        }

        Node->AllocateDefaultPins();
        Creator.Finalize();

        FString ComponentClassPath;
        if (Payload.IsValid())
        {
            Payload->TryGetStringField(TEXT("componentClassPath"), ComponentClassPath);
            if (ComponentClassPath.IsEmpty())
            {
                Payload->TryGetStringField(TEXT("selectedClassPath"), ComponentClassPath);
            }
        }

        if (!ComponentClassPath.IsEmpty())
        {
            UClass* ComponentClass = ResolveClass(ComponentClassPath);
            if (ComponentClass == nullptr || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
            {
                OutError = FString::Printf(TEXT("Failed to resolve component class: %s"), *ComponentClassPath);
                return false;
            }

            if (UEdGraphPin* ClassPin = Node->GetClassPin())
            {
                ClassPin->DefaultObject = ComponentClass;
                ClassPin->DefaultValue = ComponentClass->GetPathName();
                Node->PinDefaultValueChanged(ClassPin);
            }
        }

        OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
        return true;
    }

    static bool AddFunctionResultNode(
        const FString& BlueprintAssetPath,
        const FString& GraphName,
        int32 NodePosX,
        int32 NodePosY,
        FString& OutNodeGuid,
        FString& OutError)
    {
        OutNodeGuid.Empty();
        OutError.Empty();

        UBlueprint* Blueprint = LoadBlueprintByAssetPath(BlueprintAssetPath);
        UEdGraph* TargetGraph = ResolveTargetGraph(Blueprint, GraphName);
        if (!Blueprint || !TargetGraph)
        {
            OutError = TEXT("Failed to resolve blueprint/target graph.");
            return false;
        }

        FGraphNodeCreator<UK2Node_FunctionResult> Creator(*TargetGraph);
        UK2Node_FunctionResult* Node = Creator.CreateNode();
        if (Node == nullptr)
        {
            OutError = TEXT("Failed to create FunctionResult node.");
            return false;
        }

        Node->NodePosX = NodePosX;
        Node->NodePosY = NodePosY;
        if (!Node->IsCompatibleWithGraph(TargetGraph))
        {
            OutError = TEXT("FunctionResult is not compatible with the target graph context.");
            return false;
        }

        Node->AllocateDefaultPins();
        Creator.Finalize();
        Node->PostPlacedNewNode();

        OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
        return true;
    }
}

bool FLoomleBlueprintAdapter::CreateBlueprint(const FString& AssetPath, const FString& ParentClassPath, FString& OutBlueprintObjectPath, FString& OutError)
{
    OutBlueprintObjectPath.Empty();
    OutError.Empty();

    if (!FPackageName::IsValidLongPackageName(AssetPath))
    {
        OutError = TEXT("Invalid AssetPath; expected /Game/... long package name.");
        return false;
    }

    if (UBlueprint* Existing = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(AssetPath))
    {
        const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
        OutBlueprintObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
        Existing->MarkPackageDirty();
        return true;
    }

    UClass* ParentClass = LoomleBlueprintAdapterInternal::ResolveClass(ParentClassPath);
    if (!ParentClass || !ParentClass->IsChildOf(AActor::StaticClass()))
    {
        OutError = FString::Printf(TEXT("Failed to resolve actor parent class: %s"), *ParentClassPath);
        return false;
    }

    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    UPackage* Package = CreatePackage(*AssetPath);
    if (!Package)
    {
        OutError = FString::Printf(TEXT("Failed to create package: %s"), *AssetPath);
        return false;
    }

    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        ParentClass,
        Package,
        *AssetName,
        BPTYPE_Normal,
        UBlueprint::StaticClass(),
        UBlueprintGeneratedClass::StaticClass(),
        NAME_None);

    if (!Blueprint)
    {
        OutError = TEXT("FKismetEditorUtilities::CreateBlueprint failed.");
        return false;
    }

    FAssetRegistryModule::AssetCreated(Blueprint);
    Blueprint->MarkPackageDirty();
    OutBlueprintObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
    return true;
}

bool FLoomleBlueprintAdapter::DescribeUserDefinedEnum(const FString& EnumAssetPath, FString& OutEnumJson, FString& OutError)
{
    OutEnumJson.Empty();
    OutError.Empty();

    if (!FPackageName::IsValidLongPackageName(EnumAssetPath))
    {
        OutError = TEXT("Invalid enum assetPath; expected /Game/... long package name.");
        return false;
    }

    UUserDefinedEnum* Enum = LoomleBlueprintAdapterInternal::LoadUserDefinedEnumByAssetPath(EnumAssetPath);
    if (Enum == nullptr)
    {
        OutError = TEXT("Blueprint enum asset not found.");
        return false;
    }

    OutEnumJson = LoomleBlueprintAdapterInternal::JsonObjectToCondensedString(
        LoomleBlueprintAdapterInternal::SerializeUserDefinedEnum(Enum));
    return true;
}

bool FLoomleBlueprintAdapter::CreateUserDefinedEnum(const FString& EnumAssetPath, const FString& PayloadJson, FString& OutEnumJson, FString& OutError)
{
    OutEnumJson.Empty();
    OutError.Empty();

    if (!FPackageName::IsValidLongPackageName(EnumAssetPath))
    {
        OutError = TEXT("Invalid enum assetPath; expected /Game/... long package name.");
        return false;
    }

    if (LoomleBlueprintAdapterInternal::LoadUserDefinedEnumByAssetPath(EnumAssetPath) != nullptr)
    {
        OutError = TEXT("Blueprint enum asset already exists.");
        return false;
    }

    TSharedPtr<FJsonObject> Payload;
    if (!LoomleBlueprintAdapterInternal::ParsePayloadJson(PayloadJson, Payload))
    {
        OutError = TEXT("Failed to parse Blueprint enum payload JSON.");
        return false;
    }

    TArray<FString> EntryNames;
    TMap<FString, FString> DisplayNames;
    if (!LoomleBlueprintAdapterInternal::ParseEnumEntrySpecs(Payload, EntryNames, DisplayNames, OutError))
    {
        return false;
    }

    const FString AssetName = FPackageName::GetLongPackageAssetName(EnumAssetPath);
    UPackage* Package = CreatePackage(*EnumAssetPath);
    if (Package == nullptr)
    {
        OutError = FString::Printf(TEXT("Failed to create package: %s"), *EnumAssetPath);
        return false;
    }

    UUserDefinedEnum* Enum = Cast<UUserDefinedEnum>(FEnumEditorUtils::CreateUserDefinedEnum(
        Package,
        FName(*AssetName),
        RF_Public | RF_Standalone | RF_Transactional));
    if (Enum == nullptr)
    {
        OutError = TEXT("Failed to create Blueprint user-defined enum.");
        return false;
    }

    if (!LoomleBlueprintAdapterInternal::ApplyUserDefinedEnumEntries(Enum, EntryNames, DisplayNames, OutError))
    {
        return false;
    }

    FAssetRegistryModule::AssetCreated(Enum);
    Enum->MarkPackageDirty();
    OutEnumJson = LoomleBlueprintAdapterInternal::JsonObjectToCondensedString(
        LoomleBlueprintAdapterInternal::SerializeUserDefinedEnum(Enum));
    return true;
}

bool FLoomleBlueprintAdapter::UpdateUserDefinedEnumEntries(const FString& EnumAssetPath, const FString& PayloadJson, FString& OutEnumJson, FString& OutError)
{
    OutEnumJson.Empty();
    OutError.Empty();

    if (!FPackageName::IsValidLongPackageName(EnumAssetPath))
    {
        OutError = TEXT("Invalid enum assetPath; expected /Game/... long package name.");
        return false;
    }

    UUserDefinedEnum* Enum = LoomleBlueprintAdapterInternal::LoadUserDefinedEnumByAssetPath(EnumAssetPath);
    if (Enum == nullptr)
    {
        OutError = TEXT("Blueprint enum asset not found.");
        return false;
    }

    TSharedPtr<FJsonObject> Payload;
    if (!LoomleBlueprintAdapterInternal::ParsePayloadJson(PayloadJson, Payload))
    {
        OutError = TEXT("Failed to parse Blueprint enum payload JSON.");
        return false;
    }

    TArray<FString> EntryNames;
    TMap<FString, FString> DisplayNames;
    if (!LoomleBlueprintAdapterInternal::ParseEnumEntrySpecs(Payload, EntryNames, DisplayNames, OutError))
    {
        return false;
    }

    if (!LoomleBlueprintAdapterInternal::ApplyUserDefinedEnumEntries(Enum, EntryNames, DisplayNames, OutError))
    {
        return false;
    }

    OutEnumJson = LoomleBlueprintAdapterInternal::JsonObjectToCondensedString(
        LoomleBlueprintAdapterInternal::SerializeUserDefinedEnum(Enum));
    return true;
}

bool FLoomleBlueprintAdapter::SetParentClass(const FString& BlueprintAssetPath, const FString& ParentClassPath, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (Blueprint == nullptr)
    {
        OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintAssetPath);
        return false;
    }

    UClass* ParentClass = LoomleBlueprintAdapterInternal::ResolveClass(ParentClassPath);
    if (ParentClass == nullptr || !FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
    {
        OutError = FString::Printf(TEXT("Failed to resolve Blueprint parent class: %s"), *ParentClassPath);
        return false;
    }

    if (Blueprint->ParentClass == ParentClass)
    {
        return true;
    }

    Blueprint->Modify();
    if (Blueprint->SimpleConstructionScript != nullptr)
    {
        Blueprint->SimpleConstructionScript->Modify();
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node != nullptr)
            {
                Node->Modify();
            }
        }
    }

    Blueprint->ParentClass = ParentClass;
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::ListImplementedInterfaces(const FString& BlueprintAssetPath, FString& OutInterfacesJson, FString& OutError)
{
    OutInterfacesJson = TEXT("[]");
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (Blueprint == nullptr)
    {
        OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintAssetPath);
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> Interfaces;
    for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
    {
        UClass* InterfaceClass = InterfaceDesc.Interface;
        if (InterfaceClass == nullptr)
        {
            continue;
        }

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), InterfaceClass->GetName());
        Entry->SetStringField(TEXT("classPath"), InterfaceClass->GetPathName());
        Entry->SetStringField(TEXT("classPathName"), InterfaceClass->GetClassPathName().ToString());
        Entry->SetStringField(TEXT("displayName"), InterfaceClass->GetDisplayNameText().ToString());

        TArray<TSharedPtr<FJsonValue>> Graphs;
        for (UEdGraph* Graph : InterfaceDesc.Graphs)
        {
            if (Graph == nullptr)
            {
                continue;
            }

            TSharedPtr<FJsonObject> GraphEntry = MakeShared<FJsonObject>();
            GraphEntry->SetStringField(TEXT("name"), Graph->GetName());
            GraphEntry->SetStringField(TEXT("graphName"), Graph->GetName());
            GraphEntry->SetStringField(TEXT("graphClassPath"), Graph->GetClass() ? Graph->GetClass()->GetPathName() : TEXT(""));
            Graphs.Add(MakeShared<FJsonValueObject>(GraphEntry));
        }
        Entry->SetArrayField(TEXT("graphs"), Graphs);
        Interfaces.Add(MakeShared<FJsonValueObject>(Entry));
    }

    OutInterfacesJson = LoomleBlueprintAdapterInternal::JsonArrayToString(Interfaces);
    return true;
}

bool FLoomleBlueprintAdapter::AddInterface(const FString& BlueprintAssetPath, const FString& InterfaceClassPath, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (Blueprint == nullptr)
    {
        OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintAssetPath);
        return false;
    }

    UClass* InterfaceClass = LoomleBlueprintAdapterInternal::ResolveClass(InterfaceClassPath);
    if (InterfaceClass == nullptr || !InterfaceClass->IsChildOf(UInterface::StaticClass()))
    {
        OutError = FString::Printf(TEXT("Failed to resolve Blueprint interface class: %s"), *InterfaceClassPath);
        return false;
    }

    for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
    {
        if (InterfaceDesc.Interface == InterfaceClass)
        {
            return true;
        }
    }

    if (!FKismetEditorUtilities::CanBlueprintImplementInterface(Blueprint, InterfaceClass))
    {
        OutError = FString::Printf(TEXT("Blueprint cannot implement interface: %s"), *InterfaceClassPath);
        return false;
    }

    if (!FBlueprintEditorUtils::ImplementNewInterface(Blueprint, InterfaceClass->GetClassPathName()))
    {
        OutError = FString::Printf(TEXT("Failed to add Blueprint interface: %s"), *InterfaceClassPath);
        return false;
    }

    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::RemoveInterface(const FString& BlueprintAssetPath, const FString& InterfaceClassPath, bool bPreserveFunctions, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (Blueprint == nullptr)
    {
        OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintAssetPath);
        return false;
    }

    UClass* InterfaceClass = LoomleBlueprintAdapterInternal::ResolveClass(InterfaceClassPath);
    if (InterfaceClass == nullptr || !InterfaceClass->IsChildOf(UInterface::StaticClass()))
    {
        OutError = FString::Printf(TEXT("Failed to resolve Blueprint interface class: %s"), *InterfaceClassPath);
        return false;
    }

    bool bFound = false;
    for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
    {
        if (InterfaceDesc.Interface == InterfaceClass)
        {
            bFound = true;
            break;
        }
    }
    if (!bFound)
    {
        return true;
    }

    FBlueprintEditorUtils::RemoveInterface(Blueprint, InterfaceClass->GetClassPathName(), bPreserveFunctions);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::AddComponent(const FString& BlueprintAssetPath, const FString& ComponentClassPath, const FString& ComponentName, const FString& ParentComponentName, FString& OutError)
{
    OutError.Empty();
    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (!Blueprint || !Blueprint->SimpleConstructionScript)
    {
        OutError = FString::Printf(TEXT("Blueprint not found or no SCS: %s"), *BlueprintAssetPath);
        return false;
    }

    UClass* ComponentClass = LoomleBlueprintAdapterInternal::ResolveClass(ComponentClassPath);
    if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
    {
        OutError = FString::Printf(TEXT("Failed to resolve component class: %s"), *ComponentClassPath);
        return false;
    }

    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    USCS_Node* NewNode = SCS->CreateNode(ComponentClass, *ComponentName);
    if (!NewNode)
    {
        OutError = TEXT("Failed to create SCS node.");
        return false;
    }

    USCS_Node* ParentNode = nullptr;
    if (!ParentComponentName.IsEmpty())
    {
        for (USCS_Node* Node : SCS->GetAllNodes())
        {
            if (Node && Node->GetVariableName().ToString().Equals(ParentComponentName))
            {
                ParentNode = Node;
                break;
            }
        }
    }

    if (ParentNode)
    {
        ParentNode->AddChildNode(NewNode);
    }
    else
    {
        SCS->AddNode(NewNode);
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::SetStaticMeshComponentAsset(const FString& BlueprintAssetPath, const FString& ComponentName, const FString& MeshAssetPath, FString& OutError)
{
    OutError.Empty();
    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    USCS_Node* Node = LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, ComponentName);
    UStaticMeshComponent* MeshComp = Node ? Cast<UStaticMeshComponent>(Node->ComponentTemplate) : nullptr;
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshAssetPath);
    if (!Blueprint || !MeshComp || !Mesh)
    {
        OutError = TEXT("Failed to resolve blueprint/static mesh component/mesh asset.");
        return false;
    }

    MeshComp->SetStaticMesh(Mesh);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::SetSceneComponentRelativeLocation(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Location, FString& OutError)
{
    OutError.Empty();
    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    USCS_Node* Node = LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, ComponentName);
    USceneComponent* Comp = Node ? Cast<USceneComponent>(Node->ComponentTemplate) : nullptr;
    if (!Blueprint || !Comp)
    {
        OutError = TEXT("Failed to resolve blueprint/scene component.");
        return false;
    }

    Comp->SetRelativeLocation(Location);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::SetSceneComponentRelativeScale3D(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Scale3D, FString& OutError)
{
    OutError.Empty();
    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    USCS_Node* Node = LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, ComponentName);
    USceneComponent* Comp = Node ? Cast<USceneComponent>(Node->ComponentTemplate) : nullptr;
    if (!Blueprint || !Comp)
    {
        OutError = TEXT("Failed to resolve blueprint/scene component.");
        return false;
    }

    Comp->SetRelativeScale3D(Scale3D);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::SetPrimitiveComponentCollisionEnabled(const FString& BlueprintAssetPath, const FString& ComponentName, const FString& CollisionMode, FString& OutError)
{
    OutError.Empty();
    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    USCS_Node* Node = LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, ComponentName);
    UPrimitiveComponent* Comp = Node ? Cast<UPrimitiveComponent>(Node->ComponentTemplate) : nullptr;
    if (!Blueprint || !Comp)
    {
        OutError = TEXT("Failed to resolve blueprint/primitive component.");
        return false;
    }

    const FString Key = CollisionMode.ToLower();
    if (Key.Equals(TEXT("nocollision")))
    {
        Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    else if (Key.Equals(TEXT("queryonly")))
    {
        Comp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    }
    else if (Key.Equals(TEXT("queryandphysics")))
    {
        Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }
    else
    {
        OutError = FString::Printf(TEXT("Unknown collision mode: %s"), *CollisionMode);
        return false;
    }

    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::SetBoxComponentExtent(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Extent, FString& OutError)
{
    OutError.Empty();
    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    USCS_Node* Node = LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, ComponentName);
    UBoxComponent* Comp = Node ? Cast<UBoxComponent>(Node->ComponentTemplate) : nullptr;
    if (!Blueprint || !Comp)
    {
        OutError = TEXT("Failed to resolve blueprint/box component.");
        return false;
    }

    Comp->SetBoxExtent(Extent);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::SetPrimitiveComponentGenerateOverlapEvents(const FString& BlueprintAssetPath, const FString& ComponentName, bool bGenerate, FString& OutError)
{
    OutError.Empty();
    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    USCS_Node* Node = LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, ComponentName);
    UPrimitiveComponent* Comp = Node ? Cast<UPrimitiveComponent>(Node->ComponentTemplate) : nullptr;
    if (!Blueprint || !Comp)
    {
        OutError = TEXT("Failed to resolve blueprint/primitive component.");
        return false;
    }

    Comp->SetGenerateOverlapEvents(bGenerate);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::ValidateMemberEditOperation(const FString& MemberKind, const FString& Operation, FString& OutError)
{
    OutError.Empty();

    if (Operation.IsEmpty())
    {
        OutError = TEXT("blueprint.member.edit requires operation.");
        return false;
    }

    const FString MemberKindLower = MemberKind.ToLower();
    const FString OperationLower = Operation.ToLower();

    if (MemberKindLower.Equals(TEXT("component"), ESearchCase::CaseSensitive))
    {
        if (LoomleBlueprintAdapterInternal::IsMemberEditOperationSupported(
            OperationLower,
            {
                TEXT("create"),
                TEXT("rename"),
                TEXT("delete"),
                TEXT("reparent"),
                TEXT("reorder"),
                TEXT("update"),
                TEXT("setstaticmeshasset"),
                TEXT("setrelativelocation"),
                TEXT("setrelativescale3d"),
                TEXT("setcollisionenabled"),
                TEXT("setboxextent"),
                TEXT("setgenerateoverlapevents"),
            }))
        {
            return true;
        }
        return LoomleBlueprintAdapterInternal::RejectUnsupportedMemberEditOperation(TEXT("component"), Operation, OperationLower, OutError);
    }

    if (MemberKindLower.Equals(TEXT("variable"), ESearchCase::CaseSensitive))
    {
        if (LoomleBlueprintAdapterInternal::IsMemberEditOperationSupported(
            OperationLower,
            {
                TEXT("create"),
                TEXT("rename"),
                TEXT("delete"),
                TEXT("reorder"),
                TEXT("setdefault"),
                TEXT("update"),
            }))
        {
            return true;
        }
        return LoomleBlueprintAdapterInternal::RejectUnsupportedMemberEditOperation(TEXT("variable"), Operation, OperationLower, OutError);
    }

    if (MemberKindLower.Equals(TEXT("function"), ESearchCase::CaseSensitive))
    {
        if (LoomleBlueprintAdapterInternal::IsMemberEditOperationSupported(
            OperationLower,
            {
                TEXT("create"),
                TEXT("rename"),
                TEXT("delete"),
                TEXT("updatesignature"),
                TEXT("setflags"),
            }))
        {
            return true;
        }
        return LoomleBlueprintAdapterInternal::RejectUnsupportedMemberEditOperation(TEXT("function"), Operation, OperationLower, OutError);
    }

    if (MemberKindLower.Equals(TEXT("macro"), ESearchCase::CaseSensitive))
    {
        if (LoomleBlueprintAdapterInternal::IsMemberEditOperationSupported(
            OperationLower,
            {
                TEXT("create"),
                TEXT("rename"),
                TEXT("delete"),
                TEXT("updatesignature"),
            }))
        {
            return true;
        }
        return LoomleBlueprintAdapterInternal::RejectUnsupportedMemberEditOperation(TEXT("macro"), Operation, OperationLower, OutError);
    }

    if (MemberKindLower.Equals(TEXT("dispatcher"), ESearchCase::CaseSensitive))
    {
        if (LoomleBlueprintAdapterInternal::IsMemberEditOperationSupported(
            OperationLower,
            {
                TEXT("create"),
                TEXT("rename"),
                TEXT("delete"),
                TEXT("updatesignature"),
            }))
        {
            return true;
        }
        return LoomleBlueprintAdapterInternal::RejectUnsupportedMemberEditOperation(TEXT("dispatcher"), Operation, OperationLower, OutError);
    }

    if (MemberKindLower.Equals(TEXT("event"), ESearchCase::CaseSensitive)
        || MemberKindLower.Equals(TEXT("customevent"), ESearchCase::CaseSensitive))
    {
        if (LoomleBlueprintAdapterInternal::IsMemberEditOperationSupported(
            OperationLower,
            {
                TEXT("create"),
                TEXT("rename"),
                TEXT("delete"),
                TEXT("updatesignature"),
                TEXT("addinput"),
                TEXT("setflags"),
            }))
        {
            return true;
        }
        return LoomleBlueprintAdapterInternal::RejectUnsupportedMemberEditOperation(TEXT("event"), Operation, OperationLower, OutError);
    }

    OutError = FString::Printf(TEXT("Unsupported Blueprint member kind: %s"), *MemberKind);
    return false;
}

bool FLoomleBlueprintAdapter::ValidateMemberEditRequest(
    const FString& BlueprintAssetPath,
    const FString& MemberKind,
    const FString& Operation,
    const FString& PayloadJson,
    FString& OutError)
{
    OutError.Empty();

    if (!ValidateMemberEditOperation(MemberKind, Operation, OutError))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Payload;
    if (!LoomleBlueprintAdapterInternal::ParsePayloadJson(PayloadJson, Payload))
    {
        OutError = FString::Printf(TEXT("Failed to parse %s payload JSON."), *MemberKind);
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (Blueprint == nullptr)
    {
        OutError = TEXT("Failed to resolve blueprint.");
        return false;
    }

    const FString MemberKindLower = MemberKind.ToLower();
    const FString OperationLower = Operation.ToLower();

    if (MemberKindLower.Equals(TEXT("component"), ESearchCase::CaseSensitive))
    {
        USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
        if (SCS == nullptr)
        {
            OutError = TEXT("Blueprint not found or SimpleConstructionScript is unavailable.");
            return false;
        }

        if (OperationLower.Equals(TEXT("create")))
        {
            FString ComponentClassPath;
            FString ComponentName;
            FString ParentComponentName;
            LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("componentClassPath"), TEXT("classPath")}, ComponentClassPath);
            LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("componentName"), TEXT("name")}, ComponentName);
            LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("parentComponentName"), TEXT("parentName")}, ParentComponentName);
            if (ComponentName.IsEmpty())
            {
                OutError = TEXT("component create requires componentName.");
                return false;
            }
            UClass* ComponentClass = LoomleBlueprintAdapterInternal::ResolveClass(ComponentClassPath);
            if (ComponentClass == nullptr || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
            {
                OutError = FString::Printf(TEXT("Failed to resolve component class: %s"), *ComponentClassPath);
                return false;
            }
            if (!ParentComponentName.IsEmpty() && LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, ParentComponentName) == nullptr)
            {
                OutError = FString::Printf(TEXT("Failed to resolve parent component: %s"), *ParentComponentName);
                return false;
            }
            return true;
        }

        FString ComponentName;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("componentName"), TEXT("name")}, ComponentName);
        USCS_Node* Node = LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, ComponentName);
        if (Node == nullptr)
        {
            OutError = FString::Printf(TEXT("Failed to resolve component node: %s"), *ComponentName);
            return false;
        }
        if (OperationLower.Equals(TEXT("rename")))
        {
            FString NewName;
            LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("newName"), TEXT("name")}, NewName);
            if (NewName.IsEmpty())
            {
                OutError = TEXT("component rename requires newName.");
                return false;
            }
        }
        else if (OperationLower.Equals(TEXT("reorder")))
        {
            FString TargetComponentName;
            LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("targetComponentName"), TEXT("targetName")}, TargetComponentName);
            if (TargetComponentName.IsEmpty())
            {
                OutError = TEXT("component reorder requires targetComponentName.");
                return false;
            }
            if (LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, TargetComponentName) == nullptr)
            {
                OutError = FString::Printf(TEXT("Failed to resolve reorder target component: %s"), *TargetComponentName);
                return false;
            }
        }
        return true;
    }

    if (MemberKindLower.Equals(TEXT("variable"), ESearchCase::CaseSensitive))
    {
        FString VariableNameText;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("variableName"), TEXT("name")}, VariableNameText);
        if (OperationLower.Equals(TEXT("create")))
        {
            if (VariableNameText.IsEmpty())
            {
                OutError = TEXT("variable create requires variableName.");
                return false;
            }
            FEdGraphPinType PinType;
            return LoomleBlueprintAdapterInternal::ParsePinTypeFromPayload(Payload, PinType, OutError);
        }

        FBPVariableDescription* VariableDescription = LoomleBlueprintAdapterInternal::FindVariableDescription(Blueprint, FName(*VariableNameText));
        if (VariableDescription == nullptr)
        {
            OutError = FString::Printf(TEXT("Failed to resolve Blueprint member variable: %s"), *VariableNameText);
            return false;
        }
        if (OperationLower.Equals(TEXT("rename")))
        {
            FString NewNameText;
            LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("newName"), TEXT("name")}, NewNameText);
            if (NewNameText.IsEmpty())
            {
                OutError = TEXT("variable rename requires newName.");
                return false;
            }
        }
        else if (OperationLower.Equals(TEXT("reorder")))
        {
            FString TargetVariableName;
            LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("targetVariableName"), TEXT("targetName")}, TargetVariableName);
            if (TargetVariableName.IsEmpty())
            {
                OutError = TEXT("variable reorder requires targetVariableName.");
                return false;
            }
        }
        else if (OperationLower.Equals(TEXT("update")) && (Payload->HasField(TEXT("type")) || Payload->HasField(TEXT("pinType")) || Payload->HasField(TEXT("varType"))))
        {
            FEdGraphPinType PinType;
            if (!LoomleBlueprintAdapterInternal::ParsePinTypeFromPayload(Payload, PinType, OutError))
            {
                return false;
            }
        }
        return true;
    }

    if (MemberKindLower.Equals(TEXT("function"), ESearchCase::CaseSensitive)
        || MemberKindLower.Equals(TEXT("macro"), ESearchCase::CaseSensitive)
        || MemberKindLower.Equals(TEXT("dispatcher"), ESearchCase::CaseSensitive))
    {
        const TCHAR* NameFieldForMessage = MemberKindLower.Equals(TEXT("function"), ESearchCase::CaseSensitive)
            ? TEXT("functionName")
            : MemberKindLower.Equals(TEXT("macro"), ESearchCase::CaseSensitive)
            ? TEXT("macroName")
            : TEXT("dispatcherName");
        FString GraphName;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {NameFieldForMessage, TEXT("name")}, GraphName);
        if (OperationLower.Equals(TEXT("create")))
        {
            if (GraphName.IsEmpty())
            {
                OutError = FString::Printf(TEXT("%s create requires %s."), *MemberKindLower, NameFieldForMessage);
                return false;
            }
            if (MemberKindLower.Equals(TEXT("dispatcher"), ESearchCase::CaseSensitive))
            {
                return true;
            }
        }
        else if (LoomleBlueprintAdapterInternal::FindFunctionLikeGraph(Blueprint, GraphName) == nullptr)
        {
            OutError = FString::Printf(TEXT("Failed to resolve %s graph: %s"), *MemberKindLower, *GraphName);
            return false;
        }

        if (OperationLower.Equals(TEXT("rename")))
        {
            FString NewName;
            LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("newName"), TEXT("name")}, NewName);
            if (NewName.IsEmpty())
            {
                OutError = FString::Printf(TEXT("%s rename requires newName."), *MemberKindLower);
                return false;
            }
        }
        return true;
    }

    if (MemberKindLower.Equals(TEXT("event"), ESearchCase::CaseSensitive)
        || MemberKindLower.Equals(TEXT("customevent"), ESearchCase::CaseSensitive))
    {
        FString EventName;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("eventName"), TEXT("customEventName"), TEXT("name")}, EventName);
        if (EventName.IsEmpty())
        {
            OutError = OperationLower.Equals(TEXT("create")) ? TEXT("event create requires name.") : TEXT("event operation requires name.");
            return false;
        }
        if (OperationLower.Equals(TEXT("create")))
        {
            FString GraphName;
            LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("graphName")}, GraphName);
            if (!GraphName.IsEmpty() && LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName) == nullptr)
            {
                OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
                return false;
            }
            return true;
        }

        UK2Node_CustomEvent* CustomEvent = LoomleBlueprintAdapterInternal::FindCustomEventNode(Blueprint, EventName);
        if (CustomEvent == nullptr)
        {
            OutError = FString::Printf(TEXT("Failed to resolve custom event: %s"), *EventName);
            return false;
        }
        if (OperationLower.Equals(TEXT("rename")))
        {
            FString NewName;
            LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("newName")}, NewName);
            if (NewName.IsEmpty())
            {
                OutError = TEXT("event rename requires newName.");
                return false;
            }
        }
        return true;
    }

    OutError = FString::Printf(TEXT("Unsupported Blueprint member kind: %s"), *MemberKind);
    return false;
}

bool FLoomleBlueprintAdapter::EditComponentMember(const FString& BlueprintAssetPath, const FString& Operation, const FString& PayloadJson, FString& OutError)
{
    OutError.Empty();

    if (!ValidateMemberEditOperation(TEXT("component"), Operation, OutError))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Payload;
    if (!LoomleBlueprintAdapterInternal::ParsePayloadJson(PayloadJson, Payload))
    {
        OutError = TEXT("Failed to parse component payload JSON.");
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    USimpleConstructionScript* SCS = Blueprint ? Blueprint->SimpleConstructionScript : nullptr;
    if (Blueprint == nullptr || SCS == nullptr)
    {
        OutError = TEXT("Blueprint not found or SimpleConstructionScript is unavailable.");
        return false;
    }

    const FString OperationLower = Operation.ToLower();
    if (OperationLower.Equals(TEXT("create")))
    {
        FString ComponentClassPath;
        FString ComponentName;
        FString ParentComponentName;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("componentClassPath"), TEXT("classPath")}, ComponentClassPath);
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("componentName"), TEXT("name")}, ComponentName);
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("parentComponentName"), TEXT("parentName")}, ParentComponentName);
        return AddComponent(BlueprintAssetPath, ComponentClassPath, ComponentName, ParentComponentName, OutError);
    }

    FString ComponentName;
    LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("componentName"), TEXT("name")}, ComponentName);
    USCS_Node* Node = LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, ComponentName);
    if (Node == nullptr)
    {
        OutError = FString::Printf(TEXT("Failed to resolve component node: %s"), *ComponentName);
        return false;
    }

    if (OperationLower.Equals(TEXT("rename")))
    {
        FString NewName;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("newName"), TEXT("name")}, NewName);
        if (NewName.IsEmpty())
        {
            OutError = TEXT("component rename requires newName.");
            return false;
        }
        FBlueprintEditorUtils::RenameComponentMemberVariable(Blueprint, Node, FName(*NewName));
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
        return true;
    }

    if (OperationLower.Equals(TEXT("delete")))
    {
        const bool bPromoteChildren = Payload.IsValid() && Payload->HasTypedField<EJson::Boolean>(TEXT("promoteChildren"))
            ? Payload->GetBoolField(TEXT("promoteChildren"))
            : false;
        if (bPromoteChildren)
        {
            SCS->RemoveNodeAndPromoteChildren(Node);
        }
        else
        {
            SCS->RemoveNode(Node);
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
        return true;
    }

    if (OperationLower.Equals(TEXT("reparent")))
    {
        FString ParentComponentName;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("parentComponentName"), TEXT("parentName")}, ParentComponentName);
        if (ParentComponentName.IsEmpty())
        {
            if (USCS_Node* ExistingParent = SCS->FindParentNode(Node))
            {
                ExistingParent->RemoveChildNode(Node, false);
            }
            else
            {
                SCS->RemoveNode(Node, false);
            }
            LoomleBlueprintAdapterInternal::ClearSCSNodeParent(Node);
            SCS->AddNode(Node);
        }
        else
        {
            USCS_Node* ParentNode = LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, ParentComponentName);
            if (ParentNode == nullptr)
            {
                OutError = FString::Printf(TEXT("Failed to resolve parent component: %s"), *ParentComponentName);
                return false;
            }

            if (LoomleBlueprintAdapterInternal::IsSameOrDescendantNode(ParentNode, Node))
            {
                OutError = TEXT("Cannot reparent a component to itself or one of its descendants.");
                return false;
            }

            const bool bWasRootNode = SCS->GetRootNodes().Contains(Node);
            if (USCS_Node* ExistingParent = SCS->FindParentNode(Node))
            {
                ExistingParent->RemoveChildNode(Node, false);
            }
            else
            {
                SCS->RemoveNode(Node, false);
            }
            Node->SetParent(ParentNode);
            ParentNode->AddChildNode(Node, bWasRootNode);
        }

        SCS->ValidateSceneRootNodes();
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
        return true;
    }

    if (OperationLower.Equals(TEXT("reorder")))
    {
        FString TargetComponentName;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("targetComponentName"), TEXT("targetName")}, TargetComponentName);
        if (TargetComponentName.IsEmpty())
        {
            OutError = TEXT("component reorder requires targetComponentName.");
            return false;
        }

        USCS_Node* TargetNode = LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, TargetComponentName);
        if (TargetNode == nullptr)
        {
            OutError = FString::Printf(TEXT("Failed to resolve reorder target component: %s"), *TargetComponentName);
            return false;
        }

        FString Placement = TEXT("after");
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("placement"), TEXT("position")}, Placement);
        const bool bMoveBefore = Placement.Equals(TEXT("before"), ESearchCase::IgnoreCase);

        USCS_Node* ParentNode = SCS->FindParentNode(Node);
        USCS_Node* TargetParentNode = SCS->FindParentNode(TargetNode);
        if (ParentNode != TargetParentNode)
        {
            OutError = TEXT("Component reorder requires the component and target to share the same parent.");
            return false;
        }

        const bool bOk = ParentNode != nullptr
            ? LoomleBlueprintAdapterInternal::ReorderSCSChildren(ParentNode, Node, TargetNode, bMoveBefore, OutError)
            : LoomleBlueprintAdapterInternal::ReorderSCSRoots(SCS, Node, TargetNode, bMoveBefore, OutError);
        if (!bOk)
        {
            return false;
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
        return true;
    }

    if (OperationLower.Equals(TEXT("update"))
        || OperationLower.Equals(TEXT("setstaticmeshasset"))
        || OperationLower.Equals(TEXT("setrelativelocation"))
        || OperationLower.Equals(TEXT("setrelativescale3d"))
        || OperationLower.Equals(TEXT("setcollisionenabled"))
        || OperationLower.Equals(TEXT("setboxextent"))
        || OperationLower.Equals(TEXT("setgenerateoverlapevents")))
    {
        FString MeshAssetPath;
        if (OperationLower.Equals(TEXT("setstaticmeshasset"))
            || LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("meshAssetPath"), TEXT("staticMeshAssetPath")}, MeshAssetPath))
        {
            if (!MeshAssetPath.IsEmpty())
            {
                if (!SetStaticMeshComponentAsset(BlueprintAssetPath, Node->GetVariableName().ToString(), MeshAssetPath, OutError))
                {
                    return false;
                }
            }
        }

        FVector VectorValue;
        if (OperationLower.Equals(TEXT("setrelativelocation")) || LoomleBlueprintAdapterInternal::TryReadVectorFieldAny(Payload, {TEXT("relativeLocation"), TEXT("location")}, VectorValue))
        {
            if (!SetSceneComponentRelativeLocation(BlueprintAssetPath, Node->GetVariableName().ToString(), VectorValue, OutError))
            {
                return false;
            }
        }

        if (OperationLower.Equals(TEXT("setrelativescale3d")) || LoomleBlueprintAdapterInternal::TryReadVectorFieldAny(Payload, {TEXT("relativeScale3D"), TEXT("scale3D"), TEXT("scale")}, VectorValue))
        {
            if (!SetSceneComponentRelativeScale3D(BlueprintAssetPath, Node->GetVariableName().ToString(), VectorValue, OutError))
            {
                return false;
            }
        }

        FString CollisionMode;
        if (OperationLower.Equals(TEXT("setcollisionenabled")) || LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("collisionMode"), TEXT("collisionEnabled")}, CollisionMode))
        {
            if (!CollisionMode.IsEmpty()
                && !SetPrimitiveComponentCollisionEnabled(BlueprintAssetPath, Node->GetVariableName().ToString(), CollisionMode, OutError))
            {
                return false;
            }
        }

        if (OperationLower.Equals(TEXT("setboxextent")) || LoomleBlueprintAdapterInternal::TryReadVectorFieldAny(Payload, {TEXT("boxExtent"), TEXT("extent")}, VectorValue))
        {
            UBoxComponent* BoxComponent = Cast<UBoxComponent>(Node->ComponentTemplate);
            if (BoxComponent != nullptr)
            {
                if (!SetBoxComponentExtent(BlueprintAssetPath, Node->GetVariableName().ToString(), VectorValue, OutError))
                {
                    return false;
                }
            }
        }

        bool bGenerateOverlap = false;
        if (OperationLower.Equals(TEXT("setgenerateoverlapevents")) || LoomleBlueprintAdapterInternal::TryGetBoolFieldAny(Payload, {TEXT("generateOverlapEvents")}, bGenerateOverlap))
        {
            if (!SetPrimitiveComponentGenerateOverlapEvents(BlueprintAssetPath, Node->GetVariableName().ToString(), bGenerateOverlap, OutError))
            {
                return false;
            }
        }

        Blueprint->MarkPackageDirty();
        return true;
    }

    OutError = FString::Printf(TEXT("Unsupported component operation: %s"), *Operation);
    return false;
}

bool FLoomleBlueprintAdapter::EditVariableMember(const FString& BlueprintAssetPath, const FString& Operation, const FString& PayloadJson, FString& OutError)
{
    OutError.Empty();

    if (!ValidateMemberEditOperation(TEXT("variable"), Operation, OutError))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Payload;
    if (!LoomleBlueprintAdapterInternal::ParsePayloadJson(PayloadJson, Payload))
    {
        OutError = TEXT("Failed to parse variable payload JSON.");
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (Blueprint == nullptr)
    {
        OutError = TEXT("Failed to resolve blueprint.");
        return false;
    }

    const FString OperationLower = Operation.ToLower();
    if (OperationLower.Equals(TEXT("create")))
    {
        FString VariableNameText;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("variableName"), TEXT("name")}, VariableNameText);
        if (VariableNameText.IsEmpty())
        {
            OutError = TEXT("variable create requires variableName.");
            return false;
        }

        FEdGraphPinType PinType;
        if (!LoomleBlueprintAdapterInternal::ParsePinTypeFromPayload(Payload, PinType, OutError))
        {
            return false;
        }

        FString DefaultValue;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("defaultValue"), TEXT("default")}, DefaultValue);
        if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*VariableNameText), PinType, DefaultValue))
        {
            OutError = FString::Printf(TEXT("Failed to add Blueprint member variable: %s"), *VariableNameText);
            return false;
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
        return true;
    }

    FString VariableNameText;
    LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("variableName"), TEXT("name")}, VariableNameText);
    const FName VariableName(*VariableNameText);
    FBPVariableDescription* VariableDescription = LoomleBlueprintAdapterInternal::FindVariableDescription(Blueprint, VariableName);
    if (VariableDescription == nullptr)
    {
        OutError = FString::Printf(TEXT("Failed to resolve Blueprint member variable: %s"), *VariableNameText);
        return false;
    }

    if (OperationLower.Equals(TEXT("rename")))
    {
        FString NewNameText;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("newName"), TEXT("name")}, NewNameText);
        if (NewNameText.IsEmpty())
        {
            OutError = TEXT("variable rename requires newName.");
            return false;
        }
        FBlueprintEditorUtils::RenameMemberVariable(Blueprint, VariableName, FName(*NewNameText));
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
        return true;
    }

    if (OperationLower.Equals(TEXT("delete")))
    {
        FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, VariableName);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
        return true;
    }

    if (OperationLower.Equals(TEXT("reorder")))
    {
        FString TargetVariableName;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("targetVariableName"), TEXT("targetName")}, TargetVariableName);
        if (TargetVariableName.IsEmpty())
        {
            OutError = TEXT("variable reorder requires targetVariableName.");
            return false;
        }

        FString Placement = TEXT("after");
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("placement"), TEXT("position")}, Placement);
        UStruct* VariableScope = LoomleBlueprintAdapterInternal::ResolveBlueprintMemberVariableScope(Blueprint);
        if (VariableScope == nullptr)
        {
            OutError = TEXT("Failed to resolve Blueprint member variable scope.");
            return false;
        }
        const bool bOk = Placement.Equals(TEXT("before"), ESearchCase::IgnoreCase)
            ? FBlueprintEditorUtils::MoveVariableBeforeVariable(Blueprint, VariableScope, VariableName, FName(*TargetVariableName), false)
            : FBlueprintEditorUtils::MoveVariableAfterVariable(Blueprint, VariableScope, VariableName, FName(*TargetVariableName), false);
        if (!bOk)
        {
            OutError = TEXT("Variable reorder failed.");
            return false;
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
        return true;
    }

    if (OperationLower.Equals(TEXT("setdefault")) || OperationLower.Equals(TEXT("update")))
    {
        if (OperationLower.Equals(TEXT("update")) && (Payload->HasField(TEXT("type")) || Payload->HasField(TEXT("pinType")) || Payload->HasField(TEXT("varType"))))
        {
            FEdGraphPinType PinType;
            if (!LoomleBlueprintAdapterInternal::ParsePinTypeFromPayload(Payload, PinType, OutError))
            {
                return false;
            }
            FBlueprintEditorUtils::ChangeMemberVariableType(Blueprint, VariableName, PinType);
        }

        FString DefaultValue;
        if (LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("defaultValue"), TEXT("default")}, DefaultValue))
        {
            VariableDescription = LoomleBlueprintAdapterInternal::FindVariableDescription(Blueprint, VariableName);
            if (VariableDescription == nullptr)
            {
                OutError = TEXT("Variable disappeared while applying default value.");
                return false;
            }
            VariableDescription->DefaultValue = DefaultValue;
        }

        FString Category;
        if (LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("category")}, Category))
        {
            FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, VariableName, nullptr, FText::FromString(Category), false);
        }

        FString Tooltip;
        if (LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("tooltip")}, Tooltip))
        {
            FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, VariableName, nullptr, FBlueprintMetadata::MD_Tooltip, Tooltip);
        }

        bool bBoolValue = false;
        if (LoomleBlueprintAdapterInternal::TryGetBoolFieldAny(Payload, {TEXT("blueprintReadWrite"), TEXT("editable")}, bBoolValue))
        {
            FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, VariableName, bBoolValue);
        }
        if (LoomleBlueprintAdapterInternal::TryGetBoolFieldAny(Payload, {TEXT("readOnly")}, bBoolValue))
        {
            FBlueprintEditorUtils::SetBlueprintPropertyReadOnlyFlag(Blueprint, VariableName, bBoolValue);
        }
        if (LoomleBlueprintAdapterInternal::TryGetBoolFieldAny(Payload, {TEXT("transient")}, bBoolValue))
        {
            FBlueprintEditorUtils::SetVariableTransientFlag(Blueprint, VariableName, bBoolValue);
        }
        if (LoomleBlueprintAdapterInternal::TryGetBoolFieldAny(Payload, {TEXT("saveGame")}, bBoolValue))
        {
            FBlueprintEditorUtils::SetVariableSaveGameFlag(Blueprint, VariableName, bBoolValue);
        }
        if (LoomleBlueprintAdapterInternal::TryGetBoolFieldAny(Payload, {TEXT("advancedDisplay")}, bBoolValue))
        {
            FBlueprintEditorUtils::SetVariableAdvancedDisplayFlag(Blueprint, VariableName, bBoolValue);
        }
        if (LoomleBlueprintAdapterInternal::TryGetBoolFieldAny(Payload, {TEXT("deprecated")}, bBoolValue))
        {
            FBlueprintEditorUtils::SetVariableDeprecatedFlag(Blueprint, VariableName, bBoolValue);
        }
        if (LoomleBlueprintAdapterInternal::TryGetBoolFieldAny(Payload, {TEXT("exposeOnSpawn")}, bBoolValue))
        {
            if (bBoolValue)
            {
                FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, VariableName, nullptr, FBlueprintMetadata::MD_ExposeOnSpawn, TEXT("true"));
            }
            else
            {
                FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(Blueprint, VariableName, nullptr, FBlueprintMetadata::MD_ExposeOnSpawn);
            }
        }
        if (LoomleBlueprintAdapterInternal::TryGetBoolFieldAny(Payload, {TEXT("private")}, bBoolValue))
        {
            if (bBoolValue)
            {
                FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, VariableName, nullptr, FBlueprintMetadata::MD_Private, TEXT("true"));
            }
            else
            {
                FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(Blueprint, VariableName, nullptr, FBlueprintMetadata::MD_Private);
            }
        }

        FString Replication;
        if (LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("replication")}, Replication))
        {
            const FString RepLower = Replication.ToLower().Replace(TEXT("_"), TEXT(""));
            uint64* Flags = FBlueprintEditorUtils::GetBlueprintVariablePropertyFlags(Blueprint, VariableName);
            if (Flags == nullptr)
            {
                OutError = TEXT("Failed to get variable property flags for replication.");
                return false;
            }

            if (RepLower == TEXT("none") || RepLower == TEXT("notreplicated"))
            {
                *Flags &= ~(CPF_Net | CPF_RepNotify);
                FBlueprintEditorUtils::SetBlueprintVariableRepNotifyFunc(Blueprint, VariableName, NAME_None);
            }
            else if (RepLower == TEXT("replicated"))
            {
                *Flags |= CPF_Net;
                *Flags &= ~CPF_RepNotify;
                FBlueprintEditorUtils::SetBlueprintVariableRepNotifyFunc(Blueprint, VariableName, NAME_None);
            }
            else if (RepLower == TEXT("repnotify"))
            {
                *Flags |= CPF_Net | CPF_RepNotify;
            }
            else
            {
                OutError = FString::Printf(TEXT("Unsupported replication value: %s. Valid: none, replicated, repNotify."), *Replication);
                return false;
            }
        }

        FString RepNotifyFunc;
        if (LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("repNotifyFunc")}, RepNotifyFunc))
        {
            FBlueprintEditorUtils::SetBlueprintVariableRepNotifyFunc(
                Blueprint, VariableName,
                RepNotifyFunc.IsEmpty() ? NAME_None : FName(*RepNotifyFunc));
        }

        FString ReplicationConditionStr;
        if (LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("replicationCondition")}, ReplicationConditionStr))
        {
            ELifetimeCondition NewCondition = COND_None;
            bool bParsed = false;

            // Try numeric string first
            if (ReplicationConditionStr.IsNumeric())
            {
                const int32 IntVal = FCString::Atoi(*ReplicationConditionStr);
                NewCondition = static_cast<ELifetimeCondition>(IntVal);
                bParsed = true;
            }
            else if (const UEnum* ConditionEnum = StaticEnum<ELifetimeCondition>())
            {
                const int64 EnumVal = ConditionEnum->GetValueByNameString(ReplicationConditionStr, EGetByNameFlags::None);
                if (EnumVal != INDEX_NONE)
                {
                    NewCondition = static_cast<ELifetimeCondition>(EnumVal);
                    bParsed = true;
                }
            }

            if (!bParsed)
            {
                OutError = FString::Printf(TEXT("Unsupported replicationCondition value: %s. Use a COND_* enum name (e.g. COND_None, COND_OwnerOnly) or integer."), *ReplicationConditionStr);
                return false;
            }

            // Update the variable description (persisted in Blueprint asset)
            FBPVariableDescription* VarDesc = LoomleBlueprintAdapterInternal::FindVariableDescription(Blueprint, VariableName);
            if (VarDesc)
            {
                VarDesc->ReplicationCondition = NewCondition;
            }

            // Also update the skeleton class property for immediate reflection before next compile
            if (UClass* SkeletonClass = Blueprint->SkeletonGeneratedClass)
            {
                if (FProperty* Prop = SkeletonClass->FindPropertyByName(VariableName))
                {
                    Prop->SetBlueprintReplicationCondition(NewCondition);
                }
            }
        }

        Blueprint->MarkPackageDirty();
        return true;
    }

    OutError = FString::Printf(TEXT("Unsupported variable operation: %s"), *Operation);
    return false;
}

bool FLoomleBlueprintAdapter::EditFunctionMember(const FString& BlueprintAssetPath, const FString& Operation, const FString& PayloadJson, FString& OutError)
{
    OutError.Empty();

    if (!ValidateMemberEditOperation(TEXT("function"), Operation, OutError))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Payload;
    if (!LoomleBlueprintAdapterInternal::ParsePayloadJson(PayloadJson, Payload))
    {
        OutError = TEXT("Failed to parse function payload JSON.");
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (Blueprint == nullptr)
    {
        OutError = TEXT("Failed to resolve blueprint.");
        return false;
    }

    const FString OperationLower = Operation.ToLower();
    if (OperationLower.Equals(TEXT("create")))
    {
        FString FunctionName;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("functionName"), TEXT("name")}, FunctionName);
        if (FunctionName.IsEmpty())
        {
            OutError = TEXT("function create requires functionName.");
            return false;
        }
        if (!AddFunctionGraph(BlueprintAssetPath, FunctionName, OutError))
        {
            return false;
        }
        Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    }

    FString FunctionName;
    LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("functionName"), TEXT("name")}, FunctionName);
    UEdGraph* FunctionGraph = LoomleBlueprintAdapterInternal::FindFunctionLikeGraph(Blueprint, FunctionName);
    if (FunctionGraph == nullptr)
    {
        OutError = FString::Printf(TEXT("Failed to resolve function graph: %s"), *FunctionName);
        return false;
    }

    if (OperationLower.Equals(TEXT("rename")))
    {
        FString NewName;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("newName"), TEXT("name")}, NewName);
        if (NewName.IsEmpty())
        {
            OutError = TEXT("function rename requires newName.");
            return false;
        }
        return RenameGraph(BlueprintAssetPath, FunctionName, NewName, OutError);
    }

    if (OperationLower.Equals(TEXT("delete")))
    {
        return DeleteGraph(BlueprintAssetPath, FunctionName, OutError);
    }

    if (OperationLower.Equals(TEXT("updatesignature")) || OperationLower.Equals(TEXT("create")))
    {
        const TArray<TSharedPtr<FJsonValue>> Inputs = LoomleBlueprintAdapterInternal::TryGetArrayFieldAny(Payload, {TEXT("inputs"), TEXT("parameters")});
        const TArray<TSharedPtr<FJsonValue>> Outputs = LoomleBlueprintAdapterInternal::TryGetArrayFieldAny(Payload, {TEXT("outputs"), TEXT("returns")});
        if (!LoomleBlueprintAdapterInternal::ApplySignatureToGraph(Blueprint, FunctionGraph, Inputs, Outputs, OutError))
        {
            return false;
        }
    }

    if (OperationLower.Equals(TEXT("setflags")) || OperationLower.Equals(TEXT("create")))
    {
        TWeakObjectPtr<UK2Node_EditablePinBase> EntryNodeWeak;
        TWeakObjectPtr<UK2Node_EditablePinBase> ResultNodeWeak;
        FBlueprintEditorUtils::GetEntryAndResultNodes(FunctionGraph, EntryNodeWeak, ResultNodeWeak);
        UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(EntryNodeWeak.Get());
        if (EntryNode == nullptr)
        {
            OutError = TEXT("Function entry node is missing.");
            return false;
        }

        EntryNode->Modify();
        UFunction* SkeletonFunction = nullptr;
        if (Blueprint->SkeletonGeneratedClass != nullptr)
        {
            SkeletonFunction = Blueprint->SkeletonGeneratedClass->FindFunctionByName(FName(*FunctionGraph->GetName()));
        }
        if (SkeletonFunction != nullptr)
        {
            SkeletonFunction->Modify();
        }
        int32 Flags = EntryNode->GetExtraFlags();
        auto SetFlag = [&Flags, SkeletonFunction, Payload](const TCHAR* FieldName, int32 Flag)
        {
            bool bValue = false;
            if (Payload.IsValid() && Payload->TryGetBoolField(FieldName, bValue))
            {
                const EFunctionFlags FunctionFlag = static_cast<EFunctionFlags>(Flag);
                if (bValue)
                {
                    Flags |= Flag;
                    if (SkeletonFunction != nullptr)
                    {
                        SkeletonFunction->FunctionFlags |= FunctionFlag;
                    }
                }
                else
                {
                    Flags &= ~Flag;
                    if (SkeletonFunction != nullptr)
                    {
                        SkeletonFunction->FunctionFlags &= ~FunctionFlag;
                    }
                }
            }
        };
        SetFlag(TEXT("pure"), FUNC_BlueprintPure);
        SetFlag(TEXT("const"), FUNC_Const);
        SetFlag(TEXT("public"), FUNC_Public);
        SetFlag(TEXT("protected"), FUNC_Protected);
        SetFlag(TEXT("private"), FUNC_Private);
        EntryNode->SetExtraFlags(Flags);

        const bool bDisableOrphanSaving = EntryNode->bDisableOrphanPinSaving;
        EntryNode->bDisableOrphanPinSaving = true;
        EntryNode->ReconstructNode();
        EntryNode->bDisableOrphanPinSaving = bDisableOrphanSaving;
        if (UK2Node_EditablePinBase* ResultNode = ResultNodeWeak.Get())
        {
            const bool bResultDisableOrphanSaving = ResultNode->bDisableOrphanPinSaving;
            ResultNode->bDisableOrphanPinSaving = true;
            ResultNode->ReconstructNode();
            ResultNode->bDisableOrphanPinSaving = bResultDisableOrphanSaving;
        }
        GetDefault<UEdGraphSchema_K2>()->HandleParameterDefaultValueChanged(EntryNode);

        bool bCallInEditor = false;
        if (LoomleBlueprintAdapterInternal::TryGetBoolFieldAny(Payload, {TEXT("callInEditor")}, bCallInEditor))
        {
            EntryNode->MetaData.bCallInEditor = bCallInEditor;
        }
        bool bDeprecated = false;
        if (LoomleBlueprintAdapterInternal::TryGetBoolFieldAny(Payload, {TEXT("deprecated")}, bDeprecated))
        {
            EntryNode->MetaData.bIsDeprecated = bDeprecated;
        }
        FString DeprecationMessage;
        if (LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("deprecationMessage")}, DeprecationMessage))
        {
            EntryNode->MetaData.DeprecationMessage = DeprecationMessage;
        }
        FString Category;
        if (LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("category")}, Category))
        {
            FBlueprintEditorUtils::SetBlueprintFunctionOrMacroCategory(FunctionGraph, FText::FromString(Category), false);
        }
        FString Tooltip;
        if (LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("tooltip")}, Tooltip))
        {
            EntryNode->MetaData.ToolTip = FText::FromString(Tooltip);
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
    }

    return true;
}

bool FLoomleBlueprintAdapter::EditMacroMember(const FString& BlueprintAssetPath, const FString& Operation, const FString& PayloadJson, FString& OutError)
{
    OutError.Empty();

    if (!ValidateMemberEditOperation(TEXT("macro"), Operation, OutError))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Payload;
    if (!LoomleBlueprintAdapterInternal::ParsePayloadJson(PayloadJson, Payload))
    {
        OutError = TEXT("Failed to parse macro payload JSON.");
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (Blueprint == nullptr)
    {
        OutError = TEXT("Failed to resolve blueprint.");
        return false;
    }

    const FString OperationLower = Operation.ToLower();
    if (OperationLower.Equals(TEXT("create")))
    {
        FString MacroName;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("macroName"), TEXT("name")}, MacroName);
        if (MacroName.IsEmpty())
        {
            OutError = TEXT("macro create requires macroName.");
            return false;
        }
        if (!AddMacroGraph(BlueprintAssetPath, MacroName, OutError))
        {
            return false;
        }
        Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    }

    FString MacroName;
    LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("macroName"), TEXT("name")}, MacroName);
    UEdGraph* MacroGraph = LoomleBlueprintAdapterInternal::FindFunctionLikeGraph(Blueprint, MacroName);
    if (MacroGraph == nullptr)
    {
        OutError = FString::Printf(TEXT("Failed to resolve macro graph: %s"), *MacroName);
        return false;
    }

    if (OperationLower.Equals(TEXT("rename")))
    {
        FString NewName;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("newName"), TEXT("name")}, NewName);
        if (NewName.IsEmpty())
        {
            OutError = TEXT("macro rename requires newName.");
            return false;
        }
        return RenameGraph(BlueprintAssetPath, MacroName, NewName, OutError);
    }

    if (OperationLower.Equals(TEXT("delete")))
    {
        return DeleteGraph(BlueprintAssetPath, MacroName, OutError);
    }

    if (OperationLower.Equals(TEXT("updatesignature")) || OperationLower.Equals(TEXT("create")))
    {
        const TArray<TSharedPtr<FJsonValue>> Inputs = LoomleBlueprintAdapterInternal::TryGetArrayFieldAny(Payload, {TEXT("inputs"), TEXT("parameters")});
        const TArray<TSharedPtr<FJsonValue>> Outputs = LoomleBlueprintAdapterInternal::TryGetArrayFieldAny(Payload, {TEXT("outputs"), TEXT("returns")});
        if (!LoomleBlueprintAdapterInternal::ApplySignatureToGraph(Blueprint, MacroGraph, Inputs, Outputs, OutError))
        {
            return false;
        }
    }

    FString Category;
    if (LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("category")}, Category))
    {
        FBlueprintEditorUtils::SetBlueprintFunctionOrMacroCategory(MacroGraph, FText::FromString(Category), false);
    }

    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::EditDispatcherMember(const FString& BlueprintAssetPath, const FString& Operation, const FString& PayloadJson, FString& OutError)
{
    OutError.Empty();

    if (!ValidateMemberEditOperation(TEXT("dispatcher"), Operation, OutError))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Payload;
    if (!LoomleBlueprintAdapterInternal::ParsePayloadJson(PayloadJson, Payload))
    {
        OutError = TEXT("Failed to parse dispatcher payload JSON.");
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (Blueprint == nullptr)
    {
        OutError = TEXT("Failed to resolve blueprint.");
        return false;
    }

    const FString OperationLower = Operation.ToLower();
    if (OperationLower.Equals(TEXT("create")))
    {
        FString DispatcherName;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("dispatcherName"), TEXT("name")}, DispatcherName);
        if (DispatcherName.IsEmpty())
        {
            OutError = TEXT("dispatcher create requires dispatcherName.");
            return false;
        }

        FEdGraphPinType DelegateType;
        DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
        if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*DispatcherName), DelegateType))
        {
            OutError = FString::Printf(TEXT("Failed to create dispatcher variable: %s"), *DispatcherName);
            return false;
        }

        const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
        UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*DispatcherName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        if (NewGraph == nullptr || K2Schema == nullptr)
        {
            OutError = TEXT("Failed to create dispatcher graph.");
            return false;
        }

        K2Schema->CreateDefaultNodesForGraph(*NewGraph);
        K2Schema->CreateFunctionGraphTerminators(*NewGraph, static_cast<UClass*>(nullptr));
        K2Schema->AddExtraFunctionFlags(NewGraph, FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
        K2Schema->MarkFunctionEntryAsEditable(NewGraph, true);
        Blueprint->DelegateSignatureGraphs.Add(NewGraph);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
    }

    FString DispatcherName;
    LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("dispatcherName"), TEXT("name")}, DispatcherName);
    UEdGraph* DispatcherGraph = LoomleBlueprintAdapterInternal::FindFunctionLikeGraph(Blueprint, DispatcherName);
    if (DispatcherGraph == nullptr)
    {
        OutError = FString::Printf(TEXT("Failed to resolve dispatcher graph: %s"), *DispatcherName);
        return false;
    }

    if (OperationLower.Equals(TEXT("rename")))
    {
        FString NewName;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("newName"), TEXT("name")}, NewName);
        if (NewName.IsEmpty())
        {
            OutError = TEXT("dispatcher rename requires newName.");
            return false;
        }
        FBlueprintEditorUtils::RenameMemberVariable(Blueprint, FName(*DispatcherName), FName(*NewName));
        Blueprint->MarkPackageDirty();
        return true;
    }

    if (OperationLower.Equals(TEXT("delete")))
    {
        FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, FName(*DispatcherName));
        return DeleteGraph(BlueprintAssetPath, DispatcherName, OutError);
    }

    if (OperationLower.Equals(TEXT("updatesignature")) || OperationLower.Equals(TEXT("create")))
    {
        const TArray<TSharedPtr<FJsonValue>> Inputs = LoomleBlueprintAdapterInternal::TryGetArrayFieldAny(Payload, {TEXT("inputs"), TEXT("parameters")});
        const TArray<TSharedPtr<FJsonValue>> Outputs = LoomleBlueprintAdapterInternal::TryGetArrayFieldAny(Payload, {TEXT("outputs"), TEXT("returns")});
        if (!LoomleBlueprintAdapterInternal::ApplySignatureToGraph(Blueprint, DispatcherGraph, Inputs, Outputs, OutError))
        {
            return false;
        }
    }

    FString Category;
    if (LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("category")}, Category))
    {
        FBlueprintEditorUtils::SetBlueprintFunctionOrMacroCategory(DispatcherGraph, FText::FromString(Category), false);
    }

    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::EditEventMember(const FString& BlueprintAssetPath, const FString& Operation, const FString& PayloadJson, FString& OutError)
{
    OutError.Empty();

    if (!ValidateMemberEditOperation(TEXT("event"), Operation, OutError))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Payload;
    if (!LoomleBlueprintAdapterInternal::ParsePayloadJson(PayloadJson, Payload))
    {
        OutError = TEXT("Failed to parse event payload JSON.");
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (Blueprint == nullptr)
    {
        OutError = TEXT("Failed to resolve blueprint.");
        return false;
    }

    const FString OperationLower = Operation.ToLower();
    FString EventName;
    LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("eventName"), TEXT("customEventName"), TEXT("name")}, EventName);

    if (OperationLower.Equals(TEXT("create")))
    {
        if (EventName.IsEmpty())
        {
            OutError = TEXT("event create requires name.");
            return false;
        }

        FString GraphName;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("graphName")}, GraphName);
        if (GraphName.IsEmpty())
        {
            if (const TSharedPtr<FJsonObject> GraphObject = LoomleBlueprintAdapterInternal::TryGetObjectFieldAny(Payload, {TEXT("graph")}))
            {
                LoomleBlueprintAdapterInternal::TryGetStringFieldAny(GraphObject, {TEXT("name"), TEXT("graphName")}, GraphName);
            }
        }

        int32 NodePosX = 0;
        int32 NodePosY = 0;
        double X = 0.0;
        double Y = 0.0;
        if (LoomleBlueprintAdapterInternal::TryGetNumberFieldAny(Payload, {TEXT("x")}, X))
        {
            NodePosX = static_cast<int32>(X);
        }
        if (LoomleBlueprintAdapterInternal::TryGetNumberFieldAny(Payload, {TEXT("y")}, Y))
        {
            NodePosY = static_cast<int32>(Y);
        }
        if (const TSharedPtr<FJsonObject> PositionObject = LoomleBlueprintAdapterInternal::TryGetObjectFieldAny(Payload, {TEXT("position")}))
        {
            if (LoomleBlueprintAdapterInternal::TryGetNumberFieldAny(PositionObject, {TEXT("x")}, X))
            {
                NodePosX = static_cast<int32>(X);
            }
            if (LoomleBlueprintAdapterInternal::TryGetNumberFieldAny(PositionObject, {TEXT("y")}, Y))
            {
                NodePosY = static_cast<int32>(Y);
            }
        }

        FString NodeGuid;
        return AddCustomEventNode(BlueprintAssetPath, GraphName, EventName, PayloadJson, NodePosX, NodePosY, NodeGuid, OutError);
    }

    if (EventName.IsEmpty())
    {
        OutError = TEXT("event operation requires name.");
        return false;
    }

    UK2Node_CustomEvent* CustomEvent = LoomleBlueprintAdapterInternal::FindCustomEventNode(Blueprint, EventName);
    if (CustomEvent == nullptr)
    {
        OutError = FString::Printf(TEXT("Failed to resolve custom event: %s"), *EventName);
        return false;
    }

    if (OperationLower.Equals(TEXT("rename")))
    {
        FString NewName;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("newName")}, NewName);
        if (NewName.IsEmpty())
        {
            OutError = TEXT("event rename requires newName.");
            return false;
        }
        if (LoomleBlueprintAdapterInternal::FindCustomEventNode(Blueprint, NewName) != nullptr)
        {
            OutError = FString::Printf(TEXT("Custom event already exists: %s"), *NewName);
            return false;
        }
        const FName UniqueName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, NewName);
        if (!UniqueName.ToString().Equals(NewName, ESearchCase::CaseSensitive))
        {
            OutError = FString::Printf(TEXT("Blueprint member name is already in use: %s"), *NewName);
            return false;
        }

        CustomEvent->Modify();
        CustomEvent->OnRenameNode(NewName);
        CustomEvent->ReconstructNode();
        FBlueprintEditorUtils::ValidateBlueprintChildVariables(Blueprint, CustomEvent->CustomFunctionName);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
        return true;
    }

    if (OperationLower.Equals(TEXT("delete")))
    {
        if (UEdGraph* Graph = CustomEvent->GetGraph())
        {
            Graph->Modify();
            Graph->RemoveNode(CustomEvent);
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            Blueprint->MarkPackageDirty();
            return true;
        }

        OutError = TEXT("Custom event graph is missing.");
        return false;
    }

    if (OperationLower.Equals(TEXT("updatesignature")))
    {
        const TArray<TSharedPtr<FJsonValue>> Outputs = LoomleBlueprintAdapterInternal::TryGetArrayFieldAny(Payload, {TEXT("outputs"), TEXT("returns")});
        if (Outputs.Num() > 0)
        {
            OutError = LoomleBlueprintAdapterInternal::MakeCustomEventInputError(
                TEXT("CUSTOM_EVENT_OUTPUTS_UNSUPPORTED"),
                TEXT("customEventOutputsUnsupported"),
                TEXT("Custom event signatures only support input parameters."),
                TEXT("validateSignature"),
                EventName,
                CustomEvent,
                Payload,
                TEXT("Use inputs/parameters for Custom Event payload data; events do not support return values."));
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>> Inputs = LoomleBlueprintAdapterInternal::TryGetArrayFieldAny(Payload, {TEXT("inputs"), TEXT("parameters")});
        return LoomleBlueprintAdapterInternal::ApplySignatureToCustomEvent(Blueprint, CustomEvent, Inputs, OutError);
    }

    if (OperationLower.Equals(TEXT("addinput")))
    {
        return LoomleBlueprintAdapterInternal::AddInputToCustomEvent(Blueprint, CustomEvent, Payload, OutError);
    }

    if (OperationLower.Equals(TEXT("setflags")))
    {
        return LoomleBlueprintAdapterInternal::ApplyCustomEventNetworkSettings(Blueprint, CustomEvent, Payload, OutError);
    }

    OutError = FString::Printf(TEXT("Unsupported event operation: %s"), *Operation);
    return false;
}

bool FLoomleBlueprintAdapter::AddEventNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& EventName, const FString& EventClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    UClass* EventClass = LoomleBlueprintAdapterInternal::ResolveClass(EventClassPath);
    if (!Blueprint || !EventGraph || !EventClass)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph/event class.");
        return false;
    }

    int32 PosY = NodePosY;
    UK2Node_Event* Node = FKismetEditorUtilities::AddDefaultEventNode(Blueprint, EventGraph, *EventName, EventClass, PosY);
    if (!Node)
    {
        OutError = FString::Printf(TEXT("Failed to add event node: %s"), *EventName);
        return false;
    }

    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddCustomEventNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& EventName, const FString& PayloadJson, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    if (EventName.IsEmpty())
    {
        OutError = TEXT("Custom event name is required.");
        return false;
    }

    TSharedPtr<FJsonObject> Payload;
    if (!LoomleBlueprintAdapterInternal::ParsePayloadJson(PayloadJson, Payload))
    {
        OutError = TEXT("Invalid custom event payload JSON.");
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }
    if (!Blueprint->UbergraphPages.Contains(EventGraph))
    {
        OutError = TEXT("Custom events can only be created in an event graph.");
        return false;
    }

    if (LoomleBlueprintAdapterInternal::FindCustomEventNode(Blueprint, EventName) != nullptr)
    {
        OutError = FString::Printf(TEXT("Custom event already exists: %s"), *EventName);
        return false;
    }

    const FName UniqueName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, EventName);
    if (!UniqueName.ToString().Equals(EventName, ESearchCase::CaseSensitive))
    {
        OutError = FString::Printf(TEXT("Blueprint member name is already in use: %s"), *EventName);
        return false;
    }

    UK2Node_CustomEvent* Node = NewObject<UK2Node_CustomEvent>(EventGraph);
    if (Node == nullptr)
    {
        OutError = TEXT("Failed to create custom event node.");
        return false;
    }

    EventGraph->Modify();
    Node->SetFlags(RF_Transactional);
    Node->CustomFunctionName = FName(*EventName);
    Node->bOverrideFunction = false;
    Node->bIsEditable = true;
    Node->CreateNewGuid();
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->PostPlacedNewNode();
    Node->AllocateDefaultPins();
    EventGraph->AddNode(Node, false, false);

    const TArray<TSharedPtr<FJsonValue>> Outputs = LoomleBlueprintAdapterInternal::TryGetArrayFieldAny(Payload, {TEXT("outputs"), TEXT("returns")});
    if (Outputs.Num() > 0)
    {
        EventGraph->RemoveNode(Node);
        OutError = LoomleBlueprintAdapterInternal::MakeCustomEventInputError(
            TEXT("CUSTOM_EVENT_OUTPUTS_UNSUPPORTED"),
            TEXT("customEventOutputsUnsupported"),
            TEXT("Custom event signatures only support input parameters."),
            TEXT("validateSignature"),
            EventName,
            Node,
            Payload,
            TEXT("Use inputs/parameters for Custom Event payload data; events do not support return values."));
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>> Inputs = LoomleBlueprintAdapterInternal::TryGetArrayFieldAny(Payload, {TEXT("inputs"), TEXT("parameters")});
    if (!LoomleBlueprintAdapterInternal::ApplySignatureToCustomEvent(Blueprint, Node, Inputs, OutError))
    {
        EventGraph->RemoveNode(Node);
        return false;
    }
    if (!LoomleBlueprintAdapterInternal::ApplyCustomEventNetworkSettings(Blueprint, Node, Payload, OutError))
    {
        EventGraph->RemoveNode(Node);
        return false;
    }

    FBlueprintEditorUtils::ValidateBlueprintChildVariables(Blueprint, Node->CustomFunctionName);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Blueprint->MarkPackageDirty();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddCastNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& TargetClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    UClass* TargetClass = LoomleBlueprintAdapterInternal::ResolveClass(TargetClassPath);
    if (!Blueprint || !EventGraph || !TargetClass)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph/target class.");
        return false;
    }

    FGraphNodeCreator<UK2Node_DynamicCast> Creator(*EventGraph);
    UK2Node_DynamicCast* Node = Creator.CreateNode();
    Node->TargetType = TargetClass;
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->AllocateDefaultPins();
    Node->SetPurity(false);
    Creator.Finalize();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddCallFunctionNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& FunctionClassPath, const FString& FunctionName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    LoomleBlueprintAdapterInternal::FBlueprintFunctionDescriptor Descriptor;
    Descriptor.ClassPath = FunctionClassPath;
    Descriptor.FunctionName = FunctionName;
    return LoomleBlueprintAdapterInternal::AddCallFunctionNodeByDescriptor(
        BlueprintAssetPath,
        GraphName,
        Descriptor,
        NodePosX,
        NodePosY,
        OutNodeGuid,
        OutError);
}

bool FLoomleBlueprintAdapter::AddBranchNode(const FString& BlueprintAssetPath, const FString& GraphName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    FGraphNodeCreator<UK2Node_IfThenElse> Creator(*EventGraph);
    UK2Node_IfThenElse* Node = Creator.CreateNode();
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->AllocateDefaultPins();
    Creator.Finalize();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddExecutionSequenceNode(const FString& BlueprintAssetPath, const FString& GraphName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    FGraphNodeCreator<UK2Node_ExecutionSequence> Creator(*EventGraph);
    UK2Node_ExecutionSequence* Node = Creator.CreateNode();
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->AllocateDefaultPins();
    Creator.Finalize();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddMacroNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& MacroLibraryAssetPath, const FString& MacroGraphName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    const FString EffectiveMacroLibraryAssetPath = LoomleBlueprintAdapterInternal::ResolveMacroLibraryAssetPathOrDefault(MacroLibraryAssetPath);
    UBlueprint* MacroLibrary = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(EffectiveMacroLibraryAssetPath);
    UEdGraph* MacroGraph = LoomleBlueprintAdapterInternal::FindGraphByName(MacroLibrary, MacroGraphName);
    if (MacroGraphName.IsEmpty())
    {
        OutError = LoomleBlueprintAdapterInternal::MakeMacroNodeError(
            TEXT("INVALID_ARGUMENT"),
            TEXT("addNode.byMacro requires macroGraphName."),
            EffectiveMacroLibraryAssetPath,
            MacroGraphName);
        return false;
    }
    if (!Blueprint)
    {
        OutError = LoomleBlueprintAdapterInternal::MakeMacroNodeError(
            TEXT("BLUEPRINT_NOT_FOUND"),
            FString::Printf(TEXT("Blueprint asset was not found: %s"), *BlueprintAssetPath),
            EffectiveMacroLibraryAssetPath,
            MacroGraphName);
        return false;
    }
    if (!EventGraph)
    {
        OutError = LoomleBlueprintAdapterInternal::MakeMacroNodeError(
            TEXT("GRAPH_NOT_FOUND"),
            GraphName.IsEmpty()
                ? TEXT("Target event graph was not found.")
                : FString::Printf(TEXT("Target graph was not found: %s"), *GraphName),
            EffectiveMacroLibraryAssetPath,
            MacroGraphName);
        return false;
    }
    if (!MacroLibrary)
    {
        OutError = LoomleBlueprintAdapterInternal::MakeMacroNodeError(
            TEXT("MACRO_LIBRARY_NOT_FOUND"),
            FString::Printf(TEXT("Macro library asset was not found: %s"), *EffectiveMacroLibraryAssetPath),
            EffectiveMacroLibraryAssetPath,
            MacroGraphName);
        return false;
    }
    if (!MacroGraph)
    {
        OutError = LoomleBlueprintAdapterInternal::MakeMacroNodeError(
            TEXT("MACRO_GRAPH_NOT_FOUND"),
            FString::Printf(TEXT("Macro graph '%s' was not found in macro library '%s'."), *MacroGraphName, *EffectiveMacroLibraryAssetPath),
            EffectiveMacroLibraryAssetPath,
            MacroGraphName,
            MacroLibrary);
        return false;
    }

    FGraphNodeCreator<UK2Node_MacroInstance> Creator(*EventGraph);
    UK2Node_MacroInstance* Node = Creator.CreateNode();
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->SetMacroGraph(MacroGraph);
    Node->AllocateDefaultPins();
    Creator.Finalize();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::ListMacroGraphs(const FString& MacroLibraryAssetPath, FString& OutGraphsJson, FString& OutError)
{
    OutGraphsJson.Empty();
    OutError.Empty();

    const FString EffectiveMacroLibraryAssetPath = LoomleBlueprintAdapterInternal::ResolveMacroLibraryAssetPathOrDefault(MacroLibraryAssetPath);
    UBlueprint* MacroLibrary = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(EffectiveMacroLibraryAssetPath);
    if (MacroLibrary == nullptr)
    {
        OutError = LoomleBlueprintAdapterInternal::MakeMacroNodeError(
            TEXT("MACRO_LIBRARY_NOT_FOUND"),
            FString::Printf(TEXT("Macro library asset was not found: %s"), *EffectiveMacroLibraryAssetPath),
            EffectiveMacroLibraryAssetPath,
            TEXT(""));
        return false;
    }

    OutGraphsJson = LoomleBlueprintAdapterInternal::JsonArrayToString(
        LoomleBlueprintAdapterInternal::MakeMacroGraphNameArray(MacroLibrary));
    return true;
}

bool FLoomleBlueprintAdapter::AddCommentNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& CommentText, int32 NodePosX, int32 NodePosY, int32 Width, int32 Height, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    FGraphNodeCreator<UEdGraphNode_Comment> Creator(*EventGraph);
    UEdGraphNode_Comment* Node = Creator.CreateNode();
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->NodeComment = CommentText;
    Node->NodeWidth = Width > 0 ? Width : Node->NodeWidth;
    Node->NodeHeight = Height > 0 ? Height : Node->NodeHeight;
    Creator.Finalize();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddKnotNode(const FString& BlueprintAssetPath, const FString& GraphName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    FGraphNodeCreator<UK2Node_Knot> Creator(*EventGraph);
    UK2Node_Knot* Node = Creator.CreateNode();
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->AllocateDefaultPins();
    Creator.Finalize();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddVariableGetNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& VariableName, const FString& VariableClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    bool bSelfContext = true;
    UClass* OwnerClass = nullptr;
    const FProperty* Property = LoomleBlueprintAdapterInternal::ResolveVariableProperty(
        Blueprint, VariableName, VariableClassPath, bSelfContext, OwnerClass, nullptr);
    if (!Property)
    {
        OutError = FString::Printf(TEXT("Failed to resolve variable property: %s"), *VariableName);
        return false;
    }

    FGraphNodeCreator<UK2Node_VariableGet> Creator(*EventGraph);
    UK2Node_VariableGet* Node = Creator.CreateNode();
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->SetFromProperty(Property, bSelfContext, OwnerClass);
    Node->AllocateDefaultPins();
    Creator.Finalize();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddVariableSetNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& VariableName, const FString& VariableClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    bool bSelfContext = true;
    UClass* OwnerClass = nullptr;
    const FProperty* Property = LoomleBlueprintAdapterInternal::ResolveVariableProperty(
        Blueprint, VariableName, VariableClassPath, bSelfContext, OwnerClass, nullptr);
    if (!Property)
    {
        OutError = FString::Printf(TEXT("Failed to resolve variable property: %s"), *VariableName);
        return false;
    }

    FGraphNodeCreator<UK2Node_VariableSet> Creator(*EventGraph);
    UK2Node_VariableSet* Node = Creator.CreateNode();
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->SetFromProperty(Property, bSelfContext, OwnerClass);
    Node->AllocateDefaultPins();
    Creator.Finalize();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddNodeByClass(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeClassPath, const FString& PayloadJson, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    TSharedPtr<FJsonObject> Payload;
    if (!LoomleBlueprintAdapterInternal::ParsePayloadJson(PayloadJson, Payload))
    {
        OutError = TEXT("Invalid payload JSON.");
        return false;
    }

    const FString NormalizedClass = NodeClassPath.ToLower();
    if (NormalizedClass.Contains(TEXT("k2node_customevent")))
    {
        FString EventName;
        LoomleBlueprintAdapterInternal::TryGetStringFieldAny(Payload, {TEXT("eventName"), TEXT("customEventName"), TEXT("name")}, EventName);
        return AddCustomEventNode(BlueprintAssetPath, GraphName, EventName, PayloadJson, NodePosX, NodePosY, OutNodeGuid, OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_event")))
    {
        FString EventName;
        FString EventClassPath;
        Payload->TryGetStringField(TEXT("eventName"), EventName);
        Payload->TryGetStringField(TEXT("eventClassPath"), EventClassPath);
        return AddEventNode(BlueprintAssetPath, GraphName, EventName, EventClassPath, NodePosX, NodePosY, OutNodeGuid, OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_dynamiccast")))
    {
        FString TargetClassPath;
        Payload->TryGetStringField(TEXT("targetClassPath"), TargetClassPath);
        return AddCastNode(BlueprintAssetPath, GraphName, TargetClassPath, NodePosX, NodePosY, OutNodeGuid, OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_composite")))
    {
        return LoomleBlueprintAdapterInternal::AddCompositeNode(
            BlueprintAssetPath,
            GraphName,
            Payload,
            NodePosX,
            NodePosY,
            OutNodeGuid,
            OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_addcomponentbyclass")))
    {
        return LoomleBlueprintAdapterInternal::AddContextSensitiveAddComponentByClassNode(
            BlueprintAssetPath,
            GraphName,
            Payload,
            NodePosX,
            NodePosY,
            OutNodeGuid,
            OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_functionresult")))
    {
        return LoomleBlueprintAdapterInternal::AddFunctionResultNode(
            BlueprintAssetPath,
            GraphName,
            NodePosX,
            NodePosY,
            OutNodeGuid,
            OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_addcomponent")))
    {
        return LoomleBlueprintAdapterInternal::AddEmbeddedAddComponentNode(
            BlueprintAssetPath,
            GraphName,
            Payload,
            NodePosX,
            NodePosY,
            OutNodeGuid,
            OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_timeline")))
    {
        return LoomleBlueprintAdapterInternal::AddTimelineNode(
            BlueprintAssetPath,
            GraphName,
            Payload,
            NodePosX,
            NodePosY,
            OutNodeGuid,
            OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_self")))
    {
        return LoomleBlueprintAdapterInternal::AddSelfNode(
            BlueprintAssetPath,
            GraphName,
            NodePosX,
            NodePosY,
            OutNodeGuid,
            OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_callfunction")))
    {
        const LoomleBlueprintAdapterInternal::FBlueprintFunctionDescriptor Descriptor =
            LoomleBlueprintAdapterInternal::ParseFunctionDescriptorFromPayload(Payload);
        return LoomleBlueprintAdapterInternal::AddCallFunctionNodeByDescriptor(
            BlueprintAssetPath,
            GraphName,
            Descriptor,
            NodePosX,
            NodePosY,
            OutNodeGuid,
            OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_executionsequence")))
    {
        return AddExecutionSequenceNode(BlueprintAssetPath, GraphName, NodePosX, NodePosY, OutNodeGuid, OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_ifthenelse")))
    {
        return AddBranchNode(BlueprintAssetPath, GraphName, NodePosX, NodePosY, OutNodeGuid, OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_macroinstance")))
    {
        const LoomleBlueprintAdapterInternal::FBlueprintMacroDescriptor Descriptor =
            LoomleBlueprintAdapterInternal::ParseMacroDescriptorFromPayload(Payload);
        return AddMacroNode(
            BlueprintAssetPath,
            GraphName,
            Descriptor.MacroLibraryAssetPath,
            Descriptor.MacroGraphName,
            NodePosX,
            NodePosY,
            OutNodeGuid,
            OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_variableget")))
    {
        FString VariableName;
        FString VariableClassPath;
        Payload->TryGetStringField(TEXT("variableName"), VariableName);
        Payload->TryGetStringField(TEXT("variableClassPath"), VariableClassPath);
        return AddVariableGetNode(BlueprintAssetPath, GraphName, VariableName, VariableClassPath, NodePosX, NodePosY, OutNodeGuid, OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_variableset")))
    {
        FString VariableName;
        FString VariableClassPath;
        Payload->TryGetStringField(TEXT("variableName"), VariableName);
        Payload->TryGetStringField(TEXT("variableClassPath"), VariableClassPath);
        return AddVariableSetNode(BlueprintAssetPath, GraphName, VariableName, VariableClassPath, NodePosX, NodePosY, OutNodeGuid, OutError);
    }
    if (NormalizedClass.Contains(TEXT("edgraphnode_comment")))
    {
        FString Text;
        double WidthNumber = 0.0;
        double HeightNumber = 0.0;
        Payload->TryGetStringField(TEXT("text"), Text);
        Payload->TryGetNumberField(TEXT("width"), WidthNumber);
        Payload->TryGetNumberField(TEXT("height"), HeightNumber);
        return AddCommentNode(BlueprintAssetPath, GraphName, Text, NodePosX, NodePosY, static_cast<int32>(WidthNumber), static_cast<int32>(HeightNumber), OutNodeGuid, OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_knot")))
    {
        return AddKnotNode(BlueprintAssetPath, GraphName, NodePosX, NodePosY, OutNodeGuid, OutError);
    }

    OutError = FString::Printf(TEXT("Unsupported nodeClassPath for addNode.byClass: %s"), *NodeClassPath);
    return false;
}

bool FLoomleBlueprintAdapter::ConnectPins(const FString& BlueprintAssetPath, const FString& GraphName, const FString& FromNodeGuid, const FString& FromPinName, const FString& ToNodeGuid, const FString& ToPinName, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    UEdGraphNode* FromNode = LoomleBlueprintAdapterInternal::FindNodeByGuid(EventGraph, FromNodeGuid);
    UEdGraphNode* ToNode = LoomleBlueprintAdapterInternal::FindNodeByGuid(EventGraph, ToNodeGuid);
    if (!FromNode)
    {
        OutError = FString::Printf(TEXT("NODE_REF_NOT_FOUND: from.node id was not found in graph: %s"), *FromNodeGuid);
        return false;
    }
    if (!ToNode)
    {
        OutError = FString::Printf(TEXT("NODE_REF_NOT_FOUND: to.node id was not found in graph: %s"), *ToNodeGuid);
        return false;
    }

    UEdGraphPin* FromPin = LoomleBlueprintAdapterInternal::ResolvePin(FromNode, FromPinName);
    UEdGraphPin* ToPin = LoomleBlueprintAdapterInternal::ResolvePin(ToNode, ToPinName);
    if (!FromPin)
    {
        OutError = FString::Printf(TEXT("PIN_REF_NOT_FOUND: from pin was not found on node %s: %s"), *FromNodeGuid, *FromPinName);
        return false;
    }
    if (!ToPin)
    {
        OutError = FString::Printf(TEXT("PIN_REF_NOT_FOUND: to pin was not found on node %s: %s"), *ToNodeGuid, *ToPinName);
        return false;
    }
    if (FromPin->Direction != EGPD_Output || ToPin->Direction != EGPD_Input)
    {
        OutError = TEXT("CONNECT_REQUIRES_OUTPUT_TO_INPUT: connect requires from to be an output pin and to to be an input pin.");
        return false;
    }

    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    if (!Schema)
    {
        OutError = TEXT("INTERNAL_ERROR: Failed to resolve K2 graph schema.");
        return false;
    }

    const FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
    if (Response.Response == CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE || Response.Response == CONNECT_RESPONSE_MAKE_WITH_PROMOTION)
    {
        OutError = FString::Printf(TEXT("CONNECT_PIN_TYPE_MISMATCH: Pins require conversion or promotion, which blueprint.graph.edit does not auto-insert. %s"), *Response.Message.ToString());
        return false;
    }
    if (Response.Response == CONNECT_RESPONSE_DISALLOW)
    {
        OutError = FString::Printf(TEXT("CONNECT_PIN_TYPE_MISMATCH: %s"), *Response.Message.ToString());
        return false;
    }

    Blueprint->Modify();
    EventGraph->Modify();
    FromNode->Modify();
    ToNode->Modify();
    if (!Schema->TryCreateConnection(FromPin, ToPin))
    {
        OutError = FString::Printf(TEXT("CONNECT_PIN_TYPE_MISMATCH: %s"), *Response.Message.ToString());
        return false;
    }

    EventGraph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::DuplicateNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, int32 DeltaX, int32 DeltaY, FString& OutNewNodeGuid, FString& OutError)
{
    OutNewNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* TargetGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !TargetGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    UEdGraphNode* SourceNode = LoomleBlueprintAdapterInternal::ResolveNodeByToken(TargetGraph, NodeGuid);
    if (SourceNode == nullptr)
    {
        OutError = FString::Printf(TEXT("Node not found in graph by id/path/name: %s"), *NodeGuid);
        return false;
    }
    if (!SourceNode->CanDuplicateNode())
    {
        OutError = TEXT("Node does not support duplication.");
        return false;
    }

    TSet<UObject*> NodesToExport;
    NodesToExport.Add(SourceNode);

    FString ExportedText;
    FEdGraphUtilities::ExportNodesToText(NodesToExport, ExportedText);
    if (ExportedText.IsEmpty() || !FEdGraphUtilities::CanImportNodesFromText(TargetGraph, ExportedText))
    {
        OutError = TEXT("Node duplication export/import pipeline rejected the node.");
        return false;
    }

    Blueprint->Modify();
    TargetGraph->Modify();

    TSet<UEdGraphNode*> ImportedNodes;
    FEdGraphUtilities::ImportNodesFromText(TargetGraph, ExportedText, ImportedNodes);
    if (ImportedNodes.Num() == 0)
    {
        OutError = TEXT("Node duplication import produced no nodes.");
        return false;
    }

    UEdGraphNode* DuplicatedNode = nullptr;
    for (UEdGraphNode* ImportedNode : ImportedNodes)
    {
        if (ImportedNode != nullptr && ImportedNode->GetClass() == SourceNode->GetClass())
        {
            DuplicatedNode = ImportedNode;
            break;
        }
    }
    if (DuplicatedNode == nullptr)
    {
        DuplicatedNode = ImportedNodes.Array()[0];
    }
    if (DuplicatedNode == nullptr)
    {
        OutError = TEXT("Node duplication import returned only null nodes.");
        return false;
    }

    DuplicatedNode->Modify();
    DuplicatedNode->CreateNewGuid();
    DuplicatedNode->NodePosX = SourceNode->NodePosX + DeltaX;
    DuplicatedNode->NodePosY = SourceNode->NodePosY + DeltaY;
    DuplicatedNode->SnapToGrid(16);

    TargetGraph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Blueprint->MarkPackageDirty();

    OutNewNodeGuid = DuplicatedNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::DisconnectPins(const FString& BlueprintAssetPath, const FString& GraphName, const FString& FromNodeGuid, const FString& FromPinName, const FString& ToNodeGuid, const FString& ToPinName, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    UEdGraphNode* FromNode = LoomleBlueprintAdapterInternal::FindNodeByGuid(EventGraph, FromNodeGuid);
    UEdGraphNode* ToNode = LoomleBlueprintAdapterInternal::FindNodeByGuid(EventGraph, ToNodeGuid);
    if (!FromNode)
    {
        OutError = FString::Printf(TEXT("NODE_REF_NOT_FOUND: from.node id was not found in graph: %s"), *FromNodeGuid);
        return false;
    }
    if (!ToNode)
    {
        OutError = FString::Printf(TEXT("NODE_REF_NOT_FOUND: to.node id was not found in graph: %s"), *ToNodeGuid);
        return false;
    }

    UEdGraphPin* FromPin = LoomleBlueprintAdapterInternal::ResolvePin(FromNode, FromPinName);
    UEdGraphPin* ToPin = LoomleBlueprintAdapterInternal::ResolvePin(ToNode, ToPinName);
    if (!FromPin)
    {
        OutError = FString::Printf(TEXT("PIN_REF_NOT_FOUND: from pin was not found on node %s: %s"), *FromNodeGuid, *FromPinName);
        return false;
    }
    if (!ToPin)
    {
        OutError = FString::Printf(TEXT("PIN_REF_NOT_FOUND: to pin was not found on node %s: %s"), *ToNodeGuid, *ToPinName);
        return false;
    }
    if (FromPin->Direction != EGPD_Output || ToPin->Direction != EGPD_Input)
    {
        OutError = TEXT("CONNECT_REQUIRES_OUTPUT_TO_INPUT: disconnect requires from to be an output pin and to to be an input pin.");
        return false;
    }

    if (!(FromPin->LinkedTo.Contains(ToPin) || ToPin->LinkedTo.Contains(FromPin)))
    {
        OutError = TEXT("LINK_NOT_FOUND: specified pin link does not exist; disconnect treated it as a no-op.");
        return true;
    }

    Blueprint->Modify();
    EventGraph->Modify();
    FromNode->Modify();
    ToNode->Modify();
    FromPin->BreakLinkTo(ToPin);
    EventGraph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::BreakPinLinks(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, const FString& PinName, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    UEdGraphNode* Node = LoomleBlueprintAdapterInternal::FindNodeByGuid(EventGraph, NodeGuid);
    UEdGraphPin* Pin = LoomleBlueprintAdapterInternal::ResolvePin(Node, PinName);
    if (!Node)
    {
        OutError = FString::Printf(TEXT("NODE_REF_NOT_FOUND: target.node id was not found in graph: %s"), *NodeGuid);
        return false;
    }
    if (!Pin)
    {
        OutError = FString::Printf(TEXT("PIN_REF_NOT_FOUND: target pin was not found on node %s: %s"), *NodeGuid, *PinName);
        return false;
    }

    Blueprint->Modify();
    EventGraph->Modify();
    Node->Modify();
    Pin->BreakAllPinLinks(true);
    EventGraph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::RemoveNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    UEdGraphNode* Node = LoomleBlueprintAdapterInternal::ResolveNodeByToken(EventGraph, NodeGuid);
    if (!Node)
    {
        OutError = FString::Printf(TEXT("Node not found in graph by id/path/name: %s"), *NodeGuid);
        return false;
    }

    Blueprint->Modify();
    EventGraph->Modify();
    Node->DestroyNode();

    if (LoomleBlueprintAdapterInternal::ResolveNodeByToken(EventGraph, NodeGuid) != nullptr)
    {
        UEdGraphNode* FallbackNode = LoomleBlueprintAdapterInternal::ResolveNodeByToken(EventGraph, NodeGuid);
        if (FallbackNode)
        {
            FallbackNode->Modify();
            FallbackNode->BreakAllNodeLinks();
            EventGraph->RemoveNode(FallbackNode);
        }
    }

    if (LoomleBlueprintAdapterInternal::ResolveNodeByToken(EventGraph, NodeGuid) != nullptr)
    {
        OutError = FString::Printf(TEXT("Node remained in graph after removal attempt: %s"), *NodeGuid);
        return false;
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::MoveNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, int32 NodePosX, int32 NodePosY, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    UEdGraphNode* Node = LoomleBlueprintAdapterInternal::FindNodeByGuid(EventGraph, NodeGuid);
    if (!Node)
    {
        OutError = FString::Printf(TEXT("Node not found by guid: %s"), *NodeGuid);
        return false;
    }

    EventGraph->Modify();
    Node->Modify();
    const int32 OriginalX = Node->NodePosX;
    const int32 OriginalY = Node->NodePosY;
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->SnapToGrid(16);
    EventGraph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Blueprint->MarkPackageDirty();

    if ((OriginalX != NodePosX || OriginalY != NodePosY)
        && Node->NodePosX == OriginalX
        && Node->NodePosY == OriginalY)
    {
        OutError = FString::Printf(
            TEXT("Node move verification failed. Requested (%d, %d), observed no movement from (%d, %d)."),
            NodePosX,
            NodePosY,
            OriginalX,
            OriginalY);
        return false;
    }

    return true;
}

bool FLoomleBlueprintAdapter::SetPinDefaultValue(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, const FString& PinName, const FString& Value, FString& OutError)
{
    return SetPinDefaultValue(BlueprintAssetPath, GraphName, NodeGuid, PinName, Value, TEXT(""), TEXT(""), OutError);
}

bool FLoomleBlueprintAdapter::SetPinDefaultValue(
    const FString& BlueprintAssetPath,
    const FString& GraphName,
    const FString& NodeGuid,
    const FString& PinName,
    const FString& Value,
    const FString& ObjectPath,
    const FString& TextValue,
    FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    UEdGraphNode* Node = LoomleBlueprintAdapterInternal::ResolveNodeByToken(EventGraph, NodeGuid);
    UEdGraphPin* Pin = LoomleBlueprintAdapterInternal::ResolvePin(Node, PinName);
    if (!Node)
    {
        OutError = FString::Printf(TEXT("Node not found in graph by id/path/name: %s"), *NodeGuid);
        return false;
    }
    if (!Pin)
    {
        OutError = FString::Printf(TEXT("Pin not found on node %s: %s"), *Node->GetName(), *PinName);
        return false;
    }
    if (Pin->LinkedTo.Num() > 0)
    {
        OutError = FString::Printf(TEXT("PIN_DEFAULT_REQUIRES_UNLINKED_PIN: Pin %s on node %s has existing links. Break links before setting a default value."), *PinName, *Node->GetName());
        return false;
    }

    EventGraph->Modify();
    Node->Modify();
    if (!ObjectPath.IsEmpty())
    {
        UObject* DefaultObject = LoomleBlueprintAdapterInternal::ResolveDefaultObject(ObjectPath);
        if (DefaultObject == nullptr)
        {
            OutError = FString::Printf(TEXT("PIN_DEFAULT_OBJECT_NOT_FOUND: Failed to resolve default object path for pin %s: %s"), *PinName, *ObjectPath);
            return false;
        }
        const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
        if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_SoftObject
            && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_SoftClass)
        {
            const FString ValidationError = Schema->IsPinDefaultValid(Pin, FString(), DefaultObject, FText::GetEmpty());
            if (!ValidationError.IsEmpty())
            {
                OutError = FString::Printf(TEXT("PIN_DEFAULT_OBJECT_INVALID_FOR_PIN: %s"), *ValidationError);
                return false;
            }
        }
        Schema->TrySetDefaultObject(*Pin, DefaultObject, false);
    }
    else if (!TextValue.IsEmpty())
    {
        Pin->DefaultValue.Empty();
        Pin->DefaultObject = nullptr;
        Pin->DefaultTextValue = FText::FromString(TextValue);
        Node->PinDefaultValueChanged(Pin);
    }
    else
    {
        Pin->DefaultValue = Value;
        Pin->DefaultObject = nullptr;
        Pin->DefaultTextValue = FText::GetEmpty();
        Node->PinDefaultValueChanged(Pin);
    }
    EventGraph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::SetNodeComment(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, const FString& Comment, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* TargetGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !TargetGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    UEdGraphNode* Node = LoomleBlueprintAdapterInternal::ResolveNodeByToken(TargetGraph, NodeGuid);
    if (Node == nullptr)
    {
        OutError = FString::Printf(TEXT("Node not found in graph by id/path/name: %s"), *NodeGuid);
        return false;
    }

    TargetGraph->Modify();
    Node->Modify();
    Node->NodeComment = Comment;

    TargetGraph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::SetNodeEnabled(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, bool bEnabled, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* TargetGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !TargetGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    UEdGraphNode* Node = LoomleBlueprintAdapterInternal::ResolveNodeByToken(TargetGraph, NodeGuid);
    if (Node == nullptr)
    {
        OutError = FString::Printf(TEXT("Node not found in graph by id/path/name: %s"), *NodeGuid);
        return false;
    }

    TargetGraph->Modify();
    Node->Modify();
    Node->SetEnabledState(bEnabled ? ENodeEnabledState::Enabled : ENodeEnabledState::Disabled);

    TargetGraph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::AddFunctionGraph(const FString& AssetPath, const FString& GraphName, FString& OutError)
{
    OutError.Empty();

    if (GraphName.IsEmpty())
    {
        OutError = TEXT("Graph name is required.");
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(AssetPath);
    if (Blueprint == nullptr)
    {
        OutError = TEXT("Failed to resolve blueprint.");
        return false;
    }
    if (LoomleBlueprintAdapterInternal::FindGraphByName(Blueprint, GraphName) != nullptr)
    {
        OutError = FString::Printf(TEXT("Graph already exists: %s"), *GraphName);
        return false;
    }

    Blueprint->Modify();
    UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
        Blueprint,
        *GraphName,
        UEdGraph::StaticClass(),
        UEdGraphSchema_K2::StaticClass());
    if (NewGraph == nullptr)
    {
        OutError = TEXT("Failed to create function graph.");
        return false;
    }

    FBlueprintEditorUtils::AddFunctionGraph(Blueprint, NewGraph, true, static_cast<UClass*>(nullptr));
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::AddMacroGraph(const FString& AssetPath, const FString& GraphName, FString& OutError)
{
    OutError.Empty();

    if (GraphName.IsEmpty())
    {
        OutError = TEXT("Graph name is required.");
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(AssetPath);
    if (Blueprint == nullptr)
    {
        OutError = TEXT("Failed to resolve blueprint.");
        return false;
    }
    if (LoomleBlueprintAdapterInternal::FindGraphByName(Blueprint, GraphName) != nullptr)
    {
        OutError = FString::Printf(TEXT("Graph already exists: %s"), *GraphName);
        return false;
    }

    Blueprint->Modify();
    UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
        Blueprint,
        *GraphName,
        UEdGraph::StaticClass(),
        UEdGraphSchema_K2::StaticClass());
    if (NewGraph == nullptr)
    {
        OutError = TEXT("Failed to create macro graph.");
        return false;
    }

    FBlueprintEditorUtils::AddMacroGraph(Blueprint, NewGraph, true, nullptr);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::RenameGraph(const FString& AssetPath, const FString& OldGraphName, const FString& NewGraphName, FString& OutError)
{
    OutError.Empty();

    if (OldGraphName.IsEmpty() || NewGraphName.IsEmpty())
    {
        OutError = TEXT("Both old and new graph names are required.");
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(AssetPath);
    if (Blueprint == nullptr)
    {
        OutError = TEXT("Failed to resolve blueprint.");
        return false;
    }

    UEdGraph* Graph = LoomleBlueprintAdapterInternal::FindGraphByName(Blueprint, OldGraphName);
    if (Graph == nullptr)
    {
        OutError = FString::Printf(TEXT("Graph not found: %s"), *OldGraphName);
        return false;
    }
    if (!OldGraphName.Equals(NewGraphName) && LoomleBlueprintAdapterInternal::FindGraphByName(Blueprint, NewGraphName) != nullptr)
    {
        OutError = FString::Printf(TEXT("Graph already exists: %s"), *NewGraphName);
        return false;
    }

    Blueprint->Modify();
    Graph->Modify();
    FBlueprintEditorUtils::RenameGraph(Graph, NewGraphName);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::DeleteGraph(const FString& AssetPath, const FString& GraphName, FString& OutError)
{
    OutError.Empty();

    if (GraphName.IsEmpty())
    {
        OutError = TEXT("Graph name is required.");
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(AssetPath);
    if (Blueprint == nullptr)
    {
        OutError = TEXT("Failed to resolve blueprint.");
        return false;
    }

    UEdGraph* Graph = LoomleBlueprintAdapterInternal::FindGraphByName(Blueprint, GraphName);
    if (Graph == nullptr)
    {
        OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
        return false;
    }
    if (Blueprint->UbergraphPages.Contains(Graph) || GraphName.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase))
    {
        OutError = TEXT("Deleting the EventGraph is not allowed.");
        return false;
    }

    Blueprint->Modify();
    Graph->Modify();
    FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::DescribePinTarget(
    const FString& BlueprintAssetPath,
    const FString& GraphName,
    const FString& NodeToken,
    const FString& PinName,
    FString& OutDetailsJson,
    FString& OutError)
{
    OutDetailsJson = TEXT("{}");
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
    Details->SetStringField(TEXT("assetPath"), BlueprintAssetPath);
    Details->SetStringField(TEXT("graphName"), GraphName);

    TSharedPtr<FJsonObject> RequestedTarget = MakeShared<FJsonObject>();
    RequestedTarget->SetStringField(TEXT("nodeToken"), NodeToken);
    RequestedTarget->SetStringField(TEXT("pinName"), PinName);
    Details->SetObjectField(TEXT("requestedTarget"), RequestedTarget);

    TArray<TSharedPtr<FJsonValue>> ExpectedTargetForms;
    auto AddExpectedTargetForm = [&ExpectedTargetForms](const FString& NodeField, const FString& PinField)
    {
        TSharedPtr<FJsonObject> Form = MakeShared<FJsonObject>();
        TSharedPtr<FJsonObject> Target = MakeShared<FJsonObject>();
        Target->SetStringField(NodeField, FString::Printf(TEXT("<%s>"), *NodeField));
        Target->SetStringField(PinField, FString::Printf(TEXT("<%s>"), *PinField));
        Form->SetObjectField(TEXT("target"), Target);
        ExpectedTargetForms.Add(MakeShared<FJsonValueObject>(Form));
    };
    AddExpectedTargetForm(TEXT("nodeId"), TEXT("pinName"));
    AddExpectedTargetForm(TEXT("nodeId"), TEXT("pin"));
    AddExpectedTargetForm(TEXT("nodePath"), TEXT("pinName"));
    AddExpectedTargetForm(TEXT("nodeName"), TEXT("pin"));
    AddExpectedTargetForm(TEXT("nodeRef"), TEXT("pin"));
    Details->SetArrayField(TEXT("expectedTargetForms"), ExpectedTargetForms);

    UEdGraphNode* Node = LoomleBlueprintAdapterInternal::ResolveNodeByToken(EventGraph, NodeToken);
    Details->SetBoolField(TEXT("nodeFound"), Node != nullptr);
    if (Node)
    {
        TSharedPtr<FJsonObject> NodeSummary = MakeShared<FJsonObject>();
        NodeSummary->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        NodeSummary->SetStringField(TEXT("nodeName"), Node->GetName());
        NodeSummary->SetStringField(TEXT("nodePath"), Node->GetPathName());
        NodeSummary->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
        Details->SetObjectField(TEXT("matchedNode"), NodeSummary);

        TArray<TSharedPtr<FJsonValue>> CandidatePins;
        for (const UEdGraphPin* CandidatePin : Node->Pins)
        {
            if (!CandidatePin)
            {
                continue;
            }

            TSharedPtr<FJsonObject> PinSummary = MakeShared<FJsonObject>();
            PinSummary->SetStringField(TEXT("pinName"), CandidatePin->PinName.ToString());
            PinSummary->SetStringField(TEXT("direction"), LoomleBlueprintAdapterInternal::PinDirectionToString(CandidatePin->Direction));
            PinSummary->SetStringField(TEXT("category"), CandidatePin->PinType.PinCategory.ToString());
            PinSummary->SetStringField(TEXT("subCategory"), CandidatePin->PinType.PinSubCategory.ToString());
            PinSummary->SetStringField(TEXT("defaultValue"), CandidatePin->DefaultValue);
            CandidatePins.Add(MakeShared<FJsonValueObject>(PinSummary));
        }
        Details->SetArrayField(TEXT("candidatePins"), CandidatePins);
    }
    else
    {
        Details->SetArrayField(TEXT("candidatePins"), TArray<TSharedPtr<FJsonValue>>{});
    }

    OutDetailsJson = LoomleBlueprintAdapterInternal::JsonObjectToString(Details);
    return true;
}

bool FLoomleBlueprintAdapter::ListEventGraphNodes(const FString& BlueprintAssetPath, FString& OutNodesJson, FString& OutError)
{
    return ListGraphNodes(BlueprintAssetPath, TEXT("EventGraph"), OutNodesJson, OutError);
}

bool FLoomleBlueprintAdapter::ListBlueprintGraphs(const FString& BlueprintAssetPath, FString& OutGraphsJson, FString& OutError)
{
    OutGraphsJson = TEXT("[]");
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (!Blueprint)
    {
        OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintAssetPath);
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> Graphs;
    LoomleBlueprintAdapterInternal::AppendGraphListEntries(Blueprint->UbergraphPages, TEXT("Event"), Graphs);
    LoomleBlueprintAdapterInternal::AppendGraphListEntries(Blueprint->FunctionGraphs, TEXT("Function"), Graphs);
    LoomleBlueprintAdapterInternal::AppendGraphListEntries(Blueprint->MacroGraphs, TEXT("Macro"), Graphs);
    for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
    {
        LoomleBlueprintAdapterInternal::AppendGraphListEntries(InterfaceDesc.Graphs, TEXT("Interface"), Graphs);
    }

    OutGraphsJson = LoomleBlueprintAdapterInternal::JsonArrayToString(Graphs);
    return true;
}

bool FLoomleBlueprintAdapter::ResolveGraphIdByName(const FString& BlueprintAssetPath, const FString& GraphName, FString& OutGraphId, FString& OutError)
{
    OutGraphId.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (!Blueprint)
    {
        OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintAssetPath);
        return false;
    }

    UEdGraph* Graph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Graph)
    {
        OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
        return false;
    }

    OutGraphId = Graph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
    return true;
}

bool FLoomleBlueprintAdapter::ResolveGraphNameById(const FString& BlueprintAssetPath, const FString& GraphId, FString& OutGraphName, FString& OutError)
{
    OutGraphName.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (!Blueprint)
    {
        OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintAssetPath);
        return false;
    }

    UEdGraph* Graph = LoomleBlueprintAdapterInternal::FindGraphById(Blueprint, GraphId);
    if (!Graph)
    {
        OutError = FString::Printf(TEXT("Graph not found by id: %s"), *GraphId);
        return false;
    }

    OutGraphName = Graph->GetName();
    return true;
}

bool FLoomleBlueprintAdapter::ListGraphNodes(
    const FString& BlueprintAssetPath,
    const FString& GraphName,
    FString& OutNodesJson,
    FString& OutError,
    const FLoomleBlueprintNodeListOptions* Options,
    FLoomleBlueprintNodeListStats* OutStats)
{
    OutNodesJson = TEXT("[]");
    OutError.Empty();
    if (OutStats)
    {
        *OutStats = FLoomleBlueprintNodeListStats{};
    }

    if (GraphName.IsEmpty())
    {
        OutError = TEXT("GraphName is required.");
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* Graph = LoomleBlueprintAdapterInternal::FindGraphByName(Blueprint, GraphName);
    if (!Blueprint || !Graph)
    {
        OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
        return false;
    }

    LoomleBlueprintAdapterInternal::SerializeFilteredNodeList(
        Graph->Nodes,
        Graph->GetName(),
        Options,
        OutNodesJson,
        OutStats);
    return true;
}

bool FLoomleBlueprintAdapter::ListCompositeSubgraphNodes(
    const FString& BlueprintAssetPath,
    const FString& CompositeNodeGuid,
    FString& OutSubgraphName,
    FString& OutNodesJson,
    FString& OutError,
    const FLoomleBlueprintNodeListOptions* Options,
    FLoomleBlueprintNodeListStats* OutStats)
{
    OutSubgraphName.Empty();
    OutNodesJson = TEXT("[]");
    OutError.Empty();
    if (OutStats)
    {
        *OutStats = FLoomleBlueprintNodeListStats{};
    }

    if (CompositeNodeGuid.IsEmpty())
    {
        OutError = TEXT("CompositeNodeGuid is required.");
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (!Blueprint)
    {
        OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintAssetPath);
        return false;
    }

    UEdGraph* ParentGraph = nullptr;
    UEdGraphNode* OwnerNode = nullptr;
    UEdGraph* SubGraph = nullptr;
    if (!LoomleBlueprintAdapterInternal::ResolveCompositeSubgraphByNodeToken(
            Blueprint,
            CompositeNodeGuid,
            ParentGraph,
            OwnerNode,
            SubGraph,
            OutError))
    {
        return false;
    }
    (void)ParentGraph;
    (void)OwnerNode;

    if (SubGraph == nullptr)
    {
        OutError = FString::Printf(TEXT("Node %s BoundGraph is null."), *CompositeNodeGuid);
        return false;
    }

    OutSubgraphName = SubGraph->GetName();
    LoomleBlueprintAdapterInternal::SerializeFilteredNodeList(
        SubGraph->Nodes,
        OutSubgraphName,
        Options,
        OutNodesJson,
        OutStats);
    return true;
}

bool FLoomleBlueprintAdapter::ListCompositeSubgraphs(
    const FString& BlueprintAssetPath,
    FString& OutGraphsJson,
    FString& OutError)
{
    OutGraphsJson = TEXT("[]");
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (!Blueprint)
    {
        OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintAssetPath);
        return false;
    }

    TArray<UEdGraph*> RootGraphs;
    LoomleBlueprintAdapterInternal::CollectRootGraphs(Blueprint, RootGraphs);

    TArray<TSharedPtr<FJsonValue>> Graphs;
    for (UEdGraph* RootGraph : RootGraphs)
    {
        if (RootGraph == nullptr)
        {
            continue;
        }

        for (UEdGraphNode* Node : RootGraph->Nodes)
        {
            if (Node == nullptr)
            {
                continue;
            }

            UEdGraph* SubGraph = LoomleBlueprintAdapterInternal::ResolveCompositeBoundGraph(Node);
            if (SubGraph == nullptr)
            {
                continue;
            }

            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            const FString GraphId = SubGraph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
            Entry->SetStringField(TEXT("id"), GraphId);
            Entry->SetStringField(TEXT("graphId"), GraphId);
            Entry->SetStringField(TEXT("graphName"), SubGraph->GetName());
            Entry->SetStringField(TEXT("graphKind"), TEXT("subgraph"));
            Entry->SetStringField(TEXT("graphClassPath"), SubGraph->GetClass() ? SubGraph->GetClass()->GetPathName() : TEXT(""));
            Entry->SetStringField(TEXT("parentGraphName"), RootGraph->GetName());
            Entry->SetStringField(TEXT("ownerNodeId"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
            Graphs.Add(MakeShared<FJsonValueObject>(Entry));
        }
    }

    OutGraphsJson = LoomleBlueprintAdapterInternal::JsonArrayToString(Graphs);
    return true;
}

bool FLoomleBlueprintAdapter::GetNodeDetails(const FString& BlueprintAssetPath, const FString& NodeGuid, FString& OutNodeJson, FString& OutError)
{
    OutNodeJson = TEXT("{}");
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::GetEventGraph(Blueprint);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/event graph.");
        return false;
    }

    UEdGraphNode* Node = LoomleBlueprintAdapterInternal::FindNodeByGuid(EventGraph, NodeGuid);
    if (!Node)
    {
        OutError = FString::Printf(TEXT("Node not found by guid: %s"), *NodeGuid);
        return false;
    }

    OutNodeJson = LoomleBlueprintAdapterInternal::JsonObjectToString(LoomleBlueprintAdapterInternal::SerializeNode(Node));
    return true;
}

bool FLoomleBlueprintAdapter::FindNodesByClass(const FString& BlueprintAssetPath, const FString& NodeClassPathOrName, FString& OutNodesJson, FString& OutError)
{
    OutNodesJson = TEXT("[]");
    OutError.Empty();

    if (NodeClassPathOrName.IsEmpty())
    {
        OutError = TEXT("NodeClassPathOrName is empty.");
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::GetEventGraph(Blueprint);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/event graph.");
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> Nodes;
    for (const UEdGraphNode* Node : EventGraph->Nodes)
    {
        if (LoomleBlueprintAdapterInternal::NodeClassMatches(Node, NodeClassPathOrName))
        {
            Nodes.Add(MakeShared<FJsonValueObject>(LoomleBlueprintAdapterInternal::SerializeNode(Node)));
        }
    }

    OutNodesJson = LoomleBlueprintAdapterInternal::JsonArrayToString(Nodes);
    return true;
}

bool FLoomleBlueprintAdapter::CompileBlueprint(const FString& BlueprintAssetPath, const FString& GraphName, FString& OutError)
{
    OutError.Empty();
    (void)GraphName;

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (!Blueprint)
    {
        OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintAssetPath);
        return false;
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    Blueprint->MarkPackageDirty();

    if (Blueprint->Status == BS_Error)
    {
        OutError = TEXT("Compile failed. Check UE compiler log.");
        return false;
    }

    return true;
}

bool FLoomleBlueprintAdapter::SpawnBlueprintActor(const FString& BlueprintAssetPath, FVector Location, FRotator Rotation, FString& OutActorPath, FString& OutError)
{
    OutActorPath.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (!Blueprint || !Blueprint->GeneratedClass)
    {
        OutError = TEXT("Blueprint or GeneratedClass not found.");
        return false;
    }

    if (!GEditor)
    {
        OutError = TEXT("GEditor is not available.");
        return false;
    }

    UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
    if (!EditorWorld)
    {
        OutError = TEXT("Editor world not found.");
        return false;
    }

    AActor* Spawned = EditorWorld->SpawnActor<AActor>(Blueprint->GeneratedClass, Location, Rotation);
    if (!Spawned)
    {
        OutError = TEXT("SpawnActor failed.");
        return false;
    }

    OutActorPath = Spawned->GetPathName();
    return true;
}

bool FLoomleBlueprintAdapter::SearchBlueprintPalette(
    const FString& AssetPath,
    const FString& Query,
    const FString& Family,
    int32 Limit,
    int32 Offset,
    FString& OutJson,
    FString& OutError)
{
    using namespace LoomleBlueprintAdapterInternal;

    OutJson.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
    if (!Blueprint)
    {
        OutError = TEXT("ASSET_NOT_FOUND");
        return false;
    }

    UClass* BlueprintClass = Blueprint->GeneratedClass ? Blueprint->GeneratedClass : Blueprint->SkeletonGeneratedClass;

    // Helpers ----------------------------------------------------------------

    // Lower-case + strip spaces for fuzzy matching
    auto Normalize = [](const FString& In) -> FString
    {
        return In.ToLower().Replace(TEXT(" "), TEXT("")).Replace(TEXT("_"), TEXT(""));
    };

    // Query words for multi-token match
    TArray<FString> QueryWords;
    if (!Query.IsEmpty())
    {
        FString NormQuery = Normalize(Query);
        NormQuery.ParseIntoArray(QueryWords, TEXT(" "), true);
        // Also split the non-normalized query for display matching
        // Re-normalize each word
        for (FString& Word : QueryWords)
        {
            Word = Normalize(Word);
        }
        if (QueryWords.Num() == 0)
        {
            QueryWords.Add(NormQuery);
        }
    }

    auto MatchesQuery = [&](const FString& Title, const FString& ExtraKeywords) -> bool
    {
        if (QueryWords.Num() == 0)
        {
            return true;
        }
        FString Haystack = Normalize(Title) + Normalize(ExtraKeywords);
        for (const FString& Word : QueryWords)
        {
            if (!Haystack.Contains(Word))
            {
                return false;
            }
        }
        return true;
    };

    bool bFilterFamily = !Family.IsEmpty();

    // Collect all entries ---------------------------------------------------
    TArray<TSharedPtr<FJsonObject>> Entries;

    // --- Variables (non-component properties) ---
    if (!bFilterFamily || Family.Equals(TEXT("variable")))
    {
        TSet<FString> Seen;
        for (UClass* Class = BlueprintClass; Class != nullptr; Class = Class->GetSuperClass())
        {
            for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
            {
                FProperty* Prop = *It;
                if (Prop == nullptr)
                {
                    continue;
                }

                const FString VarName = Prop->GetName();
                if (Seen.Contains(VarName))
                {
                    continue;
                }

                // Skip component properties (they'll appear under "component")
                if (Prop->IsA<FObjectProperty>())
                {
                    FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop);
                    if (ObjProp && ObjProp->PropertyClass && ObjProp->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
                    {
                        continue;
                    }
                }

                Seen.Add(VarName);

                FString DisplayName = FName::NameToDisplayString(VarName, false);
                FString Keywords = VarName;

                if (!MatchesQuery(DisplayName, Keywords))
                {
                    continue;
                }

                TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("title"), FString::Printf(TEXT("Get %s"), *DisplayName));
                Entry->SetStringField(TEXT("family"), TEXT("variable"));
                Entry->SetStringField(TEXT("variableName"), VarName);

                // addNode for Get
                TSharedPtr<FJsonObject> AddNodeGet = MakeShared<FJsonObject>();
                AddNodeGet->SetStringField(TEXT("kind"), TEXT("addNode"));
                TSharedPtr<FJsonObject> NodeTypeGet = MakeShared<FJsonObject>();
                NodeTypeGet->SetStringField(TEXT("type"), TEXT("byvariable"));
                NodeTypeGet->SetStringField(TEXT("variableName"), VarName);
                NodeTypeGet->SetStringField(TEXT("mode"), TEXT("get"));
                AddNodeGet->SetObjectField(TEXT("nodeType"), NodeTypeGet);

                // addNode for Set
                TSharedPtr<FJsonObject> AddNodeSet = MakeShared<FJsonObject>();
                AddNodeSet->SetStringField(TEXT("kind"), TEXT("addNode"));
                TSharedPtr<FJsonObject> NodeTypeSet = MakeShared<FJsonObject>();
                NodeTypeSet->SetStringField(TEXT("type"), TEXT("byvariable"));
                NodeTypeSet->SetStringField(TEXT("variableName"), VarName);
                NodeTypeSet->SetStringField(TEXT("mode"), TEXT("set"));
                AddNodeSet->SetObjectField(TEXT("nodeType"), NodeTypeSet);

                TArray<TSharedPtr<FJsonValue>> AddNodes;
                AddNodes.Add(MakeShared<FJsonValueObject>(AddNodeGet));
                AddNodes.Add(MakeShared<FJsonValueObject>(AddNodeSet));
                Entry->SetArrayField(TEXT("addNodes"), AddNodes);
                Entry->SetObjectField(TEXT("addNode"), AddNodeGet);

                Entries.Add(Entry);
            }
        }
    }

    // --- Components (SCS variables that are UActorComponent subclasses) ---
    if (!bFilterFamily || Family.Equals(TEXT("component")))
    {
        if (Blueprint->SimpleConstructionScript != nullptr)
        {
            for (USCS_Node* ScsNode : Blueprint->SimpleConstructionScript->GetAllNodes())
            {
                if (ScsNode == nullptr)
                {
                    continue;
                }

                const FString VarName = ScsNode->GetVariableName().ToString();
                FString DisplayName = FName::NameToDisplayString(VarName, false);
                FString Keywords = VarName;

                if (!MatchesQuery(DisplayName, Keywords))
                {
                    continue;
                }

                TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("title"), FString::Printf(TEXT("Get %s"), *DisplayName));
                Entry->SetStringField(TEXT("family"), TEXT("component"));
                Entry->SetStringField(TEXT("variableName"), VarName);

                UBlueprintGeneratedClass* BGC = Cast<UBlueprintGeneratedClass>(BlueprintClass);
                UActorComponent* Template = BGC ? ScsNode->GetActualComponentTemplate(BGC) : nullptr;
                if (Template && Template->GetClass())
                {
                    Entry->SetStringField(TEXT("componentClassPath"), Template->GetClass()->GetPathName());
                }

                // addNode for Get (components are read-only, so only Get)
                TSharedPtr<FJsonObject> AddNodeGet = MakeShared<FJsonObject>();
                AddNodeGet->SetStringField(TEXT("kind"), TEXT("addNode"));
                TSharedPtr<FJsonObject> NodeTypeGet = MakeShared<FJsonObject>();
                NodeTypeGet->SetStringField(TEXT("type"), TEXT("byvariable"));
                NodeTypeGet->SetStringField(TEXT("variableName"), VarName);
                NodeTypeGet->SetStringField(TEXT("mode"), TEXT("get"));
                AddNodeGet->SetObjectField(TEXT("nodeType"), NodeTypeGet);

                TArray<TSharedPtr<FJsonValue>> AddNodes;
                AddNodes.Add(MakeShared<FJsonValueObject>(AddNodeGet));
                Entry->SetArrayField(TEXT("addNodes"), AddNodes);
                Entry->SetObjectField(TEXT("addNode"), AddNodeGet);

                Entries.Add(Entry);
            }
        }
    }

    // --- Functions ---
    if (!bFilterFamily || Family.Equals(TEXT("function")))
    {
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (Graph == nullptr)
            {
                continue;
            }

            const FString GraphName = Graph->GetName();
            FString DisplayName = FName::NameToDisplayString(GraphName, false);

            if (!MatchesQuery(DisplayName, GraphName))
            {
                continue;
            }

            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("title"), FString::Printf(TEXT("Call %s"), *DisplayName));
            Entry->SetStringField(TEXT("family"), TEXT("function"));
            Entry->SetStringField(TEXT("graphName"), GraphName);

            TSharedPtr<FJsonObject> AddNode = MakeShared<FJsonObject>();
            AddNode->SetStringField(TEXT("kind"), TEXT("addNode"));
            TSharedPtr<FJsonObject> NodeType = MakeShared<FJsonObject>();
            NodeType->SetStringField(TEXT("type"), TEXT("bycallfunction"));
            NodeType->SetStringField(TEXT("functionClassPath"), Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetPathName() : TEXT(""));
            NodeType->SetStringField(TEXT("functionName"), GraphName);
            AddNode->SetObjectField(TEXT("nodeType"), NodeType);

            TArray<TSharedPtr<FJsonValue>> AddNodes;
            AddNodes.Add(MakeShared<FJsonValueObject>(AddNode));
            Entry->SetArrayField(TEXT("addNodes"), AddNodes);
            Entry->SetObjectField(TEXT("addNode"), AddNode);

            Entries.Add(Entry);
        }
    }

    // --- Macros ---
    if (!bFilterFamily || Family.Equals(TEXT("macro")))
    {
        for (UEdGraph* Graph : Blueprint->MacroGraphs)
        {
            if (Graph == nullptr)
            {
                continue;
            }

            const FString GraphName = Graph->GetName();
            FString DisplayName = FName::NameToDisplayString(GraphName, false);

            if (!MatchesQuery(DisplayName, GraphName))
            {
                continue;
            }

            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("title"), DisplayName);
            Entry->SetStringField(TEXT("family"), TEXT("macro"));
            Entry->SetStringField(TEXT("graphName"), GraphName);

            TSharedPtr<FJsonObject> AddNode = MakeShared<FJsonObject>();
            AddNode->SetStringField(TEXT("kind"), TEXT("addNode"));
            TSharedPtr<FJsonObject> NodeType = MakeShared<FJsonObject>();
            NodeType->SetStringField(TEXT("type"), TEXT("bymacro"));
            NodeType->SetStringField(TEXT("macroLibraryAssetPath"), AssetPath);
            NodeType->SetStringField(TEXT("macroGraphName"), GraphName);
            AddNode->SetObjectField(TEXT("nodeType"), NodeType);

            TArray<TSharedPtr<FJsonValue>> AddNodes;
            AddNodes.Add(MakeShared<FJsonValueObject>(AddNode));
            Entry->SetArrayField(TEXT("addNodes"), AddNodes);
            Entry->SetObjectField(TEXT("addNode"), AddNode);

            Entries.Add(Entry);
        }
    }

    // --- Dispatchers (Event Dispatchers) ---
    if (!bFilterFamily || Family.Equals(TEXT("dispatcher")))
    {
        for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
        {
            if (Graph == nullptr)
            {
                continue;
            }

            const FString GraphName = Graph->GetName();
            FString DisplayName = FName::NameToDisplayString(GraphName, false);

            if (!MatchesQuery(DisplayName, GraphName))
            {
                continue;
            }

            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("title"), FString::Printf(TEXT("Call %s"), *DisplayName));
            Entry->SetStringField(TEXT("family"), TEXT("dispatcher"));
            Entry->SetStringField(TEXT("graphName"), GraphName);

            // Dispatchers: Call, Bind, Unbind, Assign (caller side)
            TSharedPtr<FJsonObject> AddNode = MakeShared<FJsonObject>();
            AddNode->SetStringField(TEXT("kind"), TEXT("addNode"));
            TSharedPtr<FJsonObject> NodeType = MakeShared<FJsonObject>();
            NodeType->SetStringField(TEXT("type"), TEXT("bycallfunction"));
            NodeType->SetStringField(TEXT("functionClassPath"), Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetPathName() : TEXT(""));
            NodeType->SetStringField(TEXT("functionName"), GraphName);
            AddNode->SetObjectField(TEXT("nodeType"), NodeType);

            TArray<TSharedPtr<FJsonValue>> AddNodes;
            AddNodes.Add(MakeShared<FJsonValueObject>(AddNode));
            Entry->SetArrayField(TEXT("addNodes"), AddNodes);
            Entry->SetObjectField(TEXT("addNode"), AddNode);

            Entries.Add(Entry);
        }
    }

    // --- Events (Custom Events from event graphs) ---
    if (!bFilterFamily || Family.Equals(TEXT("event")))
    {
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

                // Only custom events (K2Node_CustomEvent) — non-custom events are engine-side
                const UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node);
                if (CustomEvent == nullptr)
                {
                    continue;
                }

                const FString EventName = CustomEvent->CustomFunctionName.ToString();
                FString DisplayName = FName::NameToDisplayString(EventName, false);

                if (!MatchesQuery(DisplayName, EventName))
                {
                    continue;
                }

                TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("title"), FString::Printf(TEXT("Call %s (Custom Event)"), *DisplayName));
                Entry->SetStringField(TEXT("family"), TEXT("event"));
                Entry->SetStringField(TEXT("eventName"), EventName);
                Entry->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));

                TSharedPtr<FJsonObject> AddNode = MakeShared<FJsonObject>();
                AddNode->SetStringField(TEXT("kind"), TEXT("addNode"));
                TSharedPtr<FJsonObject> NodeType = MakeShared<FJsonObject>();
                NodeType->SetStringField(TEXT("type"), TEXT("bycustomevent"));
                NodeType->SetStringField(TEXT("eventName"), EventName);
                AddNode->SetObjectField(TEXT("nodeType"), NodeType);

                TArray<TSharedPtr<FJsonValue>> AddNodes;
                AddNodes.Add(MakeShared<FJsonValueObject>(AddNode));
                Entry->SetArrayField(TEXT("addNodes"), AddNodes);
                Entry->SetObjectField(TEXT("addNode"), AddNode);

                Entries.Add(Entry);
            }
        }
    }

    // --- Utility nodes (static table) ---
    if (!bFilterFamily || Family.Equals(TEXT("utility")))
    {
        struct FUtilityNodeDef
        {
            const TCHAR* Title;
            const TCHAR* Keywords;
            const TCHAR* NodeClassPath;
        };

        static const FUtilityNodeDef UtilityNodes[] = {
            { TEXT("Branch"), TEXT("if condition branch bool"), TEXT("K2Node_IfThenElse") },
            { TEXT("Sequence"), TEXT("sequence multi exec then"), TEXT("K2Node_ExecutionSequence") },
            { TEXT("Reroute"), TEXT("knot reroute redirect wire"), TEXT("K2Node_Knot") },
            { TEXT("Comment"), TEXT("comment note annotation box"), TEXT("EdGraphNode_Comment") },
        };

        for (const FUtilityNodeDef& Def : UtilityNodes)
        {
            FString Title = Def.Title;
            FString Keywords = Def.Keywords;

            if (!MatchesQuery(Title, Keywords))
            {
                continue;
            }

            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("title"), Title);
            Entry->SetStringField(TEXT("family"), TEXT("utility"));

            TSharedPtr<FJsonObject> AddNode = MakeShared<FJsonObject>();
            AddNode->SetStringField(TEXT("kind"), TEXT("addNode"));
            TSharedPtr<FJsonObject> NodeType = MakeShared<FJsonObject>();
            NodeType->SetStringField(TEXT("type"), TEXT("byclass"));
            NodeType->SetStringField(TEXT("nodeClassPath"), FString(Def.NodeClassPath));
            AddNode->SetObjectField(TEXT("nodeType"), NodeType);

            TArray<TSharedPtr<FJsonValue>> AddNodes;
            AddNodes.Add(MakeShared<FJsonValueObject>(AddNode));
            Entry->SetArrayField(TEXT("addNodes"), AddNodes);
            Entry->SetObjectField(TEXT("addNode"), AddNode);

            Entries.Add(Entry);
        }
    }

    // Paginate ---------------------------------------------------------------
    const int32 TotalMatching = Entries.Num();
    const int32 EffectiveOffset = FMath::Clamp(Offset, 0, TotalMatching);
    const int32 EffectiveLimit = Limit > 0 ? Limit : TotalMatching;

    TArray<TSharedPtr<FJsonValue>> PageEntries;
    for (int32 Idx = EffectiveOffset; Idx < TotalMatching && PageEntries.Num() < EffectiveLimit; ++Idx)
    {
        PageEntries.Add(MakeShared<FJsonValueObject>(Entries[Idx]));
    }

    // Serialize --------------------------------------------------------------
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("total"), TotalMatching);
    Root->SetNumberField(TEXT("offset"), EffectiveOffset);
    Root->SetArrayField(TEXT("entries"), PageEntries);

    TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

    return true;
}
