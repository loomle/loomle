import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const workflowUrl = new URL(
  "../../.github/workflows/promote-github-release.yml",
  import.meta.url,
);

test("promotion validates a maintainer tag before creating the release", async () => {
  const workflow = await readFile(workflowUrl, "utf8");
  const tagLookup =
    'gh api "repos/$GITHUB_REPOSITORY/git/ref/tags/$TAG" > "$tag_ref"';
  const exactTagCheck =
    '[[ "$tag_type" != "commit" || "$tag_sha" != "$HEAD_SHA" ]]';
  const releaseCommand = 'gh release create "$TAG"';

  const tagIndex = workflow.indexOf(tagLookup);
  const exactTagIndex = workflow.indexOf(exactTagCheck);
  const releaseIndex = workflow.indexOf(releaseCommand);

  assert.notEqual(tagIndex, -1);
  assert.notEqual(exactTagIndex, -1);
  assert.notEqual(releaseIndex, -1);
  assert.ok(tagIndex < exactTagIndex);
  assert.ok(exactTagIndex < releaseIndex);
  assert.doesNotMatch(workflow, /git push origin "refs\/tags\/\$TAG"/);
  assert.doesNotMatch(workflow, /gh release create[\s\S]*?--target "\$HEAD_SHA"/);
});

test("promotion publishes versioned archives and final-only stable aliases", async () => {
  const workflow = await readFile(workflowUrl, "utf8");
  const mergeCommand = "node packaging/release/merge-platform-packages.mjs";
  const releaseCommand = 'gh release create "$TAG"';

  assert.ok(workflow.indexOf(mergeCommand) < workflow.indexOf(releaseCommand));
  assert.match(workflow, /loomle-bridge-\$VERSION-source\.zip/);
  assert.match(workflow, /loomle-bridge-\$VERSION-fab-source\.zip/);
  assert.match(workflow, /--distribution fab/);
  assert.match(workflow, /value\.channel !== "fab"/);
  assert.match(workflow, /value\.channel !== "github"/);
  assert.match(workflow, /darwin-arm64\/distribution\.json/);
  assert.match(workflow, /win32-x64\/distribution\.json/);
  assert.match(workflow, /fab-source\/LoomleBridge\/Resources\/Loomle\/darwin-arm64\/loomle/);
  assert.match(workflow, /fab-source\/LoomleBridge\/Resources\/Loomle\/win32-x64\/loomle\.exe/);
  assert.match(workflow, /loomle-bridge-\$VERSION-\$ue\.zip/);
  assert.match(workflow, /source_alias="\$release_root\/assets\/loomle-bridge-source\.zip"/);
  assert.match(workflow, /plugin_alias="\$release_root\/assets\/loomle-bridge-\$ue\.zip"/);
  assert.match(workflow, /cp "\$source_archive" "\$source_alias"/);
  assert.match(workflow, /cp "\$plugin_archive" "\$plugin_alias"/);
  assert.match(workflow, /cmp -s "\$source_archive" "\$source_alias"/);
  assert.match(workflow, /cmp -s "\$plugin_archive" "\$plugin_alias"/);
  assert.match(
    workflow,
    /release_assets=\([\s\S]*?"\$UE_5_7_ARCHIVE"[\s\S]*?"\$UE_5_8_ARCHIVE"[\s\S]*?"\$UE_5_7_ALIAS"[\s\S]*?"\$UE_5_8_ALIAS"[\s\S]*?\)/,
  );
  assert.match(
    workflow,
    /if \[\[ "\$CHANNEL" == "prerelease" \]\]; then[\s\S]*?release_assets=\([\s\S]*?"\$UE_5_7_ARCHIVE"[\s\S]*?"\$UE_5_8_ARCHIVE"[\s\S]*?\)/,
  );
  assert.match(
    workflow,
    /gh release create "\$TAG" \\\n\s+"\$\{release_assets\[@\]\}"/,
  );
  const publishStep = workflow.slice(workflow.indexOf(releaseCommand));
  assert.doesNotMatch(publishStep, /\$MAC_(FAB_SOURCE|ARCHIVE)/);
  assert.doesNotMatch(publishStep, /\$WINDOWS_(FAB_SOURCE|ARCHIVE)/);
  assert.doesNotMatch(workflow, /loomle-bridge-\$VERSION\.zip/);
  assert.doesNotMatch(workflow, /loomle-bridge\.zip/);
});

test("promotion publishes immutable Registry and Claude candidates without promoting stores", async () => {
  const workflow = await readFile(workflowUrl, "utf8");
  const assembleIndex = workflow.indexOf("npm run assemble:agents");
  const releaseIndex = workflow.indexOf('gh release create "$TAG"');

  assert.notEqual(assembleIndex, -1);
  assert.ok(assembleIndex < releaseIndex);
  assert.match(workflow, /loomle-mcp-registry-\$VERSION\.mcpb/);
  assert.match(workflow, /loomle-claude-\$VERSION\.mcpb/);
  assert.match(workflow, /Internal Codex compatibility package is missing/);
  assert.match(workflow, /receipt\.packages\.mcpRegistry\.clientSha256/);
  assert.match(workflow, /receipt\.packages\.claude\.clientSha256/);
  assert.match(workflow, /receipt\.packages\.codex\.clientSha256/);
  assert.match(workflow, /"\$REGISTRY_SERVER"/);
  const publishStep = workflow.slice(workflow.indexOf('gh release create "$TAG"'));
  assert.doesNotMatch(publishStep, /CODEX_ARCHIVE|loomle-codex-marketplace/);
  assert.doesNotMatch(workflow, /mcp-publisher publish/);
  assert.doesNotMatch(workflow, /codex plugin marketplace/);
  assert.doesNotMatch(workflow, /claude.*submit/i);
});

test("promotion requires both UE versions from each native verification run", async () => {
  const workflow = await readFile(workflowUrl, "utf8");
  for (const engine of ["5.7", "5.8"]) {
    assert.match(workflow, new RegExp(`loomle-fab-plugin-\\$ue-darwin-arm64`));
    assert.match(workflow, new RegExp(`--ue${engine.replace(".", "\\.")}-darwin-arm64-archive`));
    assert.match(workflow, new RegExp(`--ue${engine.replace(".", "\\.")}-win32-x64-archive`));
  }
});

test("native verification compiles each engine candidate exactly once", async () => {
  for (const name of ["verify-fab-mac.yml", "verify-fab-windows.yml"]) {
    const workflow = await readFile(
      new URL(`../../.github/workflows/${name}`, import.meta.url),
      "utf8",
    );
    assert.equal(
      (workflow.match(/(?:"\$RUN_UAT"|& \$env:RUN_UAT) BuildPlugin/g) ?? []).length,
      1,
      `${name} should invoke BuildPlugin exactly once per engine job`,
    );
    assert.match(workflow, /tested-plugin\.mjs prepare/);
    assert.match(workflow, /tested-plugin\.mjs finalize/);
    assert.match(workflow, /Source[\\/]LoomleBridgeTests/);
    assert.doesNotMatch(workflow, /Source[\\/]LoomleBridge[\\/]Private[\\/]Tests/);
  }
});
