// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

namespace Loomle::Sal::StableIdentity
{
inline bool ValidateCanonicalGuidPath(
    const TArray<FString>& IdentityPath,
    FString& OutCode,
    FString& OutMessage)
{
    for (const FString& Segment : IdentityPath)
    {
        FGuid Guid;
        if (!FGuid::Parse(Segment, Guid)
            || !Guid.IsValid()
            || Segment
                != Guid.ToString(EGuidFormats::DigitsWithHyphensLower))
        {
            OutCode = TEXT("validation.invalid_reference");
            OutMessage = TEXT("StableRef Guid identity components must use canonical lowercase digits-with-hyphens.");
            return false;
        }
    }
    return true;
}
}
