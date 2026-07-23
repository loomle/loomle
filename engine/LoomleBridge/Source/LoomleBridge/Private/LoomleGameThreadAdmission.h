// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

#include <atomic>

namespace Loomle::Runtime
{
enum class EGameThreadAdmissionState : uint8
{
    Waiting,
    Started,
    Cancelled,
};

/**
 * One-shot handoff shared by an RPC worker and its queued Game Thread task.
 * Exactly one side may move Waiting to Started or Cancelled.
 */
class FGameThreadAdmission final
{
public:
    bool TryStart()
    {
        uint8 Expected = static_cast<uint8>(EGameThreadAdmissionState::Waiting);
        return State.compare_exchange_strong(Expected, static_cast<uint8>(EGameThreadAdmissionState::Started));
    }

    bool TryCancel()
    {
        uint8 Expected = static_cast<uint8>(EGameThreadAdmissionState::Waiting);
        return State.compare_exchange_strong(Expected, static_cast<uint8>(EGameThreadAdmissionState::Cancelled));
    }

    EGameThreadAdmissionState GetState() const
    {
        return static_cast<EGameThreadAdmissionState>(State.load());
    }

private:
    std::atomic<uint8> State { static_cast<uint8>(EGameThreadAdmissionState::Waiting) };
};
}
