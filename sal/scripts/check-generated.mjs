import { execFileSync } from "node:child_process";
import { readFile } from "node:fs/promises";

const generatedFiles = [
  new URL("../src/generated/sal-object-schema.ts", import.meta.url),
  new URL("../src/generated/sal-object-schema-data.ts", import.meta.url),
];

const before = await Promise.all(generatedFiles.map((file) => readFile(file, "utf8")));
const cwd = new URL("..", import.meta.url);
if (process.platform === "win32") {
  const commandShell = process.env.ComSpec;
  if (!commandShell) {
    throw new Error("The generated SAL schema check requires the Windows command shell.");
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
const after = await Promise.all(generatedFiles.map((file) => readFile(file, "utf8")));

for (let index = 0; index < generatedFiles.length; index += 1) {
  if (before[index] !== after[index]) {
    throw new Error(
      `${generatedFiles[index].pathname} was stale; generated files changed during the idempotence check.`,
    );
  }
}

console.log("[PASS] generated SAL schema files are current");
