import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const workflowUrl = new URL(
  "../../.github/workflows/promote-github-release.yml",
  import.meta.url,
);

test("promotion tags the exact verified commit before creating the release", async () => {
  const workflow = await readFile(workflowUrl, "utf8");
  const tagCommand = 'git tag "$TAG" "$HEAD_SHA"';
  const pushCommand = 'git push origin "refs/tags/$TAG"';
  const releaseCommand = 'gh release create "$TAG"';

  const tagIndex = workflow.indexOf(tagCommand);
  const pushIndex = workflow.indexOf(pushCommand);
  const releaseIndex = workflow.indexOf(releaseCommand);

  assert.notEqual(tagIndex, -1);
  assert.notEqual(pushIndex, -1);
  assert.notEqual(releaseIndex, -1);
  assert.ok(tagIndex < pushIndex);
  assert.ok(pushIndex < releaseIndex);
  assert.doesNotMatch(workflow, /gh release create[\s\S]*?--target "\$HEAD_SHA"/);
});
