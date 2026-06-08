import { readdir, readFile } from "node:fs/promises";
import assert from "node:assert/strict";
import { compilePatch, parseLgl, printLgl } from "../../src/index.js";

const graphSource = await readFile("examples/blueprint/begin-play-print.lgl", "utf8");
const graph = parseLgl(graphSource);

assert.equal(graph.kind, "graph");
assert.equal(graph.name, "EventGraph");
assert.equal(graph.statements.length, 3);
assert.equal(printLgl(graph), graphSource.endsWith("\n") ? graphSource : `${graphSource}\n`);

const patchSource = await readFile("examples/blueprint/insert-delay.patch.lgl", "utf8");
const patch = parseLgl(patchSource);

assert.equal(patch.kind, "patch");
assert.equal(patch.name, "EventGraph");
assert.equal(patch.statements.length, 4);

if (patch.kind === "patch") {
  assert.deepEqual(compilePatch(patch), {
    graph: "EventGraph",
    paletteBindings: [
      {
        symbol: "Delay",
        source: { kind: "palette", query: "Delay" },
        where: [
          {
            key: "function",
            value: { kind: "string", value: "/Script/Engine.KismetSystemLibrary.Delay" }
          }
        ]
      }
    ],
    commands: [
      { kind: "setProperty", node: "print", property: "Text", value: "Game Started" },
      { kind: "addNode", alias: "delay", symbol: "Delay", args: [1] },
      {
        kind: "rewire",
        from: { node: "begin", pin: "Then" },
        to: { node: "delay", pin: "Exec" }
      },
      {
        kind: "connect",
        from: { node: "delay", pin: "Then" },
        to: { node: "print", pin: "Exec" }
      }
    ]
  });
}

for (const file of await readdir("examples/blueprint")) {
  if (!file.endsWith(".lgl")) {
    continue;
  }
  const source = await readFile(`examples/blueprint/${file}`, "utf8");
  const document = parseLgl(source);
  assert.ok(document.statements.length > 0, `${file} should contain statements`);
  if (document.kind === "patch") {
    assert.ok(compilePatch(document).commands.length > 0, `${file} should compile to commands`);
  }
}

console.log("golden tests passed");
