import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import { constants, createReadStream } from "node:fs";
import {
  access,
  chmod,
  copyFile,
  mkdtemp,
  readFile,
  rm,
  stat,
} from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { guide } from "@loomle/interfaces";

const repoRoot = fileURLToPath(new URL("../../", import.meta.url));
const target = parseTarget(process.argv.slice(2));
const executablePath = resolve(repoRoot, `.tmp/client/${target}/loomle`);
const receiptPath = resolve(repoRoot, `.tmp/client/${target}/build.json`);
const product = JSON.parse(await readFile(resolve(repoRoot, "package.json"), "utf8"));
const runtimeManifest = JSON.parse(await readFile(
  resolve(repoRoot, "packaging/client/node-runtime.json"),
  "utf8",
));

assert.equal(target, `${process.platform}-${process.arch}`, "Executable smoke tests must run natively.");
await access(executablePath, constants.X_OK);
assert.ok((await stat(executablePath)).size > 0, "Executable is empty.");
const executableHash = await sha256(executablePath);
assert.deepEqual(JSON.parse(await readFile(receiptPath, "utf8")), {
  schemaVersion: 2,
  productVersion: product.version,
  protocolVersion: product.loomle.protocolVersion,
  target,
  nodeVersion: runtimeManifest.nodeVersion,
  runtimeSha256: runtimeManifest.targets[target].sha256,
  executable: "loomle",
  sha256: executableHash,
});
assert.equal(
  await fileContains(executablePath, repoRoot),
  false,
  "Executable contains its build checkout path.",
);

const fileResult = run("file", [executablePath]);
assert.match(fileResult.stdout, /Mach-O 64-bit executable arm64/);
run("codesign", ["--verify", "--strict", "--verbose=2", executablePath]);

const directory = await mkdtemp(join(tmpdir(), "loomle sea smoke "));
const isolatedExecutable = join(directory, "Loomle Client");
await copyFile(executablePath, isolatedExecutable);
await chmod(isolatedExecutable, 0o755);

try {
  const environment = {
    HOME: directory,
    TMPDIR: directory,
    PATH: "/usr/bin:/bin",
    NODE_PATH: "",
    NODE_OPTIONS: "",
    LOOMLE_PROJECT_ROOT: "",
  };

  const help = run(isolatedExecutable, ["--help"], { cwd: directory, env: environment });
  assert.equal(help.stdout, "Usage: loomle [mcp]\n");
  assert.equal(help.stderr, "");

  const unknown = run(isolatedExecutable, ["unknown"], {
    cwd: directory,
    env: environment,
    expectedStatus: 1,
  });
  assert.equal(unknown.stdout, "");
  assert.equal(
    unknown.stderr,
    "[loomle] Unknown command: unknown. Usage: loomle [mcp]\n",
  );

  let serverStderr = "";
  const transport = new StdioClientTransport({
    command: isolatedExecutable,
    cwd: directory,
    env: environment,
    stderr: "pipe",
  });
  transport.stderr.on("data", (chunk) => {
    serverStderr += chunk.toString();
  });
  const client = new Client({ name: "loomle-sea-smoke", version: "1.0.0" });

  try {
    await client.connect(transport);
    assert.deepEqual(client.getServerVersion(), {
      name: "loomle",
      version: product.version,
    });
    assert.equal(client.getInstructions(), undefined);

    const tools = await client.listTools();
    assert.deepEqual(tools.tools.map((tool) => tool.name), [
      "project",
      "sal_query",
      "sal_patch",
      "sal_schema",
      "editor_context",
    ]);
    assert.equal(
      tools.tools.find((tool) => tool.name === "sal_schema")?.description,
      guide,
    );
    assert.equal(
      tools.tools.filter((tool) => tool.description?.includes(guide)).length,
      1,
    );

    const schema = await client.callTool({ name: "sal_schema", arguments: {} });
    assert.notEqual(schema.isError, true);
    assert.match(JSON.stringify(schema), /asset/);
    assert.match(JSON.stringify(schema), /blueprint/);
    assert.match(JSON.stringify(schema), /graph/);
    assert.match(JSON.stringify(schema), /state_tree/);

    const query = await client.callTool({
      name: "sal_query",
      arguments: { text: 'query asset\nassets "door"' },
    });
    assert.equal(query.isError, true);
    assert.match(JSON.stringify(query), /No online Loomle runtime was found/);
  } finally {
    await client.close().catch(() => undefined);
  }
  assert.equal(serverStderr, "");

  console.log(`Passed isolated executable smoke tests: ${executablePath}`);
  console.log(`SHA-256: ${executableHash}`);
} finally {
  await rm(directory, { recursive: true, force: true });
}

function parseTarget(args) {
  if (args.length === 0) return `${process.platform}-${process.arch}`;
  if (args.length === 2 && args[0] === "--target" && args[1]) return args[1];
  throw new Error("Usage: node packaging/client/test.mjs [--target <platform-arch>]");
}

function run(command, args, options = {}) {
  const result = spawnSync(command, args, {
    cwd: options.cwd ?? repoRoot,
    env: options.env,
    encoding: "utf8",
    timeout: 15_000,
  });
  if (result.error) throw result.error;
  assert.equal(result.status, options.expectedStatus ?? 0, result.stderr);
  return { stdout: result.stdout, stderr: result.stderr };
}

async function sha256(path) {
  const hash = createHash("sha256");
  for await (const chunk of createReadStream(path)) hash.update(chunk);
  return hash.digest("hex");
}

async function fileContains(path, text) {
  const needle = Buffer.from(text);
  let carry = Buffer.alloc(0);
  for await (const chunk of createReadStream(path)) {
    const data = carry.length === 0 ? chunk : Buffer.concat([carry, chunk]);
    if (data.indexOf(needle) !== -1) return true;
    carry = data.subarray(Math.max(0, data.length - needle.length + 1));
  }
  return false;
}
