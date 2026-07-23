import { readFile, writeFile } from "node:fs/promises";

const packageRoot = new URL("../", import.meta.url);
const generatedFiles = [
  new URL("src/generated/sal-object-schema.ts", packageRoot),
  new URL("src/generated/sal-object-schema-data.ts", packageRoot),
];

for (const fileUrl of generatedFiles) {
  const source = await readFile(fileUrl, "utf8");
  const normalized = source.replace(/\r\n?/g, "\n");
  await writeFile(fileUrl, normalized, "utf8");
}
