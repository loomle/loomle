import assert from "node:assert/strict";
import { mkdtemp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import test from "node:test";
import {
  checkProductVersion,
  generateProductVersion,
  renderProductVersionModule,
} from "./product-version.mjs";

const VERSION = "0.7.0-dev.1";

async function writeJson(root, relativePath, value) {
  const path = join(root, relativePath);
  await mkdir(dirname(path), { recursive: true });
  await writeFile(path, `${JSON.stringify(value, null, 2)}\n`, "utf8");
}

async function createFixture() {
  const root = await mkdtemp(join(tmpdir(), "loomle-product-version-"));
  await writeJson(root, "package.json", { name: "loomle", version: VERSION });
  for (const workspace of ["client", "sal", "interfaces"]) {
    await writeJson(root, `${workspace}/package.json`, {
      name: `@loomle/${workspace}`,
      version: "0.0.0",
    });
  }
  await writeJson(root, "package-lock.json", {
    name: "loomle",
    version: VERSION,
    packages: {
      "": { name: "loomle", version: VERSION },
      client: { name: "@loomle/client", version: "0.0.0" },
      sal: { name: "@loomle/sal", version: "0.0.0" },
      interfaces: { name: "@loomle/interfaces", version: "0.0.0" },
    },
  });
  await writeJson(root, "engine/LoomleBridge/LoomleBridge.uplugin", {
    FileVersion: 3,
    Version: 107,
    VersionName: "stale",
    EngineVersion: "5.7.0",
  });
  return root;
}

test("generates runtime product versions without changing the Fab build number", async () => {
  const root = await createFixture();
  try {
    const result = await generateProductVersion(root);
    assert.deepEqual(result, {
      version: VERSION,
      generatedChanged: true,
      pluginChanged: true,
    });
    assert.equal(
      await readFile(join(root, "client/src/generated/product-version.ts"), "utf8"),
      renderProductVersionModule(VERSION),
    );
    const plugin = JSON.parse(
      await readFile(join(root, "engine/LoomleBridge/LoomleBridge.uplugin"), "utf8"),
    );
    assert.equal(plugin.VersionName, VERSION);
    assert.equal(plugin.Version, 107);
    assert.equal(await checkProductVersion(root), VERSION);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("check mode rejects stale generated values without rewriting them", async () => {
  const root = await createFixture();
  try {
    await generateProductVersion(root);
    const generatedPath = join(root, "client/src/generated/product-version.ts");
    await writeFile(generatedPath, "stale\n", "utf8");
    await assert.rejects(
      checkProductVersion(root),
      (error) => error instanceof Error
        && error.message.includes("client/src/generated/product-version.ts")
        && error.message.includes("npm run generate:version"),
    );
    assert.equal(await readFile(generatedPath, "utf8"), "stale\n");
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
