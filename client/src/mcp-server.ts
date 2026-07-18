import { loadSalGuide } from "@loomle/sal";
import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  CallToolRequestSchema,
  ErrorCode,
  ListToolsRequestSchema,
  McpError,
} from "@modelcontextprotocol/sdk/types.js";
import { SalToolService, toolDefinitions } from "./tools.js";

const publicToolNames = new Set<string>(toolDefinitions.map((tool) => tool.name));

export async function createMcpServer(service: SalToolService): Promise<Server> {
  const instructions = await loadSalGuide();

  const server = new Server(
    { name: "loomle", version: "0.7.0-dev.1" },
    {
      capabilities: { tools: {} },
      instructions,
    },
  );

  server.setRequestHandler(ListToolsRequestSchema, async () => ({
    tools: toolDefinitions,
  }));
  server.setRequestHandler(CallToolRequestSchema, async (request) => {
    if (!publicToolNames.has(request.params.name)) {
      throw new McpError(
        ErrorCode.InvalidParams,
        `Unknown Loomle tool: ${request.params.name}.`,
      );
    }
    return service.call(request.params.name, request.params.arguments);
  });

  return server;
}

export async function runMcpServer(service: SalToolService): Promise<void> {
  const server = await createMcpServer(service);
  const transport = new StdioServerTransport();
  await server.connect(transport);
}
