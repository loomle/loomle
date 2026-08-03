import { execFileSync } from "node:child_process";
import { readFile } from "node:fs/promises";

const catalog = new URL("../src/generated/agent-skills.ts", import.meta.url);
const before = await readFile(catalog, "utf8");
const cwd = new URL("..", import.meta.url);

if (process.platform === "win32") {
  const commandShell = process.env.ComSpec;
  if (!commandShell) throw new Error("The generated Agent Skill check requires cmd.exe.");
  execFileSync(commandShell, ["/d", "/s", "/c", "npm.cmd run generate:skills"], {
    cwd,
    stdio: "inherit",
  });
} else {
  execFileSync("npm", ["run", "generate:skills"], { cwd, stdio: "inherit" });
}

const after = await readFile(catalog, "utf8");
if (before !== after) {
  throw new Error(`${catalog.pathname} was stale; regenerate and commit it.`);
}

console.log("[PASS] generated Agent Skill catalog is current");
