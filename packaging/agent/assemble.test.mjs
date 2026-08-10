import assert from "node:assert/strict";
import { mkdtemp, readFile, rm, stat, writeFile, mkdir } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { spawnSync } from "node:child_process";
import test from "node:test";

import { assembleAgentPackages } from "./assemble.mjs";

test("derives Registry, Claude, and Codex packages from one Client bundle", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-agent-packages-"));
  const repoRoot = join(root, "repo");
  const outputDir = join(root, "output");
  try {
    await write(join(repoRoot, "package.json"), JSON.stringify({ version: "0.7.10" }));
    await write(join(repoRoot, "client", "dist", "main.cjs"), "console.log('loomle');\n");
    await write(join(repoRoot, "LICENSE"), "MIT fixture\n");
    await write(join(repoRoot, "packaging", "fab", "media", "loomle-mark.svg"), "<svg/>\n");
    await write(join(repoRoot, "packaging", "agent", "assets", "loomle-icon.png"), "PNG fixture\n");
    await write(join(repoRoot, "packaging", "agent", "mcpb-readme.md"), "# Loomle fixture\n");
    await write(join(repoRoot, "package-lock.json"), JSON.stringify({ packages: {} }));

    const result = await assembleAgentPackages({
      repoRoot,
      outputDir,
      versionCheck: async () => "0.7.10",
    });
    const receipt = result.receipt;
    assert.equal(receipt.productVersion, "0.7.10");
    assert.equal(new Set([
      receipt.clientBundleSha256,
      receipt.packages.mcpRegistry.clientSha256,
      receipt.packages.claude.clientSha256,
      receipt.packages.codex.clientSha256,
    ]).size, 1);

    const registryManifest = JSON.parse(readArchive(
      join(outputDir, receipt.packages.mcpRegistry.archive),
      "manifest.json",
    ));
    const claudeManifest = JSON.parse(readArchive(
      join(outputDir, receipt.packages.claude.archive),
      "manifest.json",
    ));
    assert.equal(registryManifest.manifest_version, "0.3");
    assert.equal(registryManifest.icon, "icon.png");
    assert.deepEqual(registryManifest.privacy_policies, ["https://loomle.ai/privacy/"]);
    assert.equal(registryManifest.server.mcp_config.env.LOOMLE_DISTRIBUTION_CHANNEL, "mcp_registry");
    assert.equal(claudeManifest.server.mcp_config.env.LOOMLE_DISTRIBUTION_CHANNEL, "claude");
    assert.equal(registryManifest.version, "0.7.10");
    assert.equal(claudeManifest.version, "0.7.10");
    for (const archive of [
      join(outputDir, receipt.packages.mcpRegistry.archive),
      join(outputDir, receipt.packages.claude.archive),
    ]) {
      assert.match(readArchive(archive, "README.md"), /Loomle fixture/);
      assert.equal(readArchive(archive, "icon.png"), "PNG fixture\n");
    }

    const serverJson = JSON.parse(await readFile(join(outputDir, "registry", "server.json"), "utf8"));
    assert.equal(serverJson.name, "io.github.loomle/loomle");
    assert.equal(serverJson.packages[0].fileSha256, receipt.packages.mcpRegistry.archiveSha256);
    assert.match(serverJson.packages[0].identifier, /v0\.7\.10\/loomle-mcp-registry-0\.7\.10\.mcpb$/);

    const codexRoot = join(outputDir, receipt.packages.codex.root);
    const plugin = JSON.parse(await readFile(
      join(codexRoot, "plugins", "loomle", ".codex-plugin", "plugin.json"),
      "utf8",
    ));
    const mcp = JSON.parse(await readFile(
      join(codexRoot, "plugins", "loomle", ".mcp.json"),
      "utf8",
    ));
    const marketplace = JSON.parse(await readFile(
      join(codexRoot, ".agents", "plugins", "marketplace.json"),
      "utf8",
    ));
    assert.equal(plugin.name, "loomle");
    assert.equal(plugin.version, "0.7.10");
    assert.equal(mcp.mcpServers.loomle.env.LOOMLE_DISTRIBUTION_CHANNEL, "github");
    assert.equal(marketplace.plugins[0].source.path, "./plugins/loomle");
    assert.equal(marketplace.plugins[0].policy.installation, "AVAILABLE");
    assert.match(receipt.packages.codex.archive, /loomle-codex-marketplace-0\.7\.10\.zip$/);
    assert.match(readArchive(
      join(outputDir, receipt.packages.codex.archive),
      "codex-marketplace/plugins/loomle/.codex-plugin/plugin.json",
    ), /"version": "0\.7\.10"/);
    await assert.rejects(stat(join(outputDir, ".work")), /ENOENT/);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("refuses an output that can erase the repository", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-agent-packages-"));
  try {
    await assert.rejects(assembleAgentPackages({
      repoRoot: root,
      outputDir: root,
      versionCheck: async () => "0.7.10",
    }), /must not contain the repository root/);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

function readArchive(archive, entry) {
  const result = spawnSync("unzip", ["-p", archive, entry], { encoding: "utf8" });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(result.stderr);
  return result.stdout;
}

async function write(path, contents) {
  await mkdir(dirname(path), { recursive: true });
  await writeFile(path, contents);
}
