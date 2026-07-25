import { execFileSync } from "node:child_process";
import { readFile } from "node:fs/promises";

const generatedFiles = [
  new URL("../src/generated/sal-object-schema.ts", import.meta.url),
  new URL("../src/generated/sal-object-schema-data.ts", import.meta.url),
];

const before = await Promise.all(generatedFiles.map((file) => readFile(file, "utf8")));
execFileSync("npm", ["run", "generate"], {
  cwd: new URL("..", import.meta.url),
  stdio: "inherit",
});
const after = await Promise.all(generatedFiles.map((file) => readFile(file, "utf8")));

for (let index = 0; index < generatedFiles.length; index += 1) {
  if (before[index] !== after[index]) {
    throw new Error(
      `${generatedFiles[index].pathname} was stale; generated files changed during the idempotence check.`,
    );
  }
}

console.log("[PASS] generated SAL schema files are current");
