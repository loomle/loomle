// Copyright 2026 Loomle contributors.

#include "SalReferenceIndex.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Blueprint/BlueprintSupport.h"
#include "CoreGlobals.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "FindInBlueprintManager.h"
#include "Misc/PackageName.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/EditorObjectVersion.h"

namespace Loomle::Sal
{
namespace ReferenceIndexPrivate
{
constexpr int32 MaxEncodedCharacters = 512 * 1024;

FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

bool ParseGuidText(const FString& Text, FGuid& OutGuid)
{
    return (FGuid::ParseExact(Text, EGuidFormats::Digits, OutGuid)
            || FGuid::Parse(Text, OutGuid))
        && OutGuid.IsValid();
}

FString BlueprintPath(const FAssetData& Data)
{
    FString WithinPackage;
    if (!Data.GetTagValue(FBlueprintTags::BlueprintPathWithinPackage, WithinPackage)
        || WithinPackage.IsEmpty())
    {
        return Data.GetSoftObjectPath().ToString();
    }
    if (WithinPackage.StartsWith(TEXT("/")))
    {
        return WithinPackage;
    }
    const FString ContainerPath = Data.GetSoftObjectPath().ToString();
    const FString AssetName = Data.AssetName.ToString();
    if (WithinPackage == AssetName)
    {
        return ContainerPath;
    }
    if (WithinPackage.StartsWith(AssetName + TEXT(".")))
    {
        return ContainerPath + TEXT(":") + WithinPackage.Mid(AssetName.Len() + 1);
    }
    if (WithinPackage.StartsWith(AssetName + TEXT(":")))
    {
        return Data.PackageName.ToString() + TEXT(".") + WithinPackage;
    }
    return ContainerPath + TEXT(":") + WithinPackage;
}

FTopLevelAssetPath ClassPathFromExportText(const FString& Text)
{
    if (Text.IsEmpty() || Text == TEXT("None"))
    {
        return FTopLevelAssetPath();
    }
    return FTopLevelAssetPath(FPackageName::ExportTextPathToObjectPath(Text));
}

FTopLevelAssetPath GeneratedClassPath(const FAssetData& Data)
{
    FString Text;
    return Data.GetTagValue(FBlueprintTags::GeneratedClassPath, Text)
        ? ClassPathFromExportText(Text)
        : FTopLevelAssetPath();
}

bool DecodeLookupToken(
    const FString& Token,
    const TMap<int32, FText>& Lookup,
    FString& OutText)
{
    int32 Index = INDEX_NONE;
    const FText* Text = LexTryParseString(Index, *Token) ? Lookup.Find(Index) : nullptr;
    if (Text == nullptr)
    {
        return false;
    }
    OutText = Text->BuildSourceString();
    return true;
}

bool LookupRequiresAssetResolution(const TMap<int32, FText>& Lookup)
{
    for (const TPair<int32, FText>& Pair : Lookup)
    {
        if (Pair.Value.IsFromStringTable())
        {
            return true;
        }
    }
    return false;
}

TSharedPtr<FJsonValue> FindDirectField(
    const TSharedPtr<FJsonObject>& Object,
    const TMap<int32, FText>& Lookup,
    const FString& Field)
{
    if (!Object.IsValid())
    {
        return nullptr;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
    {
        FString Key;
        if (DecodeLookupToken(Pair.Key, Lookup, Key) && Key == Field)
        {
            return Pair.Value;
        }
    }
    return nullptr;
}

bool FindScalarRecursive(
    const TSharedPtr<FJsonValue>& Value,
    const TMap<int32, FText>& Lookup,
    const FString& Field,
    TSharedPtr<FJsonValue>& OutValue)
{
    if (!Value.IsValid())
    {
        return false;
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (Value->TryGetObject(Object) && Object != nullptr && (*Object).IsValid())
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Object)->Values)
        {
            FString Key;
            const bool bField = DecodeLookupToken(Pair.Key, Lookup, Key) && Key == Field;
            if (bField && Pair.Value.IsValid() && Pair.Value->Type != EJson::Object && Pair.Value->Type != EJson::Array)
            {
                OutValue = Pair.Value;
                return true;
            }
            if (FindScalarRecursive(Pair.Value, Lookup, Field, OutValue))
            {
                return true;
            }
        }
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    if (Value->TryGetArray(Array) && Array != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Item : *Array)
        {
            if (FindScalarRecursive(Item, Lookup, Field, OutValue))
            {
                return true;
            }
        }
    }
    return false;
}

bool DecodedString(const TSharedPtr<FJsonValue>& Value, const TMap<int32, FText>& Lookup, FString& Out)
{
    FString Token;
    return Value.IsValid() && Value->TryGetString(Token) && DecodeLookupToken(Token, Lookup, Out);
}

bool DirectString(
    const TSharedPtr<FJsonObject>& Object,
    const TMap<int32, FText>& Lookup,
    const FString& Field,
    FString& Out)
{
    return DecodedString(FindDirectField(Object, Lookup, Field), Lookup, Out);
}

bool RecursiveString(
    const TSharedPtr<FJsonValue>& Root,
    const TMap<int32, FText>& Lookup,
    const FString& Field,
    FString& Out)
{
    TSharedPtr<FJsonValue> Value;
    return FindScalarRecursive(Root, Lookup, Field, Value) && DecodedString(Value, Lookup, Out);
}

bool RecursiveBool(
    const TSharedPtr<FJsonValue>& Root,
    const TMap<int32, FText>& Lookup,
    const FString& Field,
    bool& Out)
{
    TSharedPtr<FJsonValue> Value;
    return FindScalarRecursive(Root, Lookup, Field, Value) && Value.IsValid() && Value->TryGetBool(Out);
}

bool RecursiveUint32(
    const TSharedPtr<FJsonValue>& Root,
    const TMap<int32, FText>& Lookup,
    const FString& Field,
    uint32& Out)
{
    TSharedPtr<FJsonValue> Value;
    double Number = 0.0;
    if (!FindScalarRecursive(Root, Lookup, Field, Value)
        || !Value.IsValid()
        || !Value->TryGetNumber(Number)
        || Number < 0.0
        || Number > static_cast<double>(MAX_uint32)
        || FMath::FloorToDouble(Number) != Number)
    {
        return false;
    }
    Out = static_cast<uint32>(Number);
    return true;
}

bool MemberGuid(
    const TSharedPtr<FJsonValue>& VariableReference,
    const TMap<int32, FText>& Lookup,
    FGuid& OutGuid)
{
    uint32 A = 0;
    uint32 B = 0;
    uint32 C = 0;
    uint32 D = 0;
    if (!RecursiveUint32(VariableReference, Lookup, TEXT("A"), A)
        || !RecursiveUint32(VariableReference, Lookup, TEXT("B"), B)
        || !RecursiveUint32(VariableReference, Lookup, TEXT("C"), C)
        || !RecursiveUint32(VariableReference, Lookup, TEXT("D"), D))
    {
        return false;
    }
    OutGuid = FGuid(A, B, C, D);
    return true;
}

bool IsIndexedVariableTarget(const FCanonicalReference& Identity)
{
    switch (Identity.Kind)
    {
    case EReferenceDeclarationKind::BlueprintMemberVariable:
    case EReferenceDeclarationKind::LocalVariable:
    case EReferenceDeclarationKind::NativeProperty:
        return true;
    default:
        return false;
    }
}

enum class EOwnerMatch : uint8
{
    Match,
    NoMatch,
    Unknown
};

EOwnerMatch MatchClassLineage(
    IAssetRegistry& Registry,
    const FTopLevelAssetPath& Candidate,
    const FTopLevelAssetPath& Target)
{
    if (Candidate.IsNull() || Target.IsNull())
    {
        return EOwnerMatch::Unknown;
    }
    if (Candidate == Target)
    {
        return EOwnerMatch::Match;
    }
    TArray<FTopLevelAssetPath> Ancestors;
    if (!Registry.GetAncestorClassNames(Candidate, Ancestors))
    {
        return EOwnerMatch::Unknown;
    }
    return Ancestors.Contains(Target) ? EOwnerMatch::Match : EOwnerMatch::NoMatch;
}

EOwnerMatch MatchOwner(
    const FString& CandidateBlueprintPath,
    const TSet<FTopLevelAssetPath>& CandidateClassLineage,
    const bool bCandidateClassLineageComplete,
    IAssetRegistry* Registry,
    const TSharedPtr<FJsonValue>& VariableReference,
    const TMap<int32, FText>& Lookup,
    const FReferenceIndexTarget& Target)
{
    FString Scope;
    if (!RecursiveString(VariableReference, Lookup, TEXT("MemberScope"), Scope))
    {
        return EOwnerMatch::Unknown;
    }
    if (!Scope.IsEmpty())
    {
        if (Target.Identity.Kind != EReferenceDeclarationKind::LocalVariable)
        {
            return EOwnerMatch::NoMatch;
        }
        if (Target.ScopeName.IsEmpty())
        {
            return EOwnerMatch::Unknown;
        }
        return CandidateBlueprintPath == Target.Identity.OwnerPath
                && FName(*Scope) == FName(*Target.ScopeName)
            ? EOwnerMatch::Match
            : EOwnerMatch::NoMatch;
    }
    if (Target.Identity.Kind == EReferenceDeclarationKind::LocalVariable)
    {
        return EOwnerMatch::NoMatch;
    }

    bool bSelfContext = false;
    if (!RecursiveBool(VariableReference, Lookup, TEXT("bSelfContext"), bSelfContext))
    {
        return EOwnerMatch::Unknown;
    }
    if (bSelfContext)
    {
        if (CandidateBlueprintPath == Target.Identity.OwnerPath)
        {
            return EOwnerMatch::Match;
        }
        if (Target.OwnerClassPath.IsNull())
        {
            return EOwnerMatch::Unknown;
        }
        if (CandidateClassLineage.Contains(Target.OwnerClassPath))
        {
            return EOwnerMatch::Match;
        }
        return bCandidateClassLineageComplete ? EOwnerMatch::NoMatch : EOwnerMatch::Unknown;
    }

    FString MemberParent;
    if (!RecursiveString(VariableReference, Lookup, TEXT("MemberParent"), MemberParent))
    {
        return EOwnerMatch::Unknown;
    }
    const FTopLevelAssetPath ParentClass = ClassPathFromExportText(MemberParent);
    if (ParentClass.IsNull() || Target.OwnerClassPath.IsNull())
    {
        return EOwnerMatch::Unknown;
    }
    if (Registry == nullptr)
    {
        return ParentClass == Target.OwnerClassPath ? EOwnerMatch::Match : EOwnerMatch::NoMatch;
    }
    return MatchClassLineage(*Registry, ParentClass, Target.OwnerClassPath);
}

bool CollectLocatorCounts(
    const TSharedPtr<FJsonObject>& Root,
    const TMap<int32, FText>& Lookup,
    const TSet<FString>& GraphCategories,
    TMap<FString, int32>& OutNodeIds,
    TFunctionRef<bool()> IsCancelled,
    bool& bOutCancelled)
{
    bOutCancelled = false;
    for (const TPair<FString, TSharedPtr<FJsonValue>>& RootPair : Root->Values)
    {
        if (IsCancelled())
        {
            bOutCancelled = true;
            return false;
        }
        FString Category;
        const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
        if (!DecodeLookupToken(RootPair.Key, Lookup, Category)
            || !GraphCategories.Contains(Category)
            || !RootPair.Value.IsValid()
            || !RootPair.Value->TryGetArray(Graphs)
            || Graphs == nullptr)
        {
            continue;
        }
        for (const TSharedPtr<FJsonValue>& GraphValue : *Graphs)
        {
            if (IsCancelled())
            {
                bOutCancelled = true;
                return false;
            }
            const TSharedPtr<FJsonObject>* Graph = nullptr;
            if (!GraphValue.IsValid() || !GraphValue->TryGetObject(Graph) || Graph == nullptr || !(*Graph).IsValid())
            {
                return false;
            }
            const TSharedPtr<FJsonValue> NodesValue =
                FindDirectField(*Graph, Lookup, FFindInBlueprintSearchTags::FiB_Nodes.BuildSourceString());
            const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
            if (!NodesValue.IsValid() || !NodesValue->TryGetArray(Nodes) || Nodes == nullptr)
            {
                continue;
            }
            for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
            {
                if (IsCancelled())
                {
                    bOutCancelled = true;
                    return false;
                }
                const TSharedPtr<FJsonObject>* Node = nullptr;
                FString NodeGuid;
                FGuid ParsedNodeGuid;
                if (NodeValue.IsValid()
                    && NodeValue->TryGetObject(Node)
                    && Node != nullptr
                    && (*Node).IsValid()
                    && DirectString(*Node, Lookup, FFindInBlueprintSearchTags::FiB_NodeGuid.BuildSourceString(), NodeGuid)
                    && ParseGuidText(NodeGuid, ParsedNodeGuid))
                {
                    ++OutNodeIds.FindOrAdd(GuidText(ParsedNodeGuid));
                }
            }
        }
    }
    return true;
}

FReferenceIndexScanResult ScanDecoded(
    const TSharedPtr<FJsonObject>& Root,
    const TMap<int32, FText>& Lookup,
    const FString& CandidateBlueprintPath,
    const TSet<FTopLevelAssetPath>& CandidateClassLineage,
    const bool bCandidateClassLineageComplete,
    IAssetRegistry* Registry,
    const FReferenceIndexTarget& Target,
    TFunctionRef<bool()> IsCancelled)
{
    FReferenceIndexScanResult Result;
    Result.Status = EReferenceIndexScanStatus::Parsed;
    if (!Root.IsValid())
    {
        Result.Status = EReferenceIndexScanStatus::Corrupt;
        Result.Message = TEXT("FiB JSON root is invalid.");
        return Result;
    }

    const TSet<FString> GraphCategories = {
        FFindInBlueprintSearchTags::FiB_UberGraphs.BuildSourceString(),
        FFindInBlueprintSearchTags::FiB_Functions.BuildSourceString(),
        FFindInBlueprintSearchTags::FiB_Macros.BuildSourceString(),
        FFindInBlueprintSearchTags::FiB_SubGraphs.BuildSourceString(),
        FFindInBlueprintSearchTags::FiB_ExtensionGraphs.BuildSourceString()
    };
    TMap<FString, int32> NodeIdCounts;
    bool bLocatorCountCancelled = false;
    if (!CollectLocatorCounts(
            Root,
            Lookup,
            GraphCategories,
            NodeIdCounts,
            IsCancelled,
            bLocatorCountCancelled))
    {
        Result.Status = bLocatorCountCancelled
            ? EReferenceIndexScanStatus::Cancelled
            : EReferenceIndexScanStatus::Corrupt;
        Result.Message = bLocatorCountCancelled
            ? TEXT("Reference index decoding was cancelled.")
            : TEXT("FiB contains a malformed graph entry.");
        return Result;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& RootPair : Root->Values)
    {
        if (IsCancelled())
        {
            Result.Status = EReferenceIndexScanStatus::Cancelled;
            Result.Message = TEXT("Reference index decoding was cancelled.");
            Result.Sites.Reset();
            return Result;
        }
        FString Category;
        const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
        if (!DecodeLookupToken(RootPair.Key, Lookup, Category)
            || !GraphCategories.Contains(Category)
            || !RootPair.Value.IsValid()
            || !RootPair.Value->TryGetArray(Graphs)
            || Graphs == nullptr)
        {
            continue;
        }
        for (const TSharedPtr<FJsonValue>& GraphValue : *Graphs)
        {
            if (IsCancelled())
            {
                Result.Status = EReferenceIndexScanStatus::Cancelled;
                Result.Message = TEXT("Reference index decoding was cancelled.");
                Result.Sites.Reset();
                return Result;
            }
            const TSharedPtr<FJsonObject>* Graph = nullptr;
            if (!GraphValue.IsValid() || !GraphValue->TryGetObject(Graph) || Graph == nullptr || !(*Graph).IsValid())
            {
                Result.Status = EReferenceIndexScanStatus::Corrupt;
                Result.Message = TEXT("FiB graph entry is not an object.");
                Result.Sites.Reset();
                return Result;
            }
            FString GraphName;
            DirectString(*Graph, Lookup, FFindInBlueprintSearchTags::FiB_Name.BuildSourceString(), GraphName);
            const TSharedPtr<FJsonValue> NodesValue =
                FindDirectField(*Graph, Lookup, FFindInBlueprintSearchTags::FiB_Nodes.BuildSourceString());
            const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
            if (!NodesValue.IsValid() || !NodesValue->TryGetArray(Nodes) || Nodes == nullptr)
            {
                continue;
            }
            for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
            {
                if (IsCancelled())
                {
                    Result.Status = EReferenceIndexScanStatus::Cancelled;
                    Result.Message = TEXT("Reference index decoding was cancelled.");
                    Result.Sites.Reset();
                    return Result;
                }
                const TSharedPtr<FJsonObject>* Node = nullptr;
                if (!NodeValue.IsValid() || !NodeValue->TryGetObject(Node) || Node == nullptr || !(*Node).IsValid())
                {
                    continue;
                }
                const TSharedPtr<FJsonValue> VariableReference = FindDirectField(*Node, Lookup, TEXT("VariableReference"));
                if (!VariableReference.IsValid())
                {
                    continue;
                }

                FGuid IndexedGuid;
                FString IndexedName;
                MemberGuid(VariableReference, Lookup, IndexedGuid);
                RecursiveString(VariableReference, Lookup, TEXT("MemberName"), IndexedName);
                const bool bIdentityMatches = Target.Identity.IsNative()
                    ? !IndexedName.IsEmpty() && FName(*IndexedName) == Target.Identity.Name
                    : IndexedGuid.IsValid() && IndexedGuid == Target.Identity.Guid;
                if (!bIdentityMatches)
                {
                    continue;
                }

                const EOwnerMatch Owner = MatchOwner(
                    CandidateBlueprintPath,
                    CandidateClassLineage,
                    bCandidateClassLineageComplete,
                    Registry,
                    VariableReference,
                    Lookup,
                    Target);
                if (Owner == EOwnerMatch::Unknown)
                {
                    Result.Status = EReferenceIndexScanStatus::Corrupt;
                    Result.Message = TEXT("FiB contains a matching member identity whose owner cannot be verified from Asset Registry class ancestry.");
                    Result.Sites.Reset();
                    return Result;
                }
                if (Owner == EOwnerMatch::NoMatch)
                {
                    continue;
                }

                FString NodeGuidText;
                FGuid NodeGuid;
                if (!DirectString(*Node, Lookup, FFindInBlueprintSearchTags::FiB_NodeGuid.BuildSourceString(), NodeGuidText)
                    || !ParseGuidText(NodeGuidText, NodeGuid))
                {
                    Result.Status = EReferenceIndexScanStatus::Corrupt;
                    Result.Message = TEXT("A matching FiB node has no valid NodeGuid.");
                    Result.Sites.Reset();
                    return Result;
                }
                if (NodeIdCounts.FindRef(GuidText(NodeGuid)) != 1)
                {
                    Result.Status = EReferenceIndexScanStatus::Corrupt;
                    Result.Message = TEXT("A matching FiB NodeGuid is ambiguous in the Blueprint index.");
                    Result.Sites.Reset();
                    return Result;
                }

                FReferenceIndexSite Site;
                Site.BlueprintPath = CandidateBlueprintPath;
                Site.GraphCategory = Category;
                Site.GraphDisplayName = GraphName;
                Site.NodeId = GuidText(NodeGuid);
                FString ShortClassName;
                DirectString(*Node, Lookup, FFindInBlueprintSearchTags::FiB_ClassName.BuildSourceString(), ShortClassName);
                // FiB stores only the schema's short node class label. Keep it
                // as provenance; never inspect/load UObject classes to expand
                // it into an apparently authoritative type path.
                Site.NodeType = MoveTemp(ShortClassName);
                DirectString(*Node, Lookup, FFindInBlueprintSearchTags::FiB_Name.BuildSourceString(), Site.NodeTitle);
                Site.MatchedPath = TEXT("VariableReference");
                Result.Sites.Add(MoveTemp(Site));
            }
        }
    }
    return Result;
}

bool DecodeInt32At(const FString& Encoded, const int32 CharacterOffset, int32& OutValue)
{
    if (CharacterOffset < 0
        || Encoded.Len() - CharacterOffset < static_cast<int32>(sizeof(int32)))
    {
        return false;
    }
    TArray<uint8> Bytes;
    Bytes.AddUninitialized(sizeof(int32));
    if (StringToBytes(
            Encoded.Mid(CharacterOffset, sizeof(int32)),
            Bytes.GetData(),
            sizeof(int32)) != sizeof(int32))
    {
        return false;
    }
    FMemoryReader Reader(Bytes);
    Reader << OutValue;
    return !Reader.IsError();
}

bool ValidateEncodedLayout(const FString& Encoded, const bool bVersioned, FString& OutError)
{
    const int32 LookupSizeOffset = bVersioned ? sizeof(int32) : 0;
    int32 LookupCharacters = 0;
    if (!DecodeInt32At(Encoded, LookupSizeOffset, LookupCharacters))
    {
        OutError = TEXT("The FiB index has no readable lookup-table size header.");
        return false;
    }
    const int64 JsonOffset = static_cast<int64>(LookupSizeOffset)
        + sizeof(int32)
        + static_cast<int64>(LookupCharacters);
    if (LookupCharacters < 0 || JsonOffset < 0 || JsonOffset >= Encoded.Len())
    {
        OutError = TEXT("The FiB lookup-table size exceeds the encoded index bounds.");
        return false;
    }
    const int32 LookupOffset = LookupSizeOffset + sizeof(int32);
    int32 LookupEntries = 0;
    if (LookupCharacters < static_cast<int32>(sizeof(int32))
        || !DecodeInt32At(Encoded, LookupOffset, LookupEntries)
        || LookupEntries < 0
        || LookupEntries > LookupCharacters / static_cast<int32>(sizeof(int32)))
    {
        OutError = TEXT("The FiB lookup table has an invalid element count.");
        return false;
    }
    int32 FirstJsonCharacter = static_cast<int32>(JsonOffset);
    while (FirstJsonCharacter < Encoded.Len() && FChar::IsWhitespace(Encoded[FirstJsonCharacter]))
    {
        ++FirstJsonCharacter;
    }
    if (FirstJsonCharacter >= Encoded.Len() || Encoded[FirstJsonCharacter] != TEXT('{'))
    {
        OutError = TEXT("The FiB index has no JSON object after its lookup table.");
        return false;
    }
    return true;
}

class FBoundedFiBMemoryReader final : public FMemoryReader
{
public:
    explicit FBoundedFiBMemoryReader(const TArray<uint8>& Bytes)
        : FMemoryReader(Bytes)
    {
        // FText/FString serializers consult this before allocating variable
        // payloads. No declaration inside the lookup may exceed its envelope.
        ArMaxSerializeSize = Bytes.Num();
    }
};

bool DecodeEncodedIndex(
    const FString& Encoded,
    const bool bVersioned,
    const int32 EditorObjectVersion,
    TSharedPtr<FJsonObject>& OutRoot,
    TMap<int32, FText>& OutLookup,
    FString& OutError)
{
    const int32 LookupSizeOffset = bVersioned ? sizeof(int32) : 0;
    int32 LookupCharacters = 0;
    if (!DecodeInt32At(Encoded, LookupSizeOffset, LookupCharacters)
        || LookupCharacters < static_cast<int32>(sizeof(int32)))
    {
        OutError = TEXT("The FiB index has no bounded lookup-table envelope.");
        return false;
    }
    const int32 LookupOffset = LookupSizeOffset + sizeof(int32);
    const int64 JsonOffset64 = static_cast<int64>(LookupOffset) + LookupCharacters;
    if (JsonOffset64 < 0 || JsonOffset64 >= Encoded.Len())
    {
        OutError = TEXT("The FiB lookup-table envelope exceeds the encoded index.");
        return false;
    }

    TArray<uint8> LookupBytes;
    LookupBytes.AddUninitialized(LookupCharacters);
    if (StringToBytes(
            Encoded.Mid(LookupOffset, LookupCharacters),
            LookupBytes.GetData(),
            LookupCharacters) != LookupCharacters)
    {
        OutError = TEXT("The FiB lookup table is not valid byte-string encoding.");
        return false;
    }

    FBoundedFiBMemoryReader LookupReader(LookupBytes);
    LookupReader.SetCustomVersion(
        FEditorObjectVersion::GUID,
        EditorObjectVersion,
        TEXT("Dev-Editor"));
    LookupReader << OutLookup;
    if (LookupReader.IsError() || LookupReader.Tell() != LookupReader.TotalSize())
    {
        OutLookup.Reset();
        OutError = TEXT("The FiB lookup table could not be decoded within its declared bounds.");
        return false;
    }

    const FString Json = Encoded.Mid(static_cast<int32>(JsonOffset64));
    const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(JsonReader, OutRoot) || !OutRoot.IsValid())
    {
        OutLookup.Reset();
        OutError = TEXT("The FiB JSON body is invalid.");
        return false;
    }
    return true;
}

bool ReadEditorObjectVersion(IAssetRegistry& Registry, const FAssetData& Data, int32& OutVersion)
{
    const TOptional<FAssetPackageData> PackageData = Registry.GetAssetPackageDataCopy(Data.PackageName);
    if (!PackageData.IsSet())
    {
        return false;
    }
    for (const UE::AssetRegistry::FPackageCustomVersion& Version : PackageData->GetCustomVersions())
    {
        if (Version.Key == FEditorObjectVersion::GUID)
        {
            OutVersion = Version.Version;
            return OutVersion >= 0;
        }
    }
    return false;
}

struct FCandidateLineage
{
    TSet<FTopLevelAssetPath> Paths;
    bool bComplete = false;
};

FCandidateLineage CandidateLineage(IAssetRegistry& Registry, const FAssetData& Data)
{
    FCandidateLineage Result;
    const FTopLevelAssetPath Generated = GeneratedClassPath(Data);
    if (Generated.IsNull())
    {
        return Result;
    }
    Result.Paths.Add(Generated);
    TArray<FTopLevelAssetPath> Ancestors;
    Result.bComplete = Registry.GetAncestorClassNames(Generated, Ancestors);
    if (Result.bComplete)
    {
        Result.Paths.Append(Ancestors);
    }
    return Result;
}
}

FTopLevelAssetPath FSalReferenceIndex::ResolveOwnerClassPath(
    IAssetRegistry& Registry,
    const FCanonicalReference& Identity)
{
    using namespace ReferenceIndexPrivate;
    if (Identity.IsNative())
    {
        return ClassPathFromExportText(Identity.OwnerPath);
    }
    if (const UBlueprint* LoadedOwner = FindObject<UBlueprint>(nullptr, *Identity.OwnerPath))
    {
        if (LoadedOwner->GeneratedClass != nullptr)
        {
            return LoadedOwner->GeneratedClass->GetClassPathName();
        }
    }
    const FString PackageName = FPackageName::ObjectPathToPackageName(Identity.OwnerPath);
    TArray<FAssetData> Assets;
    Registry.GetAssetsByPackageName(FName(*PackageName), Assets, false);
    for (const FAssetData& Data : Assets)
    {
        if (BlueprintPath(Data) == Identity.OwnerPath)
        {
            return GeneratedClassPath(Data);
        }
    }
    return FTopLevelAssetPath();
}

FReferenceIndexScanResult FSalReferenceIndex::ScanAsset(
    const FAssetData& Data,
    IAssetRegistry& Registry,
    const FReferenceIndexTarget& Target,
    TFunctionRef<bool()> IsCancelled)
{
    using namespace ReferenceIndexPrivate;
    FReferenceIndexScanResult Result;
    if (IsInGameThread())
    {
        Result.Status = EReferenceIndexScanStatus::Unsupported;
        Result.Message = TEXT("FiB decoding must run off the game thread so FText deserialization cannot load String Table assets.");
        return Result;
    }
    if (IsCancelled())
    {
        Result.Status = EReferenceIndexScanStatus::Cancelled;
        Result.Message = TEXT("Reference index decoding was cancelled.");
        return Result;
    }
    if (!IsIndexedVariableTarget(Target.Identity))
    {
        Result.Status = EReferenceIndexScanStatus::Unsupported;
        Result.Message = TEXT("UE 5.7 FiB does not persist this reference fact with exact authored identity.");
        return Result;
    }

    FName Tag = FBlueprintTags::FindInBlueprintsData;
    FAssetDataTagMapSharedView::FFindTagResult Found = Data.TagsAndValues.FindTag(Tag);
    bool bVersioned = Found.IsSet();
    int32 FiBVersion = EFiBVersion::FIB_VER_NONE;
    if (!Found.IsSet())
    {
        Tag = FBlueprintTags::UnversionedFindInBlueprintsData;
        Found = Data.TagsAndValues.FindTag(Tag);
        bVersioned = false;
        FiBVersion = EFiBVersion::FIB_VER_BASE;
    }
    if (!Found.IsSet())
    {
        Result.Status = EReferenceIndexScanStatus::Missing;
        Result.Message = TEXT("The unloaded Blueprint container has no saved FiB index.");
        return Result;
    }

    const FString Encoded = Found.GetValue();
    if (Encoded.IsEmpty())
    {
        Result.Status = EReferenceIndexScanStatus::Missing;
        Result.Message = TEXT("The unloaded Blueprint container has an empty FiB index.");
        return Result;
    }
    if (Encoded.Len() > MaxEncodedCharacters)
    {
        Result.Status = EReferenceIndexScanStatus::Oversized;
        Result.Message = FString::Printf(
            TEXT("The FiB index has %d characters, above Loomle's %d-character per-container decode limit."),
            Encoded.Len(),
            MaxEncodedCharacters);
        return Result;
    }
    if (FiBVersion == EFiBVersion::FIB_VER_NONE && !DecodeInt32At(Encoded, 0, FiBVersion))
    {
        Result.Status = EReferenceIndexScanStatus::Corrupt;
        Result.Message = TEXT("The versioned FiB index has no readable format version.");
        return Result;
    }
    if (FiBVersion < EFiBVersion::FIB_VER_LATEST)
    {
        Result.Status = EReferenceIndexScanStatus::Outdated;
        Result.Message = TEXT("The FiB index predates the latest UE 5.7 format required to cover variable references in every indexed Graph category.");
        return Result;
    }
    if (FiBVersion > EFiBVersion::FIB_VER_LATEST)
    {
        Result.Status = EReferenceIndexScanStatus::Corrupt;
        Result.Message = TEXT("The FiB index uses a newer format than this UE 5.7 Bridge can decode safely.");
        return Result;
    }
    FString LayoutError;
    if (!ValidateEncodedLayout(Encoded, bVersioned, LayoutError))
    {
        Result.Status = EReferenceIndexScanStatus::Corrupt;
        Result.Message = MoveTemp(LayoutError);
        return Result;
    }

    int32 EditorObjectVersion = -1;
    if (!ReadEditorObjectVersion(Registry, Data, EditorObjectVersion))
    {
        Result.Status = EReferenceIndexScanStatus::Corrupt;
        Result.Message = TEXT("The Blueprint package summary has no readable editor object version for FiB decoding.");
        return Result;
    }
    TMap<int32, FText> Lookup;
    TSharedPtr<FJsonObject> Root;
    FString DecodeError;
    if (!DecodeEncodedIndex(
            Encoded,
            bVersioned,
            EditorObjectVersion,
            Root,
            Lookup,
            DecodeError))
    {
        Result.Status = EReferenceIndexScanStatus::Corrupt;
        Result.Message = MoveTemp(DecodeError);
        return Result;
    }
    if (IsCancelled())
    {
        Result.Status = EReferenceIndexScanStatus::Cancelled;
        Result.Message = TEXT("Reference index decoding was cancelled.");
        return Result;
    }
    if (LookupRequiresAssetResolution(Lookup))
    {
        Result.Status = EReferenceIndexScanStatus::Unsupported;
        Result.Message = TEXT("The FiB lookup table contains String Table text that cannot be resolved under Loomle's zero-load policy.");
        return Result;
    }
    const FCandidateLineage Lineage = CandidateLineage(Registry, Data);
    return ScanDecoded(
        Root,
        Lookup,
        BlueprintPath(Data),
        Lineage.Paths,
        Lineage.bComplete,
        &Registry,
        Target,
        IsCancelled);
}

#if WITH_DEV_AUTOMATION_TESTS
FReferenceIndexScanResult FSalReferenceIndex::ScanDecodedForTesting(
    const TSharedPtr<FJsonObject>& Root,
    const TMap<int32, FText>& Lookup,
    const FString& CandidateBlueprintPath,
    const TSet<FTopLevelAssetPath>& CandidateClassLineage,
    const FReferenceIndexTarget& Target,
    const bool bCandidateClassLineageComplete)
{
    return ReferenceIndexPrivate::ScanDecoded(
        Root,
        Lookup,
        CandidateBlueprintPath,
        CandidateClassLineage,
        bCandidateClassLineageComplete,
        nullptr,
        Target,
        [] { return false; });
}

bool FSalReferenceIndex::ValidateEncodedLayoutForTesting(
    const FString& Encoded,
    const bool bVersioned,
    FString& OutError)
{
    return ReferenceIndexPrivate::ValidateEncodedLayout(Encoded, bVersioned, OutError);
}

bool FSalReferenceIndex::LookupRequiresAssetResolutionForTesting(
    const TMap<int32, FText>& Lookup)
{
    return ReferenceIndexPrivate::LookupRequiresAssetResolution(Lookup);
}
#endif
}
