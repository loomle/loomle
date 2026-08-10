#!/usr/bin/env node

import { readFile, readdir, stat } from "node:fs/promises";
import { basename, join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const CHANNELS = new Set(["fab", "claude"]);
const FAB_LISTING = "https://www.fab.com/listings/f0fb545c-b1d9-4525-8642-3f170134c428";

export function validateChannelDocument(value, expectedChannel) {
  if (!CHANNELS.has(expectedChannel)) fail(`unsupported channel: ${expectedChannel}`);
  requireExactKeys(value, expectedChannel === "fab"
    ? ["schemaVersion", "channel", "version", "publishedAt", "listingUrl"]
    : ["schemaVersion", "channel", "version", "publishedAt", "listingUrl", "bridge"]);
  if (value.schemaVersion !== 1) fail("schemaVersion must equal 1");
  if (value.channel !== expectedChannel) {
    fail(`channel must equal ${expectedChannel}`);
  }
  if (typeof value.version !== "string" || !/^\d+\.\d+\.\d+$/.test(value.version)) {
    fail("version must be a stable semantic version");
  }
  if (typeof value.publishedAt !== "string" || !isIsoTimestamp(value.publishedAt)) {
    fail("publishedAt must be a canonical UTC timestamp");
  }
  validateListing(value.listingUrl, expectedChannel);
  if (expectedChannel !== "fab") validateBridge(value.bridge, value.version);
  return value;
}

export async function validateChannelDirectory(directory) {
  const root = resolve(directory);
  const rootStat = await stat(root).catch((error) => {
    if (error?.code === "ENOENT") fail(`channel directory not found: ${root}`);
    throw error;
  });
  if (!rootStat.isDirectory()) fail(`channel document path is not a directory: ${root}`);
  const files = (await readdir(root))
    .filter((name) => name.endsWith(".json"))
    .sort();
  const validated = [];
  for (const name of files) {
    const channel = basename(name, ".json");
    if (!CHANNELS.has(channel)) fail(`unexpected channel document: ${name}`);
    let value;
    try {
      value = JSON.parse(await readFile(join(root, name), "utf8"));
    } catch (error) {
      fail(`cannot read ${name}: ${error.message}`);
    }
    validateChannelDocument(value, channel);
    validated.push(name);
  }
  return validated;
}

function validateListing(value, channel) {
  if (typeof value !== "string") fail("listingUrl must be a URL");
  let url;
  try {
    url = new URL(value);
  } catch {
    fail("listingUrl must be a URL");
  }
  if (url.protocol !== "https:" || url.username || url.password || url.hash) {
    fail("listingUrl must be a public HTTPS URL without credentials or a fragment");
  }
  if (channel === "fab" && url.href !== FAB_LISTING) {
    fail("Fab listingUrl does not identify the Loomle listing");
  }
  if (channel === "claude" && !["claude.ai", "claude.com"].includes(url.hostname)) {
    fail("Claude listingUrl must use an official Claude origin");
  }
}

function validateBridge(value, version) {
  requireExactKeys(value, ["source", "tag", "assets"]);
  if (value.source !== "github_release") fail("bridge.source must equal github_release");
  if (value.tag !== `v${version}`) fail("bridge.tag must match the channel version");
  requireExactKeys(value.assets, ["ue5.7", "ue5.8"]);
  for (const engineVersion of ["5.7", "5.8"]) {
    const asset = value.assets[`ue${engineVersion}`];
    requireExactKeys(asset, ["url", "sha256"]);
    const name = `loomle-bridge-${version}-ue${engineVersion}.zip`;
    const expectedUrl = `https://github.com/loomle/loomle/releases/download/v${version}/${name}`;
    if (asset.url !== expectedUrl) {
      fail(`bridge asset URL must be the exact versioned GitHub Release URL for UE ${engineVersion}`);
    }
    if (typeof asset.sha256 !== "string" || !/^[0-9a-f]{64}$/.test(asset.sha256)) {
      fail(`bridge asset SHA-256 must be lowercase hexadecimal for UE ${engineVersion}`);
    }
  }
}

function requireExactKeys(value, expected) {
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    fail("channel document fields must be objects");
  }
  const actual = Object.keys(value).sort();
  const required = [...expected].sort();
  if (JSON.stringify(actual) !== JSON.stringify(required)) {
    fail(`unexpected fields: expected ${required.join(", ")}; found ${actual.join(", ")}`);
  }
}

function isIsoTimestamp(value) {
  if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{3})?Z$/.test(value)) return false;
  const milliseconds = Date.parse(value);
  if (!Number.isFinite(milliseconds)) return false;
  const canonical = new Date(milliseconds).toISOString();
  return value.includes(".") ? canonical === value : canonical.replace(".000Z", "Z") === value;
}

function fail(message) {
  throw new Error(message);
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  if (process.argv.length !== 4 || process.argv[2] !== "--directory") {
    throw new Error("Usage: node packaging/tools/channel-document.mjs --directory <path>");
  }
  validateChannelDirectory(process.argv[3])
    .then((files) => process.stdout.write(
      `Validated ${files.length} channel document${files.length === 1 ? "" : "s"}.\n`,
    ))
    .catch((error) => {
      process.stderr.write(`[FAIL] ${error.message}\n`);
      process.exitCode = 1;
    });
}
