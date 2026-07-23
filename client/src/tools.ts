import {
  createSal,
  objectResultToTextResult,
  type Diagnostic,
  type MutationResult,
  type Query,
  type Result,
  type Sal,
  type SalExecutionOptions,
  type TextResult,
} from "@loomle/sal";
import { catalog, guide } from "@loomle/interfaces";
import type { ProjectController, ProjectReport } from "./runtime.js";
import { RuntimeRpcError, type RpcInvoker } from "./runtime-rpc.js";

export type PublicToolName = "project" | "sal_query" | "sal_patch" | "sal_schema" | "editor_context";

export interface ToolDefinition {
  name: PublicToolName;
  description: string;
  inputSchema: Record<string, unknown>;
  annotations: {
    readOnlyHint: boolean;
    destructiveHint: boolean;
    idempotentHint: boolean;
  };
}

export interface McpTextContent {
  type: "text";
  text: string;
}

export interface McpToolResult {
  [key: string]: unknown;
  content: McpTextContent[];
  isError?: boolean;
}

const interfaceNames = catalog.map(({ name }) => name);

export const toolDefinitions: readonly ToolDefinition[] = [
  {
    name: "project",
    description: "Inspect Loomle projects or bind this MCP session to one project. Call with no arguments to see the binding and candidates; pass projectId or projectRoot to bind. Binding is sticky, survives Editor restarts, and never falls through to another project while offline.",
    inputSchema: {
      type: "object",
      properties: {
        projectId: {
          type: "string",
          minLength: 1,
          description: "Stable project ID returned by project.",
        },
        projectRoot: {
          type: "string",
          minLength: 1,
          description: "Directory containing exactly one .uproject file.",
        },
      },
      maxProperties: 1,
      additionalProperties: false,
    },
    annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: true },
  },
  {
    name: "sal_query",
    description: "Read Unreal Engine objects with one self-contained SAL Query Text. Returns ordered SAL Object Text.",
    inputSchema: textInputSchema("Self-contained SAL Query Text."),
    annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true },
  },
  {
    name: "sal_patch",
    description: "Validate or modify Unreal Engine objects with one ordered SAL Patch Text. Use 'dry run' before risky edits.",
    inputSchema: textInputSchema("Self-contained SAL Patch Text."),
    annotations: { readOnlyHint: false, destructiveHint: true, idempotentHint: false },
  },
  {
    name: "sal_schema",
    description: guide,
    inputSchema: {
      type: "object",
      properties: {
        module: {
          type: "string",
          enum: [...interfaceNames],
          description: "Optional interface module.",
        },
      },
      additionalProperties: false,
    },
    annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true },
  },
  {
    name: "editor_context",
    description: "Return the user's current Unreal Editor interaction target as compact, ordered SAL Object Text.",
    inputSchema: { type: "object", properties: {}, additionalProperties: false },
    annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true },
  },
];

export class SalToolService {
  private readonly sal: Sal;

  constructor(private readonly rpc: RpcInvoker & Partial<ProjectController>) {
    this.sal = createSal({
      catalog,
      executor: {
        interfaces: interfaceNames,
        query: async (object: Query, options?: SalExecutionOptions) => (
          this.rpc.invoke("sal.query", { object }, options?.signal) as Promise<Result>
        ),
        patch: async (object, options?: SalExecutionOptions) => (
          this.rpc.invoke("sal.patch", { object }, options?.signal) as Promise<MutationResult>
        ),
      },
    });
  }

  async call(name: string, args: unknown, signal?: AbortSignal): Promise<McpToolResult> {
    try {
      const object = requireArguments(args);
      switch (name) {
        case "project": {
          requireOnly(object, ["projectId", "projectRoot"], name);
          const projectId = optionalString(object.projectId, "projectId");
          const projectRoot = optionalString(object.projectRoot, "projectRoot");
          if (projectId && projectRoot) {
            throw new ToolInputError("project accepts either projectId or projectRoot, not both.");
          }
          if (!this.rpc.project) {
            throw new RuntimeRpcError(
              "runtime.client_error",
              "This Loomle Client does not provide project binding.",
            );
          }
          return projectResult(await this.rpc.project({ projectId, projectRoot }));
        }
        case "sal_query":
          return toMcpResult(await this.sal.query(requireText(object, name), { signal }));
        case "sal_patch":
          // Once a mutation has been dispatched, abandoning the MCP wait must
          // not be reported as if Unreal rolled the edit back. Mutation
          // cancellation needs its own apply-boundary contract; keep runtime
          // cancellation scoped to read-only queries for now.
          return toMcpResult(await this.sal.patch(requireText(object, name)));
        case "sal_schema":
          requireOnly(object, ["module"], name);
          return toMcpResult(await this.sal.schema(optionalString(object.module, "module")));
        case "editor_context":
          requireOnly(object, [], name);
          return toMcpResult(await objectResultToTextResult(
            await this.rpc.invoke("editor.context", {}, signal),
          ));
        default:
          return toolFailure("tool.unknown", `Unknown Loomle tool: ${name}.`);
      }
    } catch (error) {
      return name === "project" ? projectFailureFromError(error) : toolFailureFromError(error);
    }
  }

  setMcpRoots(roots: readonly string[] | undefined, supported: boolean): void {
    this.rpc.setMcpRoots?.(roots, supported);
  }
}

function projectResult(report: ProjectReport): McpToolResult {
  const lines = [
    `bound: ${report.boundProjectId ?? "none"}`,
    "projects:",
    ...report.projects.map((project) => [
      `- ${project.projectId}`,
      `  name: ${project.name}`,
      `  projectRoot: ${project.projectRoot}`,
      `  status: ${project.status}`,
      `  bound: ${project.bound}`,
    ].join("\n")),
  ];
  if (report.projects.length === 0) {
    lines.push("  none");
  } else if (!report.boundProjectId) {
    lines.push("next: call project with one projectId or projectRoot to bind this session");
  }
  return { content: [{ type: "text", text: lines.join("\n") }] };
}

function projectFailureFromError(error: unknown): McpToolResult {
  const code = error instanceof RuntimeRpcError ? error.code : errorCode(error);
  const message = error instanceof Error ? error.message : String(error);
  const detail = error instanceof RuntimeRpcError && error.detail
    ? `\ndetail: ${error.detail}`
    : "";
  return {
    content: [{ type: "text", text: `ERROR ${code}: ${message}${detail}` }],
    isError: true,
  };
}

export function toMcpResult(result: TextResult): McpToolResult {
  const sections: string[] = [];
  if (result.text !== undefined && result.text.length > 0) sections.push(result.text);

  const metadata = formatMetadata(result);
  if (metadata) sections.push(metadata);
  if (result.diagnostics.length > 0) {
    sections.push(formatDiagnostics(result.diagnostics));
  }
  if (sections.length === 0) sections.push(salComment("SAL returned no Object Text."));

  const isError = result.isError === true
    || result.diagnostics.some((diagnostic) => diagnostic.severity === "error");
  return {
    content: [{ type: "text", text: sections.join("\n\n") }],
    ...(isError ? { isError: true } : {}),
  };
}

function textInputSchema(description: string): Record<string, unknown> {
  return {
    type: "object",
    properties: {
      text: { type: "string", minLength: 1, description },
    },
    required: ["text"],
    additionalProperties: false,
  };
}

function requireArguments(value: unknown): Record<string, unknown> {
  if (value === undefined) return {};
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    throw new ToolInputError("Tool arguments must be an object.");
  }
  return value as Record<string, unknown>;
}

function requireText(object: Record<string, unknown>, tool: string): string {
  requireOnly(object, ["text"], tool);
  if (typeof object.text !== "string" || object.text.trim().length === 0) {
    throw new ToolInputError(`${tool} requires non-empty text.`);
  }
  return object.text;
}

function requireOnly(object: Record<string, unknown>, keys: readonly string[], tool: string): void {
  const allowed = new Set(keys);
  const unknown = Object.keys(object).filter((key) => !allowed.has(key));
  if (unknown.length > 0) {
    throw new ToolInputError(`${tool} does not accept: ${unknown.join(", ")}.`);
  }
}

function optionalString(value: unknown, name: string): string | undefined {
  if (value === undefined) return undefined;
  if (typeof value !== "string" || value.trim().length === 0) {
    throw new ToolInputError(`${name} must be a non-empty string.`);
  }
  return value;
}

function formatDiagnostics(diagnostics: readonly Diagnostic[]): string {
  const text = ["SAL diagnostics", ...diagnostics.map((diagnostic) => {
    const lines = [
      `${diagnostic.severity.toUpperCase()} ${diagnostic.code}: ${diagnostic.message}`,
    ];
    if (diagnostic.span !== undefined) {
      const length = diagnostic.span.length === undefined ? "" : `, length ${diagnostic.span.length}`;
      lines.push(`  at: line ${diagnostic.span.line}, column ${diagnostic.span.column}${length}`);
    }
    if (diagnostic.path !== undefined) lines.push(`  path: ${formatDiagnosticValue(diagnostic.path)}`);
    if (diagnostic.domain !== undefined) lines.push(`  domain: ${diagnostic.domain}`);
    if (diagnostic.operation !== undefined) lines.push(`  operation: ${diagnostic.operation}`);
    if (diagnostic.ref !== undefined) lines.push(`  ref: ${diagnostic.ref}`);
    if (diagnostic.expected !== undefined) lines.push(`  expected: ${formatDiagnosticValue(diagnostic.expected)}`);
    if (diagnostic.actual !== undefined) lines.push(`  actual: ${formatDiagnosticValue(diagnostic.actual)}`);
    if (diagnostic.supported !== undefined) lines.push(`  supported: ${formatDiagnosticValue(diagnostic.supported)}`);
    if (diagnostic.matches !== undefined) lines.push(`  matches: ${formatDiagnosticValue(diagnostic.matches)}`);
    if (diagnostic.suggestion !== undefined) lines.push(`  next: ${diagnostic.suggestion}`);
    return lines.join("\n");
  })].join("\n");
  return salComment(text);
}

function formatDiagnosticValue(value: unknown): string {
  try {
    return JSON.stringify(value) ?? String(value);
  } catch {
    return String(value);
  }
}

function formatMetadata(result: TextResult): string | undefined {
  const lines: string[] = [];
  if (result.operation !== undefined) lines.push(`operation: ${result.operation}`);
  if (result.dryRun !== undefined) lines.push(`dryRun: ${result.dryRun}`);
  if (result.valid !== undefined) lines.push(`valid: ${result.valid}`);
  if (result.applied !== undefined) lines.push(`applied: ${result.applied}`);
  if (result.assetPath !== undefined) lines.push(`assetPath: ${result.assetPath}`);
  if (result.previousRevision !== undefined) lines.push(`previousRevision: ${result.previousRevision}`);
  if (result.newRevision !== undefined) lines.push(`newRevision: ${result.newRevision}`);
  if (result.resolvedRefs !== undefined) lines.push(`resolvedRefs: ${formatDiagnosticValue(result.resolvedRefs)}`);
  if (result.planned !== undefined) lines.push(`planned: ${formatDiagnosticValue(result.planned)}`);
  if (result.diff !== undefined) lines.push(`diff: ${formatDiagnosticValue(result.diff)}`);
  if (result.page?.next !== undefined) lines.push(`next: ${result.page.next}`);
  return lines.length > 0 ? salComment(["SAL result", ...lines].join("\n")) : undefined;
}

function salComment(text: string): string {
  // A diagnostic value can contain arbitrary user text. Fall back to
  // independent line comments if it contains the block delimiter so the
  // complete MCP response always remains valid SAL Object Text.
  return text.includes("###")
    ? text.split("\n").map((line) => line.trim().length === 0 ? "" : `# ${line}`).join("\n")
    : `###\n${text}\n###`;
}

function toolFailure(code: string, message: string): McpToolResult {
  const diagnostic: Diagnostic = { severity: "error", code, message };
  return toMcpResult({ diagnostics: [diagnostic] });
}

function toolFailureFromError(error: unknown): McpToolResult {
  const message = error instanceof Error ? error.message : String(error);
  if (!(error instanceof RuntimeRpcError)) {
    return toolFailure(errorCode(error), message);
  }

  const lines = [message];
  if (error.detail !== undefined) lines.push(`  detail: ${error.detail}`);
  lines.push(`  retryable: ${error.retryable}`);
  const diagnostic: Diagnostic = {
    severity: "error",
    code: String(error.code),
    message: lines.join("\n"),
    ...(error.retryable ? {
      suggestion: "Re-check the current Editor and object state before retrying. Never blindly replay a Patch after a lost response.",
    } : {}),
  };
  return toMcpResult({ diagnostics: [diagnostic] });
}

function errorCode(error: unknown): string {
  if (error instanceof ToolInputError) return "tool.invalid_arguments";
  return "runtime.client_error";
}

class ToolInputError extends Error {}
