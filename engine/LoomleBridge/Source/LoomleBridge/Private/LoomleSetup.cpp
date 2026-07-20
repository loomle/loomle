// Copyright 2026 Loomle contributors.

#include "LoomleSetup.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/Platform.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <initializer_list>

namespace LoomleSetup
{
namespace
{
struct FParsedCodexEntry
{
    EClientEntryKind Kind = EClientEntryKind::None;
    TArray<FString> Lines;
    FString Command;
    TArray<FString> Args;
    int32 SectionIndex = INDEX_NONE;
    int32 SectionEndIndex = INDEX_NONE;
    int32 CommandIndex = INDEX_NONE;
    int32 ArgsIndex = INDEX_NONE;
    bool bHasEntry = false;
    bool bAmbiguous = false;
    bool bSyntaxUnverified = false;
};

enum class ETomlStringMode
{
    None,
    Basic,
    Literal,
    MultilineBasic,
    MultilineLiteral
};

struct FTomlLexResult
{
    TArray<bool> TopLevelLines;
    TArray<int32> StatementEndLines;
    bool bStructurallyValid = true;
};

FTomlLexResult LexToml(const TArray<FString>& Lines)
{
    FTomlLexResult Result;
    Result.TopLevelLines.Init(false, Lines.Num());
    Result.StatementEndLines.SetNumUninitialized(Lines.Num());
    for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
    {
        Result.StatementEndLines[LineIndex] = LineIndex;
    }
    ETomlStringMode StringMode = ETomlStringMode::None;
    TArray<TCHAR> Delimiters;
    bool bEscaped = false;
    int32 OpenStatementLine = INDEX_NONE;

    for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
    {
        Result.TopLevelLines[LineIndex] = StringMode == ETomlStringMode::None && Delimiters.IsEmpty();
        const FString& Line = Lines[LineIndex];
        if (Result.TopLevelLines[LineIndex] && !Line.TrimStartAndEnd().IsEmpty())
        {
            OpenStatementLine = LineIndex;
        }
        bool bComment = false;
        for (int32 Index = 0; Index < Line.Len(); ++Index)
        {
            const TCHAR Character = Line[Index];
            const bool bTripleDouble = Character == TEXT('"')
                && Line.IsValidIndex(Index + 2)
                && Line[Index + 1] == TEXT('"')
                && Line[Index + 2] == TEXT('"');
            const bool bTripleSingle = Character == TEXT('\'')
                && Line.IsValidIndex(Index + 2)
                && Line[Index + 1] == TEXT('\'')
                && Line[Index + 2] == TEXT('\'');

            if (bComment)
            {
                continue;
            }
            if (StringMode == ETomlStringMode::Basic)
            {
                if (bEscaped)
                {
                    bEscaped = false;
                }
                else if (Character == TEXT('\\'))
                {
                    bEscaped = true;
                }
                else if (Character == TEXT('"'))
                {
                    StringMode = ETomlStringMode::None;
                }
                continue;
            }
            if (StringMode == ETomlStringMode::Literal)
            {
                if (Character == TEXT('\''))
                {
                    StringMode = ETomlStringMode::None;
                }
                continue;
            }
            if (StringMode == ETomlStringMode::MultilineBasic)
            {
                if (bEscaped)
                {
                    bEscaped = false;
                }
                else if (Character == TEXT('\\'))
                {
                    bEscaped = true;
                }
                else if (Character == TEXT('"'))
                {
                    int32 QuoteCount = 1;
                    while (Line.IsValidIndex(Index + QuoteCount)
                        && Line[Index + QuoteCount] == TEXT('"'))
                    {
                        ++QuoteCount;
                    }
                    if (QuoteCount >= 3)
                    {
                        if (QuoteCount > 5)
                        {
                            Result.bStructurallyValid = false;
                            return Result;
                        }
                        StringMode = ETomlStringMode::None;
                    }
                    Index += QuoteCount - 1;
                }
                continue;
            }
            if (StringMode == ETomlStringMode::MultilineLiteral)
            {
                if (Character == TEXT('\''))
                {
                    int32 QuoteCount = 1;
                    while (Line.IsValidIndex(Index + QuoteCount)
                        && Line[Index + QuoteCount] == TEXT('\''))
                    {
                        ++QuoteCount;
                    }
                    if (QuoteCount >= 3)
                    {
                        if (QuoteCount > 5)
                        {
                            Result.bStructurallyValid = false;
                            return Result;
                        }
                        StringMode = ETomlStringMode::None;
                    }
                    Index += QuoteCount - 1;
                }
                continue;
            }

            if (Character == TEXT('#'))
            {
                bComment = true;
            }
            else if (bTripleDouble)
            {
                StringMode = ETomlStringMode::MultilineBasic;
                Index += 2;
            }
            else if (bTripleSingle)
            {
                StringMode = ETomlStringMode::MultilineLiteral;
                Index += 2;
            }
            else if (Character == TEXT('"'))
            {
                StringMode = ETomlStringMode::Basic;
            }
            else if (Character == TEXT('\''))
            {
                StringMode = ETomlStringMode::Literal;
            }
            else if (Character == TEXT('[') || Character == TEXT('{'))
            {
                Delimiters.Add(Character);
            }
            else if (Character == TEXT(']') || Character == TEXT('}'))
            {
                const TCHAR ExpectedOpen = Character == TEXT(']') ? TEXT('[') : TEXT('{');
                if (Delimiters.IsEmpty() || Delimiters.Last() != ExpectedOpen)
                {
                    Result.bStructurallyValid = false;
                    return Result;
                }
                Delimiters.Pop(EAllowShrinking::No);
            }
        }

        if (StringMode == ETomlStringMode::Basic || StringMode == ETomlStringMode::Literal)
        {
            Result.bStructurallyValid = false;
            return Result;
        }
        if (StringMode == ETomlStringMode::MultilineBasic)
        {
            bEscaped = false;
        }
        if (OpenStatementLine != INDEX_NONE
            && StringMode == ETomlStringMode::None
            && Delimiters.IsEmpty())
        {
            Result.StatementEndLines[OpenStatementLine] = LineIndex;
            OpenStatementLine = INDEX_NONE;
        }
    }

    Result.bStructurallyValid = StringMode == ETomlStringMode::None && Delimiters.IsEmpty();
    return Result;
}

FString NormalizePath(FString Path)
{
    Path.TrimStartAndEndInline();
    FPaths::NormalizeFilename(Path);
    while (Path.Len() > 1 && Path.EndsWith(TEXT("/")))
    {
        Path.LeftChopInline(1, EAllowShrinking::No);
    }
    return Path;
}

bool SamePath(const FString& Left, const FString& Right)
{
    const FString NormalizedLeft = Left.TrimStartAndEnd();
    const FString NormalizedRight = Right.TrimStartAndEnd();
    return !NormalizedLeft.IsEmpty()
        && !NormalizedRight.IsEmpty()
        && !FPaths::IsRelative(NormalizedLeft)
        && !FPaths::IsRelative(NormalizedRight)
        && FPaths::IsSamePath(NormalizedLeft, NormalizedRight);
}

bool HasMcpArgs(const TArray<FString>& Args)
{
    return Args.Num() == 1 && Args[0].Equals(TEXT("mcp"), ESearchCase::CaseSensitive);
}

bool IsCanonicalBundledClientPath(const FString& Command)
{
    const FString Normalized = NormalizePath(Command);
    if (Normalized.IsEmpty() || FPaths::IsRelative(Normalized))
    {
        return false;
    }

    const FString Marker = TEXT("/LoomleBridge/Resources/Loomle/");
#if PLATFORM_MICROSOFT
    constexpr ESearchCase::Type PathSearchCase = ESearchCase::IgnoreCase;
#else
    constexpr ESearchCase::Type PathSearchCase = ESearchCase::CaseSensitive;
#endif
    const int32 ResourcesIndex = Normalized.Find(Marker, PathSearchCase, ESearchDir::FromEnd);
    if (ResourcesIndex == INDEX_NONE)
    {
        return false;
    }

    const FString Tail = Normalized.Mid(ResourcesIndex + Marker.Len());
    TArray<FString> Parts;
    Tail.ParseIntoArray(Parts, TEXT("/"), true);
    if (Parts.Num() != 2)
    {
        return false;
    }

    const FString& Target = Parts[0];
    const FString& FileName = Parts[1];
    const FString CurrentTarget = GetCurrentClientTarget();
    if (CurrentTarget.IsEmpty() || !Target.Equals(CurrentTarget, PathSearchCase))
    {
        return false;
    }

    return Target.StartsWith(TEXT("win32-"), PathSearchCase)
        ? FileName.Equals(TEXT("loomle.exe"), PathSearchCase)
        : FileName.Equals(TEXT("loomle"), PathSearchCase);
}

bool IsLegacyPythonEntry(const FString& Command, const TArray<FString>& Args)
{
    if (!Command.Equals(TEXT("uv"), ESearchCase::CaseSensitive) || Args.Num() != 4)
    {
        return false;
    }

    const FString McpDirectory = NormalizePath(Args[1]);
    return Args[0].Equals(TEXT("--directory"), ESearchCase::CaseSensitive)
        && !McpDirectory.IsEmpty()
        && !FPaths::IsRelative(McpDirectory)
        && McpDirectory.EndsWith(TEXT("/Resources/MCP"), ESearchCase::CaseSensitive)
        && McpDirectory.Contains(TEXT("/LoomleBridge/"), ESearchCase::CaseSensitive)
        && Args[2].Equals(TEXT("run"), ESearchCase::CaseSensitive)
        && Args[3].Equals(TEXT("loomle_mcp_server.py"), ESearchCase::CaseSensitive);
}

bool IsLegacyGlobalEntry(
    const FString& Command,
    const TArray<FString>& Args,
    const FString& LoomleHomeDirectory)
{
    if (Args.Num() != 1
        || !Args[0].Equals(TEXT("mcp"), ESearchCase::CaseSensitive)
        || Command.IsEmpty()
        || FPaths::IsRelative(Command))
    {
        return false;
    }

    const FString LegacyPath = FPaths::Combine(LoomleHomeDirectory, TEXT(".loomle"), TEXT("bin"), TEXT("loomle"));
    return SamePath(Command, LegacyPath) || SamePath(Command, LegacyPath + TEXT(".exe"));
}

EClientEntryKind ClassifyEntry(
    const FString& Command,
    const TArray<FString>& Args,
    const FString& BundledClientPath,
    bool bBundledClientAvailable,
    const FString& LoomleHomeDirectory,
    FClientFileExists ClientFileExists)
{
    if (Command.IsEmpty())
    {
        return EClientEntryKind::Manual;
    }
    if (IsLegacyPythonEntry(Command, Args))
    {
        return EClientEntryKind::LegacyPython;
    }
    if (IsLegacyGlobalEntry(Command, Args, LoomleHomeDirectory))
    {
        return EClientEntryKind::LegacyGlobal;
    }
    if (HasMcpArgs(Args) && SamePath(Command, BundledClientPath))
    {
        return bBundledClientAvailable
            ? EClientEntryKind::Bundled
            : EClientEntryKind::StaleBundled;
    }
    if (HasMcpArgs(Args) && IsCanonicalBundledClientPath(Command))
    {
        return ClientFileExists(Command)
            ? EClientEntryKind::Bundled
            : EClientEntryKind::StaleBundled;
    }
    return EClientEntryKind::Manual;
}

bool ParseTomlBasicString(const FString& Text, int32& InOutIndex, FString& OutValue)
{
    if (!Text.IsValidIndex(InOutIndex) || Text[InOutIndex] != TEXT('"'))
    {
        return false;
    }

    ++InOutIndex;
    OutValue.Reset();
    while (Text.IsValidIndex(InOutIndex))
    {
        const TCHAR Character = Text[InOutIndex++];
        if (Character == TEXT('"'))
        {
            return true;
        }
        if (Character != TEXT('\\'))
        {
            OutValue.AppendChar(Character);
            continue;
        }
        if (!Text.IsValidIndex(InOutIndex))
        {
            return false;
        }

        const TCHAR Escape = Text[InOutIndex++];
        switch (Escape)
        {
        case TEXT('b'):
            OutValue.AppendChar(TEXT('\b'));
            break;
        case TEXT('t'):
            OutValue.AppendChar(TEXT('\t'));
            break;
        case TEXT('n'):
            OutValue.AppendChar(TEXT('\n'));
            break;
        case TEXT('f'):
            OutValue.AppendChar(TEXT('\f'));
            break;
        case TEXT('r'):
            OutValue.AppendChar(TEXT('\r'));
            break;
        case TEXT('"'):
            OutValue.AppendChar(TEXT('"'));
            break;
        case TEXT('\\'):
            OutValue.AppendChar(TEXT('\\'));
            break;
        default:
            return false;
        }
    }
    return false;
}

bool ParseTomlLiteralString(const FString& Text, int32& InOutIndex, FString& OutValue)
{
    if (!Text.IsValidIndex(InOutIndex) || Text[InOutIndex] != TEXT('\''))
    {
        return false;
    }

    const int32 Start = ++InOutIndex;
    while (Text.IsValidIndex(InOutIndex))
    {
        if (Text[InOutIndex] == TEXT('\n') || Text[InOutIndex] == TEXT('\r'))
        {
            return false;
        }
        if (Text[InOutIndex] == TEXT('\''))
        {
            OutValue = Text.Mid(Start, InOutIndex - Start);
            ++InOutIndex;
            return true;
        }
        ++InOutIndex;
    }
    return false;
}

bool ParseTomlString(const FString& Text, int32& InOutIndex, FString& OutValue)
{
    return Text.IsValidIndex(InOutIndex)
        && (Text[InOutIndex] == TEXT('"')
            ? ParseTomlBasicString(Text, InOutIndex, OutValue)
            : ParseTomlLiteralString(Text, InOutIndex, OutValue));
}

void SkipWhitespace(const FString& Text, int32& InOutIndex)
{
    while (Text.IsValidIndex(InOutIndex) && FChar::IsWhitespace(Text[InOutIndex]))
    {
        ++InOutIndex;
    }
}

void SkipTomlArrayTrivia(const FString& Text, int32& InOutIndex)
{
    while (true)
    {
        SkipWhitespace(Text, InOutIndex);
        if (!Text.IsValidIndex(InOutIndex) || Text[InOutIndex] != TEXT('#'))
        {
            return;
        }
        while (Text.IsValidIndex(InOutIndex)
            && Text[InOutIndex] != TEXT('\n')
            && Text[InOutIndex] != TEXT('\r'))
        {
            ++InOutIndex;
        }
    }
}

bool EndsTomlValue(const FString& Text, int32 Index)
{
    SkipWhitespace(Text, Index);
    return !Text.IsValidIndex(Index) || Text[Index] == TEXT('#');
}

bool ParseTomlStringValue(const FString& Text, FString& OutValue)
{
    int32 Index = 0;
    SkipWhitespace(Text, Index);
    return ParseTomlString(Text, Index, OutValue) && EndsTomlValue(Text, Index);
}

bool ParseTomlStringArray(const FString& Text, TArray<FString>& OutValues)
{
    int32 Index = 0;
    SkipWhitespace(Text, Index);
    if (!Text.IsValidIndex(Index) || Text[Index++] != TEXT('['))
    {
        return false;
    }

    OutValues.Reset();
    SkipTomlArrayTrivia(Text, Index);
    if (Text.IsValidIndex(Index) && Text[Index] == TEXT(']'))
    {
        ++Index;
        return EndsTomlValue(Text, Index);
    }

    while (Text.IsValidIndex(Index))
    {
        FString Value;
        if (!ParseTomlString(Text, Index, Value))
        {
            return false;
        }
        OutValues.Add(MoveTemp(Value));
        SkipTomlArrayTrivia(Text, Index);
        if (!Text.IsValidIndex(Index))
        {
            return false;
        }
        if (Text[Index] == TEXT(']'))
        {
            ++Index;
            return EndsTomlValue(Text, Index);
        }
        if (Text[Index++] != TEXT(','))
        {
            return false;
        }
        SkipTomlArrayTrivia(Text, Index);
        if (Text.IsValidIndex(Index) && Text[Index] == TEXT(']'))
        {
            ++Index;
            return EndsTomlValue(Text, Index);
        }
    }
    return false;
}

bool ParseTomlKeyValue(const FString& Line, FString& OutKey, FString& OutValue)
{
    const FString Trimmed = Line.TrimStartAndEnd();
    if (Trimmed.IsEmpty() || Trimmed.StartsWith(TEXT("#")))
    {
        return false;
    }
    int32 EqualsIndex = INDEX_NONE;
    if (!Trimmed.FindChar(TEXT('='), EqualsIndex))
    {
        return false;
    }
    OutKey = Trimmed.Left(EqualsIndex).TrimStartAndEnd();
    OutValue = Trimmed.Mid(EqualsIndex + 1);
    return !OutKey.IsEmpty();
}

enum class ETomlTableKind
{
    NotTable,
    Other,
    LoomleRoot,
    LoomleDescendant,
    AmbiguousLoomle
};

FString StripTomlComment(const FString& Line)
{
    bool bInBasicString = false;
    bool bInLiteralString = false;
    bool bEscaped = false;
    for (int32 Index = 0; Index < Line.Len(); ++Index)
    {
        const TCHAR Character = Line[Index];
        if (bInBasicString)
        {
            if (bEscaped)
            {
                bEscaped = false;
            }
            else if (Character == TEXT('\\'))
            {
                bEscaped = true;
            }
            else if (Character == TEXT('"'))
            {
                bInBasicString = false;
            }
            continue;
        }
        if (bInLiteralString)
        {
            if (Character == TEXT('\''))
            {
                bInLiteralString = false;
            }
            continue;
        }
        if (Character == TEXT('"'))
        {
            bInBasicString = true;
        }
        else if (Character == TEXT('\''))
        {
            bInLiteralString = true;
        }
        else if (Character == TEXT('#'))
        {
            return Line.Left(Index);
        }
    }
    return Line;
}

bool ParseTomlKeySegment(const FString& Text, int32& InOutIndex, FString& OutSegment)
{
    SkipWhitespace(Text, InOutIndex);
    if (!Text.IsValidIndex(InOutIndex))
    {
        return false;
    }
    if (Text[InOutIndex] == TEXT('"'))
    {
        return ParseTomlBasicString(Text, InOutIndex, OutSegment);
    }
    if (Text[InOutIndex] == TEXT('\''))
    {
        const int32 Start = ++InOutIndex;
        while (Text.IsValidIndex(InOutIndex) && Text[InOutIndex] != TEXT('\''))
        {
            ++InOutIndex;
        }
        if (!Text.IsValidIndex(InOutIndex))
        {
            return false;
        }
        OutSegment = Text.Mid(Start, InOutIndex - Start);
        ++InOutIndex;
        return true;
    }

    const int32 Start = InOutIndex;
    while (Text.IsValidIndex(InOutIndex))
    {
        const TCHAR Character = Text[InOutIndex];
        if (!(FChar::IsAlnum(Character) || Character == TEXT('_') || Character == TEXT('-')))
        {
            break;
        }
        ++InOutIndex;
    }
    if (InOutIndex == Start)
    {
        return false;
    }
    OutSegment = Text.Mid(Start, InOutIndex - Start);
    return true;
}

bool ParseTomlDottedKey(const FString& Text, TArray<FString>& OutSegments)
{
    int32 Index = 0;
    OutSegments.Reset();
    while (true)
    {
        FString Segment;
        if (!ParseTomlKeySegment(Text, Index, Segment))
        {
            return false;
        }
        OutSegments.Add(MoveTemp(Segment));
        SkipWhitespace(Text, Index);
        if (!Text.IsValidIndex(Index))
        {
            return true;
        }
        if (Text[Index++] != TEXT('.'))
        {
            return false;
        }
    }
}

bool StartsWithSegments(const TArray<FString>& Segments, std::initializer_list<const TCHAR*> Prefix)
{
    if (Segments.Num() < static_cast<int32>(Prefix.size()))
    {
        return false;
    }
    int32 Index = 0;
    for (const TCHAR* Expected : Prefix)
    {
        if (!Segments[Index++].Equals(Expected, ESearchCase::CaseSensitive))
        {
            return false;
        }
    }
    return true;
}

bool IsPrefixOfSegments(const TArray<FString>& Segments, std::initializer_list<const TCHAR*> FullPath)
{
    if (Segments.Num() > static_cast<int32>(FullPath.size()))
    {
        return false;
    }
    int32 Index = 0;
    for (const TCHAR* Expected : FullPath)
    {
        if (Index >= Segments.Num())
        {
            return true;
        }
        if (!Segments[Index++].Equals(Expected, ESearchCase::CaseSensitive))
        {
            return false;
        }
    }
    return true;
}

FString MakeTomlPathKey(const TArray<FString>& Segments)
{
    FString Result;
    for (const FString& Segment : Segments)
    {
        Result += FString::Printf(TEXT("%d:%s;"), Segment.Len(), *Segment);
    }
    return Result;
}

bool HasTomlPathPrefix(const TArray<FString>& Path, const TArray<FString>& Prefix)
{
    if (Path.Num() < Prefix.Num())
    {
        return false;
    }
    for (int32 Index = 0; Index < Prefix.Num(); ++Index)
    {
        if (!Path[Index].Equals(Prefix[Index], ESearchCase::CaseSensitive))
        {
            return false;
        }
    }
    return true;
}

bool IsAtOrUnderArrayTable(
    const TArray<FString>& Path,
    const TArray<TArray<FString>>& ArrayTablePaths,
    bool* bOutExact = nullptr)
{
    if (bOutExact != nullptr)
    {
        *bOutExact = false;
    }
    bool bFoundPrefix = false;
    for (const TArray<FString>& ArrayTablePath : ArrayTablePaths)
    {
        if (!HasTomlPathPrefix(Path, ArrayTablePath))
        {
            continue;
        }
        bFoundPrefix = true;
        if (Path.Num() == ArrayTablePath.Num())
        {
            if (bOutExact != nullptr)
            {
                *bOutExact = true;
            }
            return true;
        }
    }
    return bFoundPrefix;
}

bool LooksPossiblyLikeLoomleTable(const FString& Text)
{
    const FString Lower = Text.ToLower();
    return Lower.Contains(TEXT("loomle"))
        && (Lower.Contains(TEXT("mcp_servers"))
            || (Lower.Contains(TEXT("mcp")) && Lower.Contains(TEXT("servers"))));
}

ETomlTableKind ClassifyTomlTable(
    const FString& Line,
    TArray<FString>* OutSegments = nullptr,
    bool* bOutArrayTable = nullptr)
{
    if (OutSegments != nullptr)
    {
        OutSegments->Reset();
    }
    if (bOutArrayTable != nullptr)
    {
        *bOutArrayTable = false;
    }
    const FString WithoutComment = StripTomlComment(Line).TrimStartAndEnd();
    if (!WithoutComment.StartsWith(TEXT("[")))
    {
        return ETomlTableKind::NotTable;
    }

    const bool bArrayTable = WithoutComment.StartsWith(TEXT("[["));
    if (bOutArrayTable != nullptr)
    {
        *bOutArrayTable = bArrayTable;
    }
    const FString Open = bArrayTable ? TEXT("[[") : TEXT("[");
    const FString Close = bArrayTable ? TEXT("]]" ) : TEXT("]");
    if (!WithoutComment.EndsWith(Close) || WithoutComment.Len() <= Open.Len() + Close.Len())
    {
        return LooksPossiblyLikeLoomleTable(WithoutComment) || WithoutComment.Contains(TEXT("mcp_servers"))
            ? ETomlTableKind::AmbiguousLoomle
            : ETomlTableKind::Other;
    }

    const FString Key = WithoutComment.Mid(Open.Len(), WithoutComment.Len() - Open.Len() - Close.Len());
    TArray<FString> Segments;
    if (!ParseTomlDottedKey(Key, Segments))
    {
        return LooksPossiblyLikeLoomleTable(Key) || Key.Contains(TEXT("mcp_servers"))
            ? ETomlTableKind::AmbiguousLoomle
            : ETomlTableKind::Other;
    }
    if (OutSegments != nullptr)
    {
        *OutSegments = Segments;
    }

    const bool bCurrentPrefix = StartsWithSegments(Segments, { TEXT("mcp_servers"), TEXT("loomle") });
    const bool bLegacyPrefix = StartsWithSegments(Segments, { TEXT("mcp"), TEXT("servers"), TEXT("loomle") });
    const bool bCurrentAncestor = IsPrefixOfSegments(Segments, { TEXT("mcp_servers"), TEXT("loomle") });
    const bool bLegacyAncestor = IsPrefixOfSegments(Segments, { TEXT("mcp"), TEXT("servers"), TEXT("loomle") });
    if (bArrayTable && (bCurrentPrefix || bLegacyPrefix || bCurrentAncestor || bLegacyAncestor))
    {
        return ETomlTableKind::AmbiguousLoomle;
    }
    if (!bCurrentPrefix && !bLegacyPrefix)
    {
        return ETomlTableKind::Other;
    }
    const int32 RootSegmentCount = bCurrentPrefix ? 2 : 3;
    return Segments.Num() == RootSegmentCount
        ? ETomlTableKind::LoomleRoot
        : ETomlTableKind::LoomleDescendant;
}

bool IsAlternativeLoomleAssignment(
    const FString& Line,
    const TArray<FString>& CurrentTableSegments,
    ETomlTableKind CurrentTableKind)
{
    if (CurrentTableKind == ETomlTableKind::LoomleRoot
        || CurrentTableKind == ETomlTableKind::LoomleDescendant)
    {
        return false;
    }

    FString Key;
    FString Value;
    if (!ParseTomlKeyValue(Line, Key, Value))
    {
        return false;
    }

    TArray<FString> KeySegments;
    if (!ParseTomlDottedKey(Key, KeySegments))
    {
        return LooksPossiblyLikeLoomleTable(Key);
    }

    TArray<FString> AbsoluteSegments = CurrentTableSegments;
    AbsoluteSegments.Append(KeySegments);
    return StartsWithSegments(AbsoluteSegments, { TEXT("mcp_servers"), TEXT("loomle") })
        || IsPrefixOfSegments(AbsoluteSegments, { TEXT("mcp_servers"), TEXT("loomle") })
        || StartsWithSegments(AbsoluteSegments, { TEXT("mcp"), TEXT("servers"), TEXT("loomle") })
        || IsPrefixOfSegments(AbsoluteSegments, { TEXT("mcp"), TEXT("servers"), TEXT("loomle") });
}

bool IsTomlSection(const FString& Line)
{
    return Line.TrimStart().StartsWith(TEXT("["));
}

FString GetTomlStatement(
    const TArray<FString>& Lines,
    const FTomlLexResult& LexResult,
    int32 StartLine)
{
    int32 EndLine = StartLine;
    if (LexResult.StatementEndLines.IsValidIndex(StartLine))
    {
        EndLine = FMath::Clamp(LexResult.StatementEndLines[StartLine], StartLine, Lines.Num() - 1);
    }

    FString Statement;
    for (int32 LineIndex = StartLine; LineIndex <= EndLine; ++LineIndex)
    {
        if (!Statement.IsEmpty())
        {
            Statement += TEXT("\n");
        }
        Statement += Lines[LineIndex];
    }
    return Statement;
}

FParsedCodexEntry ParseCodexEntry(
    const FString& RawConfig,
    const FString& BundledClientPath,
    bool bBundledClientAvailable,
    const FString& LoomleHomeDirectory,
    FClientFileExists ClientFileExists)
{
    FParsedCodexEntry Result;
    RawConfig.ParseIntoArrayLines(Result.Lines, false);
    const FTomlLexResult LexResult = LexToml(Result.Lines);
    if (!LexResult.bStructurallyValid)
    {
        Result.bAmbiguous = true;
        Result.Kind = EClientEntryKind::Manual;
        return Result;
    }

    bool bSawLoomleDescendant = false;
    TArray<FString> CurrentTableSegments;
    ETomlTableKind CurrentTableKind = ETomlTableKind::Other;
    bool bCurrentTableIsArray = false;
    TSet<FString> DefinedKeys;
    TSet<FString> DefinedTables;
    TSet<FString> ArrayTables;
    TArray<TArray<FString>> ArrayTableSegmentPaths;
    for (int32 Index = 0; Index < Result.Lines.Num(); ++Index)
    {
        if (!LexResult.TopLevelLines.IsValidIndex(Index) || !LexResult.TopLevelLines[Index])
        {
            continue;
        }
        TArray<FString> TableSegments;
        bool bTableIsArray = false;
        const ETomlTableKind TableKind = ClassifyTomlTable(
            Result.Lines[Index],
            &TableSegments,
            &bTableIsArray);
        if (TableKind == ETomlTableKind::NotTable)
        {
            if (IsAlternativeLoomleAssignment(Result.Lines[Index], CurrentTableSegments, CurrentTableKind))
            {
                Result.bAmbiguous = true;
                Result.Kind = EClientEntryKind::Manual;
                return Result;
            }

            const FString TrimmedLine = Result.Lines[Index].TrimStartAndEnd();
            if (!TrimmedLine.IsEmpty() && !TrimmedLine.StartsWith(TEXT("#")))
            {
                FString Key;
                FString Value;
                TArray<FString> KeySegments;
                if (!ParseTomlKeyValue(Result.Lines[Index], Key, Value))
                {
                    Result.bAmbiguous = true;
                    Result.Kind = EClientEntryKind::Manual;
                    return Result;
                }

                const FString TrimmedValue = Value.TrimStartAndEnd();
                if (TrimmedValue.IsEmpty() || TrimmedValue.StartsWith(TEXT("=")))
                {
                    Result.bAmbiguous = true;
                    Result.Kind = EClientEntryKind::Manual;
                    return Result;
                }
                if (!ParseTomlDottedKey(Key, KeySegments))
                {
                    Result.bSyntaxUnverified = true;
                    continue;
                }

                TArray<FString> AbsoluteKeySegments = CurrentTableSegments;
                AbsoluteKeySegments.Append(KeySegments);
                const FString AbsoluteKey = MakeTomlPathKey(AbsoluteKeySegments);
                const bool bKeyUnderArrayTable = bCurrentTableIsArray
                    || IsAtOrUnderArrayTable(AbsoluteKeySegments, ArrayTableSegmentPaths);
                if (!bKeyUnderArrayTable
                    && (DefinedKeys.Contains(AbsoluteKey) || DefinedTables.Contains(AbsoluteKey)))
                {
                    Result.bAmbiguous = true;
                    Result.Kind = EClientEntryKind::Manual;
                    return Result;
                }
                if (!bKeyUnderArrayTable)
                {
                    DefinedKeys.Add(AbsoluteKey);
                }
                else
                {
                    Result.bSyntaxUnverified = true;
                }

                const bool bSupportedRootField = CurrentTableKind == ETomlTableKind::LoomleRoot
                    && KeySegments.Num() == 1
                    && (KeySegments[0].Equals(TEXT("command")) || KeySegments[0].Equals(TEXT("args")));
                if (!bSupportedRootField)
                {
                    Result.bSyntaxUnverified = true;
                }
            }
            continue;
        }
        if (TableKind == ETomlTableKind::AmbiguousLoomle)
        {
            Result.bAmbiguous = true;
            Result.Kind = EClientEntryKind::Manual;
            return Result;
        }

        if (TableSegments.IsEmpty())
        {
            Result.bAmbiguous = true;
            Result.Kind = EClientEntryKind::Manual;
            return Result;
        }
        const FString TablePath = MakeTomlPathKey(TableSegments);
        bool bExactArrayTable = false;
        const bool bTableUnderArray = IsAtOrUnderArrayTable(
            TableSegments,
            ArrayTableSegmentPaths,
            &bExactArrayTable);
        if ((!bTableUnderArray && DefinedKeys.Contains(TablePath))
            || (!bTableUnderArray && !bTableIsArray && DefinedTables.Contains(TablePath))
            || (!bTableUnderArray && bTableIsArray && DefinedTables.Contains(TablePath) && !ArrayTables.Contains(TablePath))
            || (!bTableIsArray && bExactArrayTable))
        {
            Result.bAmbiguous = true;
            Result.Kind = EClientEntryKind::Manual;
            return Result;
        }
        DefinedTables.Add(TablePath);
        if (bTableIsArray)
        {
            ArrayTables.Add(TablePath);
            if (!bExactArrayTable)
            {
                ArrayTableSegmentPaths.Add(TableSegments);
            }
        }
        if (bTableUnderArray)
        {
            Result.bSyntaxUnverified = true;
        }
        CurrentTableSegments = MoveTemp(TableSegments);
        CurrentTableKind = TableKind;
        bCurrentTableIsArray = bTableIsArray;
        if (CurrentTableKind == ETomlTableKind::Other)
        {
            Result.bSyntaxUnverified = true;
        }
        if (TableKind == ETomlTableKind::LoomleDescendant)
        {
            bSawLoomleDescendant = true;
            continue;
        }
        if (TableKind != ETomlTableKind::LoomleRoot)
        {
            continue;
        }
        if (Result.SectionIndex != INDEX_NONE)
        {
            Result.bAmbiguous = true;
            Result.Kind = EClientEntryKind::Manual;
            return Result;
        }
        Result.SectionIndex = Index;
    }

    if (Result.SectionIndex == INDEX_NONE)
    {
        if (bSawLoomleDescendant)
        {
            Result.bAmbiguous = true;
            Result.Kind = EClientEntryKind::Manual;
        }
        return Result;
    }
    Result.bHasEntry = true;
    Result.SectionEndIndex = Result.Lines.Num();
    for (int32 Index = Result.SectionIndex + 1; Index < Result.Lines.Num(); ++Index)
    {
        if (LexResult.TopLevelLines.IsValidIndex(Index)
            && LexResult.TopLevelLines[Index]
            && IsTomlSection(Result.Lines[Index]))
        {
            Result.SectionEndIndex = Index;
            break;
        }
    }

    for (int32 Index = Result.SectionIndex + 1; Index < Result.SectionEndIndex; ++Index)
    {
        if (!LexResult.TopLevelLines.IsValidIndex(Index) || !LexResult.TopLevelLines[Index])
        {
            continue;
        }
        FString Key;
        FString Value;
        const FString Statement = GetTomlStatement(Result.Lines, LexResult, Index);
        if (!ParseTomlKeyValue(Statement, Key, Value))
        {
            continue;
        }
        TArray<FString> FieldKeySegments;
        if (!ParseTomlDottedKey(Key, FieldKeySegments) || FieldKeySegments.Num() != 1)
        {
            continue;
        }
        const FString& FieldKey = FieldKeySegments[0];
        if (FieldKey.Equals(TEXT("command")))
        {
            if (Result.CommandIndex != INDEX_NONE || !ParseTomlStringValue(Value, Result.Command))
            {
                Result.bAmbiguous = true;
                Result.Kind = EClientEntryKind::Manual;
                return Result;
            }
            Result.CommandIndex = Index;
        }
        else if (FieldKey.Equals(TEXT("args")))
        {
            if (Result.ArgsIndex != INDEX_NONE || !ParseTomlStringArray(Value, Result.Args))
            {
                Result.bAmbiguous = true;
                Result.Kind = EClientEntryKind::Manual;
                return Result;
            }
            Result.ArgsIndex = Index;
        }
    }

    if (Result.CommandIndex == INDEX_NONE)
    {
        Result.bAmbiguous = true;
        Result.Kind = EClientEntryKind::Manual;
        return Result;
    }
    Result.Kind = ClassifyEntry(
        Result.Command,
        Result.Args,
        BundledClientPath,
        bBundledClientAvailable,
        LoomleHomeDirectory,
        ClientFileExists);
    return Result;
}

FString TomlQuotedString(const FString& Value)
{
    FString Escaped = Value;
    Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
    Escaped.ReplaceInline(TEXT("\b"), TEXT("\\b"));
    Escaped.ReplaceInline(TEXT("\t"), TEXT("\\t"));
    Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Escaped.ReplaceInline(TEXT("\f"), TEXT("\\f"));
    Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    return FString::Printf(TEXT("\"%s\""), *Escaped);
}

bool NeedsMigration(EClientEntryKind Kind)
{
    return Kind == EClientEntryKind::LegacyPython
        || Kind == EClientEntryKind::LegacyGlobal
        || Kind == EClientEntryKind::StaleBundled;
}

FString MakeCodexSuggestedText(const FString& BundledClientPath)
{
    return FString::Printf(
        TEXT("[mcp_servers.loomle]\ncommand = %s\nargs = [\"mcp\"]"),
        *TomlQuotedString(BundledClientPath));
}

FString MakeClaudeSuggestedText(const FString& BundledClientPath)
{
    return FString::Printf(
        TEXT("{\n  \"mcpServers\": {\n    \"loomle\": {\n      \"command\": %s,\n      \"args\": [\"mcp\"]\n    }\n  }\n}"),
        *TomlQuotedString(BundledClientPath));
}

void SetMissingPayloadResult(FConfigAssessment& Assessment)
{
    if (Assessment.ExistingKind == EClientEntryKind::Bundled
        || Assessment.ExistingKind == EClientEntryKind::Manual)
    {
        return;
    }
    Assessment.bBlocked = true;
    Assessment.Message = TEXT("The bundled Loomle Client is missing; no configuration change was attempted.");
}

TArray<FString> JsonArgs(const TSharedPtr<FJsonObject>& Entry, bool& bOutValid)
{
    bOutValid = true;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Entry->TryGetArrayField(TEXT("args"), Values))
    {
        bOutValid = !Entry->HasField(TEXT("args"));
        return {};
    }
    if (Values == nullptr)
    {
        bOutValid = false;
        return {};
    }

    TArray<FString> Result;
    for (const TSharedPtr<FJsonValue>& Value : *Values)
    {
        FString StringValue;
        if (!Value.IsValid() || !Value->TryGetString(StringValue))
        {
            bOutValid = false;
            return {};
        }
        Result.Add(MoveTemp(StringValue));
    }
    return Result;
}

struct FJsonContainer
{
    TSet<FString> Keys;
    bool bObject = false;
};

bool HasDuplicateJsonObjectKeys(const FString& RawConfig, bool& bOutValid)
{
    bOutValid = true;
    if (RawConfig.TrimStartAndEnd().IsEmpty())
    {
        return false;
    }

    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawConfig);
    TArray<FJsonContainer> Containers;
    EJsonNotation Notation;
    while (Reader->ReadNext(Notation))
    {
        if (Notation == EJsonNotation::Error)
        {
            bOutValid = false;
            return false;
        }

        if (!Containers.IsEmpty()
            && Containers.Last().bObject
            && Notation != EJsonNotation::ObjectEnd)
        {
            const FString& Identifier = Reader->GetIdentifier();
            if (Containers.Last().Keys.Contains(Identifier))
            {
                return true;
            }
            Containers.Last().Keys.Add(Identifier);
        }

        if (Notation == EJsonNotation::ObjectStart || Notation == EJsonNotation::ArrayStart)
        {
            FJsonContainer& Container = Containers.AddDefaulted_GetRef();
            Container.bObject = Notation == EJsonNotation::ObjectStart;
        }
        else if (Notation == EJsonNotation::ObjectEnd || Notation == EJsonNotation::ArrayEnd)
        {
            if (Containers.IsEmpty()
                || Containers.Last().bObject != (Notation == EJsonNotation::ObjectEnd))
            {
                bOutValid = false;
                return false;
            }
            Containers.Pop(EAllowShrinking::No);
        }
    }

    bOutValid = Reader->GetErrorMessage().IsEmpty() && Containers.IsEmpty();
    return false;
}
}

FString MakeClientTarget(const FString& NodePlatform, const FString& Architecture)
{
    if ((NodePlatform.Equals(TEXT("darwin"))
            || NodePlatform.Equals(TEXT("win32"))
            || NodePlatform.Equals(TEXT("linux")))
        && (Architecture.Equals(TEXT("arm64")) || Architecture.Equals(TEXT("x64"))))
    {
        return NodePlatform + TEXT("-") + Architecture;
    }
    return FString();
}

FString GetCurrentClientTarget()
{
    FString NodePlatform;
#if PLATFORM_MAC
    NodePlatform = TEXT("darwin");
#elif PLATFORM_WINDOWS
    NodePlatform = TEXT("win32");
#elif PLATFORM_LINUX
    NodePlatform = TEXT("linux");
#else
    return FString();
#endif

    FString Architecture;
#if PLATFORM_CPU_ARM_FAMILY
    Architecture = TEXT("arm64");
#elif PLATFORM_CPU_X86_FAMILY && PLATFORM_64BITS
    Architecture = TEXT("x64");
#else
    return FString();
#endif
    return MakeClientTarget(NodePlatform, Architecture);
}

FString GetBundledClientPath(const FString& PluginBaseDir)
{
    const FString Target = GetCurrentClientTarget();
    if (Target.IsEmpty())
    {
        return FString();
    }
#if PLATFORM_WINDOWS
    const TCHAR* FileName = TEXT("loomle.exe");
#else
    const TCHAR* FileName = TEXT("loomle");
#endif
    return FPaths::Combine(PluginBaseDir, TEXT("Resources"), TEXT("Loomle"), Target, FileName);
}

bool HasBundledClient(const FString& PluginBaseDir)
{
    const FString Path = GetBundledClientPath(PluginBaseDir);
    return !Path.IsEmpty() && IFileManager::Get().FileSize(*Path) > 0;
}

FString ResolveCodexConfigPath(
    const FString& LoomleHomeDirectory,
    const FString& CodexHomeEnvironment)
{
    FString Root = CodexHomeEnvironment.TrimStartAndEnd();
    const bool bUsesCodexHome = !Root.IsEmpty();
    if (!bUsesCodexHome)
    {
        Root = LoomleHomeDirectory.TrimStartAndEnd();
    }
    if (Root.IsEmpty() || FPaths::IsRelative(Root))
    {
        return FString();
    }

    FPaths::NormalizeDirectoryName(Root);
    if (!FPaths::CollapseRelativeDirectories(Root) || Root.IsEmpty() || FPaths::IsRelative(Root))
    {
        return FString();
    }

    return bUsesCodexHome
        ? FPaths::Combine(Root, TEXT("config.toml"))
        : FPaths::Combine(Root, TEXT(".codex"), TEXT("config.toml"));
}

FString ClientEntryKindToString(EClientEntryKind Kind)
{
    switch (Kind)
    {
    case EClientEntryKind::Bundled:
        return TEXT("bundled_client");
    case EClientEntryKind::StaleBundled:
        return TEXT("stale_bundled_client");
    case EClientEntryKind::LegacyPython:
        return TEXT("legacy_python");
    case EClientEntryKind::LegacyGlobal:
        return TEXT("legacy_global");
    case EClientEntryKind::Manual:
        return TEXT("manual");
    case EClientEntryKind::None:
    default:
        return TEXT("none");
    }
}

FConfigAssessment AssessCodexConfig(
    const FString& RawConfig,
    const FString& BundledClientPath,
    bool bBundledClientAvailable,
    const FString& LoomleHomeDirectory,
    FClientFileExists ClientFileExists)
{
    FConfigAssessment Assessment;
    Assessment.SuggestedText = MakeCodexSuggestedText(BundledClientPath);
    FParsedCodexEntry Entry = ParseCodexEntry(
        RawConfig,
        BundledClientPath,
        bBundledClientAvailable,
        LoomleHomeDirectory,
        ClientFileExists);
    Assessment.ExistingKind = Entry.Kind;
    Assessment.bSyntaxUnverified = Entry.bSyntaxUnverified;

    if (Entry.bAmbiguous)
    {
        Assessment.SuggestedText.Reset();
        Assessment.bBlocked = true;
        Assessment.Message = TEXT("The Codex config is structurally invalid or its Loomle MCP definition is ambiguous. No configuration change was attempted.");
        return Assessment;
    }
    if (Entry.Kind == EClientEntryKind::Bundled)
    {
        Assessment.Message = Entry.bSyntaxUnverified
            ? TEXT("Codex uses a compatible bundled Loomle Client, but unrelated TOML was not fully validated. Loomle made no changes.")
            : TEXT("Codex currently uses a compatible bundled Loomle Client.");
        return Assessment;
    }
    if (Entry.Kind == EClientEntryKind::Manual)
    {
        Assessment.Message = Entry.bSyntaxUnverified
            ? TEXT("Codex has a custom Loomle MCP entry and TOML that was not fully validated. Loomle kept it unchanged.")
            : TEXT("Codex has a custom Loomle MCP entry. Loomle kept it unchanged.");
        return Assessment;
    }
    if (!bBundledClientAvailable)
    {
        SetMissingPayloadResult(Assessment);
        return Assessment;
    }
    if (Entry.Kind == EClientEntryKind::None)
    {
        Assessment.bNeedsConfiguration = true;
        Assessment.Message = Entry.bSyntaxUnverified
            ? TEXT("No Codex Loomle MCP entry was recognized. Validate the existing TOML before adding the suggested entry.")
            : TEXT("Codex has no Loomle MCP entry. Copy the setup prompt to configure it.");
        return Assessment;
    }

    Assessment.bNeedsMigration = NeedsMigration(Entry.Kind);
    Assessment.Message = Entry.bSyntaxUnverified
        ? TEXT("Codex uses a legacy or stale Loomle MCP entry, and unrelated TOML was not fully validated. Validate the file before replacing only the Loomle entry.")
        : TEXT("Codex uses a legacy or stale Loomle MCP entry. Copy the setup prompt to migrate it.");
    return Assessment;
}

FConfigAssessment AssessClaudeConfig(
    const FString& RawConfig,
    const FString& BundledClientPath,
    bool bBundledClientAvailable,
    const FString& LoomleHomeDirectory,
    FClientFileExists ClientFileExists)
{
    FConfigAssessment Assessment;
    Assessment.SuggestedText = MakeClaudeSuggestedText(BundledClientPath);

    bool bJsonValid = true;
    if (HasDuplicateJsonObjectKeys(RawConfig, bJsonValid))
    {
        Assessment.ExistingKind = EClientEntryKind::Manual;
        Assessment.bBlocked = true;
        Assessment.Message = TEXT("Claude Desktop config contains duplicate object keys. No configuration change was attempted.");
        return Assessment;
    }
    if (!bJsonValid)
    {
        Assessment.ExistingKind = EClientEntryKind::Manual;
        Assessment.bBlocked = true;
        Assessment.Message = TEXT("Claude Desktop config is not valid JSON. No configuration change was attempted.");
        return Assessment;
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    if (!RawConfig.TrimStartAndEnd().IsEmpty())
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawConfig);
        if (!FJsonSerializer::Deserialize(
                Reader,
                Root,
                FJsonSerializer::EFlags::StoreNumbersAsStrings)
            || !Root.IsValid())
        {
            Assessment.ExistingKind = EClientEntryKind::Manual;
            Assessment.bBlocked = true;
            Assessment.Message = TEXT("Claude Desktop config is not a JSON object. No configuration change was attempted.");
            return Assessment;
        }
    }

    TSharedPtr<FJsonObject> McpServers;
    const TSharedPtr<FJsonObject>* ExistingMcpServers = nullptr;
    if (Root->TryGetObjectField(TEXT("mcpServers"), ExistingMcpServers)
        && ExistingMcpServers != nullptr
        && (*ExistingMcpServers).IsValid())
    {
        McpServers = *ExistingMcpServers;
    }
    else if (Root->HasField(TEXT("mcpServers")))
    {
        Assessment.ExistingKind = EClientEntryKind::Manual;
        Assessment.bBlocked = true;
        Assessment.Message = TEXT("Claude Desktop config mcpServers is not an object. No configuration change was attempted.");
        return Assessment;
    }
    else
    {
        Assessment.ExistingKind = EClientEntryKind::None;
    }

    TSharedPtr<FJsonObject> Entry;
    const TSharedPtr<FJsonObject>* ExistingEntry = nullptr;
    if (McpServers.IsValid()
        && McpServers->TryGetObjectField(TEXT("loomle"), ExistingEntry)
        && ExistingEntry != nullptr
        && (*ExistingEntry).IsValid())
    {
        Entry = *ExistingEntry;
    }
    else if (McpServers.IsValid() && McpServers->HasField(TEXT("loomle")))
    {
        Assessment.ExistingKind = EClientEntryKind::Manual;
        Assessment.bBlocked = true;
        Assessment.Message = TEXT("Claude Desktop Loomle MCP entry is not an object. No configuration change was attempted.");
        return Assessment;
    }

    if (Entry.IsValid())
    {
        FString Command;
        bool bArgsValid = false;
        const TArray<FString> Args = JsonArgs(Entry, bArgsValid);
        if (!Entry->TryGetStringField(TEXT("command"), Command) || !bArgsValid)
        {
            Assessment.ExistingKind = EClientEntryKind::Manual;
            Assessment.bBlocked = true;
            Assessment.Message = TEXT("Claude Desktop Loomle MCP entry could not be classified. No configuration change was attempted.");
            return Assessment;
        }
        Assessment.ExistingKind = ClassifyEntry(
            Command,
            Args,
            BundledClientPath,
            bBundledClientAvailable,
            LoomleHomeDirectory,
            ClientFileExists);
    }

    if (Assessment.ExistingKind == EClientEntryKind::Bundled)
    {
        Assessment.Message = TEXT("Claude Desktop currently uses a compatible bundled Loomle Client.");
        return Assessment;
    }
    if (Assessment.ExistingKind == EClientEntryKind::Manual)
    {
        Assessment.Message = TEXT("Claude Desktop has a custom Loomle MCP entry. Loomle kept it unchanged.");
        return Assessment;
    }
    if (!bBundledClientAvailable)
    {
        SetMissingPayloadResult(Assessment);
        return Assessment;
    }
    if (Assessment.ExistingKind == EClientEntryKind::None)
    {
        Assessment.bNeedsConfiguration = true;
        Assessment.Message = TEXT("Claude Desktop has no Loomle MCP entry. Copy the setup prompt to configure it.");
        return Assessment;
    }

    Assessment.bNeedsMigration = NeedsMigration(Assessment.ExistingKind);
    Assessment.Message = TEXT("Claude Desktop uses a legacy or stale Loomle MCP entry. Copy the setup prompt to migrate it.");
    return Assessment;
}
}
