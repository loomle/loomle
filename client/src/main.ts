#!/usr/bin/env node

import { runMcpServer } from "./mcp-server.js";
import { DiscoveredRuntimeInvoker } from "./runtime.js";
import { SalToolService } from "./tools.js";

async function main(): Promise<void> {
  const command = process.argv[2];
  if (command === "--help" || command === "-h") {
    process.stdout.write("Usage: loomle [mcp]\n");
    return;
  }
  if (command !== undefined && command !== "mcp") {
    throw new Error(`Unknown command: ${command}. Usage: loomle [mcp]`);
  }

  const runtime = new DiscoveredRuntimeInvoker();
  const shutdown = () => runtime.close();
  process.once("exit", shutdown);
  process.once("SIGINT", () => {
    shutdown();
    process.exit(0);
  });
  process.once("SIGTERM", () => {
    shutdown();
    process.exit(0);
  });

  await runMcpServer(new SalToolService(runtime));
}

main().catch((error: unknown) => {
  // stdout belongs exclusively to MCP stdio framing.
  process.stderr.write(`[loomle] ${error instanceof Error ? error.message : String(error)}\n`);
  process.exitCode = 1;
});
