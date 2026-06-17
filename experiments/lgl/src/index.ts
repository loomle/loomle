import type {
  ObjectResult,
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
  Detail,
  Diagnostic,
  DisconnectByEdge,
  DisconnectByPin,
  Edge,
  EqCondition,
  Expr,
  FieldRef,
  Find,
  FindNode,
  FindNodes,
  FindPaletteEntry,
  FindPath,
  FindSurrounding,
  Graph,
  GraphIdRef,
  GraphNameRef,
  GraphRef,
  Insert,
  LGLObjectSchema,
  LglObject,
  MoveBy,
  MoveTo,
  Name,
  Node,
  NodeLayout,
  ObjectResult,
  Op,
  Palette,
  PaletteBinding,
  PaletteEntryRef,
  Patch,
  Pin,
  PinChain,
  PinChainSegment,
  PinLayout,
  PinRef,
  PinSegment,
  Point,
  Query,
  Reconstruct,
  Remove,
  Set,
  SourceSpan,
  Target,
  ThroughSegment,
  Value,
} from "./generated/lgl-object-schema.js";

export type LglText = string;

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
