import {
  createMemoryExecutor,
  type MemoryDocument,
  type MemoryExecutor,
} from "../memory-executor.js";

export interface CreateMemoryBlueprintExecutorOptions {
  documents: MemoryDocument[];
}

export type MemoryBlueprintExecutor = MemoryExecutor;

export function createMemoryBlueprintExecutor(
  options: CreateMemoryBlueprintExecutorOptions,
): MemoryBlueprintExecutor {
  return createMemoryExecutor({ interfaces: ["blueprint"], documents: options.documents });
}
