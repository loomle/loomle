import { execFileSync } from "node:child_process";
import { readFile } from "node:fs/promises";

const catalog = new URL("../src/generated/catalog.ts", import.meta.url);
const before = await readFile(catalog, "utf8");

const cwd = new URL("..", import.meta.url);
if (process.platform === "win32") {
  const commandShell = process.env.ComSpec;
  if (!commandShell) {
    throw new Error("The generated interface catalog check requires the Windows command shell.");
  }
  execFileSync(commandShell, ["/d", "/s", "/c", "npm.cmd run generate"], {
    cwd,
    stdio: "inherit",
  });
} else {
  execFileSync("npm", ["run", "generate"], {
    cwd,
    stdio: "inherit",
  });
}

const after = await readFile(catalog, "utf8");
if (before !== after) {
  throw new Error(
    `${catalog.pathname} was stale; the generated catalog changed during the idempotence check.`,
  );
}

console.log("[PASS] generated interface catalog is current");
