export const SOURCE_UNREAL_VERSION = "5.7";

export const SUPPORTED_UNREAL_VERSIONS = Object.freeze([
  "5.7",
  "5.8",
]);

export function requireSupportedUnrealVersion(value) {
  if (!SUPPORTED_UNREAL_VERSIONS.includes(value)) {
    throw new Error(
      `unsupported Unreal Engine version ${JSON.stringify(value)}; accepted versions:`
      + ` ${SUPPORTED_UNREAL_VERSIONS.join(", ")}`,
    );
  }
  return value;
}

export function descriptorEngineVersion(value) {
  return `${requireSupportedUnrealVersion(value)}.0`;
}

export function unrealVersionSlug(value) {
  return `ue${requireSupportedUnrealVersion(value)}`;
}
