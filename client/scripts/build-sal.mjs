import { spawnSync } from "node:child_process";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const env = { ...process.env };
// An outer npm run exports its own prefix. Remove it so the nested npm process
// resolves the sibling SAL package's tools and node_modules.
delete env.npm_config_prefix;

const npm = process.platform === "win32" ? "npm.cmd" : "npm";
const scriptDirectory = fileURLToPath(new URL(".", import.meta.url));
const salRoot = resolve(scriptDirectory, "..", "..", "experiments", "sal");
const result = spawnSync(npm, ["--prefix", salRoot, "run", "build"], {
  env,
  stdio: "inherit",
});

if (result.error) throw result.error;
if (result.status !== 0) process.exit(result.status ?? 1);
