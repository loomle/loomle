import { isAbsolute, resolve } from "node:path";

export const CLIENT_RUNTIME_CACHE_ENV = "LOOMLE_CLIENT_RUNTIME_CACHE";

export function resolveClientRuntimeCache(repoRoot, environment = process.env) {
  const configured = environment[CLIENT_RUNTIME_CACHE_ENV]?.trim();
  if (!configured) {
    return resolve(repoRoot, ".tmp", "client-cache");
  }
  if (!isAbsolute(configured)) {
    throw new Error(`${CLIENT_RUNTIME_CACHE_ENV} must be an absolute path.`);
  }
  return resolve(configured);
}
