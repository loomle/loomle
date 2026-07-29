import assert from "node:assert/strict";
import { cp, mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import test from "node:test";

import { verifyPackageDerivation } from "./verify-derivation.mjs";

test("accepts a GitHub package derived by adding only BuildPlugin output", async () => {
  const fixture = await createFixture();
  try {
    const result = await verifyPackageDerivation({
      sourcePluginRoot: fixture.sourceRoot,
      githubPluginRoot: fixture.githubRoot,
      target: "darwin-arm64",
    });
    assert.equal(result.target, "darwin-arm64");
    assert.equal(result.githubGeneratedFileCount, 2);
    assert.ok(result.comparedFileCount >= 7);
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a GitHub package that changes a Fab source file", async () => {
  const fixture = await createFixture();
  try {
    await writeFile(
      join(fixture.githubRoot, "Source", "LoomleBridge", "Bridge.cpp"),
      "changed\n",
    );
    await assert.rejects(
      verifyPackageDerivation({
        sourcePluginRoot: fixture.sourceRoot,
        githubPluginRoot: fixture.githubRoot,
        target: "darwin-arm64",
      }),
      /changed Fab source file: Source\/LoomleBridge\/Bridge\.cpp/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a GitHub package that omits Fab source", async () => {
  const fixture = await createFixture();
  try {
    await rm(
      join(fixture.githubRoot, "Source", "LoomleBridge", "Bridge.cpp"),
      { force: true },
    );
    await assert.rejects(
      verifyPackageDerivation({
        sourcePluginRoot: fixture.sourceRoot,
        githubPluginRoot: fixture.githubRoot,
        target: "darwin-arm64",
      }),
      /omitted Fab source file: Source\/LoomleBridge\/Bridge\.cpp/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a non-build addition in the GitHub package", async () => {
  const fixture = await createFixture();
  try {
    await write(join(fixture.githubRoot, "Docs", "extra.txt"), "extra\n");
    await assert.rejects(
      verifyPackageDerivation({
        sourcePluginRoot: fixture.sourceRoot,
        githubPluginRoot: fixture.githubRoot,
        target: "darwin-arm64",
      }),
      /non-build file absent from Fab source: Docs\/extra\.txt/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects an empty non-build directory in the GitHub package", async () => {
  const fixture = await createFixture();
  try {
    await mkdir(join(fixture.githubRoot, "Build", "Unexpected"), { recursive: true });
    await assert.rejects(
      verifyPackageDerivation({
        sourcePluginRoot: fixture.sourceRoot,
        githubPluginRoot: fixture.githubRoot,
        target: "darwin-arm64",
      }),
      /non-build directory absent from Fab source: Build/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects descriptor changes beyond BuildPlugin installation fields", async () => {
  const fixture = await createFixture();
  try {
    const descriptorPath = join(fixture.githubRoot, "LoomleBridge.uplugin");
    const descriptor = JSON.parse(await readFile(descriptorPath, "utf8"));
    descriptor.VersionName = "unexpected";
    await writeFile(descriptorPath, JSON.stringify(descriptor));
    await assert.rejects(
      verifyPackageDerivation({
        sourcePluginRoot: fixture.sourceRoot,
        githubPluginRoot: fixture.githubRoot,
        target: "darwin-arm64",
      }),
      /descriptor differs from the Fab source/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects generated UE output in the Fab source package", async () => {
  const fixture = await createFixture();
  try {
    await write(join(fixture.sourceRoot, "Binaries", "Mac", "unexpected.dylib"), "binary");
    await assert.rejects(
      verifyPackageDerivation({
        sourcePluginRoot: fixture.sourceRoot,
        githubPluginRoot: fixture.githubRoot,
        target: "darwin-arm64",
      }),
      /Fab source package contains forbidden generated path: Binaries/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

async function createFixture() {
  const root = await mkdtemp(join(tmpdir(), "loomle-package-derivation-"));
  const sourceRoot = join(root, "fab", "LoomleBridge");
  const githubRoot = join(root, "github", "LoomleBridge");
  const descriptor = {
    FileVersion: 3,
    Version: 107,
    VersionName: "0.7.0",
    IsBetaVersion: false,
    SupportedTargetPlatforms: ["Mac"],
    Modules: [{
      Name: "LoomleBridge",
      Type: "Editor",
      PlatformAllowList: ["Mac"],
    }],
  };

  await write(join(sourceRoot, "LoomleBridge.uplugin"), JSON.stringify(descriptor));
  await write(join(sourceRoot, "Config", "FilterPlugin.ini"), "[FilterPlugin]\n");
  await mkdir(join(sourceRoot, "Content"), { recursive: true });
  await write(join(sourceRoot, "Resources", "LoomleToolbarIcon.png"), "icon");
  await write(
    join(sourceRoot, "Resources", "Loomle", "darwin-arm64", "loomle"),
    "client",
  );
  await write(
    join(sourceRoot, "Source", "LoomleBridge", "LoomleBridge.Build.cs"),
    "rules\n",
  );
  await write(
    join(sourceRoot, "Source", "LoomleBridge", "Bridge.cpp"),
    "source\n",
  );
  await write(join(sourceRoot, "README.md"), "readme\n");
  await write(join(sourceRoot, "LICENSE"), "license\n");
  await write(join(sourceRoot, "THIRD_PARTY_NOTICES.txt"), "notices\n");

  await cp(sourceRoot, githubRoot, { recursive: true });
  await write(
    join(githubRoot, "Binaries", "Mac", "UnrealEditor-LoomleBridge.dylib"),
    "bridge",
  );
  await write(
    join(githubRoot, "Binaries", "Mac", "UnrealEditor.modules"),
    "modules",
  );
  await write(
    join(githubRoot, "LoomleBridge.uplugin"),
    JSON.stringify({
      ...descriptor,
      MarketplaceURL: "",
      Installed: true,
      IsBetaVersion: undefined,
    }),
  );
  return { githubRoot, root, sourceRoot };
}

async function write(path, content) {
  await mkdir(dirname(path), { recursive: true });
  await writeFile(path, content);
}
