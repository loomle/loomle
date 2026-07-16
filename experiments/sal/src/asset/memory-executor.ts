import {
  createMemoryExecutor,
  type MemoryDocument,
  type MemoryExecutor,
} from "../memory-executor.js";

export interface CreateMemoryAssetExecutorOptions {
  documents: MemoryDocument[];
}

export type MemoryAssetExecutor = MemoryExecutor;

export function createMemoryAssetExecutor(
  options: CreateMemoryAssetExecutorOptions,
): MemoryAssetExecutor {
  return createMemoryExecutor({ interfaces: ["asset"], documents: options.documents });
}
