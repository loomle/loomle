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
