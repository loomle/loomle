#!/usr/bin/env node

import { readFile } from "node:fs/promises";
import { pathToFileURL } from "node:url";

export const PE_MACHINE_AMD64 = 0x8664;

export async function readPeMachine(path) {
  const bytes = await readFile(path);
  if (bytes.length < 0x40 || bytes.subarray(0, 2).toString("ascii") !== "MZ") {
    throw new Error(`not a PE image: ${path}`);
  }
  const peOffset = bytes.readUInt32LE(0x3c);
  if (peOffset + 6 > bytes.length
      || bytes.subarray(peOffset, peOffset + 4).toString("hex") !== "50450000") {
    throw new Error(`invalid PE header: ${path}`);
  }
  return bytes.readUInt16LE(peOffset + 4);
}

export async function assertPeAmd64(path) {
  const machine = await readPeMachine(path);
  if (machine !== PE_MACHINE_AMD64) {
    throw new Error(
      `expected PE AMD64 machine 0x8664, found 0x${machine.toString(16)}: ${path}`,
    );
  }
}

async function main(args) {
  if (args.length === 0) {
    throw new Error("Usage: node packaging/tools/pe-machine.mjs <file> [...]");
  }
  for (const path of args) {
    await assertPeAmd64(path);
    console.log(`PE AMD64: ${path}`);
  }
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  main(process.argv.slice(2)).catch((error) => {
    console.error(error.message);
    process.exitCode = 1;
  });
}
