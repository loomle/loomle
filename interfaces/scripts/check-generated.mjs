import { execFileSync } from "node:child_process";
import { readFile } from "node:fs/promises";

const catalog = new URL("../src/generated/catalog.ts", import.meta.url);
const before = await readFile(catalog, "utf8");

execFileSync("npm", ["run", "generate"], {
  cwd: new URL("..", import.meta.url),
  stdio: "inherit",
});

const after = await readFile(catalog, "utf8");
if (before !== after) {
  throw new Error(
    `${catalog.pathname} was stale; the generated catalog changed during the idempotence check.`,
  );
}

console.log("[PASS] generated interface catalog is current");
