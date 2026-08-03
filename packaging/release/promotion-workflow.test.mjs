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
  assert.match(workflow, /loomle-bridge-\$VERSION\.zip/);
  assert.match(workflow, /source_alias="\$release_root\/assets\/loomle-bridge-source\.zip"/);
  assert.match(workflow, /plugin_alias="\$release_root\/assets\/loomle-bridge\.zip"/);
  assert.match(workflow, /cp "\$source_archive" "\$source_alias"/);
  assert.match(workflow, /cp "\$plugin_archive" "\$plugin_alias"/);
  assert.match(workflow, /cmp -s "\$source_archive" "\$source_alias"/);
  assert.match(workflow, /cmp -s "\$plugin_archive" "\$plugin_alias"/);
  assert.match(
    workflow,
    /release_assets=\(\s+"\$SOURCE_ARCHIVE"\s+"\$SOURCE_SHA_FILE"\s+"\$PLUGIN_ARCHIVE"\s+"\$PLUGIN_SHA_FILE"\s+"\$SOURCE_ALIAS"\s+"\$SOURCE_ALIAS_SHA_FILE"\s+"\$PLUGIN_ALIAS"\s+"\$PLUGIN_ALIAS_SHA_FILE"\s+\)/,
  );
  assert.match(
    workflow,
    /if \[\[ "\$CHANNEL" == "prerelease" \]\]; then[\s\S]*?release_assets=\(\s+"\$SOURCE_ARCHIVE"\s+"\$SOURCE_SHA_FILE"\s+"\$PLUGIN_ARCHIVE"\s+"\$PLUGIN_SHA_FILE"\s+\)/,
  );
  assert.match(
    workflow,
    /gh release create "\$TAG" \\\n\s+"\$\{release_assets\[@\]\}"/,
  );
  const publishStep = workflow.slice(workflow.indexOf(releaseCommand));
  assert.doesNotMatch(publishStep, /\$MAC_(FAB_SOURCE|ARCHIVE)/);
  assert.doesNotMatch(publishStep, /\$WINDOWS_(FAB_SOURCE|ARCHIVE)/);
});
