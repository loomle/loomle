import assert from "node:assert/strict";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";

import {
  PE_MACHINE_AMD64,
  assertPeAmd64,
  readPeMachine,
} from "./pe-machine.mjs";

test("reads and accepts a PE AMD64 machine header", async () => {
  const fixture = await createPe(PE_MACHINE_AMD64);
  try {
    assert.equal(await readPeMachine(fixture.path), PE_MACHINE_AMD64);
    await assertPeAmd64(fixture.path);
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects malformed and non-AMD64 images", async () => {
  const malformedRoot = await mkdtemp(join(tmpdir(), "loomle-pe-malformed-"));
  const malformedPath = join(malformedRoot, "invalid.exe");
  await writeFile(malformedPath, "not PE");
  const x86 = await createPe(0x014c);
  try {
    await assert.rejects(readPeMachine(malformedPath), /not a PE image/);
    await assert.rejects(assertPeAmd64(x86.path), /found 0x14c/);
  } finally {
    await rm(malformedRoot, { recursive: true, force: true });
    await rm(x86.root, { recursive: true, force: true });
  }
});

async function createPe(machine) {
  const root = await mkdtemp(join(tmpdir(), "loomle-pe-"));
  const path = join(root, "fixture.exe");
  const bytes = Buffer.alloc(256);
  bytes.write("MZ", 0, "ascii");
  bytes.writeUInt32LE(0x80, 0x3c);
  bytes.write("PE\0\0", 0x80, "ascii");
  bytes.writeUInt16LE(machine, 0x84);
  await writeFile(path, bytes);
  return { root, path };
}
