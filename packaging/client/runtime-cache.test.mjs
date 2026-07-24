import assert from "node:assert/strict";
import { resolve } from "node:path";
import test from "node:test";
import {
  CLIENT_RUNTIME_CACHE_ENV,
  resolveClientRuntimeCache,
} from "./runtime-cache.mjs";

test("defaults Client runtime cache to the disposable repository cache", () => {
  assert.equal(
    resolveClientRuntimeCache("/repo", {}),
    resolve("/repo", ".tmp", "client-cache"),
  );
});

test("accepts an absolute persistent Client runtime cache", () => {
  const cache = resolve("/runner/tool-cache/loomle-client-runtime");
  assert.equal(
    resolveClientRuntimeCache("/repo", {
      [CLIENT_RUNTIME_CACHE_ENV]: cache,
    }),
    cache,
  );
});

test("rejects an ambiguous relative Client runtime cache", () => {
  assert.throws(
    () => resolveClientRuntimeCache("/repo", {
      [CLIENT_RUNTIME_CACHE_ENV]: "runner-cache",
    }),
    /must be an absolute path/,
  );
});
