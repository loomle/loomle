import type {
  Diagnostic,
  MutationResult,
  ObjectResult,
  Patch,
  Query,
  Result,
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
  ReferencesOperation,
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
  StableMemberRef,
  Statement,
  SummaryOperation,
  Target,
  TreeOperation,
  Wrap,
} from "./generated/sal-object-schema.js";

export type SalText = string;

export interface SalExecutionOptions {
  signal?: AbortSignal;
}

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
  query(text: SalText, options?: SalExecutionOptions): Promise<TextResult>;
  patch(text: SalText, options?: SalExecutionOptions): Promise<TextResult>;
  schema(module?: string): Promise<TextResult>;
}

export interface SalInterface {
  readonly name: string;
  readonly description: string;
  readonly text: string;
}

export interface CreateSalOptions {
  executor: SalExecutor;
  catalog: readonly SalInterface[];
}

export interface SalExecutor {
  readonly interfaces: readonly string[];
  query(object: Query, options?: SalExecutionOptions): Promise<Result>;
  patch?(object: Patch, options?: SalExecutionOptions): Promise<MutationResult>;
}

export { formatSalObject } from "./formatter.js";
export { parseSalObject } from "./parser.js";
export { createSal } from "./sdk.js";
export { objectResultToTextResult } from "./result.js";
