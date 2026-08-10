import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import test from "node:test";

import { verifyRegistryCandidate } from "./verify-registry-candidate.mjs";

test("verifies one immutable Registry MCPB candidate", async () => {
  const fixture = await createFixture();
  try {
    const result = await verifyRegistryCandidate(fixture.options);
    assert.equal(result.version, "0.7.10");
    assert.equal(result.archiveSha256, fixture.sha256);
    assert.match(result.identifier, /v0\.7\.10\/loomle-mcp-registry-0\.7\.10\.mcpb$/);
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects checksum, version, and channel drift", async () => {
  const fixture = await createFixture();
  try {
    await writeFile(fixture.options.shaFilePath, `${"0".repeat(64)}  loomle-mcp-registry-0.7.10.mcpb\n`);
    await assert.rejects(verifyRegistryCandidate(fixture.options), /checksum sidecar/);

    await writeFile(fixture.options.shaFilePath, `${fixture.sha256}  loomle-mcp-registry-0.7.10.mcpb\n`);
    const server = JSON.parse(await readFile(fixture.options.serverJsonPath, "utf8"));
    server.version = "0.7.9";
    await writeFile(fixture.options.serverJsonPath, `${JSON.stringify(server)}\n`);
    await assert.rejects(verifyRegistryCandidate(fixture.options), /selected version/);
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

async function createFixture() {
  const root = await mkdtemp(join(tmpdir(), "loomle-registry-candidate-"));
  const staging = join(root, "staging");
  const archiveName = "loomle-mcp-registry-0.7.10.mcpb";
  const archive = join(root, archiveName);
  await mkdir(join(staging, "server"), { recursive: true });
  await writeFile(join(staging, "server", "loomle.cjs"), "console.log('loomle');\n");
  await writeFile(join(staging, "manifest.json"), `${JSON.stringify({
    manifest_version: "0.3",
    name: "loomle",
    version: "0.7.10",
    server: {
      type: "node",
      entry_point: "server/loomle.cjs",
      mcp_config: {
        env: { LOOMLE_DISTRIBUTION_CHANNEL: "mcp_registry" },
      },
    },
  })}\n`);
  const zipped = spawnSync("zip", ["-X", "-q", "-r", archive, "."], { cwd: staging });
  if (zipped.error) throw zipped.error;
  if (zipped.status !== 0) throw new Error(zipped.stderr?.toString());
  const sha256 = createHash("sha256").update(await readFile(archive)).digest("hex");
  const shaFilePath = `${archive}.sha256`;
  await writeFile(shaFilePath, `${sha256}  ${archiveName}\n`);
  const serverJsonPath = join(root, "server.json");
  await writeFile(serverJsonPath, `${JSON.stringify({
    $schema: "https://static.modelcontextprotocol.io/schemas/2025-12-11/server.schema.json",
    name: "io.github.loomle/loomle",
    title: "Loomle MCP for Unreal",
    description: "Structured, verifiable Unreal Engine editing for AI agents.",
    version: "0.7.10",
    websiteUrl: "https://loomle.ai",
    repository: { url: "https://github.com/loomle/loomle", source: "github" },
    packages: [{
      registryType: "mcpb",
      identifier: `https://github.com/loomle/loomle/releases/download/v0.7.10/${archiveName}`,
      fileSha256: sha256,
      transport: { type: "stdio" },
    }],
  })}\n`);
  return {
    root,
    sha256,
    options: {
      mcpbPath: archive,
      repository: "loomle/loomle",
      serverJsonPath,
      shaFilePath,
      tag: "v0.7.10",
    },
  };
}
