import type {
  Result,
  Patch,
  Query,
} from "./generated/lgl-object-schema.js";

export type {
  Add,
  AndCondition,
  Binding,
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
  FindNodes,
  FindPaletteEntry,
  FindPath,
  Graph,
  GraphFind,
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
  OrderBy,
  Patch,
  PaletteCreationEntry,
  PaletteSourceRef,
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
  SourceSpan,
  Target,
  Value,
} from "./generated/lgl-object-schema.js";

export type LglText = string;
export type ObjectResult = Result;
export type Find = import("./generated/lgl-object-schema.js").GraphFind;
export type Op = import("./generated/lgl-object-schema.js").GraphPatchOp;

export interface TextResult {
  text?: LglText;
  diagnostics: ObjectResult["diagnostics"];
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
  patch(object: Patch): Promise<ObjectResult>;
}

export { formatLglObject } from "./formatter.js";
export { parseLglObject } from "./parser.js";
export { createLgl } from "./sdk.js";
export { createMemoryGraphAdapter } from "./memory-adapter.js";
export type {
  CreateMemoryGraphAdapterOptions,
  MemoryGraphAdapter,
} from "./memory-adapter.js";
