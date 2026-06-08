export type Document = GraphDocument | PatchDocument;

export interface SourceSpan {
  line: number;
  column: number;
}

export interface GraphDocument {
  kind: "graph";
  name: string;
  bindings: UseBinding[];
  statements: GraphStatement[];
}

export interface PatchDocument {
  kind: "patch";
  name: string;
  bindings: UseBinding[];
  statements: PatchStatement[];
}

export type GraphStatement = NodeDeclaration | EdgeStatement;

export type PatchStatement =
  | SetStatement
  | AddStatement
  | RewireStatement
  | ConnectStatement;

export interface NodeDeclaration {
  kind: "node";
  alias: string;
  type: string;
  args: Literal[];
  span: SourceSpan;
}

export interface EdgeStatement {
  kind: "edge";
  from: PinRef;
  to: PinRef;
  span: SourceSpan;
}

export interface SetStatement {
  kind: "set";
  target: PropertyRef;
  value: Literal;
  span: SourceSpan;
}

export interface AddStatement {
  kind: "add";
  node: NodeDeclaration;
  span: SourceSpan;
}

export interface RewireStatement {
  kind: "rewire";
  from: PinRef;
  to: PinRef;
  span: SourceSpan;
}

export interface ConnectStatement {
  kind: "connect";
  from: PinRef;
  to: PinRef;
  span: SourceSpan;
}

export interface PinRef {
  node: string;
  pin: string;
}

export interface PropertyRef {
  node: string;
  property: string;
}

export interface UseBinding {
  kind: "use";
  symbol: string;
  source: BindingSource;
  context?: BindingContext;
  where?: BindingWhere[];
  span: SourceSpan;
}

export type BindingSource =
  | { kind: "palette"; query: string }
  | { kind: "palette_entry"; id: string };

export type BindingContext =
  | { kind: "component"; name: string }
  | { kind: "fromPin"; pin: PinRef };

export interface BindingWhere {
  key: string;
  value: Literal;
}

export type Literal =
  | { kind: "string"; value: string }
  | { kind: "number"; value: number }
  | { kind: "identifier"; value: string }
  | { kind: "boolean"; value: boolean }
  | { kind: "null"; value: null };
