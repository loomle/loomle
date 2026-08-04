// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace Loomle::EditorControl
{
/**
 * Executes private editor.open/editor.close transport operations.
 *
 * Arguments contain exactly one canonical normalized Blueprint or Graph
 * Target. Results keep the SAL ObjectResult subject separate from transient
 * Editor presentation outcome metadata.
 */
class FEditorControlService
{
public:
    static TSharedPtr<FJsonObject> Execute(
        const FString& Operation,
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutDispatchError);
};
}
