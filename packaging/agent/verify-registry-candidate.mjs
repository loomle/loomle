#!/usr/bin/env node

import { createHash } from "node:crypto";
import { createReadStream } from "node:fs";
import { readFile, stat } from "node:fs/promises";
import { basename, resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { pathToFileURL } from "node:url";

const SCHEMA = "https://static.modelcontextprotocol.io/schemas/2025-12-11/server.schema.json";
const SERVER_NAME = "io.github.loomle/loomle";

export async function verifyRegistryCandidate({
  mcpbPath,
  repository,
  serverJsonPath,
  shaFilePath,
  tag,
}) {
  if (!/^v\d+\.\d+\.\d+$/.test(tag)) {
    throw new Error("Registry promotion tag must be v<stable-semantic-version>.");
  }
  if (!/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(repository)) {
    throw new Error("Repository must be an owner/name GitHub repository.");
  }
  const version = tag.slice(1);
  const archive = resolve(mcpbPath);
  const archiveName = `loomle-mcp-registry-${version}.mcpb`;
  if (basename(archive) !== archiveName) {
    throw new Error(`Registry MCPB must be named ${archiveName}.`);
  }
  const archiveStat = await stat(archive);
  if (!archiveStat.isFile() || archiveStat.size <= 0) {
    throw new Error("Registry MCPB must be a non-empty file.");
  }
  const archiveSha256 = await sha256(archive);
  const sidecar = parseShaFile(await readFile(resolve(shaFilePath), "utf8"));
  if (sidecar.name !== archiveName || sidecar.sha256 !== archiveSha256) {
    throw new Error("Registry MCPB checksum sidecar does not match the candidate.");
  }

  const server = JSON.parse(await readFile(resolve(serverJsonPath), "utf8"));
  requireExactKeys(server, [
    "$schema",
    "name",
    "title",
    "description",
    "version",
    "websiteUrl",
    "repository",
    "packages",
  ]);
  if (server.$schema !== SCHEMA
    || server.name !== SERVER_NAME
    || server.version !== version
    || server.websiteUrl !== "https://loomle.ai"
    || typeof server.title !== "string"
    || server.title.length === 0
    || typeof server.description !== "string"
    || server.description.length === 0) {
    throw new Error("Registry server metadata does not match Loomle or the selected version.");
  }
  requireExactKeys(server.repository, ["url", "source"]);
  const repositoryUrl = `https://github.com/${repository}`;
  if (server.repository.url !== repositoryUrl || server.repository.source !== "github") {
    throw new Error("Registry repository metadata does not match the publishing repository.");
  }
  if (!Array.isArray(server.packages) || server.packages.length !== 1) {
    throw new Error("Registry server metadata must contain exactly one package.");
  }
  const packageValue = server.packages[0];
  requireExactKeys(packageValue, [
    "registryType",
    "identifier",
    "fileSha256",
    "transport",
  ]);
  requireExactKeys(packageValue.transport, ["type"]);
  const identifier = `${repositoryUrl}/releases/download/${tag}/${archiveName}`;
  if (packageValue.registryType !== "mcpb"
    || packageValue.identifier !== identifier
    || packageValue.fileSha256 !== archiveSha256
    || packageValue.transport.type !== "stdio") {
    throw new Error("Registry package metadata does not match the exact MCPB candidate.");
  }

  const manifest = JSON.parse(readArchiveEntry(archive, "manifest.json"));
  if (manifest.manifest_version !== "0.3"
    || manifest.name !== "loomle"
    || manifest.version !== version
    || manifest.server?.type !== "node"
    || manifest.server?.entry_point !== "server/loomle.cjs"
    || manifest.server?.mcp_config?.env?.LOOMLE_DISTRIBUTION_CHANNEL !== "mcp_registry") {
    throw new Error("Registry MCPB manifest does not identify the selected Registry version.");
  }
  if (readArchiveEntry(archive, "server/loomle.cjs").length === 0) {
    throw new Error("Registry MCPB contains an empty Client bundle.");
  }

  return {
    archive: archiveName,
    archiveSha256,
    identifier,
    serverName: SERVER_NAME,
    version,
  };
}

function parseShaFile(value) {
  const match = /^([0-9a-f]{64})\s+\*?([^\r\n]+)\r?\n?$/.exec(value);
  if (!match || match[2].includes("/") || match[2].includes("\\")) {
    throw new Error("Registry checksum sidecar must contain one local filename.");
  }
  return { sha256: match[1], name: match[2] };
}

function readArchiveEntry(archive, entry) {
  const result = spawnSync("unzip", ["-p", archive, entry], {
    encoding: "utf8",
    maxBuffer: 64 * 1024 * 1024,
  });
  if (result.error) throw result.error;
  if (result.status !== 0) {
    throw new Error(`Cannot read ${entry} from Registry MCPB: ${result.stderr.trim()}`);
  }
  return result.stdout;
}

function requireExactKeys(value, expected) {
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new Error("Registry metadata fields must be objects.");
  }
  const actual = Object.keys(value).sort();
  const required = [...expected].sort();
  if (JSON.stringify(actual) !== JSON.stringify(required)) {
    throw new Error(`Unexpected Registry metadata fields: ${actual.join(", ")}.`);
  }
}

function sha256(path) {
  return new Promise((resolveHash, reject) => {
    const hash = createHash("sha256");
    const stream = createReadStream(path);
    stream.on("error", reject);
    stream.on("data", (chunk) => hash.update(chunk));
    stream.on("end", () => resolveHash(hash.digest("hex")));
  });
}

function parseArgs(argv) {
  const values = new Map();
  for (let index = 0; index < argv.length; index += 2) {
    const key = argv[index];
    const value = argv[index + 1];
    if (!key?.startsWith("--") || value === undefined) {
      throw new Error("Registry candidate arguments must be --name value pairs.");
    }
    values.set(key.slice(2), value);
  }
  for (const required of ["mcpb", "repository", "server-json", "sha-file", "tag"]) {
    if (!values.has(required)) throw new Error(`Missing --${required}.`);
  }
  return {
    mcpbPath: values.get("mcpb"),
    repository: values.get("repository"),
    serverJsonPath: values.get("server-json"),
    shaFilePath: values.get("sha-file"),
    tag: values.get("tag"),
  };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  verifyRegistryCandidate(parseArgs(process.argv.slice(2)))
    .then((result) => process.stdout.write(`${JSON.stringify(result, null, 2)}\n`))
    .catch((error) => {
      process.stderr.write(`[FAIL] ${error.message}\n`);
      process.exitCode = 1;
    });
}
