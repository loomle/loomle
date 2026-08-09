import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import {
  mkdir,
  mkdtemp,
  readFile,
  rm,
  stat,
  writeFile,
} from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import test from "node:test";

import {
  finalizeTestedPlugin,
  prepareTestedPlugin,
} from "./tested-plugin.mjs";

test("prepares one BuildPlugin input by overlaying only the development test module", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-tested-plugin-prepare-"));
  try {
    const repoRoot = join(root, "repo");
    const sourcePlugin = join(root, "source", "LoomleBridge");
    const outputDir = join(root, "tested");
    await writeJson(join(repoRoot, "engine", "LoomleBridge", "LoomleBridge.uplugin"),
      descriptor([bridgeModule(["Mac", "Win64"]), testModule(["Mac", "Win64"])]));
    await write(
      join(repoRoot, "engine", "LoomleBridge", "Source", "LoomleBridgeTests", "LoomleBridgeTests.Build.cs"),
      "test build rules\n",
    );
    await write(
      join(repoRoot, "engine", "LoomleBridge", "Source", "LoomleBridgeTests", "Private", "BridgeTests.cpp"),
      "tests\n",
    );
    await writeJson(
      join(sourcePlugin, "LoomleBridge.uplugin"),
      descriptor([bridgeModule(["Mac"])]),
    );
    await write(
      join(sourcePlugin, "Source", "LoomleBridge", "LoomleBridge.Build.cs"),
      "production build rules\n",
    );
    await write(join(sourcePlugin, "Resources", "Loomle", "client"), "client\n");

    const result = await prepareTestedPlugin({ repoRoot, sourcePlugin, outputDir });
    const preparedDescriptor = await readJson(
      join(result.pluginRoot, "LoomleBridge.uplugin"),
    );
    assert.deepEqual(
      preparedDescriptor.Modules.map(({ Name }) => Name),
      ["LoomleBridge", "LoomleBridgeTests"],
    );
    assert.deepEqual(preparedDescriptor.Modules[1].PlatformAllowList, ["Mac"]);
    assert.equal(
      await readFile(
        join(result.pluginRoot, "Source", "LoomleBridgeTests", "Private", "BridgeTests.cpp"),
        "utf8",
      ),
      "tests\n",
    );
    assert.equal(
      await readFile(
        join(result.pluginRoot, "Source", "LoomleBridge", "LoomleBridge.Build.cs"),
        "utf8",
      ),
      "production build rules\n",
    );
    assert.deepEqual(
      (await readJson(join(sourcePlugin, "LoomleBridge.uplugin"))).Modules.map(({ Name }) => Name),
      ["LoomleBridge"],
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

for (const [target, binariesDirectory, productionBinary, testBinary] of [
  ["darwin-arm64", "Mac", "libUnrealEditor-LoomleBridge.dylib", "libUnrealEditor-LoomleBridgeTests.dylib"],
  ["win32-x64", "Win64", "UnrealEditor-LoomleBridge.dll", "UnrealEditor-LoomleBridgeTests.dll"],
]) {
  test(`finalizes the exact tested production binary for ${target}`, async () => {
    const root = await mkdtemp(join(tmpdir(), "loomle-tested-plugin-finalize-"));
    try {
      const pluginDir = join(root, "LoomleBridge");
      const binaries = join(pluginDir, "Binaries", binariesDirectory);
      const receiptPath = join(root, "receipt.json");
      await writeJson(
        join(pluginDir, "LoomleBridge.uplugin"),
        descriptor([bridgeModule([binariesDirectory]), testModule([binariesDirectory])]),
      );
      await write(join(pluginDir, "Source", "LoomleBridge", "Private", "Bridge.cpp"), "source\n");
      await write(join(pluginDir, "Source", "LoomleBridgeTests", "Private", "BridgeTests.cpp"), "tests\n");
      await write(join(binaries, productionBinary), "production-binary\n");
      await write(join(binaries, testBinary), "test-binary\n");
      await write(join(binaries, "UnrealEditor-LoomleBridgeTests.pdb"), "symbols\n");
      await writeJson(join(binaries, "UnrealEditor.modules"), {
        BuildId: "build-id",
        Modules: {
          LoomleBridge: productionBinary,
          LoomleBridgeTests: testBinary,
        },
      });

      const before = sha256("production-binary\n");
      const receipt = await finalizeTestedPlugin({ pluginDir, target, receiptPath });
      assert.equal(receipt.productionSha256, before);
      assert.equal(receipt.productionBinary, `Binaries/${binariesDirectory}/${productionBinary}`);
      assert.equal(
        sha256(await readFile(join(binaries, productionBinary), "utf8")),
        before,
      );
      assert.deepEqual(
        (await readJson(join(pluginDir, "LoomleBridge.uplugin"))).Modules.map(({ Name }) => Name),
        ["LoomleBridge"],
      );
      assert.deepEqual(
        (await readJson(join(binaries, "UnrealEditor.modules"))).Modules,
        { LoomleBridge: productionBinary },
      );
      assert.equal(await exists(join(pluginDir, "Source", "LoomleBridgeTests")), false);
      assert.equal(await exists(join(binaries, testBinary)), false);
      assert.equal(await exists(join(binaries, "UnrealEditor-LoomleBridgeTests.pdb")), false);
      assert.deepEqual(await readJson(receiptPath), receipt);
    } finally {
      await rm(root, { recursive: true, force: true });
    }
  });
}

function descriptor(Modules) {
  return {
    FileVersion: 3,
    Version: 107,
    VersionName: "0.7.9",
    CanContainContent: false,
    Modules,
  };
}

function bridgeModule(PlatformAllowList) {
  return {
    Name: "LoomleBridge",
    Type: "Editor",
    LoadingPhase: "PostEngineInit",
    PlatformAllowList,
  };
}

function testModule(PlatformAllowList) {
  return {
    Name: "LoomleBridgeTests",
    Type: "Editor",
    LoadingPhase: "PostEngineInit",
    PlatformAllowList,
  };
}

async function write(path, contents) {
  await mkdir(dirname(path), { recursive: true });
  await writeFile(path, contents);
}

async function writeJson(path, value) {
  await write(path, `${JSON.stringify(value, null, 2)}\n`);
}

async function readJson(path) {
  return JSON.parse(await readFile(path, "utf8"));
}

async function exists(path) {
  try {
    await stat(path);
    return true;
  } catch (error) {
    if (error?.code === "ENOENT") return false;
    throw error;
  }
}

function sha256(value) {
  return createHash("sha256").update(value).digest("hex");
}
