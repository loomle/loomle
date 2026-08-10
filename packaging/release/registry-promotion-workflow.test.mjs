import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const workflowUrl = new URL(
  "../../.github/workflows/promote-mcp-registry.yml",
  import.meta.url,
);

test("Registry promotion is manual, OIDC-authenticated, and version-immutable", async () => {
  const workflow = await readFile(workflowUrl, "utf8");
  assert.match(workflow, /workflow_dispatch:/);
  assert.doesNotMatch(workflow, /push:\s*\n\s*tags:/);
  assert.match(workflow, /id-token: write/);
  assert.match(workflow, /release\.draft !== false \|\| release\.prerelease !== false/);
  assert.match(workflow, /versions\/\$VERSION/);
  assert.match(workflow, /already exists and is immutable/);
});

test("Registry promotion verifies exact Release assets before publishing", async () => {
  const workflow = await readFile(workflowUrl, "utf8");
  const verify = workflow.indexOf("node packaging/agent/verify-registry-candidate.mjs");
  const authenticate = workflow.indexOf("mcp-publisher login github-oidc");
  const publish = workflow.indexOf("mcp-publisher publish $CANDIDATE/server.json");
  assert.ok(verify >= 0 && verify < authenticate && authenticate < publish);
  assert.match(workflow, /loomle-mcp-registry-\$version\.mcpb/);
  assert.match(workflow, /loomle-mcp-registry-\$version\.mcpb\.sha256/);
  assert.match(workflow, /--pattern "server\.json"/);
  assert.match(workflow, /mcp-publisher_linux_amd64\.tar\.gz/);
  assert.match(workflow, /a06c9096dcb9727c13555b6be26c7effa707b01f06a4c561ba7a3635443cf2cc/);
  assert.doesNotMatch(workflow, /releases\/latest\/download\/mcp-publisher/);
});
