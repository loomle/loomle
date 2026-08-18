import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { copyFile, mkdtemp, readdir, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { guide } from "@loomle/interfaces";
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
    assert.equal(client.getInstructions(), undefined);
    const tools = await client.listTools();
    assert.deepEqual(tools.tools.map((tool) => tool.name), [
      "status",
      "project",
      "sal_query",
      "sal_patch",
      "sal_schema",
      "agent_skill",
      "editor",
      "python",
    ]);
    assert.equal(
      tools.tools.find((tool) => tool.name === "sal_schema")?.description,
      guide,
    );
    assert.deepEqual(
      ((tools.tools.find((tool) => tool.name === "sal_schema")?.inputSchema
        .properties as Record<string, { enum?: string[] }>).module.enum),
      [
        "asset",
        "blueprint",
        "class",
        "graph",
        "state_tree",
        "widget",
        "level",
        "pcg",
        "pcg_component",
      ],
    );
    assert.equal(
      tools.tools.filter((tool) => tool.description?.includes(guide)).length,
      1,
    );
    assert.match(
      tools.tools.find((tool) => tool.name === "agent_skill")?.description ?? "",
      /debug-unreal-pie-with-python/,
    );
    assert.match(
      tools.tools.find((tool) => tool.name === "agent_skill")?.description ?? "",
      /format-unreal-blueprints/,
    );
    assert.match(
      tools.tools.find((tool) => tool.name === "agent_skill")?.description ?? "",
      /use-unreal-python/,
    );
    assert.match(
      tools.tools.find((tool) => tool.name === "python")?.description ?? "",
      /Before run, load use-unreal-python/,
    );

    const pcgComponentSchema = await client.callTool({
      name: "sal_schema",
      arguments: { module: "pcg_component" },
    });
    assert.notEqual(pcgComponentSchema.isError, true);
    assert.match(JSON.stringify(pcgComponentSchema), /# pcg_component/);
    assert.match(
      JSON.stringify(pcgComponentSchema),
      /Patch Target under the async edit guard/,
    );

    const pythonSkill = await client.callTool({
      name: "agent_skill",
      arguments: { name: "use-unreal-python" },
    });
    assert.notEqual(pythonSkill.isError, true);
    assert.match(JSON.stringify(pythonSkill), /# Use Unreal Python/);
    assert.match(JSON.stringify(pythonSkill), /stateMayHaveChanged/);

    const pieSkill = await client.callTool({
      name: "agent_skill",
      arguments: { name: "debug-unreal-pie-with-python" },
    });
    assert.notEqual(pieSkill.isError, true);
    assert.match(JSON.stringify(pieSkill), /# Debug Unreal PIE with Python/);
    assert.match(JSON.stringify(pieSkill), /editor_request_begin_play/);

    const skill = await client.callTool({
      name: "agent_skill",
      arguments: { name: "format-unreal-blueprints" },
    });
    assert.notEqual(skill.isError, true);
    assert.match(JSON.stringify(skill), /# Format Unreal Blueprints/);
    assert.match(JSON.stringify(skill), /# Blueprint K2 Layout Rules/);

    // A valid Query forces the embedded AJV validators to compile before the
    // isolated project lookup fails. This catches hidden package loads that a
    // handshake-only smoke test cannot see.
    const query = await client.callTool({
      name: "sal_query",
      arguments: {
        text: "assets = target { domain: asset }\n\nquery assets\nassets \"door\"",
      },
    });
    assert.equal(query.isError, true);
    assert.match(JSON.stringify(query), /project\.selection_required/);
  } finally {
    await client.close().catch(() => undefined);
    await rm(isolated.directory, { recursive: true, force: true });
  }
});
