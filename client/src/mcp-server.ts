import { fileURLToPath } from "node:url";
import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  CallToolRequestSchema,
  ErrorCode,
  ListToolsRequestSchema,
  McpError,
  RootsListChangedNotificationSchema,
} from "@modelcontextprotocol/sdk/types.js";
import { productVersion } from "./generated/product-version.js";
import { SalToolService, toolDefinitions } from "./tools.js";

const publicToolNames = new Set<string>(toolDefinitions.map((tool) => tool.name));

export async function createMcpServer(service: SalToolService): Promise<Server> {
  const server = new Server(
    { name: "loomle", version: productVersion },
    {
      capabilities: { tools: {} },
    },
  );

  server.setRequestHandler(ListToolsRequestSchema, async () => ({
    tools: toolDefinitions,
  }));
  server.setRequestHandler(CallToolRequestSchema, async (request, extra) => {
    if (!publicToolNames.has(request.params.name)) {
      throw new McpError(
        ErrorCode.InvalidParams,
        `Unknown Loomle tool: ${request.params.name}.`,
      );
    }
    return service.call(request.params.name, request.params.arguments, extra.signal);
  });

  let rootsRefreshGeneration = 0;
  const refreshRoots = async () => {
    const generation = ++rootsRefreshGeneration;
    const supported = server.getClientCapabilities()?.roots !== undefined;
    // Mark Roots support before awaiting the list so a concurrent tool call
    // never falls back to process.cwd while the authoritative MCP roots are
    // still in flight.
    service.setMcpRoots(undefined, supported);
    if (!supported) return;
    try {
      const result = await server.listRoots();
      if (generation !== rootsRefreshGeneration) return;
      const resolvedRoots: string[] = [];
      let hasUnresolvedRoot = false;
      for (const root of result.roots) {
        try {
          const url = new URL(root.uri);
          if (url.protocol === "file:") resolvedRoots.push(fileURLToPath(url));
          else hasUnresolvedRoot = true;
        } catch {
          hasUnresolvedRoot = true;
        }
      }
      // A non-empty host Root that cannot be mapped to a local path is still
      // authoritative. Keep selection unresolved instead of treating it as an
      // empty workspace and auto-binding an unrelated sole project.
      service.setMcpRoots(hasUnresolvedRoot ? undefined : resolvedRoots, true);
    } catch {
      if (generation !== rootsRefreshGeneration) return;
      // A host that advertises Roots remains authoritative even if one list
      // request fails. Falling back to cwd could bind the wrong project.
      service.setMcpRoots(undefined, true);
    }
  };
  server.oninitialized = () => {
    void refreshRoots();
  };
  server.setNotificationHandler(RootsListChangedNotificationSchema, refreshRoots);

  return server;
}

export async function runMcpServer(service: SalToolService): Promise<void> {
  const server = await createMcpServer(service);
  const transport = new StdioServerTransport();
  await server.connect(transport);
}
