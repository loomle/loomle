import type {
  Result,
  Patch,
  Query,
} from "./generated/sal-object-schema.js";

export type {
  Add,
  AndCondition,
  Asset,
  AssetResult,
  AssetTarget,
  Blueprint,
  BlueprintComponent,
  BlueprintMember,
  BlueprintPatchOp,
  BlueprintResult,
  BlueprintTarget,
  Binding,
  BindingValue,
  Call,
  Condition,
  Connect,
  ContainsCondition,
  CreationEntry,
  PaletteResult,
  Detail,
  Diagnostic,
  DisconnectByEdge,
  DisconnectByPin,
  Edge,
  EqCondition,
  Expr,
  FieldPath,
  FindAssets,
  FindBlueprintComponents,
  FindBlueprintMembers,
  FindWidgetTree,
  FindWidgets,
  FindNodes,
  FindPaletteEntry,
  FindPath,
  Graph,
  GraphFind,
  GraphTarget,
  GraphIdRef,
  GraphNameRef,
  GraphPatchOp,
  GraphRef,
  IdRef,
  Insert,
  SALNormalizedObjectSchema,
  SalObject,
  LocalRef,
  MemberRef,
  MoveBy,
  MoveTo,
  Name,
  Node,
  NodeCreation,
  OrderBy,
  Patch,
  PaletteEntry,
  PaletteNodeCreation,
  PaletteSourceRef,
  PatchOp,
  Page,
  Pin,
  PinContext,
  PinRef,
  Point,
  Query,
  Reconstruct,
  Ref,
  Remove,
  Result,
  Set,
  SetTarget,
  ShortcutEntry,
  ShortcutNodeCreation,
  SourceSpan,
  Target,
  Value,
  WidgetDocument,
  ClassEntry,
  WidgetNode,
  WidgetPatchOp,
  Property,
  WidgetResult,
  WidgetTarget,
} from "./generated/sal-object-schema.js";

export type SalText = string;
export type ObjectResult = Result;
export type Find = import("./generated/sal-object-schema.js").Find;
export type Op = import("./generated/sal-object-schema.js").PatchOp;

export interface TextResult {
  text?: SalText;
  diagnostics: ObjectResult["diagnostics"];
  page?: ObjectResult["page"];
}

export interface SchemaResult {
  schema: unknown;
  diagnostics: ObjectResult["diagnostics"];
}

export interface Sal {
  query(text: SalText): Promise<TextResult>;
  patch(text: SalText): Promise<TextResult>;
  schema(): Promise<SchemaResult>;
}

export interface CreateSalOptions {
  adapters?: Adapter[];
}

export interface Adapter {
  domain: string;
  query(object: Query): Promise<ObjectResult>;
  patch?(object: Patch): Promise<ObjectResult>;
}

export { formatSalObject } from "./formatter.js";
export { parseSalObject } from "./parser.js";
export { createSal } from "./sdk.js";
export { createMemoryGraphAdapter } from "./memory-adapter.js";
export type {
  CreateMemoryGraphAdapterOptions,
  MemoryGraphAdapter,
} from "./memory-adapter.js";
export { createMemoryAssetAdapter } from "./asset/memory-adapter.js";
export type {
  CreateMemoryAssetAdapterOptions,
  MemoryAssetAdapter,
} from "./asset/memory-adapter.js";
export { createMemoryBlueprintAdapter } from "./blueprint/memory-adapter.js";
export type {
  CreateMemoryBlueprintAdapterOptions,
  MemoryBlueprintAdapter,
} from "./blueprint/memory-adapter.js";
export { createMemoryWidgetAdapter } from "./widget/memory-adapter.js";
export type {
  CreateMemoryWidgetAdapterOptions,
  MemoryWidgetAdapter,
} from "./widget/memory-adapter.js";
