import assert from "node:assert/strict";
import {
  cp,
  mkdir,
  mkdtemp,
  readFile,
  rm,
  stat,
  writeFile,
} from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";

import { mergePlatformPackages } from "./merge-platform-packages.mjs";

const PLATFORMS = [
  {
    target: "darwin-arm64",
    unrealPlatform: "Mac",
    client: "Resources/Loomle/darwin-arm64/loomle",
    binary: "Binaries/Mac/UnrealEditor-LoomleBridge.dylib",
  },
  {
    target: "win32-x64",
    unrealPlatform: "Win64",
    client: "Resources/Loomle/win32-x64/loomle.exe",
    binary: "Binaries/Win64/UnrealEditor-LoomleBridge.dll",
  },
];

test("merges verified native fragments into one source and one complete package", async () => {
  const fixture = await createFixture();
  try {
    for (const root of [fixture.windowsSource, fixture.windowsPlugin]) {
      await writeFile(join(root, "README.md"), "shared\r\nreadme\r\n");
      await writeFile(
        join(root, "Source", "LoomleBridge", "Bridge.cpp"),
        "shared\r\nsource\r\n",
      );
    }
    for (const root of [fixture.macSource, fixture.macPlugin]) {
      await writeFile(join(root, "README.md"), "shared\nreadme\n");
      await writeFile(
        join(root, "Source", "LoomleBridge", "Bridge.cpp"),
        "shared\nsource\n",
      );
    }
    const result = await mergePlatformPackages(fixture.input);
    assert.deepEqual(result.targets, ["darwin-arm64", "win32-x64"]);

    for (const platform of PLATFORMS) {
      assert.equal(
        await readFile(join(fixture.outputSource, platform.client), "utf8"),
        `${platform.target} client`,
      );
      assert.equal(
        await readFile(join(fixture.outputPlugin, platform.binary), "utf8"),
        `${platform.target} bridge`,
      );
    }
    await assert.rejects(
      stat(join(fixture.outputSource, "Binaries")),
      /ENOENT/,
    );

    const sourceDescriptor = await readJson(
      join(fixture.outputSource, "LoomleBridge.uplugin"),
    );
    const pluginDescriptor = await readJson(
      join(fixture.outputPlugin, "LoomleBridge.uplugin"),
    );
    assert.deepEqual(sourceDescriptor.SupportedTargetPlatforms, ["Mac", "Win64"]);
    assert.deepEqual(sourceDescriptor.Modules[0].PlatformAllowList, ["Mac", "Win64"]);
    assert.equal(sourceDescriptor.Installed, undefined);
    assert.equal(pluginDescriptor.Installed, true);
    assert.equal(
      await readFile(join(fixture.outputSource, "README.md"), "utf8"),
      "shared\nreadme\n",
    );
    assert.equal(
      await readFile(
        join(fixture.outputPlugin, "Source", "LoomleBridge", "Bridge.cpp"),
        "utf8",
      ),
      "shared\nsource\n",
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects shared source drift", async () => {
  const fixture = await createFixture();
  try {
    await writeFile(
      join(fixture.windowsSource, "Source", "LoomleBridge", "Bridge.cpp"),
      "windows drift",
    );
    await writeFile(
      join(fixture.windowsPlugin, "Source", "LoomleBridge", "Bridge.cpp"),
      "windows drift",
    );
    await assert.rejects(
      mergePlatformPackages(fixture.input),
      /native fragments changed shared source file: Source\/LoomleBridge\/Bridge\.cpp/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a missing native Client", async () => {
  const fixture = await createFixture();
  try {
    await rm(join(fixture.windowsSource, PLATFORMS[1].client));
    await rm(join(fixture.windowsPlugin, PLATFORMS[1].client));
    await assert.rejects(
      mergePlatformPackages(fixture.input),
      /Fab source is missing the target Client/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a native descriptor with the wrong platform", async () => {
  const fixture = await createFixture();
  try {
    const descriptorPath = join(fixture.windowsSource, "LoomleBridge.uplugin");
    const descriptor = await readJson(descriptorPath);
    descriptor.SupportedTargetPlatforms = ["Mac"];
    descriptor.Modules[0].PlatformAllowList = ["Mac"];
    await writeFile(descriptorPath, JSON.stringify(descriptor));
    const pluginDescriptorPath = join(fixture.windowsPlugin, "LoomleBridge.uplugin");
    const pluginDescriptor = await readJson(pluginDescriptorPath);
    pluginDescriptor.SupportedTargetPlatforms = ["Mac"];
    pluginDescriptor.Modules[0].PlatformAllowList = ["Mac"];
    await writeFile(pluginDescriptorPath, JSON.stringify(pluginDescriptor));
    await assert.rejects(
      mergePlatformPackages(fixture.input),
      /win32-x64 descriptor does not match its native package role/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

async function createFixture() {
  const root = await mkdtemp(join(tmpdir(), "loomle-package-merge-"));
  const paths = {};
  for (const platform of PLATFORMS) {
    const source = join(root, platform.target, "source", "LoomleBridge");
    const plugin = join(root, platform.target, "plugin", "LoomleBridge");
    await mkdir(join(source, "Config"), { recursive: true });
    await mkdir(join(source, "Content"), { recursive: true });
    await mkdir(join(source, "Resources", "Loomle", platform.target), {
      recursive: true,
    });
    await mkdir(join(source, "Source", "LoomleBridge"), { recursive: true });
    await writeFile(join(source, "Config", "FilterPlugin.ini"), "[FilterPlugin]");
    await writeFile(join(source, "README.md"), "shared readme");
    await writeFile(join(source, "Source", "LoomleBridge", "Bridge.cpp"), "shared source");
    await writeFile(join(source, platform.client), `${platform.target} client`);
    await writeFile(
      join(source, "LoomleBridge.uplugin"),
      JSON.stringify(descriptor(platform.unrealPlatform, false)),
    );
    await cp(source, plugin, { recursive: true });
    const pluginDescriptor = descriptor(platform.unrealPlatform, true);
    await writeFile(
      join(plugin, "LoomleBridge.uplugin"),
      JSON.stringify(pluginDescriptor),
    );
    await mkdir(join(plugin, "Binaries", platform.unrealPlatform), {
      recursive: true,
    });
    await writeFile(join(plugin, platform.binary), `${platform.target} bridge`);
    paths[platform.target] = { source, plugin };
  }

  const outputSource = join(root, "merged", "source", "LoomleBridge");
  const outputPlugin = join(root, "merged", "plugin", "LoomleBridge");
  return {
    root,
    macSource: paths["darwin-arm64"].source,
    macPlugin: paths["darwin-arm64"].plugin,
    windowsSource: paths["win32-x64"].source,
    windowsPlugin: paths["win32-x64"].plugin,
    outputSource,
    outputPlugin,
    input: {
      macSourcePluginRoot: paths["darwin-arm64"].source,
      macPluginRoot: paths["darwin-arm64"].plugin,
      windowsSourcePluginRoot: paths["win32-x64"].source,
      windowsPluginRoot: paths["win32-x64"].plugin,
      outputSourcePluginRoot: outputSource,
      outputPluginRoot: outputPlugin,
    },
  };
}

function descriptor(platform, installed) {
  return {
    FileVersion: 3,
    Version: 107,
    VersionName: "0.7.1",
    CanContainContent: false,
    SupportedTargetPlatforms: [platform],
    ...(installed ? { Installed: true } : {}),
    Modules: [{
      Name: "LoomleBridge",
      Type: "Editor",
      PlatformAllowList: [platform],
    }],
  };
}

async function readJson(path) {
  return JSON.parse(await readFile(path, "utf8"));
}
