import { spawnSync } from "node:child_process";
import { readdirSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const directory = fileURLToPath(new URL("../../.tmp/client-tests/tests/", import.meta.url));
const files = readdirSync(directory)
  .filter((name) => name.endsWith(".test.js"))
  .sort()
  .map((name) => resolve(directory, name));

if (files.length === 0) {
  throw new Error(`No compiled Client tests found in ${directory}.`);
}

const result = spawnSync(process.execPath, ["--test", ...files], { stdio: "inherit" });
if (result.error) throw result.error;
process.exitCode = result.status ?? 1;
