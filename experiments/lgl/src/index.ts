import type {
  Result,
  Patch,
  Query,
} from "./generated/lgl-object-schema.js";

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
  CreationResult,
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
  LGLNormalizedObjectSchema,
  LglObject,
  LocalRef,
  MemberRef,
  MoveBy,
  MoveTo,
  Name,
  Node,
  NodeCreation,
  OrderBy,
  Patch,
  PaletteCreationEntry,
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
  ShortcutCreationEntry,
  ShortcutNodeCreation,
  SourceSpan,
  Target,
  Value,
  WidgetDocument,
  WidgetNode,
  WidgetPatchOp,
  WidgetResult,
  WidgetTarget,
} from "./generated/lgl-object-schema.js";

export type LglText = string;
export type ObjectResult = Result;
export type Find = import("./generated/lgl-object-schema.js").Find;
export type Op = import("./generated/lgl-object-schema.js").PatchOp;

export interface TextResult {
  text?: LglText;
  diagnostics: ObjectResult["diagnostics"];
  page?: ObjectResult["page"];
}

export interface Lgl {
  query(text: LglText): Promise<TextResult>;
  patch(text: LglText): Promise<TextResult>;
}

export interface CreateLglOptions {
  adapters?: Adapter[];
}

export interface Adapter {
  domain: string;
  query(object: Query): Promise<ObjectResult>;
  patch?(object: Patch): Promise<ObjectResult>;
}

export { formatLglObject } from "./formatter.js";
export { parseLglObject } from "./parser.js";
export { createLgl } from "./sdk.js";
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
