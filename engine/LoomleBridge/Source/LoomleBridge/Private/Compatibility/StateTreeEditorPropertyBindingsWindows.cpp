// Copyright 2026 Loomle contributors.

#include "StateTreeEditorPropertyBindings.h"

#if PLATFORM_WINDOWS

FPropertyBindingBinding* FStateTreeEditorPropertyBindings::AddBindingInternal(
    const FPropertyBindingPath& InSourcePath,
    const FPropertyBindingPath& InTargetPath)
{
    constexpr bool bIsOutputBinding = false;
    return &PropertyBindings.Emplace_GetRef(
        InSourcePath,
        InTargetPath,
        bIsOutputBinding);
}

#endif
