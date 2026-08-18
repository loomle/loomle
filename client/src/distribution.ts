import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";

export const distributionChannelEnvironment = "LOOMLE_DISTRIBUTION_CHANNEL";
export const distributionChannelFileName = "distribution.json";

export const distributionChannels = [
  "github",
  "fab",
  "mcp_registry",
  "claude",
  "development",
] as const;

export type DistributionChannel = (typeof distributionChannels)[number];

interface DistributionResolutionOptions {
  executable?: string;
  environment?: Readonly<Record<string, string | undefined>>;
}

export function resolveDistributionChannel(
  options: DistributionResolutionOptions = {},
): DistributionChannel {
  const environment = options.environment ?? process.env;
  const packagedValue = environment[distributionChannelEnvironment];
  if (packagedValue !== undefined) {
    return isDistributionChannel(packagedValue) ? packagedValue : "development";
  }

  const executable = options.executable ?? process.execPath;
  try {
    const value = JSON.parse(readFileSync(
      join(dirname(executable), distributionChannelFileName),
      "utf8",
    )) as unknown;
    return parseDistributionFile(value) ?? "development";
  } catch {
    return "development";
  }
}

export function isDistributionChannel(value: unknown): value is DistributionChannel {
  return typeof value === "string"
    && (distributionChannels as readonly string[]).includes(value);
}

function parseDistributionFile(value: unknown): DistributionChannel | undefined {
  if (!isRecord(value)) return undefined;
  const keys = Object.keys(value).sort();
  if (keys.length !== 2 || keys[0] !== "channel" || keys[1] !== "schemaVersion") {
    return undefined;
  }
  if (value.schemaVersion !== 1 || !isDistributionChannel(value.channel)) {
    return undefined;
  }
  return value.channel;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}
