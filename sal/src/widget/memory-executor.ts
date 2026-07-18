import {
  createMemoryExecutor,
  type MemoryDocument,
  type MemoryExecutor,
} from "../memory-executor.js";

export interface CreateMemoryWidgetExecutorOptions {
  documents: MemoryDocument[];
}

export type MemoryWidgetExecutor = MemoryExecutor;

export function createMemoryWidgetExecutor(
  options: CreateMemoryWidgetExecutorOptions,
): MemoryWidgetExecutor {
  return createMemoryExecutor({
    interfaces: ["blueprint", "widget"],
    documents: options.documents,
  });
}
