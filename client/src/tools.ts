import {
  createSal,
  objectResultToTextResult,
  parseCanonicalTargetText,
  unresolvedTextResult,
  type Diagnostic,
  type PatchResult,
  type Query,
  type QueryResult,
  type Sal,
  type SalExecutionOptions,
  type TextResult,
} from "@loomle/sal";
import { catalog, guide } from "@loomle/interfaces";
import { agentSkills } from "./generated/agent-skills.js";
import type {
  ProjectController,
  ProjectReport,
  SessionStatusController,
} from "./runtime.js";
import { RuntimeRpcError, type RpcInvoker } from "./runtime-rpc.js";
import {
  ClientStatusService,
  type ClientStatusReport,
  type StatusProvider,
} from "./status.js";

export type PublicToolName =
  | "status"
  | "project"
  | "sal_query"
  | "sal_patch"
  | "sal_schema"
  | "agent_skill"
  | "editor"
  | "python";

export interface ToolDefinition {
  name: PublicToolName;
  description: string;
  inputSchema: Record<string, unknown>;
  outputSchema?: Record<string, unknown>;
  annotations: {
    readOnlyHint: boolean;
    destructiveHint: boolean;
    idempotentHint: boolean;
    openWorldHint?: boolean;
  };
}

export interface McpTextContent {
  type: "text";
  text: string;
}

export interface McpToolResult {
  [key: string]: unknown;
  content: McpTextContent[];
  structuredContent?: Record<string, unknown>;
  isError?: boolean;
}

type PythonExecutionStatus = "running" | "succeeded" | "failed" | "lost";

interface PythonExecutionResult extends Record<string, unknown> {
  status: PythonExecutionStatus;
  stateMayHaveChanged: boolean;
}

const pythonInputSchema: Record<string, unknown> = {
  type: "object",
  oneOf: [
    {
      properties: {
        operation: { const: "run" },
        script: {
          type: "string",
          minLength: 1,
          maxLength: 262_144,
          description: "Inline Unreal Editor Python defining one synchronous run() entry point.",
        },
      },
      required: ["operation", "script"],
      additionalProperties: false,
    },
    {
      properties: {
        operation: { const: "poll" },
        executionId: {
          type: "string",
          minLength: 1,
          description: "Opaque handle returned by an earlier running result.",
        },
      },
      required: ["operation", "executionId"],
      additionalProperties: false,
    },
  ],
};

const pythonOutputSchema: Record<string, unknown> = {
  type: "object",
  required: ["status", "stateMayHaveChanged"],
  properties: {
    status: { type: "string", enum: ["running", "succeeded", "failed", "lost"] },
    executionId: { type: "string", minLength: 1 },
    stateMayHaveChanged: { type: "boolean" },
    result: { type: "object", additionalProperties: true },
    error: {
      type: "object",
      required: ["code", "phase", "message", "retryable"],
      properties: {
        code: { type: "string", minLength: 1 },
        phase: {
          type: "string",
          enum: ["validation", "staging", "execution", "result", "runtime"],
        },
        message: { type: "string", minLength: 1 },
        traceback: { type: "string" },
        retryable: { type: "boolean" },
      },
      additionalProperties: false,
    },
    logs: {
      type: "array",
      maxItems: 1_000,
      items: {
        type: "object",
        required: ["type", "output"],
        properties: {
          type: { type: "string", enum: ["info", "warning", "error"] },
          output: { type: "string" },
        },
        additionalProperties: false,
      },
    },
    logsTruncated: { type: "boolean" },
    durationMs: { type: "integer", minimum: 0 },
    elapsedMs: { type: "integer", minimum: 0 },
    continuation: {
      type: "object",
      required: ["tool", "arguments", "pollAfterMs"],
      properties: {
        tool: { const: "python" },
        arguments: {
          type: "object",
          required: ["operation", "executionId"],
          properties: {
            operation: { const: "poll" },
            executionId: { type: "string", minLength: 1 },
          },
          additionalProperties: false,
        },
        pollAfterMs: { type: "integer", minimum: 0 },
      },
      additionalProperties: false,
    },
  },
  additionalProperties: false,
};

const interfaceNames = catalog.map(({ name }) => name);
const agentSkillNames = agentSkills.map(({ name }) => name);
const agentSkillDescription = [
  "Load a Loomle Agent Skill when the task matches its description. Call with no arguments to list resident Skills or with one exact name to load its complete instructions and Markdown references.",
  "Resident Skills:",
  ...agentSkills.map(({ name, description }) => `- ${name}: ${description}`),
].join("\n");

export const toolDefinitions: readonly ToolDefinition[] = [
  {
    name: "status",
    description: "Inspect Loomle Client and update status plus the bound session and Bridge health. Call once before the first Loomle operation in a task.",
    inputSchema: { type: "object", properties: {}, additionalProperties: false },
    annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true },
  },
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
    description: "Read Unreal Engine objects with one self-contained SAL Query Text. The first text block is canonical SAL Result Text; diagnostics use later text blocks.",
    inputSchema: textInputSchema("Self-contained SAL Query Text."),
    annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true },
  },
  {
    name: "sal_patch",
    description: "Validate or modify Unreal Engine objects with one ordered SAL Patch Text. The first result block is canonical SAL Result Text; metadata and diagnostics use later blocks. Use 'dry run' before risky edits.",
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
    name: "agent_skill",
    description: agentSkillDescription,
    inputSchema: {
      type: "object",
      properties: {
        name: {
          type: "string",
          enum: [...agentSkillNames],
          description: "Optional exact resident Agent Skill name.",
        },
      },
      additionalProperties: false,
    },
    annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true },
  },
  {
    name: "editor",
    description: "Observe or control the Unreal Blueprint Editor. Call with no arguments for current context, or use open/close with one bare canonical SAL Blueprint or Graph Target expression.",
    inputSchema: {
      type: "object",
      properties: {
        operation: {
          type: "string",
          enum: ["context", "open", "close"],
          description: "Defaults to context when omitted. A target without an explicit open or close operation is invalid.",
        },
        target: {
          type: "string",
          minLength: 1,
          description: "One bare canonical SAL Blueprint or Graph Target expression. Required for open and close; invalid for context.",
        },
      },
      additionalProperties: false,
    },
    annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: true },
  },
  {
    name: "python",
    description: "Run full Python inside the bound Unreal Editor only when no structured Loomle interface covers the required UE capability. Use run normally. If it returns running, follow its poll continuation exactly and never replay the script. No dry run, rollback, safe cancellation, or idempotency.",
    inputSchema: pythonInputSchema,
    outputSchema: pythonOutputSchema,
    annotations: {
      readOnlyHint: false,
      destructiveHint: true,
      idempotentHint: false,
      openWorldHint: true,
    },
  },
];

export class SalToolService {
  private readonly sal: Sal;
  private readonly status: StatusProvider;

  constructor(
    private readonly rpc: RpcInvoker & Partial<ProjectController & SessionStatusController>,
    status?: StatusProvider,
  ) {
    this.status = status ?? new ClientStatusService(rpc);
    this.sal = createSal({
      catalog,
      executor: {
        interfaces: interfaceNames,
        query: async (object: Query, options?: SalExecutionOptions) => (
          this.rpc.invoke("sal.query", { object }, options?.signal) as Promise<QueryResult>
        ),
        patch: async (object, options?: SalExecutionOptions) => (
          this.rpc.invoke("sal.patch", { object }, options?.signal) as Promise<PatchResult>
        ),
      },
    });
  }

  async call(name: string, args: unknown, signal?: AbortSignal): Promise<McpToolResult> {
    try {
      const object = requireArguments(args);
      switch (name) {
        case "status":
          requireOnly(object, [], name);
          return statusResult(await this.status.report());
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
        case "agent_skill":
          requireOnly(object, ["name"], name);
          return agentSkillResult(optionalString(object.name, "name"));
        case "editor":
          return await this.callEditor(object, signal);
        case "python":
          return await this.callPython(object, signal);
        default:
          return toolFailure("tool.unknown", `Unknown Loomle tool: ${name}.`);
      }
    } catch (error) {
      if (name === "project") return projectFailureFromError(error);
      if (name === "editor") {
        return toMcpResult(editorTextFailureFromError(error));
      }
      if (name === "python") return pythonToolFailureFromError(error);
      return isResultTool(name)
        ? resultToolFailureFromError(error)
        : toolFailureFromError(error);
    }
  }

  setMcpRoots(roots: readonly string[] | undefined, supported: boolean): void {
    this.rpc.setMcpRoots?.(roots, supported);
  }

  private async callEditor(
    object: Record<string, unknown>,
    signal?: AbortSignal,
  ): Promise<McpToolResult> {
    requireOnly(object, ["operation", "target"], "editor");
    const operation = optionalEditorOperation(object.operation);

    if (operation === undefined) {
      if (object.target !== undefined) {
        throw new ToolInputError("editor requires an explicit open or close operation when target is provided.");
      }
      return toMcpResult(await objectResultToTextResult(
        await this.rpc.invoke("editor.context", {}, signal),
      ));
    }

    if (operation === "context") {
      if (object.target !== undefined) {
        throw new ToolInputError("editor context does not accept target.");
      }
      return toMcpResult(await objectResultToTextResult(
        await this.rpc.invoke("editor.context", {}, signal),
      ));
    }

    const targetText = optionalString(object.target, "target");
    if (targetText === undefined) {
      throw new ToolInputError(`editor ${operation} requires target.`);
    }

    const parsed = parseCanonicalTargetText(targetText);
    if (parsed.target === undefined) {
      return editorControlMcpResult(
        unresolvedTextResult(parsed.diagnostics),
        { operation, status: "failed" },
      );
    }

    try {
      const response = requireEditorControlResult(
        await this.rpc.invoke(`editor.${operation}`, { target: parsed.target }, signal),
        operation,
      );
      const subject = await objectResultToTextResult(response.subject);
      if (response.outcome.status === "failed"
        && !subject.diagnostics.some(({ severity }) => severity === "error")) {
        throw new Error("Editor control returned failed without an error diagnostic.");
      }
      if (response.outcome.status !== "failed"
        && subject.diagnostics.some(({ severity }) => severity === "error")) {
        throw new Error("Editor control returned a successful status with an error diagnostic.");
      }
      if (response.outcome.status !== "failed"
        && subject.targetContext !== "exact_target") {
        throw new Error("Editor control returned a successful status without an exact Target.");
      }
      return editorControlMcpResult(subject, response.outcome);
    } catch (error) {
      return editorControlMcpResult(
        editorTextFailureFromError(error),
        { operation, status: "failed" },
      );
    }
  }

  private async callPython(
    object: Record<string, unknown>,
    signal?: AbortSignal,
  ): Promise<McpToolResult> {
    const operation = requirePythonOperation(object.operation);
    if (operation === "run") {
      requireOnly(object, ["operation", "script"], "python run");
      const script = requireBoundedString(object.script, "script", 262_144);
      // Once admitted, raw Unreal Python cannot be safely cancelled. Do not
      // forward the host AbortSignal into this mutating execution.
      return pythonMcpResult(requirePythonExecutionResult(
        await this.rpc.invoke("python.run", { script }),
      ));
    }

    requireOnly(object, ["operation", "executionId"], "python poll");
    const executionId = requireBoundedString(object.executionId, "executionId", 256);
    return pythonMcpResult(requirePythonExecutionResult(
      await this.rpc.invoke("python.poll", { executionId }, signal),
    ));
  }
}

type EditorOperation = "context" | EditorControlOperation;
type EditorControlOperation = "open" | "close";
type EditorOpenStatus = "opened" | "focused" | "already_focused" | "failed";
type EditorCloseStatus = "closed" | "already_closed" | "failed";
type EditorControlOutcome =
  | { operation: "open"; status: EditorOpenStatus }
  | { operation: "close"; status: EditorCloseStatus };

interface EditorControlResult {
  subject: unknown;
  outcome: EditorControlOutcome;
}

function agentSkillResult(name: string | undefined): McpToolResult {
  if (name === undefined) {
    return {
      content: [{
        type: "text",
        text: [
          "agent_skills:",
          ...agentSkills.flatMap((skill) => [
            `- name: ${skill.name}`,
            `  description: ${JSON.stringify(skill.description)}`,
          ]),
          "next: call agent_skill with one exact name when its description matches the task",
        ].join("\n"),
      }],
    };
  }

  const skill = agentSkills.find((candidate) => candidate.name === name);
  if (!skill) throw new ToolInputError(`Unknown Loomle Agent Skill: ${name}.`);
  return {
    content: skill.files.map((file) => ({
      type: "text" as const,
      text: `agent_skill: ${skill.name}\nfile: ${file.path}\n\n${file.text}`,
    })),
  };
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

function statusResult(report: ClientStatusReport): McpToolResult {
  const lines = [
    "client:",
    `  version: ${report.client.version}`,
    `  pid: ${report.client.pid}`,
    `  target: ${report.client.target ?? "unsupported"}`,
    `  executable: ${JSON.stringify(report.client.executable)}`,
    "update:",
    `  status: ${report.update.status}`,
  ];
  if (report.update.version) lines.push(`  version: ${report.update.version}`);
  if (report.update.releaseUrl) lines.push(`  release: ${JSON.stringify(report.update.releaseUrl)}`);
  if (report.update.assetUrl) lines.push(`  asset: ${JSON.stringify(report.update.assetUrl)}`);
  if (report.update.sha256) lines.push(`  sha256: ${report.update.sha256}`);
  if (report.update.reason) lines.push(`  reason: ${report.update.reason}`);

  lines.push("session:");
  lines.push(`  project: ${report.session.project?.projectId ?? "none"}`);
  if (report.session.project?.name) lines.push(`  name: ${JSON.stringify(report.session.project.name)}`);
  if (report.session.project?.projectRoot) {
    lines.push(`  projectRoot: ${JSON.stringify(report.session.project.projectRoot)}`);
  }
  lines.push(`  status: ${report.session.status}`);
  if (report.session.reason) lines.push(`  reason: ${report.session.reason}`);

  if (report.session.bridge) {
    lines.push("bridge:");
    if (report.session.bridge.version) {
      lines.push(`  version: ${report.session.bridge.version}`);
    }
    if (report.session.bridge.protocolVersion !== undefined) {
      lines.push(`  protocolVersion: ${report.session.bridge.protocolVersion}`);
    }
    if (report.session.bridge.pluginPath) {
      lines.push(`  plugin: ${JSON.stringify(report.session.bridge.pluginPath)}`);
    }
    if (report.session.bridge.installScope) {
      lines.push(`  installScope: ${report.session.bridge.installScope}`);
    }
    if (report.session.bridge.managedBy) {
      lines.push(`  managedBy: ${report.session.bridge.managedBy}`);
    }
  }

  if (report.update.status === "available") {
    const shared = "Ask the user before updating. After approval, ensure affected Unreal Editors are closed, ";
    lines.push(report.client.platform === "win32"
      ? `next: ${shared}use a normal PowerShell to find Loomle Client processes with the executable path above and stop each with Stop-Process -Id <pid>, replace the complete plugin, then restart the MCP Server.`
      : `next: ${shared}replace the complete plugin, then restart the MCP Server.`);
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
  const sections: McpTextContent[] = [];
  if (result.text !== undefined && result.text.length > 0) {
    sections.push({ type: "text", text: result.text });
  }

  const metadata = formatMetadata(result);
  if (metadata) sections.push({ type: "text", text: metadata });
  if (result.diagnostics.length > 0) {
    sections.push({ type: "text", text: formatDiagnostics(result.diagnostics) });
  }
  if (sections.length === 0) {
    sections.push({ type: "text", text: salComment("SAL returned no Object Text.") });
  }

  const isError = result.isError === true
    || result.diagnostics.some((diagnostic) => diagnostic.severity === "error");
  return {
    content: sections,
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

function requireBoundedString(value: unknown, name: string, maxLength: number): string {
  if (typeof value !== "string" || value.trim().length === 0) {
    throw new ToolInputError(`${name} must be a non-empty string.`);
  }
  if (value.length > maxLength) {
    throw new ToolInputError(`${name} must not exceed ${maxLength} characters.`);
  }
  return value;
}

function requirePythonOperation(value: unknown): "run" | "poll" {
  if (value === "run" || value === "poll") return value;
  throw new ToolInputError("python operation must be run or poll.");
}

function optionalEditorOperation(value: unknown): EditorOperation | undefined {
  if (value === undefined) return undefined;
  if (value === "context" || value === "open" || value === "close") return value;
  throw new ToolInputError("operation must be context, open, or close.");
}

function requireEditorControlResult(
  value: unknown,
  expectedOperation: EditorControlOperation,
): EditorControlResult {
  if (!isRecord(value)
    || !hasExactKeys(value, ["subject", "outcome"])
    || !isRecord(value.outcome)
    || !hasExactKeys(value.outcome, ["operation", "status"])) {
    throw new Error("Editor control returned an invalid result wrapper.");
  }
  if (value.outcome.operation !== expectedOperation) {
    throw new Error("Editor control returned an outcome for the wrong operation.");
  }
  const allowedStatuses = expectedOperation === "open"
    ? new Set<unknown>(["opened", "focused", "already_focused", "failed"])
    : new Set<unknown>(["closed", "already_closed", "failed"]);
  if (!allowedStatuses.has(value.outcome.status)) {
    throw new Error("Editor control returned an invalid terminal status.");
  }
  return value as unknown as EditorControlResult;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function hasExactKeys(value: Record<string, unknown>, expected: readonly string[]): boolean {
  const keys = Object.keys(value);
  return keys.length === expected.length && expected.every((key) => keys.includes(key));
}

function editorControlMcpResult(
  subject: TextResult,
  outcome: EditorControlOutcome,
): McpToolResult {
  const text = subject.text ?? "result unresolved_target\nno_objects";
  const content: McpTextContent[] = [
    { type: "text", text },
    {
      type: "text",
      text: salComment([
        "Editor result",
        `operation: ${outcome.operation}`,
        `status: ${outcome.status}`,
      ].join("\n")),
    },
  ];
  if (subject.diagnostics.length > 0) {
    content.push({ type: "text", text: formatDiagnostics(subject.diagnostics) });
  }
  const isError = subject.isError === true
    || subject.diagnostics.some(({ severity }) => severity === "error");
  return {
    content,
    ...(isError ? { isError: true } : {}),
  };
}

function pythonMcpResult(result: PythonExecutionResult): McpToolResult {
  const isError = result.status === "failed" || result.status === "lost";
  const text = result.status === "running"
    ? [
      `Python execution ${String(result.executionId)} is still running.`,
      "Do not run the script again.",
      `Call python with ${JSON.stringify((result.continuation as { arguments: unknown }).arguments)}.`,
    ].join("\n")
    : JSON.stringify(result);
  return {
    content: [{ type: "text", text }],
    structuredContent: result,
    ...(isError ? { isError: true } : {}),
  };
}

function requirePythonExecutionResult(value: unknown): PythonExecutionResult {
  if (!isRecord(value)) throw new Error("Python returned an invalid execution result.");
  requireExactAllowedKeys(value, [
    "status",
    "executionId",
    "stateMayHaveChanged",
    "result",
    "error",
    "logs",
    "logsTruncated",
    "durationMs",
    "elapsedMs",
    "continuation",
  ], "Python execution result");
  const status = value.status;
  if (status !== "running" && status !== "succeeded" && status !== "failed" && status !== "lost") {
    throw new Error("Python returned an invalid execution status.");
  }
  if (typeof value.stateMayHaveChanged !== "boolean") {
    throw new Error("Python execution result is missing stateMayHaveChanged.");
  }
  const hasExecutionId = typeof value.executionId === "string" && value.executionId.length > 0;
  if (value.executionId !== undefined && !hasExecutionId) {
    throw new Error("Python execution result has an invalid executionId.");
  }

  if (status === "running") {
    if (!hasExecutionId || !isNonNegativeInteger(value.elapsedMs)) {
      throw new Error("Running Python execution is missing its handle or elapsed time.");
    }
    requirePythonContinuation(value.continuation, value.executionId as string);
    requireAbsent(value, ["result", "error", "logs", "logsTruncated", "durationMs"], status);
  } else if (status === "succeeded") {
    if (!isRecord(value.result) || !isJsonCompatible(value.result)) {
      throw new Error("Successful Python execution returned an invalid result object.");
    }
    requirePythonTerminalOutput(value);
    requireAbsent(value, ["error", "elapsedMs", "continuation"], status);
  } else if (status === "failed") {
    requirePythonError(value.error);
    if (value.logs !== undefined || value.logsTruncated !== undefined || value.durationMs !== undefined) {
      requirePythonTerminalOutput(value);
    }
    requireAbsent(value, ["result", "elapsedMs", "continuation"], status);
  } else {
    if (!hasExecutionId) throw new Error("Lost Python execution is missing executionId.");
    requirePythonError(value.error);
    requireAbsent(value, ["result", "logs", "logsTruncated", "durationMs", "elapsedMs", "continuation"], status);
  }
  return value as PythonExecutionResult;
}

function requirePythonTerminalOutput(value: Record<string, unknown>): void {
  if (!Array.isArray(value.logs) || value.logs.length > 1_000
    || !value.logs.every(isPythonLogEntry)
    || typeof value.logsTruncated !== "boolean"
    || !isNonNegativeInteger(value.durationMs)) {
    throw new Error("Python execution returned invalid terminal output.");
  }
}

function requirePythonError(value: unknown): void {
  if (!isRecord(value)
    || !hasExactKeys(value, value.traceback === undefined
      ? ["code", "phase", "message", "retryable"]
      : ["code", "phase", "message", "traceback", "retryable"])
    || typeof value.code !== "string"
    || value.code.length === 0
    || !["validation", "staging", "execution", "result", "runtime"].includes(String(value.phase))
    || typeof value.message !== "string"
    || value.message.length === 0
    || (value.traceback !== undefined && typeof value.traceback !== "string")
    || typeof value.retryable !== "boolean") {
    throw new Error("Python execution returned an invalid error object.");
  }
}

function requirePythonContinuation(value: unknown, executionId: string): void {
  if (!isRecord(value)
    || !hasExactKeys(value, ["tool", "arguments", "pollAfterMs"])
    || value.tool !== "python"
    || !isRecord(value.arguments)
    || !hasExactKeys(value.arguments, ["operation", "executionId"])
    || value.arguments.operation !== "poll"
    || value.arguments.executionId !== executionId
    || !isNonNegativeInteger(value.pollAfterMs)) {
    throw new Error("Running Python execution returned an invalid continuation.");
  }
}

function isPythonLogEntry(value: unknown): boolean {
  return isRecord(value)
    && hasExactKeys(value, ["type", "output"])
    && (value.type === "info" || value.type === "warning" || value.type === "error")
    && typeof value.output === "string";
}

function isJsonCompatible(value: unknown, seen = new Set<object>()): boolean {
  if (value === null || typeof value === "string" || typeof value === "boolean") return true;
  if (typeof value === "number") {
    return Number.isFinite(value) && (!Number.isInteger(value) || Number.isSafeInteger(value));
  }
  if (typeof value !== "object") return false;
  if (seen.has(value)) return false;
  seen.add(value);
  const valid = Array.isArray(value)
    ? value.every((entry) => isJsonCompatible(entry, seen))
    : Object.values(value as Record<string, unknown>).every((entry) => isJsonCompatible(entry, seen));
  seen.delete(value);
  return valid;
}

function isNonNegativeInteger(value: unknown): value is number {
  return typeof value === "number" && Number.isSafeInteger(value) && value >= 0;
}

function requireAbsent(
  value: Record<string, unknown>,
  keys: readonly string[],
  status: string,
): void {
  const present = keys.filter((key) => value[key] !== undefined);
  if (present.length > 0) {
    throw new Error(`Python ${status} result must not contain: ${present.join(", ")}.`);
  }
}

function requireExactAllowedKeys(
  value: Record<string, unknown>,
  allowedKeys: readonly string[],
  label: string,
): void {
  const allowed = new Set(allowedKeys);
  const unknown = Object.keys(value).filter((key) => !allowed.has(key));
  if (unknown.length > 0) throw new Error(`${label} contains unknown fields: ${unknown.join(", ")}.`);
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

function pythonToolFailureFromError(error: unknown): McpToolResult {
  const code = error instanceof RuntimeRpcError ? error.code : errorCode(error);
  const message = error instanceof Error ? error.message : String(error);
  const lines = [`ERROR ${code}: ${message}`];
  if (error instanceof RuntimeRpcError) {
    if (error.detail !== undefined) lines.push(`detail: ${error.detail}`);
    lines.push(`retryable: ${error.retryable}`);
  }
  return {
    content: [{ type: "text", text: lines.join("\n") }],
    isError: true,
  };
}

function resultToolFailureFromError(error: unknown): McpToolResult {
  return toMcpResult(resultTextFailureFromError(error));
}

function resultTextFailureFromError(error: unknown): TextResult {
  const message = error instanceof Error ? error.message : String(error);
  if (!(error instanceof RuntimeRpcError)) {
    const diagnostic: Diagnostic = {
      severity: "error",
      code: errorCode(error),
      message,
    };
    return unresolvedTextResult([diagnostic]);
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
  return unresolvedTextResult([diagnostic]);
}

function editorTextFailureFromError(error: unknown): TextResult {
  if (!(error instanceof RuntimeRpcError)) return resultTextFailureFromError(error);

  const lines = [error.message];
  if (error.detail !== undefined) lines.push(`  detail: ${error.detail}`);
  lines.push(`  retryable: ${error.retryable}`);
  const diagnostic: Diagnostic = {
    severity: "error",
    code: String(error.code),
    message: lines.join("\n"),
    ...(error.retryable ? {
      suggestion: "Call editor with no arguments to re-read the current presentation before retrying. Open and close are idempotent requested postconditions.",
    } : {}),
  };
  return unresolvedTextResult([diagnostic]);
}

function isResultTool(name: string): boolean {
  return name === "sal_query"
    || name === "sal_patch"
    || name === "editor";
}

function errorCode(error: unknown): string {
  if (error instanceof ToolInputError) return "tool.invalid_arguments";
  return "runtime.client_error";
}

class ToolInputError extends Error {}
