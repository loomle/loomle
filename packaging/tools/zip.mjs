import { readFileSync } from "node:fs";
import { mkdir, readFile, readdir, writeFile } from "node:fs/promises";
import { dirname, join, relative, resolve, sep } from "node:path";

import { unzipSync, zipSync } from "fflate";

const ZIP_EPOCH = new Date("1980-01-01T00:00:00.000Z");

export async function writeZipFromDirectory({
  sourceDirectory,
  destination,
  prefix = "",
}) {
  const source = resolve(sourceDirectory);
  const output = resolve(destination);
  const entries = {};
  await collectFiles(source, source, normalizeEntry(prefix), entries);
  if (Object.keys(entries).length === 0) {
    throw new Error(`ZIP source directory contains no files: ${source}.`);
  }
  await mkdir(dirname(output), { recursive: true });
  await writeFile(output, zipSync(entries, { level: 9 }));
  return output;
}

export function readZipEntry(archive, entry) {
  const normalized = normalizeEntry(entry);
  if (!normalized) throw new Error("ZIP entry must be a non-empty relative path.");
  let entries;
  try {
    entries = unzipSync(new Uint8Array(readFileSync(resolve(archive))));
  } catch (error) {
    throw new Error(`Cannot read ZIP archive ${archive}: ${error.message}`);
  }
  const value = entries[normalized];
  if (!value) throw new Error(`ZIP archive ${archive} does not contain ${normalized}.`);
  return Buffer.from(value);
}

export function readZipText(archive, entry) {
  return readZipEntry(archive, entry).toString("utf8");
}

async function collectFiles(root, directory, prefix, entries) {
  const children = await readdir(directory, { withFileTypes: true });
  children.sort((left, right) => left.name.localeCompare(right.name, "en"));
  for (const child of children) {
    const path = join(directory, child.name);
    if (child.isDirectory()) {
      await collectFiles(root, path, prefix, entries);
      continue;
    }
    if (!child.isFile()) {
      throw new Error(`ZIP source contains an unsupported entry: ${path}.`);
    }
    const local = normalizeEntry(relative(root, path));
    const name = prefix ? `${prefix}/${local}` : local;
    entries[name] = [new Uint8Array(await readFile(path)), { mtime: ZIP_EPOCH }];
  }
}

function normalizeEntry(value) {
  const normalized = value.split(sep).join("/").replace(/^\.\//, "").replace(/\/$/, "");
  if (normalized === "") return "";
  if (normalized.startsWith("/") || normalized.split("/").includes("..")) {
    throw new Error(`ZIP entry must remain relative: ${JSON.stringify(value)}.`);
  }
  return normalized;
}
