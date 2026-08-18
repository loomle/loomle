import { readFile, writeFile } from "node:fs/promises";

export const DISTRIBUTION_FILE_NAME = "distribution.json";

const CHANNELS = new Set([
  "github",
  "fab",
  "mcp_registry",
  "claude",
  "development",
]);

export function renderDistributionFile(channel) {
  requireChannel(channel);
  return `${JSON.stringify({ schemaVersion: 1, channel }, null, 2)}\n`;
}

export async function writeDistributionFile(path, channel) {
  await writeFile(path, renderDistributionFile(channel));
}

export async function requireDistributionFile(path, expectedChannel) {
  requireChannel(expectedChannel);
  let value;
  try {
    value = JSON.parse(await readFile(path, "utf8"));
  } catch (error) {
    throw new Error(`cannot read distribution metadata ${path}: ${error.message}`);
  }
  const keys = value && typeof value === "object" && !Array.isArray(value)
    ? Object.keys(value).sort()
    : [];
  if (keys.length !== 2
      || keys[0] !== "channel"
      || keys[1] !== "schemaVersion"
      || value.schemaVersion !== 1
      || value.channel !== expectedChannel) {
    throw new Error(
      `distribution metadata ${path} must be the strict ${expectedChannel} schemaVersion 1 document.`,
    );
  }
  return value;
}

function requireChannel(channel) {
  if (!CHANNELS.has(channel)) {
    throw new Error(`unsupported distribution channel: ${JSON.stringify(channel)}.`);
  }
}
