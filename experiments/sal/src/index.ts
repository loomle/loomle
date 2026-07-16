import type {
  Diagnostic,
  ObjectResult,
  Patch,
  Query,
  ResultPage,
  SalObject,
} from "./generated/sal-object-schema.js";

export type {
  Add,
  AndCondition,
  Binding,
  BindingMemberRef,
  BindingTarget,
  Break,
  Call,
  CollectionOperation,
  Comment,
  CompareCondition,
  Compile,
  Condition,
  Connect,
  ContainsCondition,
  Diagnostic,
  DiagnosticPath,
  Disconnect,
  Edge,
  EqCondition,
  Expr,
  FieldPath,
  FlowOperation,
  IdOperation,
  InlineObject,
  Insert,
  Invoke,
  InvokeOutput,
  LocalRef,
  MemberRef,
  Move,
  MutationResult,
  Name,
  NamedOperation,
  NeCondition,
  NotCondition,
  ObjectResult,
  ObjectText,
  OrderBy,
  OrCondition,
  Page,
  PaletteEntriesOperation,
  PalettePinContext,
  Patch,
  PatchOperation,
  PatchStatement,
  Point,
  Query,
  QueryOperation,
  Ref,
  Remove,
  Replace,
  Reset,
  Result,
  ResultPage,
  SALNormalizedObjectSchema,
  SalObject,
  Save,
  Set,
  SourceSpan,
  StableRef,
  Statement,
  SummaryOperation,
  Target,
  TreeOperation,
  Wrap,
} from "./generated/sal-object-schema.js";

export type SalText = string;

export interface ParseResult {
  object?: SalObject;
  diagnostics: Diagnostic[];
}

export interface TextResult {
  text?: SalText;
  diagnostics: Diagnostic[];
  page?: ResultPage;
  isError?: boolean;
  dryRun?: boolean;
  valid?: boolean;
  applied?: boolean;
  assetPath?: string;
  operation?: string;
  resolvedRefs?: unknown;
  planned?: unknown;
  diff?: unknown;
  previousRevision?: string;
  newRevision?: string;
}

export interface Sal {
  query(text: SalText): Promise<TextResult>;
  patch(text: SalText): Promise<TextResult>;
  schema(module?: string): Promise<TextResult>;
}

export interface CreateSalOptions {
  executor: SalExecutor;
}

export interface SalExecutor {
  readonly interfaces: readonly string[];
  query(object: Query): Promise<ObjectResult>;
  patch?(object: Patch): Promise<ObjectResult>;
}

export { formatSalObject } from "./formatter.js";
export { parseSalObject } from "./parser.js";
export { createSal } from "./sdk.js";
export { createMemoryExecutor, createMemoryGraphExecutor } from "./memory-executor.js";
export type {
  CreateMemoryExecutorOptions,
  CreateMemoryGraphExecutorOptions,
  MemoryDocument,
  MemoryExecutor,
  MemoryGraphExecutor,
} from "./memory-executor.js";
export { createMemoryAssetExecutor } from "./asset/memory-executor.js";
export type { CreateMemoryAssetExecutorOptions, MemoryAssetExecutor } from "./asset/memory-executor.js";
export { createMemoryBlueprintExecutor } from "./blueprint/memory-executor.js";
export type { CreateMemoryBlueprintExecutorOptions, MemoryBlueprintExecutor } from "./blueprint/memory-executor.js";
export { createMemoryWidgetExecutor } from "./widget/memory-executor.js";
export type { CreateMemoryWidgetExecutorOptions, MemoryWidgetExecutor } from "./widget/memory-executor.js";
