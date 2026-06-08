import { readFile } from "node:fs/promises";
import { compilePatch, parseLgl, printLgl } from "./index.js";

const [command, file] = process.argv.slice(2);

if (!command || !file || !["parse", "compile", "print"].includes(command)) {
  console.error("Usage: node dist/src/cli.js <parse|compile|print> <file.lgl>");
  process.exit(2);
}

const source = await readFile(file, "utf8");
const document = parseLgl(source);

if (command === "parse") {
  console.log(JSON.stringify(document, null, 2));
} else if (command === "print") {
  process.stdout.write(printLgl(document));
} else {
  if (document.kind !== "patch") {
    console.error("compile expects a patch document.");
    process.exit(2);
  }
  const patch = document;
  console.log(JSON.stringify(compilePatch(patch), null, 2));
}
