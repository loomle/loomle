import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import {
  chmod,
  mkdir,
  mkdtemp,
  readFile,
  readdir,
  rm,
  stat,
  symlink,
  writeFile,
} from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import test from "node:test";

import { assembleFabPlugin } from "./assemble.mjs";
import {
  renderBridgeProtocolVersionHeader,
  renderClientProtocolVersionModule,
  renderProductVersionModule,
} from "../tools/product-version.mjs";

const PRODUCT_VERSION = "0.7.0";
const PROTOCOL_VERSION = 2;

test("assembles the Bridge source and only the canonical TypeScript Client executable", async () => {
  const fixture = await createFixture("darwin-arm64");
  try {
    const result = await assembleFabPlugin({
      repoRoot: fixture.repoRoot,
      outputDir: fixture.outputDir,
      target: "darwin-arm64",
    });
    const pluginRoot = join(fixture.outputDir, "LoomleBridge");
    const stagedClient = join(
      pluginRoot,
      "Resources",
      "Loomle",
      "darwin-arm64",
      "loomle",
    );

    assert.equal(result.pluginRoot, pluginRoot);
    assert.equal(result.client, stagedClient);
    assert.equal(result.target, "darwin-arm64");
    assert.equal(result.packageKind, "fab-plugin");
    assert.match(result.clientSha256, /^[0-9a-f]{64}$/);
    assert.equal(await readFile(stagedClient, "utf8"), "canonical-client");
    if (process.platform !== "win32") {
      assert.notEqual((await stat(stagedClient)).mode & 0o111, 0);
    }
    assert.equal(await readFile(join(pluginRoot, "README.md"), "utf8"), "fab readme\n");
    assert.equal(await exists(join(pluginRoot, "Source", "LoomleBridge", "LoomleBridge.Build.cs")), true);
    assert.equal(
      await exists(join(pluginRoot, "Source", "LoomleBridge", "Private", "Tests")),
      false,
    );
    assert.equal(
      await exists(join(
        pluginRoot,
        "Source",
        "LoomleBridge",
        "Private",
        "Tests",
        "SalTestReflectedSchema.h",
      )),
      false,
    );
    assert.equal(await exists(join(pluginRoot, "Resources", "MCP")), false);
    assert.equal(
      await exists(join(pluginRoot, "Resources", "Loomle", "darwin-arm64", "build.json")),
      false,
    );
    assert.equal(await exists(join(pluginRoot, "Content")), true);
    assert.deepEqual(await readdir(join(pluginRoot, "Content")), []);
    assert.equal(await readFile(join(pluginRoot, "LICENSE"), "utf8"), "loomle license\n");
    const notices = await readFile(join(pluginRoot, "THIRD_PARTY_NOTICES.txt"), "utf8");
    assert.match(notices, /Node\.js 24\.18\.0/);
    assert.match(notices, /node license text/);
    assert.match(notices, /example-package 1\.2\.3/);
    assert.match(notices, /example dependency license/);
    assert.equal(await exists(join(pluginRoot, "Binaries")), false);
    assert.equal(await exists(join(pluginRoot, "Intermediate")), false);
    assert.equal(await exists(join(pluginRoot, "Saved")), false);
    assert.equal(await exists(join(pluginRoot, "Resources", "Loomle", "stale-target")), false);
    const descriptor = JSON.parse(await readFile(
      join(pluginRoot, "LoomleBridge.uplugin"),
      "utf8",
    ));
    assert.deepEqual(descriptor.SupportedTargetPlatforms, ["Mac"]);
    assert.deepEqual(descriptor.Modules[0].PlatformAllowList, ["Mac"]);
    assert.equal(descriptor.Modules[0].PlatformArchitectureAllowList, undefined);
    const sourceDescriptor = JSON.parse(await readFile(
      join(fixture.repoRoot, "engine", "LoomleBridge", "LoomleBridge.uplugin"),
      "utf8",
    ));
    assert.deepEqual(sourceDescriptor.Modules[0].PlatformAllowList, ["Mac", "Win64"]);
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("assembles the canonical Windows x64 Client and narrows the descriptor to Win64", async () => {
  const fixture = await createFixture("win32-x64");
  try {
    const result = await assembleFabPlugin({
      repoRoot: fixture.repoRoot,
      outputDir: fixture.outputDir,
      target: "win32-x64",
    });
    const pluginRoot = join(fixture.outputDir, "LoomleBridge");
    const stagedClient = join(
      pluginRoot,
      "Resources",
      "Loomle",
      "win32-x64",
      "loomle.exe",
    );

    assert.equal(result.client, stagedClient);
    assert.equal(result.target, "win32-x64");
    assert.equal(await readFile(stagedClient, "utf8"), "canonical-client");
    const descriptor = JSON.parse(await readFile(
      join(pluginRoot, "LoomleBridge.uplugin"),
      "utf8",
    ));
    assert.deepEqual(descriptor.SupportedTargetPlatforms, ["Win64"]);
    assert.deepEqual(descriptor.Modules[0].PlatformAllowList, ["Win64"]);
    assert.equal(descriptor.Modules[0].PlatformArchitectureAllowList, undefined);
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects native targets that have not completed the QA acceptance path", async () => {
  const fixture = await createFixture("darwin-arm64");
  try {
    for (const target of ["darwin-x64", "linux-x64", "unknown"]) {
      await assert.rejects(
        assembleFabPlugin({
          repoRoot: fixture.repoRoot,
          outputDir: fixture.outputDir,
          target,
        }),
        /unsupported Fab target .*; accepted targets: darwin-arm64, win32-x64/,
      );
    }
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a source descriptor without the target module", async () => {
  const fixture = await createFixture("darwin-arm64", { missingModule: true });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /Modules must contain exactly one module named "LoomleBridge"/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a source descriptor with a release-visible test module", async () => {
  const fixture = await createFixture("darwin-arm64", { extraTestModule: true });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /Modules must contain exactly one module named "LoomleBridge"/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a source module that does not allow the target platform", async () => {
  const fixture = await createFixture("darwin-arm64", { disallowMac: true });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /source LoomleBridge module does not allow Mac/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a source plugin that does not support the target platform", async () => {
  const fixture = await createFixture("darwin-arm64", { disallowPluginMac: true });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /source LoomleBridge plugin does not support Mac/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("removes an architecture filter that would skip the module in a universal Mac Editor", async () => {
  const fixture = await createFixture("darwin-arm64", {
    sourceArchitectureAllowList: ["Mac:arm64"],
  });
  try {
    await assembleFabPlugin({
      repoRoot: fixture.repoRoot,
      outputDir: fixture.outputDir,
      target: "darwin-arm64",
    });
    const descriptor = JSON.parse(await readFile(
      join(fixture.outputDir, "LoomleBridge", "LoomleBridge.uplugin"),
      "utf8",
    ));
    assert.equal(descriptor.Modules[0].PlatformArchitectureAllowList, undefined);
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("fails instead of falling back when the canonical Client is missing", async () => {
  const fixture = await createFixture("darwin-arm64", { includeClient: false });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /canonical Client executable not found: .*\.tmp[\\/]client[\\/]darwin-arm64[\\/]loomle/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a canonical Client without its build receipt", async () => {
  const fixture = await createFixture("darwin-arm64", { includeReceipt: false });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /canonical Client build receipt not found: .*\.tmp[\\/]client[\\/]darwin-arm64[\\/]build\.json/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects the obsolete Client receipt schema", async () => {
  const fixture = await createFixture("darwin-arm64", { receiptSchemaVersion: 2 });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /build receipt must use schemaVersion 3/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a canonical Client receipt from another product version", async () => {
  const fixture = await createFixture("darwin-arm64", { receiptProductVersion: "0.6.0" });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /build receipt productVersion "0\.6\.0" does not match product version "0\.7\.0"/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a canonical Client receipt from another protocol version", async () => {
  const fixture = await createFixture("darwin-arm64", { receiptProtocolVersion: 1 });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /build receipt protocolVersion 1 does not match protocol version 2/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects stale generated protocol artifacts before assembly", async () => {
  const fixture = await createFixture("darwin-arm64", { staleBridgeProtocolHeader: true });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /Generated\/LoomleProtocolVersion\.h/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a canonical Client receipt from another pinned Node version", async () => {
  const fixture = await createFixture("darwin-arm64", { receiptNodeVersion: "22.0.0" });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /build receipt nodeVersion "22\.0\.0" does not match pinned Node version "24\.18\.0"/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a canonical Client receipt from another pinned runtime archive", async () => {
  const fixture = await createFixture("darwin-arm64", { receiptRuntimeSha256: "1".repeat(64) });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /build receipt runtimeSha256 "1{64}" does not match pinned runtime SHA-256 "2{64}"/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a canonical Client whose bytes do not match its build receipt", async () => {
  const fixture = await createFixture("darwin-arm64", { receiptSha256: "0".repeat(64) });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /executable SHA-256 [0-9a-f]{64} does not match build receipt 0{64}/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a missing canonical Node runtime license", async () => {
  const fixture = await createFixture("darwin-arm64", { includeNodeLicense: false });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /canonical Client Node license not found/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a canonical Node runtime license whose bytes do not match its receipt", async () => {
  const fixture = await createFixture("darwin-arm64", {
    receiptNodeLicenseSha256: "0".repeat(64),
  });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /Node license SHA-256 [0-9a-f]{64} does not match build receipt 0{64}/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a production dependency without distributable license text", async () => {
  const fixture = await createFixture("darwin-arm64", { missingDependencyLicense: true });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /example-package@1\.2\.3 has no distributable license file/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a stale staged plugin version", async () => {
  const fixture = await createFixture("darwin-arm64", { pluginVersion: "0.6.0" });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /LoomleBridge\.uplugin VersionName is "0\.6\.0"; expected "0\.7\.0"/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a FilterPlugin contract that still names Resources\/MCP", async () => {
  const fixture = await createFixture("darwin-arm64", { legacyFilter: true });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /FilterPlugin\.ini is missing required entries:.*Resources\/Loomle/s,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a FilterPlugin contract that can retain BuildPlugin Intermediate output", async () => {
  const fixture = await createFixture("darwin-arm64", {
    missingIntermediateExclusion: true,
  });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /FilterPlugin\.ini is missing required entries:.*-\/Intermediate\/\.\.\./s,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects unexpected platform binaries outside the canonical Client path", async () => {
  const fixture = await createFixture("darwin-arm64", { unexpectedBinary: true });
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: fixture.outputDir,
        target: "darwin-arm64",
      }),
      /unexpected platform\/build outputs:.*unrelated\.dll/s,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("refuses to reset an output directory that contains an assembly input", async () => {
  const fixture = await createFixture("darwin-arm64");
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: join(fixture.repoRoot, ".tmp"),
        target: "darwin-arm64",
      }),
      /output directory must not overlap an assembly input/,
    );
    assert.equal(
      await exists(join(fixture.repoRoot, ".tmp", "client", "darwin-arm64", "loomle")),
      true,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("refuses an output directory nested inside the Bridge source", async () => {
  const fixture = await createFixture("darwin-arm64");
  const sourceFile = join(
    fixture.repoRoot,
    "engine",
    "LoomleBridge",
    "Source",
    "LoomleBridge",
    "LoomleBridge.Build.cs",
  );
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: join(fixture.repoRoot, "engine", "LoomleBridge", "Source"),
        target: "darwin-arm64",
      }),
      /output directory must not overlap an assembly input/,
    );
    assert.equal(await readFile(sourceFile, "utf8"), "build rules\n");
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("resolves symbolic-link output aliases before allowing destructive reset", async () => {
  const fixture = await createFixture("darwin-arm64");
  const sourceDirectory = join(
    fixture.repoRoot,
    "engine",
    "LoomleBridge",
    "Source",
  );
  const sourceFile = join(sourceDirectory, "LoomleBridge", "LoomleBridge.Build.cs");
  const outputAlias = join(fixture.root, "output-alias");
  await symlink(
    sourceDirectory,
    outputAlias,
    process.platform === "win32" ? "junction" : "dir",
  );
  try {
    await assert.rejects(
      assembleFabPlugin({
        repoRoot: fixture.repoRoot,
        outputDir: outputAlias,
        target: "darwin-arm64",
      }),
      /output directory must not overlap an assembly input/,
    );
    assert.equal(await readFile(sourceFile, "utf8"), "build rules\n");
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

async function createFixture(target, options = {}) {
  const root = await mkdtemp(join(tmpdir(), "loomle-fab-assembler-"));
  const repoRoot = join(root, "repo");
  const outputDir = join(root, "output");
  const pluginRoot = join(repoRoot, "engine", "LoomleBridge");
  const executableName = target.startsWith("win32-") ? "loomle.exe" : "loomle";

  await write(join(repoRoot, "package.json"), JSON.stringify({
    name: "loomle",
    version: PRODUCT_VERSION,
    loomle: { protocolVersion: PROTOCOL_VERSION },
  }));
  for (const workspace of ["client", "sal", "interfaces"]) {
    await write(join(repoRoot, workspace, "package.json"), JSON.stringify({
      name: `@loomle/${workspace}`,
      version: "0.0.0",
    }));
  }
  await write(join(repoRoot, "package-lock.json"), JSON.stringify({
    name: "loomle",
    version: PRODUCT_VERSION,
    packages: {
      "": { name: "loomle", version: PRODUCT_VERSION },
      client: { name: "@loomle/client", version: "0.0.0" },
      sal: { name: "@loomle/sal", version: "0.0.0" },
      interfaces: { name: "@loomle/interfaces", version: "0.0.0" },
      "node_modules/example-package": { version: "1.2.3" },
    },
  }));
  await write(join(repoRoot, "LICENSE"), "loomle license\n");
  await write(
    join(repoRoot, "node_modules", "example-package", "package.json"),
    JSON.stringify({
      name: "example-package",
      version: "1.2.3",
      license: "MIT",
    }),
  );
  if (!options.missingDependencyLicense) {
    await write(
      join(repoRoot, "node_modules", "example-package", "LICENSE"),
      "example dependency license\n",
    );
  }
  await write(
    join(repoRoot, "client", "src", "generated", "product-version.ts"),
    renderProductVersionModule(PRODUCT_VERSION),
  );
  await write(
    join(repoRoot, "client", "src", "generated", "protocol-version.ts"),
    renderClientProtocolVersionModule(PROTOCOL_VERSION),
  );
  await write(
    join(pluginRoot, "Source", "LoomleBridge", "Private", "Generated", "LoomleProtocolVersion.h"),
    options.staleBridgeProtocolHeader
      ? "stale\n"
      : renderBridgeProtocolVersionHeader(PROTOCOL_VERSION),
  );
  await write(
    join(repoRoot, "packaging", "client", "node-runtime.json"),
    JSON.stringify({
      nodeVersion: "24.18.0",
      targets: { [target]: { sha256: "2".repeat(64) } },
    }),
  );
  await write(join(pluginRoot, "LoomleBridge.uplugin"), JSON.stringify({
    FileVersion: 3,
    Version: 107,
    VersionName: options.pluginVersion ?? PRODUCT_VERSION,
    CanContainContent: false,
    SupportedTargetPlatforms: options.disallowPluginMac ? ["Win64"] : ["Mac", "Win64"],
    Modules: [
      {
        Name: options.missingModule ? "AnotherModule" : "LoomleBridge",
        Type: "Editor",
        LoadingPhase: "PostEngineInit",
        PlatformAllowList: options.disallowMac ? ["Win64"] : ["Mac", "Win64"],
        ...(options.sourceArchitectureAllowList
          ? { PlatformArchitectureAllowList: options.sourceArchitectureAllowList }
          : {}),
      },
      ...(options.extraTestModule
        ? [{
          Name: "LoomleBridgeTests",
          Type: "Editor",
          LoadingPhase: "PostEngineInit",
          PlatformAllowList: ["Mac", "Win64"],
        }]
        : []),
    ],
  }));
  await write(join(pluginRoot, "Source", "LoomleBridge", "LoomleBridge.Build.cs"), "build rules\n");
  await write(
    join(pluginRoot, "Source", "LoomleBridge", "Private", "Tests", "BridgeTests.cpp"),
    "test-only source\n",
  );
  await write(
    join(
      pluginRoot,
      "Source",
      "LoomleBridge",
      "Private",
      "Tests",
      "SalTestReflectedSchema.h",
    ),
    [
      "#pragma once",
      "",
      "#include \"CoreMinimal.h\"",
      "#include \"SalTestReflectedSchema.generated.h\"",
      "",
      "USTRUCT()",
      "struct FLOOMLETESTREFLECTEDSCHEMA",
      "{",
      "  GENERATED_BODY()",
      "",
      "  UPROPERTY()",
      "  int32 Value = 0;",
      "};",
      "",
      "UCLASS()",
      "class ULOOMLETESTREFLECTEDOBJECT : public UObject",
      "{",
      "  GENERATED_BODY()",
      "};",
      "",
    ].join("\n"),
  );
  await write(
    join(pluginRoot, "Config", "FilterPlugin.ini"),
    options.legacyFilter
      ? "[FilterPlugin]\n/Config/FilterPlugin.ini\n/Resources/MCP/...\n/Resources/LoomleToolbarIcon.png\n/README.md\n/LICENSE\n/THIRD_PARTY_NOTICES.txt\n"
      : "[FilterPlugin]\n/Config/FilterPlugin.ini\n/Resources/Loomle/...\n/Resources/LoomleToolbarIcon.png\n/README.md\n/LICENSE\n/THIRD_PARTY_NOTICES.txt\n"
        + (options.missingIntermediateExclusion ? "" : "-/Intermediate/...\n"),
  );
  await write(join(pluginRoot, "Resources", "LoomleToolbarIcon.png"), "icon");
  await write(join(pluginRoot, "Resources", "MCP", "legacy.py"), "retired");
  await write(join(pluginRoot, "Resources", "Loomle", "stale-target", "loomle"), "stale");
  if (options.unexpectedBinary) {
    await write(join(pluginRoot, "Resources", "unrelated.dll"), "build output");
  }
  await write(join(pluginRoot, "Binaries", "Mac", "bridge.dylib"), "build output");
  await write(join(pluginRoot, "Intermediate", "cache"), "build output");
  await write(join(pluginRoot, "Saved", "cache"), "build output");
  await write(join(pluginRoot, "Content", "placeholder"), "unused");
  await write(join(repoRoot, "packaging", "fab", "FAB_PLUGIN_README.md"), "fab readme\n");

  if (options.includeClient !== false) {
    const client = join(repoRoot, ".tmp", "client", target, executableName);
    const nodeLicense = join(dirname(client), "node-license.txt");
    await write(client, "canonical-client");
    if (options.includeNodeLicense !== false) {
      await write(nodeLicense, "node license text\n");
    }
    if (!target.startsWith("win32-")) await chmod(client, 0o755);
    if (options.includeReceipt !== false) {
      await write(join(dirname(client), "build.json"), `${JSON.stringify({
        schemaVersion: options.receiptSchemaVersion ?? 3,
        productVersion: options.receiptProductVersion ?? PRODUCT_VERSION,
        protocolVersion: options.receiptProtocolVersion ?? PROTOCOL_VERSION,
        target,
        nodeVersion: options.receiptNodeVersion ?? "24.18.0",
        runtimeSha256: options.receiptRuntimeSha256 ?? "2".repeat(64),
        executable: executableName,
        sha256: options.receiptSha256
          ?? createHash("sha256").update("canonical-client").digest("hex"),
        nodeLicenseSha256: options.receiptNodeLicenseSha256
          ?? createHash("sha256").update("node license text\n").digest("hex"),
      }, null, 2)}\n`);
    }
  }
  return { root, repoRoot, outputDir };
}

async function write(path, content) {
  await mkdir(dirname(path), { recursive: true });
  await writeFile(path, content);
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
