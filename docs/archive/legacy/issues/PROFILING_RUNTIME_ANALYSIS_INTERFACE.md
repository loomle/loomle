# LOOMLE Local Issue: Top-Level `profiling` Interface as an Official Unreal Data Bridge

## Problem

`LOOMLE` can now run `execute` during `PIE`, which restores raw Unreal Python
reach.

That is necessary, but it is still not sufficient for profiling workflows.

Today an agent that wants runtime performance data often falls back to:

- toggling stat overlays and reading screenshots
- issuing raw console commands and scraping log text
- guessing how Unreal groups or averages the displayed values

This is the wrong product boundary.

For profiling, `LOOMLE` should not try to be the analyzer.
It should be the data bridge.

The product job is:

1. expose official Unreal profiling surfaces in stable machine-readable form
2. preserve official semantics instead of inventing a parallel taxonomy
3. make those surfaces usable during `PIE`
4. support heavier capture-style workflows through shared `jobs`

The agent can then do the interpretation.

## Current Implementation Status

The current `LOOMLE` implementation already ships:

- `profiling.action = "unit"`
- `profiling.action = "game"`
- `profiling.action = "gpu"`
- `profiling.action = "ticks"`
- `profiling.action = "memory"` with `kind = "summary"`

Current behavior notes:

- implemented profiling actions are available during `PIE`
- `unit` and `game` may return a retryable warmup error on first read
  - `STAT_UNIT_WARMUP_REQUIRED`
  - `STATS_GROUP_WARMUP_REQUIRED`
- `unit` then returns structured official `FStatUnitData`
- `game` then returns structured `FLatestGameThreadStatsData` group data,
  including hierarchical rows, flat rows, and optional thread breakdown
- `ticks` returns official `dumpticks` data in structured form
  - `mode = "grouped"` uses official tick-context counts
  - `mode = "all" | "enabled" | "disabled"` preserves official dump modes and
    bridges rows plus prerequisite lists
- `memory` returns a structured summary bridged from official engine memory
  statistics, allocator stats, and texture/RHI memory stats

## Research Summary

The engine already exposes several distinct profiling families.
They do not all look alike.

### 1. `stat unit` family

This is the cleanest structured source.

The engine keeps a dedicated `FStatUnitData` structure in
[UnrealClient.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Source/Runtime/Engine/Public/UnrealClient.h).

It already contains:

- filtered running-average values
  - `FrameTime`
  - `GameThreadTime`
  - `RenderThreadTime`
  - `RenderThreadTimeCriticalPath`
  - `GameThreadTimeCriticalPath`
  - `GPUFrameTime[gpu]`
  - `GPUClockFraction[gpu]`
  - `GPUUsageFraction[gpu]`
  - `GPUMemoryUsage[gpu]`
  - `GPUExternalUsageFraction[gpu]`
  - `GPUExternalMemoryUsage[gpu]`
  - `RHITTime`
  - `InputLatencyTime`
- raw equivalents
  - `RawFrameTime`
  - `RawGameThreadTime`
  - `RawRenderThreadTime`
  - `RawGPUFrameTime[gpu]`
  - `RawRHITTime`
  - `RawInputLatencyTime`
  - plus raw GPU usage and memory fields
- non-shipping history arrays
  - `FrameTimes`
  - `GameThreadTimes`
  - `RenderThreadTimes`
  - `GPUFrameTimes[gpu]`
  - `RHITTimes`
  - `InputLatencyTimes`

This data is already consumed by official engine code in:

- `UEngine::LogPerformanceCapture`
  [UnrealEngine.cpp](/Users/Shared/Epic%20Games/UE_5.7/Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp)
- `PerformanceMonitor`
  [PerformanceMonitor.cpp](/Users/Shared/Epic%20Games/UE_5.7/Engine/Plugins/Performance/PerformanceMonitor/Source/PerformanceMonitor/Private/PerformanceMonitor.cpp)
- `FunctionalTesting`
  [FunctionalTest.cpp](/Users/Shared/Epic%20Games/UE_5.7/Engine/Source/Developer/FunctionalTesting/Private/FunctionalTest.cpp)

Conclusion:

- `unit` is a first-class structured bridge target
- `LOOMLE` should return official average values directly
- optional raw and optional non-shipping history can be exposed without
  inventing new semantics

### 2. `stat game` and stats-group family

This is not a fixed metric object.

The runtime stats system aggregates group data into several existing structures
inside the stats command path:

- `FlatAggregate`
- `FlatAggregateThreadBreakdown`
- `HierAggregate`
- `MemoryAggregate`
- `CountersAggregate`
- `GpuStatsAggregate`

These are packaged into `FActiveStatGroupInfo` in
[StatsData.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Source/Runtime/Core/Public/Stats/StatsData.h).

The underlying complex stat payload already tracks:

- inclusive sum / average / max / min
- exclusive sum / average / max / min

through `FComplexStatMessage` and `EComplexStatField` in
[StatsSystemTypes.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Source/Runtime/Core/Public/Stats/StatsSystemTypes.h).

The command implementation for `stat hier`, `stat slow`, and group displays is
built in
[StatsCommand.cpp](/Users/Shared/Epic%20Games/UE_5.7/Engine/Source/Runtime/Core/Private/Stats/StatsCommand.cpp).

Conclusion:

- `game` should not be flattened into a small fixed object
- `game` should be bridged as a table/tree family
- the bridge should preserve inclusive/exclusive and thread-breakdown data

### 3. `dumpticks`

`dumpticks` is an official console command registered by the tick task manager.

Relevant engine paths:

- registration:
  [TickTaskManager.cpp](/Users/Shared/Epic%20Games/UE_5.7/Engine/Source/Runtime/Engine/Private/TickTaskManager.cpp)
- exec handling:
  [UnrealEngine.cpp](/Users/Shared/Epic%20Games/UE_5.7/Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp)

Important details:

- it supports `GROUPED`
- it supports `ENABLED`
- it supports `DISABLED`
- the official output is currently log-oriented

Conclusion:

- `ticks` is a valid official profiling family
- but its native shape is text dump, not a ready-made UObject or struct
- `LOOMLE` should bridge it into structured rows while preserving official mode
  flags

### 4. `memreport`

`memreport` is an official report family, not a single metric call.

The command is handled in
[UnrealEngine.cpp](/Users/Shared/Epic%20Games/UE_5.7/Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp),
and it works by:

1. deferring to end-of-frame
2. forcing async flush, GC, and render flush
3. loading command lists from config sections
4. executing a sequence of subordinate console commands
5. writing report text and optional files

The command sets come from:

- `[MemReportCommands]`
- `[MemReportFullCommands]`

in
[BaseEngine.ini](/Users/Shared/Epic%20Games/UE_5.7/Engine/Config/BaseEngine.ini)

Those command lists already include things such as:

- `Mem FromReport`
- `LogCountedInstances`
- `obj list -resourcesizesort`
- `rhi.DumpMemory`
- `rhi.DumpResourceMemory`
- `rhi.dumpresourcememory summary ...`
- `listtextures ...`
- `r.DumpRenderTargetPoolMemory`

Conclusion:

- `memory` is fundamentally a report family
- a complete bridge cannot be a single scalar summary
- `LOOMLE` should expose both:
  - lightweight directly-readable memory data where available
  - official memreport capture/report outputs for full coverage

### 5. GPU profiler / capture family

There are two layers here.

First, `stat unit` already exposes top-level GPU frame time through
`FStatUnitData`.

Second, the dedicated GPU profiler has its own tree and trace model through:

- `FGPUProfilerEventNode`
- `FGPUProfilerEventNodeFrame`
- `FGPUProfiler`

in
[GPUProfiler.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Source/Runtime/RHI/Public/GPUProfiler.h)
and
[GPUProfiler.cpp](/Users/Shared/Epic%20Games/UE_5.7/Engine/Source/Runtime/RHI/Private/GPUProfiler.cpp).

There is also a trace-based path in:

- [GpuProfilerTrace.cpp](/Users/Shared/Epic%20Games/UE_5.7/Engine/Source/Runtime/RHI/Private/GpuProfilerTrace.cpp)

Conclusion:

- top-level GPU timing belongs in `unit`
- deeper GPU pass/event trees belong in `gpu`
- heavyweight GPU capture belongs in `capture`

### 6. Stats-file / capture family

Official stats capture is already a formal command family:

- `stat startfile`
- `stat startfileraw`
- `stat stopfile`

implemented by `FCommandStatsFile` in
[StatsFile.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Source/Runtime/Core/Public/Stats/StatsFile.h)
and wired through
[StatsCommand.cpp](/Users/Shared/Epic%20Games/UE_5.7/Engine/Source/Runtime/Core/Private/Stats/StatsCommand.cpp).

Conclusion:

- heavy profiling capture is already an official lifecycle
- `LOOMLE` should not invent a new capture concept
- it should bridge official capture commands and route long-running ones through
  `jobs`

## Product Direction

Introduce one top-level tool:

- `profiling`

Use one required top-level field:

- `action`

This tool is not an analyzer.
It is an official Unreal profiling data bridge.

## Core Design Rules

1. preserve official profiling families
- `profiling` should be organized around official Unreal concepts
- not around `LOOMLE`-invented bottleneck categories

2. normalize transport, not semantics
- use one stable response envelope
- do not invent product-only conclusions such as
  `primaryBottleneck` or `nextRecommendedActions`

3. expose data at its native shape
- fixed object when the engine has a fixed object
- table/tree when the engine has table/tree semantics
- report/capture when the engine works as a report/capture system

4. support `PIE`
- profiling must work in the runtime context where agents actually diagnose
  performance

5. use `jobs` only for long-running capture/report work
- not for ordinary reads

## Tool Shape

Top-level tool:

- `profiling`

First-version actions:

1. `unit`
2. `game`
3. `gpu`
4. `ticks`
5. `memory`
6. `capture`

## Shared Response Envelope

All actions should return the same top-level shape:

```json
{
  "runtime": { "...": "..." },
  "source": { "...": "..." },
  "data": { "...": "..." }
}
```

### `runtime`

Common execution context:

- `isPIE`
- `worldType`
- `worldName`
- `viewportKind`
- optional `pieInstanceId`

### `source`

Origin metadata:

- `officialCommand`
- `backend`
- optional `group`
- optional `gpuIndex`
- optional `captureKind`

### `data`

The native payload for that profiling family.

## Action Design

## 1. `unit`

Purpose:

- bridge official `stat unit`
- return official running-average values
- optionally include raw values
- optionally expose extra GPU usage and memory fields already present in
  `FStatUnitData`

Suggested input:

```json
{
  "action": "unit",
  "world": "active",
  "gpuIndex": 0,
  "includeRaw": true,
  "includeGpuUtilization": true,
  "includeHistory": false
}
```

Suggested response shape:

```json
{
  "runtime": {
    "isPIE": true,
    "worldType": "pie",
    "worldName": "UEDPIE_0_Map"
  },
  "source": {
    "officialCommand": "stat unit",
    "backend": "FStatUnitData",
    "gpuIndex": 0
  },
  "data": {
    "average": {
      "frameTimeMs": 16.97,
      "gameThreadTimeMs": 9.40,
      "gameThreadCriticalPathMs": 9.10,
      "renderThreadTimeMs": 5.10,
      "renderThreadCriticalPathMs": 4.90,
      "gpuFrameTimeMs": 14.80,
      "gpuClockFraction": 0.82,
      "gpuUsageFraction": 0.88,
      "gpuMemoryBytes": 123456789,
      "gpuExternalUsageFraction": 0.04,
      "gpuExternalMemoryBytes": 1234567,
      "rhiThreadTimeMs": 1.00,
      "inputLatencyTimeMs": 0.00
    },
    "raw": {
      "frameTimeMs": 17.20,
      "gameThreadTimeMs": 9.80,
      "renderThreadTimeMs": 5.00,
      "gpuFrameTimeMs": 15.10,
      "rhiThreadTimeMs": 1.10,
      "inputLatencyTimeMs": 0.00
    }
  }
}
```

Warmup rule:

- `unit` should follow the official `stat unit` enable path
- if `Unit` stats were not already active, `LOOMLE` may need one rendered
  frame before `FStatUnitData` contains valid timing data
- in that case the bridge should return `STAT_UNIT_WARMUP_REQUIRED` as a
  retryable control-plane error instead of silently returning all-zero timings

Notes:

- no custom sampling mode
- no custom bottleneck classification
- if `includeHistory` is later enabled in non-shipping/editor builds, it should
  expose the existing arrays directly rather than inventing a new sample model

## 2. `game`

Purpose:

- bridge official stats-group / `stat game` style data
- preserve flat, hierarchy, and per-thread breakdown

Suggested input:

```json
{
  "action": "game",
  "group": "Game",
  "displayMode": "flat|hierarchical|both",
  "includeThreadBreakdown": true,
  "sortBy": "sum|call_count|name",
  "maxDepth": 4
}
```

Suggested response shape:

```json
{
  "runtime": { "...": "..." },
  "source": {
    "officialCommand": "stat game",
    "backend": "stats_system",
    "group": "Game"
  },
  "data": {
    "flat": {
      "rows": [
        {
          "name": "World Tick Time",
          "inclusive": { "avgMs": 2.81, "maxMs": 3.40, "minMs": 2.12, "sumMs": 56.2 },
          "exclusive": { "avgMs": 1.94, "maxMs": 2.60, "minMs": 1.40, "sumMs": 38.8 },
          "callCountAvg": 1
        }
      ]
    },
    "hierarchy": {
      "rows": [
        {
          "name": "GameThread",
          "depth": 0,
          "inclusive": { "avgMs": 9.40, "maxMs": 11.20, "minMs": 8.20, "sumMs": 188.0 },
          "exclusive": { "avgMs": 0.00, "maxMs": 0.00, "minMs": 0.00, "sumMs": 0.0 },
          "callCountAvg": 1
        }
      ]
    },
    "threadBreakdown": {
      "GameThread": [ "...same row shape..." ],
      "RenderThread": [ "...same row shape..." ]
    }
  }
}
```

Notes:

- this should expose the existing stats-system aggregates
- it should not hardcode a fixed four-column UI model
- if the engine gives hierarchy plus inclusive/exclusive plus call counts, the
  bridge should preserve those dimensions explicitly

## 3. `gpu`

Purpose:

- bridge official GPU profiling data beyond top-level GPU frame time
- preserve tree/event semantics instead of reducing everything to one number

Suggested input:

```json
{
  "action": "gpu",
  "mode": "current|profiled_frame",
  "includeHistogram": false,
  "includeStats": true
}
```

Suggested response shape:

```json
{
  "runtime": { "...": "..." },
  "source": {
    "officialCommand": "profilegpu",
    "backend": "FGPUProfiler"
  },
  "data": {
    "tree": [
      {
        "name": "BasePass",
        "inclusiveMs": 3.40,
        "exclusiveMs": 3.10,
        "children": []
      }
    ],
    "stats": {
      "numDraws": 1234,
      "numPrimitives": 567890
    }
  }
}
```

Notes:

- `gpu` is not required to be available on every platform or RHI
- when unavailable, return a precise unavailable error instead of degrading to
  screenshot workflows
- top-level GPU frame time still belongs to `unit`

## 4. `ticks`

Purpose:

- bridge official `dumpticks`
- preserve mode flags such as `GROUPED`, `ENABLED`, and `DISABLED`

Suggested input:

```json
{
  "action": "ticks",
  "mode": "all|grouped|enabled|disabled"
}
```

Suggested response shape:

```json
{
  "runtime": { "...": "..." },
  "source": {
    "officialCommand": "dumpticks",
    "backend": "FTickTaskManager"
  },
  "data": {
    "mode": "grouped",
    "rows": [
      {
        "label": "CharacterMovementComponent",
        "count": 24,
        "enabledCount": 24
      }
    ]
  }
}
```

Notes:

- first version may need to parse official dump output
- that is acceptable as long as the returned rows remain faithful to official
  command modes

## 5. `memory`

Purpose:

- bridge official memory-reporting surfaces
- expose both lightweight direct data and full report-driven data

Suggested input:

```json
{
  "action": "memory",
  "mode": "summary|report",
  "profile": "default|full",
  "log": false,
  "csv": false
}
```

Suggested response shape for lightweight summary:

```json
{
  "runtime": { "...": "..." },
  "source": {
    "officialCommand": "memreport",
    "backend": "memreport_summary"
  },
  "data": {
    "summary": {
      "platformMemory": {},
      "poolCapacity": {},
      "keySections": []
    }
  }
}
```

Suggested response shape for full report invocation:

```json
{
  "runtime": { "...": "..." },
  "source": {
    "officialCommand": "memreport",
    "backend": "MemReportCommands",
    "profile": "full"
  },
  "data": {
    "reportPath": "/abs/path/to/report.memreport",
    "commandsExecuted": [
      "Mem FromReport",
      "LogCountedInstances",
      "rhi.DumpMemory"
    ]
  }
}
```

Notes:

- the engine already defines full command bundles in config
- `LOOMLE` should reuse those official bundles instead of inventing a synthetic
  memory taxonomy

## 6. `capture`

Purpose:

- bridge heavy official profiling capture commands
- use `jobs` when the lifecycle is long-running

Suggested input:

```json
{
  "action": "capture",
  "kind": "stats_file|stats_file_raw|gpu_profile|memreport",
  "execution": {
    "mode": "job",
    "idempotencyKey": "profiling-capture-001",
    "label": "profilegpu_capture"
  }
}
```

Suggested response shape:

```json
{
  "job": {
    "jobId": "job_123",
    "status": "queued"
  }
}
```

Final `jobs.result` payload should include:

- `capturePath`
- `captureKind`
- `officialCommand`
- optional secondary artifact paths

## Execution Model

Most profiling reads should remain synchronous.

Expected default behavior:

- `unit`: sync
- `game`: sync
- `gpu`: sync when current data is directly available
- `ticks`: sync
- `memory`: sync for lightweight summary, `job` for full report if needed
- `capture`: typically `job`

## Error Model

Suggested first-wave errors:

- `PIE_NOT_ACTIVE`
- `WORLD_NOT_FOUND`
- `GAME_VIEWPORT_UNAVAILABLE`
- `STAT_UNIT_DATA_UNAVAILABLE`
- `STAT_UNIT_WARMUP_REQUIRED`
- `STATS_GROUP_UNAVAILABLE`
- `GPU_PROFILER_UNAVAILABLE`
- `TICKS_DATA_UNAVAILABLE`
- `MEMORY_REPORT_UNAVAILABLE`
- `CAPTURE_KIND_UNSUPPORTED`

## Rollout Order

### Phase 1

- `profiling.action = "unit"`
- direct structured bridge of `FStatUnitData`
- `profiling.action = "game"`
- direct structured bridge of `FLatestGameThreadStatsData`
- `profiling.action = "gpu"`
- direct structured bridge of official `stat gpu` stats-group data
- `profiling.action = "ticks"`
- direct structured bridge of official `dumpticks` modes
- `profiling.action = "memory"`
- `kind = "summary"` direct bridge of platform memory, allocator stats, and
  texture/RHI memory stats

### Phase 2

- `profiling.action = "memory"`
- official memreport/report bridge beyond summary mode

### Phase 3

- `profiling.action = "capture"`

## Acceptance Criteria

This issue is complete when:

1. agents no longer need screenshots to read `stat unit`
2. agents can retrieve official `dumpticks` data without log scraping
3. agents can retrieve official stats-group data without UI-only rendering
4. memory reporting can use official memreport bundles through a stable bridge
5. heavy profiling capture flows through shared `jobs`
6. the interface remains a data bridge, not an analysis engine
