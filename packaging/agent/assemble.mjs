#!/usr/bin/env node

import { createHash } from "node:crypto";
import { createReadStream } from "node:fs";
import {
  copyFile,
  mkdir,
  readFile,
  rm,
  stat,
  writeFile,
} from "node:fs/promises";
import { dirname, isAbsolute, join, relative, resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath, pathToFileURL } from "node:url";

import { renderThirdPartyNotices } from "../release/third-party-notices.mjs";
import { checkProductVersion } from "../tools/product-version.mjs";

const DEFAULT_REPO_ROOT = fileURLToPath(new URL("../../", import.meta.url));
const REGISTRY_SCHEMA = "https://static.modelcontextprotocol.io/schemas/2025-12-11/server.schema.json";
const REPOSITORY_URL = "https://github.com/loomle/loomle";
const WEBSITE_URL = "https://loomle.ai";
const PRIVACY_URL = "https://loomle.ai/privacy/";

export async function assembleAgentPackages({
  repoRoot,
  outputDir,
  versionCheck = checkProductVersion,
}) {
  const root = resolve(repoRoot);
  const output = resolve(outputDir);
  const bundle = join(root, "client", "dist", "main.cjs");
  const license = join(root, "LICENSE");
  const logo = join(root, "packaging", "fab", "media", "loomle-mark.svg");
  const mcpbIcon = join(root, "packaging", "agent", "assets", "loomle-icon.png");
  const mcpbReadme = join(root, "packaging", "agent", "mcpb-readme.md");
  assertSafeOutput(root, output, [bundle, license, logo, mcpbIcon, mcpbReadme]);

  const version = await versionCheck(root);
  await requireNonEmptyFile(bundle, "self-contained Client bundle");
  await requireNonEmptyFile(license, "Loomle license");
  await requireNonEmptyFile(logo, "Loomle logo");
  await requireNonEmptyFile(mcpbIcon, "Loomle MCPB icon");
  await requireNonEmptyFile(mcpbReadme, "Loomle MCPB README");
  const bundleSha256 = await sha256(bundle);
  const notices = await renderThirdPartyNotices({ repoRoot: root });

  await rm(output, { recursive: true, force: true });
  await mkdir(output, { recursive: true });
  const work = join(output, ".work");
  await mkdir(work, { recursive: true });

  const registryBundleName = `loomle-mcp-registry-${version}.mcpb`;
  const claudeBundleName = `loomle-claude-${version}.mcpb`;
  const registryBundle = await createMcpb({
    bundle,
    channel: "mcp_registry",
    destination: join(output, "registry", registryBundleName),
    icon: mcpbIcon,
    license,
    notices,
    readme: mcpbReadme,
    staging: join(work, "registry"),
    version,
  });
  const claudeBundle = await createMcpb({
    bundle,
    channel: "claude",
    destination: join(output, "claude", claudeBundleName),
    icon: mcpbIcon,
    license,
    notices,
    readme: mcpbReadme,
    staging: join(work, "claude"),
    version,
  });

  const codexRoot = join(output, "codex-marketplace");
  const codexBundle = await createCodexMarketplace({
    bundle,
    license,
    logo,
    notices,
    root: codexRoot,
    version,
  });
  const codexArchiveName = `loomle-codex-marketplace-${version}.zip`;
  const codexArchive = join(output, "codex", codexArchiveName);
  await mkdir(dirname(codexArchive), { recursive: true });
  run("zip", ["-X", "-q", "-r", codexArchive, "codex-marketplace"], output);

  const registryServer = {
    $schema: REGISTRY_SCHEMA,
    name: "io.github.loomle/loomle",
    title: "Loomle MCP for Unreal",
    description: "Structured, verifiable Unreal Engine editing for AI agents.",
    version,
    websiteUrl: WEBSITE_URL,
    repository: { url: REPOSITORY_URL, source: "github" },
    packages: [{
      registryType: "mcpb",
      identifier: `${REPOSITORY_URL}/releases/download/v${version}/${registryBundleName}`,
      fileSha256: registryBundle.archiveSha256,
      transport: { type: "stdio" },
    }],
  };
  const serverJsonPath = join(output, "registry", "server.json");
  await writeJson(serverJsonPath, registryServer);

  const receipt = {
    schemaVersion: 1,
    productVersion: version,
    clientBundleSha256: bundleSha256,
    packages: {
      mcpRegistry: {
        archive: relative(output, registryBundle.destination),
        archiveSha256: registryBundle.archiveSha256,
        clientSha256: registryBundle.clientSha256,
        serverJson: relative(output, serverJsonPath),
      },
      claude: {
        archive: relative(output, claudeBundle.destination),
        archiveSha256: claudeBundle.archiveSha256,
        clientSha256: claudeBundle.clientSha256,
      },
      codex: {
        root: relative(output, codexRoot),
        archive: relative(output, codexArchive),
        archiveSha256: await sha256(codexArchive),
        clientSha256: codexBundle.clientSha256,
      },
    },
  };
  if (new Set([
    bundleSha256,
    registryBundle.clientSha256,
    claudeBundle.clientSha256,
    codexBundle.clientSha256,
  ]).size !== 1) {
    throw new Error("Agent package Client bundles are not byte-identical.");
  }
  const receiptPath = join(output, "build.json");
  await writeJson(receiptPath, receipt);
  await rm(work, { recursive: true, force: true });

  return { outputDir: output, receipt, receiptPath };
}

async function createMcpb({
  bundle,
  channel,
  destination,
  icon,
  license,
  notices,
  readme,
  staging,
  version,
}) {
  await mkdir(join(staging, "server"), { recursive: true });
  const stagedClient = join(staging, "server", "loomle.cjs");
  await copyFile(bundle, stagedClient);
  await copyFile(icon, join(staging, "icon.png"));
  await copyFile(license, join(staging, "LICENSE"));
  await copyFile(readme, join(staging, "README.md"));
  await writeFile(join(staging, "THIRD_PARTY_NOTICES.txt"), notices);
  await writeJson(join(staging, "manifest.json"), mcpbManifest(version, channel));
  await mkdir(dirname(destination), { recursive: true });
  run("zip", ["-X", "-q", "-r", destination, "."], staging);
  return {
    destination,
    archiveSha256: await sha256(destination),
    clientSha256: await sha256(stagedClient),
  };
}

function mcpbManifest(version, channel) {
  return {
    manifest_version: "0.3",
    name: "loomle",
    display_name: "Loomle MCP for Unreal",
    version,
    description: "Structured, verifiable Unreal Engine editing for AI agents.",
    long_description: "Connect an AI agent to the live Unreal Editor through Loomle's compact SAL interface. The matching Loomle Bridge plugin must be installed separately.",
    author: { name: "Loomle Lab", url: WEBSITE_URL },
    repository: { type: "git", url: `${REPOSITORY_URL}.git` },
    homepage: WEBSITE_URL,
    documentation: `${WEBSITE_URL}/install.html`,
    support: `${REPOSITORY_URL}/issues`,
    icon: "icon.png",
    license: "MIT",
    privacy_policies: [PRIVACY_URL],
    keywords: ["unreal-engine", "blueprint", "mcp", "ai-agent", "sal"],
    compatibility: {
      platforms: ["darwin", "win32"],
      runtimes: { node: ">=20" },
    },
    server: {
      type: "node",
      entry_point: "server/loomle.cjs",
      mcp_config: {
        command: "node",
        args: ["${__dirname}/server/loomle.cjs", "mcp"],
        env: { LOOMLE_DISTRIBUTION_CHANNEL: channel },
      },
    },
  };
}

async function createCodexMarketplace({ bundle, license, logo, notices, root, version }) {
  const pluginRoot = join(root, "plugins", "loomle");
  const clientPath = join(pluginRoot, "mcp", "loomle.cjs");
  await mkdir(dirname(clientPath), { recursive: true });
  await mkdir(join(pluginRoot, "assets"), { recursive: true });
  await copyFile(bundle, clientPath);
  await copyFile(license, join(pluginRoot, "LICENSE"));
  await copyFile(logo, join(pluginRoot, "assets", "loomle-mark.svg"));
  await writeFile(join(pluginRoot, "THIRD_PARTY_NOTICES.txt"), notices);
  await writeJson(join(pluginRoot, ".mcp.json"), {
    mcpServers: {
      loomle: {
        command: "node",
        args: ["./mcp/loomle.cjs", "mcp"],
        cwd: ".",
        env: { LOOMLE_DISTRIBUTION_CHANNEL: "github" },
      },
    },
  });
  await writeJson(join(pluginRoot, ".codex-plugin", "plugin.json"), {
    name: "loomle",
    version,
    description: "Structured, verifiable Unreal Engine editing for Codex.",
    author: { name: "Loomle Lab", url: WEBSITE_URL },
    homepage: WEBSITE_URL,
    repository: REPOSITORY_URL,
    license: "MIT",
    keywords: ["unreal-engine", "blueprint", "mcp", "sal"],
    mcpServers: "./.mcp.json",
    interface: {
      displayName: "Loomle for Unreal",
      shortDescription: "Let Codex inspect and edit Unreal Engine through SAL.",
      longDescription: "Loomle connects Codex to a live Unreal Editor with compact queries, stable references, schema-grounded discovery, dry runs, and verifiable patches.",
      developerName: "Loomle Lab",
      category: "Developer Tools",
      capabilities: ["Interactive", "Write"],
      websiteURL: WEBSITE_URL,
      privacyPolicyURL: PRIVACY_URL,
      brandColor: "#BD4F32",
      composerIcon: "./assets/loomle-mark.svg",
      logo: "./assets/loomle-mark.svg",
      defaultPrompt: [
        "Inspect my Unreal project and summarize the active Blueprint.",
        "Edit this Blueprint with a dry run before applying changes.",
        "Find the exact schema for the Unreal object I want to modify.",
      ],
    },
  });
  await writeJson(join(root, ".agents", "plugins", "marketplace.json"), {
    name: "loomle",
    interface: { displayName: "Loomle" },
    plugins: [{
      name: "loomle",
      source: { source: "local", path: "./plugins/loomle" },
      policy: { installation: "AVAILABLE", authentication: "ON_INSTALL" },
      category: "Developer Tools",
    }],
  });
  return { clientSha256: await sha256(clientPath) };
}

async function writeJson(path, value) {
  await mkdir(dirname(path), { recursive: true });
  await writeFile(path, `${JSON.stringify(value, null, 2)}\n`);
}

async function requireNonEmptyFile(path, label) {
  let value;
  try {
    value = await stat(path);
  } catch (error) {
    if (error?.code === "ENOENT") throw new Error(`${label} not found: ${path}`);
    throw error;
  }
  if (!value.isFile() || value.size === 0) throw new Error(`${label} must be non-empty: ${path}`);
}

function assertSafeOutput(repoRoot, output, inputs) {
  if (output === repoRoot || isWithin(output, repoRoot)) {
    throw new Error("Agent package output must not contain the repository root.");
  }
  for (const input of inputs) {
    if (isWithin(output, input) || isWithin(input, output)) {
      throw new Error(`Agent package output overlaps an input: ${input}`);
    }
  }
}

function isWithin(parent, child) {
  const path = relative(parent, child);
  return path === ""
    || (path !== ".." && !path.startsWith(`..${process.platform === "win32" ? "\\" : "/"}`)
      && !isAbsolute(path));
}

async function sha256(path) {
  const hash = createHash("sha256");
  for await (const chunk of createReadStream(path)) hash.update(chunk);
  return hash.digest("hex");
}

function run(command, args, cwd) {
  const result = spawnSync(command, args, { cwd, stdio: "inherit" });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`${command} failed with exit code ${result.status}.`);
}

function parseArguments(args) {
  let repoRoot = DEFAULT_REPO_ROOT;
  let outputDir = resolve(DEFAULT_REPO_ROOT, ".tmp", "agent-packages");
  for (let index = 0; index < args.length; index += 2) {
    const flag = args[index];
    const value = args[index + 1];
    if (!value) usage();
    if (flag === "--repo-root") repoRoot = value;
    else if (flag === "--output-dir") outputDir = value;
    else usage();
  }
  return { repoRoot, outputDir };
}

function usage() {
  throw new Error("Usage: node packaging/agent/assemble.mjs [--repo-root <path>] [--output-dir <path>]");
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  assembleAgentPackages(parseArguments(process.argv.slice(2)))
    .then((result) => process.stdout.write(`${JSON.stringify(result, null, 2)}\n`))
    .catch((error) => {
      process.stderr.write(`[FAIL] ${error.message}\n`);
      process.exitCode = 1;
    });
}
