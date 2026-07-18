import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { copyFile, mkdtemp, readdir, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import test from "node:test";
import { productVersion } from "../src/generated/product-version.js";

const productionBundle = resolve("dist/main.cjs");

async function copyIsolatedBundle(): Promise<{ directory: string; path: string }> {
  const directory = await mkdtemp(join(tmpdir(), "loomle-client-bundle-"));
  const path = join(directory, "main.cjs");
  await copyFile(productionBundle, path);
  return { directory, path };
}

function isolatedEnvironment(directory: string): Record<string, string> {
  const environment: Record<string, string> = {};
  for (const [name, value] of Object.entries(process.env)) {
    if (value !== undefined) environment[name] = value;
  }
  return {
    ...environment,
    NODE_PATH: "",
    NODE_OPTIONS: "",
    HOME: directory,
    USERPROFILE: directory,
    APPDATA: directory,
    LOCALAPPDATA: directory,
  };
}

test(
  "production output is one self-contained Client bundle",
  { timeout: 15_000 },
  async () => {
    assert.deepEqual((await readdir(resolve("dist"))).sort(), ["main.cjs"]);
    const isolated = await copyIsolatedBundle();
    try {
      const result = spawnSync(
        process.execPath,
        ["--no-global-search-paths", isolated.path, "--help"],
        {
          cwd: isolated.directory,
          env: isolatedEnvironment(isolated.directory),
          encoding: "utf8",
          timeout: 10_000,
        },
      );
      assert.equal(result.status, 0, result.stderr);
      assert.equal(result.stdout, "Usage: loomle [mcp]\n");
      assert.equal(result.stderr, "");
    } finally {
      await rm(isolated.directory, { recursive: true, force: true });
    }
  },
);

test("isolated Client bundle completes MCP initialization", { timeout: 15_000 }, async () => {
  const isolated = await copyIsolatedBundle();
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: ["--no-global-search-paths", isolated.path, "mcp"],
    cwd: isolated.directory,
    env: isolatedEnvironment(isolated.directory),
    stderr: "pipe",
  });
  const client = new Client({ name: "loomle-bundle-test", version: "1.0.0" });

  try {
    await client.connect(transport);
    assert.deepEqual(client.getServerVersion(), {
      name: "loomle",
      version: productVersion,
    });
    const tools = await client.listTools();
    assert.deepEqual(tools.tools.map((tool) => tool.name), [
      "sal_query",
      "sal_patch",
      "sal_schema",
      "editor_context",
    ]);

    // A valid Query forces the embedded AJV validators to compile before the
    // isolated runtime lookup fails. This catches hidden package loads that a
    // handshake-only smoke test cannot see.
    const query = await client.callTool({
      name: "sal_query",
      arguments: { text: "query asset\nassets \"door\"" },
    });
    assert.equal(query.isError, true);
    assert.match(JSON.stringify(query), /No online Loomle runtime was found/);
  } finally {
    await client.close().catch(() => undefined);
    await rm(isolated.directory, { recursive: true, force: true });
  }
});
